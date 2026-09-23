#pragma once
// PadEngine.h / the ViGEm adapter pull in <windows.h> — NOMINMAX first, same reason as in
// PadsWayTests (min/max macros break catch_amalgamated.hpp, see CLAUDE.md "Tests").
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PadEngine.h"
#include "input/DeviceHub.h"
#include "output/ViGEmDs4OutputAdapter.h"
#include <functional>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// E2EHarness — one real PadEngine (no UI) reading a fake "physical" DualShock 4 that the harness
// itself plugs in through ViGEm, so the whole pipeline HID read -> mapping -> virtual output runs
// exactly as in the app, with the harness on both ends.
//
// Fake pad input is written as a GamepadState in DS4 *positions*, the same convention
// ViGEmDs4OutputAdapter uses to build the DS4 report:
//   btnA = Cross   btnB = Circle   btnX = Square   btnY = Triangle
//   btnLB/btnRB = L1/R1   btnBack = Share   btnStart = Options   btnHome = PS
//   btnL3/btnR3, triggerL/R [0,1], leftX/Y + rightX/Y [-1,1] (+Y = up), dpad*.
// Output is read back from PadEngine::getLastVirtualState() — the exact state sent to ViGEm.
// ---------------------------------------------------------------------------
using Ds4Input = GamepadState;

class E2EHarness {
public:
    // Engine loop runs every ~8 ms; these leave generous room for HID + scheduling jitter.
    static constexpr int kWaitMs   = 1000;   // max wait for an expected output to show up
    static constexpr int kSettleMs = 150;    // wait before asserting something did NOT happen

    // Plugs the fake pad in, starts the engine and waits until it is Running on the fake pad.
    bool start(std::string& error);
    void stop();

    // Sends a full report: anything not set in `input` reads as released/centered.
    void press(const Ds4Input& input);
    // Neutral report, then waits until the engine's output is neutral again. False on timeout.
    bool releaseAll();

    GamepadState virtualState() const;
    // Polls the engine output until `pred` holds or `timeoutMs` passes. `lastSeen` (optional)
    // receives the last polled state either way, for failure messages.
    bool waitForVirtual(const std::function<bool(const GamepadState&)>& pred, int timeoutMs = kWaitMs,
                        GamepadState* lastSeen = nullptr) const;

    PadEngine& engine() { return *m_engine; }

private:
    std::unique_ptr<ViGEmDs4OutputAdapter> m_fakePad;
    std::unique_ptr<DeviceHub>             m_deviceHub;
    std::unique_ptr<PadEngine>             m_engine;
};

// The single instance, set up by main() before Catch2 runs any test case.
E2EHarness& harness();

// True when no button/dpad is pressed, triggers are released and sticks are centered.
bool isNeutral(const GamepadState& s);

// Same buttons/dpad and triggers/sticks within `tolerance` — the Xbox-visible part of the state
// only (paddles/touch/IMU are never sent to the virtual pad).
bool sameVirtualOutput(const GamepadState& actual, const GamepadState& expected, float tolerance = 0.1f);

// Compact human-readable form for failure messages, e.g. "A LB DPAD_UP LT=0.50 LX=1.00".
// Neutral reads as "(neutral)".
std::string describe(const GamepadState& s);
