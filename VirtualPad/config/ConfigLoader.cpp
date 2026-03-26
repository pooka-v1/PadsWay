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
        cfg.vid         = static_cast<uint16_t>(std::stoul(c.at("vid").get<std::string>(), nullptr, 16));
        cfg.pid         = static_cast<uint16_t>(std::stoul(c.at("pid").get<std::string>(), nullptr, 16));
        cfg.source_name = c.at("source_name").get<std::string>();
        cfg.mode        = c.at("mode").get<std::string>();
        cfg.dpad        = c.value("dpad", "");
        cfg.buttons     = parseButtonsJson(c.at("buttons"));

        for (const auto& [source, axisJson] : c.at("axes").items()) {
            AxisMapping m;
            m.target = axisJson.at("target").get<std::string>();
            m.invert = axisJson.value("invert", false);
            m.speed  = axisJson.value("speed",  15.0f);
            cfg.axes[source] = m;
        }

        result.push_back(std::move(cfg));
    }

    return result;
}

const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid) {
    for (const auto& c : configs)
        if (c.vid == vid && c.pid == pid)
            return &c;
    return nullptr;
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
    }
    return result;
}
