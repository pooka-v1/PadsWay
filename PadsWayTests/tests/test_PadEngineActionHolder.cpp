#include "PadEngineActionHolder.h"
#include <catch2/catch_amalgamated.hpp>

// ─── initActionHolderState() — shared construction for button/dpad/touch zone/touch gesture/axis
// action holders (see SESSION_CONTEXT.md, "Refactor de codigo", tarea 2). Characterization tests
// written before wiring this into PadEngine.cpp — cover the two shapes that matter: an int-keyed
// ButtonAction holder (button, uses `.name`) and a string-keyed HalfAxisAction holder with Ranges
// (axis/gyro/accel, uses `.target`), since those are the two field-name/type variants the
// generic init has to handle correctly.

namespace {
    auto buttonNameField = [](const ButtonAction& a) -> const std::string& { return a.name; };
    auto axisNameField   = [](const HalfAxisAction& a) -> const std::string& { return a.target; };
}

TEST_CASE("initActionHolderState builds keyboard/mouse/bot entries for a button-style (int-keyed) holder",
          "[PadEngineActionHolder]") {
    std::unordered_map<int, ButtonAction> buttons;

    ButtonAction kb; kb.type = ButtonActionType::Keyboard; kb.keys = {"ctrl", "z"};
    buttons[1] = kb;

    ButtonAction mouse; mouse.type = ButtonActionType::MouseClick; mouse.mouseButton = "left";
    buttons[2] = mouse;

    ButtonAction bot; bot.type = ButtonActionType::Bot; bot.name = "LightningBot";
    buttons[3] = bot;

    ActionHolderState<int> st;
    initActionHolderState(st, buttons, {}, buttonNameField, "button");

    REQUIRE(st.kbPrev.count(1) == 1);
    CHECK(st.kbPrev.at(1) == false);
    REQUIRE(st.mousePrev.count(2) == 1);
    CHECK(st.mousePrev.at(2) == false);
    REQUIRE(st.botNames.count(3) == 1);
    CHECK(st.botNames.at(3) == "LightningBot");
    CHECK(st.botPrev.at(3) == false);

    // Ranges must stay untouched — ButtonActionType has no Ranges member.
    CHECK(st.rangePrev.empty());
    CHECK(st.rangeMacros.empty());
}

TEST_CASE("initActionHolderState resolves a button macro by inline execution string, not the library",
          "[PadEngineActionHolder]") {
    std::unordered_map<int, ButtonAction> buttons;
    ButtonAction m; m.type = ButtonActionType::Macro; m.name = "Combo"; m.execution = "A, B";
    buttons[5] = m;

    ActionHolderState<int> st;
    initActionHolderState(st, buttons, {}, buttonNameField, "button");

    REQUIRE(st.macros.count(5) == 1);
    CHECK(st.macroNames.at(5) == "Combo");
    CHECK(st.macroPrev.at(5) == false);
}

TEST_CASE("initActionHolderState resolves a button macro from the library by name when execution is empty",
          "[PadEngineActionHolder]") {
    std::unordered_map<int, ButtonAction> buttons;
    ButtonAction m; m.type = ButtonActionType::Macro; m.name = "RightHadoken";
    buttons[7] = m;

    std::unordered_map<std::string, std::string> library = { {"RightHadoken", "CD=30, CDR=50, CR + X=80"} };

    ActionHolderState<int> st;
    initActionHolderState(st, buttons, library, buttonNameField, "button");

    REQUIRE(st.macros.count(7) == 1);
    CHECK(st.macroNames.at(7) == "RightHadoken");
}

TEST_CASE("initActionHolderState skips a button macro whose name is not found in the library",
          "[PadEngineActionHolder]") {
    std::unordered_map<int, ButtonAction> buttons;
    ButtonAction m; m.type = ButtonActionType::Macro; m.name = "Missing";
    buttons[9] = m;

    ActionHolderState<int> st;
    initActionHolderState(st, buttons, {}, buttonNameField, "button");

    CHECK(st.macros.count(9) == 0);
    CHECK(st.macroNames.count(9) == 0);
}

TEST_CASE("initActionHolderState uses .target (not .name) for a HalfAxisAction (axis/gyro) holder",
          "[PadEngineActionHolder]") {
    std::unordered_map<std::string, HalfAxisAction> axis;
    HalfAxisAction bot; bot.type = HalfAxisActionType::Bot; bot.target = "LightningBot";
    axis["left_x_pos"] = bot;

    ActionHolderState<std::string> st;
    initActionHolderState(st, axis, {}, axisNameField, "axis direction");

    REQUIRE(st.botNames.count("left_x_pos") == 1);
    CHECK(st.botNames.at("left_x_pos") == "LightningBot");
}

TEST_CASE("initActionHolderState builds Ranges entries only for HalfAxisAction holders",
          "[PadEngineActionHolder]") {
    std::unordered_map<std::string, HalfAxisAction> axis;

    HalfAxisAction ranged; ranged.type = HalfAxisActionType::Ranges;
    TriggerRange r1; r1.hasAction = true; r1.action.type = ButtonActionType::Macro; r1.action.name = "Combo";
    ranged.ranges = { r1 };
    axis["right_y_pos"] = ranged;

    std::unordered_map<std::string, std::string> library = { {"Combo", "A, B"} };

    ActionHolderState<std::string> st;
    initActionHolderState(st, axis, library, axisNameField, "axis direction");

    REQUIRE(st.rangePrev.count("right_y_pos") == 1);
    CHECK(st.rangePrev.at("right_y_pos") == std::nullopt);
    REQUIRE(st.rangeMacros.count("right_y_pos|Combo") == 1);
    CHECK(st.rangeMacroOk.at("right_y_pos|Combo") == true);
}

TEST_CASE("initActionHolderState marks a range macro as not-ok when its name is missing from the library",
          "[PadEngineActionHolder]") {
    std::unordered_map<std::string, HalfAxisAction> axis;

    HalfAxisAction ranged; ranged.type = HalfAxisActionType::Ranges;
    TriggerRange r1; r1.hasAction = true; r1.action.type = ButtonActionType::Macro; r1.action.name = "Missing";
    ranged.ranges = { r1 };
    axis["right_y_neg"] = ranged;

    ActionHolderState<std::string> st;
    initActionHolderState(st, axis, {}, axisNameField, "axis direction");

    REQUIRE(st.rangeMacroOk.count("right_y_neg|Missing") == 1);
    CHECK(st.rangeMacroOk.at("right_y_neg|Missing") == false);
    CHECK(st.rangeMacros.count("right_y_neg|Missing") == 0);
}

TEST_CASE("initActionHolderState clears previous state on rebuild", "[PadEngineActionHolder]") {
    std::unordered_map<int, ButtonAction> buttons;
    ButtonAction kb; kb.type = ButtonActionType::Keyboard; kb.keys = {"a"};
    buttons[1] = kb;

    ActionHolderState<int> st;
    initActionHolderState(st, buttons, {}, buttonNameField, "button");
    REQUIRE(st.kbPrev.count(1) == 1);

    buttons.clear();
    initActionHolderState(st, buttons, {}, buttonNameField, "button");
    CHECK(st.kbPrev.empty());
}
