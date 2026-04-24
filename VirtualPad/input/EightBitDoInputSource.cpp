#include "EightBitDoInputSource.h"
#include "StickSlotsHelper.h"
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

    if (m_hasPhysicalController) {
        // ── Component-system path ─────────────────────────────────────────────
        m_physicalState = {};
        buildPhysicalButtons(info);
        buildPhysicalAxes(info);

        bool hasAxisDpad = false;
        for (const auto& [src, m] : m_config.axes)
            if (m.target == "dpad_x" || m.target == "dpad_y") { hasAxisDpad = true; break; }
        if (!hasAxisDpad && m_config.dpad == "pov")
            parsePOV(info.dwPOV, m_physicalState.dpadUp, m_physicalState.dpadDown,
                     m_physicalState.dpadLeft, m_physicalState.dpadRight);

        state = {};
        m_physicalController.process(m_physicalState, state);
        applyAxesResidual(info, state);
        return true;
    }

    // ── Legacy path ───────────────────────────────────────────────────────────
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
    // Dpad bits also reset so axis_actions Dpad assignments clear when stick returns to neutral
    state.dpadUp = state.dpadDown = state.dpadLeft = state.dpadRight = false;
    // Mouse delta reset each frame so axis_actions mouse_move stops when stick returns to neutral
    state.mouseX = state.mouseY = 0.0f;

    // Buttons whose physical identity is a stick slot source lose their virtual
    // action entirely (one input → one output). Identified by action.physical so
    // unrelated buttons remapped to the same virtual target are not suppressed.
    auto isSlotSrc = [&](const std::string& phys) -> bool {
        if (phys.empty() || m_config.stickSlots.empty()) return false;
        for (const auto& [slot, srcs] : m_config.stickSlots)
            for (const auto& src : srcs)
                if (src == phys) return true;
        return false;
    };

    // Process all mapped buttons.
    for (const auto& [bit, action] : m_config.buttons) {
        if (isSlotSrc(action.physical)) continue;
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
    bool hasAxisDpad = false;
    for (const auto& [source, mapping] : m_config.axes)
        if (mapping.target == "dpad_x" || mapping.target == "dpad_y") { hasAxisDpad = true; break; }

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
        else if (mapping.target == "dpad_x") {
            state.dpadLeft  = v < -mapping.threshold;
            state.dpadRight = v >  mapping.threshold;
        }
        else if (mapping.target == "dpad_y") {
            state.dpadUp   = v < -mapping.threshold;
            state.dpadDown = v >  mapping.threshold;
        }
        else if (mapping.target == "btn_dir") {
            if (!mapping.btnNeg.empty()) setVirtualButton(mapping.btnNeg, v < -mapping.threshold);
            if (!mapping.btnPos.empty()) setVirtualButton(mapping.btnPos, v >  mapping.threshold);
        }
    }

    // Estado visual físico — independiente de la acción asignada.
    // Permite iluminar L4/R4/Lp/Rp aunque el perfil los tenga como Macro/Bot.
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.physical.empty()) continue;
        bool pressed = (info.dwButtons & (1u << (bit - 1))) != 0;
        setVirtualButton(action.physical, pressed);
    }

    if (!hasAxisDpad && m_config.dpad == "pov")
        parsePOV(info.dwPOV, state.dpadUp, state.dpadDown, state.dpadLeft, state.dpadRight);

    // If both digital triggers are pressed simultaneously, cancel each other out.
    // (Mirrors the physical behavior of the shared Z-axis in X-mode.)
    if (state.triggerL > 0.0f && state.triggerR > 0.0f) {
        state.triggerL = 0.0f;
        state.triggerR = 0.0f;
    }

    // ── axis_actions: per-direction half-axis processing ─────────────────────
    m_activeAxisActions.clear();
    if (!m_config.axis_actions.empty()) {
        auto setVBtn = [&](const std::string& name, bool val) {
            if (!val) return;
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

        for (const auto& [source, mapping] : m_config.axes) {
            float v = normalizeAxis(getAxisValue(info, source));
            if (mapping.invert) v = -v;

            auto processHalf = [&](const std::string& key, float halfV) {
                auto ait = m_config.axis_actions.find(key);
                if (ait == m_config.axis_actions.end()) return;
                const HalfAxisAction& ha = ait->second;
                float absV = std::abs(halfV);

                switch (ha.type) {
                case HalfAxisActionType::Analog: {
                    float outV = absV * ha.scale;
                    if (ha.outDir == "neg") outV = -outV;
                    if      (ha.target == "left_x")  state.leftX  = outV;
                    else if (ha.target == "left_y")  state.leftY  = outV;
                    else if (ha.target == "right_x") state.rightX = outV;
                    else if (ha.target == "right_y") state.rightY = outV;
                    break;
                }
                case HalfAxisActionType::VirtualButton:
                    if (absV > ha.threshold) setVBtn(ha.target, true);
                    break;
                case HalfAxisActionType::Dpad:
                    if (absV > ha.threshold) {
                        if      (ha.target == "up")    state.dpadUp    = true;
                        else if (ha.target == "down")  state.dpadDown  = true;
                        else if (ha.target == "left")  state.dpadLeft  = true;
                        else if (ha.target == "right") state.dpadRight = true;
                    }
                    break;
                case HalfAxisActionType::Trigger:
                    if (absV > ha.threshold) {
                        if      (ha.target == "l2" || ha.target == "trigger_l") state.triggerL = 1.0f;
                        else if (ha.target == "r2" || ha.target == "trigger_r") state.triggerR = 1.0f;
                    }
                    break;
                case HalfAxisActionType::StickSlot:
                    if (absV > ha.threshold) {
                        if      (ha.target == "left_x_pos")  state.leftX  =  1.0f;
                        else if (ha.target == "left_x_neg")  state.leftX  = -1.0f;
                        else if (ha.target == "left_y_pos")  state.leftY  =  1.0f;
                        else if (ha.target == "left_y_neg")  state.leftY  = -1.0f;
                        else if (ha.target == "right_x_pos") state.rightX =  1.0f;
                        else if (ha.target == "right_x_neg") state.rightX = -1.0f;
                        else if (ha.target == "right_y_pos") state.rightY =  1.0f;
                        else if (ha.target == "right_y_neg") state.rightY = -1.0f;
                    }
                    break;
                case HalfAxisActionType::MouseMove:
                    if      (ha.target == "mouse_x") state.mouseX += halfV * ha.speed;
                    else if (ha.target == "mouse_y") state.mouseY += halfV * ha.speed;
                    break;
                case HalfAxisActionType::Ranges:
                    for (const auto& r : ha.ranges) {
                        if (absV < r.from || absV > r.to || !r.hasAction) continue;
                        switch (r.action.type) {
                        case ButtonActionType::VirtualButton: setVBtn(r.action.name, true); break;
                        case ButtonActionType::Keyboard:
                        case ButtonActionType::MouseClick:
                        case ButtonActionType::Macro:
                            m_activeAxisActions.push_back(key); break;
                        default: break;
                        }
                        break;
                    }
                    break;
                case HalfAxisActionType::Macro:
                case HalfAxisActionType::Keyboard:
                case HalfAxisActionType::MouseClick:
                    if (absV > ha.threshold)
                        m_activeAxisActions.push_back(key);
                    break;
                }
                // Suppress raw axis contribution for this half when redirected to a non-analog target.
                if (ha.type != HalfAxisActionType::Analog && ha.type != HalfAxisActionType::Ranges) {
                    if      (mapping.target == "left_x")  state.leftX  = (halfV < 0.0f) ? (state.leftX  > 0.0f ? state.leftX  : 0.0f) : (state.leftX  < 0.0f ? state.leftX  : 0.0f);
                    else if (mapping.target == "left_y")  state.leftY  = (halfV < 0.0f) ? (state.leftY  > 0.0f ? state.leftY  : 0.0f) : (state.leftY  < 0.0f ? state.leftY  : 0.0f);
                    else if (mapping.target == "right_x") state.rightX = (halfV < 0.0f) ? (state.rightX > 0.0f ? state.rightX : 0.0f) : (state.rightX < 0.0f ? state.rightX : 0.0f);
                    else if (mapping.target == "right_y") state.rightY = (halfV < 0.0f) ? (state.rightY > 0.0f ? state.rightY : 0.0f) : (state.rightY < 0.0f ? state.rightY : 0.0f);
                }
            };

            // Key uses virtual axis name (mapping.target) so JSON entries match: "left_x_pos" etc.
            if (v >= 0.0f) processHalf(mapping.target + "_pos", v);
            else           processHalf(mapping.target + "_neg", v);
        }
    }

    // Button → dpad remapping: after POV and axis_actions so it only affects
    // virtual output and isn't overwritten by physical dpad processing.
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.type != ButtonActionType::VirtualButton) continue;
        if (isSlotSrc(action.physical)) continue;
        bool pressed = (info.dwButtons & (1u << (bit - 1))) != 0;
        if (!pressed) continue;
        if      (action.name == "dpad_up")    state.dpadUp    = true;
        else if (action.name == "dpad_down")  state.dpadDown  = true;
        else if (action.name == "dpad_left")  state.dpadLeft  = true;
        else if (action.name == "dpad_right") state.dpadRight = true;
    }

    // Stick slots: build physical state snapshot then apply overrides.
    if (!m_config.stickSlots.empty()) {
        GamepadState phys;
        // Physical buttons: iterate config and check raw bit mask directly.
        for (const auto& [bit, action] : m_config.buttons) {
            if (action.physical.empty()) continue;
            if (!(info.dwButtons & (1u << (bit - 1)))) continue;
            const std::string& p = action.physical;
            if      (p == "a")      phys.btnA     = true;
            else if (p == "b")      phys.btnB     = true;
            else if (p == "x")      phys.btnX     = true;
            else if (p == "y")      phys.btnY     = true;
            else if (p == "l1")     phys.btnLB    = true;
            else if (p == "r1")     phys.btnRB    = true;
            else if (p == "select") phys.btnBack  = true;
            else if (p == "start")  phys.btnStart = true;
            else if (p == "home")   phys.btnHome  = true;
            else if (p == "l3")     phys.btnL3    = true;
            else if (p == "r3")     phys.btnR3    = true;
            else if (p == "l4")     phys.btnL4    = true;
            else if (p == "r4")     phys.btnR4    = true;
            else if (p == "lp")     phys.btnLP    = true;
            else if (p == "rp")     phys.btnRP    = true;
        }
        // Triggers: post-cancellation value from state is the physical value.
        phys.triggerL = state.triggerL;
        phys.triggerR = state.triggerR;
        // Dpad: physical directions (before button→dpad remapping above).
        // Use POV/axis-driven dpad from state; button→dpad remapping only ORs in,
        // so state.dpadUp etc. already reflects physical dpad at this point.
        phys.dpadUp    = state.dpadUp;
        phys.dpadDown  = state.dpadDown;
        phys.dpadLeft  = state.dpadLeft;
        phys.dpadRight = state.dpadRight;

        applyStickSlots(m_config, phys, state);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Component-system path
// ---------------------------------------------------------------------------

void EightBitDoInputSource::buildPhysicalButtons(const JOYINFOEX& info) {
    m_lastButtonMask = info.dwButtons;

    auto setPhys = [&](const std::string& name, bool v) {
        if      (name == "a")      m_physicalState.btnA     = v;
        else if (name == "b")      m_physicalState.btnB     = v;
        else if (name == "x")      m_physicalState.btnX     = v;
        else if (name == "y")      m_physicalState.btnY     = v;
        else if (name == "l1")     m_physicalState.btnLB    = v;
        else if (name == "r1")     m_physicalState.btnRB    = v;
        else if (name == "select") m_physicalState.btnBack  = v;
        else if (name == "start")  m_physicalState.btnStart = v;
        else if (name == "home")   m_physicalState.btnHome  = v;
        else if (name == "l3")     m_physicalState.btnL3    = v;
        else if (name == "r3")     m_physicalState.btnR3    = v;
        else if (name == "l4")     m_physicalState.btnL4    = v;
        else if (name == "r4")     m_physicalState.btnR4    = v;
        else if (name == "lp")     m_physicalState.btnLP    = v;
        else if (name == "rp")     m_physicalState.btnRP    = v;
    };
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.physical.empty()) continue;
        bool pressed = (info.dwButtons & (1u << (bit - 1))) != 0;
        setPhys(action.physical, pressed);
        if (pressed && action.type == ButtonActionType::Trigger) {
            if      (action.physical == "l2") m_physicalState.triggerL = 1.0f;
            else if (action.physical == "r2") m_physicalState.triggerR = 1.0f;
        }
    }
}

