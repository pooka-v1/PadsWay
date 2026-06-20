#define NOMINMAX
#include "Macro.h"
#include <algorithm>

void Macro::setup(std::vector<CompiledStep> steps,
                  MacroRepeatMode           mode,
                  int                       totalMs,
                  int                       cycleMs)
{
    m_steps   = std::move(steps);
    m_mode    = mode;
    m_totalMs = totalMs;
    m_cycleMs = cycleMs;
    m_active  = false;
}

void Macro::start() {
    m_active    = true;
    m_startTime = GetTickCount64();
}

void Macro::stop() {
    m_active = false;
}

void Macro::toggle() {
    if (m_active) stop();
    else          start();
}

bool Macro::tick(GamepadState& state) {
    if (!m_active || m_steps.empty() || m_cycleMs <= 0)
        return false;

    int elapsed = static_cast<int>(GetTickCount64() - m_startTime);

    // Check timed stop conditions
    if (m_mode == MacroRepeatMode::Once && elapsed >= m_cycleMs) {
        m_active = false;
        return false;
    }
    if (m_mode == MacroRepeatMode::TimedMs && elapsed >= m_totalMs) {
        m_active = false;
        return false;
    }

    // Position within the current cycle
    int pos = elapsed % m_cycleMs;

    // Find the step that owns this position and apply it
    for (const auto& step : m_steps) {
        if (pos >= step.startMs && pos < step.endMs) {
            if (pos < step.startMs + step.holdMs)
                applyEffect(step.effect, state);
            // else: within the step's slot but past the hold → buttons released (no-op)
            return true;
        }
    }

    return true;  // active but between slots (shouldn't happen with a well-formed timeline)
}

void Macro::applyEffect(const MacroEffect& e, GamepadState& state) const {
    // Buttons are OR'd so the player can still hold buttons simultaneously
    state.btnA      |= e.btnA;
    state.btnB      |= e.btnB;
    state.btnX      |= e.btnX;
    state.btnY      |= e.btnY;
    state.btnLB     |= e.btnL1;
    state.btnRB     |= e.btnR1;
    if (e.btnL2) state.triggerL = 1.0f;
    if (e.btnR2) state.triggerR = 1.0f;
    state.btnL3     |= e.btnL3;
    state.btnR3     |= e.btnR3;
    // When the macro step owns the dpad, assign directly so physical input is suppressed.
    // This lets e.g. CD=30 crouch cleanly even if the player is pressing left or right.
    // When no dpad tokens are present, OR so physical dpad passes through unaffected.
    if (e.hasDpad) {
        state.dpadUp    = e.dpadU;
        state.dpadDown  = e.dpadD;
        state.dpadLeft  = e.dpadL;
        state.dpadRight = e.dpadR;
    } else {
        state.dpadUp    |= e.dpadU;
        state.dpadDown  |= e.dpadD;
        state.dpadLeft  |= e.dpadL;
        state.dpadRight |= e.dpadR;
    }
    state.btnStart  |= e.btnSt;
    state.btnBack   |= e.btnSe;

    // Analog sticks: accumulate per half-axis (same logic as StickAccumulator) so physical
    // input on the perpendicular axis is preserved when the macro only sets one direction.
    // A macro token LAY-1.0 sets leftX=0, leftY=-1; with accumulation leftX stays at whatever
    // the physical controller contributed, giving a correct diagonal instead of killing it.
    if (e.hasLeftStick) {
        if      (e.leftX > 0.0f) state.leftX = std::max(state.leftX, e.leftX);
        else if (e.leftX < 0.0f) state.leftX = std::min(state.leftX, e.leftX);
        if      (e.leftY > 0.0f) state.leftY = std::max(state.leftY, e.leftY);
        else if (e.leftY < 0.0f) state.leftY = std::min(state.leftY, e.leftY);
    }
    if (e.hasRightStick) {
        if      (e.rightX > 0.0f) state.rightX = std::max(state.rightX, e.rightX);
        else if (e.rightX < 0.0f) state.rightX = std::min(state.rightX, e.rightX);
        if      (e.rightY > 0.0f) state.rightY = std::max(state.rightY, e.rightY);
        else if (e.rightY < 0.0f) state.rightY = std::min(state.rightY, e.rightY);
    }
}
