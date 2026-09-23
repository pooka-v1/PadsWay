#pragma once
#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include <chrono>
#include <string>
#include <thread>
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
// assigned output AND nothing else (e.g. the source's own original output must be gone).
inline void checkPressGives(const Ds4Input& physical, const GamepadState& expected) {
    REQUIRE(harness().releaseAll());
    harness().press(physical);
    GamepadState seen;
    const bool matched = harness().waitForVirtual(
        [&](const GamepadState& s) { return sameVirtualOutput(s, expected); }, E2EHarness::kWaitMs, &seen);
    const std::string expectedText = describe(expected);
    const std::string seenText     = describe(seen);
    CAPTURE(expectedText, seenText);
    CHECK(matched);
    CHECK(harness().releaseAll());
}

// Press -> the engine reports `botName` ON; release and press again -> OFF. Only the BotToggle
// events are checked, not the bot's own output (LightningBot's depends on what's on screen).
inline void checkPressTogglesBot(const Ds4Input& physical, const std::string& botName) {
    auto botToggled = [&botName](bool on) {
        return [&botName, on](const PadEvent& e) {
            return e.type == PadEventType::BotToggle && e.name == botName && e.active == on;
        };
    };
    REQUIRE(harness().releaseAll());
    harness().clearEvents();

    harness().press(physical);
    const bool turnedOn = harness().waitForEvent(botToggled(true));
    harness().press(Ds4Input{});   // release: next press must be a fresh edge
    std::this_thread::sleep_for(std::chrono::milliseconds(E2EHarness::kSettleMs));

    harness().press(physical);
    const bool turnedOff = harness().waitForEvent(botToggled(false));
    harness().press(Ds4Input{});

    CHECK(turnedOn);
    CHECK(turnedOff);
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
