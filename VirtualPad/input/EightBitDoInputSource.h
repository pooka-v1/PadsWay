#pragma once

#include <windows.h>
#include <mmsystem.h>
#include "IInputSource.h"
#include "ControllerConfig.h"

// Reads a controller in D-mode (DirectInput/WinMM) using a runtime config
// that defines which WinMM buttons/axes map to which virtual gamepad outputs.
class EightBitDoInputSource : public IInputSource {
public:
    EightBitDoInputSource(UINT joyId, const ControllerConfig& config);

    bool        isConnected()       const override;
    bool        read(GamepadState& state) override;
    const char* getName()           const override { return m_config.source_name.c_str(); }
    DWORD       getLastButtonMask() const override { return m_lastButtonMask; }

private:
    UINT             m_joyId;
    ControllerConfig m_config;
    DWORD            m_lastButtonMask = 0;

    static DWORD getAxisValue(const JOYINFOEX& info, const std::string& source);
    static float normalizeAxis(DWORD value);
    static float normalizeTrigger(DWORD value);
    static void  parsePOV(DWORD pov, bool& up, bool& down, bool& left, bool& right);
};
