// PadEngineActionDispatch.h pulls in <windows.h> (via PadEngine.h) — NOMINMAX first, otherwise
// the min/max macros break catch_amalgamated.hpp (see CLAUDE.md, "Tests").
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PadEngineActionDispatch.h"
#include "bots/BotLoader.h"
#include <memory>
#include <catch2/catch_amalgamated.hpp>

// ─── PadEngineActionDispatch — edge-triggered Keyboard/MouseClick/Bot/Macro dispatch shared by every
// action holder in PadEngine::threadFunc(). The OS side (SendInput) is replaced by a recorder through
// OsInputSender, and bots are in-process fakes registered with BotLoader::add() (no DLL involved),
// so these tests never press a real key or click on the machine running them.

namespace {

struct DispatchRecorder {
    struct KeyCall   { std::vector<std::string> keys; bool press; };
    struct MouseCall { std::string button; bool press; };

    std::vector<KeyCall>   keyCalls;
    std::vector<MouseCall> mouseCalls;
    std::vector<PadEvent>  events;

    PadEngineActionDispatch makeDispatch() {
        OsInputSender osInput;
        osInput.keyCombo = [this](const std::vector<std::string>& keys, bool press) {
            keyCalls.push_back({ keys, press });
        };
        osInput.mouseButton = [this](const std::string& button, bool press) {
            mouseCalls.push_back({ button, press });
        };
        return PadEngineActionDispatch([this](PadEvent e) { events.push_back(e); }, osInput);
    }
};

int g_fakeBotStarts = 0;
int g_fakeBotStops  = 0;
void __cdecl fakeBotStart(BotHandle) { ++g_fakeBotStarts; }
void __cdecl fakeBotStop(BotHandle)  { ++g_fakeBotStops; }

// dll/handle stay null, so ~BotLoader() neither calls bot_destroy nor FreeLibrary on it.
BotInstance* addFakeBot(BotLoader& loader, const std::string& name) {
    g_fakeBotStarts = 0;
    g_fakeBotStops  = 0;
    auto bot = std::make_unique<BotInstance>();
    bot->name     = name;
    bot->fn_start = fakeBotStart;
    bot->fn_stop  = fakeBotStop;
    BotInstance* raw = bot.get();
    loader.add(std::move(bot));
    return raw;
}

Macro makeMacro(MacroRepeatMode mode) {
    Macro mac;
    mac.setup({}, mode, 0, 200);
    return mac;
}

ButtonAction keyboardAction(std::vector<std::string> keys) {
    ButtonAction a; a.type = ButtonActionType::Keyboard; a.keys = std::move(keys);
    return a;
}

ButtonAction mouseAction(const std::string& button) {
    ButtonAction a; a.type = ButtonActionType::MouseClick; a.mouseButton = button;
    return a;
}

ButtonAction namedAction(ButtonActionType type, const std::string& name) {
    ButtonAction a; a.type = type; a.name = name;
    return a;
}

} // namespace

// ─── keyboard() ─────────────────────────────────────────────────────────────

TEST_CASE("dispatch.keyboard fresh press sends the combo and reports a KeyboardAction event",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = false;

    int edge = dispatch.keyboard(false, true, prev, { "ctrl", "z" });

    CHECK(edge == 1);
    CHECK(prev == true);
    REQUIRE(rec.keyCalls.size() == 1);
    const std::vector<std::string> expectedKeys{ "ctrl", "z" };
    CHECK(rec.keyCalls[0].keys == expectedKeys);
    CHECK(rec.keyCalls[0].press == true);
    REQUIRE(rec.events.size() == 1);
    CHECK(rec.events[0].type == PadEventType::KeyboardAction);
    CHECK(rec.events[0].name == "ctrl+z");
    CHECK(rec.events[0].active == true);
}

TEST_CASE("dispatch.keyboard holding the source does not re-send the combo",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = true;

    int edge = dispatch.keyboard(false, true, prev, { "a" });

    CHECK(edge == 0);
    CHECK(prev == true);
    CHECK(rec.keyCalls.empty());
    CHECK(rec.events.empty());
}

