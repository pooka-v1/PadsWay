#include "config/ConfigLoader.h"
#include "input/ControllerConfig.h"
#include "ui/PadLayout.h"
#include <fstream>
#include <cstdio>
#include <catch2/catch_amalgamated.hpp>

TEST_CASE("loadControllerConfigs throws for nonexistent file", "[ConfigLoader]") {
    bool threw = false;
    try { loadControllerConfigs("__nonexistent_abc__.json"); } catch (const std::runtime_error&) { threw = true; } catch (...) {}
    CHECK(threw);
}

TEST_CASE("loadControllerConfigs parses empty controllers array", "[ConfigLoader]") {
    const std::string path = "test_tmp_controllers_empty.json";
    { std::ofstream f(path); f << R"({"controllers": []})"; }
    auto result = loadControllerConfigs(path);
    std::remove(path.c_str());
    REQUIRE(result.empty());
}

TEST_CASE("loadControllerConfigs parses one controller", "[ConfigLoader]") {
    const std::string path = "test_tmp_controllers_one.json";
    { std::ofstream f(path); 
      f << R"({
        "controllers": [{
          "vid": "0x1234",
          "pid": "0x5678",
          "source_name": "TestController",
          "mode": "gamepad",
          "connection": "",
          "buttons": {
            "1": "A"
          },
          "axes": {},
          "axis_actions": {},
          "dpad_remap": {},
          "dpad_actions": {},
          "stick_slots": {}
        }]
      })"; }
    auto result = loadControllerConfigs(path);
    std::remove(path.c_str());
    REQUIRE(result.size() == 1);
    const auto& cfg = result[0];
    REQUIRE(cfg.vid == 0x1234);
    REQUIRE(cfg.pid == 0x5678);
    REQUIRE(cfg.source_name == "TestController");
    REQUIRE(cfg.mode == "gamepad");
    REQUIRE(cfg.buttons.size() == 1);
    REQUIRE(cfg.buttons.at(1).name == "A");
}

TEST_CASE("findConfig returns nullptr for empty configs", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    const auto* result = findConfig(configs, 0x1234, 0x5678);
    REQUIRE(result == nullptr);
}

TEST_CASE("findConfig matches exact VID/PID with no discriminators", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig cfg;
    cfg.vid = 0x1234;
    cfg.pid = 0x5678;
    configs.push_back(cfg);
    const auto* result = findConfig(configs, 0x1234, 0x5678);
    REQUIRE(result != nullptr);
    REQUIRE(result->vid == 0x1234);
    REQUIRE(result->pid == 0x5678);
}

TEST_CASE("findConfig prefers connection match over generic", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig usbCfg, btCfg;
    usbCfg.vid = 0x1234; usbCfg.pid = 0x5678; usbCfg.connection = "usb";
    btCfg.vid = 0x1234; btCfg.pid = 0x5678; btCfg.connection = "";
    configs.push_back(usbCfg);
    configs.push_back(btCfg);
    const auto* result = findConfig(configs, 0x1234, 0x5678, "usb");
    REQUIRE(result != nullptr);
    REQUIRE(result->connection == "usb");
}

TEST_CASE("findConfig skips entries with non-matching discriminators", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig cfg;
    cfg.vid = 0x1234; cfg.pid = 0x5678; cfg.connection = "usb";
    configs.push_back(cfg);
    const auto* result = findConfig(configs, 0x1234, 0x5678, "bt");
    REQUIRE(result == nullptr);
}

TEST_CASE("findConfig returns nullptr for no matching VID/PID", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig cfg;
    cfg.vid = 0x1234; cfg.pid = 0x5678;
    configs.push_back(cfg);
    const auto* result = findConfig(configs, 0x9ABC, 0xDEF0);
    REQUIRE(result == nullptr);
}

TEST_CASE("loadMacroLibrary returns empty map for nonexistent file", "[ConfigLoader]") {
    auto result = loadMacroLibrary("__nonexistent_abc__.json");
    REQUIRE(result.empty());
}

TEST_CASE("loadMacroLibrary reads name-to-execution pairs", "[ConfigLoader]") {
    const std::string path = "test_tmp_macros.json";
    { std::ofstream f(path); f << R"({"jump": "A,500,A", "dodge": "B"})"; }
    auto result = loadMacroLibrary(path);
    std::remove(path.c_str());
    REQUIRE(result.size() == 2);
    REQUIRE(result.at("jump") == "A,500,A");
    REQUIRE(result.at("dodge") == "B");
}

