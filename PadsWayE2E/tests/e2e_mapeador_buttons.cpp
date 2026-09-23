#include "E2EMapping.h"   // first: defines NOMINMAX before any <windows.h>
#include "E2ESandbox.h"
#include "e2e_checks.h"
#include <catch2/catch_amalgamated.hpp>

// ─── Fase 1 — Mapeador (Normal mode), source = a physical BUTTON. One assignment per test case:
// assign -> save -> press -> check -> undo, then check the button is back to its shipped output
// (proves the undo too). Keyboard/mouse targets come with the low-level input hooks.

using E2EMapping::ScopedAssignment;

namespace {

// Fake DS4 presses (DS4 positions, see E2EHarness.h).
Ds4Input cross()    { return with([](GamepadState& s) { s.btnA = true; }); }
Ds4Input circle()   { return with([](GamepadState& s) { s.btnB = true; }); }
Ds4Input square()   { return with([](GamepadState& s) { s.btnX = true; }); }
Ds4Input triangle() { return with([](GamepadState& s) { s.btnY = true; }); }
Ds4Input l1()       { return with([](GamepadState& s) { s.btnLB = true; }); }
Ds4Input r1()       { return with([](GamepadState& s) { s.btnRB = true; }); }
Ds4Input options()  { return with([](GamepadState& s) { s.btnStart = true; }); }

} // namespace

TEST_CASE("Mapeador button -> other button: Cross gives B", "[e2e][mapeador][buttons]") {
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignVirtual(model, "a", "b");
        E2EMapping::saveNormalMode(model);

        checkPressGives(cross(), with([](GamepadState& s) { s.btnB = true; }));
    }
    checkPressGives(cross(), with([](GamepadState& s) { s.btnA = true; }));
}

TEST_CASE("Mapeador button -> other button: Options gives A", "[e2e][mapeador][buttons]") {
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignVirtual(model, "start", "a");
        E2EMapping::saveNormalMode(model);

        checkPressGives(options(), with([](GamepadState& s) { s.btnA = true; }));
    }
    checkPressGives(options(), with([](GamepadState& s) { s.btnStart = true; }));
}

TEST_CASE("Mapeador button -> dpad direction: Circle gives dpad up", "[e2e][mapeador][buttons]") {
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignVirtual(model, "b", "dpad_up");
        E2EMapping::saveNormalMode(model);

        checkPressGives(circle(), with([](GamepadState& s) { s.dpadUp = true; }));
    }
    checkPressGives(circle(), with([](GamepadState& s) { s.btnB = true; }));
}

TEST_CASE("Mapeador button -> trigger: Square gives R2 fully pressed", "[e2e][mapeador][buttons]") {
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignTrigger(model, "x", "r2");
        E2EMapping::saveNormalMode(model);

        checkPressGives(square(), with([](GamepadState& s) { s.triggerR = 1.0f; }));
    }
    checkPressGives(square(), with([](GamepadState& s) { s.btnX = true; }));
}

TEST_CASE("Mapeador button -> stick half-axis: Triangle gives right stick fully left",
          "[e2e][mapeador][buttons]") {
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignVirtual(model, "y", "right_x_neg");
        E2EMapping::saveNormalMode(model);

        checkPressGives(triangle(), with([](GamepadState& s) { s.rightX = -1.0f; }));
    }
    checkPressGives(triangle(), with([](GamepadState& s) { s.btnY = true; }));
}

TEST_CASE("Mapeador button -> macro: L1 plays the X+Y combo once for ~300 ms", "[e2e][mapeador][buttons]") {
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignAction(model, "l1", E2EMapping::macroAction(E2ESandbox::kComboMacroName));
        E2EMapping::saveNormalMode(model);

        checkHoldPlaysOnceMacro(l1(), with([](GamepadState& s) { s.btnX = true; s.btnY = true; }), 200, 600);
    }
    checkPressGives(l1(), with([](GamepadState& s) { s.btnLB = true; }));
}

TEST_CASE("Mapeador button -> bot: R1 toggles LightningBot on and off", "[e2e][mapeador][buttons]") {
    if (!E2EMapping::isBotLoaded(E2ESandbox::kTestBotName))
        SKIP("LightningBot.dll not loaded: build LightningBotDLL so it lands in PadsWay/data/bots/");
    {
        ScopedAssignment undo;
        MappingModel model = E2EMapping::openMapeador();
        E2EMapping::assignAction(model, "r1", E2EMapping::botAction(E2ESandbox::kTestBotName));
        E2EMapping::saveNormalMode(model);

        checkPressTogglesBot(r1(), E2ESandbox::kTestBotName);
    }
    checkPressGives(r1(), with([](GamepadState& s) { s.btnRB = true; }));
}
