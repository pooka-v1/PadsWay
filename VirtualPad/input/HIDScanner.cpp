#include "HIDScanner.h"
#include <setupapi.h>
#include <hidsdi.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

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

        if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifaceData, detail, requiredSize, nullptr, nullptr))
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
            CloseHandle(h);
            continue;
        }

        std::string productName;
        wchar_t nameBuf[256] = {};
        if (HidD_GetProductString(h, nameBuf, sizeof(nameBuf))) {
            char narrow[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, nameBuf, -1, narrow, sizeof(narrow), nullptr, nullptr);
            productName = narrow;
        }

        CloseHandle(h);

        // Detect connection type from path.
        // USB HID paths look like: \\?\HID#VID_xxxx&PID_xxxx#...
        // BT HID paths use: BTHENUM, BLUETOOTHHIDDEVICE, BTH_HID,
        // or the Bluetooth HID service GUID {00001124-0000-1000-8000-00805f9b34fb}
        std::string pathUpper = path;
        for (auto& ch : pathUpper) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        std::string connType = (pathUpper.find("BTHENUM")               != std::string::npos ||
                                pathUpper.find("BLUETOOTHHIDDEVICE")    != std::string::npos ||
                                pathUpper.find("BTH_HID")               != std::string::npos ||
                                pathUpper.find("00001124-0000-1000-8000-00805F9B34FB") != std::string::npos)
                               ? "bt" : "usb";

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
