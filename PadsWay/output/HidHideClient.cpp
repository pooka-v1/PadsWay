#include "HidHideClient.h"
#include "../Log.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <algorithm>

static std::string toUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

// HidHide matches whitelist entries by NT device path (\Device\HarddiskVolumeN\...),
// NOT the Win32 drive-letter path (C:\...) that GetModuleFileNameW returns. We must
// translate the drive letter to its NT device name via QueryDosDevice, otherwise the
// running exe is never recognised as whitelisted and HidHide keeps hiding the physical
// pads from it — the engine still reads a handle opened before the device was hidden,
// but a fresh enumeration (Scanner / Wizard) sees nothing.
static std::wstring toNtDevicePath(const std::wstring& win32Path) {
    if (win32Path.size() < 2 || win32Path[1] != L':')
        return win32Path;                          // not a drive-letter path; leave as-is

    std::wstring drive = win32Path.substr(0, 2);   // e.g. "C:"  (no trailing backslash)
    WCHAR ntDevice[MAX_PATH] = {};
    if (QueryDosDeviceW(drive.c_str(), ntDevice, MAX_PATH) == 0) {
        spdlog::warn("[HidHide] QueryDosDevice failed for '{}' (error {})",
                     toUtf8(drive), GetLastError());
        return win32Path;                          // fallback: better than nothing
    }
    // "\Device\HarddiskVolumeN" + "\rest\of\path.exe"
    return std::wstring(ntDevice) + win32Path.substr(2);
}

// Leaf (file name) of a path, splitting on either separator. NT device paths use
// backslashes (\Device\HarddiskVolumeN\...\PadsWay.exe); we only need the last
// component to recognise our own (possibly stale) whitelist entries.
static std::wstring pathLeaf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

// True if a whitelist entry belongs to this app, regardless of which volume/folder
// it points at. Used to purge stale entries left by previous installs / dev builds
// before re-adding the current exe. "This app" is whatever exe is running the engine
// (PadsWay.exe normally, PadsWayE2E.exe for the E2E harness): each host purges only
// its own entries, so the harness never removes PadsWay's and vice versa.
static bool isOwnExe(const std::wstring& path, const std::wstring& runningExeLeaf) {
    return _wcsicmp(pathLeaf(path).c_str(), runningExeLeaf.c_str()) == 0;
}

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

// ---------------------------------------------------------------------------
// MULTI_SZ helpers
// A MULTI_SZ buffer is a sequence of null-terminated wide strings followed
// by an extra null terminator — the standard Windows multi-string format.
// ---------------------------------------------------------------------------

static std::vector<std::wstring> decodeMultiString(const std::vector<WCHAR>& buf) {
    std::vector<std::wstring> result;
    const WCHAR* p   = buf.data();
    const WCHAR* end = buf.data() + buf.size();
    while (p < end && *p != L'\0') {
        std::wstring s(p);
        result.push_back(s);
        p += s.size() + 1;
    }
    return result;
}

static std::vector<WCHAR> encodeMultiString(const std::vector<std::wstring>& strings) {
    std::vector<WCHAR> buf;
    for (const auto& s : strings) {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(L'\0');
    }
    buf.push_back(L'\0');   // final double-null terminator
    return buf;
}

// ---------------------------------------------------------------------------
// HidHideClient
// ---------------------------------------------------------------------------

HidHideClient::HidHideClient() {
    m_handle = CreateFileW(
        L"\\\\.\\HidHide",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (m_handle == INVALID_HANDLE_VALUE)
        spdlog::warn("[HidHide] Driver not found — hiding disabled (error {})", GetLastError());
    else
        spdlog::info("[HidHide] Driver found.");
}

HidHideClient::~HidHideClient() {
    unhideDevice();
    if (m_handle != INVALID_HANDLE_VALUE)
        CloseHandle(m_handle);
}

// ---------------------------------------------------------------------------
// IOCTL get/set
// ---------------------------------------------------------------------------

std::vector<std::wstring> HidHideClient::getList(DWORD ioctl) {
    // First call: null output buffer to query required size
    DWORD needed = 0;
    if (!DeviceIoControl(m_handle, ioctl, nullptr, 0, nullptr, 0, &needed, nullptr)) {
        DWORD err = GetLastError();
        spdlog::error("[HidHide] getList size query failed (error {})", err);
        return {};
    }
    if (needed == 0) return {};

    // Second call: actual data
    std::vector<WCHAR> buf(needed / sizeof(WCHAR));
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(m_handle, ioctl,
                         nullptr, 0,
                         buf.data(), needed,
                         &bytesReturned, nullptr)) {
        spdlog::error("[HidHide] getList data fetch failed (error {})", GetLastError());
        return {};
    }
    buf.resize(bytesReturned / sizeof(WCHAR));
    return decodeMultiString(buf);
}