TEST_CASE("applyProfile applies button overrides", "[ConfigLoader]") {
    ControllerConfig base;
    base.vid = 0x1234; base.pid = 0x5678;
    ButtonAction baseAct;
    baseAct.type = ButtonActionType::VirtualButton;
    baseAct.name = "a"; baseAct.physical = "a";
    base.buttons[1] = baseAct;

    ButtonAction overrideAct;
    overrideAct.type = ButtonActionType::VirtualButton;
    overrideAct.name = "b";
    GameProfile profile;
    profile.buttons["a"] = overrideAct;
    auto result = applyProfile(base, profile);
    REQUIRE(result.buttons.size() == 1);
    REQUIRE(result.buttons.at(1).name == "b");
}

TEST_CASE("applyProfile ignores overrides for virtual button not in controller", "[ConfigLoader]") {
    ControllerConfig base;
    base.vid = 0x1234; base.pid = 0x5678;
    ButtonAction baseAct;
    baseAct.type = ButtonActionType::VirtualButton;
    baseAct.name = "a"; baseAct.physical = "a";
    base.buttons[1] = baseAct;

    ButtonAction overrideAct;
    overrideAct.type = ButtonActionType::VirtualButton;
    overrideAct.name = "b";
    GameProfile profile;
    profile.buttons["x"] = overrideAct;
    auto result = applyProfile(base, profile);
    REQUIRE(result.buttons.size() == 1);
    REQUIRE(result.buttons.at(1).name == "a");
}

TEST_CASE("applyProfile applies override via physShort fallback (extra buttons)", "[ConfigLoader]") {
    ControllerConfig base;
    base.vid = 0x1234; base.pid = 0x5678;
    ButtonAction baseAct;
    baseAct.type = ButtonActionType::Macro;
    baseAct.name = "fly_macro"; baseAct.physical = "lp";
    base.buttons[5] = baseAct;

    ButtonAction overrideAct;
    overrideAct.type = ButtonActionType::Macro;
    overrideAct.name = "charge_macro";
    GameProfile profile;
    profile.buttons["lp"] = overrideAct;
    auto result = applyProfile(base, profile);
    REQUIRE(result.buttons.size() == 1);
    REQUIRE(result.buttons.at(5).name == "charge_macro");
    REQUIRE(result.buttons.at(5).physical == "lp");
}

TEST_CASE("loadPhysicalControllers throws for nonexistent file", "[ConfigLoader]") {
    bool threw = false;
    try { loadPhysicalControllers("__nonexistent_abc__.json"); } catch (const std::runtime_error&) { threw = true; } catch (...) {}
    CHECK(threw);
}

TEST_CASE("loadPhysicalControllers parses empty controllers array", "[ConfigLoader]") {
    const std::string path = "test_tmp_physical_empty.json";
    { std::ofstream f(path); f << R"({"controllers": []})"; }
    auto result = loadPhysicalControllers(path);
    std::remove(path.c_str());
    REQUIRE(result.empty());
}

TEST_CASE("loadPhysicalControllers parses one controller", "[ConfigLoader]") {
    const std::string path = "test_tmp_physical_one.json";
    { std::ofstream f(path); 
      f << R"({
        "controllers": [{
          "vid": "0x1234",
          "pid": "0x5678",
          "source_name": "TestController",
          "buttons": {
            "1": {"physical": "a", "virtual": "a"}
          },
          "axes": {},
          "axis_actions": {},
          "dpad_remap": {},
          "dpad_actions": {},
          "stick_slots": {}
        }]
      })"; }
    auto result = loadPhysicalControllers(path);
    std::remove(path.c_str());
    REQUIRE(result.size() == 1);
    const auto& ctrl = result[0];
    REQUIRE(ctrl.vid == 0x1234);
    REQUIRE(ctrl.pid == 0x5678);
    REQUIRE(ctrl.name == "TestController");
    REQUIRE(ctrl.baseLayer[static_cast<size_t>(ComponentId::BtnA)].has_value());
}

TEST_CASE("loadPadLayouts returns empty vector for nonexistent file", "[ConfigLoader]") {
    auto result = loadPadLayouts("__nonexistent_abc__.json");
    REQUIRE(result.empty());
}

