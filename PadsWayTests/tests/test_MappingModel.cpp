#include <catch2/catch_amalgamated.hpp>
#include "ui/MappingModel.h"
#include "input/ControllerConfig.h"
#include "config/ConfigLoader.h"
#include <cstdio>

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

// ---------------------------------------------------------------------------
// saveProfile — full parity round-trips (saveProfile → loadGameProfile →
// applyProfile must reproduce the model state)
// ---------------------------------------------------------------------------

static ControllerConfig makeProfileBase() {
    ControllerConfig base;
    base.vid = 0x1234;
    base.pid = 0x5678;
    base.buttons[1]  = ButtonAction{ButtonActionType::VirtualButton, "a", "a"};
    ButtonAction kb;
    kb.type = ButtonActionType::Keyboard;
    kb.physical = "home";
    kb.keys = {"alt", "tab"};
    base.buttons[13] = kb;
    AxisMapping ax;
    ax.target = "right_x"; ax.stickId = "right_x";
    base.axes["hid_z"] = ax;
    HalfAxisAction pos; pos.type = HalfAxisActionType::StickSlot; pos.target = "right_x_pos";
    HalfAxisAction neg; neg.type = HalfAxisActionType::StickSlot; neg.target = "right_x_neg";
    base.axis_actions["right_x_pos"] = pos;
    base.axis_actions["right_x_neg"] = neg;
    base.dpadRemap["up"] = "a";
    base.triggerLHasAction = true;
    base.triggerLAction.type = ButtonActionType::Keyboard;
    base.triggerLAction.physical = "l2";
    base.triggerLAction.keys = {"space"};
    return base;
}

TEST_CASE("saveProfile with unmodified model writes no override sections", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();
    MappingModel model;
    model.loadProfile(base, GameProfile{});

    const std::string path = "test_tmp_profile_save_clean.json";
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);
    std::remove(path.c_str());

    REQUIRE(p.profile_name == "G");
    REQUIRE(p.buttons.empty());
    REQUIRE(p.axes.empty());
    REQUIRE_FALSE(p.hasAxisActions);
    REQUIRE_FALSE(p.hasDpadRemap);
    REQUIRE_FALSE(p.hasTriggerL);
    REQUIRE_FALSE(p.hasTriggerR);
}

TEST_CASE("saveProfile round-trips mouse on right stick (axis_actions)", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();
    MappingModel model;
    model.loadProfile(base, GameProfile{});

    HalfAxisAction mm;
    mm.type = HalfAxisActionType::MouseMove; mm.target = "mouse_x"; mm.speed = 20.0f;
    model.axisActionEdits["right_x_pos"] = mm;
    model.axisActionEdits["right_x_neg"] = mm;

    const std::string path = "test_tmp_profile_save_aa.json";
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);
    std::remove(path.c_str());

    REQUIRE(p.hasAxisActions);
    REQUIRE(p.axis_actions.at("right_x_pos").type   == HalfAxisActionType::MouseMove);
    REQUIRE(p.axis_actions.at("right_x_pos").target == "mouse_x");
    REQUIRE(p.axis_actions.at("right_x_pos").speed  == 20.0f);

    auto eff = applyProfile(base, p);
    REQUIRE(eff.axis_actions.at("right_x_pos").type == HalfAxisActionType::MouseMove);
    REQUIRE(eff.axis_actions.at("right_x_neg").type == HalfAxisActionType::MouseMove);
}

TEST_CASE("saveProfile removes axis_actions section when reverted to base", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();

    // Existing profile on disk with mouse on the right stick.
    GameProfile prev;
    prev.hasAxisActions = true;
    HalfAxisAction mm;
    mm.type = HalfAxisActionType::MouseMove; mm.target = "mouse_x"; mm.speed = 15.0f;
    prev.axis_actions["right_x_pos"] = mm;
    prev.axis_actions["right_x_neg"] = mm;

    const std::string path = "test_tmp_profile_save_revert.json";
    {
        MappingModel tmp;
        tmp.loadProfile(base, prev);
        tmp.saveProfile(path, "G", base);  // seed the file with the mouse override
    }

    MappingModel model;
    model.loadProfile(base, loadGameProfile(path));
    REQUIRE(model.axisActionEdits.at("right_x_pos").type == HalfAxisActionType::MouseMove);

    // User clears the mouse: restore the identity slots (same as base).
    model.axisActionEdits = base.axis_actions;
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);
    std::remove(path.c_str());

    REQUIRE_FALSE(p.hasAxisActions);
    auto eff = applyProfile(base, p);
    REQUIRE(eff.axis_actions.at("right_x_pos").type == HalfAxisActionType::StickSlot);
}

TEST_CASE("saveProfile writes trigger tombstone when base action is cleared", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();
    MappingModel model;
    model.loadProfile(base, GameProfile{});
    REQUIRE(model.trigActionEdits.count("l2") == 1);

    model.trigActionEdits.erase("l2");  // user clears the base keyboard action

    const std::string path = "test_tmp_profile_save_trig.json";
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);
    std::remove(path.c_str());

    REQUIRE(p.hasTriggerL);
    REQUIRE_FALSE(p.triggerLHasAction);
    REQUIRE_FALSE(p.hasTriggerR);

    auto eff = applyProfile(base, p);
    REQUIRE_FALSE(eff.triggerLHasAction);
}

TEST_CASE("saveProfile writes dpad_remap when a direction changes", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();
    MappingModel model;
    model.loadProfile(base, GameProfile{});

    model.buttonEdits["dpad_up"] = "b";  // base maps it to "a"

    const std::string path = "test_tmp_profile_save_dpad.json";
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);
    std::remove(path.c_str());

    REQUIRE(p.hasDpadRemap);
    REQUIRE(p.dpadRemap.at("up") == "b");

    auto eff = applyProfile(base, p);
    REQUIRE(eff.dpadRemap.at("up") == "b");
}

TEST_CASE("saveProfile axes per-key diff against base", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();
    MappingModel model;
    model.loadProfile(base, GameProfile{});

    AxisMapping em;
    em.target = "dpad_x"; em.stickId = "right_x";
    model.axisEdits["right_x"] = em;

    const std::string path = "test_tmp_profile_save_axes.json";
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);

    REQUIRE(p.axes.at("right_x").target == "dpad_x");
    auto eff = applyProfile(base, p);
    REQUIRE(eff.axes.at("hid_z").target == "dpad_x");

    // Revert to identity → key disappears from the profile.
    model.axisEdits["right_x"].target = "right_x";
    model.saveProfile(path, "G", base);
    auto p2 = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE(p2.axes.empty());
}

TEST_CASE("saveProfile saves keyboard override differing only in keys", "[MappingModel]") {
    ControllerConfig base = makeProfileBase();
    MappingModel model;
    model.loadProfile(base, GameProfile{});
    REQUIRE(model.actionEdits.at("home").keys == std::vector<std::string>{"alt", "tab"});

    model.actionEdits.at("home").keys = {"ctrl", "c"};

    const std::string path = "test_tmp_profile_save_kb.json";
    model.saveProfile(path, "G", base);
    auto p = loadGameProfile(path);
    std::remove(path.c_str());

    REQUIRE(p.buttons.at("home").type == ButtonActionType::Keyboard);
    REQUIRE(p.buttons.at("home").keys == std::vector<std::string>{"ctrl", "c"});
}