TEST_CASE("dispatch.keyboard release sends key-up with no UI event", "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = true;

    int edge = dispatch.keyboard(false, false, prev, { "alt", "tab" });

    CHECK(edge == -1);
    CHECK(prev == false);
    REQUIRE(rec.keyCalls.size() == 1);
    const std::vector<std::string> expectedKeys{ "alt", "tab" };
    CHECK(rec.keyCalls[0].keys == expectedKeys);
    CHECK(rec.keyCalls[0].press == false);
    CHECK(rec.events.empty());
}

TEST_CASE("dispatch.keyboard with the editor open sends nothing and leaves prev untouched",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = false;

    int edge = dispatch.keyboard(true, true, prev, { "a" });

    CHECK(edge == 0);
    CHECK(prev == false);
    CHECK(rec.keyCalls.empty());
    CHECK(rec.events.empty());
}

TEST_CASE("dispatch.keyboard a source held while the editor closes fires once the editor is closed",
          "[PadEngineActionDispatch]") {
    // Bailing out before touching prev is what makes this work — see PadEngineActionDispatch.h.
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = false;

    dispatch.keyboard(true, true, prev, { "a" });           // editor open, button already held
    int edge = dispatch.keyboard(false, true, prev, { "a" }); // editor closes, still held

    CHECK(edge == 1);
    REQUIRE(rec.keyCalls.size() == 1);
    CHECK(rec.keyCalls[0].press == true);
}

// ─── mouse() ────────────────────────────────────────────────────────────────

TEST_CASE("dispatch.mouse press then release sends down/up and one click event",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = false;

    CHECK(dispatch.mouse(false, true, prev, "right") == 1);
    CHECK(dispatch.mouse(false, true, prev, "right") == 0);   // held
    CHECK(dispatch.mouse(false, false, prev, "right") == -1);

    REQUIRE(rec.mouseCalls.size() == 2);
    CHECK(rec.mouseCalls[0].button == "right");
    CHECK(rec.mouseCalls[0].press == true);
    CHECK(rec.mouseCalls[1].press == false);
    REQUIRE(rec.events.size() == 1);
    CHECK(rec.events[0].type == PadEventType::MouseAction);
    CHECK(rec.events[0].name == "right click");
    CHECK(prev == false);
}

TEST_CASE("dispatch.mouse with the editor open sends nothing and leaves prev untouched",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    bool prev = false;

    CHECK(dispatch.mouse(true, true, prev, "left") == 0);
    CHECK(prev == false);
    CHECK(rec.mouseCalls.empty());
    CHECK(rec.events.empty());
}

// ─── bot() ──────────────────────────────────────────────────────────────────

TEST_CASE("dispatch.bot each fresh press toggles the bot and reports its new state",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    BotLoader loader;
    BotInstance* bot = addFakeBot(loader, "FakeBot");
    bool prev = false;

    dispatch.bot(false, true, prev, "FakeBot", loader);    // press -> ON
    dispatch.bot(false, true, prev, "FakeBot", loader);    // held  -> nothing
    dispatch.bot(false, false, prev, "FakeBot", loader);   // release -> nothing
    CHECK(bot->isActive());
    CHECK(g_fakeBotStarts == 1);

    dispatch.bot(false, true, prev, "FakeBot", loader);    // press again -> OFF
    CHECK_FALSE(bot->isActive());
    CHECK(g_fakeBotStops == 1);

    REQUIRE(rec.events.size() == 2);
    CHECK(rec.events[0].type == PadEventType::BotToggle);
    CHECK(rec.events[0].name == "FakeBot");
    CHECK(rec.events[0].active == true);
    CHECK(rec.events[1].active == false);
}

TEST_CASE("dispatch.bot unknown bot name reports nothing but still tracks the edge",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    BotLoader loader;
    bool prev = false;

    dispatch.bot(false, true, prev, "NotLoaded", loader);

    CHECK(prev == true);
    CHECK(rec.events.empty());
}

