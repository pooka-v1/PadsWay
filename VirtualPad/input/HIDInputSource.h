#pragma once
#include "IInputSource.h"
#include "HIDDevice.h"
#include "ControllerConfig.h"
#include "ComponentTypes.h"
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
    std::vector<std::string> getActiveAxisActions() const override { return m_activeAxisActions; }
    const std::unordered_map<std::string, ButtonAction>& getActiveAxisRangeActions() const override { return m_activeAxisRangeActions; }
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
    GamepadState             m_physicalState;
    std::vector<std::string> m_activeAxisActions;
    std::unordered_map<std::string, ButtonAction> m_activeAxisRangeActions;
    PhysicalController       m_physicalController;
    bool                     m_hasPhysicalController = false;

    struct AxisUsage { USHORT page; USHORT usage; };
    static AxisUsage usageFromAxisName(const std::string& name);
    static void   parseHIDDpad(ULONG hatValue, bool& up, bool& down, bool& left, bool& right);
    void          applyButtons (PCHAR buf, ULONG bufLen,    GamepadState& state);
    void          applyAxes    (PCHAR buf, ULONG bufLen,    GamepadState& state);
    void          applyTouchpad(PCHAR buf, ULONG bytesRead, GamepadState& state);
    void          applyIMU     (PCHAR buf, ULONG bytesRead, GamepadState& state);
    void          buildPhysicalButtons (PCHAR buf, ULONG bufLen);
    void          buildPhysicalAxes    (PCHAR buf, ULONG bufLen);
    void          applyAxesResidual    (PCHAR buf, ULONG bufLen, GamepadState& state);
};
