#include <catch2/catch_amalgamated.hpp>
#include "macros/Macro.h"

TEST_CASE("Macro default construction: inactive, empty steps, Once mode", "[Macro]") {
    Macro macro;
    REQUIRE(macro.isActive()   == false);
    REQUIRE(macro.getSteps().empty());
    REQUIRE(macro.getMode()    == MacroRepeatMode::Once);
    REQUIRE(macro.getTotalMs() == 0);
    REQUIRE(macro.getCycleMs() == 0);
}

TEST_CASE("Macro::setup populates all getters", "[Macro]") {
    Macro macro;
    CompiledStep step;
    step.startMs     = 0;
    step.holdMs      = 80;
    step.endMs       = 200;
    step.effect.btnA = true;

    macro.setup({step}, MacroRepeatMode::Once, 0, 200);

    REQUIRE(macro.getSteps().size() == 1);
    CHECK(macro.getSteps()[0].effect.btnA == true);
    CHECK(macro.getSteps()[0].holdMs      == 80);
    CHECK(macro.getSteps()[0].startMs     == 0);
    CHECK(macro.getSteps()[0].endMs       == 200);
    REQUIRE(macro.getMode()    == MacroRepeatMode::Once);
    REQUIRE(macro.getTotalMs() == 0);
    REQUIRE(macro.getCycleMs() == 200);
}

TEST_CASE("Macro::setup TimedMs stores totalMs and cycleMs", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::TimedMs, 5000, 400);
    REQUIRE(macro.getMode()    == MacroRepeatMode::TimedMs);
    REQUIRE(macro.getTotalMs() == 5000);
    REQUIRE(macro.getCycleMs() == 400);
}

TEST_CASE("Macro::setup UntilRelease mode", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::UntilRelease, 0, 200);
    REQUIRE(macro.getMode() == MacroRepeatMode::UntilRelease);
}

TEST_CASE("Macro::setup Toggle mode", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Toggle, 0, 200);
    REQUIRE(macro.getMode() == MacroRepeatMode::Toggle);
}

TEST_CASE("Macro::setup replaces prior state on second call", "[Macro]") {
    Macro macro;
    CompiledStep s1, s2;
    s1.effect.btnA = true; s1.endMs = 200;
    s2.effect.btnB = true; s2.startMs = 200; s2.endMs = 400;

    macro.setup({s1}, MacroRepeatMode::Once, 0, 200);
    macro.setup({s1, s2}, MacroRepeatMode::TimedMs, 3000, 400);

    REQUIRE(macro.getSteps().size() == 2);
    CHECK(macro.getSteps()[0].effect.btnA == true);
    CHECK(macro.getSteps()[1].effect.btnB == true);
    REQUIRE(macro.getMode()    == MacroRepeatMode::TimedMs);
    REQUIRE(macro.getTotalMs() == 3000);
    REQUIRE(macro.getCycleMs() == 400);
}

TEST_CASE("Macro::setup preserves all steps and effects", "[Macro]") {
    Macro macro;
    CompiledStep s1, s2, s3;
    s1.effect.btnA = true;  s1.startMs = 0;   s1.holdMs = 80; s1.endMs = 200;
    s2.effect.btnB = true;  s2.startMs = 200; s2.holdMs = 80; s2.endMs = 400;
    s3.effect.btnX = true;  s3.startMs = 400; s3.holdMs = 80; s3.endMs = 600;

    macro.setup({s1, s2, s3}, MacroRepeatMode::Once, 0, 600);

    REQUIRE(macro.getSteps().size() == 3);
    CHECK(macro.getSteps()[0].effect.btnA == true);
    CHECK(macro.getSteps()[1].effect.btnB == true);
    CHECK(macro.getSteps()[2].effect.btnX == true);
    REQUIRE(macro.getCycleMs() == 600);
}

TEST_CASE("Macro::setup does not activate macro", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Once, 0, 200);
    REQUIRE(macro.isActive() == false);
}

TEST_CASE("Macro::start activates macro", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Once, 0, 200);
    macro.start();
    REQUIRE(macro.isActive() == true);
}

TEST_CASE("Macro::stop deactivates macro", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Once, 0, 200);
    macro.start();
    macro.stop();
    REQUIRE(macro.isActive() == false);
}

TEST_CASE("Macro::toggle starts inactive macro", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Toggle, 0, 200);
    REQUIRE(macro.isActive() == false);
    macro.toggle();
    REQUIRE(macro.isActive() == true);
}

TEST_CASE("Macro::toggle stops active macro", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Toggle, 0, 200);
    macro.start();
    macro.toggle();
    REQUIRE(macro.isActive() == false);
}

TEST_CASE("Macro::tick returns false when inactive", "[Macro]") {
    Macro macro;
    CompiledStep step;
    step.startMs = 0; step.holdMs = 80; step.endMs = 200;
    step.effect.btnA = true;
    macro.setup({step}, MacroRepeatMode::Once, 0, 200);
    GamepadState state;
    REQUIRE(macro.tick(state) == false);
    REQUIRE(state.btnA == false);
}

