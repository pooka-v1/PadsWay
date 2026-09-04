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

// ─── applyDeadzoneMax() / applyDeadzoneMaxSigned() ───────────────────────────
// Calibration remap shared by StickAccumulator::flush(), physical triggers, and
// gyro/accel axes — see ARCHITECTURE.md "Calibracion de entrada".

TEST_CASE("applyDeadzoneMax default {0,1} is a no-op passthrough", "[ComponentTypes][Calibration]") {
    REQUIRE(applyDeadzoneMax(0.3f, 0.0f, 1.0f) == Catch::Approx(0.3f));
    REQUIRE(applyDeadzoneMax(0.0f, 0.0f, 1.0f) == Catch::Approx(0.0f));
    REQUIRE(applyDeadzoneMax(1.0f, 0.0f, 1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("applyDeadzoneMax below deadzone reads 0", "[ComponentTypes][Calibration]") {
    REQUIRE(applyDeadzoneMax(0.05f, 0.1f, 1.0f) == Catch::Approx(0.0f));
}

TEST_CASE("applyDeadzoneMax exactly at deadzone reads 0", "[ComponentTypes][Calibration]") {
    REQUIRE(applyDeadzoneMax(0.1f, 0.1f, 1.0f) == Catch::Approx(0.0f));
}

TEST_CASE("applyDeadzoneMax at/above max saturates to 1", "[ComponentTypes][Calibration]") {
    REQUIRE(applyDeadzoneMax(0.8f, 0.1f, 0.8f) == Catch::Approx(1.0f));
    REQUIRE(applyDeadzoneMax(1.0f, 0.1f, 0.8f) == Catch::Approx(1.0f));  // overflow clamps
}

TEST_CASE("applyDeadzoneMax is linear between deadzone and max", "[ComponentTypes][Calibration]") {
    // deadzone=0.2, max=0.8 -> range 0.6; midpoint 0.5 -> (0.5-0.2)/0.6
    REQUIRE(applyDeadzoneMax(0.5f, 0.2f, 0.8f) == Catch::Approx(0.5f).epsilon(0.001f));
}

TEST_CASE("applyDeadzoneMax degenerate deadzone==max acts as a hard threshold", "[ComponentTypes][Calibration]") {
    REQUIRE(applyDeadzoneMax(0.4f, 0.5f, 0.5f) == Catch::Approx(0.0f));
    REQUIRE(applyDeadzoneMax(0.5f, 0.5f, 0.5f) == Catch::Approx(1.0f));
    REQUIRE(applyDeadzoneMax(0.6f, 0.5f, 0.5f) == Catch::Approx(1.0f));
}

TEST_CASE("applyDeadzoneMaxSigned preserves the sign of the input", "[ComponentTypes][Calibration]") {
    float pos = applyDeadzoneMaxSigned(0.9f, 0.1f, 1.0f);
    float neg = applyDeadzoneMaxSigned(-0.9f, 0.1f, 1.0f);
    REQUIRE(pos == Catch::Approx(-neg));
    REQUIRE(pos > 0.0f);
    REQUIRE(neg < 0.0f);
}

TEST_CASE("applyDeadzoneMaxSigned negative value inside deadzone reads 0", "[ComponentTypes][Calibration]") {
    REQUIRE(applyDeadzoneMaxSigned(-0.05f, 0.1f, 1.0f) == Catch::Approx(0.0f));
}

TEST_CASE("applyDeadzoneMaxSigned max<1 boosts sensitivity past the raw ceiling", "[ComponentTypes][Calibration]") {
    // Gyro/accel have no mechanical stop, so max<1 is a valid "reach full output early" gain.
    REQUIRE(applyDeadzoneMaxSigned(0.5f, 0.0f, 0.5f) == Catch::Approx(1.0f));
}

// ─── applyTouchAxisCalib() ────────────────────────────────────────────────────

TEST_CASE("applyTouchAxisCalib default max=1 is a no-op passthrough", "[ComponentTypes][Calibration]") {
    REQUIRE(applyTouchAxisCalib(0.5f, 1.0f) == Catch::Approx(0.5f));
    REQUIRE(applyTouchAxisCalib(0.0f, 1.0f) == Catch::Approx(0.0f));
    REQUIRE(applyTouchAxisCalib(1.0f, 1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("applyTouchAxisCalib max<1 reaches the edge before the raw reading gets there", "[ComponentTypes][Calibration]") {
    // Compensates a wizard maxX/maxY that landed short of the true physical edge (see
    // TouchpadConfig's comment) — the finger never has to reach raw 1.0 to read as "at the edge".
    REQUIRE(applyTouchAxisCalib(0.9f, 0.8f) == Catch::Approx(1.0f));
    REQUIRE(applyTouchAxisCalib(0.1f, 0.8f) == Catch::Approx(0.0f));
}

TEST_CASE("touchAxisBeyondMax flags a raw position past the calibrated edge", "[ComponentTypes][Calibration]") {
    // Used to gate Raton mode — a touch resting past max moves no cursor at all that frame.
    REQUIRE(touchAxisBeyondMax(0.95f, 0.8f) == true);
    REQUIRE(touchAxisBeyondMax(0.05f, 0.8f) == true);
    REQUIRE(touchAxisBeyondMax(0.5f, 0.8f)  == false);
    REQUIRE(touchAxisBeyondMax(0.5f, 1.0f)  == false);
}

// ─── StickAccumulator::flush() with calibration ──────────────────────────────

TEST_CASE("StickAccumulator::flush default calib matches the no-calib overload", "[ComponentTypes][Calibration]") {
    StickAccumulator acc;
    acc.xPos = 0.5f;
    float x = 0.f, y = 0.f;
    acc.flush(x, y, StickCalibration{});
    REQUIRE(x == Catch::Approx(0.5f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush magnitude inside deadzone reads (0,0)", "[ComponentTypes][Calibration]") {
    StickAccumulator acc;
    acc.xPos = 0.1f;  // mag 0.1
    float x = 99.f, y = 99.f;
    acc.flush(x, y, StickCalibration{0.15f, 1.0f});
    REQUIRE(x == Catch::Approx(0.0f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush remaps magnitude while preserving direction", "[ComponentTypes][Calibration]") {
    StickAccumulator acc;
    acc.xPos = 0.6f;  // single axis: mag == vx, so outX should equal the shaped magnitude exactly
    float x = 0.f, y = 0.f;
    acc.flush(x, y, StickCalibration{0.2f, 0.8f});
    REQUIRE(x == Catch::Approx(applyDeadzoneMax(0.6f, 0.2f, 0.8f)).epsilon(0.001f));
    REQUIRE(y == Catch::Approx(0.0f));
}

TEST_CASE("StickAccumulator::flush calibration keeps the diagonal angle unchanged", "[ComponentTypes][Calibration]") {
    StickAccumulator acc;
    acc.xPos = 0.5f;
    acc.yPos = 0.5f;  // 45 degrees
    float x = 0.f, y = 0.f;
    acc.flush(x, y, StickCalibration{0.1f, 0.9f});
    REQUIRE(x == Catch::Approx(y).epsilon(0.001f));  // still 45 degrees after remap
    REQUIRE(x > 0.5f);  // deadzone/max remap boosted the magnitude past the raw 0.5
}

// ─── PhysicalTrigger::process ─────────────────────────────────────────────────

TEST_CASE("PhysicalTrigger::process passthrough writes the raw value to triggerL", "[ComponentTypes]") {
    PhysicalTrigger trig{TriggerSide::L, {}};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    trig.process(0.7f, out, left, right, gyro);

    REQUIRE(out.triggerL == Catch::Approx(0.7f));
    REQUIRE(out.triggerR == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalTrigger::process below a range's threshold fires nothing", "[ComponentTypes]") {
    RangedHalfAxis axis;
    axis.ranges.push_back({0.5f, 1.0f, VirtualButton{ButtonId::A}});
    PhysicalTrigger trig{TriggerSide::L, axis};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    trig.process(0.3f, out, left, right, gyro);

    REQUIRE(out.btnA == false);
    REQUIRE(out.triggerL == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalTrigger::process at/above a range's threshold fires the mapped button", "[ComponentTypes]") {
    RangedHalfAxis axis;
    axis.ranges.push_back({0.5f, 1.0f, VirtualButton{ButtonId::A}});
    PhysicalTrigger trig{TriggerSide::L, axis};
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    trig.process(0.8f, out, left, right, gyro);

    REQUIRE(out.btnA == true);
    REQUIRE(out.triggerL == Catch::Approx(0.0f));  // range consumed the value, no passthrough
}

// ─── PhysicalAnalogDir::process — dirSign for VirtualMouseMove ────────────────

TEST_CASE("PhysicalAnalogDir::process pos slot moves the mouse in the positive direction", "[ComponentTypes]") {
    RangedHalfAxis axis;
    axis.ranges.push_back({0.0f, 1.0f, VirtualMouseMove{MouseAxis::X, 20.0f}});
    PhysicalAnalogDir ad{StickSlotId::LeftXPos, axis};
    GamepadState       out;
    StickAccumulator   left, right;
    GyroAccumulator    gyro;

    ad.process(0.5f, out, left, right, gyro);

    REQUIRE(out.mouseX == Catch::Approx(10.0f));
}

TEST_CASE("PhysicalAnalogDir::process neg slot moves the mouse in the negative direction", "[ComponentTypes]") {
    // Neg slots carry the unsigned magnitude of the negative direction — dirSign restores
    // the sign for proportional targets like VirtualMouseMove.
    RangedHalfAxis axis;
    axis.ranges.push_back({0.0f, 1.0f, VirtualMouseMove{MouseAxis::X, 20.0f}});
    PhysicalAnalogDir ad{StickSlotId::LeftXNeg, axis};
    GamepadState       out;
    StickAccumulator   left, right;
    GyroAccumulator    gyro;

    ad.process(0.5f, out, left, right, gyro);

    REQUIRE(out.mouseX == Catch::Approx(-10.0f));
}

// ─── PhysicalGyro::process ────────────────────────────────────────────────────

TEST_CASE("PhysicalGyro::process inactive frame leaves the accumulator untouched", "[ComponentTypes]") {
    PhysicalGyro     comp;  // all halves default to empty ranges (passthrough)
    GamepadState     physical;
    physical.gyroActive = false;
    physical.gyroX = 0.9f;
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyroAcc;

    comp.process(physical, out, left, right, gyroAcc);

    REQUIRE(out.gyroActive == false);
    REQUIRE(gyroAcc.xPos == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalGyro::process positive reading routes to the Pos half only", "[ComponentTypes]") {
    PhysicalGyro     comp;
    GamepadState     physical;
    physical.gyroActive = true;
    physical.gyroX = 0.7f;
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyroAcc;

    comp.process(physical, out, left, right, gyroAcc);

    REQUIRE(out.gyroActive == true);
    REQUIRE(gyroAcc.xPos == Catch::Approx(0.7f));
    REQUIRE(gyroAcc.xNeg == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalGyro::process negative reading routes to the Neg half only", "[ComponentTypes]") {
    PhysicalGyro     comp;
    GamepadState     physical;
    physical.gyroActive = true;
    physical.gyroX = -0.6f;
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyroAcc;

    comp.process(physical, out, left, right, gyroAcc);

    REQUIRE(gyroAcc.xPos == Catch::Approx(0.0f));
    REQUIRE(gyroAcc.xNeg == Catch::Approx(0.6f));
}

// ─── PhysicalAccel::process ───────────────────────────────────────────────────
// Same shape as PhysicalGyro — see the struct comment in ComponentTypes.h for the
// axis-letter caveat (accelX/Y/Z do not share gyro's pitch/yaw/roll semantics).

TEST_CASE("PhysicalAccel::process inactive frame leaves the accumulator untouched", "[ComponentTypes]") {
    PhysicalAccel    comp;
    GamepadState     physical;
    physical.accelActive = false;
    physical.accelY = 0.8f;
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  accelAcc;

    comp.process(physical, out, left, right, accelAcc);

    REQUIRE(out.accelActive == false);
    REQUIRE(accelAcc.yPos == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalAccel::process routes Y axis reading to the accumulator", "[ComponentTypes]") {
    PhysicalAccel    comp;
    GamepadState     physical;
    physical.accelActive = true;
    physical.accelY = 0.4f;
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  accelAcc;

    comp.process(physical, out, left, right, accelAcc);

    REQUIRE(out.accelActive == true);
    REQUIRE(accelAcc.yPos == Catch::Approx(0.4f));
    REQUIRE(accelAcc.yNeg == Catch::Approx(0.0f));
}

// ─── PhysicalTouchpad::process() — Zonas dead border ─────────────────────────
// See BITACORA.md 2026/09/02: xMax/yMax must exclude a finger past the calibrated edge from
// hitting any region at all (a real dead border), not saturate it into whatever region the
// pad's physical edge happens to land in.

namespace {
TouchpadConfig makeQuadrantZonesConfig(float xMax, float yMax) {
    TouchpadConfig cfg;
    cfg.enabled     = true;
    cfg.surfaceMode = TouchpadSurfaceMode::Zones;
    cfg.xMax = xMax;
    cfg.yMax = yMax;
    auto rect = [](const char* id, float x0, float x1, float y0, float y1) {
        TouchZoneRegion r;
        r.id = id; r.shape = TouchZoneShape::Rect;
        r.xMin = x0; r.xMax = x1; r.yMin = y0; r.yMax = y1;
        return r;
    };
    cfg.zones = {
        rect("nw", 0.0f, 0.5f, 0.0f, 0.5f),
        rect("ne", 0.5f, 1.0f, 0.0f, 0.5f),
        rect("sw", 0.0f, 0.5f, 0.5f, 1.0f),
        rect("se", 0.5f, 1.0f, 0.5f, 1.0f),
    };
    return cfg;
}
}  // namespace

TEST_CASE("PhysicalTouchpad::process Zonas within the calibrated edge hits the expected quadrant", "[ComponentTypes][Calibration]") {
    PhysicalTouchpad comp{ makeQuadrantZonesConfig(0.51f, 0.51f) };
    GamepadState physical;
    physical.touch1Active = true;
    physical.touch1X = 0.7f;  // offset = 0.4, within xMax=0.51
    physical.touch1Y = 0.3f;  // offset = -0.4, within yMax=0.51
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    comp.process(physical, out, left, right, gyro);

    REQUIRE(out.activeTouchZone1 == "ne");
}

TEST_CASE("PhysicalTouchpad::process Zonas past the calibrated edge hits no region", "[ComponentTypes][Calibration]") {
    PhysicalTouchpad comp{ makeQuadrantZonesConfig(0.51f, 0.51f) };
    GamepadState physical;
    physical.touch1Active = true;
    physical.touch1X = 0.95f;  // offset = 0.9, past xMax=0.51 — dead border
    physical.touch1Y = 0.05f;  // offset = -0.9, past yMax=0.51 too
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    comp.process(physical, out, left, right, gyro);

    REQUIRE(out.activeTouchZone1 == "");
}

TEST_CASE("PhysicalTouchpad::process Zonas default xMax/yMax=1 never excludes (no-op)", "[ComponentTypes][Calibration]") {
    PhysicalTouchpad comp{ makeQuadrantZonesConfig(1.0f, 1.0f) };
    GamepadState physical;
    physical.touch1Active = true;
    physical.touch1X = 0.95f;
    physical.touch1Y = 0.05f;
    GamepadState     out;
    StickAccumulator left, right;
    GyroAccumulator  gyro;

    comp.process(physical, out, left, right, gyro);

    REQUIRE(out.activeTouchZone1 == "ne");
}

// ─── PhysicalController::process() — end-to-end calibration pipeline ─────────
// See ARCHITECTURE.md "Calibracion de entrada" — this is the integration point
// added by the calibration commit: deadzone/max applied to sticks, triggers and
// gyro/accel axes before/after the Component System's normal dispatch.

TEST_CASE("PhysicalController::process applies trigger calibration to a passthrough trigger", "[ComponentTypes][Calibration]") {
    PhysicalController pc;
    pc.triggerLCalib = {0.2f, 0.8f};
    pc[ComponentId::TriggerL] = PhysicalTrigger{TriggerSide::L, {}};

    GamepadState physical;
    physical.triggerL = 0.3f;
    GamepadState output;
    pc.process(physical, output);

    REQUIRE(output.triggerL == Catch::Approx(applyDeadzoneMax(0.3f, 0.2f, 0.8f)).epsilon(0.001f));
}

TEST_CASE("PhysicalController::process applies left stick radial calibration", "[ComponentTypes][Calibration]") {
    PhysicalController pc;
    pc.leftStickCalib = {0.1f, 0.9f};
    pc[ComponentId::LeftXPos] = PhysicalAnalogDir{StickSlotId::LeftXPos, {}};
    pc[ComponentId::LeftXNeg] = PhysicalAnalogDir{StickSlotId::LeftXNeg, {}};

    GamepadState physical;
    physical.leftX = 0.6f;
    GamepadState output;
    pc.process(physical, output);

    REQUIRE(output.leftX == Catch::Approx(applyDeadzoneMax(0.6f, 0.1f, 0.9f)).epsilon(0.001f));
    REQUIRE(output.leftY == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalController::process applies per-axis gyro calibration before PhysicalGyro sees it", "[ComponentTypes][Calibration]") {
    PhysicalController pc;
    pc.gyroXCalib = {0.1f, 0.9f};
    pc[ComponentId::Gyro] = PhysicalGyro{};  // all halves passthrough

    GamepadState physical;
    physical.gyroActive = true;
    physical.gyroX = 0.55f;
    GamepadState output;
    pc.process(physical, output);

    float expected = applyDeadzoneMaxSigned(0.55f, 0.1f, 0.9f);
    REQUIRE(output.gyroX == Catch::Approx(expected).epsilon(0.001f));
}

TEST_CASE("PhysicalController::process gyro reading inside the deadzone reads exactly 0", "[ComponentTypes][Calibration]") {
    PhysicalController pc;
    pc.gyroXCalib = {0.1f, 0.9f};
    pc[ComponentId::Gyro] = PhysicalGyro{};

    GamepadState physical;
    physical.gyroActive = true;
    physical.gyroX = 0.05f;  // below deadzone
    GamepadState output;
    pc.process(physical, output);

    REQUIRE(output.gyroX == Catch::Approx(0.0f));
}

TEST_CASE("PhysicalController::process applies per-axis accel calibration before PhysicalAccel sees it", "[ComponentTypes][Calibration]") {
    PhysicalController pc;
    pc.accelZCalib = {0.0f, 0.5f};  // max<1 boosts sensitivity, see applyDeadzoneMaxSigned
    pc[ComponentId::Accel] = PhysicalAccel{};

    GamepadState physical;
    physical.accelActive = true;
    physical.accelZ = 0.5f;
    GamepadState output;
    pc.process(physical, output);

    REQUIRE(output.accelZ == Catch::Approx(1.0f));
}
