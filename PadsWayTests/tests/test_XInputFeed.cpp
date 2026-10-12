#ifndef NOMINMAX
#define NOMINMAX   // XInputFeed.h pulls in <windows.h> — see CLAUDE.md, "Tests"
#endif
#include "input/XInputFeed.h"
#include <catch2/catch_amalgamated.hpp>
#include <cstring>
#include <ios>
#include <utility>

// A pad behind xinputhid is read through its XUSB interface, but its controllers.json entry was captured over
// HID (XInputFeed.h). xinputGamepadToRawState must therefore give the same RawHIDState layout that
// xinputhid gives over HID — a wrong bit or sign here would silently remap a pad that worked.

namespace {

XINPUT_GAMEPAD restingPad() {
    XINPUT_GAMEPAD pad = {};
    return pad;
}

RawHIDState convert(const XINPUT_GAMEPAD& pad) {
    RawHIDState out;
    xinputGamepadToRawState(pad, out);
    return out;
}

constexpr ULONG kHatCentred = 0xFFFFFFFF;

} // namespace

TEST_CASE("XInput at rest: no buttons, centred sticks, released triggers", "[XInputFeed]") {
    const RawHIDState s = convert(restingPad());
    CHECK(s.valid);
    CHECK(s.buttonsValid);
    CHECK(s.buttonMask == 0);
    CHECK(s.axisX  == 0.0f);
    CHECK(s.axisY  == 0.0f);
    CHECK(s.axisRx == 0.0f);
    CHECK(s.axisRy == 0.0f);
    CHECK(s.axisZ  == 0.0f);
    CHECK(s.axisBrake == -1.0f);   // a released trigger sits at the bottom of [-1, 1], as over HID
    CHECK(s.axisAccel == -1.0f);
    CHECK(s.hat == kHatCentred);
}

TEST_CASE("XInput carries the axes it fills, and only those", "[XInputFeed]") {
    const RawHIDState s = convert(restingPad());
    CHECK(s.hasAxis(RawAxis::X));
    CHECK(s.hasAxis(RawAxis::Y));
    CHECK(s.hasAxis(RawAxis::Z));
    CHECK(s.hasAxis(RawAxis::Rx));
    CHECK(s.hasAxis(RawAxis::Ry));
    CHECK(s.hasAxis(RawAxis::Brake));
    CHECK(s.hasAxis(RawAxis::Accel));
    CHECK_FALSE(s.hasAxis(RawAxis::Rz));
}

TEST_CASE("XInput buttons land on the HID button numbers of xinputhid", "[XInputFeed]") {
    // { XInput bit, HID button number (1-based) }
    const std::pair<WORD, int> expected[] = {
        { XINPUT_GAMEPAD_A,              1 },
        { XINPUT_GAMEPAD_B,              2 },
        { XINPUT_GAMEPAD_X,              3 },
        { XINPUT_GAMEPAD_Y,              4 },
        { XINPUT_GAMEPAD_LEFT_SHOULDER,  5 },
        { XINPUT_GAMEPAD_RIGHT_SHOULDER, 6 },
        { XINPUT_GAMEPAD_BACK,           7 },
        { XINPUT_GAMEPAD_START,          8 },
        { XINPUT_GAMEPAD_LEFT_THUMB,     9 },
        { XINPUT_GAMEPAD_RIGHT_THUMB,   10 },
        { 0x0400 /* Guide */,           11 },
    };
    for (const auto& [xinputBit, hidButton] : expected) {
        XINPUT_GAMEPAD pad = restingPad();
        pad.wButtons = xinputBit;
        INFO("XInput bit 0x" << std::hex << xinputBit);
        CHECK(convert(pad).buttonMask == (1u << (hidButton - 1)));
    }
}

TEST_CASE("XInput d-pad goes to the hat, never to the button mask", "[XInputFeed]") {
    const std::pair<WORD, ULONG> expected[] = {
        { XINPUT_GAMEPAD_DPAD_UP,                              0 },
        { XINPUT_GAMEPAD_DPAD_UP   | XINPUT_GAMEPAD_DPAD_RIGHT, 1 },
        { XINPUT_GAMEPAD_DPAD_RIGHT,                           2 },
        { XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT, 3 },
        { XINPUT_GAMEPAD_DPAD_DOWN,                            4 },
        { XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,  5 },
        { XINPUT_GAMEPAD_DPAD_LEFT,                            6 },
        { XINPUT_GAMEPAD_DPAD_UP   | XINPUT_GAMEPAD_DPAD_LEFT,  7 },
        { XINPUT_GAMEPAD_DPAD_UP   | XINPUT_GAMEPAD_DPAD_DOWN,  kHatCentred },   // opposites cancel
    };
    for (const auto& [dpadBits, hat] : expected) {
        XINPUT_GAMEPAD pad = restingPad();
        pad.wButtons = dpadBits;
        INFO("d-pad bits 0x" << std::hex << dpadBits);
        const RawHIDState s = convert(pad);
        CHECK(s.hat == hat);
        CHECK(s.buttonMask == 0);
    }
}

