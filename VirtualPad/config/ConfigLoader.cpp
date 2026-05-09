#include "ConfigLoader.h"
#include "../nlohmann/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static ButtonAction parseButtonAction(const json& val) {
    ButtonAction action;

    if (val.is_string()) {
        // Formato corto: "b" → {physical:"b", virtual:"b"}
        action.type     = ButtonActionType::VirtualButton;
        action.name     = val.get<std::string>();
        action.physical = action.name;
        return action;
    }

    // Formato objeto — leer physical si está presente
    if (val.contains("physical"))
        action.physical = val["physical"].get<std::string>();

    // Determinar la acción
    if (val.contains("virtual")) {
        action.type = ButtonActionType::VirtualButton;
        action.name = val["virtual"].get<std::string>();
    } else if (val.contains("type")) {
        std::string type = val.at("type").get<std::string>();
        if (type == "bot") {
            action.type = ButtonActionType::Bot;
            action.name = val.at("name").get<std::string>();
        } else if (type == "macro") {
            action.type = ButtonActionType::Macro;
            action.name = val.at("name").get<std::string>();
            if (val.contains("execution"))
                action.execution = val["execution"].get<std::string>();
        } else if (type == "trigger") {
            action.type   = ButtonActionType::Trigger;
            action.target = val.at("target").get<std::string>();
            if (val.contains("axis")) action.axis = val["axis"].get<std::string>();
        } else if (type == "trigger_passthrough") {
            action.type   = ButtonActionType::TriggerPassthrough;
            action.target = val.at("target").get<std::string>();
        } else if (type == "keyboard") {
            action.type = ButtonActionType::Keyboard;
            if (val.contains("keys"))
                for (const auto& k : val["keys"])
                    action.keys.push_back(k.get<std::string>());
        } else if (type == "mouse_click") {
            action.type        = ButtonActionType::MouseClick;
            action.mouseButton = val.value("button", "left");
        }
    }
    // else: solo physical, sin acción (type queda VirtualButton con name vacío)

    return action;
}

// Parses an axis_actions JSON object into a map.
// Key format: "<virtual_axis>_pos" or "<virtual_axis>_neg" (e.g. "left_x_pos", "right_y_neg")
static std::unordered_map<std::string, HalfAxisAction> parseAxisActionsJson(const json& j) {
    std::unordered_map<std::string, HalfAxisAction> result;
    for (const auto& [key, val] : j.items()) {
        if (!key.empty() && key[0] == '_') continue;
        HalfAxisAction a;

        if (val.contains("ranges") && val["ranges"].is_array()) {
            a.type = HalfAxisActionType::Ranges;
            for (const auto& r : val["ranges"]) {
                TriggerRange tr;
                tr.from = r.value("from", 0.0f);
                tr.to   = r.value("to",   1.0f);
                if (r.contains("action")) {
                    tr.action    = parseButtonAction(r["action"]);
                    tr.hasAction = true;
                }
                a.ranges.push_back(tr);
            }
        } else if (val.contains("virtual")) {
            std::string v = val["virtual"].get<std::string>();
            if (v == "dpad_up" || v == "dpad_down" || v == "dpad_left" || v == "dpad_right") {
                a.type   = HalfAxisActionType::Dpad;
                a.target = v.substr(5); // strip "dpad_" → "up"|"down"|"left"|"right"
            } else if (isStickSlotDir(v)) {
                a.type   = HalfAxisActionType::StickSlot;
                a.target = v;
            } else if (v == "l2" || v == "r2" || v == "trigger_l" || v == "trigger_r") {
                a.type   = HalfAxisActionType::Trigger;
                a.target = v;
            } else {
                a.type   = HalfAxisActionType::VirtualButton;
                a.target = v;
            }
            a.threshold = val.value("threshold", 0.5f);
        } else if (val.contains("target") && !val.contains("type")) {
            // Proportional mouse: { "target": "mouse_x|mouse_y", "speed": N }
            a.type   = HalfAxisActionType::MouseMove;
            a.target = val["target"].get<std::string>();
            a.speed  = val.value("speed", 15.0f);
        } else if (val.contains("type")) {
            std::string type = val["type"].get<std::string>();
            if (type == "keyboard") {
                a.type = HalfAxisActionType::Keyboard;
                if (val.contains("keys"))
                    for (const auto& k : val["keys"])
                        a.keys.push_back(k.get<std::string>());
            } else if (type == "macro") {
                a.type      = HalfAxisActionType::Macro;
                a.target    = val.value("name", val.value("target", std::string{}));
                a.execution = val.value("execution", std::string{});
            } else if (type == "mouse_click") {
                a.type        = HalfAxisActionType::MouseClick;
                a.mouseButton = val.value("button", "left");
            } else if (type == "analog") {
                // Legacy: { "type": "analog", "target": "left_x", "out_dir": "pos", "scale": 1.0 }
                a.type   = HalfAxisActionType::Analog;
                a.target = val.value("target",  std::string{});
                a.outDir = val.value("out_dir", std::string{});
                a.scale  = val.value("scale",   1.0f);
            } else if (type == "button") {
                a.type   = HalfAxisActionType::VirtualButton;
                a.target = val.value("target", std::string{});
            } else if (type == "dpad") {
                a.type   = HalfAxisActionType::Dpad;
                a.target = val.value("target", std::string{});
            } else if (type == "mouse") {
                a.type        = HalfAxisActionType::MouseClick;
                a.mouseButton = val.value("button", "left");
            }
            a.threshold = val.value("threshold", 0.5f);
        }

        result[key] = std::move(a);
    }
    return result;
}

