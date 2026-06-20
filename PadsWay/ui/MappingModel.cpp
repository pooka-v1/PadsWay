#include "MappingModel.h"
#include "../nlohmann/json.hpp"
using json = nlohmann::json;

#include <algorithm>
#include <fstream>
#include <cstdio>
#include <windows.h>

// ---------------------------------------------------------------------------
// JSON serializers shared by save() (controllers.json) and saveProfile().
// saveProfile() builds each section from the model edits AND from the base
// config with the same builders, so equality means "same as base" → omit.
// ---------------------------------------------------------------------------

static json actionToJson(const ButtonAction& act) {
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
        if (!act.execution.empty()) j["execution"] = act.execution;
    } else if (act.type == ButtonActionType::Bot) {
        j["type"] = "bot";
        j["name"] = act.name;
    } else if (act.type == ButtonActionType::Trigger) {
        j["type"]   = "trigger";
        j["target"] = act.target;
    }
    return j;
}

static json halfAxisToJson(const HalfAxisAction& ha) {
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
    case HalfAxisActionType::Bot:
        j["type"] = "bot";
        j["name"] = ha.target;
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
            if (r.hasAction)
                rj["action"] = actionToJson(r.action);
            arr.push_back(rj);
        }
        j["ranges"] = arr;
        break;
    }
    }
    return j;
}

static json axisActionsToJson(const std::unordered_map<std::string, HalfAxisAction>& m) {
    json j = json::object();
    for (const auto& [key, ha] : m)
        j[key] = halfAxisToJson(ha);
    return j;
}

// Trigger side from ranges: a single range collapses to its plain action,
// multiple ranges serialize as { "ranges": [...] }. Returns null when empty.
// Works on both RangeEdit and TriggerRange (same field shape).
template <typename R>
static json trigSideFromRanges(const std::vector<R>& ranges) {
    json result;  // default: null
    if (ranges.empty()) return result;
    if (ranges.size() == 1) {
        if (ranges[0].hasAction)
            result = actionToJson(ranges[0].action);
        return result;
    }
    json side = json::object();
    json arr  = json::array();
    for (const auto& re : ranges) {
        json r;
        r["from"] = re.from;
        r["to"]   = re.to;
        if (re.hasAction)
            r["action"] = actionToJson(re.action);
        arr.push_back(r);
    }
    side["ranges"] = arr;
    return side;
}

static json trigSideJsonFromEdits(
        const std::unordered_map<std::string, ButtonAction>& trigActionEdits,
        const std::string& key, const std::vector<RangeEdit>& ranges) {
    auto it = trigActionEdits.find(key);
    if (it != trigActionEdits.end())
        return actionToJson(it->second);
    return trigSideFromRanges(ranges);
}

// Same serialization built from a parsed config — mirrors what the model
// would hold right after reloadFromConfig() on that config.
static json trigSideJsonFromConfig(const ControllerConfig& cfg, const char* src) {
    for (const auto& [slot, srcs] : cfg.stickSlots)
        if (std::find(srcs.begin(), srcs.end(), src) != srcs.end()) {
            ButtonAction a;
            a.type = ButtonActionType::VirtualButton;
            a.name = slot;
            return actionToJson(a);
        }
    const bool isL = (std::string(src) == "l2");
    if (isL ? cfg.triggerLHasAction : cfg.triggerRHasAction)
        return actionToJson(isL ? cfg.triggerLAction : cfg.triggerRAction);
    return trigSideFromRanges(isL ? cfg.triggerLRanges : cfg.triggerRRanges);
}

static json dpadJsonFromEdits(
        const std::unordered_map<std::string, ButtonAction>& actionEdits,
        const std::unordered_map<std::string, std::string>&  buttonEdits) {
    json j = json::object();
    for (const char* dir : { "up", "down", "left", "right" }) {
        std::string key = std::string("dpad_") + dir;
        auto ait = actionEdits.find(key);
        if (ait != actionEdits.end()) {
            json actJson = actionToJson(ait->second);
            actJson["physical"] = key;
            j[dir] = std::move(actJson);
        } else {
            auto bit = buttonEdits.find(key);
            // Identity remap (dpad_up → dpad_up) is the same as no remap — skip it.
            if (bit != buttonEdits.end() && !bit->second.empty() && bit->second != key)
                j[dir] = bit->second;
        }
    }
    return j;
}

static json dpadJsonFromConfig(const ControllerConfig& cfg) {
    json j = json::object();
    for (const char* dir : { "up", "down", "left", "right" }) {
        std::string key = std::string("dpad_") + dir;
        auto ait = cfg.dpadActions.find(dir);
        if (ait != cfg.dpadActions.end()) {
            json actJson = actionToJson(ait->second);
            actJson["physical"] = key;
            j[dir] = std::move(actJson);
            continue;
        }
        auto rit = cfg.dpadRemap.find(dir);
        if (rit != cfg.dpadRemap.end()) {
            if (rit->second != key)   // skip identity remaps
                j[dir] = rit->second;
            continue;
        }
        for (const auto& [slot, srcs] : cfg.stickSlots)
            if (std::find(srcs.begin(), srcs.end(), key) != srcs.end()) {
                j[dir] = slot;
                break;
            }
    }
    return j;
}