TEST_CASE("XInput sticks: X as is, Y flipped to HID's downwards convention", "[XInputFeed]") {
    XINPUT_GAMEPAD pad = restingPad();
    pad.sThumbLX = 32767;    // left stick right
    pad.sThumbLY = 32767;    // left stick up
    pad.sThumbRX = -32768;   // right stick left
    pad.sThumbRY = -32768;   // right stick down
    const RawHIDState s = convert(pad);
    CHECK(s.axisX  == Catch::Approx( 1.0f));
    CHECK(s.axisY  == Catch::Approx(-1.0f));   // up = negative over HID
    CHECK(s.axisRx == Catch::Approx(-1.0f));   // -32768 / 32767 is clamped to -1
    CHECK(s.axisRy == Catch::Approx( 1.0f));
}

TEST_CASE("XInput triggers: separate on brake/accel, combined on Z with LT positive", "[XInputFeed]") {
    SECTION("left trigger") {
        XINPUT_GAMEPAD pad = restingPad();
        pad.bLeftTrigger = 255;
        const RawHIDState s = convert(pad);
        CHECK(s.axisBrake == Catch::Approx( 1.0f));
        CHECK(s.axisAccel == Catch::Approx(-1.0f));
        CHECK(s.axisZ     == Catch::Approx( 1.0f));   // trigger_combined: positive = left
    }
    SECTION("right trigger") {
        XINPUT_GAMEPAD pad = restingPad();
        pad.bRightTrigger = 255;
        const RawHIDState s = convert(pad);
        CHECK(s.axisBrake == Catch::Approx(-1.0f));
        CHECK(s.axisAccel == Catch::Approx( 1.0f));
        CHECK(s.axisZ     == Catch::Approx(-1.0f));
    }
    SECTION("both fully pressed: separate axes keep both, Z cancels out") {
        XINPUT_GAMEPAD pad = restingPad();
        pad.bLeftTrigger  = 255;
        pad.bRightTrigger = 255;
        const RawHIDState s = convert(pad);
        CHECK(s.axisBrake == Catch::Approx(1.0f));
        CHECK(s.axisAccel == Catch::Approx(1.0f));
        CHECK(s.axisZ     == Catch::Approx(0.0f));
    }
}

// A real answer of the Pro 2 to the XUSB "get gamepad state" IOCTL (temp/gameinput-probe/xusb_probe),
// with A pressed and the sticks resting slightly off-centre.
constexpr BYTE kPro2AnswerWithA[] = {
    0x03, 0x01, 0x01, 0x00, 0xFF, 0x4A, 0x01, 0x00, 0x00, 0x00, 0x13, 0x00, 0x10, 0x00, 0x00,
    0xFF, 0xFC, 0x00, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

TEST_CASE("XUSB answer: the gamepad state sits at offset 11", "[XInputFeed]") {
    XINPUT_GAMEPAD pad = {};
    REQUIRE(xusbStateToGamepad(kPro2AnswerWithA, sizeof(kPro2AnswerWithA), pad));
    CHECK(pad.wButtons      == XINPUT_GAMEPAD_A);
    CHECK(pad.bLeftTrigger  == 0);
    CHECK(pad.bRightTrigger == 0);
    CHECK(pad.sThumbLX      == -769);   // 0xFCFF
    CHECK(pad.sThumbLY      ==  768);   // 0x0300
    CHECK(pad.sThumbRX      ==  0);
    CHECK(pad.sThumbRY      == -1);
}

TEST_CASE("XUSB answer: rejected when too short or the pad isn't connected", "[XInputFeed]") {
    XINPUT_GAMEPAD pad = {};
    CHECK_FALSE(xusbStateToGamepad(kPro2AnswerWithA, 22, pad));   // one byte short of the gamepad

    BYTE disconnected[sizeof(kPro2AnswerWithA)];
    std::memcpy(disconnected, kPro2AnswerWithA, sizeof(disconnected));
    disconnected[2] = 0x00;   // status byte
    CHECK_FALSE(xusbStateToGamepad(disconnected, sizeof(disconnected), pad));
}

TEST_CASE("XInput raw bytes: a zero report-ID slot followed by XINPUT_GAMEPAD", "[XInputFeed]") {
    XINPUT_GAMEPAD pad = restingPad();
    pad.wButtons     = XINPUT_GAMEPAD_A;
    pad.bLeftTrigger = 0x42;
    pad.sThumbLX     = 0x1234;
    const RawHIDState s = convert(pad);
    REQUIRE(s.raw.size() == 1 + sizeof(XINPUT_GAMEPAD));
    CHECK(s.raw[0] == 0);
    CHECK(std::memcmp(s.raw.data() + 1, &pad, sizeof(XINPUT_GAMEPAD)) == 0);
}
