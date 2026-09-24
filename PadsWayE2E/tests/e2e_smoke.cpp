#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include "e2e_checks.h"
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

void checkPassthrough(const PassthroughCase& c) {
    CAPTURE(c.name);
    checkPressGives(c.physical, c.expected);
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

// PS -> Home left out on purpose: a Home on the virtual pad makes Windows inject its Xbox Guide
// key (VK 0x07) for the Game Bar, and Home maps like any other button anyway. Same rule for the
// cases file — see PadsWayE2E/cases/README.md.
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