// Whole-axis mapping as stored in a profile's "axes" section, keyed by stickId.
static json axisMappingToJson(const AxisMapping& m, const std::string& sid) {
    json j = json::object();
    j["target"]    = m.target;
    j["stick_id"]  = sid;
    j["invert"]    = m.invert;
    j["speed"]     = m.speed;
    j["threshold"] = m.threshold;
    if (!m.btnNeg.empty()) j["btn_neg"] = m.btnNeg;
    if (!m.btnPos.empty()) j["btn_pos"] = m.btnPos;
    return j;
}

// ---------------------------------------------------------------------------
void MappingModel::clear() {
    buttonEdits.clear();
    actionEdits.clear();
    axisEdits.clear();
    axisActionEdits.clear();
    trigActionEdits.clear();
    trigLRangeEdits.clear();
    trigRRangeEdits.clear();
    stickSlotEdits.clear();
    contextBotsEdits.clear();
}

// ---------------------------------------------------------------------------
void MappingModel::reloadFromConfig(const ControllerConfig& cfg) {
    clear();

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
            actionEdits[action.physical] = action;
            break;
        default: break;
        }
    }

    for (const auto& [dir, vShort] : cfg.dpadRemap)
        buttonEdits["dpad_" + dir] = vShort;
    for (const auto& [dir, action] : cfg.dpadActions)
        actionEdits["dpad_" + dir] = action;
    for (const auto& [slotDir, srcs] : cfg.stickSlots)
        for (const auto& src : srcs)
            if (src.rfind("dpad_", 0) == 0)
                buttonEdits[src] = slotDir;

    if (cfg.triggerLHasAction) trigActionEdits["l2"] = cfg.triggerLAction;
    if (cfg.triggerRHasAction) trigActionEdits["r2"] = cfg.triggerRAction;
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
}

// ---------------------------------------------------------------------------
void MappingModel::reload(const std::vector<ControllerConfig>& configs) {
    for (const auto& cfg : configs) {
        if (cfg.vid != vid || cfg.pid != pid) continue;
        reloadFromConfig(cfg);
        break;
    }
}

// ---------------------------------------------------------------------------
void MappingModel::loadProfile(const ControllerConfig& base, const GameProfile& profile) {
    vid = base.vid;
    pid = base.pid;
    reloadFromConfig(applyProfile(base, profile));
    contextBotsEdits = profile.context_bots;
}

