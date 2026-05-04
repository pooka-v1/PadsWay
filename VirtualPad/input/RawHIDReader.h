#pragma once
#include "HIDDevice.h"
#include <string>

// Raw HID report snapshot — no ControllerConfig, no GamepadState.
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
};

// Lightweight HID reader for the binding wizard and Scanner.
// Opens a device by path (from HIDScanner) and reads raw button/axis data
// without any controller config or GamepadState mapping.
// Uses HIDDevice for I/O; handles are closed cleanly on disconnect.
class RawHIDReader {
public:
    explicit RawHIDReader(const std::string& devicePath, const std::string& name = "");
    ~RawHIDReader() = default;

    bool isOpen() const { return m_hid.isConnected(); }

    // Reads one report. Returns false on disconnect (device is closed cleanly).
    // On timeout (no new data within timeoutMs) returns true with the previous state unchanged.
    // Use timeoutMs=0 from the render thread to avoid blocking.
    bool read(RawHIDState& out, int timeoutMs = 20);

private:
    HIDDevice m_hid;
};
