#include <catch2/catch_amalgamated.hpp>
#include "ui/MappingModel.h"
#include "input/ControllerConfig.h"

TEST_CASE("MappingModel::clear resets all maps", "[MappingModel]") {
    MappingModel model;
    model.buttonEdits["a"] = "b";
    model.actionEdits["c"] = ButtonAction{ButtonActionType::Keyboard};
    model.axisEdits["left_x"] = AxisMapping{};
    model.axisActionEdits["left_x_pos"] = HalfAxisAction{};
    model.trigActionEdits["l2"] = ButtonAction{};
    model.trigLRangeEdits.push_back(RangeEdit{});
    model.trigRRangeEdits.push_back(RangeEdit{});
    model.stickSlotEdits["left_x_neg"] = "d";

    model.clear();

    REQUIRE(model.buttonEdits.empty());
    REQUIRE(model.actionEdits.empty());
    REQUIRE(model.axisEdits.empty());
    REQUIRE(model.axisActionEdits.empty());
    REQUIRE(model.trigActionEdits.empty());
    REQUIRE(model.trigLRangeEdits.empty());
    REQUIRE(model.trigRRangeEdits.empty());
    REQUIRE(model.stickSlotEdits.empty());

    // vid and pid are not cleared
    REQUIRE(model.vid == 0);
    REQUIRE(model.pid == 0);
}

TEST_CASE("MappingModel::reload loads VirtualButton correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.buttons[1] = ButtonAction{ButtonActionType::VirtualButton, "b", "a"};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.size() == 1);
    REQUIRE(model.buttonEdits.at("a") == "b");
}

TEST_CASE("MappingModel::reload skips identity remaps", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.buttons[1] = ButtonAction{ButtonActionType::VirtualButton, "a", "a"};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.empty());
}

TEST_CASE("MappingModel::reload loads Keyboard action correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    ButtonAction action{ButtonActionType::Keyboard};
    action.physical = "a";
    action.keys = {"alt", "tab"};
    cfg.buttons[1] = action;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.actionEdits.size() == 1);
    REQUIRE(model.actionEdits.at("a").type == ButtonActionType::Keyboard);
    REQUIRE(model.actionEdits.at("a").keys == action.keys);
}

TEST_CASE("MappingModel::reload loads Macro action correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    ButtonAction action{ButtonActionType::Macro};
    action.physical = "a";
    action.execution = "A,500,A";
    cfg.buttons[1] = action;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.actionEdits.size() == 1);
    REQUIRE(model.actionEdits.at("a").type == ButtonActionType::Macro);
    REQUIRE(model.actionEdits.at("a").execution == action.execution);
}

TEST_CASE("MappingModel::reload ignores Bot and TriggerPassthrough", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.buttons[1] = ButtonAction{ButtonActionType::Bot};
    cfg.buttons[2] = ButtonAction{ButtonActionType::TriggerPassthrough};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.empty());
    REQUIRE(model.actionEdits.empty());
}

TEST_CASE("MappingModel::reload loads dpadRemap correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.dpadRemap["up"] = "dpad_up";
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.size() == 1);
    REQUIRE(model.buttonEdits.at("dpad_up") == "dpad_up");
}

TEST_CASE("MappingModel::reload loads dpadActions correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    ButtonAction action{ButtonActionType::Keyboard};
    action.keys = {"alt", "tab"};
    cfg.dpadActions["up"] = action;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.actionEdits.size() == 1);
    REQUIRE(model.actionEdits.at("dpad_up").type == ButtonActionType::Keyboard);
    REQUIRE(model.actionEdits.at("dpad_up").keys == action.keys);
}

TEST_CASE("MappingModel::reload loads stickSlots correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.buttons[1] = ButtonAction{ButtonActionType::VirtualButton, "left_x_pos", "a"};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.size() == 1);
    REQUIRE(model.buttonEdits.at("a") == "left_x_pos");
}

TEST_CASE("MappingModel::reload loads triggerLAction correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    ButtonAction action{ButtonActionType::VirtualButton};
    action.name = "l2";
    cfg.triggerLAction = action;
    cfg.triggerLHasAction = true;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.trigActionEdits.size() == 1);
    REQUIRE(model.trigActionEdits.at("l2").type == ButtonActionType::VirtualButton);
    REQUIRE(model.trigActionEdits.at("l2").name == "l2");
}

TEST_CASE("MappingModel::reload loads triggerRAction correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    ButtonAction action{ButtonActionType::VirtualButton};
    action.name = "r2";
    cfg.triggerRAction = action;
    cfg.triggerRHasAction = true;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.trigActionEdits.size() == 1);
    REQUIRE(model.trigActionEdits.at("r2").type == ButtonActionType::VirtualButton);
    REQUIRE(model.trigActionEdits.at("r2").name == "r2");
}

TEST_CASE("MappingModel::reload loads triggerLRanges correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    TriggerRange range{0.0f, 1.0f};
    ButtonAction action{ButtonActionType::VirtualButton};
    action.name = "l2";
    range.action = action;
    range.hasAction = true;
    cfg.triggerLRanges.push_back(range);
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.trigLRangeEdits.size() == 1);
    REQUIRE(model.trigLRangeEdits[0].from == range.from);
    REQUIRE(model.trigLRangeEdits[0].to == range.to);
    REQUIRE(model.trigLRangeEdits[0].action.type == ButtonActionType::VirtualButton);
    REQUIRE(model.trigLRangeEdits[0].action.name == "l2");
}