TEST_CASE("dispatch.bot with the editor open does not toggle and leaves prev untouched",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    BotLoader loader;
    BotInstance* bot = addFakeBot(loader, "FakeBot");
    bool prev = false;

    dispatch.bot(true, true, prev, "FakeBot", loader);

    CHECK_FALSE(bot->isActive());
    CHECK(prev == false);
    CHECK(rec.events.empty());
}

// ─── macro() ────────────────────────────────────────────────────────────────

TEST_CASE("dispatch.macro UntilRelease runs only while the source is held", "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    Macro mac = makeMacro(MacroRepeatMode::UntilRelease);
    bool prev = false;

    CHECK(dispatch.macro(false, mac, true, prev) == true);
    CHECK(mac.isActive());
    CHECK(dispatch.macro(false, mac, true, prev) == false);   // held: not a fresh press
    CHECK(mac.isActive());
    CHECK(dispatch.macro(false, mac, false, prev) == false);
    CHECK_FALSE(mac.isActive());
}

TEST_CASE("dispatch.macro Toggle mode flips on each fresh press and ignores releases",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    Macro mac = makeMacro(MacroRepeatMode::Toggle);
    bool prev = false;

    dispatch.macro(false, mac, true, prev);
    dispatch.macro(false, mac, false, prev);
    CHECK(mac.isActive());                   // release did not stop it

    dispatch.macro(false, mac, true, prev);
    CHECK_FALSE(mac.isActive());             // second press toggled it off
}

TEST_CASE("dispatch.macro Once/TimedMs start via toggle on a fresh press", "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    auto mode = GENERATE(MacroRepeatMode::Once, MacroRepeatMode::TimedMs);
    Macro mac = makeMacro(mode);
    bool prev = false;

    CHECK(dispatch.macro(false, mac, true, prev) == true);
    CHECK(mac.isActive());
    dispatch.macro(false, mac, false, prev);
    CHECK(mac.isActive());                   // release never stops a non-UntilRelease macro
}

TEST_CASE("dispatch.macro with the editor open does not start and leaves prev untouched",
          "[PadEngineActionDispatch]") {
    DispatchRecorder rec;
    auto dispatch = rec.makeDispatch();
    Macro mac = makeMacro(MacroRepeatMode::UntilRelease);
    bool prev = false;

    CHECK(dispatch.macro(true, mac, true, prev) == false);
    CHECK_FALSE(mac.isActive());
    CHECK(prev == false);
}

// ─── rangeAction() ──────────────────────────────────────────────────────────

namespace {
struct RangeFixture {
    DispatchRecorder rec;
    PadEngineActionDispatch dispatch = rec.makeDispatch();
    BotLoader loader;
    std::optional<ButtonAction> prev;
    std::unordered_map<std::string, ButtonAction> active;
    std::unordered_map<std::string, Macro> macros;
    std::unordered_map<std::string, bool> macroOk;
    GamepadState state;

    void tick(bool editorOpen = false) {
        dispatch.rangeAction(editorOpen, "left_x_pos", prev, active, macros, macroOk, state, loader);
    }
};
} // namespace

TEST_CASE("dispatch.rangeAction nothing active and nothing before does nothing", "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.tick();
    CHECK_FALSE(fx.prev.has_value());
    CHECK(fx.rec.keyCalls.empty());
    CHECK(fx.rec.events.empty());
}

TEST_CASE("dispatch.rangeAction activating a keyboard range presses once while it stays active",
          "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.active["left_x_pos"] = keyboardAction({ "w" });

    fx.tick();
    fx.tick();   // same range still active — no re-send

    REQUIRE(fx.rec.keyCalls.size() == 1);
    CHECK(fx.rec.keyCalls[0].press == true);
    REQUIRE(fx.prev.has_value());
    CHECK(fx.prev->keys == std::vector<std::string>{ "w" });
    REQUIRE(fx.rec.events.size() == 1);
    CHECK(fx.rec.events[0].type == PadEventType::KeyboardAction);
    CHECK(fx.rec.events[0].name == "w");
}