void EightBitDoInputSource::buildPhysicalAxes(const JOYINFOEX& info) {
    for (const auto& [source, mapping] : m_config.axes) {
        float v = normalizeAxis(getAxisValue(info, source));
        if (mapping.invert) v = -v;

        if      (mapping.target == "left_x")  m_physicalState.leftX  = v;
        else if (mapping.target == "left_y")  m_physicalState.leftY  = v;
        else if (mapping.target == "right_x") m_physicalState.rightX = v;
        else if (mapping.target == "right_y") m_physicalState.rightY = v;
        else if (mapping.target == "trigger_l")
            m_physicalState.triggerL = (v + 1.0f) * 0.5f;
        else if (mapping.target == "trigger_r")
            m_physicalState.triggerR = (v + 1.0f) * 0.5f;
        else if (mapping.target == "trigger_combined") {
            m_physicalState.triggerL = (v > 0.0f) ?  v : 0.0f;
            m_physicalState.triggerR = (v < 0.0f) ? -v : 0.0f;
        }
    }
}

void EightBitDoInputSource::applyAxesResidual(const JOYINFOEX& info, GamepadState& state) {
    auto setBtn = [&](const std::string& name, bool v) {
        if (!v) return;
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
    };

    m_activeAxisActions.clear();
    for (const auto& [source, mapping] : m_config.axes) {
        float v = normalizeAxis(getAxisValue(info, source));
        if (mapping.invert) v = -v;

        if (mapping.target == "mouse_x") state.mouseX = v;
        else if (mapping.target == "mouse_y") state.mouseY = v;
        else if (mapping.target == "dpad_x") {
            state.dpadLeft  = v < -mapping.threshold;
            state.dpadRight = v >  mapping.threshold;
        }
        else if (mapping.target == "dpad_y") {
            state.dpadUp   = v < -mapping.threshold;
            state.dpadDown = v >  mapping.threshold;
        }
        else if (mapping.target == "trigger_combined") {
            float tl = (v > 0.0f) ?  v : 0.0f;
            float tr = (v < 0.0f) ? -v : 0.0f;
            if (tl > state.triggerL) state.triggerL = tl;
            if (tr > state.triggerR) state.triggerR = tr;
        }
        else if (mapping.target == "btn_dir") {
            if (!mapping.btnNeg.empty()) setBtn(mapping.btnNeg, v < -mapping.threshold);
            if (!mapping.btnPos.empty()) setBtn(mapping.btnPos, v >  mapping.threshold);
        }

        if (!m_config.axis_actions.empty()) {
            auto checkHalf = [&](const std::string& key, float halfV) {
                auto ait = m_config.axis_actions.find(key);
                if (ait == m_config.axis_actions.end()) return;
                const HalfAxisAction& ha = ait->second;
                float absV = std::abs(halfV);
                switch (ha.type) {
                case HalfAxisActionType::Macro:
                case HalfAxisActionType::Keyboard:
                case HalfAxisActionType::MouseClick:
                    if (absV > ha.threshold)
                        m_activeAxisActions.push_back(key);
                    break;
                case HalfAxisActionType::Ranges:
                    for (const auto& r : ha.ranges) {
                        if (absV < r.from || absV > r.to || !r.hasAction) continue;
                        if (r.action.type == ButtonActionType::Keyboard   ||
                            r.action.type == ButtonActionType::MouseClick  ||
                            r.action.type == ButtonActionType::Macro)
                            m_activeAxisActions.push_back(key);
                        break;
                    }
                    break;
                default: break;
                }
            };
            if (v >= 0.0f) checkHalf(mapping.target + "_pos", v);
            else           checkHalf(mapping.target + "_neg", v);
        }
    }
}

// ---------------------------------------------------------------------------

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
