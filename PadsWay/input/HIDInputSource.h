#pragma once
#include "IInputSource.h"
#include "HIDDevice.h"
#include "ControllerConfig.h"
#include "ComponentTypes.h"
#include "RawHIDReader.h"
#include <string>
#include <unordered_map>
#include <atomic>

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
    DWORD       getLastRawHat()       const override { return m_lastRawHat.load(); }
    void        setConfig(const ControllerConfig& cfg) override { m_config = cfg; }
    GamepadState getPhysicalState()   const override { return m_physicalState; }
    // Generic HID decode (buttons/axes/hat/raw bytes), independent of controllers.json mapping.
    // Populated every read() alongside the mapped GamepadState above. Used by DeviceHub to serve
    // the Scanner tab, which needs to inspect a device whether or not it has a config entry yet.
    const RawHIDState& getLastRawSnapshot() const { return m_lastRawSnapshot; }
    std::vector<std::string> getActiveAxisActions() const override { return m_activeAxisActions; }
    const std::unordered_map<std::string, ButtonAction>& getActiveAxisRangeActions() const override { return m_activeAxisRangeActions; }
    std::vector<std::string> getActiveGyroActions() const override { return m_activeGyroActions; }
    const std::unordered_map<std::string, ButtonAction>& getActiveGyroRangeActions() const override { return m_activeGyroRangeActions; }
    std::vector<std::string> getActiveAccelActions() const override { return m_activeAccelActions; }
    const std::unordered_map<std::string, ButtonAction>& getActiveAccelRangeActions() const override { return m_activeAccelRangeActions; }
    void        setPhysicalController(const PhysicalController& ctrl) override {
        m_physicalController    = ctrl;
        m_hasPhysicalController = true;
    }

private:
    HIDDevice        m_hid;
    ControllerConfig m_config;
    std::string      m_name;
    DWORD            m_lastButtonMask = 0;
    std::atomic<DWORD> m_lastRawHat  { 0xFFFFFFFF };
    int              m_readCount      = 0;
    int              m_btnErrCount    = 0;
    float            m_lastTouchX      = 0.0f;
    float            m_lastTouchY      = 0.0f;
    bool             m_lastTouchActive = false;
    float            m_lastTouch2X      = 0.0f;
    float            m_lastTouch2Y      = 0.0f;
    bool             m_lastTouch2Active = false;
    // Gesture-threshold harness (measure, not classify — see ARCHITECTURE.md "Touchpad"): each
    // finger's touch-down position/time, captured on the rising edge, consumed on the falling
    // edge to log one summary line per session.
    float            m_touch1SessStartX  = 0.0f;
    float            m_touch1SessStartY  = 0.0f;
    ULONGLONG        m_touch1SessStartMs = 0;
    float            m_touch2SessStartX  = 0.0f;
    float            m_touch2SessStartY  = 0.0f;
    ULONGLONG        m_touch2SessStartMs = 0;
    GamepadState             m_physicalState;
    RawHIDState              m_lastRawSnapshot;
    std::vector<std::string> m_activeAxisActions;
    std::unordered_map<std::string, ButtonAction> m_activeAxisRangeActions;
    std::vector<std::string> m_activeGyroActions;
    std::unordered_map<std::string, ButtonAction> m_activeGyroRangeActions;
    std::vector<std::string> m_activeAccelActions;
    std::unordered_map<std::string, ButtonAction> m_activeAccelRangeActions;
    PhysicalController       m_physicalController;
    bool                     m_hasPhysicalController = false;

    struct AxisUsage { USHORT page; USHORT usage; };
    static AxisUsage usageFromAxisName(const std::string& name);
    static void   parseHIDDpad(ULONG hatValue, bool& up, bool& down, bool& left, bool& right);
    void          applyButtons (PCHAR buf, ULONG bufLen,    GamepadState& state);
    void          applyAxes    (PCHAR buf, ULONG bufLen,    GamepadState& state);
    void          applyTouchpad(PCHAR buf, ULONG bytesRead, GamepadState& state);
    // Logs one [TOUCH][sess] line for a finger's just-ended touch session — see the harness
    // comment near m_touch1SessStartX above. x0/y0/x1/y1 are normalized [0,1] touchpad coords.
    void          logTouchSession(int finger, float x0, float y0, float x1, float y1,
                                   ULONGLONG startMs) const;
    void          applyIMU     (PCHAR buf, ULONG bytesRead, GamepadState& state);
    void          applyImuActions();
    void          buildPhysicalButtons (PCHAR buf, ULONG bufLen);
    void          buildPhysicalAxes    (PCHAR buf, ULONG bufLen);
    void          applyAxesResidual    (PCHAR buf, ULONG bufLen, GamepadState& state);
};
