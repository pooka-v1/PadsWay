#include "MappingModel.h"
#include "../nlohmann/json.hpp"
using json = nlohmann::json;

#include <fstream>
#include <cstdio>
#include <windows.h>

// ---------------------------------------------------------------------------
void MappingModel::clear() {
    buttonEdits.clear();
    h5ActionEdits.clear();
    h6AxisEdits.clear();
    axisActionEdits.clear();
    trigActionEdits.clear();
    trigLRangeEdits.clear();
    trigRRangeEdits.clear();
    stickSlotEdits.clear();
}

// ---------------------------------------------------------------------------
void MappingModel::reload(const std::vector<ControllerConfig>& configs) {
    clear();

    for (const auto& cfg : configs) {
        if (cfg.vid != vid || cfg.pid != pid) continue;

        for (const auto& [idx, action] : cfg.buttons) {
            if (action.physical.empty()) continue;
            switch (action.type) {
            case ButtonActionType::VirtualButton:
                if (!action.name.empty() && action.physical != action.name)
                    buttonEdits[action.physical] = action.name;
                break;
            case ButtonActionType::Keyboard:
            case ButtonActionType::MouseClick:
            case ButtonActionType::Macro:
            case ButtonActionType::Trigger:
                h5ActionEdits[action.physical] = action;
                break;
            default: break;
            }
        }

        for (const auto& [dir, vShort] : cfg.dpadRemap)
            buttonEdits["dpad_" + dir] = vShort;
        for (const auto& [dir, action] : cfg.dpadActions)
            h5ActionEdits["dpad_" + dir] = action;
        // Dpad sources assigned to stick slots are in stickSlots (not dpadRemap).
        for (const auto& [slotDir, srcs] : cfg.stickSlots)
            for (const auto& src : srcs)
                if (src.rfind("dpad_", 0) == 0)
                    buttonEdits[src] = slotDir;

        if (cfg.triggerLHasAction) trigActionEdits["l2"] = cfg.triggerLAction;
        if (cfg.triggerRHasAction) trigActionEdits["r2"] = cfg.triggerRAction;
        // Trigger sources assigned to stick slots are in stickSlots (not triggerL/RAction).
        for (const auto& [slotDir, srcs] : cfg.stickSlots)
            for (const auto& src : srcs)
                if (src == "l2" || src == "r2") {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = src; act.name = slotDir;
                    trigActionEdits[src] = act;
                }

        auto loadRanges = [](const std::vector<TriggerRange>& src,
                              std::vector<RangeEdit>& dst) {
            dst.clear();
            for (const auto& r : src) {
                RangeEdit re;
                re.from      = r.from;
                re.to        = r.to;
                re.action    = r.action;
                re.hasAction = r.hasAction;
                dst.push_back(re);
            }
        };
        loadRanges(cfg.triggerLRanges, trigLRangeEdits);
        loadRanges(cfg.triggerRRanges, trigRRangeEdits);

        for (const auto& [key, action] : cfg.axis_actions)
            axisActionEdits[key] = action;

        // stickSlotEdits reserved for future inverse case (analog stick → virtual component).
        // Button-sourced slot assignments are loaded via buttonEdits (VirtualButton case above).
        break;
    }
}

