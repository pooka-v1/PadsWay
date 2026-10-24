#include "ControllerModes.h"
#include "ConfigLoader.h"
#include "../nlohmann/json.hpp"
#include <cctype>
#include <fstream>

using json = nlohmann::json;

bool isKnownControllerMode(const std::string& mode) {
    return mode == "d" || mode == "x" || mode == "s";
}

namespace {

void warn(std::vector<std::string>* warnings, const std::string& message) {
    if (warnings) warnings->push_back(message);
}

// "2DC8" -> 0x2DC8. False on anything that isn't 1-4 hex digits (stoul alone would accept "12zz").
bool parseHexId(const json& value, uint16_t& out) {
    if (!value.is_string()) return false;
    const std::string text = value.get<std::string>();
    if (text.empty() || text.size() > 4) return false;
    for (char c : text)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    out = static_cast<uint16_t>(std::stoul(text, nullptr, 16));
    return true;
}

// Layer 3: a whole vendor that only makes pads of one family. Only one direction holds: X-mode
// does not imply 045E (8BitDo and Logitech keep their own VID) — those go in known_controllers.json.
std::string modeByVendor(uint16_t vid) {
    switch (vid) {
        case 0x045E: return "x";   // Microsoft — Xbox pads and every pad imitating one
        case 0x054C: return "d";   // Sony — DS4/DualSense and clones in PS mode, plain HID
        case 0x057E: return "s";   // Nintendo — Switch pads and clones in S-mode
        default:     return {};
    }
}

} // namespace

std::vector<KnownControllerId> loadKnownControllers(const std::string& path,
                                                    std::vector<std::string>* warnings) {
    std::ifstream f(path);
    if (!f.is_open()) {
        warn(warnings, "known controllers file not found: " + path);
        return {};
    }
    // No exceptions: a malformed file must not stop the app, it only feeds a proposal.
    const json root = json::parse(f, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("controllers") ||
        !root["controllers"].is_array()) {
        warn(warnings, "known controllers file ignored, not a valid list: " + path);
        return {};
    }

    std::vector<KnownControllerId> result;
    int row = 0;
    for (const auto& entry : root["controllers"]) {
        ++row;
        KnownControllerId id;
        const bool valid = entry.is_object() &&
                           entry.contains("vid") && parseHexId(entry["vid"], id.vid) &&
                           entry.contains("pid") && parseHexId(entry["pid"], id.pid) &&
                           entry.contains("mode") && entry["mode"].is_string() &&
                           isKnownControllerMode(entry["mode"].get<std::string>());
        if (!valid) {
            warn(warnings, "known controllers row " + std::to_string(row) + " skipped: needs hex vid/pid "
                           "and a mode of d/x/s");
            continue;
        }
        id.mode = entry["mode"].get<std::string>();
        if (entry.contains("name") && entry["name"].is_string())
            id.name = entry["name"].get<std::string>();
        result.push_back(std::move(id));
    }
    return result;
}

std::string deduceMode(uint16_t vid, uint16_t pid, const std::string& productName,
                       const std::vector<KnownControllerId>& known,
                       const std::vector<ControllerConfig>& userConfigs) {
    for (const auto& id : known)
        if (id.vid == vid && id.pid == pid) return id.mode;

    if (const ControllerConfig* cfg = findConfig(userConfigs, vid, pid, productName);
        cfg && !cfg->mode.empty())
        return cfg->mode;

    return modeByVendor(vid);
}
