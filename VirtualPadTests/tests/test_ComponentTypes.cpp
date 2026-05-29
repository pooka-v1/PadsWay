#include <catch2/catch_amalgamated.hpp>
#include "input/ComponentTypes.h"
#include <cmath>

// ─── StickAccumulator::flush() ───────────────────────────────────────────────

TEST_CASE("StickAccumulator::flush neutral → (0, 0)", "[ComponentTypes]") {
    StickAccumulator acc;
    float x = 99.f, y = 99.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(0.0f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush full right → (1, 0)", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.xPos = 1.0f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(1.0f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush full left → (-1, 0)", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.xNeg = 1.0f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(-1.0f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush full up → (0, 1)", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.yPos = 1.0f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(0.0f));
    REQUIRE(y == Catch::Approx(1.0f));
}

TEST_CASE("StickAccumulator::flush full down → (0, -1)", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.yNeg = 1.0f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(0.0f));
    REQUIRE(y == Catch::Approx(-1.0f));
}

TEST_CASE("StickAccumulator::flush opposing X cancel to zero", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.xPos = 1.0f;
    acc.xNeg = 1.0f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(0.0f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush diagonal normalised to unit length", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.xPos = 1.0f;
    acc.yPos = 1.0f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    float mag = std::sqrt(x * x + y * y);
    REQUIRE(mag == Catch::Approx(1.0f).epsilon(0.001f));
    REQUIRE(x   == Catch::Approx(1.0f / std::sqrt(2.0f)).epsilon(0.001f));
    REQUIRE(y   == Catch::Approx(1.0f / std::sqrt(2.0f)).epsilon(0.001f));
}

TEST_CASE("StickAccumulator::flush sub-unit value passes through without normalisation", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.xPos = 0.5f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(0.5f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush partial opposing X results in net difference", "[ComponentTypes]") {
    StickAccumulator acc;
    acc.xPos = 0.8f;
    acc.xNeg = 0.3f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y);
    REQUIRE(x == Catch::Approx(0.5f));
    REQUIRE(y == Catch::Approx(0.0f));
}

// ─── GyroAccumulator::flush() ────────────────────────────────────────────────

TEST_CASE("GyroAccumulator::flush neutral → (0, 0, 0)", "[ComponentTypes]") {
    GyroAccumulator acc;
    float x = 99.f, y = 99.f, z = 99.f;
    acc.flush(x, y, z);
    REQUIRE(x == Catch::Approx(0.0f));
    REQUIRE(y == Catch::Approx(0.0f));
    REQUIRE(z == Catch::Approx(0.0f));
}

TEST_CASE("GyroAccumulator::flush positive X axis", "[ComponentTypes]") {
    GyroAccumulator acc;
    acc.xPos = 0.7f;
    float x = 0.f, y = 0.f, z = 0.f;
    acc.flush(x, y, z);
    REQUIRE(x == Catch::Approx(0.7f));
    REQUIRE(y == Catch::Approx(0.0f));
    REQUIRE(z == Catch::Approx(0.0f));
}

TEST_CASE("GyroAccumulator::flush opposing axes cancel", "[ComponentTypes]") {
    GyroAccumulator acc;
    acc.xPos = 0.6f;
    acc.xNeg = 0.6f;
    float x = 0.f, y = 0.f, z = 0.f;
    acc.flush(x, y, z);
    REQUIRE(x == Catch::Approx(0.0f));
}

TEST_CASE("GyroAccumulator::flush clamps positive overflow to 1.0", "[ComponentTypes]") {
    GyroAccumulator acc;
    acc.xPos = 1.5f;
    float x = 0.f, y = 0.f, z = 0.f;
    acc.flush(x, y, z);
    REQUIRE(x == Catch::Approx(1.0f));
}

TEST_CASE("GyroAccumulator::flush clamps negative overflow to -1.0", "[ComponentTypes]") {
    GyroAccumulator acc;
    acc.xNeg = 1.5f;
    float x = 0.f, y = 0.f, z = 0.f;
    acc.flush(x, y, z);
    REQUIRE(x == Catch::Approx(-1.0f));
}

TEST_CASE("GyroAccumulator::flush all three axes independently", "[ComponentTypes]") {
    GyroAccumulator acc;
    acc.xPos = 0.3f;
    acc.yNeg = 0.5f;
    acc.zPos = 0.8f;
    float x = 0.f, y = 0.f, z = 0.f;
    acc.flush(x, y, z);
    REQUIRE(x == Catch::Approx(0.3f));
    REQUIRE(y == Catch::Approx(-0.5f));
    REQUIRE(z == Catch::Approx(0.8f));
}

TEST_CASE("GyroAccumulator::flush net negative after partial cancel", "[ComponentTypes]") {
    GyroAccumulator acc;
    acc.yPos = 0.2f;
    acc.yNeg = 0.7f;
    float x = 0.f, y = 0.f, z = 0.f;
    acc.flush(x, y, z);
    REQUIRE(y == Catch::Approx(-0.5f));
}

// ─── PhysicalButton::process() ───────────────────────────────────────────────

TEST_CASE("PhysicalButton::process not pressed → no output change", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 1;
    btn.target = VirtualButton{ButtonId::A};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(false, out, left, right, gyro);

    REQUIRE(out.btnA == false);
}

TEST_CASE("PhysicalButton::process pressed → VirtualButton A", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 1;
    btn.target = VirtualButton{ButtonId::A};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.btnA == true);
    CHECK(out.btnB == false);
    CHECK(out.btnX == false);
    CHECK(out.btnY == false);
}

TEST_CASE("PhysicalButton::process pressed → VirtualButton B", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 2;
    btn.target = VirtualButton{ButtonId::B};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.btnB == true);
    REQUIRE(out.btnA == false);
}

