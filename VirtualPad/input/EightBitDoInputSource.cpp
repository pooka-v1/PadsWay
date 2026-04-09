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

    // OR semantics: only set true, never overwrite with false.
    // GamepadState starts zeroed each frame, so un-pressed buttons are already false.
    // This allows multiple physical buttons to map to the same virtual target.
    auto setVirtualButton = [&](const std::string& name, bool value) {
        if (!value) return;
        if      (name == "a")      state.btnA     = true;
        else if (name == "b")      state.btnB     = true;
        else if (name == "x")      state.btnX     = true;
        else if (name == "y")      state.btnY     = true;
        else if (name == "l1")     state.btnLB    = true;
        else if (name == "r1")     state.btnRB    = true;
        else if (name == "select") state.btnBack  = true;
        else if (name == "start")  state.btnStart = true;
        else if (name == "home")   state.btnHome  = true;
        else if (name == "l3")     state.btnL3    = true;
        else if (name == "r3")     state.btnR3    = true;
        else if (name == "l4")     state.btnL4    = true;
        else if (name == "r4")     state.btnR4    = true;
        else if (name == "lp")     state.btnLP    = true;
        else if (name == "rp")     state.btnRP    = true;
    };

    // Reset virtual button states before remapping so OR logic works correctly
    // regardless of unordered_map iteration order.
    state.btnA = state.btnB = state.btnX    = state.btnY   = false;
    state.btnLB = state.btnRB = false;
    state.btnBack = state.btnStart = state.btnHome = false;
    state.btnL3 = state.btnR3 = false;
    state.btnL4 = state.btnR4 = false;
    state.btnLP = state.btnRP = false;

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
            if (v > 0.0f) {
                if      (action.target == "l2") state.triggerL = v;
                else if (action.target == "r2") state.triggerR = v;
            }
            break;
        }
        case ButtonActionType::Bot:
        case ButtonActionType::Macro:
            // Handled in PadEngine — skip here.
            break;
        }
    }

    // Estado visual físico — independiente de la acción asignada.
    // Permite iluminar L4/R4/Lp/Rp aunque el perfil los tenga como Macro/Bot.
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.physical.empty()) continue;
        bool pressed = (info.dwButtons & (1u << (bit - 1))) != 0;
        setVirtualButton(action.physical, pressed);
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

    // Estado visual físico — independiente de la acción asignada.
    // Permite iluminar L4/R4/Lp/Rp aunque el perfil los tenga como Macro/Bot.
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.physical.empty()) continue;
        bool pressed = (info.dwButtons & (1u << (bit - 1))) != 0;
        setVirtualButton(action.physical, pressed);
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
