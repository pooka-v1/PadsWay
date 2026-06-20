#pragma once
#include <vector>
#include <windows.h>
#include "../GamepadState.h"

// ---- Effect: what a single step writes to the controller state ----
// Buttons are OR'd into the existing state; sticks override only if hasXxxStick is true.
struct MacroEffect {
    bool  btnA  = false, btnB   = false, btnX  = false, btnY  = false;
    bool  btnL1 = false, btnR1  = false;
    bool  btnL2 = false, btnR2  = false;   // will set triggerL/R = 1.0
    bool  btnL3 = false, btnR3  = false;
    bool  dpadU = false, dpadD  = false, dpadL = false, dpadR = false;
    bool  btnSt = false, btnSe  = false;   // Start, Select/Back

    // When true, applyEffect assigns dpad bits (overrides physical input).
    // Set automatically when any dpad token is present in a macro step.
    bool hasDpad = false;

    float leftX  = 0.0f, leftY  = 0.0f;
    float rightX = 0.0f, rightY = 0.0f;

    // True when this step explicitly controls these axes.
    // Needed to distinguish "set to 0 (center)" from "not set".
    bool hasLeftStick  = false;
    bool hasRightStick = false;
};

// ---- One compiled step in the macro timeline (all offsets from cycle start) ----
//
//  |<-- holdMs -->|<-- gap -->|
//  startMs                    endMs
//  ^effect applied             ^next step begins
//
struct CompiledStep {
    int         startMs = 0;
    int         holdMs  = 0;   // how long the effect is active
    int         endMs   = 0;   // start of next step (= startMs + slot)
    MacroEffect effect;
};

// ---- Repeat mode: determines when/how the macro stops ----
enum class MacroRepeatMode {
    Once,           // run one cycle then stop automatically
    TimedMs,        // loop cycles until totalMs have elapsed, then stop
    UntilRelease,   // loop until stop() is called (button released)
    Toggle          // loop until toggle() is called again
};

// ---- Macro: runtime executor ----
class Macro {
public:
    Macro() = default;

    // Called once after parsing. Sets up the compiled timeline.
    void setup(std::vector<CompiledStep> steps,
               MacroRepeatMode           mode,
               int                       totalMs,   // for TimedMs: total run time (ms)
               int                       cycleMs);  // duration of one full cycle (ms)

    void start();      // activate (begin from t=0)
    void stop();       // deactivate
    void toggle();     // start if stopped, stop if running

    bool            isActive() const { return m_active;  }
    MacroRepeatMode getMode()  const { return m_mode;    }
    int             getTotalMs() const { return m_totalMs; }
    int             getCycleMs() const { return m_cycleMs; }
    const std::vector<CompiledStep>& getSteps() const { return m_steps; }

    // Advances the macro and ORs/overrides its effects into state.
    // Returns true while the macro was active this tick.
    // Sets isActive()=false automatically when a timed run ends.
    bool tick(GamepadState& state);

private:
    std::vector<CompiledStep> m_steps;
    MacroRepeatMode           m_mode     = MacroRepeatMode::Once;
    int                       m_totalMs  = 0;
    int                       m_cycleMs  = 0;

    bool      m_active    = false;
    ULONGLONG m_startTime = 0;

    void applyEffect(const MacroEffect& e, GamepadState& state) const;
};
