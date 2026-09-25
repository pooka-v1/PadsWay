#pragma once
#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include "E2ESandbox.h"
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <catch2/catch_amalgamated.hpp>

// ─── Shared assertions for E2E test cases. Each one starts from a released pad, and leaves it
// released again, so test cases never leak held inputs into each other.

// Builds a state with a single setter applied, e.g. with([](GamepadState& s) { s.btnA = true; }).
inline GamepadState with(void (*set)(GamepadState&)) {
    GamepadState s;
    set(s);
    return s;
}

// Presses `physical` on the fake pad and requires exactly `expected` on the virtual pad — the
// assigned output AND nothing else (e.g. the source's own original output must be gone). No
// keyboard/mouse input may be injected either (e.g. after undoing a keyboard assignment).
inline void checkPressGives(const Ds4Input& physical, const GamepadState& expected) {
    REQUIRE(harness().releaseAll());
    harness().osInput().clear();
    harness().press(physical);
    GamepadState seen;
    const bool matched = harness().waitForVirtual(
        [&](const GamepadState& s) { return sameVirtualOutput(s, expected); }, E2EHarness::kWaitMs, &seen);
    const std::string expectedText = describe(expected);
    const std::string seenText     = describe(seen);
    CAPTURE(expectedText, seenText);
    CHECK(matched);
    CHECK(harness().releaseAll());

    const std::vector<InjectedInput> injected = harness().osInput().recorded();
    const std::string injectedText = describe(injected);
    CAPTURE(injectedText);
    CHECK(injected.empty());
}

// Presses `physical`: the engine must inject exactly `onPress` (in order) and give no gamepad
// output at all; on release, exactly `onRelease`. Keyboard combos press in order and release in
// reverse, so the caller spells out both sequences.
inline void checkPressInjects(const Ds4Input& physical, const std::vector<InjectedInput>& onPress,
                              const std::vector<InjectedInput>& onRelease) {
    E2EInputCapture& os = harness().osInput();
    auto injectsExactly = [&os](const std::vector<InjectedInput>& expected) {
        os.waitFor([&](const std::vector<InjectedInput>& r) { return r.size() >= expected.size(); },
                   E2EHarness::kWaitMs);
        // Give any extra event (a repeat, a stray key) time to show up before comparing.
        std::this_thread::sleep_for(std::chrono::milliseconds(E2EHarness::kSettleMs));
        const std::vector<InjectedInput> seen = os.recorded();
        const std::string expectedText = describe(expected);
        const std::string seenText     = describe(seen);
        CAPTURE(expectedText, seenText);
        CHECK(seen == expected);
    };

    REQUIRE(harness().releaseAll());
    os.clear();
    harness().press(physical);
    injectsExactly(onPress);
    const GamepadState held = harness().virtualState();
    const std::string heldText = describe(held);
    CAPTURE(heldText);
    CHECK(isNeutral(held));   // the source's own gamepad output is gone

    os.clear();
    harness().press(Ds4Input{});
    injectsExactly(onRelease);
    CHECK(harness().releaseAll());
}

// Press -> the engine reports TestBot ON, and with the source released the virtual pad shows the
// bot's loop alone: A, then B, then A again (it keeps going, it isn't a one-shot). Press again ->
// OFF, and the pad stays neutral for longer than a full loop.
// Only CHECKs between ON and OFF, never REQUIRE: the engine has no way to stop a bot from outside
// (a config reload doesn't), so an early abort would leave it pressing A/B through every later case.
inline void checkPressTogglesTestBot(const Ds4Input& physical) {
    const std::string botName = E2ESandbox::kTestBotName;
    auto botToggled = [&botName](bool on) {
        return [&botName, on](const PadEvent& e) {
            return e.type == PadEventType::BotToggle && e.name == botName && e.active == on;
        };
    };
    auto showsOnly = [](const GamepadState& expected) {
        GamepadState seen;
        const bool matched = harness().waitForVirtual(
            [&](const GamepadState& s) { return sameVirtualOutput(s, expected); }, E2EHarness::kWaitMs, &seen);
        const std::string expectedText = describe(expected);
        const std::string seenText     = describe(seen);
        CAPTURE(expectedText, seenText);
        CHECK(matched);
    };
    const GamepadState onlyA = with([](GamepadState& s) { s.btnA = true; });
    const GamepadState onlyB = with([](GamepadState& s) { s.btnB = true; });

    REQUIRE(harness().releaseAll());
    harness().clearEvents();

    harness().press(physical);
    const bool turnedOn = harness().waitForEvent(botToggled(true));
    CHECK(turnedOn);
    harness().press(Ds4Input{});   // release: the bot alone on the pad, and the next press is a fresh edge
    if (!turnedOn) return;         // pressing again now could be what turns it ON, and leave it there

    showsOnly(onlyA);
    showsOnly(onlyB);
    showsOnly(onlyA);

    harness().press(physical);
    const bool turnedOff = harness().waitForEvent(botToggled(false));
    harness().press(Ds4Input{});
    CHECK(turnedOff);

    CHECK(harness().releaseAll());
    GamepadState seen;
    const bool outputAgain = harness().waitForVirtual(
        [](const GamepadState& s) { return !isNeutral(s); }, 2 * E2ESandbox::kTestBotPhaseMs + E2EHarness::kSettleMs,
        &seen);
    const std::string afterOffText = describe(seen);
    CAPTURE(afterOffText);
    CHECK_FALSE(outputAgain);
}

