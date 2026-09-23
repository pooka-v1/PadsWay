#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include "E2ESandbox.h"
#include "config/ConfigLoader.h"
#include "macros/MacroParser.h"
#include "Paths.h"
#include <catch2/catch_amalgamated.hpp>

// ─── Fase 0 — smoke: the sandbox DS4 entry exactly as shipped in controllers.json (no profile, no
// customization) must turn every physical component into its own Xbox counterpart. This is also
// the precondition for every mapping test: if a plain Cross doesn't come out as A, nothing built on
// top of it can be trusted.

namespace {

struct PassthroughCase {
    const char*  name;
    Ds4Input     physical;   // what the fake DS4 presses (DS4 positions, see E2EHarness.h)
    GamepadState expected;   // what must come out of the virtual Xbox pad — and nothing else
};

PassthroughCase makeCase(const char* name, void (*set)(GamepadState&)) {
    PassthroughCase c{ name, {}, {} };
    set(c.physical);
    set(c.expected);   // uncustomized DS4: output mirrors input one to one
    return c;
}

// Presses `physical`, waits for exactly `expected` on the virtual pad, then releases.
void checkPassthrough(const PassthroughCase& c) {
    CAPTURE(c.name);
    REQUIRE(harness().releaseAll());

    harness().press(c.physical);
    GamepadState seen;
    const bool matched = harness().waitForVirtual(
        [&](const GamepadState& s) { return sameVirtualOutput(s, c.expected); },
        E2EHarness::kWaitMs, &seen);
    const std::string expectedText = describe(c.expected);
    const std::string seenText     = describe(seen);
    CAPTURE(expectedText, seenText);
    CHECK(matched);

    CHECK(harness().releaseAll());
}

} // namespace

TEST_CASE("E2E smoke: engine runs on the fake DS4 with its shipped layout and no profile", "[e2e][smoke]") {
    const DeviceCandidate active = harness().engine().getActiveDevice();
    CHECK(active.vid == 0x054C);
    CHECK(active.pid == 0x09CC);
    CHECK(harness().engine().getActiveLayoutId() == "dualshock4");
    CHECK(harness().engine().getActiveProfileName().empty());
    CHECK(harness().releaseAll());
}

TEST_CASE("E2E smoke: sandbox macro library holds the X+Y combo macro, held 300 ms", "[e2e][smoke]") {
    // Read back through the same path the engine uses, so this also proves the sandbox is the
    // data folder the engine is actually looking at.
    const auto library = loadMacroLibrary(Paths::userData("data/macros.json"));
    REQUIRE(library.count(E2ESandbox::kComboMacroName) == 1);

    Macro macro;
    REQUIRE_NOTHROW(MacroParser::parse(library.at(E2ESandbox::kComboMacroName), macro));
    CHECK(macro.getMode() == MacroRepeatMode::Once);
    REQUIRE(macro.getSteps().size() == 1);
    const CompiledStep& step = macro.getSteps()[0];
    CHECK(step.effect.btnX);
    CHECK(step.effect.btnY);
    CHECK_FALSE(step.effect.btnA);
    CHECK(step.holdMs == 300);
}

TEST_CASE("E2E smoke: uncustomized DS4 buttons come out as their own Xbox buttons", "[e2e][smoke]") {
    const PassthroughCase cases[] = {
        makeCase("Cross -> A",       [](GamepadState& s) { s.btnA = true; }),
        makeCase("Circle -> B",      [](GamepadState& s) { s.btnB = true; }),
        makeCase("Square -> X",      [](GamepadState& s) { s.btnX = true; }),
        makeCase("Triangle -> Y",    [](GamepadState& s) { s.btnY = true; }),
        makeCase("L1 -> LB",         [](GamepadState& s) { s.btnLB = true; }),
        makeCase("R1 -> RB",         [](GamepadState& s) { s.btnRB = true; }),
        makeCase("Share -> Back",    [](GamepadState& s) { s.btnBack = true; }),
        makeCase("Options -> Start", [](GamepadState& s) { s.btnStart = true; }),
        makeCase("PS -> Home",       [](GamepadState& s) { s.btnHome = true; }),
        makeCase("L3 -> L3",         [](GamepadState& s) { s.btnL3 = true; }),
        makeCase("R3 -> R3",         [](GamepadState& s) { s.btnR3 = true; }),
    };
    for (const auto& c : cases) checkPassthrough(c);
}

TEST_CASE("E2E smoke: uncustomized DS4 dpad directions come out as the same directions", "[e2e][smoke]") {
    const PassthroughCase cases[] = {
        makeCase("Dpad up",    [](GamepadState& s) { s.dpadUp = true; }),
        makeCase("Dpad down",  [](GamepadState& s) { s.dpadDown = true; }),
        makeCase("Dpad left",  [](GamepadState& s) { s.dpadLeft = true; }),
        makeCase("Dpad right", [](GamepadState& s) { s.dpadRight = true; }),
        makeCase("Dpad up+right (diagonal)", [](GamepadState& s) { s.dpadUp = true; s.dpadRight = true; }),
    };
    for (const auto& c : cases) checkPassthrough(c);
}

TEST_CASE("E2E smoke: uncustomized DS4 triggers pass their analog value through", "[e2e][smoke]") {
    const PassthroughCase cases[] = {
        makeCase("L2 full",  [](GamepadState& s) { s.triggerL = 1.0f; }),
        makeCase("R2 full",  [](GamepadState& s) { s.triggerR = 1.0f; }),
        makeCase("L2 half",  [](GamepadState& s) { s.triggerL = 0.5f; }),
        makeCase("R2 half",  [](GamepadState& s) { s.triggerR = 0.5f; }),
    };
    for (const auto& c : cases) checkPassthrough(c);
}

TEST_CASE("E2E smoke: uncustomized DS4 sticks come out on the same stick and direction", "[e2e][smoke]") {
    const PassthroughCase cases[] = {
        makeCase("Left stick right",  [](GamepadState& s) { s.leftX = 1.0f; }),
        makeCase("Left stick left",   [](GamepadState& s) { s.leftX = -1.0f; }),
        makeCase("Left stick up",     [](GamepadState& s) { s.leftY = 1.0f; }),
        makeCase("Left stick down",   [](GamepadState& s) { s.leftY = -1.0f; }),
        makeCase("Right stick right", [](GamepadState& s) { s.rightX = 1.0f; }),
        makeCase("Right stick left",  [](GamepadState& s) { s.rightX = -1.0f; }),
        makeCase("Right stick up",    [](GamepadState& s) { s.rightY = 1.0f; }),
        makeCase("Right stick down",  [](GamepadState& s) { s.rightY = -1.0f; }),
    };
    for (const auto& c : cases) checkPassthrough(c);
}