TEST_CASE("loadPadLayouts parses one layout", "[ConfigLoader]") {
    const std::string path = "test_tmp_layout_one.json";
    { std::ofstream f(path); 
      f << R"({
        "layouts": [{
          "id": "TestLayout",
          "canvas": {"W": 480, "FrontH": 200, "TopH": 320},
          "components": [
            {
              "type": "button",
              "id": "btnA",
              "state": "a"
            }
          ]
        }]
      })"; }
    auto result = loadPadLayouts(path);
    std::remove(path.c_str());
    REQUIRE(result.size() == 1);
    const auto& layout = result[0];
    REQUIRE(layout.id == "TestLayout");
    REQUIRE(layout.W == 480.0f);
    REQUIRE(layout.FrontH == 200.0f);
    REQUIRE(layout.TopH == 320.0f);
    REQUIRE(layout.components.size() == 1);
    const auto& comp = layout.components[0];
    REQUIRE(comp.type == "button");
    REQUIRE(comp.id == "btnA");
    REQUIRE(comp.state == "a");
}

TEST_CASE("findLayout returns nullptr for nonexistent id", "[ConfigLoader]") {
    std::vector<PadLayout> layouts;
    PadLayout layout;
    layout.id = "TestLayout";
    layouts.push_back(layout);
    const auto* result = findLayout(layouts, "Nonexistent");
    REQUIRE(result == nullptr);
}

TEST_CASE("findLayout finds existing id", "[ConfigLoader]") {
    std::vector<PadLayout> layouts;
    PadLayout layout;
    layout.id = "TestLayout";
    layouts.push_back(layout);
    const auto* result = findLayout(layouts, "TestLayout");
    REQUIRE(result != nullptr);
    REQUIRE(result->id == "TestLayout");
}

TEST_CASE("findConfig prefers source_name match over generic fallback", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig namedCfg, genericCfg;
    namedCfg.vid  = 0x1234; namedCfg.pid  = 0x5678; namedCfg.source_name  = "DualSense";
    genericCfg.vid = 0x1234; genericCfg.pid = 0x5678; genericCfg.source_name = "";
    configs.push_back(namedCfg);
    configs.push_back(genericCfg);
    const auto* result = findConfig(configs, 0x1234, 0x5678, "", "DualSense");
    REQUIRE(result != nullptr);
    REQUIRE(result->source_name == "DualSense");
}

TEST_CASE("findConfig falls back to generic when source_name does not match named entry", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig namedCfg, genericCfg;
    namedCfg.vid   = 0x1234; namedCfg.pid   = 0x5678; namedCfg.source_name  = "DualSense";
    genericCfg.vid = 0x1234; genericCfg.pid = 0x5678; genericCfg.source_name = "";
    configs.push_back(namedCfg);
    configs.push_back(genericCfg);
    const auto* result = findConfig(configs, 0x1234, 0x5678, "", "DualShock4");
    REQUIRE(result != nullptr);
    REQUIRE(result->source_name == "");
}

TEST_CASE("findConfig skips entry with non-matching source_name when no generic fallback exists", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig cfg;
    cfg.vid = 0x1234; cfg.pid = 0x5678; cfg.source_name = "DualSense";
    configs.push_back(cfg);
    const auto* result = findConfig(configs, 0x1234, 0x5678, "", "DualShock4");
    REQUIRE(result == nullptr);
}

TEST_CASE("findConfig connection+source_name both match beats connection-only match", "[ConfigLoader]") {
    std::vector<ControllerConfig> configs;
    ControllerConfig connOnly, connAndName;
    connOnly.vid    = 0x1234; connOnly.pid    = 0x5678; connOnly.connection    = "usb";
    connAndName.vid = 0x1234; connAndName.pid = 0x5678; connAndName.connection = "usb";
    connAndName.source_name = "DualSense";
    configs.push_back(connOnly);
    configs.push_back(connAndName);
    const auto* result = findConfig(configs, 0x1234, 0x5678, "usb", "DualSense");
    REQUIRE(result != nullptr);
    REQUIRE(result->source_name == "DualSense");
}

TEST_CASE("loadGameProfile returns empty profile for nonexistent file", "[ConfigLoader]") {
    auto result = loadGameProfile("__nonexistent_abc__.json");
    REQUIRE(result.profile_name.empty());
    REQUIRE(result.buttons.empty());
    REQUIRE(result.axes.empty());
}

