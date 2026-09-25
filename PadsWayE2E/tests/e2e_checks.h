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
