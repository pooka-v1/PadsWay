#include "Strings.h"
#include "../nlohmann/json.hpp"
#include "../Log.h"
#include <fstream>
#include <unordered_map>

using json = nlohmann::json;

static std::unordered_map<std::string, std::string> s_map;

// Recursively flattens a nested JSON object into dot-separated keys.
// { "tab": { "pads": "Mandos" } } -> { "tab.pads": "Mandos" }
static void flatten(const json& node, const std::string& prefix) {
    for (const auto& [k, v] : node.items()) {
        std::string key = prefix.empty() ? k : prefix + "." + k;
        if (v.is_object())
            flatten(v, key);
        else if (v.is_string())
            s_map[key] = v.get<std::string>();
    }
}

void Strings::load(const std::string& locale) {
    s_map.clear();
    std::string path = "data/strings/strings_" + locale + ".json";
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::debug("Strings: file not found: {}", path);
        return;
    }
    try {
        json root = json::parse(f);
        flatten(root, "");
        spdlog::debug("Strings: loaded {} keys from {}", s_map.size(), path);
    } catch (const std::exception& e) {
        spdlog::error("Strings: parse error in {}: {}", path, e.what());
    }
}

const char* tr(const std::string& key) {
    auto it = s_map.find(key);
    if (it == s_map.end()) return key.c_str();
    return it->second.c_str();
}
