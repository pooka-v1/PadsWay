#pragma once
#include "ConfigLoader.h"   // makeConfigId
#include "../nlohmann/json.hpp"
#include <string>
#include <utility>
#include <vector>

// Puts one wizard-built entry into a parsed controllers.json root (its "controllers" array is
// created if missing). All or nothing:
//   - configId names an entry already in the file -> that entry is REPLACED whole by `entry`, and
//     only its config_id survives. Whatever the wizard didn't write this time is dropped on
//     purpose: a skipped or failed step (no "imu", no "touchpad"), and the Mapeador's settings
//     (calibration, trigger_actions, touchpad surface/zones...). The wizard rebuilds the pad's raw
//     base entry; customisation lives in game profiles.
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
