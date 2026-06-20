#include "ComponentTypes.h"
#include <algorithm>
#include <functional>
#include <variant>

// ─── Internal helpers ─────────────────────────────────────────────────────────

// Writes a VirtualTarget to the output GamepadState.
// value: [0.0, 1.0] — proportional for analog targets, 1.0 for binary targets.
// VirtualKeyboard, VirtualMacro, VirtualMouseClick, VirtualBot are marker targets:
// their activation is detected by PadEngine by inspecting the PhysicalComponent
// directly. process() does not write anything for them.
static void applyVirtualTarget(const VirtualTarget& vt, float value,
                                GamepadState& out,
                                StickAccumulator& left, StickAccumulator& right,
                                GyroAccumulator& gyro,
                                float dirSign = 1.0f) {
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, VirtualButton>) {
            if (value <= 0.0f) return;
            switch (v.id) {
                case ButtonId::A:     out.btnA     = true; break;
                case ButtonId::B:     out.btnB     = true; break;
                case ButtonId::X:     out.btnX     = true; break;
                case ButtonId::Y:     out.btnY     = true; break;
                case ButtonId::LB:    out.btnLB    = true; break;
                case ButtonId::RB:    out.btnRB    = true; break;
                case ButtonId::L3:    out.btnL3    = true; break;
                case ButtonId::R3:    out.btnR3    = true; break;
                case ButtonId::Back:  out.btnBack  = true; break;
                case ButtonId::Start: out.btnStart = true; break;
                case ButtonId::Home:  out.btnHome  = true; break;
            }
        } else if constexpr (std::is_same_v<T, VirtualDpadDir>) {
            if (value <= 0.0f) return;
            switch (v.dir) {
                case DpadDir::Up:    out.dpadUp    = true; break;
                case DpadDir::Down:  out.dpadDown  = true; break;
                case DpadDir::Left:  out.dpadLeft  = true; break;
                case DpadDir::Right: out.dpadRight = true; break;
            }
        } else if constexpr (std::is_same_v<T, VirtualTrigger>) {
            float tv = std::clamp(value, 0.0f, 1.0f);
            if (v.side == TriggerSide::L) out.triggerL = std::max(out.triggerL, tv);
            else                          out.triggerR = std::max(out.triggerR, tv);
        } else if constexpr (std::is_same_v<T, VirtualStickSlot>) {
            float sv = std::clamp(value, 0.0f, 1.0f);
            switch (v.slot) {
                case StickSlotId::LeftXPos:  left.xPos  = std::max(left.xPos,  sv); break;
                case StickSlotId::LeftXNeg:  left.xNeg  = std::max(left.xNeg,  sv); break;
                case StickSlotId::LeftYPos:  left.yPos  = std::max(left.yPos,  sv); break;
                case StickSlotId::LeftYNeg:  left.yNeg  = std::max(left.yNeg,  sv); break;
                case StickSlotId::RightXPos: right.xPos = std::max(right.xPos, sv); break;
                case StickSlotId::RightXNeg: right.xNeg = std::max(right.xNeg, sv); break;
                case StickSlotId::RightYPos: right.yPos = std::max(right.yPos, sv); break;
                case StickSlotId::RightYNeg: right.yNeg = std::max(right.yNeg, sv); break;
            }
        } else if constexpr (std::is_same_v<T, VirtualMouseMove>) {
            float mv = value * dirSign * v.speed;
            if (v.axis == MouseAxis::X) out.mouseX += mv;
            else                        out.mouseY += mv;
        }
        // VirtualKeyboard, VirtualMacro, VirtualMouseClick, VirtualBot, VirtualPassthrough:
        // handled externally — process() does not write to GamepadState for these.
    }, vt);
}

// Returns true if a VirtualTarget is proportional (value passes through as-is).
// False = binary (use value=1.0 when the range threshold is met).
static bool isProportionalTarget(const VirtualTarget& vt) {
    return std::holds_alternative<VirtualTrigger>(vt) ||
           std::holds_alternative<VirtualStickSlot>(vt) ||
           std::holds_alternative<VirtualMouseMove>(vt) ||
           std::holds_alternative<VirtualPassthrough>(vt);
}

