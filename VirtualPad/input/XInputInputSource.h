#pragma once

#include <windows.h>
#include <xinput.h>
#include "WinMMInputSource.h"

// Reads a controller via XInputGetState() (mode="xinput").
// Builds a synthetic JOYINFOEX from XInput data so the full WinMM-based
// mapping logic in WinMMInputSource can be reused unchanged.
// Fixes BUG-WINMM-XSLOT: XInput multi-slot devices create multiple WinMM
// bridge entries; reading via XInput directly bypasses that issue entirely.
class XInputInputSource : public WinMMInputSource {
public:
    XInputInputSource(UINT xInputSlot, const ControllerConfig& config);

    bool isConnected() const override;
    bool read(GamepadState& state)  override;

private:
    mutable UINT m_xInputSlot;  // cached active slot; updated on each scan

    UINT        findActiveSlot() const;
    static JOYINFOEX buildFakeJoyInfo(const XINPUT_GAMEPAD& gp);
};
