#include "HIDScanner.h"
#include "../Log.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <cfgmgr32.h>
#include <BluetoothAPIs.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "Bthprops.lib")

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
        std::string pathUpper = path;
        for (auto& ch : pathUpper) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        bool isBt = (pathUpper.find("BTHENUM")               != std::string::npos ||
                     pathUpper.find("BLUETOOTHHIDDEVICE")    != std::string::npos ||
                     pathUpper.find("BTH_HID")               != std::string::npos ||
                     pathUpper.find("00001124-0000-1000-8000-00805F9B34FB") != std::string::npos);

        // For BT devices the HID interface node has a generic class-driver name.
        // The real manufacturer name is in the BTHENUM parent node's instance ID:
        //   BTHENUM\{...}\7&xxx&x&AABBCCDDEEFF_00000001
        // We extract the BT MAC (12 hex chars before the last underscore) and use
        // BluetoothGetDeviceInfo to read the name Windows stored at pairing time.
        std::string productName;
        if (isBt) {
            DEVINST parentInst = 0;
            if (CM_Get_Parent(&parentInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                char parentId[512] = {};
                if (CM_Get_Device_IDA(parentInst, parentId, sizeof(parentId), 0) == CR_SUCCESS) {
                    // Find the 12-char hex BT address before the last '_' in the instance ID
                    std::string idStr = parentId;
                    for (auto& c : idStr) c = (char)toupper((unsigned char)c);
                    auto lastUs = idStr.rfind('_');
                    if (lastUs != std::string::npos && lastUs >= 12) {
                        std::string mac = idStr.substr(lastUs - 12, 12);
                        bool valid = true;
                        for (char c : mac) if (!isxdigit((unsigned char)c)) { valid = false; break; }
                        if (valid) {
                            // Build BLUETOOTH_ADDRESS: MAC string "AABBCCDDEEFF"
                            // → rgBytes[5]=0xAA … rgBytes[0]=0xFF (LSB-first)
                            BLUETOOTH_ADDRESS btAddr = {};
                            for (int b = 0; b < 6; ++b)
                                btAddr.rgBytes[b] = (BYTE)strtol(mac.substr((5 - b) * 2, 2).c_str(), nullptr, 16);
                            BLUETOOTH_DEVICE_INFO btInfo = {};
                            btInfo.dwSize  = sizeof(btInfo);
                            btInfo.Address = btAddr;
                            if (BluetoothGetDeviceInfo(nullptr, &btInfo) == ERROR_SUCCESS
                                    && btInfo.szName[0]) {
                                char narrow[256] = {};
                                WideCharToMultiByte(CP_UTF8, 0, btInfo.szName, -1,
                                                    narrow, sizeof(narrow), nullptr, nullptr);
                                productName = narrow;
                            }
                        }
                    }
                }
            }
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

        std::string connType = isBt ? "bt" : "usb";

        DeviceInfo dev;
        dev.vid            = attribs.VendorID;
        dev.pid            = attribs.ProductID;
        dev.usagePage      = caps.UsagePage;
        dev.usage          = caps.Usage;
        dev.path           = std::move(path);
        dev.productName    = std::move(productName);
        dev.connectionType = std::move(connType);
        result.push_back(std::move(dev));
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}
