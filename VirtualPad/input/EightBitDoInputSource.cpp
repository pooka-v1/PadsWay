#include "EightBitDoInputSource.h"
#include <algorithm>

#pragma comment(lib, "WinMM.lib")

EightBitDoInputSource::EightBitDoInputSource(UINT joyId, const ControllerConfig& config)
    : m_joyId(joyId), m_config(config) {}

bool EightBitDoInputSource::isConnected() const {
    JOYINFOEX info;
    info.dwSize  = sizeof(JOYINFOEX);
    info.dwFlags = JOY_RETURNBUTTONS;
    return joyGetPosEx(m_joyId, &info) == JOYERR_NOERROR;
}

bool EightBitDoInputSource::read(GamepadState& state) {
    JOYINFOEX info = {};
    info.dwSize  = sizeof(JOYINFOEX);
    info.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(m_joyId, &info) != JOYERR_NOERROR)
        return false;

    m_lastButtonMask = info.dwButtons;

    // Apply a virtual button name to the corresponding GamepadState field.
    auto setVirtualButton = [&](const std::string& name, bool value) {
        if      (name == "a")      state.btnA     = value;
        else if (name == "b")      state.btnB     = value;
        else if (name == "x")      state.btnX     = value;
        else if (name == "y")      state.btnY     = value;
        else if (name == "l1")     state.btnLB    = value;
        else if (name == "r1")     state.btnRB    = value;
        else if (name == "select") state.btnBack  = value;
        else if (name == "start")  state.btnStart = value;
        else if (name == "home")   state.btnHome  = value;
        else if (name == "l3")     state.btnL3    = value;
        else if (name == "r3")     state.btnR3    = value;
    };

    // Process all mapped buttons.
    for (const auto& [bit, action] : m_config.buttons) {
        bool pressed = (info.dwButtons & (1u << (bit - 1))) != 0;
        switch (action.type) {
        case ButtonActionType::VirtualButton:
            setVirtualButton(action.name, pressed);
            break;
        case ButtonActionType::Trigger: {
            float v = !action.axis.empty()
                ? normalizeTrigger(getAxisValue(info, action.axis))
                : (pressed ? 1.0f : 0.0f);
            if      (action.target == "l2") state.triggerL = v;
            else if (action.target == "r2") state.triggerR = v;
            break;
        }
        case ButtonActionType::Bot:
        case ButtonActionType::Macro:
            // Handled in PadEngine — skip here.
            break;
        }
    }

    // Process all mapped axes.
    for (const auto& [source, mapping] : m_config.axes) {
        float v = normalizeAxis(getAxisValue(info, source));
        if (mapping.invert) v = -v;
        if      (mapping.target == "left_x")  state.leftX  = v;
        else if (mapping.target == "left_y")  state.leftY  = v;
        else if (mapping.target == "right_x") state.rightX = v;
        else if (mapping.target == "right_y") state.rightY = v;
        else if (mapping.target == "trigger_combined") {
            // Shared axis: positive half → L2, negative half → R2
            state.triggerL = (v > 0.0f) ?  v : 0.0f;
            state.triggerR = (v < 0.0f) ? -v : 0.0f;
        }
        else if (mapping.target == "mouse_x") state.mouseX = v;
        else if (mapping.target == "mouse_y") state.mouseY = v;
    }

    if (m_config.dpad == "pov")
        parsePOV(info.dwPOV, state.dpadUp, state.dpadDown, state.dpadLeft, state.dpadRight);

    // If both digital triggers are pressed simultaneously, cancel each other out.
    // (Mirrors the physical behavior of the shared Z-axis in X-mode.)
    if (state.triggerL > 0.0f && state.triggerR > 0.0f) {
        state.triggerL = 0.0f;
        state.triggerR = 0.0f;
    }

    return true;
}

DWORD EightBitDoInputSource::getAxisValue(const JOYINFOEX& info, const std::string& source) {
    if (source == "dwXpos") return info.dwXpos;
    if (source == "dwYpos") return info.dwYpos;
    if (source == "dwZpos") return info.dwZpos;
    if (source == "dwRpos") return info.dwRpos;
    if (source == "dwUpos") return info.dwUpos;
    if (source == "dwVpos") return info.dwVpos;
    return 32768; // center value as safe fallback
}

float EightBitDoInputSource::normalizeAxis(DWORD value) {
    float normalized = (static_cast<float>(value) - 32767.5f) / 32767.5f;
    return std::clamp(normalized, -1.0f, 1.0f);
}

float EightBitDoInputSource::normalizeTrigger(DWORD value) {
    float normalized = static_cast<float>(value) / 65535.0f;
    return std::clamp(normalized, 0.0f, 1.0f);
}

void EightBitDoInputSource::parsePOV(DWORD pov, bool& up, bool& down, bool& left, bool& right) {
    if (pov == JOY_POVCENTERED) {
        up = down = left = right = false;
        return;
    }
    up    = (pov >= 31500 || pov <= 4500);
    right = (pov >= 4500  && pov <= 13500);
    down  = (pov >= 13500 && pov <= 22500);
    left  = (pov >= 22500 && pov <= 31500);
}
