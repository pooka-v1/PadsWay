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
    JOYINFOEX info;
    info.dwSize  = sizeof(JOYINFOEX);
    info.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(m_joyId, &info) != JOYERR_NOERROR)
        return false;

    // Reads a button by name; returns false if not mapped.
    auto btn = [&](const std::string& name) -> bool {
        auto it = m_config.buttons.find(name);
        if (it == m_config.buttons.end()) return false;
        return (info.dwButtons & (1u << (it->second - 1))) != 0;
    };

    // Reads an axis by name; applies inversion if configured.
    auto axis = [&](const std::string& name) -> float {
        auto it = m_config.axes.find(name);
        if (it == m_config.axes.end()) return 0.0f;
        float v = normalizeAxis(getAxisValue(info, it->second.source));
        return it->second.invert ? -v : v;
    };

    // Reads a trigger by name; axis takes priority over button fallback.
    auto trigger = [&](const std::string& name) -> float {
        auto it = m_config.triggers.find(name);
        if (it == m_config.triggers.end()) return 0.0f;
        const auto& t = it->second;
        if (!t.axis.empty())
            return normalizeTrigger(getAxisValue(info, t.axis));
        if (t.button > 0)
            return (info.dwButtons & (1u << (t.button - 1))) ? 1.0f : 0.0f;
        return 0.0f;
    };

    state.btnA     = btn("a");
    state.btnB     = btn("b");
    state.btnX     = btn("x");
    state.btnY     = btn("y");
    state.btnLB    = btn("l1");
    state.btnRB    = btn("r1");
    state.btnBack  = btn("select");
    state.btnStart = btn("start");
    state.btnHome  = btn("home");
    state.btnL3    = btn("l3");
    state.btnR3    = btn("r3");

    state.leftX  = axis("left_x");
    state.leftY  = axis("left_y");
    state.rightX = axis("right_x");
    state.rightY = axis("right_y");

    state.triggerL = trigger("l2");
    state.triggerR = trigger("r2");

    if (m_config.dpad == "pov")
        parsePOV(info.dwPOV, state.dpadUp, state.dpadDown, state.dpadLeft, state.dpadRight);

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
