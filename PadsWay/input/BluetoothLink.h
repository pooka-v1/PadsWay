#pragma once
#include <windows.h>
#include <cfgmgr32.h>
#include <string>

// What the Windows Bluetooth stack knows about a paired pad, by its BT address.
//
// Why it exists: Windows can keep the HID node of a BT pad that was switched off ("ghost" node:
// present, status OK, but no reports, and reads never fail). The BT stack still knows the link is
// gone, so this is the ground truth both the scanner (don't list it) and HIDDevice (an open pad
// that went silent) ask. Addresses are 12 uppercase hex chars, MSB first: "AABBCCDDEEFF".
namespace BluetoothLink {

struct DeviceState {
    bool        known     = false;   // the BT stack answered for this address
    bool        connected = false;   // link up right now (only meaningful when known)
    std::string name;                // name stored at pairing time (UTF-8), may be empty
};

// BT address of a BT HID node, from its BTHENUM parent's instance ID. Empty if not found.
std::string addressOfBtNode(DEVINST devInst);

// Same, starting from a HID device interface path (what HIDDevice is opened with).
std::string addressOfInterfacePath(const std::string& interfacePath);

// Asks the BT stack about a paired device. known == false if the address is malformed or the
// stack doesn't answer — callers must then treat the pad as they did before (assume alive).
DeviceState queryDevice(const std::string& address);

} // namespace BluetoothLink