TEST_CASE("Macro::tick returns false for empty steps even when started", "[Macro]") {
    Macro macro;
    macro.setup({}, MacroRepeatMode::Once, 0, 200);
    macro.start();
    GamepadState state;
    REQUIRE(macro.tick(state) == false);
}

// ─── Macro::tick applying a step's effect (applyEffect). Timing is real (GetTickCount64), so every
// macro below uses a 60 s cycle whose single step starts at 0: the first tick right after start()
// always lands inside that step, which keeps these tests deterministic.

namespace {
constexpr int kLongCycleMs = 60000;

Macro startedMacroWith(const MacroEffect& effect, int holdMs = kLongCycleMs) {
    CompiledStep step;
    step.startMs = 0;
    step.holdMs  = holdMs;
    step.endMs   = kLongCycleMs;
    step.effect  = effect;
    Macro macro;
    macro.setup({step}, MacroRepeatMode::Toggle, 0, kLongCycleMs);
    macro.start();
    return macro;
}
} // namespace

TEST_CASE("Macro::tick ORs buttons into the physical state instead of replacing it", "[Macro]") {
    MacroEffect effect;
    effect.btnA = true; effect.btnL1 = true; effect.btnSt = true;
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    state.btnB = true;                 // player is holding B
    REQUIRE(macro.tick(state) == true);

    CHECK(state.btnA);
    CHECK(state.btnLB);
    CHECK(state.btnStart);
    CHECK(state.btnB);                 // physical input survives
}

TEST_CASE("Macro::tick L2/R2 tokens drive the triggers to full", "[Macro]") {
    MacroEffect effect;
    effect.btnL2 = true;
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    state.triggerR = 0.3f;
    macro.tick(state);

    CHECK(state.triggerL == 1.0f);
    CHECK(state.triggerR == 0.3f);     // not part of the effect: untouched
}

TEST_CASE("Macro::tick a step that owns the dpad overrides the physical dpad", "[Macro]") {
    MacroEffect effect;
    effect.hasDpad = true;
    effect.dpadD   = true;
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    state.dpadLeft = true;             // player pressing left while the macro crouches
    macro.tick(state);

    CHECK(state.dpadDown);
    CHECK_FALSE(state.dpadLeft);
}

TEST_CASE("Macro::tick a step without dpad tokens lets the physical dpad through", "[Macro]") {
    MacroEffect effect;
    effect.btnX = true;                // hasDpad stays false
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    state.dpadLeft = true;
    macro.tick(state);

    CHECK(state.dpadLeft);
    CHECK(state.btnX);
}

TEST_CASE("Macro::tick stick effect only pushes the half-axes it sets", "[Macro]") {
    MacroEffect effect;
    effect.hasLeftStick = true;
    effect.leftX = 0.0f;               // e.g. LAY-1.0: X not driven
    effect.leftY = -1.0f;
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    state.leftX = 0.5f;                // physical X survives -> correct diagonal
    macro.tick(state);

    CHECK(state.leftX == 0.5f);
    CHECK(state.leftY == -1.0f);
}

TEST_CASE("Macro::tick stick effect never pulls an axis back towards center", "[Macro]") {
    MacroEffect effect;
    effect.hasRightStick = true;
    effect.rightX = 0.5f;
    effect.rightY = -0.5f;
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    state.rightX = 0.8f;               // already further in the same direction
    state.rightY = 0.4f;               // opposite direction: macro wins
    macro.tick(state);

    CHECK(state.rightX == 0.8f);
    CHECK(state.rightY == -0.5f);
}

TEST_CASE("Macro::tick stick values are ignored unless the step owns that stick", "[Macro]") {
    MacroEffect effect;
    effect.leftX = 1.0f;               // hasLeftStick stays false
    Macro macro = startedMacroWith(effect);

    GamepadState state;
    macro.tick(state);

    CHECK(state.leftX == 0.0f);
}

TEST_CASE("Macro::tick inside a step's slot but past its hold applies nothing", "[Macro]") {
    MacroEffect effect;
    effect.btnA = true;
    Macro macro = startedMacroWith(effect, 0);   // hold 0 ms: always "released" part of the slot

    GamepadState state;
    CHECK(macro.tick(state) == true);  // still running
    CHECK_FALSE(state.btnA);
}

TEST_CASE("Macro::tick TimedMs with totalMs 0 stops on its first tick", "[Macro]") {
    CompiledStep step;
    step.endMs = 200; step.holdMs = 80; step.effect.btnA = true;
    Macro macro;
    macro.setup({step}, MacroRepeatMode::TimedMs, 0, 200);
    macro.start();

    GamepadState state;
    CHECK(macro.tick(state) == false);
    CHECK_FALSE(macro.isActive());
    CHECK_FALSE(state.btnA);
}

TEST_CASE("Macro::tick with a zero-length cycle does nothing but stays active", "[Macro]") {
    CompiledStep step;
    step.effect.btnA = true;
    Macro macro;
    macro.setup({step}, MacroRepeatMode::Toggle, 0, 0);
    macro.start();

    GamepadState state;
    CHECK(macro.tick(state) == false);
    CHECK(macro.isActive());
    CHECK_FALSE(state.btnA);
}