// Parses a buttons JSON object into a map.
// Keys starting with '_' are skipped (they are pseudo-comments).
static std::unordered_map<int, ButtonAction> parseButtonsJson(const json& buttonsJson) {
    std::unordered_map<int, ButtonAction> result;
    for (const auto& [key, val] : buttonsJson.items()) {
        if (!key.empty() && key[0] == '_') continue;
        result[std::stoi(key)] = parseButtonAction(val);
    }
    return result;
}

// ---------------------------------------------------------------------------

std::vector<ControllerConfig> loadControllerConfigs(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);

    json root = json::parse(f);
    std::vector<ControllerConfig> result;

    for (const auto& c : root.at("controllers")) {
        ControllerConfig cfg;
        cfg.vid          = static_cast<uint16_t>(std::stoul(c.at("vid").get<std::string>(), nullptr, 16));
        cfg.pid          = static_cast<uint16_t>(std::stoul(c.at("pid").get<std::string>(), nullptr, 16));
        cfg.source_name  = c.at("source_name").get<std::string>();
        cfg.mode         = c.at("mode").get<std::string>();
        cfg.dpad         = c.value("dpad", "");
        cfg.layout_id    = c.value("layout_id", "");
        cfg.connection   = c.value("connection", "");
        cfg.buttons     = parseButtonsJson(c.at("buttons"));
        // Derive stick slot assignments from button entries (virtual = slot direction).
        for (const auto& [bit, action] : cfg.buttons) {
            if (action.type == ButtonActionType::VirtualButton &&
                !action.physical.empty() && isStickSlotDir(action.name))
                cfg.stickSlots[action.name].push_back(action.physical);
        }

        for (const auto& [source, axisJson] : c.at("axes").items()) {
            AxisMapping m;
            m.target    = axisJson.at("target").get<std::string>();
            m.invert    = axisJson.value("invert",    false);
            m.speed     = axisJson.value("speed",     15.0f);
            m.stickId   = axisJson.value("stick_id",  std::string{});
            m.btnNeg    = axisJson.value("btn_neg",   std::string{});
            m.btnPos    = axisJson.value("btn_pos",   std::string{});
            m.threshold = axisJson.value("threshold", 0.5f);
            // infer stickId from target for first-time load (no stick_id field yet)
            if (m.stickId.empty() &&
                (m.target == "left_x" || m.target == "left_y" ||
                 m.target == "right_x" || m.target == "right_y"))
                m.stickId = m.target;
            cfg.axes[source] = m;
        }

        if (c.contains("axis_actions"))
            cfg.axis_actions = parseAxisActionsJson(c.at("axis_actions"));

        if (c.contains("dpad_remap") && c["dpad_remap"].is_object())
            for (const auto& [dir, btn] : c["dpad_remap"].items()) {
                if (btn.is_string()) {
                    const std::string val = btn.get<std::string>();
                    if (isStickSlotDir(val))
                        cfg.stickSlots[val].push_back("dpad_" + dir);
                    else
                        cfg.dpadRemap[dir] = val;
                } else if (btn.is_object())
                    cfg.dpadActions[dir] = parseButtonAction(btn);
            }

        if (c.contains("trigger_actions") && c["trigger_actions"].is_object()) {
            const auto& ta = c["trigger_actions"];
            auto parseTrigSide = [&](const char* key,
                                     ButtonAction& simpleAct, bool& hasSimple,
                                     std::vector<TriggerRange>& ranges) {
                if (!ta.contains(key)) return;
                const auto& t = ta[key];
                if (t.is_object() && t.contains("ranges") && t["ranges"].is_array()) {
                    for (const auto& r : t["ranges"]) {
                        TriggerRange tr;
                        tr.from   = r.value("from", 0.0f);
                        tr.to     = r.value("to",   1.0f);
                        if (r.contains("action")) {
                            tr.action    = parseButtonAction(r["action"]);
                            tr.hasAction = true;
                        }
                        ranges.push_back(tr);
                    }
                } else {
                    simpleAct  = parseButtonAction(t);
                    hasSimple  = true;
                }
            };
            parseTrigSide("l2", cfg.triggerLAction, cfg.triggerLHasAction, cfg.triggerLRanges);
            parseTrigSide("r2", cfg.triggerRAction, cfg.triggerRHasAction, cfg.triggerRRanges);

            // Derive stickSlots from simple trigger actions targeting a slot direction.
            auto deriveTrigSlot = [&](ButtonAction& act, bool& hasAct, const char* src) {
                if (hasAct && act.type == ButtonActionType::VirtualButton &&
                    isStickSlotDir(act.name)) {
                    cfg.stickSlots[act.name].push_back(src);
                    hasAct = false;
                }
            };
            deriveTrigSlot(cfg.triggerLAction, cfg.triggerLHasAction, "l2");
            deriveTrigSlot(cfg.triggerRAction, cfg.triggerRHasAction, "r2");
        }

        if (c.contains("stick_slots") && c["stick_slots"].is_object()) {
            for (const auto& [slot, val] : c["stick_slots"].items()) {
                if (val.is_string())
                    cfg.stickSlots[slot].push_back(val.get<std::string>());
                else if (val.is_object() && val.contains("source"))
                    cfg.stickSlots[slot].push_back(val["source"].get<std::string>());
            }
        }

        if (c.contains("touchpad")) {
            const auto& tp        = c["touchpad"];
            cfg.touchpad.enabled      = tp.value("enabled",       false);
            cfg.touchpad.dataOffset   = tp.value("data_offset",   34);
            cfg.touchpad.maxX         = tp.value("max_x",         1919);
            cfg.touchpad.maxY         = tp.value("max_y",         942);
            cfg.touchpad.mouseEnabled = tp.value("mouse_enabled", false);
        }

        if (c.contains("imu")) {
            const auto& im     = c["imu"];
            cfg.imu.enabled    = im.value("enabled",     false);
            cfg.imu.gyroOffset = im.value("gyro_offset", 13);
            cfg.imu.gyroScale  = im.value("gyro_scale",  1.0f / 32768.0f);
        }

        result.push_back(std::move(cfg));
    }

    return result;
}

