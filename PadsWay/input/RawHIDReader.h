#pragma once
#include "HIDDevice.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

// Axis slots of RawHIDState, in field order. Bit N of RawHIDState::axisMask = slot N present.
enum class RawAxis : uint8_t { X, Y, Z, Rx, Ry, Rz, Brake, Accel };

// Raw HID report snapshot — no ControllerConfig, no GamepadState. The canonical input every
// consumer reads from (mapping included): nothing downstream reads the vendor's bytes directly.
// buttonMask: bit N set = HID button usage N+1 is pressed (up to 32 buttons).
// Axes normalised to [-1, 1] using logical min/max from the HID descriptor.
// hat: raw hat value (relative to logMin); 0xFFFFFFFF = neutral / out of range.
struct RawHIDState {
    DWORD  buttonMask = 0;
    float  axisX     = 0.0f;   // HID usage 0x30 (Generic Desktop)
    float  axisY     = 0.0f;   // HID usage 0x31
    float  axisZ     = 0.0f;   // HID usage 0x32
    float  axisRx    = 0.0f;   // HID usage 0x33
    float  axisRy    = 0.0f;   // HID usage 0x34
    float  axisRz    = 0.0f;   // HID usage 0x35
    float  axisBrake = 0.0f;   // HID usage 0xC4 (Simulation page) — e.g. Pro 3 L2
    float  axisAccel = 0.0f;   // HID usage 0xC5 (Simulation page) — e.g. Pro 3 R2
    ULONG  hat    = 0xFFFFFFFF;
    bool   valid  = false;

    // What the LAST decoded report actually carried. A field this report didn't carry (usage not
    // in the descriptor, or not in this report ID) keeps its previous value with its flag clear,
    // so the mapping can skip it instead of acting on a stale or zero value.
    bool     buttonsValid = false;   // buttonMask was decoded from this report
    uint16_t axisMask     = 0;       // bit per RawAxis

    bool  hasAxis(RawAxis a) const { return (axisMask >> static_cast<int>(a)) & 1u; }
    float axis(RawAxis a) const {
        switch (a) {
        case RawAxis::X:     return axisX;
        case RawAxis::Y:     return axisY;
        case RawAxis::Z:     return axisZ;
        case RawAxis::Rx:    return axisRx;
        case RawAxis::Ry:    return axisRy;
        case RawAxis::Rz:    return axisRz;
        case RawAxis::Brake: return axisBrake;
        case RawAxis::Accel: return axisAccel;
        }
        return 0.0f;
    }

    // Full raw input report bytes for this frame (size = bytes actually read).
    // Used by the IMU calibration wizard to scan for undeclared sensor data beyond
    // the HID-declared usages above — gyro/accel are not exposed as HID axes.
    std::vector<uint8_t> raw;
};

// Decodes one already-read HID report (hid.reportBuf(), populated by a prior hid.read()) into
// out: buttons, the fixed set of Generic Desktop/Simulation axes, hat, and the full raw byte
// snapshot. Shared by RawHIDReader (below) and DeviceHub's HIDInputSource-backed connections
// (see input/DeviceHub.h) so the Scanner gets the same generic view regardless of who is driving
// the reads — config-independent, works even for a device with no controllers.json entry yet.
void decodeRawHIDReport(const HIDDevice& hid, RawHIDState& out);

class ControllerProtocol;   // ControllerProtocol.h includes this header (RawHIDState)

// Lightweight HID reader for the binding wizard.
// Opens a device by path (from HIDScanner) and reads raw button/axis data
// without any controller config or GamepadState mapping.
// Goes through the pad's controller protocol, picked by its VID/PID (there is no config yet —
// the wizard exists to create it), so it sees exactly what the engine will read: a DS4 over BT
// is activated and decoded in the USB layout, same as in HIDInputSource.
// Uses HIDDevice for I/O; handles are closed cleanly on disconnect.
class RawHIDReader {
public:
    explicit RawHIDReader(const std::string& devicePath, const std::string& name = "");
    ~RawHIDReader();   // defined in the .cpp: ControllerProtocol is incomplete here

    bool isOpen() const { return m_hid.isConnected(); }

    // Reads one report. Returns false on disconnect (device is closed cleanly).
    // On timeout (no new data within timeoutMs), or a report the protocol drops, returns true
    // with the previous state unchanged. The first call also activates the pad's full mode.
    // Use timeoutMs=0 from the render thread to avoid blocking.
    bool read(RawHIDState& out, int timeoutMs = 20);

private:
    HIDDevice                           m_hid;
    std::string                         m_name;
    // Built after m_hid (declaration order): it may hold a reference to it.
    std::unique_ptr<ControllerProtocol> m_protocol;
    bool                                m_fullModePending = true;
};
