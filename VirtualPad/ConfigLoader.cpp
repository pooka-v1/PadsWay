#include "ConfigLoader.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

static AxisMapping parseAxis(const json& j) {
    AxisMapping m;
    m.source = j.at("source").get<std::string>();
    m.invert = j.value("invert", false);
    return m;
}

static TriggerMapping parseTrigger(const json& j) {
    TriggerMapping m;
    m.button = 0;
    if (j.contains("axis"))   m.axis   = j["axis"].get<std::string>();
    if (j.contains("button")) m.button = j["button"].get<int>();
    return m;
}

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

        for (auto& [name, num]  : c.at("buttons").items())
            cfg.buttons[name] = num.get<int>();

        for (auto& [name, axis] : c.at("axes").items())
            cfg.axes[name] = parseAxis(axis);

        for (auto& [name, trig] : c.at("triggers").items())
            cfg.triggers[name] = parseTrigger(trig);

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
