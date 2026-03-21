#include "Macro.h"

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
    state.dpadUp    |= e.dpadU;
    state.dpadDown  |= e.dpadD;
    state.dpadLeft  |= e.dpadL;
    state.dpadRight |= e.dpadR;
    state.btnStart  |= e.btnSt;
    state.btnBack   |= e.btnSe;

    // Analog sticks override the real stick only when this step explicitly sets them
    if (e.hasLeftStick)  { state.leftX  = e.leftX;  state.leftY  = e.leftY;  }
    if (e.hasRightStick) { state.rightX = e.rightX; state.rightY = e.rightY; }
}
