#pragma once
#include "ConfigLoader.h"   // makeConfigId
#include "../nlohmann/json.hpp"
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