TEST_CASE("MappingModel::reload loads triggerRRanges correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    TriggerRange range{0.0f, 1.0f};
    ButtonAction action{ButtonActionType::VirtualButton};
    action.name = "r2";
    range.action = action;
    range.hasAction = true;
    cfg.triggerRRanges.push_back(range);
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.trigRRangeEdits.size() == 1);
    REQUIRE(model.trigRRangeEdits[0].from == range.from);
    REQUIRE(model.trigRRangeEdits[0].to == range.to);
    REQUIRE(model.trigRRangeEdits[0].action.type == ButtonActionType::VirtualButton);
    REQUIRE(model.trigRRangeEdits[0].action.name == "r2");
}

TEST_CASE("MappingModel::reload loads axis_actions correctly", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    HalfAxisAction action{HalfAxisActionType::VirtualButton};
    action.target = "left_x_pos";
    cfg.axis_actions["left_x_pos"] = action;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.axisActionEdits.size() == 1);
    REQUIRE(model.axisActionEdits.at("left_x_pos").type == HalfAxisActionType::VirtualButton);
    REQUIRE(model.axisActionEdits.at("left_x_pos").target == "left_x_pos");
}

TEST_CASE("MappingModel::reload ignores non-matching vid/pid", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x8765;
    model.pid = 0x4321;
    model.reload(configs);

    REQUIRE(model.buttonEdits.empty());
    REQUIRE(model.actionEdits.empty());
}

TEST_CASE("MappingModel::reload handles empty config", "[MappingModel]") {
    MappingModel model;
    std::vector<ControllerConfig> configs;

    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.empty());
    REQUIRE(model.actionEdits.empty());
}

TEST_CASE("MappingModel::reload handles multiple configs", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;
    cfg.buttons[1] = ButtonAction{ButtonActionType::VirtualButton, "b", "a"};
    ButtonAction kbAction{ButtonActionType::Keyboard};
    kbAction.physical = "c";
    kbAction.keys = {"alt", "tab"};
    cfg.buttons[2] = kbAction;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.size() == 1);
    REQUIRE(model.buttonEdits.at("a") == "b");
    REQUIRE(model.actionEdits.size() == 1);
    REQUIRE(model.actionEdits.at("c").type == ButtonActionType::Keyboard);
    REQUIRE(model.actionEdits.at("c").keys == kbAction.keys);
}

TEST_CASE("MappingModel::reload handles overlapping configs", "[MappingModel]") {
    // reload() stops at the first matching config (break) — the second is ignored.
    MappingModel model;
    ControllerConfig cfg1, cfg2;
    cfg1.vid = 0x1234;
    cfg1.pid = 0x5678;
    cfg1.buttons[1] = ButtonAction{ButtonActionType::VirtualButton, "b", "a"};
    cfg2.vid = 0x1234;
    cfg2.pid = 0x5678;
    ButtonAction kbAction{ButtonActionType::Keyboard};
    kbAction.physical = "a";
    kbAction.keys = {"alt", "tab"};
    cfg2.buttons[1] = kbAction;

    std::vector<ControllerConfig> configs = {cfg1, cfg2};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.size() == 1);
    REQUIRE(model.buttonEdits.at("a") == "b");
    REQUIRE(model.actionEdits.empty());
}

TEST_CASE("MappingModel::reload handles Macro action not in buttonEdits", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    ButtonAction action{ButtonActionType::Macro};
    action.physical = "a";
    action.execution = "A,500,A";
    cfg.buttons[1] = action;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.actionEdits.size() == 1);
    REQUIRE(model.actionEdits.at("a").type == ButtonActionType::Macro);
    REQUIRE(model.actionEdits.at("a").execution == action.execution);
    REQUIRE(model.buttonEdits.empty());
}

TEST_CASE("MappingModel::reload trigger stickSlot source l2 goes to trigActionEdits", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.stickSlots["right_x_pos"] = {"l2"};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.trigActionEdits.size() == 1);
    REQUIRE(model.trigActionEdits.at("l2").type     == ButtonActionType::VirtualButton);
    REQUIRE(model.trigActionEdits.at("l2").physical == "l2");
    REQUIRE(model.trigActionEdits.at("l2").name     == "right_x_pos");
    REQUIRE(model.buttonEdits.empty());
}

TEST_CASE("MappingModel::reload trigger stickSlot source r2 goes to trigActionEdits", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.stickSlots["left_y_neg"] = {"r2"};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.trigActionEdits.size() == 1);
    REQUIRE(model.trigActionEdits.at("r2").type     == ButtonActionType::VirtualButton);
    REQUIRE(model.trigActionEdits.at("r2").physical == "r2");
    REQUIRE(model.trigActionEdits.at("r2").name     == "left_y_neg");
    REQUIRE(model.buttonEdits.empty());
}

TEST_CASE("MappingModel::reload dpad stickSlot source goes to buttonEdits", "[MappingModel]") {
    MappingModel model;
    ControllerConfig cfg;
    cfg.stickSlots["right_x_pos"] = {"dpad_up"};
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;

    std::vector<ControllerConfig> configs = {cfg};
    model.vid = 0x1234;
    model.pid = 0x5678;
    model.reload(configs);

    REQUIRE(model.buttonEdits.size() == 1);
    REQUIRE(model.buttonEdits.at("dpad_up") == "right_x_pos");
    REQUIRE(model.trigActionEdits.empty());
}