// ---------------------------------------------------------------------------
void MappingModel::save(const std::string& path) {
    json root;
    {
        std::ifstream f(path);
        if (f.is_open()) root = json::parse(f);
    }
    if (!root.contains("controllers") || !root["controllers"].is_array()) return;

    char vidStr[8], pidStr[8];
    snprintf(vidStr, sizeof(vidStr), "%04X", vid);
    snprintf(pidStr, sizeof(pidStr), "%04X", pid);

    for (auto& ctrl : root["controllers"]) {
        if (ctrl.value("vid", "") != std::string(vidStr) ||
            ctrl.value("pid", "") != std::string(pidStr)) continue;
        if (!ctrl.contains("buttons")) continue;

        // --- Buttons ---
        std::vector<std::pair<std::string, json>> changes;
        for (auto& [key, btn] : ctrl["buttons"].items()) {
            std::string physShort;
            if (btn.is_string())
                physShort = btn.get<std::string>();
            else if (btn.is_object() && btn.contains("physical"))
                physShort = btn["physical"].get<std::string>();
            else continue;

            json newBtn = btn.is_object() ? btn : json::object();
            if (!btn.is_object()) newBtn["physical"] = physShort;
            bool changed = false;

            auto h5it = h5ActionEdits.find(physShort);
            if (h5it != h5ActionEdits.end()) {
                const ButtonAction& act = h5it->second;
                newBtn.erase("virtual");
                if (act.type == ButtonActionType::Keyboard) {
                    newBtn["type"] = "keyboard";
                    newBtn.erase("name");
                    json keysArr = json::array();
                    for (const auto& k : act.keys) keysArr.push_back(k);
                    newBtn["keys"] = keysArr;
                } else if (act.type == ButtonActionType::MouseClick) {
                    newBtn["type"]   = "mouse_click";
                    newBtn["button"] = act.mouseButton;
                    newBtn.erase("name"); newBtn.erase("keys");
                } else if (act.type == ButtonActionType::Macro) {
                    newBtn["type"] = "macro";
                    newBtn["name"] = act.name;
                    newBtn.erase("keys"); newBtn.erase("button");
                } else if (act.type == ButtonActionType::Trigger) {
                    newBtn["type"]   = "trigger";
                    newBtn["target"] = act.target;
                    newBtn.erase("virtual"); newBtn.erase("name");
                    newBtn.erase("keys");    newBtn.erase("button");
                }
                changed = true;
            } else {
                auto it = buttonEdits.find(physShort);
                if (it != buttonEdits.end()) {
                    newBtn.erase("type"); newBtn.erase("target");
                    newBtn.erase("keys"); newBtn.erase("button"); newBtn.erase("name");
                    if (it->second.empty())
                        newBtn.erase("virtual");
                    else
                        newBtn["virtual"] = it->second;
                    changed = true;
                }
            }
            if (changed) changes.push_back({ key, std::move(newBtn) });
        }
        for (auto& [key, val] : changes)
            ctrl["buttons"][key] = val;

        // --- Dpad remap ---
        {
            json dpadRemapJson = json::object();
            for (const char* dir : {"up", "down", "left", "right"}) {
                std::string key = std::string("dpad_") + dir;
                auto h5it = h5ActionEdits.find(key);
                if (h5it != h5ActionEdits.end()) {
                    const ButtonAction& act = h5it->second;
                    json actJson = json::object();
                    actJson["physical"] = key;
                    if (act.type == ButtonActionType::Keyboard) {
                        actJson["type"] = "keyboard";
                        json keysArr = json::array();
                        for (const auto& k : act.keys) keysArr.push_back(k);
                        actJson["keys"] = keysArr;
                    } else if (act.type == ButtonActionType::MouseClick) {
                        actJson["type"]   = "mouse_click";
                        actJson["button"] = act.mouseButton;
                    } else if (act.type == ButtonActionType::Macro) {
                        actJson["type"] = "macro";
                        actJson["name"] = act.name;
                    } else if (act.type == ButtonActionType::Trigger) {
                        actJson["type"]   = "trigger";
                        actJson["target"] = act.target;
                    }
                    dpadRemapJson[dir] = std::move(actJson);
                } else {
                    auto it = buttonEdits.find(key);
                    if (it != buttonEdits.end() && !it->second.empty())
                        dpadRemapJson[dir] = it->second;
                }
            }
            if (dpadRemapJson.empty())
                ctrl.erase("dpad_remap");
            else
                ctrl["dpad_remap"] = std::move(dpadRemapJson);
        }

        // --- Trigger actions (H7) ---
        {
            auto actToJson = [](const ButtonAction& act) {
                json j = json::object();
                if (act.type == ButtonActionType::TriggerPassthrough) {
                    j["type"]   = "trigger_passthrough";
                    j["target"] = act.target;
                } else if (act.type == ButtonActionType::VirtualButton) {
                    j["virtual"] = act.name;
                } else if (act.type == ButtonActionType::Keyboard) {
                    j["type"] = "keyboard";
                    json arr = json::array();
                    for (const auto& k : act.keys) arr.push_back(k);
                    j["keys"] = arr;
                } else if (act.type == ButtonActionType::MouseClick) {
                    j["type"]   = "mouse_click";
                    j["button"] = act.mouseButton;
                } else if (act.type == ButtonActionType::Macro) {
                    j["type"] = "macro";
                    j["name"] = act.name;
                } else if (act.type == ButtonActionType::Trigger) {
                    j["type"]   = "trigger";
                    j["target"] = act.target;
                }
                return j;
            };

            auto buildTrigSideJson = [&](const std::string& key,
                                          const std::vector<RangeEdit>& ranges) {
                json result;  // default: null
                auto it = trigActionEdits.find(key);
                if (it != trigActionEdits.end()) {
                    result = actToJson(it->second);
                } else if (!ranges.empty()) {
                    if (ranges.size() == 1) {
                        if (ranges[0].hasAction)
                            result = actToJson(ranges[0].action);
                    } else {
                        json side = json::object();
                        json arr  = json::array();
                        for (const auto& re : ranges) {
                            json r;
                            r["from"] = re.from;
                            r["to"]   = re.to;
                            if (re.hasAction)
                                r["action"] = actToJson(re.action);
                            arr.push_back(r);
                        }
                        side["ranges"] = arr;
                        result = side;
                    }
                }
                return result;
            };

            json taJson = json::object();
            json lSide  = buildTrigSideJson("l2", trigLRangeEdits);
            json rSide  = buildTrigSideJson("r2", trigRRangeEdits);
            if (!lSide.is_null()) taJson["l2"] = lSide;
            if (!rSide.is_null()) taJson["r2"] = rSide;

            if (taJson.empty())
                ctrl.erase("trigger_actions");
            else
                ctrl["trigger_actions"] = taJson;
        }

        // --- Axis remapping (H6) ---
        if (!h6AxisEdits.empty() && ctrl.contains("axes")) {
            for (auto& [source, axisJson] : ctrl["axes"].items()) {
                std::string sid = axisJson.value("stick_id", std::string{});
                if (sid.empty()) {
                    std::string t = axisJson.value("target", std::string{});
                    if (t == "left_x"  || t == "left_y"  ||
                        t == "right_x" || t == "right_y") sid = t;
                }
                auto eit = h6AxisEdits.find(sid);
                if (eit == h6AxisEdits.end()) continue;
                const AxisMapping& em = eit->second;
                axisJson["target"]   = em.target;
                axisJson["stick_id"] = em.stickId;
                if (em.target == "btn_dir") {
                    if (!em.btnNeg.empty()) axisJson["btn_neg"] = em.btnNeg;
                    else                    axisJson.erase("btn_neg");
                    if (!em.btnPos.empty()) axisJson["btn_pos"] = em.btnPos;
                    else                    axisJson.erase("btn_pos");
                } else {
                    axisJson.erase("btn_neg");
                    axisJson.erase("btn_pos");
                }
            }
        }

        // stick_slots section reserved for future inverse case (analog stick → virtual component).
        // Button-sourced slot assignments are saved as "virtual": "right_x_neg" in button entries.
        ctrl.erase("stick_slots");

        // --- axis_actions (H6 T4) ---
        {
            auto halfAxisToJson = [](const HalfAxisAction& ha) -> json {
                json j;
                switch (ha.type) {
                case HalfAxisActionType::VirtualButton:
                case HalfAxisActionType::Trigger:
                case HalfAxisActionType::StickSlot:
                    j["virtual"] = ha.target;
                    break;
                case HalfAxisActionType::Dpad:
                    j["virtual"] = "dpad_" + ha.target;
                    break;
                case HalfAxisActionType::Keyboard: {
                    j["type"] = "keyboard";
                    json arr = json::array();
                    for (const auto& k : ha.keys) arr.push_back(k);
                    j["keys"] = arr;
                    break;
                }
                case HalfAxisActionType::Macro:
                    j["type"] = "macro";
                    j["name"] = ha.target;
                    if (!ha.execution.empty()) j["execution"] = ha.execution;
                    break;
                case HalfAxisActionType::MouseClick:
                    j["type"]   = "mouse_click";
                    j["button"] = ha.mouseButton;
                    break;
                case HalfAxisActionType::MouseMove:
                    j["target"] = ha.target;
                    j["speed"]  = ha.speed;
                    break;
                case HalfAxisActionType::Analog:
                    j["type"]    = "analog";
                    j["target"]  = ha.target;
                    j["out_dir"] = ha.outDir;
                    j["scale"]   = ha.scale;
                    break;
                case HalfAxisActionType::Ranges: {
                    json arr = json::array();
                    for (const auto& r : ha.ranges) {
                        json rj;
                        rj["from"] = r.from;
                        rj["to"]   = r.to;
                        if (r.hasAction) {
                            json actj;
                            const ButtonAction& act = r.action;
                            if (act.type == ButtonActionType::VirtualButton)
                                actj["virtual"] = act.name;
                            else if (act.type == ButtonActionType::Keyboard) {
                                actj["type"] = "keyboard";
                                json ka = json::array();
                                for (const auto& k : act.keys) ka.push_back(k);
                                actj["keys"] = ka;
                            } else if (act.type == ButtonActionType::MouseClick) {
                                actj["type"]   = "mouse_click";
                                actj["button"] = act.mouseButton;
                            } else if (act.type == ButtonActionType::Macro) {
                                actj["type"] = "macro";
                                actj["name"] = act.name;
                            }
                            rj["action"] = actj;
                        }
                        arr.push_back(rj);
                    }
                    j["ranges"] = arr;
                    break;
                }
                }
                return j;
            };

            if (!axisActionEdits.empty()) {
                json aaJson = json::object();
                for (const auto& [key, ha] : axisActionEdits)
                    aaJson[key] = halfAxisToJson(ha);
                ctrl["axis_actions"] = aaJson;
            } else {
                ctrl.erase("axis_actions");
            }
        }

        break;
    }

    // Validate before writing
    std::string dumped = root.dump(2);
    json::parse(dumped);  // throws if generated JSON is invalid

    // Atomic write via temp file + rename
    std::string tmpPath = path + ".tmp";
    {
        std::ofstream tmp(tmpPath);
        if (!tmp.is_open()) return;
        tmp << dumped;
    }
    MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