// Applies a RangedHalfAxis given the current physical value in [0.0, 1.0].
// passthrough: called when the active range targets VirtualPassthrough or ranges are empty.
template<typename PassthroughFn>
static void applyRangedHalfAxis(const RangedHalfAxis& rha, float value,
                                  PassthroughFn passthrough,
                                  GamepadState& out,
                                  StickAccumulator& left, StickAccumulator& right,
                                  GyroAccumulator& gyro,
                                  float dirSign = 1.0f) {
    if (rha.ranges.empty()) {
        passthrough(value);
        return;
    }
    for (const auto& r : rha.ranges) {
        if (value < r.from || value > r.to) continue;
        if (std::holds_alternative<VirtualPassthrough>(r.target)) {
            passthrough(value);
        } else {
            // Proportional only when from==0.0f (direct passthrough assignment).
            // Explicit range entries (from>0) are always binary per mapping invariant.
            float effective = (isProportionalTarget(r.target) && r.from == 0.0f) ? value : 1.0f;
            applyVirtualTarget(r.target, effective, out, left, right, gyro, dirSign);
        }
        break;  // first matching range wins
    }
}

// Reads the physical pressed state for button and dpad ComponentIds.
static bool physPressed(ComponentId id, const GamepadState& s) {
    switch (id) {
        case ComponentId::BtnA:      return s.btnA;
        case ComponentId::BtnB:      return s.btnB;
        case ComponentId::BtnX:      return s.btnX;
        case ComponentId::BtnY:      return s.btnY;
        case ComponentId::BtnLB:     return s.btnLB;
        case ComponentId::BtnRB:     return s.btnRB;
        case ComponentId::BtnL3:     return s.btnL3;
        case ComponentId::BtnR3:     return s.btnR3;
        case ComponentId::BtnBack:   return s.btnBack;
        case ComponentId::BtnStart:  return s.btnStart;
        case ComponentId::BtnHome:   return s.btnHome;
        case ComponentId::DpadUp:    return s.dpadUp;
        case ComponentId::DpadDown:  return s.dpadDown;
        case ComponentId::DpadLeft:  return s.dpadLeft;
        case ComponentId::DpadRight: return s.dpadRight;
        case ComponentId::BtnL4:     return s.btnL4;
        case ComponentId::BtnR4:     return s.btnR4;
        case ComponentId::BtnLP:     return s.btnLP;
        case ComponentId::BtnRP:     return s.btnRP;
        default:                     return false;
    }
}

// Extracts the positive half-axis magnitude [0.0, 1.0] for a given StickSlotId.
static float physHalfAxis(StickSlotId slot, const GamepadState& s) {
    switch (slot) {
        case StickSlotId::LeftXPos:  return s.leftX  > 0.0f ?  s.leftX  : 0.0f;
        case StickSlotId::LeftXNeg:  return s.leftX  < 0.0f ? -s.leftX  : 0.0f;
        case StickSlotId::LeftYPos:  return s.leftY  > 0.0f ?  s.leftY  : 0.0f;
        case StickSlotId::LeftYNeg:  return s.leftY  < 0.0f ? -s.leftY  : 0.0f;
        case StickSlotId::RightXPos: return s.rightX > 0.0f ?  s.rightX : 0.0f;
        case StickSlotId::RightXNeg: return s.rightX < 0.0f ? -s.rightX : 0.0f;
        case StickSlotId::RightYPos: return s.rightY > 0.0f ?  s.rightY : 0.0f;
        case StickSlotId::RightYNeg: return s.rightY < 0.0f ? -s.rightY : 0.0f;
        default:                     return 0.0f;
    }
}

// ─── PhysicalButton ───────────────────────────────────────────────────────────

void PhysicalButton::process(bool pressed, GamepadState& out,
                              StickAccumulator& left, StickAccumulator& right,
                              GyroAccumulator& gyro) const {
    if (!pressed) return;
    applyVirtualTarget(target, 1.0f, out, left, right, gyro);
}

