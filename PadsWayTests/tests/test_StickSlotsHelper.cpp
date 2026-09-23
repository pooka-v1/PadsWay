#include "input/StickSlotsHelper.h"
#include <catch2/catch_amalgamated.hpp>

// ─── stick_slots: digital/trigger sources driving a virtual stick half-axis (e.g. dpad -> left
// stick, L2 -> right_y_neg). stickSlotSourceValue() reads one source from the physical state;
// applyStickSlots() merges every slot into the axes-driven state, normalizes the resulting vector
// and suppresses dpad/trigger sources that were consumed as slots.

using Catch::Approx;

// ─── stickSlotSourceValue ───────────────────────────────────────────────────

TEST_CASE("stickSlotSourceValue reads buttons as 0/1 using the short physical names", "[StickSlots]") {
    GamepadState phys;
    phys.btnLB   = true;   // "l1"
    phys.btnBack = true;   // "select"
    phys.dpadLeft = true;
    phys.btnRP   = true;

    CHECK(stickSlotSourceValue("l1", phys, false, false) == 1.0f);
    CHECK(stickSlotSourceValue("select", phys, false, false) == 1.0f);
    CHECK(stickSlotSourceValue("dpad_left", phys, false, false) == 1.0f);
    CHECK(stickSlotSourceValue("rp", phys, false, false) == 1.0f);
    CHECK(stickSlotSourceValue("r1", phys, false, false) == 0.0f);
    CHECK(stickSlotSourceValue("dpad_up", phys, false, false) == 0.0f);
}

TEST_CASE("stickSlotSourceValue unknown source reads 0", "[StickSlots]") {
    GamepadState phys;
    phys.btnA = true;
    CHECK(stickSlotSourceValue("btnA", phys, false, false) == 0.0f);   // state name, not short name
    CHECK(stickSlotSourceValue("", phys, false, false) == 0.0f);
}

TEST_CASE("stickSlotSourceValue trigger without ranges is analog", "[StickSlots]") {
    GamepadState phys;
    phys.triggerL = 0.3f;
    phys.triggerR = 0.8f;
    CHECK(stickSlotSourceValue("l2", phys, false, false) == Approx(0.3f));
    CHECK(stickSlotSourceValue("r2", phys, false, false) == Approx(0.8f));
}

TEST_CASE("stickSlotSourceValue trigger with ranges is digital at the 0.5 threshold", "[StickSlots]") {
    GamepadState phys;
    phys.triggerL = 0.5f;    // not strictly above the threshold
    phys.triggerR = 0.51f;
    CHECK(stickSlotSourceValue("l2", phys, true, true) == 0.0f);
    CHECK(stickSlotSourceValue("r2", phys, true, true) == 1.0f);
    // Each side only looks at its own ranges flag.
    CHECK(stickSlotSourceValue("l2", phys, false, true) == Approx(0.5f));
}

// ─── applyStickSlots ────────────────────────────────────────────────────────

namespace {
ControllerConfig configWithSlots(std::unordered_map<std::string, std::vector<std::string>> slots) {
    ControllerConfig cfg;
    cfg.stickSlots = std::move(slots);
    return cfg;
}
} // namespace

TEST_CASE("applyStickSlots with no slots leaves the state untouched", "[StickSlots]") {
    ControllerConfig cfg;
    GamepadState phys; phys.dpadUp = true;
    GamepadState state; state.dpadUp = true; state.leftX = 0.4f;

    applyStickSlots(cfg, phys, state);

    CHECK(state.dpadUp == true);
    CHECK(state.leftX == 0.4f);
}

TEST_CASE("applyStickSlots pressed button source drives its half-axis to full", "[StickSlots]") {
    auto cfg = configWithSlots({ { "left_x_neg", { "x" } } });
    GamepadState phys; phys.btnX = true;
    GamepadState state;

    applyStickSlots(cfg, phys, state);

    CHECK(state.leftX == Approx(-1.0f));
    CHECK(state.leftY == Approx(0.0f));
}

