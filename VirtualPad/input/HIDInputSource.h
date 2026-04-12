#pragma once
#include "IInputSource.h"
#include "ControllerConfig.h"
#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>

// Reads a HID joystick/gamepad device using the HidP API.
// Axis mapping uses HID usage names in controllers.json:
//   Generic Desktop (page 0x01): "hid_x", "hid_y", "hid_z", "hid_rx", "hid_ry", "hid_rz"
//   Simulation Controls (page 0x02): "hid_brake", "hid_accel"
// Button indices are 1-based HID button usages (same scheme as WinMM).
// D-pad: set "dpad": "hid_hat" in the controller config.
class HIDInputSource : public IInputSource {
public:
    HIDInputSource(const std::string& devicePath, const ControllerConfig& config);
    ~HIDInputSource() override;

    bool        isConnected()         const override;
    bool        read(GamepadState& state) override;
    const char* getName()             const override { return m_name.c_str(); }
    DWORD       getLastButtonMask()   const override { return m_lastButtonMask; }
    void        setConfig(const ControllerConfig& cfg) override { m_config = cfg; }
    GamepadState getPhysicalState()   const override { return m_physicalState; }
    std::vector<std::string> getActiveAxisActions() const override { return m_activeAxisActions; }

private:
    HANDLE           m_device         = INVALID_HANDLE_VALUE;
    HANDLE           m_event          = nullptr;
    void*            m_preparsed      = nullptr;  // PHIDP_PREPARSED_DATA (opaque)
    ULONG            m_inputReportLen = 0;
    ControllerConfig m_config;
    std::string      m_name;
    std::vector<BYTE> m_reportBuf;
    bool             m_connected      = false;
    DWORD            m_lastButtonMask = 0;
    int              m_readCount      = 0;
    int              m_btnErrCount    = 0;
    BYTE             m_buttonReportId = 0xFF; // report ID in descriptor for buttons (0xFF = unknown)
    float            m_lastTouchX      = 0.0f;
    float            m_lastTouchY      = 0.0f;
    bool             m_lastTouchActive = false;
    GamepadState             m_physicalState;      // physical display state (pre-remapping)
    std::vector<std::string> m_activeAxisActions;  // axis_action keys active this frame (for PadEngine edge detection)

    struct ValueRange { LONG logMin; LONG logMax; USHORT bitSize; };
    std::unordered_map<USHORT, ValueRange> m_valueCaps; // HID usage → logical range
    std::unordered_map<USHORT, USHORT>     m_usagePage; // HID usage → actual page from descriptor

    struct AxisUsage { USHORT page; USHORT usage; };
    static AxisUsage usageFromAxisName(const std::string& name);
    float            normalizeHIDAxis(USHORT usage, ULONG rawValue) const;
    static void   parseHIDDpad(ULONG hatValue, bool& up, bool& down, bool& left, bool& right);
    void          applyButtons (PCHAR buf, ULONG bufLen,    GamepadState& state);
    void          applyAxes    (PCHAR buf, ULONG bufLen,    GamepadState& state);
    void          applyTouchpad(PCHAR buf, ULONG bytesRead, GamepadState& state);
    void          applyIMU     (PCHAR buf, ULONG bytesRead, GamepadState& state);
};