TEST_CASE("PhysicalButton::process pressed → VirtualButton LB", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 3;
    btn.target = VirtualButton{ButtonId::LB};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.btnLB == true);
}

TEST_CASE("PhysicalButton::process pressed → VirtualButton Start", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 4;
    btn.target = VirtualButton{ButtonId::Start};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.btnStart == true);
}

TEST_CASE("PhysicalButton::process pressed → VirtualDpadDir Up", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 5;
    btn.target = VirtualDpadDir{DpadDir::Up};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.dpadUp    == true);
    CHECK(out.dpadDown  == false);
    CHECK(out.dpadLeft  == false);
    CHECK(out.dpadRight == false);
}

TEST_CASE("PhysicalButton::process pressed → VirtualDpadDir Right", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 6;
    btn.target = VirtualDpadDir{DpadDir::Right};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.dpadRight == true);
    REQUIRE(out.dpadUp    == false);
}

TEST_CASE("PhysicalButton::process pressed → VirtualTrigger L set to 1.0", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 7;
    btn.target = VirtualTrigger{TriggerSide::L};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.triggerL == Catch::Approx(1.0f));
    REQUIRE(out.triggerR == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalButton::process pressed → VirtualTrigger R set to 1.0", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 8;
    btn.target = VirtualTrigger{TriggerSide::R};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(out.triggerR == Catch::Approx(1.0f));
    REQUIRE(out.triggerL == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalButton::process pressed → VirtualStickSlot LeftXPos drives accumulator", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 9;
    btn.target = VirtualStickSlot{StickSlotId::LeftXPos};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(left.xPos == Catch::Approx(1.0f));
    REQUIRE(left.xNeg == Catch::Approx(0.0f));
    REQUIRE(right.xPos == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalButton::process pressed → VirtualStickSlot RightYNeg drives right accumulator", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 10;
    btn.target = VirtualStickSlot{StickSlotId::RightYNeg};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    REQUIRE(right.yNeg == Catch::Approx(1.0f));
    REQUIRE(right.yPos == Catch::Approx(0.0f));
    REQUIRE(left.yNeg  == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalButton::process does not modify unrelated GamepadState fields", "[ComponentTypes]") {
    PhysicalButton btn;
    btn.bit    = 1;
    btn.target = VirtualButton{ButtonId::A};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    btn.process(true, out, left, right, gyro);

    CHECK(out.btnB     == false);
    CHECK(out.btnLB    == false);
    CHECK(out.dpadUp   == false);
    CHECK(out.triggerL == Catch::Approx(0.0f));
    CHECK(left.xPos    == Catch::Approx(0.0f));
}
