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

TEST_CASE("loadVirtualPadConfig returns defaults for nonexistent file", "[ConfigLoader]") {
    auto result = loadVirtualPadConfig("__nonexistent_abc__.json");
    REQUIRE(result.vid      == 0x5650);
    REQUIRE(result.pid      == 0x0001);
    REQUIRE(result.logLevel == "info");
    REQUIRE(result.locale   == "en");
}