TEST_CASE("loadGameProfile reads profile_name from JSON", "[ConfigLoader]") {
    const std::string path = "test_tmp_profile.json";
    { std::ofstream f(path); f << R"({"profile_name": "TestGame", "buttons": {}, "axes": {}})"; }
    auto result = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE(result.profile_name == "TestGame");
    REQUIRE(result.buttons.empty());
    REQUIRE(result.axes.empty());
}

TEST_CASE("loadGameProfile leaves section flags false when sections absent", "[ConfigLoader]") {
    const std::string path = "test_tmp_profile_flags.json";
    { std::ofstream f(path); f << R"({"profile_name": "G", "buttons": {"a": {"virtual": "b"}}})"; }
    auto p = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE_FALSE(p.hasAxisActions);
    REQUIRE_FALSE(p.hasDpadRemap);
    REQUIRE_FALSE(p.hasTriggerL);
    REQUIRE_FALSE(p.hasTriggerR);
}

TEST_CASE("loadGameProfile parses axis_actions section", "[ConfigLoader]") {
    const std::string path = "test_tmp_profile_aa.json";
    { std::ofstream f(path); f << R"({
        "profile_name": "G",
        "axis_actions": {
            "right_x_pos": { "target": "mouse_x", "speed": 20.0 },
            "right_x_neg": { "target": "mouse_x", "speed": 20.0 },
            "left_y_pos":  { "virtual": "a" }
        }
    })"; }
    auto p = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE(p.hasAxisActions);
    REQUIRE(p.axis_actions.size() == 3);
    REQUIRE(p.axis_actions.at("right_x_pos").type == HalfAxisActionType::MouseMove);
    REQUIRE(p.axis_actions.at("right_x_pos").target == "mouse_x");
    REQUIRE(p.axis_actions.at("right_x_pos").speed == 20.0f);
    REQUIRE(p.axis_actions.at("left_y_pos").type == HalfAxisActionType::VirtualButton);
    REQUIRE(p.axis_actions.at("left_y_pos").target == "a");
}

TEST_CASE("loadGameProfile parses dpad_remap section", "[ConfigLoader]") {
    const std::string path = "test_tmp_profile_dpad.json";
    { std::ofstream f(path); f << R"({
        "profile_name": "G",
        "dpad_remap": {
            "up":   "a",
            "down": { "type": "keyboard", "keys": ["w"] },
            "left": "right_x_neg"
        }
    })"; }
    auto p = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE(p.hasDpadRemap);
    REQUIRE(p.dpadRemap.at("up") == "a");
    REQUIRE(p.dpadActions.at("down").type == ButtonActionType::Keyboard);
    REQUIRE(p.dpadActions.at("down").keys == std::vector<std::string>{"w"});
    REQUIRE(p.dpadSlots.at("left") == "right_x_neg");
}

TEST_CASE("loadGameProfile parses trigger_actions with null tombstone", "[ConfigLoader]") {
    const std::string path = "test_tmp_profile_trig.json";
    { std::ofstream f(path); f << R"({
        "profile_name": "G",
        "trigger_actions": {
            "l2": null,
            "r2": { "type": "keyboard", "keys": ["space"] }
        }
    })"; }
    auto p = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE(p.hasTriggerL);
    REQUIRE_FALSE(p.triggerLHasAction);
    REQUIRE(p.triggerLRanges.empty());
    REQUIRE(p.hasTriggerR);
    REQUIRE(p.triggerRHasAction);
    REQUIRE(p.triggerRAction.type == ButtonActionType::Keyboard);
}

TEST_CASE("loadGameProfile parses trigger_actions ranges", "[ConfigLoader]") {
    const std::string path = "test_tmp_profile_trigranges.json";
    { std::ofstream f(path); f << R"({
        "profile_name": "G",
        "trigger_actions": {
            "l2": { "ranges": [
                { "from": 0.0, "to": 0.5 },
                { "from": 0.5, "to": 1.0, "action": { "virtual": "a" } }
            ]}
        }
    })"; }
    auto p = loadGameProfile(path);
    std::remove(path.c_str());
    REQUIRE(p.hasTriggerL);
    REQUIRE_FALSE(p.triggerLHasAction);
    REQUIRE(p.triggerLRanges.size() == 2);
    REQUIRE_FALSE(p.triggerLRanges[0].hasAction);
    REQUIRE(p.triggerLRanges[1].hasAction);
    REQUIRE(p.triggerLRanges[1].action.name == "a");
}

