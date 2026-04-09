#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>

// Raw HID report snapshot — no ControllerConfig, no GamepadState.
// buttonMask: bit N set = HID button usage N+1 is pressed (up to 32 buttons).
// Axes normalised to [-1, 1] using logical min/max from the HID descriptor.
// hat: raw hat value; 0xFFFFFFFF = neutral / out of range.
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

// Lightweight HID reader for the binding wizard.
// Opens a device by path (from HIDScanner) and reads raw button/axis data
// without any controller config or GamepadState mapping.
class RawHIDReader {
public:
    explicit RawHIDReader(const std::string& devicePath);
    ~RawHIDReader();

    bool isOpen() const { return m_device != INVALID_HANDLE_VALUE; }

    // Reads one report. Returns false on disconnect.
    // On timeout (no new data in 20 ms) returns true with the previous state unchanged.
    bool read(RawHIDState& out);

private:
    HANDLE            m_device    = INVALID_HANDLE_VALUE;
    HANDLE            m_event     = nullptr;
    void*             m_preparsed = nullptr;
    ULONG             m_reportLen = 0;
    std::vector<BYTE> m_buf;
    BYTE              m_btnReportId = 0xFF;

    struct ValueRange { LONG logMin; LONG logMax; USHORT bitSize; };
    std::unordered_map<USHORT, ValueRange> m_valueCaps;
    std::unordered_map<USHORT, USHORT>     m_usagePage; // usage → actual page from descriptor

    float normalize(USHORT usage, ULONG raw) const;
};