// Holds `physical` (a stick half-axis bound to mouse movement): the engine must move the cursor
// the expected way — at least a few MouseMove events, none against `dxSign`/`dySign` (-1/0/+1,
// screen coordinates, 0 = no movement allowed on that axis) — with no gamepad output, and stop
// moving it on release. Only the direction is checked: dx/dy come after Windows pointer ballistics.
inline void checkHoldMovesMouse(const Ds4Input& physical, int dxSign, int dySign) {
    constexpr size_t kMinMoves = 3;
    auto sign = [](LONG v) { return (v > 0) - (v < 0); };
    E2EInputCapture& os = harness().osInput();

    REQUIRE(harness().releaseAll());
    // The hook reports where the cursor WOULD go, clamped to the screen: a cursor resting on an edge
    // would read as "no movement" that way. Start from the middle (swallowed moves never shift it).
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2);
    os.clear();
    harness().press(physical);
    os.waitFor([&](const std::vector<InjectedInput>& r) { return r.size() >= kMinMoves; }, E2EHarness::kWaitMs);
    const GamepadState               held  = harness().virtualState();
    const std::vector<InjectedInput> moves = os.recorded();
    harness().press(Ds4Input{});

    size_t rightWay = 0;
    bool   wrongWay = false;
    for (const InjectedInput& m : moves) {
        if (m.kind != InjectedInput::Kind::MouseMove) { wrongWay = true; continue; }
        const int sx = sign(m.dx), sy = sign(m.dy);
        if ((sx != 0 && sx != dxSign) || (sy != 0 && sy != dySign)) wrongWay = true;
        else if (sx != 0 || sy != 0) ++rightWay;
    }
    const std::string movesText = describe(moves);
    const std::string heldText  = describe(held);
    CAPTURE(dxSign, dySign, movesText, heldText);
    CHECK(rightWay >= kMinMoves);
    CHECK_FALSE(wrongWay);
    CHECK(isNeutral(held));   // the source's own stick output is gone

    // Stops on release: let a tick already in flight land, then nothing more may arrive.
    std::this_thread::sleep_for(std::chrono::milliseconds(E2EHarness::kSettleMs));
    os.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(E2EHarness::kSettleMs));
    const std::string afterReleaseText = describe(os.recorded());
    CAPTURE(afterReleaseText);
    CHECK(os.recorded().empty());
    CHECK(harness().releaseAll());
}

// Presses and HOLDS `physical`: a Once macro must play `playing` and then end on its own — the
// output goes back to neutral while the source is still held (that's what tells a macro apart from
// a plain remap). `minMs`/`maxMs` bound how long `playing` stayed on screen.
inline void checkHoldPlaysOnceMacro(const Ds4Input& physical, const GamepadState& playing, int minMs, int maxMs) {
    using Clock = std::chrono::steady_clock;
    REQUIRE(harness().releaseAll());
    harness().press(physical);

    GamepadState seen;
    const bool started = harness().waitForVirtual(
        [&](const GamepadState& s) { return sameVirtualOutput(s, playing); }, E2EHarness::kWaitMs, &seen);
    const auto startedAt = Clock::now();
    const std::string expectedText = describe(playing);
    const std::string seenText     = describe(seen);
    CAPTURE(expectedText, seenText);
    REQUIRE(started);

    const bool ended = harness().waitForVirtual(isNeutral, maxMs + 500, &seen);
    const auto playedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startedAt).count());
    const std::string afterText = describe(seen);
    CAPTURE(playedMs, afterText);
    CHECK(ended);                 // stopped by itself, source still held
    CHECK(playedMs >= minMs);
    CHECK(playedMs <= maxMs);

    CHECK(harness().releaseAll());
}
