#ifndef NOMINMAX
#define NOMINMAX   // RawHIDReader.h pulls in <windows.h> via HIDDevice.h — see CLAUDE.md, "Tests"
#endif
#include "input/RawHIDReader.h"
#include <catch2/catch_amalgamated.hpp>

// RawHIDState is the canonical input the mapping reads from (ARCHITECTURE.md, "Protocolos de
// mando" → principios, rule 4). HIDInputSource trusts hasAxis()/axis() to pick the right field and
// to skip axes the report didn't carry — a slot/field mix-up here would silently swap sticks.

namespace {
// Distinct value per field, so a wrong slot → field mapping can't pass by accident.
RawHIDState allAxesDistinct() {
    RawHIDState s;
    s.axisX = 0.1f; s.axisY = 0.2f; s.axisZ = 0.3f; s.axisRx = 0.4f;
    s.axisRy = 0.5f; s.axisRz = 0.6f; s.axisBrake = 0.7f; s.axisAccel = 0.8f;
    return s;
}
} // namespace

TEST_CASE("RawHIDState::axis returns the field of each slot", "[RawHIDState]") {
    const RawHIDState s = allAxesDistinct();
    CHECK(s.axis(RawAxis::X)     == 0.1f);
    CHECK(s.axis(RawAxis::Y)     == 0.2f);
    CHECK(s.axis(RawAxis::Z)     == 0.3f);
    CHECK(s.axis(RawAxis::Rx)    == 0.4f);
    CHECK(s.axis(RawAxis::Ry)    == 0.5f);
    CHECK(s.axis(RawAxis::Rz)    == 0.6f);
    CHECK(s.axis(RawAxis::Brake) == 0.7f);
    CHECK(s.axis(RawAxis::Accel) == 0.8f);
}

TEST_CASE("RawHIDState::hasAxis reads one bit per slot, in enum order", "[RawHIDState]") {
    RawHIDState s;
    SECTION("default state carries nothing") {
        for (int i = 0; i <= static_cast<int>(RawAxis::Accel); ++i)
            CHECK_FALSE(s.hasAxis(static_cast<RawAxis>(i)));
        CHECK_FALSE(s.buttonsValid);
    }
    SECTION("bit N is slot N") {
        s.axisMask = static_cast<uint16_t>((1u << static_cast<int>(RawAxis::Rz))
                                         | (1u << static_cast<int>(RawAxis::Brake)));
        CHECK(s.hasAxis(RawAxis::Rz));
        CHECK(s.hasAxis(RawAxis::Brake));
        CHECK_FALSE(s.hasAxis(RawAxis::Z));
        CHECK_FALSE(s.hasAxis(RawAxis::Accel));
        CHECK_FALSE(s.hasAxis(RawAxis::X));
    }
}

TEST_CASE("RawHIDState: an absent axis keeps its stale value but reports not present", "[RawHIDState]") {
    // decodeRawHIDReport() leaves a field untouched when the report doesn't carry it — consumers
    // must go through hasAxis(), never read the field blindly.
    RawHIDState s = allAxesDistinct();
    s.axisMask = static_cast<uint16_t>(1u << static_cast<int>(RawAxis::X));
    CHECK(s.hasAxis(RawAxis::X));
    CHECK_FALSE(s.hasAxis(RawAxis::Y));
    CHECK(s.axis(RawAxis::Y) == 0.2f);   // stale value still there — hence the flag
}