TEST_CASE("applyStickSlots released source keeps the axes-driven value", "[StickSlots]") {
    auto cfg = configWithSlots({ { "left_x_pos", { "a" } } });
    GamepadState phys;                       // "a" not pressed
    GamepadState state; state.leftX = -0.4f; state.leftY = 0.25f;

    applyStickSlots(cfg, phys, state);

    CHECK(state.leftX == Approx(-0.4f));
    CHECK(state.leftY == Approx(0.25f));
}

TEST_CASE("applyStickSlots OR-max: the higher of axis and slot wins per half", "[StickSlots]") {
    auto cfg = configWithSlots({ { "right_y_pos", { "l2" } } });   // analog: no trigger ranges
    GamepadState phys; phys.triggerL = 0.2f;
    GamepadState state; state.rightY = 0.6f;

    applyStickSlots(cfg, phys, state);
    CHECK(state.rightY == Approx(0.6f));     // axis 0.6 beats slot 0.2

    phys.triggerL = 0.9f;
    state.rightY = 0.6f;
    applyStickSlots(cfg, phys, state);
    CHECK(state.rightY == Approx(0.9f));     // slot 0.9 beats axis 0.6
}

TEST_CASE("applyStickSlots any of several sources in one slot drives it", "[StickSlots]") {
    auto cfg = configWithSlots({ { "left_y_pos", { "a", "dpad_up" } } });
    GamepadState phys; phys.dpadUp = true;
    GamepadState state;

    applyStickSlots(cfg, phys, state);

    CHECK(state.leftY == Approx(1.0f));
}

TEST_CASE("applyStickSlots opposing halves cancel out", "[StickSlots]") {
    auto cfg = configWithSlots({ { "left_x_pos", { "b" } }, { "left_x_neg", { "x" } } });
    GamepadState phys; phys.btnB = true; phys.btnX = true;
    GamepadState state;

    applyStickSlots(cfg, phys, state);

    CHECK(state.leftX == Approx(0.0f));
}

TEST_CASE("applyStickSlots diagonal from two slots is normalized to unit length", "[StickSlots]") {
    auto cfg = configWithSlots({ { "left_x_pos", { "dpad_right" } }, { "left_y_pos", { "dpad_up" } } });
    GamepadState phys; phys.dpadRight = true; phys.dpadUp = true;
    GamepadState state;

    applyStickSlots(cfg, phys, state);

    CHECK(state.leftX == Approx(0.70710678f));
    CHECK(state.leftY == Approx(0.70710678f));
}

TEST_CASE("applyStickSlots a right-stick slot never touches the left stick", "[StickSlots]") {
    auto cfg = configWithSlots({ { "right_x_pos", { "a" } } });
    GamepadState phys; phys.btnA = true;
    GamepadState state; state.leftX = 0.9f; state.leftY = 0.9f;   // magnitude > 1, left untouched

    applyStickSlots(cfg, phys, state);

    CHECK(state.rightX == Approx(1.0f));
    CHECK(state.leftX == Approx(0.9f));
    CHECK(state.leftY == Approx(0.9f));
}

TEST_CASE("applyStickSlots suppresses dpad and trigger sources consumed as slots", "[StickSlots]") {
    auto cfg = configWithSlots({ { "left_y_pos", { "dpad_up" } }, { "right_y_neg", { "r2" } } });
    GamepadState phys; phys.dpadUp = true; phys.triggerR = 1.0f;
    GamepadState state; state.dpadUp = true; state.dpadDown = true; state.triggerR = 1.0f;

    applyStickSlots(cfg, phys, state);

    CHECK(state.dpadUp == false);      // consumed as slot source
    CHECK(state.dpadDown == true);     // not a slot source
    CHECK(state.triggerR == 0.0f);
    CHECK(state.rightY == Approx(-1.0f));
}

TEST_CASE("applyStickSlots does not suppress button sources at the state level", "[StickSlots]") {
    // Button sources are suppressed per input source earlier in the pipeline, not here — so an
    // unrelated button remapped onto the same virtual target keeps working.
    auto cfg = configWithSlots({ { "left_x_pos", { "a" } } });
    GamepadState phys; phys.btnA = true;
    GamepadState state; state.btnA = true;

    applyStickSlots(cfg, phys, state);

    CHECK(state.btnA == true);
}
