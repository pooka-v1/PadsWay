// BotLoader.h pulls in <windows.h> — NOMINMAX first (see CLAUDE.md, "Tests").
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "bots/BotLoader.h"
#include <memory>
#include <catch2/catch_amalgamated.hpp>

// ─── BotInstance lifecycle + BotLoader registry. Bots are in-process fakes: the bot_* function
// pointers point at the counters below instead of a DLL's exports, and dll/handle stay null so
// ~BotLoader() never calls bot_destroy/FreeLibrary on them. scan() itself (LoadLibrary over a
// folder) is not covered here.

namespace {

struct FakeBotCounters {
    int starts = 0;
    int stops  = 0;
    int ticks  = 0;
};
FakeBotCounters g_counters;
int             g_tickResult = 1;

void __cdecl countingStart(BotHandle) { ++g_counters.starts; }
void __cdecl countingStop(BotHandle)  { ++g_counters.stops; }
int  __cdecl countingTick(BotHandle, BotOutput* out) {
    ++g_counters.ticks;
    out->buttons = BOT_BTN_A | BOT_BTN_DPAD_UP;
    out->rt      = 0.75f;
    return g_tickResult;
}

std::unique_ptr<BotInstance> makeFakeBot(const std::string& name) {
    g_counters   = {};
    g_tickResult = 1;
    auto bot = std::make_unique<BotInstance>();
    bot->name     = name;
    bot->fn_start = countingStart;
    bot->fn_stop  = countingStop;
    bot->fn_tick  = countingTick;
    return bot;
}

} // namespace

TEST_CASE("BotInstance starts inactive and start() calls bot_start exactly once", "[BotLoader]") {
    auto bot = makeFakeBot("Fake");
    CHECK_FALSE(bot->isActive());

    bot->start();
    bot->start();   // already running: must not call bot_start again

    CHECK(bot->isActive());
    CHECK(g_counters.starts == 1);
}

TEST_CASE("BotInstance stop() on an inactive bot is a no-op", "[BotLoader]") {
    auto bot = makeFakeBot("Fake");
    bot->stop();
    CHECK(g_counters.stops == 0);
    CHECK_FALSE(bot->isActive());
}

TEST_CASE("BotInstance toggle() alternates start and stop", "[BotLoader]") {
    auto bot = makeFakeBot("Fake");

    bot->toggle();
    CHECK(bot->isActive());
    bot->toggle();
    CHECK_FALSE(bot->isActive());

    CHECK(g_counters.starts == 1);
    CHECK(g_counters.stops == 1);
}

TEST_CASE("BotInstance tick() on an inactive bot never reaches bot_tick", "[BotLoader]") {
    auto bot = makeFakeBot("Fake");
    BotOutput out{};

    CHECK_FALSE(bot->tick(&out));
    CHECK(g_counters.ticks == 0);
    CHECK(out.buttons == 0);
}

TEST_CASE("BotInstance tick() forwards the bot's output and its applied/not-applied result",
          "[BotLoader]") {
    auto bot = makeFakeBot("Fake");
    bot->start();
    BotOutput out{};

    CHECK(bot->tick(&out));
    CHECK(out.buttons == (BOT_BTN_A | BOT_BTN_DPAD_UP));
    CHECK(out.rt == Catch::Approx(0.75f));

    g_tickResult = 0;   // bot says "nothing to apply this tick"
    CHECK_FALSE(bot->tick(&out));
    CHECK(g_counters.ticks == 2);
}

TEST_CASE("BotInstance without bot_start/bot_stop exports never becomes active", "[BotLoader]") {
    // scan() rejects such DLLs, but start()/stop() must still be safe on a half-filled instance.
    BotInstance bot;
    bot.start();
    CHECK_FALSE(bot.isActive());
    BotOutput out{};
    CHECK_FALSE(bot.tick(&out));
}

TEST_CASE("BotLoader::find returns the registered bot by exact name, nullptr otherwise",
          "[BotLoader]") {
    BotLoader loader;
    loader.add(makeFakeBot("LightningBot"));
    loader.add(makeFakeBot("FacingBot"));

    REQUIRE(loader.bots().size() == 2);
    REQUIRE(loader.find("FacingBot") != nullptr);
    CHECK(loader.find("FacingBot")->name == "FacingBot");
    CHECK(loader.find("lightningbot") == nullptr);   // case-sensitive
    CHECK(loader.find("") == nullptr);
}

TEST_CASE("BotLoader::stopAll stops every running bot and leaves stopped ones alone", "[BotLoader]") {
    BotLoader loader;
    loader.add(makeFakeBot("A"));
    loader.add(makeFakeBot("B"));   // resets counters — both bots share g_counters from here on
    loader.find("A")->start();

    loader.stopAll();

    CHECK_FALSE(loader.find("A")->isActive());
    CHECK_FALSE(loader.find("B")->isActive());
    CHECK(g_counters.stops == 1);   // B was never running, so bot_stop is not called for it
}
