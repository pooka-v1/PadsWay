#include "bot_api.h"
#include <chrono>

// ---------------------------------------------------------------------------
// TestBot — the E2E suite's own bot (PadsWayE2E). It exists so the bot-assignment tests can check a
// bot's actual output, not just its ON/OFF events, without depending on a real bot whose output
// depends on what's on screen.
//
// While ON it plays a fixed, easy-to-see loop: A alone for kPhaseMs, then B alone for kPhaseMs,
// again and again from bot_start(). OFF = no output at all.
//
// Test-only: it is copied into the E2E sandbox (temp/e2e/sandbox/data/bots/), never deployed to
// PadsWay/data/bots/ — it must not show up in the app's bot pickers or in a release.
// ---------------------------------------------------------------------------

namespace {

constexpr int kPhaseMs = 200;   // keep in sync with kTestBotPhaseMs in PadsWayE2E/src/E2ESandbox.h

struct TestBot {
    bool                                  active = false;
    std::chrono::steady_clock::time_point startedAt;
};

} // namespace

extern "C" {

__declspec(dllexport) const char* __cdecl bot_name() {
    return "TestBot";
}

__declspec(dllexport) BotHandle __cdecl bot_create() {
    return new TestBot();
}

__declspec(dllexport) void __cdecl bot_destroy(BotHandle h) {
    delete static_cast<TestBot*>(h);
}

__declspec(dllexport) void __cdecl bot_start(BotHandle h) {
    auto* bot = static_cast<TestBot*>(h);
    bot->active    = true;
    bot->startedAt = std::chrono::steady_clock::now();   // every start begins with A
}

__declspec(dllexport) void __cdecl bot_stop(BotHandle h) {
    static_cast<TestBot*>(h)->active = false;
}

__declspec(dllexport) int __cdecl bot_tick(BotHandle h, BotOutput* out) {
    auto* bot = static_cast<TestBot*>(h);
    if (!bot->active) return 0;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - bot->startedAt).count();
    const bool aPhase = (elapsedMs / kPhaseMs) % 2 == 0;
    out->buttons |= aPhase ? BOT_BTN_A : BOT_BTN_B;
    return 1;
}

} // extern "C"
