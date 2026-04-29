#include "XInputInputSource.h"
#include <algorithm>

#pragma comment(lib, "XInput.lib")

XInputInputSource::XInputInputSource(UINT xInputSlot, const ControllerConfig& config)
    : WinMMInputSource(0, config), m_xInputSlot(xInputSlot) {}

// Scan all XInput slots and return the index of the first connected one,
// or XUSER_MAX_COUNT if none found. Caches result in m_xInputSlot.
// Returns true if the WinMM bridge for this slot belongs to our ViGEm virtual pad (VID=0x5650).
static bool isViGEmSlot(UINT slot) {
    JOYCAPS caps = {};
    return joyGetDevCaps(slot, &caps, sizeof(caps)) == JOYERR_NOERROR && caps.wMid == 0x5650;
}

UINT XInputInputSource::findActiveSlot() const {
    XINPUT_STATE s = {};
    // Try cached slot first; accept it only if it is not the ViGEm virtual pad.
    if (XInputGetState(m_xInputSlot, &s) == ERROR_SUCCESS && !isViGEmSlot(m_xInputSlot))
        return m_xInputSlot;
    for (UINT i = 0; i < XUSER_MAX_COUNT; ++i) {
        if (i == m_xInputSlot) continue;
        if (XInputGetState(i, &s) != ERROR_SUCCESS) continue;
        if (isViGEmSlot(i)) continue;
        m_xInputSlot = i;
        return i;
    }
    return XUSER_MAX_COUNT;
}

bool XInputInputSource::isConnected() const {
    return findActiveSlot() != XUSER_MAX_COUNT;
}

bool XInputInputSource::read(GamepadState& state) {
    UINT slot = findActiveSlot();
    if (slot == XUSER_MAX_COUNT) return false;
    XINPUT_STATE s = {};
    if (XInputGetState(slot, &s) != ERROR_SUCCESS) return false;
    JOYINFOEX info = buildFakeJoyInfo(s.Gamepad);
    return processJoyInfo(info, state);
}

// Translates XINPUT_GAMEPAD to a synthetic JOYINFOEX so the existing WinMM
// mapping logic (axis names dwXpos/dwYpos/dwZpos/dwRpos/dwUpos, POV dpad,
// button bitmask A=bit0 … R3=bit9) works without any config changes.
JOYINFOEX XInputInputSource::buildFakeJoyInfo(const XINPUT_GAMEPAD& gp) {
    JOYINFOEX info = {};
    info.dwSize  = sizeof(JOYINFOEX);
    info.dwFlags = JOY_RETURNALL;

    // Sticks: XInput short (-32768..32767) → WinMM DWORD (0..65535), Y inverted
    info.dwXpos = (DWORD)(gp.sThumbLX + 32768);
    info.dwYpos = (DWORD)(32768 - gp.sThumbLY);
    info.dwUpos = (DWORD)(gp.sThumbRX + 32768);
    info.dwRpos = (DWORD)(32768 - gp.sThumbRY);

    // Triggers: combine into single Z axis matching WinMM bridge convention.
    // LT pushes toward 65535 (positive half → triggerL), RT toward 0 (negative half → triggerR).
    int z = 32768 + ((int)gp.bLeftTrigger - (int)gp.bRightTrigger) * 128;
    info.dwZpos = (DWORD)std::clamp(z, 0, 65535);

    // Buttons: XInput bitmask → WinMM order (A=bit0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7, L3=8, R3=9)
    DWORD btns = 0;
    if (gp.wButtons & XINPUT_GAMEPAD_A)              btns |= (1u << 0);
    if (gp.wButtons & XINPUT_GAMEPAD_B)              btns |= (1u << 1);
    if (gp.wButtons & XINPUT_GAMEPAD_X)              btns |= (1u << 2);
    if (gp.wButtons & XINPUT_GAMEPAD_Y)              btns |= (1u << 3);
    if (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  btns |= (1u << 4);
    if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) btns |= (1u << 5);
    if (gp.wButtons & XINPUT_GAMEPAD_BACK)           btns |= (1u << 6);
    if (gp.wButtons & XINPUT_GAMEPAD_START)          btns |= (1u << 7);
    if (gp.wButtons & XINPUT_GAMEPAD_LEFT_THUMB)     btns |= (1u << 8);
    if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)    btns |= (1u << 9);
    info.dwButtons = btns;

    // D-pad via POV (hundredths of a degree, clockwise from north)
    info.dwPOV = JOY_POVCENTERED;
    const bool up    = (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP)    != 0;
    const bool down  = (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  != 0;
    const bool left  = (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  != 0;
    const bool right = (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    if      ( up && !down && !left && !right) info.dwPOV =     0;
    else if ( up && !down && !left &&  right) info.dwPOV =  4500;
    else if (!up && !down && !left &&  right) info.dwPOV =  9000;
    else if (!up &&  down && !left &&  right) info.dwPOV = 13500;
    else if (!up &&  down && !left && !right) info.dwPOV = 18000;
    else if (!up &&  down &&  left && !right) info.dwPOV = 22500;
    else if (!up && !down &&  left && !right) info.dwPOV = 27000;
    else if ( up && !down &&  left && !right) info.dwPOV = 31500;

    return info;
}
