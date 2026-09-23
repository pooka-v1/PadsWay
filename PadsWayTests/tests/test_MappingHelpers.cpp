// MappingHelpers.h -> PadView.h pulls in <d3d11.h> (and with it <windows.h>) — NOMINMAX first
// (see CLAUDE.md, "Tests"). Only inline helpers are used here; nothing from PadView itself is linked.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/MappingHelpers.h"
#include <catch2/catch_amalgamated.hpp>

// ─── MappingHelpers — pure name-translation helpers shared by AppWindow and MappingEditor.
// Several of them keep their own copy of the same button table (shortToState/stateToShort,
// activateState/isStateActive), so most tests here check that those copies agree with each other.

namespace {
// Every digital source the Mapper knows by short name (see shortToState's table).
const std::vector<std::string> kShortButtonNames = {
    "a", "b", "x", "y", "l1", "r1", "select", "start", "home", "l3", "r3",
    "l4", "r4", "lp", "rp", "touch_btn", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
};
} // namespace

TEST_CASE("shortToState/stateToShort round-trip every known button", "[MappingHelpers]") {
    for (const auto& shortName : kShortButtonNames) {
        CAPTURE(shortName);
        const std::string stateName = shortToState(shortName);
        CHECK(stateName != shortName);                 // every entry is actually translated
        CHECK(stateToShort(stateName) == shortName);
    }
}

TEST_CASE("shortToState/stateToShort pass unknown names through unchanged", "[MappingHelpers]") {
    CHECK(shortToState("l2") == "l2");
    CHECK(stateToShort("triggerL") == "triggerL");
    CHECK(shortToState("") == "");
}

TEST_CASE("shortToState maps the non-obvious names", "[MappingHelpers]") {
    CHECK(shortToState("l1") == "btnLB");
    CHECK(shortToState("select") == "btnBack");
    CHECK(shortToState("touch_btn") == "btnTouch");
    CHECK(shortToState("dpad_left") == "dpadLeft");
}

TEST_CASE("activateState and isStateActive agree on every button state", "[MappingHelpers]") {
    for (const auto& shortName : kShortButtonNames) {
        const std::string stateName = shortToState(shortName);
        CAPTURE(stateName);
        GamepadState s;
        activateState(s, stateName);
        CHECK(isStateActive(s, stateName));
        // ...and it lit up only that one state.
        for (const auto& otherShort : kShortButtonNames) {
            const std::string other = shortToState(otherShort);
            if (other != stateName) {
                CAPTURE(other);
                CHECK_FALSE(isStateActive(s, other));
            }
        }
    }
}

TEST_CASE("activateState drives triggers to full; unknown names change nothing", "[MappingHelpers]") {
    GamepadState s;
    activateState(s, "triggerL");
    activateState(s, "triggerR");
    CHECK(s.triggerL == 1.0f);
    CHECK(s.triggerR == 1.0f);

    GamepadState untouched;
    activateState(untouched, "leftX");
    activateState(untouched, "nonsense");
    CHECK(untouched.leftX == 0.0f);
    CHECK_FALSE(isStateActive(untouched, "nonsense"));
}

TEST_CASE("dpadDirFromMouse picks the dominant axis, horizontal on a tie", "[MappingHelpers]") {
    CHECK(dpadDirFromMouse(ImVec2(110, 100), 100, 100) == "right");
    CHECK(dpadDirFromMouse(ImVec2(90, 102), 100, 100) == "left");
    CHECK(dpadDirFromMouse(ImVec2(101, 80), 100, 100) == "up");      // screen Y grows downwards
    CHECK(dpadDirFromMouse(ImVec2(99, 120), 100, 100) == "down");
    CHECK(dpadDirFromMouse(ImVec2(105, 95), 100, 100) == "right");   // |dx| == |dy|
    CHECK(dpadDirFromMouse(ImVec2(100, 100), 100, 100) == "right");  // dead center
}

TEST_CASE("stickIdsFromStateX and readStickXY resolve left/right sticks", "[MappingHelpers]") {
    CHECK(stickIdsFromStateX("leftX") == std::make_pair(std::string("left_x"), std::string("left_y")));
    CHECK(stickIdsFromStateX("rightX") == std::make_pair(std::string("right_x"), std::string("right_y")));
    CHECK(stickIdsFromStateX("gyroX").first.empty());

    GamepadState s;
    s.leftX = 0.1f; s.leftY = 0.2f; s.rightX = -0.3f; s.rightY = -0.4f;
    float x = 9.0f, y = 9.0f;
    readStickXY(s, "rightX", x, y);
    CHECK(x == -0.3f);
    CHECK(y == -0.4f);
    readStickXY(s, "unknown", x, y);
    CHECK(x == 0.0f);
    CHECK(y == 0.0f);
}

TEST_CASE("dpadDirToState returns the component's per-direction state", "[MappingHelpers]") {
    PadComponent dpad;
    dpad.stateUp = "dpadUp"; dpad.stateDown = "dpadDown";
    dpad.stateLeft = "dpadLeft"; dpad.stateRight = "dpadRight";

    CHECK(dpadDirToState(dpad, "up") == "dpadUp");
    CHECK(dpadDirToState(dpad, "right") == "dpadRight");
    CHECK(dpadDirToState(dpad, "diagonal").empty());
}

TEST_CASE("xboxBtnLabel labels shoulders/menu buttons and echoes anything else", "[MappingHelpers]") {
    CHECK(std::string(xboxBtnLabel("l1")) == "LB");
    CHECK(std::string(xboxBtnLabel("select")) == "Select");
    const std::string paddle = "lp";
    CHECK(std::string(xboxBtnLabel(paddle)) == "lp");
}

TEST_CASE("findCompByState matches button state, stick click and any dpad direction", "[MappingHelpers]") {
    PadLayout layout;
    PadComponent bg;    bg.type = "template";
    PadComponent btnA;  btnA.type = "button"; btnA.state = "btnA";
    PadComponent stick; stick.type = "stick"; stick.stateX = "leftX"; stick.stateClick = "btnL3";
    PadComponent dpad;  dpad.type = "dpad";
    layout.components = { bg, btnA, stick, dpad };

    CHECK(findCompByState(layout, "btnA") == 1);
    CHECK(findCompByState(layout, "btnL3") == 2);
    CHECK(findCompByState(layout, "dpadLeft") == 3);
    CHECK(findCompByState(layout, "btnB") == -1);
}

TEST_CASE("findCompByState: stateClick only counts on stick components", "[MappingHelpers]") {
    PadLayout layout;
    PadComponent deco; deco.type = "decoration"; deco.stateClick = "btnR3";
    layout.components = { deco };
    CHECK(findCompByState(layout, "btnR3") == -1);
}
