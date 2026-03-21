#pragma once
#include "../input/ControllerConfig.h"
#include <vector>
#include <string>
#include <unordered_map>

// Loads all controller configs from a JSON file.
std::vector<ControllerConfig> loadControllerConfigs(const std::string& path);

// Returns a pointer to the matching config, or nullptr if not found.
const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid);

// Loads macro library from a JSON file (name -> execution string).
// Returns an empty map if the file does not exist.
std::unordered_map<std::string, std::string> loadMacroLibrary(const std::string& path);
