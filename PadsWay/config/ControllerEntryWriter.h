#pragma once
#include "ConfigLoader.h"   // makeConfigId
#include "../nlohmann/json.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Puts one wizard-built entry into a parsed controllers.json root (its "controllers" array is
// created if missing). All or nothing:
//   - configId names an entry already in the file -> that entry is REPLACED whole by `entry`, and
//     only its identity survives: config_id plus what it answers to ("ids" and the common
//     "product_name"). The wizard only knows the id the pad came in by this time; taking its list
//     would lose the entry's other ids (other connections). Whatever the wizard didn't write
//     this time is dropped on purpose: a skipped or failed step (no "imu", no "touchpad"), and the
//     Mapeador's settings (calibration, trigger_actions, touchpad surface/zones...). The wizard
//     rebuilds the pad's raw base entry; customisation lives in game profiles.
//   - empty or unknown configId -> `entry` is appended with a fresh config_id made from `name`.
// Header-only so the wizard and the tests share it without another .cpp in every project.
// Returns the config_id the entry ended up with.
inline std::string putWizardEntry(nlohmann::json& root, nlohmann::json entry,
                                  const std::string& configId, const std::string& name) {
    if (!root.contains("controllers") || !root["controllers"].is_array())
        root["controllers"] = nlohmann::json::array();
    auto& controllers = root["controllers"];

    std::vector<std::string> existingIds;
    for (auto& existing : controllers) {
        const std::string id = existing.value("config_id", "");
        if (!configId.empty() && id == configId) {
            // Identity, not mapping: taken from the old entry as it was, absent keys included.
            static const char* const identityKeys[] = { "ids", "product_name" };
            for (const char* key : identityKeys) {
                if (existing.contains(key)) entry[key] = existing[key];
                else                        entry.erase(key);
            }
            entry["config_id"] = configId;
            existing = std::move(entry);
            return configId;
        }
        existingIds.push_back(id);
    }

    const std::string newId = makeConfigId(name, existingIds);
    entry["config_id"] = newId;
    controllers.push_back(std::move(entry));
    return newId;
}

// "Separar este mando": takes the pad's vid/pid out of the "ids" of the entry named configId, so
// that entry stops answering to it and the wizard can append the pad as a new entry. The rest of
// the entry (its mapping, its other ids) is untouched. Refuses (returns false, root unchanged)
// when the entry isn't there, has no such id, or would be left without ids — an entry must keep
// at least one (load error otherwise).
inline bool splitIdFromEntry(nlohmann::json& root, const std::string& configId,
                             uint16_t vid, uint16_t pid) {
    if (configId.empty() || !root.contains("controllers") || !root["controllers"].is_array())
        return false;
    // Ids are hex strings ("2DC8"); compared as numbers so letter case doesn't matter.
    auto hexValue = [](const nlohmann::json& id, const char* key) -> long {
        if (!id.contains(key) || !id[key].is_string()) return -1;
        try { return static_cast<long>(std::stoul(id[key].get<std::string>(), nullptr, 16)); }
        catch (...) { return -1; }
    };
    for (auto& existing : root["controllers"]) {
        if (existing.value("config_id", "") != configId) continue;
        if (!existing.contains("ids") || !existing["ids"].is_array()) return false;
        nlohmann::json kept = nlohmann::json::array();
        for (const auto& id : existing["ids"])
            if (hexValue(id, "vid") != vid || hexValue(id, "pid") != pid) kept.push_back(id);
        if (kept.size() == existing["ids"].size() || kept.empty()) return false;
        existing["ids"] = std::move(kept);
        return true;
    }
    return false;
}