void HidHideClient::setList(DWORD ioctl, const std::vector<std::wstring>& list) {
    auto  buf          = encodeMultiString(list);
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(m_handle, ioctl,
                         buf.data(), (DWORD)(buf.size() * sizeof(WCHAR)),
                         nullptr, 0,
                         &bytesReturned, nullptr))
        spdlog::error("[HidHide] setList IOCTL failed (error {})", GetLastError());
}

// ---------------------------------------------------------------------------
// Whitelist
// ---------------------------------------------------------------------------

void HidHideClient::addSelfToWhitelist() {
    if (!isAvailable()) return;

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path = toNtDevicePath(exePath);   // HidHide compares NT device paths

    // Drop every entry that belongs to this app and re-add the current path.
    // The NT device path is not stable (HarddiskVolumeN can change between boots)
    // and dev/installed builds live in different folders, so a plain "is my exact
    // path already here?" check let stale entries pile up — eventually none of them
    // matched the running exe, HidHide stopped recognising us as whitelisted, and
    // the physical pads were hidden from the engine (the ~0.5s input lag). Purging
    // our own stale entries on every start keeps exactly one, the live one.
    auto list = getList(kIoctlGetWhitelist);
    std::vector<std::wstring> kept;
    bool alreadyCurrent = false;
    int  purged = 0;
    const std::wstring runningExeLeaf = pathLeaf(path);
    for (const auto& s : list) {
        if (isOwnExe(s, runningExeLeaf)) {
            if (_wcsicmp(s.c_str(), path.c_str()) == 0) alreadyCurrent = true;
            else ++purged;
            continue;   // drop every own entry; the current one is re-added below
        }
        kept.push_back(s);   // foreign apps' entries are left untouched
    }

    if (alreadyCurrent && purged == 0) {
        spdlog::debug("[HidHide] Already in whitelist.");
        return;
    }

    kept.push_back(path);
    setList(kIoctlSetWhitelist, kept);
    if (purged > 0)
        spdlog::info("[HidHide] Purged {} stale whitelist entr{} for this app.",
                     purged, purged == 1 ? "y" : "ies");
    spdlog::info("[HidHide] Added to whitelist: {}", toUtf8(path));
}

// ---------------------------------------------------------------------------
// Active filter state
// ---------------------------------------------------------------------------

bool HidHideClient::isFilterActive() {
    BOOLEAN active = FALSE;
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(m_handle, kIoctlGetActive,
                         nullptr, 0,
                         &active, sizeof(active),
                         &bytesReturned, nullptr))
        spdlog::error("[HidHide] getActive failed (error {})", GetLastError());
    return active != FALSE;
}

void HidHideClient::setFilterActive(bool active) {
    BOOLEAN value = active ? TRUE : FALSE;
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(m_handle, kIoctlSetActive,
                         &value, sizeof(value),
                         nullptr, 0,
                         &bytesReturned, nullptr))
        spdlog::error("[HidHide] setActive failed (error {})", GetLastError());
    else
        spdlog::info("[HidHide] Filter {}.", active ? "enabled" : "disabled");
}

// ---------------------------------------------------------------------------
// Blacklist (device hiding)
// ---------------------------------------------------------------------------