TEST_CASE("applyProfile replaces axis_actions section entirely", "[ConfigLoader]") {
    ControllerConfig base;
    HalfAxisAction slot; slot.type = HalfAxisActionType::StickSlot; slot.target = "left_x_pos";
    base.axis_actions["left_x_pos"] = slot;

    GameProfile p;
    p.hasAxisActions = true;
    HalfAxisAction mm; mm.type = HalfAxisActionType::MouseMove; mm.target = "mouse_x"; mm.speed = 20.0f;
    p.axis_actions["right_x_pos"] = mm;

    auto result = applyProfile(base, p);
    REQUIRE(result.axis_actions.size() == 1);
    REQUIRE(result.axis_actions.count("left_x_pos") == 0);
    REQUIRE(result.axis_actions.at("right_x_pos").type == HalfAxisActionType::MouseMove);
}

TEST_CASE("applyProfile keeps base axis_actions when profile declares none", "[ConfigLoader]") {
    ControllerConfig base;
    HalfAxisAction slot; slot.type = HalfAxisActionType::StickSlot; slot.target = "left_x_pos";
    base.axis_actions["left_x_pos"] = slot;

    GameProfile p;  // hasAxisActions = false
    auto result = applyProfile(base, p);
    REQUIRE(result.axis_actions.size() == 1);
    REQUIRE(result.axis_actions.at("left_x_pos").type == HalfAxisActionType::StickSlot);
}

TEST_CASE("applyProfile replaces dpad section and rederives stick slots", "[ConfigLoader]") {
    ControllerConfig base;
    base.dpadRemap["down"] = "a";
    base.stickSlots["right_x_pos"].push_back("dpad_up");

    GameProfile p;
    p.hasDpadRemap = true;
    p.dpadRemap["up"]   = "b";
    p.dpadSlots["left"] = "left_x_neg";

    auto result = applyProfile(base, p);
    REQUIRE(result.dpadRemap.size() == 1);
    REQUIRE(result.dpadRemap.at("up") == "b");
    REQUIRE(result.stickSlots.count("right_x_pos") == 0);  // base dpad slot source removed
    REQUIRE(result.stickSlots.at("left_x_neg") == std::vector<std::string>{"dpad_left"});
}

TEST_CASE("applyProfile trigger tombstone resets side to default", "[ConfigLoader]") {
    ControllerConfig base;
    base.triggerLHasAction = true;
    base.triggerLAction.type = ButtonActionType::Keyboard;
    base.triggerLAction.keys = {"space"};

    GameProfile p;
    p.hasTriggerL = true;  // no action, no ranges → tombstone

    auto result = applyProfile(base, p);
    REQUIRE_FALSE(result.triggerLHasAction);
    REQUIRE(result.triggerLRanges.empty());
}

TEST_CASE("applyProfile trigger side targeting a stick slot becomes a slot source", "[ConfigLoader]") {
    ControllerConfig base;
    base.stickSlots["right_y_pos"].push_back("r2");

    GameProfile p;
    p.hasTriggerR = true;
    p.triggerRHasAction = true;
    p.triggerRAction.type = ButtonActionType::VirtualButton;
    p.triggerRAction.name = "left_y_pos";

    auto result = applyProfile(base, p);
    REQUIRE_FALSE(result.triggerRHasAction);
    REQUIRE(result.stickSlots.count("right_y_pos") == 0);  // old r2 slot removed
    REQUIRE(result.stickSlots.at("left_y_pos") == std::vector<std::string>{"r2"});
}

TEST_CASE("applyProfile button override keeps stick slot sources in sync", "[ConfigLoader]") {
    ControllerConfig base;
    base.buttons[2] = ButtonAction{ButtonActionType::VirtualButton, "right_x_pos", "b"};
    base.stickSlots["right_x_pos"].push_back("b");
    base.buttons[3] = ButtonAction{ButtonActionType::VirtualButton, "y", "c"};

    GameProfile p;
    p.buttons["right_x_pos"] = ButtonAction{ButtonActionType::VirtualButton, "x", ""};
    p.buttons["y"]           = ButtonAction{ButtonActionType::VirtualButton, "left_x_pos", ""};

    auto result = applyProfile(base, p);
    REQUIRE(result.buttons.at(2).name == "x");
    REQUIRE(result.stickSlots.count("right_x_pos") == 0);  // old slot source removed
    REQUIRE(result.stickSlots.at("left_x_pos") == std::vector<std::string>{"c"});
}

