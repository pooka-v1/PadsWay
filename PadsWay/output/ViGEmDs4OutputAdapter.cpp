#include "ViGEmDs4OutputAdapter.h"
#include "../Log.h"
#include <ViGEm/Common.h>   // DS4_REPORT, DS4_SET_DPAD, button enums
#include <algorithm>
#include <cmath>

namespace {

// A real DS4 sets the digital L2/R2 button bit once the trigger is pressed past a
// small part of its travel, alongside the analog value. Keep a modest deadzone so
// resting noise doesn't latch the button.
constexpr float kTriggerButtonThreshold = 0.12f;

// Collapse the four independent d-pad booleans into the DS4's single 8-way HAT.
// Opposite presses on an axis cancel out (matches real-hardware behaviour).
DS4_DPAD_DIRECTIONS dpadDirection(const GamepadState& s) {
    bool up = s.dpadUp, down = s.dpadDown, left = s.dpadLeft, right = s.dpadRight;
    if (up && down)    up = down = false;
    if (left && right) left = right = false;

    if (up   && right) return DS4_BUTTON_DPAD_NORTHEAST;
    if (up   && left)  return DS4_BUTTON_DPAD_NORTHWEST;
    if (down && right) return DS4_BUTTON_DPAD_SOUTHEAST;
    if (down && left)  return DS4_BUTTON_DPAD_SOUTHWEST;
    if (up)            return DS4_BUTTON_DPAD_NORTH;
    if (right)         return DS4_BUTTON_DPAD_EAST;
    if (down)          return DS4_BUTTON_DPAD_SOUTH;
    if (left)          return DS4_BUTTON_DPAD_WEST;
    return DS4_BUTTON_DPAD_NONE;
}

// Map a stick axis from our convention (float [-1,1], -1 = left/down, +1 = right/up)
// to the DS4 byte axis (0..255, 0x80 center). The DS4 Y axis grows DOWNWARD, so the
// vertical axes pass invert=true to flip our "up = +1" into the DS4's "up = 0".
BYTE axisToByte(float value, bool invert) {
    float v = std::clamp(value, -1.0f, 1.0f);
    if (invert) v = -v;
    float scaled = (v + 1.0f) * 0.5f * 255.0f;   // -1 -> 0, 0 -> ~127.5, +1 -> 255
    return static_cast<BYTE>(std::lround(scaled));
}

} // namespace

ViGEmDs4OutputAdapter::ViGEmDs4OutputAdapter(USHORT vid, USHORT pid) {
    if (!m_client) return;   // base failed to connect to ViGEmBus
    if (plugIn(vigem_target_ds4_alloc(), vid, pid))
        spdlog::info("[ViGEm] Virtual DualShock 4 controller plugged in.");
}

void ViGEmDs4OutputAdapter::update(const GamepadState& state) {
    if (!m_ready) return;

    DS4_REPORT report;
    DS4_REPORT_INIT(&report);   // sticks centered (0x80), d-pad neutral

    // --- Face / shoulder / menu / stick buttons (by physical position) ---
    USHORT buttons = 0;
    if (state.btnA)  buttons |= DS4_BUTTON_CROSS;     // bottom
    if (state.btnB)  buttons |= DS4_BUTTON_CIRCLE;    // right
    if (state.btnX)  buttons |= DS4_BUTTON_SQUARE;    // left
    if (state.btnY)  buttons |= DS4_BUTTON_TRIANGLE;  // top
    if (state.btnLB) buttons |= DS4_BUTTON_SHOULDER_LEFT;
    if (state.btnRB) buttons |= DS4_BUTTON_SHOULDER_RIGHT;
    if (state.btnBack)  buttons |= DS4_BUTTON_SHARE;
    if (state.btnStart) buttons |= DS4_BUTTON_OPTIONS;
    if (state.btnL3) buttons |= DS4_BUTTON_THUMB_LEFT;
    if (state.btnR3) buttons |= DS4_BUTTON_THUMB_RIGHT;
    if (state.triggerL > kTriggerButtonThreshold) buttons |= DS4_BUTTON_TRIGGER_LEFT;
    if (state.triggerR > kTriggerButtonThreshold) buttons |= DS4_BUTTON_TRIGGER_RIGHT;
    report.wButtons = buttons;   // overwrites the nibble; d-pad set right below

    // D-pad lives in the low 4 bits of wButtons — must come AFTER the assignment above.
    DS4_SET_DPAD(&report, dpadDirection(state));

    // --- Special buttons (PS / touchpad click) ---
    BYTE special = 0;
    if (state.btnHome)  special |= DS4_SPECIAL_BUTTON_PS;
    if (state.btnTouch) special |= DS4_SPECIAL_BUTTON_TOUCHPAD;
    report.bSpecial = special;

    // --- Triggers: float [0.0, 1.0] -> BYTE [0, 255] ---
    report.bTriggerL = toByte(state.triggerL);
    report.bTriggerR = toByte(state.triggerR);

    // --- Sticks: float [-1.0, 1.0] -> BYTE [0, 255], Y axes inverted ---
    report.bThumbLX = axisToByte(state.leftX,  false);
    report.bThumbLY = axisToByte(state.leftY,  true);
    report.bThumbRX = axisToByte(state.rightX, false);
    report.bThumbRY = axisToByte(state.rightY, true);

    vigem_target_ds4_update(m_client, m_pad, report);
}
