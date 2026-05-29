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
