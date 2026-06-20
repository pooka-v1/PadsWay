#include "HidHideClient.h"
#include "../Log.h"

#include <setupapi.h>
#include <hidsdi.h>
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

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

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

    auto list = getList(kIoctlGetWhitelist);
    for (const auto& s : list)
        if (_wcsicmp(s.c_str(), path.c_str()) == 0) {
            spdlog::debug("[HidHide] Already in whitelist.");
            return;
        }

    list.push_back(path);
    setList(kIoctlSetWhitelist, list);
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

std::wstring HidHideClient::findInstancePath(uint16_t vid, uint16_t pid) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return {};

    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(ifaceData);

    std::wstring result;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifaceData); ++i) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &requiredSize, nullptr);
        if (!requiredSize) continue;

        std::vector<BYTE> detailBuf(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(devInfoData);

        if (!SetupDiGetDeviceInterfaceDetailW(
                devInfo, &ifaceData, detail, requiredSize, nullptr, &devInfoData))
            continue;

        // Open briefly to check VID/PID
        HANDLE h = CreateFileW(detail->DevicePath,
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attribs = {};
        attribs.Size = sizeof(attribs);
        bool match = HidD_GetAttributes(h, &attribs)
                  && attribs.VendorID  == vid
                  && attribs.ProductID == pid;
        CloseHandle(h);

        if (!match) continue;

        wchar_t instanceId[MAX_PATH] = {};
        if (SetupDiGetDeviceInstanceIdW(devInfo, &devInfoData, instanceId, MAX_PATH, nullptr)) {
            result = instanceId;
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

void HidHideClient::hideDevice(uint16_t vid, uint16_t pid) {
    if (!isAvailable()) return;

    m_hiddenInstancePath = findInstancePath(vid, pid);
    if (m_hiddenInstancePath.empty()) {
        spdlog::warn("[HidHide] Instance path not found for VID:{:04X} PID:{:04X}", vid, pid);
        return;
    }

    auto list = getList(kIoctlGetBlacklist);
    for (const auto& s : list)
        if (_wcsicmp(s.c_str(), m_hiddenInstancePath.c_str()) == 0) {
            spdlog::debug("[HidHide] Device already in blacklist: {}", toUtf8(m_hiddenInstancePath));
            return;
        }

    list.push_back(m_hiddenInstancePath);
    setList(kIoctlSetBlacklist, list);
    spdlog::info("[HidHide] Device hidden: {}", toUtf8(m_hiddenInstancePath));

    if (!isFilterActive()) {
        setFilterActive(true);
        m_weActivated = true;
    }
}

void HidHideClient::unhideDevice() {
    if (!isAvailable() || m_hiddenInstancePath.empty()) return;

    auto list = getList(kIoctlGetBlacklist);
    auto it   = std::find_if(list.begin(), list.end(), [&](const std::wstring& s) {
        return _wcsicmp(s.c_str(), m_hiddenInstancePath.c_str()) == 0;
    });

    if (it == list.end()) {
        m_hiddenInstancePath.clear();
        return;
    }

    list.erase(it);
    setList(kIoctlSetBlacklist, list);
    spdlog::info("[HidHide] Device unhidden: {}", toUtf8(m_hiddenInstancePath));
    m_hiddenInstancePath.clear();

    if (m_weActivated) {
        setFilterActive(false);
        m_weActivated = false;
    }
}
