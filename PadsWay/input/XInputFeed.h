#pragma once
#include "RawHIDReader.h"   // RawHIDState
#include <windows.h>
#include <xinput.h>         // XINPUT_GAMEPAD — the type only, no XInput function is called
#include <string>

// Reads a pad that sits behind the xinputhid filter (X-mode over Bluetooth, e.g. 045E:02E0) through
// its XInput side instead of HID. xinputhid only hands its HID reports to the window in the
// foreground, so PadsWay goes deaf as soon as a game takes the focus; its XInput side has no focus
// policy. This is the only XInput use allowed in new code (CLAUDE.md) — see ARCHITECTURE.md,
// "Lectura por XInput".
//
// xinputhid adds an XUSB interface to the pad's HID device node, the same node our HID interface
// belongs to. XInput itself reads pads by sending IOCTLs to that interface; we send the same IOCTL
// to the interface of our own node. That ties the XInput state to the exact pad we opened over HID,
// where XInput slots can't: every pad behind xinputhid reports 045E:02E0, whatever its model.
//
// Not an input source of its own: it fills the same RawHIDState the HID path fills, and everything
// above it (mapping, wizard, Scanner) stays as it is. The layout copies what xinputhid gives the pad
// over HID, so the controllers.json entries captured over HID keep working:
//   buttons 1-10   A B X Y LB RB Back Start LS RS   (11 = Guide, which the HID view never shows)
//   hid_x / hid_y  left stick,  hid_rx / hid_ry  right stick   (Y grows downwards, as in HID)
//   hid_z          LT - RT, both triggers on one axis (what xinputhid exposes over HID)
//   hid_brake      LT, hid_accel  RT — separate, [-1, 1], as xinputhid also exposes them over HID
//   hat            d-pad (0 = up, clockwise; 0xFFFFFFFF = centred)
//   raw            0x00 (report ID slot) + the 12 bytes of XINPUT_GAMEPAD, for the Scanner's byte view
class XInputFeed {
public:
    XInputFeed(const std::wstring& xusbInterfacePath, const std::string& name);
    ~XInputFeed();
    XInputFeed(const XInputFeed&)            = delete;   // owns a device handle
    XInputFeed& operator=(const XInputFeed&) = delete;

    // Fills out with the pad's current state. False while the XUSB interface can't be read (not
    // open yet, or gone): out is left as it was.
    bool poll(RawHIDState& out);

private:
    std::wstring m_xusbPath;
    std::string  m_name;
    HANDLE       m_handle          = INVALID_HANDLE_VALUE;
    ULONGLONG    m_nextOpenMs      = 0;       // after a failure, retry the open only now and then
    bool         m_reportedFailure = false;   // a failure is logged once, not on every retry

    bool open();
    void close();
};

// Extracts the XINPUT_GAMEPAD from an answer to the XUSB "get gamepad state" IOCTL. False if the
// answer is too short or says the pad isn't connected. No device involved (testable).
bool xusbStateToGamepad(const BYTE* answer, DWORD size, XINPUT_GAMEPAD& pad);

// Writes one XInput pad state into out with the layout above. No device involved (testable).
void xinputGamepadToRawState(const XINPUT_GAMEPAD& pad, RawHIDState& out);

// HID read timeout for a pad read through XInput. The HID read no longer brings the data; it only
// paces the read loop and reports the unplug, so it waits much less than the usual 20 ms.
inline constexpr int kXInputPollMs = 4;

// Path of the XUSB interface on the device node of this HID interface, or empty if there is none.
// Only xinputhid adds one, so a non-empty path means the pad sits behind xinputhid. A fact of the
// driver stack Windows loaded, not of the mapping: it covers pads we don't know yet.
std::wstring findXusbInterface(const std::string& hidDevicePath);