// ─── PhysicalDpadDir ──────────────────────────────────────────────────────────

void PhysicalDpadDir::process(bool active, GamepadState& out,
                               StickAccumulator& left, StickAccumulator& right,
                               GyroAccumulator& gyro) const {
    if (!active) return;
    if (std::holds_alternative<VirtualPassthrough>(target)) {
        switch (dir) {
            case DpadDir::Up:    out.dpadUp    = true; break;
            case DpadDir::Down:  out.dpadDown  = true; break;
            case DpadDir::Left:  out.dpadLeft  = true; break;
            case DpadDir::Right: out.dpadRight = true; break;
        }
    } else {
        applyVirtualTarget(target, 1.0f, out, left, right, gyro);
    }
}

// ─── PhysicalTrigger ──────────────────────────────────────────────────────────

void PhysicalTrigger::process(float value, GamepadState& out,
                               StickAccumulator& left, StickAccumulator& right,
                               GyroAccumulator& gyro) const {
    auto passthrough = [&](float v) {
        if (side == TriggerSide::L) out.triggerL = std::max(out.triggerL, v);
        else                        out.triggerR = std::max(out.triggerR, v);
    };
    applyRangedHalfAxis(axis, value, passthrough, out, left, right, gyro);
}

// ─── PhysicalAnalogDir ────────────────────────────────────────────────────────

void PhysicalAnalogDir::process(float value, GamepadState& out,
                                  StickAccumulator& left, StickAccumulator& right,
                                  GyroAccumulator& gyro) const {
    // Neg slots carry the magnitude of the negative direction (always [0,1]).
    // VirtualMouseMove needs the signed value to know which way to move the cursor.
    float dirSign = (slot == StickSlotId::LeftXNeg  || slot == StickSlotId::LeftYNeg ||
                     slot == StickSlotId::RightXNeg || slot == StickSlotId::RightYNeg)
                    ? -1.0f : 1.0f;

    auto passthrough = [&](float v) {
        switch (slot) {
            case StickSlotId::LeftXPos:  left.xPos  = std::max(left.xPos,  v); break;
            case StickSlotId::LeftXNeg:  left.xNeg  = std::max(left.xNeg,  v); break;
            case StickSlotId::LeftYPos:  left.yPos  = std::max(left.yPos,  v); break;
            case StickSlotId::LeftYNeg:  left.yNeg  = std::max(left.yNeg,  v); break;
            case StickSlotId::RightXPos: right.xPos = std::max(right.xPos, v); break;
            case StickSlotId::RightXNeg: right.xNeg = std::max(right.xNeg, v); break;
            case StickSlotId::RightYPos: right.yPos = std::max(right.yPos, v); break;
            case StickSlotId::RightYNeg: right.yNeg = std::max(right.yNeg, v); break;
        }
    };
    applyRangedHalfAxis(axis, value, passthrough, out, left, right, gyro, dirSign);
}

// ─── PhysicalTouchpad ────────────────────────────────────────────────────────

void PhysicalTouchpad::process(const GamepadState& physical, GamepadState& out,
                                StickAccumulator&, StickAccumulator&, GyroAccumulator&) const {
    // Pass touchpad state through unchanged.
    // Surface routing (mouse movement) is handled by the input source's applyTouchpad().
    out.btnTouch    = physical.btnTouch;
    out.touch1Active = physical.touch1Active;
    out.touch1X     = physical.touch1X;
    out.touch1Y     = physical.touch1Y;
    out.touch2Active = physical.touch2Active;
    out.touch2X     = physical.touch2X;
    out.touch2Y     = physical.touch2Y;
    out.touchDeltaX = physical.touchDeltaX;
    out.touchDeltaY = physical.touchDeltaY;
}

// ─── PhysicalGyro ─────────────────────────────────────────────────────────────