// Device instance path (what the blacklist holds) of the device behind one HID interface path.
// By path, not by VID/PID: two pads can share a VID/PID (every pad behind xinputhid is 045E:02E0,
// whatever its model), and the first match would hide the wrong one.
static std::wstring instancePathFromInterface(const std::string& hidDevicePath) {
    const std::wstring path(hidDevicePath.begin(), hidDevicePath.end());   // interface paths are ASCII

    HDEVINFO devInfo = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (devInfo == INVALID_HANDLE_VALUE)
        return {};

    std::wstring result;
    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(ifaceData);
    SP_DEVINFO_DATA devInfoData = {};
    devInfoData.cbSize = sizeof(devInfoData);
    wchar_t instanceId[MAX_PATH] = {};
    // Opening the interface adds its device to the (empty) set, as element 0.
    if (SetupDiOpenDeviceInterfaceW(devInfo, path.c_str(), 0, &ifaceData)
            && SetupDiEnumDeviceInfo(devInfo, 0, &devInfoData)
            && SetupDiGetDeviceInstanceIdW(devInfo, &devInfoData, instanceId, MAX_PATH, nullptr))
        result = instanceId;

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

// True if a device node belongs to one of the Xbox-style classes whose XUSB interface XInput reads.
// HidHide filters both classes, so such a node can be hidden like a HID one.
static bool isXusbClassNode(DEVINST node) {
    wchar_t classGuid[64] = {};
    ULONG   size = sizeof(classGuid);
    if (CM_Get_DevNode_Registry_PropertyW(node, CM_DRP_CLASSGUID, nullptr, classGuid, &size, 0) != CR_SUCCESS)
        return false;
    return _wcsicmp(classGuid, L"{d61ca365-5af4-4486-998b-9db4734c6ca3}") == 0     // XnaComposite (xusb22)
        || _wcsicmp(classGuid, L"{05f5cfe2-4733-4950-a6bb-07aad01a3a84}") == 0;    // XboxComposite
}

// Instance path of the XUSB node a HID node hangs from, or empty if it has none. A pad on a wired
// Xbox-style driver (xusb22: Pro 3 X-mode receiver, Pro 2 X-mode USB) shows up as
//   USB\...&MI_00 (XnaComposite, xusb22)  ->  USB\...&IG_xx (HidUsb)  ->  HID\...&IG_xx (what we open)
// and XInput — Steam among others — reads the XUSB node, so hiding only the HID one is not enough.
// A few levels up covers that chain; pads without such a node (DualSense, D-mode, xinputhid over BT,
// whose XUSB interface is on the HID node itself) find none and stay as before.
static std::wstring xusbAncestorOf(const std::wstring& instanceId) {
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL)
            != CR_SUCCESS)
        return {};
    constexpr int kMaxLevels = 3;
    for (int level = 0; level < kMaxLevels; ++level) {
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) return {};
        node = parent;
        if (!isXusbClassNode(node)) continue;
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};
        return id;
    }
    return {};
}

void HidHideClient::hideDevice(const std::string& hidDevicePath) {
    if (!isAvailable()) return;

    const std::wstring hidInstance = instancePathFromInterface(hidDevicePath);
    if (hidInstance.empty()) {
        spdlog::warn("[HidHide] Instance path not found for {}", hidDevicePath);
        return;
    }
    m_hiddenInstancePaths = { hidInstance };
    const std::wstring xusbInstance = xusbAncestorOf(hidInstance);
    if (!xusbInstance.empty())
        m_hiddenInstancePaths.push_back(xusbInstance);

    auto list    = getList(kIoctlGetBlacklist);
    bool changed = false;
    for (const auto& id : m_hiddenInstancePaths) {
        const bool listed = std::any_of(list.begin(), list.end(), [&](const std::wstring& s) {
            return _wcsicmp(s.c_str(), id.c_str()) == 0;
        });
        if (listed) {
            spdlog::debug("[HidHide] Device already in blacklist: {}", toUtf8(id));
            continue;
        }
        list.push_back(id);
        changed = true;
        spdlog::info("[HidHide] Device hidden: {}", toUtf8(id));
    }
    if (!changed) return;
    setList(kIoctlSetBlacklist, list);

    if (!isFilterActive()) {
        setFilterActive(true);
        m_weActivated = true;
    }
}

void HidHideClient::unhideDevice() {
    if (!isAvailable() || m_hiddenInstancePaths.empty()) return;

    auto list = getList(kIoctlGetBlacklist);
    const size_t before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const std::wstring& s) {
        return std::any_of(m_hiddenInstancePaths.begin(), m_hiddenInstancePaths.end(),
            [&](const std::wstring& id) { return _wcsicmp(s.c_str(), id.c_str()) == 0; });
    }), list.end());

    if (list.size() == before) {
        m_hiddenInstancePaths.clear();
        return;
    }

    setList(kIoctlSetBlacklist, list);
    for (const auto& id : m_hiddenInstancePaths)
        spdlog::info("[HidHide] Device unhidden: {}", toUtf8(id));
    m_hiddenInstancePaths.clear();

    if (m_weActivated) {
        setFilterActive(false);
        m_weActivated = false;
    }
}