// ---------------------------------------------------------------------------
void MappingModel::saveProfile(const std::string& path, const std::string& profileName,
                               const ControllerConfig& base) {
    // Build physShort -> base action + physShort -> virtual output name.
    std::unordered_map<std::string, const ButtonAction*> baseByPhys;
    std::unordered_map<std::string, std::string>         physToVirtual;
    for (const auto& [bit, action] : base.buttons) {
        if (action.physical.empty()) continue;
        baseByPhys[action.physical] = &action;
        if (action.type == ButtonActionType::VirtualButton && !action.name.empty())
            physToVirtual[action.physical] = action.name;
    }

    json buttonsJson = json::object();

    // actionEdits: Macro/KB/Mouse/Trigger — key = virtual output name, or physShort for
    // extra buttons without a virtual Xbox equivalent (lp, rp, l4, r4).
    // applyProfile reads both via dual lookup, so physShort is a valid key.
    for (const auto& [physShort, act] : actionEdits) {
        if (physShort.rfind("dpad_", 0) == 0) continue;
        auto vit = physToVirtual.find(physShort);
        const std::string& vName = (vit != physToVirtual.end()) ? vit->second : physShort;

        json j = actionToJson(act);

        auto bit = baseByPhys.find(physShort);
        if (bit != baseByPhys.end() && actionToJson(*bit->second) == j)
            continue;  // same as base

        if (!j.empty()) buttonsJson[vName] = j;
    }

    // buttonEdits: VirtualButton remaps — key = virtual output name, or physShort fallback.
    for (const auto& [physShort, virtShort] : buttonEdits) {
        if (physShort.rfind("dpad_", 0) == 0) continue;
        auto vit = physToVirtual.find(physShort);
        const std::string& vName = (vit != physToVirtual.end()) ? vit->second : physShort;

        auto bit = baseByPhys.find(physShort);
        if (bit != baseByPhys.end() &&
            bit->second->type == ButtonActionType::VirtualButton &&
            bit->second->name == virtShort)
            continue;  // same as base

        buttonsJson[vName] = json{{"virtual", virtShort}};
    }

    json root;
    {
        std::ifstream f(path);
        if (f.is_open()) root = json::parse(f);
    }
    root["profile_name"] = profileName;
    if (buttonsJson.empty()) root.erase("buttons");
    else                     root["buttons"] = buttonsJson;
    if (contextBotsEdits.empty()) root.erase("context_bots");
    else                          root["context_bots"] = contextBotsEdits;

    // --- dpad_remap — whole-section diff against base ---
    {
        json modelDpad = dpadJsonFromEdits(actionEdits, buttonEdits);
        json baseDpad  = dpadJsonFromConfig(base);
        if (modelDpad == baseDpad) root.erase("dpad_remap");
        else                       root["dpad_remap"] = std::move(modelDpad);
    }

    // --- trigger_actions — per-side diff; a null side resets that trigger ---
    {
        json taJson = json::object();
        json lModel = trigSideJsonFromEdits(trigActionEdits, "l2", trigLRangeEdits);
        json rModel = trigSideJsonFromEdits(trigActionEdits, "r2", trigRRangeEdits);
        if (lModel != trigSideJsonFromConfig(base, "l2")) taJson["l2"] = std::move(lModel);
        if (rModel != trigSideJsonFromConfig(base, "r2")) taJson["r2"] = std::move(rModel);
        if (taJson.empty()) root.erase("trigger_actions");
        else                root["trigger_actions"] = std::move(taJson);
    }

    // --- axis_actions — whole-section diff against base ---
    {
        json modelAA = axisActionsToJson(axisActionEdits);
        json baseAA  = axisActionsToJson(base.axis_actions);
        if (modelAA == baseAA) root.erase("axis_actions");
        else                   root["axis_actions"] = std::move(modelAA);
    }

    // --- axes (whole-axis remap) — per-key diff against base, keyed by stickId ---
    if (!axisEdits.empty()) {
        std::unordered_map<std::string, const AxisMapping*> baseByStick;
        for (const auto& [src, m] : base.axes) {
            std::string sid = m.stickId.empty() ? m.target : m.stickId;
            if (!sid.empty()) baseByStick[sid] = &m;
        }
        json axesJson = (root.contains("axes") && root["axes"].is_object())
                            ? root["axes"] : json::object();
        for (const auto& [sid, em] : axisEdits) {
            json mj = axisMappingToJson(em, sid);
            auto bit = baseByStick.find(sid);
            if (bit != baseByStick.end() && axisMappingToJson(*bit->second, sid) == mj)
                axesJson.erase(sid);
            else
                axesJson[sid] = std::move(mj);
        }
        if (axesJson.empty()) root.erase("axes");
        else                  root["axes"] = std::move(axesJson);
    }

    // Remove legacy overrides array if present.
    root.erase("overrides");

    std::string dumped = root.dump(2);
    json::parse(dumped);
    std::string tmpPath = path + ".tmp";
    {
        std::ofstream tmp(tmpPath);
        if (!tmp.is_open()) return;
        tmp << dumped;
    }
    MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
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

            auto actionEditIt = actionEdits.find(physShort);
            if (actionEditIt != actionEdits.end()) {
                const ButtonAction& act = actionEditIt->second;
                newBtn.erase("virtual");
                newBtn.erase("execution");
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
                    if (!act.execution.empty()) newBtn["execution"] = act.execution;
                    newBtn.erase("keys"); newBtn.erase("button");
                } else if (act.type == ButtonActionType::Bot) {
                    newBtn["type"] = "bot";
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
                    newBtn.erase("execution");
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
            json dpadRemapJson = dpadJsonFromEdits(actionEdits, buttonEdits);
            if (dpadRemapJson.empty())
                ctrl.erase("dpad_remap");
            else
                ctrl["dpad_remap"] = std::move(dpadRemapJson);
        }

        // --- Trigger actions ---
        {
            json taJson = json::object();
            json lSide  = trigSideJsonFromEdits(trigActionEdits, "l2", trigLRangeEdits);
            json rSide  = trigSideJsonFromEdits(trigActionEdits, "r2", trigRRangeEdits);
            if (!lSide.is_null()) taJson["l2"] = lSide;
            if (!rSide.is_null()) taJson["r2"] = rSide;

            if (taJson.empty())
                ctrl.erase("trigger_actions");
            else
                ctrl["trigger_actions"] = taJson;
        }

        // --- Axis remapping ---
        if (!axisEdits.empty() && ctrl.contains("axes")) {
            for (auto& [source, axisJson] : ctrl["axes"].items()) {
                std::string sid = axisJson.value("stick_id", std::string{});
                if (sid.empty()) {
                    std::string t = axisJson.value("target", std::string{});
                    if (t == "left_x"  || t == "left_y"  ||
                        t == "right_x" || t == "right_y") sid = t;
                }
                auto eit = axisEdits.find(sid);
                if (eit == axisEdits.end()) continue;
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
            if (!axisActionEdits.empty())
                ctrl["axis_actions"] = axisActionsToJson(axisActionEdits);
            else
                ctrl.erase("axis_actions");
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