TEST_CASE("applyProfile axes override matches by stick_id", "[ConfigLoader]") {
    ControllerConfig base;
    AxisMapping m;
    m.target = "right_x"; m.stickId = "right_x";
    base.axes["hid_z"] = m;

    GameProfile p;
    AxisMapping over;
    over.target = "dpad_x"; over.stickId = "right_x";
    p.axes["right_x"] = over;

    auto result = applyProfile(base, p);
    REQUIRE(result.axes.at("hid_z").target == "dpad_x");
}

TEST_CASE("applyProfile axes stick_id match wins over target on remapped base", "[ConfigLoader]") {
    // Base maps the physical left stick to the virtual right stick.
    ControllerConfig base;
    AxisMapping m;
    m.target = "right_x"; m.stickId = "left_x";
    base.axes["hid_x"] = m;

    GameProfile p;
    AxisMapping over;
    over.target = "left_x"; over.stickId = "left_x";  // restore identity for the physical stick
    p.axes["left_x"] = over;

    auto result = applyProfile(base, p);
    REQUIRE(result.axes.at("hid_x").target == "left_x");
}

TEST_CASE("rebuildPhysicalControllerFromConfig syncs analog dirs from axis_actions", "[ConfigLoader]") {
    ControllerConfig cfg;
    AxisMapping ax; ax.target = "right_x"; ax.stickId = "right_x";
    cfg.axes["hid_z"] = ax;
    HalfAxisAction mm; mm.type = HalfAxisActionType::MouseMove; mm.target = "mouse_x"; mm.speed = 20.0f;
    cfg.axis_actions["right_x_pos"] = mm;

    PhysicalController pc;
    rebuildPhysicalControllerFromConfig(pc, cfg);

    const auto& pos = pc.baseLayer[static_cast<size_t>(ComponentId::RightXPos)];
    REQUIRE(pos.has_value());
    const auto* adPos = std::get_if<PhysicalAnalogDir>(&*pos);
    REQUIRE(adPos != nullptr);
    REQUIRE(adPos->axis.ranges.size() == 1);
    REQUIRE(std::holds_alternative<VirtualMouseMove>(adPos->axis.ranges[0].target));

    // Half without an action falls back to axis passthrough (empty ranges).
    const auto& neg = pc.baseLayer[static_cast<size_t>(ComponentId::RightXNeg)];
    REQUIRE(neg.has_value());
    const auto* adNeg = std::get_if<PhysicalAnalogDir>(&*neg);
    REQUIRE(adNeg != nullptr);
    REQUIRE(adNeg->axis.ranges.empty());

    // No axis targets the left stick → no components there.
    REQUIRE_FALSE(pc.baseLayer[static_cast<size_t>(ComponentId::LeftXPos)].has_value());
}

TEST_CASE("rebuildPhysicalControllerFromConfig resets cleared trigger to passthrough", "[ConfigLoader]") {
    ControllerConfig cfg;
    AxisMapping tl; tl.target = "trigger_l";
    cfg.axes["hid_rx"] = tl;

    // Stale component from a previous config: keyboard action on L2.
    PhysicalController pc;
    RangedHalfAxis stale;
    stale.ranges.push_back({0.5f, 1.0f, VirtualKeyboard{}});
    pc.baseLayer[static_cast<size_t>(ComponentId::TriggerL)] = PhysicalTrigger{TriggerSide::L, stale};

    rebuildPhysicalControllerFromConfig(pc, cfg);

    const auto& trig = pc.baseLayer[static_cast<size_t>(ComponentId::TriggerL)];
    REQUIRE(trig.has_value());
    const auto* pt = std::get_if<PhysicalTrigger>(&*trig);
    REQUIRE(pt != nullptr);
    REQUIRE(pt->axis.ranges.empty());  // passthrough again
}

TEST_CASE("loadVirtualPadConfig returns defaults for nonexistent file", "[ConfigLoader]") {
    auto result = loadVirtualPadConfig("__nonexistent_abc__.json");
    REQUIRE(result.vid      == 0x5650);
    REQUIRE(result.pid      == 0x0001);
    REQUIRE(result.logLevel == "info");
    REQUIRE(result.locale   == "en");
}