TEST_CASE("dispatch.rangeAction switching range releases the old action before pressing the new one",
          "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.active["left_x_pos"] = keyboardAction({ "w" });
    fx.tick();

    fx.active["left_x_pos"] = mouseAction("left");
    fx.tick();

    REQUIRE(fx.rec.keyCalls.size() == 2);
    CHECK(fx.rec.keyCalls[1].keys == std::vector<std::string>{ "w" });
    CHECK(fx.rec.keyCalls[1].press == false);
    REQUIRE(fx.rec.mouseCalls.size() == 1);
    CHECK(fx.rec.mouseCalls[0].button == "left");
    CHECK(fx.rec.mouseCalls[0].press == true);
    REQUIRE(fx.prev.has_value());
    CHECK(fx.prev->type == ButtonActionType::MouseClick);
}

TEST_CASE("dispatch.rangeAction same type with different keys still counts as a change",
          "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.active["left_x_pos"] = keyboardAction({ "w" });
    fx.tick();
    fx.active["left_x_pos"] = keyboardAction({ "shift", "w" });
    fx.tick();

    REQUIRE(fx.rec.keyCalls.size() == 3);
    CHECK(fx.rec.keyCalls[1].press == false);
    const std::vector<std::string> expectedKeys{ "shift", "w" };
    CHECK(fx.rec.keyCalls[2].keys == expectedKeys);
    CHECK(fx.rec.keyCalls[2].press == true);
}

TEST_CASE("dispatch.rangeAction going back to no range releases the last action",
          "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.active["left_x_pos"] = mouseAction("middle");
    fx.tick();
    fx.active.clear();
    fx.tick();

    REQUIRE(fx.rec.mouseCalls.size() == 2);
    CHECK(fx.rec.mouseCalls[1].button == "middle");
    CHECK(fx.rec.mouseCalls[1].press == false);
    CHECK_FALSE(fx.prev.has_value());
}

TEST_CASE("dispatch.rangeAction UntilRelease macro starts on entry and stops on leaving the range",
          "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.macros["left_x_pos|Dash"] = makeMacro(MacroRepeatMode::UntilRelease);
    fx.macroOk["left_x_pos|Dash"] = true;
    fx.active["left_x_pos"] = namedAction(ButtonActionType::Macro, "Dash");

    fx.tick();
    CHECK(fx.macros.at("left_x_pos|Dash").isActive());
    REQUIRE(fx.rec.events.size() == 1);
    CHECK(fx.rec.events[0].type == PadEventType::MacroToggle);
    CHECK(fx.rec.events[0].name == "Dash");
    CHECK(fx.rec.events[0].active == true);

    fx.active.clear();
    fx.tick();
    CHECK_FALSE(fx.macros.at("left_x_pos|Dash").isActive());
}

TEST_CASE("dispatch.rangeAction macro marked not-ok is skipped but the range is still tracked",
          "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.macros["left_x_pos|Broken"] = makeMacro(MacroRepeatMode::UntilRelease);
    fx.macroOk["left_x_pos|Broken"] = false;
    fx.active["left_x_pos"] = namedAction(ButtonActionType::Macro, "Broken");

    fx.tick();

    CHECK_FALSE(fx.macros.at("left_x_pos|Broken").isActive());
    CHECK(fx.rec.events.empty());
    CHECK(fx.prev.has_value());
}

TEST_CASE("dispatch.rangeAction bot range toggles the bot on entry", "[PadEngineActionDispatch]") {
    RangeFixture fx;
    BotInstance* bot = addFakeBot(fx.loader, "FakeBot");
    fx.active["left_x_pos"] = namedAction(ButtonActionType::Bot, "FakeBot");

    fx.tick();

    CHECK(bot->isActive());
    REQUIRE(fx.rec.events.size() == 1);
    CHECK(fx.rec.events[0].type == PadEventType::BotToggle);
    CHECK(fx.rec.events[0].active == true);
}

TEST_CASE("dispatch.rangeAction with the editor open changes nothing", "[PadEngineActionDispatch]") {
    RangeFixture fx;
    fx.active["left_x_pos"] = keyboardAction({ "w" });

    fx.tick(true);

    CHECK_FALSE(fx.prev.has_value());
    CHECK(fx.rec.keyCalls.empty());
    CHECK(fx.rec.events.empty());
}
