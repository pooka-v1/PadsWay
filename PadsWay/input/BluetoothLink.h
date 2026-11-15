#pragma once
#include "HIDScanner.h"   // HIDScanner::DeviceInfo
#include <windows.h>
#include <cfgmgr32.h>
#include <string>
#include <string_view>
#include <vector>

// What the Windows Bluetooth stack knows about a paired pad, by its BT address.
//
// Why it exists: Windows can keep the HID node of a BT pad that was switched off ("ghost" node:
// present, status OK, but no reports, and reads never fail). The BT stack still knows the link is
// gone, so this is the ground truth both the scanner (don't list it) and HIDDevice (an open pad
// that went silent) ask. Addresses are 12 uppercase hex chars, MSB first: "AABBCCDDEEFF".
//
// The rules (address parsing, when to ask, how to read the answer, BT/USB twins) are pure functions
// so they can be unit-tested; only the queries themselves talk to Windows.
namespace BluetoothLink {

struct DeviceState {
    bool        known     = false;   // the BT stack answered for this address
    bool        connected = false;   // link up right now (only meaningful when known)
    std::string name;                // name stored at pairing time (UTF-8), may be empty
};

// A live Sony pad reports every few ms over BT, so 2 s without reports is already abnormal; the
// link check then repeats at most once per second (BluetoothGetDeviceInfo is a system call, and
// on-change-only pads keep a silent pad's read loop asking while idle).
constexpr ULONGLONG kSilenceBeforeLinkCheckMs = 2000;
constexpr ULONGLONG kLinkCheckIntervalMs      = 1000;

// --- Pure rules -------------------------------------------------------------------------------

// BT address from a BTHENUM node's instance ID: the 12 hex chars before the last '_', uppercased.
//   BTHENUM\{...}_VID&0002054c_PID&09cc\7&1e9fadce&0&8430958169ac_C00000000  ->  "8430958169AC"
// Empty if there is no such run of 12 hex chars.
std::string addressFromInstanceId(std::string_view instanceId);

// Only a definite "link down" counts: if the stack didn't answer (known == false), the pad is
// assumed alive — dropping a working pad over a failed query is worse than missing a ghost.
bool isLinkDown(const DeviceState& state);

// Whether an open pad, silent since lastReportMs, should ask the BT stack now (all GetTickCount64).
bool isLinkCheckDue(ULONGLONG nowMs, ULONGLONG lastReportMs, ULONGLONG lastCheckMs);

// One pad over BT and USB at once (Sony pads keep their BT link with the cable in): drops the USB
// node, keeps the BT one. Matched by address, never by VID/PID, so a second pad of the same model
// on the cable still shows. USB nodes without an address are always kept.
void dropUsbTwinsOfBtNodes(std::vector<HIDScanner::DeviceInfo>& devices);

// --- Queries (Windows) ------------------------------------------------------------------------

// BT address of a BT HID node, from its BTHENUM parent's instance ID. Empty if not found.
std::string addressOfBtNode(DEVINST devInst);

// Same, starting from a HID device interface path (what HIDDevice is opened with).
std::string addressOfInterfacePath(const std::string& interfacePath);

// Asks the BT stack about a paired device. known == false if the address is malformed or the
// stack doesn't answer.
DeviceState queryDevice(const std::string& address);

} // namespace BluetoothLink
