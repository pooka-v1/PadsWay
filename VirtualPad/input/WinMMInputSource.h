#pragma once

#include <windows.h>
#include <mmsystem.h>
#include "IInputSource.h"
#include "ControllerConfig.h"

// Reads a controller in D-mode (DirectInput/WinMM) using a runtime config
// that defines which WinMM buttons/axes map to which virtual gamepad outputs.
class WinMMInputSource : public IInputSource {
public:
    WinMMInputSource(UINT joyId, const ControllerConfig& config);

    bool        isConnected()       const override;
    bool        read(GamepadState& state) override;
    const char* getName()           const override { return m_config.source_name.c_str(); }
    DWORD       getLastButtonMask() const override { return m_lastButtonMask; }
    void        setConfig(const ControllerConfig& cfg) override { m_config = cfg; }
    GamepadState getPhysicalState() const override { return m_physicalState; }
    std::vector<std::string> getActiveAxisActions() const override { return m_activeAxisActions; }
    const std::unordered_map<std::string, ButtonAction>& getActiveAxisRangeActions() const override { return m_activeAxisRangeActions; }
    void        setPhysicalController(const PhysicalController& ctrl) override {
        m_physicalController    = ctrl;
        m_hasPhysicalController = true;
    }

private:
    UINT             m_joyId;
    ControllerConfig m_config;
    DWORD            m_lastButtonMask = 0;
    std::vector<std::string> m_activeAxisActions;
    std::unordered_map<std::string, ButtonAction> m_activeAxisRangeActions;
    GamepadState       m_physicalState;
    PhysicalController m_physicalController;
    bool               m_hasPhysicalController = false;

    static DWORD getAxisValue(const JOYINFOEX& info, const std::string& source);
    static float normalizeAxis(DWORD value);
    static float normalizeTrigger(DWORD value);
    static void  parsePOV(DWORD pov, bool& up, bool& down, bool& left, bool& right);

    // Component-system path
    void buildPhysicalButtons(const JOYINFOEX& info);
    void buildPhysicalAxes   (const JOYINFOEX& info);
    void applyAxesResidual   (const JOYINFOEX& info, GamepadState& state);

protected:
    bool processJoyInfo(const JOYINFOEX& info, GamepadState& state);
};
