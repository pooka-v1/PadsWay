#include "ConfigLoader.h"
#include "./nlohmann/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

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

        for (const auto& [key, val] : c.at("buttons").items()) {
            int          bit = std::stoi(key);
            ButtonAction action;
            if (val.is_string()) {
                action.type = ButtonActionType::VirtualButton;
                action.name = val.get<std::string>();
            } else {
                std::string type = val.at("type").get<std::string>();
                if (type == "bot") {
                    action.type = ButtonActionType::Bot;
                    action.name = val.at("name").get<std::string>();
                } else if (type == "macro") {
                    action.type      = ButtonActionType::Macro;
                    action.name      = val.at("name").get<std::string>();
                    if (val.contains("execution"))
                        action.execution = val["execution"].get<std::string>();
                } else if (type == "trigger") {
                    action.type   = ButtonActionType::Trigger;
                    action.target = val.at("target").get<std::string>();
                    if (val.contains("axis")) action.axis = val["axis"].get<std::string>();
                }
            }
            cfg.buttons[bit] = action;
        }

        for (const auto& [source, axisJson] : c.at("axes").items()) {
            AxisMapping m;
            m.target = axisJson.at("target").get<std::string>();
            m.invert = axisJson.value("invert", false);
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