void PhysicalGyro::process(const GamepadState& physical, GamepadState& out,
                            StickAccumulator& left, StickAccumulator& right,
                            GyroAccumulator& gyro) const {
    // Per-axis passthrough or remapped output via RangedHalfAxis.
    out.gyroActive = physical.gyroActive;
    if (!physical.gyroActive) return;

    auto applyHalf = [&](const RangedHalfAxis& rha, float rawSigned,
                          std::function<void(float)> passthroughFn) {
        float halfVal = rawSigned > 0.0f ? rawSigned : 0.0f;
        applyRangedHalfAxis(rha, halfVal, passthroughFn, out, left, right, gyro);
    };

    applyHalf(xPos, physical.gyroX,
              [&](float v) { gyro.xPos = std::max(gyro.xPos, v); });
    applyHalf(xNeg, -physical.gyroX,
              [&](float v) { gyro.xNeg = std::max(gyro.xNeg, v); });
    applyHalf(yPos, physical.gyroY,
              [&](float v) { gyro.yPos = std::max(gyro.yPos, v); });
    applyHalf(yNeg, -physical.gyroY,
              [&](float v) { gyro.yNeg = std::max(gyro.yNeg, v); });
    applyHalf(zPos, physical.gyroZ,
              [&](float v) { gyro.zPos = std::max(gyro.zPos, v); });
    applyHalf(zNeg, -physical.gyroZ,
              [&](float v) { gyro.zNeg = std::max(gyro.zNeg, v); });
}

// ─── PhysicalController::process() ───────────────────────────────────────────

void PhysicalController::process(const GamepadState& physical, GamepadState& output) const {
    StickAccumulator accumLeft, accumRight;
    GyroAccumulator  accumGyro;

    // Pass 1: evaluate modifier sources → build active ModifierMask.
    ModifierMask activeMask = kModNone;
    for (size_t i = 0; i < modifierSources.size() && i < 8; ++i) {
        ComponentId mid = modifierSources[i];
        size_t      idx = static_cast<size_t>(mid);
        if (!baseLayer[idx]) continue;

        bool active = std::visit([&](const auto& c) -> bool {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, PhysicalButton> ||
                          std::is_same_v<T, PhysicalDpadDir>)
                return physPressed(mid, physical);
            if constexpr (std::is_same_v<T, PhysicalTrigger>)
                return (c.side == TriggerSide::L ? physical.triggerL : physical.triggerR) > 0.5f;
            if constexpr (std::is_same_v<T, PhysicalAnalogDir>)
                return physHalfAxis(c.slot, physical) > 0.5f;
            return false;
        }, *baseLayer[idx]);

        if (active) activeMask |= static_cast<ModifierMask>(1u << i);
    }

    // Pass 2: resolve and process all components.
    auto resolveComponent = [&](size_t idx) -> const std::optional<PhysicalComponent>& {
        if (activeMask != kModNone) {
            auto lit = modifierLayers.find(activeMask);
            if (lit != modifierLayers.end() && lit->second[idx])
                return lit->second[idx];
        }
        return baseLayer[idx];
    };

    for (size_t i = 0; i < kComponentCount; ++i) {
        const auto& opt = resolveComponent(i);
        if (!opt) continue;
        ComponentId cid = static_cast<ComponentId>(i);

        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, PhysicalButton>)
                c.process(physPressed(cid, physical), output, accumLeft, accumRight, accumGyro);
            else if constexpr (std::is_same_v<T, PhysicalDpadDir>)
                c.process(physPressed(cid, physical), output, accumLeft, accumRight, accumGyro);
            else if constexpr (std::is_same_v<T, PhysicalTrigger>)
                c.process(c.side == TriggerSide::L ? physical.triggerL : physical.triggerR,
                          output, accumLeft, accumRight, accumGyro);
            else if constexpr (std::is_same_v<T, PhysicalAnalogDir>)
                c.process(physHalfAxis(c.slot, physical), output, accumLeft, accumRight, accumGyro);
            else if constexpr (std::is_same_v<T, PhysicalTouchpad> ||
                               std::is_same_v<T, PhysicalGyro>)
                c.process(physical, output, accumLeft, accumRight, accumGyro);
        }, *opt);
    }

    accumLeft .flush(output.leftX,  output.leftY);
    accumRight.flush(output.rightX, output.rightY);
    accumGyro .flush(output.gyroX,  output.gyroY, output.gyroZ);
}
