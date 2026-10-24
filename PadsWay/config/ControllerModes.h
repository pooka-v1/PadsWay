#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../input/ControllerConfig.h"

// Controller modes — the "software" family a pad speaks: "d" (generic HID / DirectInput), "x" (Xbox),
// "s" (Switch). See ARCHITECTURE.md "Tarea 6", mode. This file only deduces a pad's mode from its
// id; the wizard shows it preselected and the user always confirms or changes it.

// True for "d" / "x" / "s" — the values an entry's mode may hold besides "" (not declared).
bool isKnownControllerMode(const std::string& mode);

// One row of data/known_controllers.json: an id whose mode we know (tested with real hardware or
// well documented). name is only for whoever reads the file.
struct KnownControllerId {
    uint16_t    vid = 0;
    uint16_t    pid = 0;
    std::string mode;
    std::string name;
};

// Loads data/known_controllers.json. Never throws: the file only feeds a proposal, so a broken
// file loads as empty and a bad row is skipped (both logged in warnings) instead of stopping the app.
std::vector<KnownControllerId> loadKnownControllers(const std::string& path,
                                                    std::vector<std::string>* warnings = nullptr);

// The mode to preselect for a pad, or "" if nothing knows it. Three layers, first hit wins:
//   1. known — ids we verified; first, so a user's past mistake can't hide one.
//   2. userConfigs — the user's controllers.json entry for this pad (findConfig), if it has a mode.
//   3. vendor rules — 045E (Microsoft) -> "x", 054C (Sony) -> "d", 057E (Nintendo) -> "s".
std::string deduceMode(uint16_t vid, uint16_t pid, const std::string& productName,
                       const std::vector<KnownControllerId>& known,
                       const std::vector<ControllerConfig>& userConfigs);
