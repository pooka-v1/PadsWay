#include "BluetoothLink.h"
#include <setupapi.h>
#include <BluetoothAPIs.h>
#include <cctype>
#include <cstdlib>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "Bthprops.lib")

namespace BluetoothLink {

// BTHENUM\{...}\7&xxx&x&AABBCCDDEEFF_C00000000  ->  "AABBCCDDEEFF" (12 hex chars before the last '_').
std::string addressOfBtNode(DEVINST devInst) {
    DEVINST parentInst = 0;
    if (CM_Get_Parent(&parentInst, devInst, 0) != CR_SUCCESS) return {};
    char parentId[512] = {};
    if (CM_Get_Device_IDA(parentInst, parentId, sizeof(parentId), 0) != CR_SUCCESS) return {};

    std::string idStr = parentId;
    for (auto& c : idStr) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    auto lastUs = idStr.rfind('_');
    if (lastUs == std::string::npos || lastUs < 12) return {};
    std::string mac = idStr.substr(lastUs - 12, 12);
    for (char c : mac) if (!isxdigit(static_cast<unsigned char>(c))) return {};
    return mac;
}

std::string addressOfInterfacePath(const std::string& interfacePath) {
    // A throwaway device info set holding just this interface: it maps the interface path back to
    // its device node (DevInst), whose parent carries the address.
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE) return {};

    std::string mac;
    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(ifaceData);
    if (SetupDiOpenDeviceInterfaceA(set, interfacePath.c_str(), 0, &ifaceData)) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &ifaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize != 0) {
            std::vector<BYTE> detailBuf(requiredSize);
            auto* detail   = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(detailBuf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
            SP_DEVINFO_DATA devInfoData = {};
            devInfoData.cbSize = sizeof(devInfoData);
            if (SetupDiGetDeviceInterfaceDetailA(set, &ifaceData, detail, requiredSize, nullptr, &devInfoData))
                mac = addressOfBtNode(devInfoData.DevInst);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return mac;
}

DeviceState queryDevice(const std::string& address) {
    DeviceState state;
    if (address.size() != 12) return state;

    // "AABBCCDDEEFF" -> rgBytes[5]=0xAA ... rgBytes[0]=0xFF (LSB first)
    BLUETOOTH_ADDRESS btAddr = {};
    for (int b = 0; b < 6; ++b)
        btAddr.rgBytes[b] = static_cast<BYTE>(strtol(address.substr((5 - b) * 2, 2).c_str(), nullptr, 16));

    BLUETOOTH_DEVICE_INFO btInfo = {};
    btInfo.dwSize  = sizeof(btInfo);
    btInfo.Address = btAddr;
    if (BluetoothGetDeviceInfo(nullptr, &btInfo) != ERROR_SUCCESS) return state;

    state.known = true;
    // fConnected carries flags (0x20 seen with a live DS4 and Pro 2), not a clean TRUE: test
    // against zero only.
    state.connected = (btInfo.fConnected != 0);
    if (btInfo.szName[0]) {
        char narrow[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, btInfo.szName, -1, narrow, sizeof(narrow), nullptr, nullptr);
        state.name = narrow;
    }
    return state;
}

} // namespace BluetoothLink