const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid,
                                   const std::string& connection,
                                   const std::string& sourceName) {
    const ControllerConfig* best      = nullptr;
    int                     bestScore = -1;

    for (const auto& c : configs) {
        if (c.vid != vid || c.pid != pid) continue;

        int score = 0;

        // connection: if entry declares it, incoming must match or we skip
        if (!c.connection.empty()) {
            if (connection.empty()) continue;
            if (c.connection != connection) continue;
            score += 2;
        }

        // source_name: only used when caller provides it (e.g. wizard re-pair)
        // Runtime engine always passes "" → source_name is ignored, entry stays eligible
        if (!sourceName.empty() && !c.source_name.empty()) {
            if (c.source_name != sourceName) continue; // explicit mismatch → skip
            score += 1;
        }

        if (score > bestScore) {
            bestScore = score;
            best      = &c;
        }
    }
    return best;
}

std::unordered_map<std::string, std::string> loadMacroLibrary(const std::string& path) {
    std::unordered_map<std::string, std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;  // optional file — no error if missing
    json root = json::parse(f);
    for (const auto& [name, val] : root.items())
        result[name] = val.get<std::string>();
    return result;
}

VirtualPadConfig loadVirtualPadConfig(const std::string& path) {
    VirtualPadConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;  // optional file — return defaults
    json root = json::parse(f);
    if (root.contains("virtual_vid"))
        cfg.vid = static_cast<uint16_t>(std::stoul(root["virtual_vid"].get<std::string>(), nullptr, 16));
    if (root.contains("virtual_pid"))
        cfg.pid = static_cast<uint16_t>(std::stoul(root["virtual_pid"].get<std::string>(), nullptr, 16));
    if (root.contains("log_level"))
        cfg.logLevel = root["log_level"].get<std::string>();
    if (root.contains("locale"))
        cfg.locale = root["locale"].get<std::string>();
    if (root.contains("pad_configurations") && root["pad_configurations"].is_object()) {
        const auto& pc = root["pad_configurations"];
        if (pc.contains("accepted_xbox_buttons") && pc["accepted_xbox_buttons"].is_array()) {
            cfg.acceptedXboxButtons.clear();
            for (const auto& b : pc["accepted_xbox_buttons"])
                cfg.acceptedXboxButtons.push_back(b.get<std::string>());
        }
        if (pc.contains("stick_select_threshold") && pc["stick_select_threshold"].is_number())
            cfg.stickSelectThreshold = pc["stick_select_threshold"].get<float>();
        if (pc.contains("stick_hold_ms") && pc["stick_hold_ms"].is_number_integer())
            cfg.stickHoldMs = pc["stick_hold_ms"].get<int>();
    }
    return cfg;
}

GameProfile loadGameProfile(const std::string& path) {
    GameProfile profile;
    std::ifstream f(path);
    if (!f.is_open()) return profile;

    json root = json::parse(f);
    profile.profile_name = root.value("profile_name", "");

    for (const auto& ov : root.value("overrides", json::array())) {
        GameProfile::Override o;
        o.vid = static_cast<uint16_t>(std::stoul(ov.at("vid").get<std::string>(), nullptr, 16));
        o.pid = static_cast<uint16_t>(std::stoul(ov.at("pid").get<std::string>(), nullptr, 16));
        if (ov.contains("buttons"))
            o.buttons = parseButtonsJson(ov.at("buttons"));
        if (ov.contains("axes")) {
            for (const auto& [source, axisJson] : ov.at("axes").items()) {
                AxisMapping m;
                m.target    = axisJson.at("target").get<std::string>();
                m.invert    = axisJson.value("invert",    false);
                m.speed     = axisJson.value("speed",     15.0f);
                m.stickId   = axisJson.value("stick_id",  std::string{});
                m.btnNeg    = axisJson.value("btn_neg",   std::string{});
                m.btnPos    = axisJson.value("btn_pos",   std::string{});
                m.threshold = axisJson.value("threshold", 0.5f);
                if (m.stickId.empty() &&
                    (m.target == "left_x" || m.target == "left_y" ||
                     m.target == "right_x" || m.target == "right_y"))
                    m.stickId = m.target;
                o.axes[source] = m;
            }
        }
        if (ov.contains("axis_actions"))
            o.axis_actions = parseAxisActionsJson(ov.at("axis_actions"));
        profile.overrides.push_back(std::move(o));
    }
    return profile;
}

