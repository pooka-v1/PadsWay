#include "HIDScanner.h"
#include "BluetoothLink.h"
#include "HidChannel.h"   // hidTransportFromPath
#include "ControllerProtocolRegistry.h"   // protocolForHardware
#include "../Log.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace {

// Sony pads keep their BT link when a USB cable goes in, so Windows shows two nodes of one pad.
// Over USB the pad only tells its BT address through its pairing-info feature report: bytes [1..6],
// LSB first (same layout Linux hid-playstation reads).
constexpr BYTE  kDs4PairingFeatureId       = 0x12;
constexpr BYTE  kDualSensePairingFeatureId = 0x09;
constexpr USHORT kPairingFeatureMinLen     = 7;   // report ID + 6 address bytes

// BT address of a Sony pad's USB node, read live from its pairing-info feature report (reading
// needs no write access). Empty for any other pad, or if the pad doesn't answer.
std::string btAddressOfSonyUsbNode(HANDLE h, USHORT vid, USHORT pid, USHORT featureLen) {
    const std::string_view protocol = protocolForHardware(vid, pid);
    BYTE reportId = 0;
    if      (protocol == kDualShock4Protocol) reportId = kDs4PairingFeatureId;
    else if (protocol == kDualSenseProtocol)  reportId = kDualSensePairingFeatureId;
    else return {};
    if (featureLen < kPairingFeatureMinLen) return {};

    // HidD_GetFeature wants the descriptor's full feature length, not the report's own.
    std::vector<BYTE> buf(featureLen, 0);
    buf[0] = reportId;
    if (!HidD_GetFeature(h, buf.data(), featureLen)) return {};

    const BYTE* lsbFirst = buf.data() + 1;
    if (std::all_of(lsbFirst, lsbFirst + 6, [](BYTE b) { return b == 0; })) return {};
    char mac[13] = {};
    for (int b = 0; b < 6; ++b)
        snprintf(mac + b * 2, 3, "%02X", lsbFirst[5 - b]);
    return mac;
}

} // namespace

std::vector<HIDScanner::DeviceInfo> HIDScanner::scan() {
    std::vector<DeviceInfo> result;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevs(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return result;

    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(ifaceData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifaceData); ++i) {
        // Get required buffer size for the detail struct
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0) continue;

        std::vector<BYTE> detailBuf(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(devInfoData);
        if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifaceData, detail, requiredSize, nullptr, &devInfoData))
            continue;

        std::string path = detail->DevicePath;

        // Open shared — many HID devices deny exclusive access
        HANDLE h = CreateFileA(path.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attribs = {};
        attribs.Size = sizeof(attribs);
        if (!HidD_GetAttributes(h, &attribs)) { CloseHandle(h); continue; }

        // Skip ViGEm virtual controllers (VID 0x5650). Orphaned instances from
        // crashed sessions can return malformed preparsed data that corrupts the heap.
        if (attribs.VendorID == 0x5650) { CloseHandle(h); continue; }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(h, &preparsed)) { CloseHandle(h); continue; }

        HIDP_CAPS caps = {};
        NTSTATUS status = HidP_GetCaps(preparsed, &caps);
        HidD_FreePreparsedData(preparsed);

        // Only joysticks (0x04) and gamepads (0x05) on the Generic Desktop page (0x01)
        if (status != HIDP_STATUS_SUCCESS
            || caps.UsagePage != 0x01
            || (caps.Usage != 0x04 && caps.Usage != 0x05)) {
            if (spdlog::should_log(spdlog::level::debug)) {
                wchar_t nb[256] = {};
                char filteredName[512] = {};
                if (HidD_GetProductString(h, nb, sizeof(nb)))
                    WideCharToMultiByte(CP_UTF8, 0, nb, -1, filteredName, sizeof(filteredName), nullptr, nullptr);
                spdlog::debug("[HIDScanner] Skipped (not gamepad): VID={:04X} PID={:04X} "
                              "UsagePage={:04X} Usage={:04X} name='{}'",
                              attribs.VendorID, attribs.ProductID,
                              caps.UsagePage, caps.Usage, filteredName);
            }
            CloseHandle(h);
            continue;
        }

        // Detect BT now so we can choose the right name source below.
        bool isBt = (hidTransportFromPath(path) == HidTransport::Bluetooth);

        // For BT devices the HID interface node has a generic class-driver name.
        // The real name is the one Windows stored at pairing time: we take the pad's BT address
        // from the BTHENUM parent node and ask BluetoothGetDeviceInfo for it.
        std::string productName;
        std::string btAddress;
        bool btLinkDown = false;
        if (isBt) {
            btAddress = BluetoothLink::addressOfBtNode(devInfoData.DevInst);
            if (!btAddress.empty()) {
                // A pad switched off can leave a ghost HID node behind (see BluetoothLink.h).
                const BluetoothLink::DeviceState btState = BluetoothLink::queryDevice(btAddress);
                btLinkDown  = BluetoothLink::isLinkDown(btState);
                productName = btState.name;
            }
        } else {
            btAddress = btAddressOfSonyUsbNode(h, attribs.VendorID, attribs.ProductID,
                                               caps.FeatureReportByteLength);
        }
        // Fallback (USB, or BT lookup failed): HidD_GetProductString.
        if (productName.empty()) {
            wchar_t nameBuf[256] = {};
            if (HidD_GetProductString(h, nameBuf, sizeof(nameBuf))) {
                char narrow[512] = {};
                WideCharToMultiByte(CP_UTF8, 0, nameBuf, -1, narrow, sizeof(narrow), nullptr, nullptr);
                productName = narrow;
            }
        }

        CloseHandle(h);

        // Ghost BT node: listing it would offer a dead pad, and the BT/USB dedupe below would hide
        // the live USB node of the same pad behind it.
        if (btLinkDown) {
            spdlog::debug("[HIDScanner] Skipped BT node with link down (ghost): VID={:04X} PID={:04X} addr={}",
                          attribs.VendorID, attribs.ProductID, btAddress);
            continue;
        }

        std::string connType = isBt ? "bt" : "usb";

        DeviceInfo dev;
        dev.vid            = attribs.VendorID;
        dev.pid            = attribs.ProductID;
        dev.usagePage      = caps.UsagePage;
        dev.usage          = caps.Usage;
        dev.path           = std::move(path);
        dev.productName    = std::move(productName);
        dev.connectionType = std::move(connType);
        dev.btAddress      = std::move(btAddress);
        result.push_back(std::move(dev));
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    // One pad over BT and USB at once: keep only the BT node — the pad keeps sending over BT, and
    // pulling the cable then changes nothing.
    BluetoothLink::dropUsbTwinsOfBtNodes(result);

    return result;
}