ControllerConfig applyProfile(const ControllerConfig& base, const GameProfile& profile) {
    ControllerConfig result = base;
    for (const auto& ov : profile.overrides) {
        if (ov.vid != base.vid || ov.pid != base.pid) continue;
        for (const auto& [bit, override_action] : ov.buttons) {
            // Preservar el physical del botón base aunque el perfil sobreescriba la acción
            std::string phys;
            auto it = result.buttons.find(bit);
            if (it != result.buttons.end())
                phys = it->second.physical;

            result.buttons[bit] = override_action;

            if (!phys.empty() && result.buttons[bit].physical.empty())
                result.buttons[bit].physical = phys;
        }
        for (const auto& [source, mapping] : ov.axes)
            result.axes[source] = mapping;
        for (const auto& [key, action] : ov.axis_actions)
            result.axis_actions[key] = action;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Component System — PhysicalController
// ---------------------------------------------------------------------------

static std::optional<ComponentId> physicalNameToComponentId(const std::string& phys) {
    if (phys == "a")                     return ComponentId::BtnA;
    if (phys == "b")                     return ComponentId::BtnB;
    if (phys == "x")                     return ComponentId::BtnX;
    if (phys == "y")                     return ComponentId::BtnY;
    if (phys == "l1")                    return ComponentId::BtnLB;
    if (phys == "r1")                    return ComponentId::BtnRB;
    if (phys == "l3")                    return ComponentId::BtnL3;
    if (phys == "r3")                    return ComponentId::BtnR3;
    if (phys == "select" || phys == "back") return ComponentId::BtnBack;
    if (phys == "start")                 return ComponentId::BtnStart;
    if (phys == "home")                  return ComponentId::BtnHome;
    if (phys == "l4")                    return ComponentId::BtnL4;
    if (phys == "r4")                    return ComponentId::BtnR4;
    if (phys == "lp" || phys == "l5")   return ComponentId::BtnLP;
    if (phys == "rp" || phys == "r5")   return ComponentId::BtnRP;
    return std::nullopt;
}

static std::optional<ButtonId> virtualNameToButtonId(const std::string& name) {
    if (name == "a")                        return ButtonId::A;
    if (name == "b")                        return ButtonId::B;
    if (name == "x")                        return ButtonId::X;
    if (name == "y")                        return ButtonId::Y;
    if (name == "l1" || name == "lb")       return ButtonId::LB;
    if (name == "r1" || name == "rb")       return ButtonId::RB;
    if (name == "l3")                       return ButtonId::L3;
    if (name == "r3")                       return ButtonId::R3;
    if (name == "select" || name == "back") return ButtonId::Back;
    if (name == "start")                    return ButtonId::Start;
    if (name == "home")                     return ButtonId::Home;
    return std::nullopt;
}

static std::optional<StickSlotId> slotStringToStickSlotId(const std::string& s) {
    if (s == "left_x_pos")  return StickSlotId::LeftXPos;
    if (s == "left_x_neg")  return StickSlotId::LeftXNeg;
    if (s == "left_y_pos")  return StickSlotId::LeftYPos;
    if (s == "left_y_neg")  return StickSlotId::LeftYNeg;
    if (s == "right_x_pos") return StickSlotId::RightXPos;
    if (s == "right_x_neg") return StickSlotId::RightXNeg;
    if (s == "right_y_pos") return StickSlotId::RightYPos;
    if (s == "right_y_neg") return StickSlotId::RightYNeg;
    return std::nullopt;
}

static std::optional<ComponentId> slotStringToComponentId(const std::string& s) {
    if (s == "left_x_pos")  return ComponentId::LeftXPos;
    if (s == "left_x_neg")  return ComponentId::LeftXNeg;
    if (s == "left_y_pos")  return ComponentId::LeftYPos;
    if (s == "left_y_neg")  return ComponentId::LeftYNeg;
    if (s == "right_x_pos") return ComponentId::RightXPos;
    if (s == "right_x_neg") return ComponentId::RightXNeg;
    if (s == "right_y_pos") return ComponentId::RightYPos;
    if (s == "right_y_neg") return ComponentId::RightYNeg;
    return std::nullopt;
}

static MouseButton stringToMouseButton2(const std::string& s) {
    if (s == "right")   return MouseButton::Right;
    if (s == "middle")  return MouseButton::Middle;
    if (s == "forward") return MouseButton::Forward;
    if (s == "back")    return MouseButton::Back;
    return MouseButton::Left;
}

static MouseAxis stringToMouseAxis(const std::string& s) {
    return (s == "mouse_y") ? MouseAxis::Y : MouseAxis::X;
}

// Translates a ButtonAction (existing system) to a VirtualTarget.
// Returns nullopt when the action has no meaningful virtual output.
static std::optional<VirtualTarget> buttonActionToVT(const ButtonAction& action) {
    switch (action.type) {
        case ButtonActionType::VirtualButton: {
            if (action.name.empty()) return std::nullopt;
            if (action.name == "dpad_up")    return VirtualDpadDir{DpadDir::Up};
            if (action.name == "dpad_down")  return VirtualDpadDir{DpadDir::Down};
            if (action.name == "dpad_left")  return VirtualDpadDir{DpadDir::Left};
            if (action.name == "dpad_right") return VirtualDpadDir{DpadDir::Right};
            if (isStickSlotDir(action.name)) {
                auto slot = slotStringToStickSlotId(action.name);
                if (slot) return VirtualStickSlot{*slot};
            }
            if (action.name == "l2" || action.name == "trigger_l") return VirtualTrigger{TriggerSide::L};
            if (action.name == "r2" || action.name == "trigger_r") return VirtualTrigger{TriggerSide::R};
            auto bid = virtualNameToButtonId(action.name);
            if (bid) return VirtualButton{*bid};
            return std::nullopt;
        }
        case ButtonActionType::Trigger:
            if (action.target == "l2") return VirtualTrigger{TriggerSide::L};
            if (action.target == "r2") return VirtualTrigger{TriggerSide::R};
            return VirtualPassthrough{};
        case ButtonActionType::TriggerPassthrough:
            return VirtualPassthrough{};
        case ButtonActionType::Bot:
            return VirtualBot{action.name};
        case ButtonActionType::Macro:
            return VirtualMacro{action.name};
        case ButtonActionType::Keyboard:
            return VirtualKeyboard{};   // key codes are strings in existing system; left empty, resolved in P3
        case ButtonActionType::MouseClick:
            return VirtualMouseClick{stringToMouseButton2(action.mouseButton)};
    }
    return std::nullopt;
}

// Translates a HalfAxisAction (existing system) to a VirtualTarget.
static std::optional<VirtualTarget> halfAxisActionToVT(const HalfAxisAction& action) {
    switch (action.type) {
        case HalfAxisActionType::VirtualButton: {
            auto bid = virtualNameToButtonId(action.target);
            if (bid) return VirtualButton{*bid};
            return std::nullopt;
        }
        case HalfAxisActionType::Dpad:
            if (action.target == "up")    return VirtualDpadDir{DpadDir::Up};
            if (action.target == "down")  return VirtualDpadDir{DpadDir::Down};
            if (action.target == "left")  return VirtualDpadDir{DpadDir::Left};
            if (action.target == "right") return VirtualDpadDir{DpadDir::Right};
            return std::nullopt;
        case HalfAxisActionType::Trigger:
            if (action.target == "l2" || action.target == "trigger_l") return VirtualTrigger{TriggerSide::L};
            if (action.target == "r2" || action.target == "trigger_r") return VirtualTrigger{TriggerSide::R};
            return std::nullopt;
        case HalfAxisActionType::StickSlot: {
            auto slot = slotStringToStickSlotId(action.target);
            if (slot) return VirtualStickSlot{*slot};
            return std::nullopt;
        }
        case HalfAxisActionType::Keyboard:
            return VirtualKeyboard{};
        case HalfAxisActionType::Macro:
            return VirtualMacro{action.target};
        case HalfAxisActionType::MouseClick:
            return VirtualMouseClick{stringToMouseButton2(action.mouseButton)};
        case HalfAxisActionType::MouseMove:
            return VirtualMouseMove{stringToMouseAxis(action.target), action.speed};
        case HalfAxisActionType::Analog:
        case HalfAxisActionType::Ranges:
            return std::nullopt;  // handled by caller
    }
    return std::nullopt;
}

// Builds a RangedHalfAxis from a simple trigger action + optional ranges.
static RangedHalfAxis buildRangedHalfAxisFromTrigger(const ButtonAction& simple, bool hasAction,
                                                      const std::vector<TriggerRange>& ranges) {
    RangedHalfAxis rha;
    if (!ranges.empty()) {
        for (const auto& tr : ranges) {
            if (!tr.hasAction) continue;
            auto vt = buttonActionToVT(tr.action);
            if (vt) rha.ranges.push_back({tr.from, tr.to, *vt});
        }
    } else if (hasAction) {
        auto vt = buttonActionToVT(simple);
        if (vt) {
            bool digital = std::holds_alternative<VirtualButton>(*vt)     ||
                           std::holds_alternative<VirtualDpadDir>(*vt)    ||
                           std::holds_alternative<VirtualMacro>(*vt)      ||
                           std::holds_alternative<VirtualKeyboard>(*vt)   ||
                           std::holds_alternative<VirtualMouseClick>(*vt);
            rha.ranges.push_back({digital ? 0.5f : 0.0f, 1.0f, *vt});
        }
        // empty ranges = implicit VirtualPassthrough
    }
    return rha;
}

// Builds a RangedHalfAxis from a HalfAxisAction (axis_actions).
static RangedHalfAxis buildRangedHalfAxisFromHalf(const HalfAxisAction& action) {
    RangedHalfAxis rha;
    if (action.type == HalfAxisActionType::Ranges) {
        for (const auto& tr : action.ranges) {
            if (!tr.hasAction) continue;
            auto vt = buttonActionToVT(tr.action);
            if (vt) rha.ranges.push_back({tr.from, tr.to, *vt});
        }
    } else {
        auto vt = halfAxisActionToVT(action);
        if (vt) {
            // Digital targets (VirtualButton, Dpad) fire at value=1.0 regardless of magnitude.
            // Using threshold as the lower bound prevents them from activating at stick rest (value=0).
            // Proportional targets (StickSlot, Trigger, MouseMove, Analog) start from 0.
            bool digital = (action.type == HalfAxisActionType::VirtualButton ||
                            action.type == HalfAxisActionType::Dpad);
            rha.ranges.push_back({digital ? action.threshold : 0.0f, 1.0f, *vt});
        }
    }
    return rha;
}

static PhysicalController parsePhysicalController(const json& c) {
    PhysicalController ctrl;
    ctrl.vid  = static_cast<uint16_t>(std::stoul(c.at("vid").get<std::string>(), nullptr, 16));
    ctrl.pid  = static_cast<uint16_t>(std::stoul(c.at("pid").get<std::string>(), nullptr, 16));
    ctrl.name = c.value("source_name", "");

    auto setBase = [&](ComponentId id, PhysicalComponent comp) {
        ctrl.baseLayer[static_cast<size_t>(id)] = std::move(comp);
    };

    // ── Buttons ──────────────────────────────────────────────────────────────
    if (c.contains("buttons")) {
        for (const auto& [key, val] : c["buttons"].items()) {
            if (!key.empty() && key[0] == '_') continue;
            ButtonAction action = parseButtonAction(val);
            if (action.physical.empty()) continue;
            auto cid = physicalNameToComponentId(action.physical);
            if (!cid) continue;
            auto vt = buttonActionToVT(action);
            if (!vt) continue;   // no virtual output (unbound paddle, etc.)
            uint8_t bit = static_cast<uint8_t>(std::stoi(key));
            setBase(*cid, PhysicalButton{bit, *vt});
        }
    }

    // ── Dpad ─────────────────────────────────────────────────────────────────
    // Default: each direction passes through to its natural DpadDir.
    // Overridden by dpad_remap / dpad_actions entries.
    if (!c.value("dpad", "").empty()) {
        static const std::pair<const char*, std::pair<DpadDir, ComponentId>> kDpadDefaults[] = {
            {"up",    {DpadDir::Up,    ComponentId::DpadUp}},
            {"down",  {DpadDir::Down,  ComponentId::DpadDown}},
            {"left",  {DpadDir::Left,  ComponentId::DpadLeft}},
            {"right", {DpadDir::Right, ComponentId::DpadRight}},
        };
        for (auto& [dirStr, dc] : kDpadDefaults)
            setBase(dc.second, PhysicalDpadDir{dc.first, VirtualPassthrough{}});

        if (c.contains("dpad_remap") && c["dpad_remap"].is_object()) {
            for (const auto& [dirStr, val] : c["dpad_remap"].items()) {
                DpadDir ddir; ComponentId cid;
                if      (dirStr == "up")    { ddir = DpadDir::Up;    cid = ComponentId::DpadUp;    }
                else if (dirStr == "down")  { ddir = DpadDir::Down;  cid = ComponentId::DpadDown;  }
                else if (dirStr == "left")  { ddir = DpadDir::Left;  cid = ComponentId::DpadLeft;  }
                else if (dirStr == "right") { ddir = DpadDir::Right; cid = ComponentId::DpadRight; }
                else continue;

                if (val.is_string()) {
                    std::string vtStr = val.get<std::string>();
                    if (isStickSlotDir(vtStr)) {
                        auto slot = slotStringToStickSlotId(vtStr);
                        if (slot) setBase(cid, PhysicalDpadDir{ddir, VirtualStickSlot{*slot}});
                    } else {
                        auto bid = virtualNameToButtonId(vtStr);
                        if (bid) setBase(cid, PhysicalDpadDir{ddir, VirtualButton{*bid}});
                    }
                } else if (val.is_object()) {
                    ButtonAction action = parseButtonAction(val);
                    auto vt = buttonActionToVT(action);
                    if (vt) setBase(cid, PhysicalDpadDir{ddir, *vt});
                }
            }
        }

        if (c.contains("dpad_actions") && c["dpad_actions"].is_object()) {
            for (const auto& [dirStr, val] : c["dpad_actions"].items()) {
                DpadDir ddir; ComponentId cid;
                if      (dirStr == "up")    { ddir = DpadDir::Up;    cid = ComponentId::DpadUp;    }
                else if (dirStr == "down")  { ddir = DpadDir::Down;  cid = ComponentId::DpadDown;  }
                else if (dirStr == "left")  { ddir = DpadDir::Left;  cid = ComponentId::DpadLeft;  }
                else if (dirStr == "right") { ddir = DpadDir::Right; cid = ComponentId::DpadRight; }
                else continue;
                ButtonAction action = parseButtonAction(val);
                auto vt = buttonActionToVT(action);
                if (vt) setBase(cid, PhysicalDpadDir{ddir, *vt});
            }
        }
    }

    // ── Triggers ─────────────────────────────────────────────────────────────
    if (c.contains("trigger_actions") && c["trigger_actions"].is_object()) {
        const auto& ta = c["trigger_actions"];
        auto parseTrig = [&](const char* key, TriggerSide side, ComponentId cid) {
            if (!ta.contains(key)) return;
            const auto& t = ta[key];
            ButtonAction simple;
            bool hasSimple = false;
            std::vector<TriggerRange> ranges;
            if (t.is_object() && t.contains("ranges") && t["ranges"].is_array()) {
                for (const auto& r : t["ranges"]) {
                    TriggerRange tr;
                    tr.from = r.value("from", 0.0f);
                    tr.to   = r.value("to",   1.0f);
                    if (r.contains("action")) {
                        tr.action    = parseButtonAction(r["action"]);
                        tr.hasAction = true;
                    }
                    ranges.push_back(tr);
                }
            } else {
                simple   = parseButtonAction(t);
                hasSimple = true;
            }
            setBase(cid, PhysicalTrigger{side,
                buildRangedHalfAxisFromTrigger(simple, hasSimple, ranges)});
        };
        parseTrig("l2", TriggerSide::L, ComponentId::TriggerL);
        parseTrig("r2", TriggerSide::R, ComponentId::TriggerR);
    }

    // ── Axis actions (per-half-axis) ─────────────────────────────────────────
    if (c.contains("axis_actions") && c["axis_actions"].is_object()) {
        auto axisMap = parseAxisActionsJson(c["axis_actions"]);
        for (const auto& [key, action] : axisMap) {
            auto cid    = slotStringToComponentId(key);
            auto slotId = slotStringToStickSlotId(key);
            if (!cid || !slotId) continue;
            setBase(*cid, PhysicalAnalogDir{*slotId, buildRangedHalfAxisFromHalf(action)});
        }
    }

    // ── Whole-axis routing → passthrough components ──────────────────────────
    // For stick/trigger targets: add VirtualPassthrough half-axis components so
    // PhysicalController::process() drives them. Skip targets handled by applyAxesResidual
    // (mouse_x/y, dpad_x/y, trigger_combined, btn_dir) — those stay in the residual path.
    if (c.contains("axes") && c["axes"].is_object()) {
        for (const auto& [source, axisJson] : c["axes"].items()) {
            if (!axisJson.is_object()) continue;
            std::string target = axisJson.value("target", "");

            auto addAnalogPair = [&](ComponentId pos, ComponentId neg,
                                     StickSlotId sPos, StickSlotId sNeg) {
                size_t ip = static_cast<size_t>(pos), in_ = static_cast<size_t>(neg);
                if (!ctrl.baseLayer[ip]) ctrl.baseLayer[ip] = PhysicalAnalogDir{sPos, {}};
                if (!ctrl.baseLayer[in_]) ctrl.baseLayer[in_] = PhysicalAnalogDir{sNeg, {}};
            };

            if      (target == "left_x")  addAnalogPair(ComponentId::LeftXPos,  ComponentId::LeftXNeg,  StickSlotId::LeftXPos,  StickSlotId::LeftXNeg);
            else if (target == "left_y")  addAnalogPair(ComponentId::LeftYPos,  ComponentId::LeftYNeg,  StickSlotId::LeftYPos,  StickSlotId::LeftYNeg);
            else if (target == "right_x") addAnalogPair(ComponentId::RightXPos, ComponentId::RightXNeg, StickSlotId::RightXPos, StickSlotId::RightXNeg);
            else if (target == "right_y") addAnalogPair(ComponentId::RightYPos, ComponentId::RightYNeg, StickSlotId::RightYPos, StickSlotId::RightYNeg);
            else if (target == "trigger_l") {
                size_t i = static_cast<size_t>(ComponentId::TriggerL);
                if (!ctrl.baseLayer[i]) ctrl.baseLayer[i] = PhysicalTrigger{TriggerSide::L, {}};
            } else if (target == "trigger_r") {
                size_t i = static_cast<size_t>(ComponentId::TriggerR);
                if (!ctrl.baseLayer[i]) ctrl.baseLayer[i] = PhysicalTrigger{TriggerSide::R, {}};
            }
        }
    }

    // ── Touchpad ─────────────────────────────────────────────────────────────
    if (c.contains("touchpad")) {
        const auto& tp = c["touchpad"];
        TouchpadConfig tpc;
        tpc.enabled      = tp.value("enabled",       false);
        tpc.dataOffset   = tp.value("data_offset",   34);
        tpc.maxX         = tp.value("max_x",         1919);
        tpc.maxY         = tp.value("max_y",         942);
        tpc.mouseEnabled = tp.value("mouse_enabled", false);
        setBase(ComponentId::Touchpad, PhysicalTouchpad{tpc});
    }

    // ── Gyro ─────────────────────────────────────────────────────────────────
    if (c.contains("imu") && c["imu"].value("enabled", false))
        setBase(ComponentId::Gyro, PhysicalGyro{});

    return ctrl;
}

std::vector<PhysicalController> loadPhysicalControllers(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);

    json root = json::parse(f);
    std::vector<PhysicalController> result;
    for (const auto& c : root.at("controllers")) {
        if (c.contains("_") || c.value("source_name", "").empty()) continue;
        result.push_back(parsePhysicalController(c));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Pad layouts
// ---------------------------------------------------------------------------

static float jf(const json& obj, const char* key, float def) {
    return obj.contains(key) ? obj[key].get<float>() : def;
}

std::vector<PadLayout> loadPadLayouts(const std::string& path) {
    std::vector<PadLayout> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;

    json root = json::parse(f);

    auto parseColor4 = [](const json& obj, const char* key,
                          float& r, float& g, float& b, float& a) {
        if (!obj.contains(key) || !obj[key].is_array() || obj[key].size() < 3) return;
        const auto& arr = obj[key];
        r = arr[0].get<float>();
        g = arr[1].get<float>();
        b = arr[2].get<float>();
        a = arr.size() >= 4 ? arr[3].get<float>() : 1.0f;
    };

    for (const auto& j : root.value("layouts", json::array())) {
        PadLayout L;
        L.id = j.value("id", "");
        if (j.contains("canvas")) {
            L.W      = jf(j["canvas"], "W",      L.W);
            L.FrontH = jf(j["canvas"], "FrontH", L.FrontH);
            L.TopH   = jf(j["canvas"], "TopH",   L.TopH);
        }

        for (const auto& cj : j.value("components", json::array())) {
            if (!cj.contains("type")) continue;  // skip comment-only entries

            PadComponent c;
            c.id      = cj.value("id",      "");
            c.view    = cj.value("view",    "top");
            c.type    = cj.value("type",    "");
            c.image   = cj.value("image",   "");
            c.overlay = cj.value("overlay", "");
            c.cx      = jf(cj, "cx", 0.0f);
            c.cy      = jf(cj, "cy", 0.0f);
            c.w       = jf(cj, "w",  0.0f);
            c.h       = jf(cj, "h",  0.0f);
            c.size      = jf(cj, "size",       0.0f);
            c.maxOffset = jf(cj, "max_offset", 0.0f);
            c.threshold = jf(cj, "threshold",  0.05f);

            if (cj.contains("overlay_scale")) {
                const auto& os = cj["overlay_scale"];
                if (os.is_array() && os.size() >= 2) {
                    c.overlayScaleX = os[0].get<float>();
                    c.overlayScaleY = os[1].get<float>();
                } else if (os.is_number()) {
                    c.overlayScaleX = c.overlayScaleY = os.get<float>();
                }
            }

            c.state      = cj.value("state",        "");
            c.stateX     = cj.value("state_x",      "");
            c.stateY     = cj.value("state_y",      "");
            c.stateClick = cj.value("state_click",  "");
            c.stateUp    = cj.value("state_up",     "");
            c.stateDown  = cj.value("state_down",   "");
            c.stateLeft  = cj.value("state_left",   "");
            c.stateRight = cj.value("state_right",  "");
            c.imageUp    = cj.value("image_up",     "");
            c.imageDown  = cj.value("image_down",   "");
            c.imageLeft  = cj.value("image_left",   "");
            c.imageRight = cj.value("image_right",  "");

            parseColor4(cj, "color",                c.colorR,         c.colorG,         c.colorB,         c.colorA);
            parseColor4(cj, "active_color",         c.activeColorR,   c.activeColorG,   c.activeColorB,   c.activeColorA);
            parseColor4(cj, "overlay_color",        c.ovColorR,       c.ovColorG,       c.ovColorB,       c.ovColorA);
            parseColor4(cj, "active_overlay_color", c.activeOvColorR, c.activeOvColorG, c.activeOvColorB, c.activeOvColorA);

            L.components.push_back(std::move(c));
        }
        result.push_back(std::move(L));
    }
    return result;
}

const PadLayout* findLayout(const std::vector<PadLayout>& layouts, const std::string& id) {
    for (const auto& L : layouts)
        if (L.id == id) return &L;
    return nullptr;
}

void savePadLayouts(const std::string& path, const std::vector<PadLayout>& layouts) {
    auto writeColor4 = [](json& obj, const char* key, float r, float g, float b, float a) {
        obj[key] = json::array({ r, g, b, a });
    };

    json root;
    json jarr = json::array();

    for (const auto& L : layouts) {
        json jl;
        jl["id"]     = L.id;
        jl["canvas"] = json{ {"W", L.W}, {"FrontH", L.FrontH}, {"TopH", L.TopH} };

        json jcomps = json::array();
        for (const auto& c : L.components) {
            json jc;
            if (!c.id.empty())  jc["id"]   = c.id;
            jc["view"]  = c.view;
            jc["type"]  = c.type;

            if (!c.image.empty())    jc["image"]   = c.image;
            if (!c.overlay.empty())  jc["overlay"]  = c.overlay;

            jc["cx"] = c.cx;
            jc["cy"] = c.cy;

            if (c.w         != 0.0f)  jc["w"]          = c.w;
            if (c.h         != 0.0f)  jc["h"]          = c.h;
            if (c.size      != 0.0f)  jc["size"]        = c.size;
            if (c.maxOffset != 0.0f)  jc["max_offset"]  = c.maxOffset;
            if (c.threshold != 0.05f) jc["threshold"]   = c.threshold;

            if (c.overlayScaleX != 1.0f || c.overlayScaleY != 1.0f) {
                if (c.overlayScaleX == c.overlayScaleY)
                    jc["overlay_scale"] = c.overlayScaleX;
                else
                    jc["overlay_scale"] = json::array({ c.overlayScaleX, c.overlayScaleY });
            }

            if (!c.state.empty())      jc["state"]       = c.state;
            if (!c.stateX.empty())     jc["state_x"]     = c.stateX;
            if (!c.stateY.empty())     jc["state_y"]     = c.stateY;
            if (!c.stateClick.empty()) jc["state_click"]  = c.stateClick;
            if (!c.stateUp.empty())    jc["state_up"]    = c.stateUp;
            if (!c.stateDown.empty())  jc["state_down"]  = c.stateDown;
            if (!c.stateLeft.empty())  jc["state_left"]  = c.stateLeft;
            if (!c.stateRight.empty()) jc["state_right"] = c.stateRight;

            if (!c.imageUp.empty())    jc["image_up"]    = c.imageUp;
            if (!c.imageDown.empty())  jc["image_down"]  = c.imageDown;
            if (!c.imageLeft.empty())  jc["image_left"]  = c.imageLeft;
            if (!c.imageRight.empty()) jc["image_right"] = c.imageRight;

            writeColor4(jc, "color", c.colorR, c.colorG, c.colorB, c.colorA);
            if (c.type == "button" || c.type == "stick" || c.type == "dpad") {
                writeColor4(jc, "active_color",
                            c.activeColorR, c.activeColorG, c.activeColorB, c.activeColorA);
                if (c.type == "button") {
                    writeColor4(jc, "overlay_color",
                                c.ovColorR, c.ovColorG, c.ovColorB, c.ovColorA);
                    writeColor4(jc, "active_overlay_color",
                                c.activeOvColorR, c.activeOvColorG, c.activeOvColorB, c.activeOvColorA);
                }
            }

            jcomps.push_back(std::move(jc));
        }
        jl["components"] = std::move(jcomps);
        jarr.push_back(std::move(jl));
    }

    root["layouts"] = std::move(jarr);

    std::ofstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot write pad_layouts: " + path);
    f << root.dump(4) << '\n';
}
