#pragma once
#include "../input/ControllerConfig.h"
#include "../input/ComponentTypes.h"
#include "../ui/PadLayout.h"
#include <vector>
#include <string>
#include <unordered_map>

// Loads all controller configs from a JSON file.
std::vector<ControllerConfig> loadControllerConfigs(const std::string& path);

// Returns a pointer to the best-matching config, or nullptr if not found.
// Matches on VID+PID (required). connection (+2) and sourceName (+1) are optional
// secondary discriminators. Entries that declare these fields are skipped when the
// incoming value doesn't match; entries without them act as generic fallbacks.
const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid,
                                   const std::string& connection  = "",
                                   const std::string& sourceName  = "");

// Loads macro library from a JSON file (name -> execution string).
// Returns an empty map if the file does not exist.
std::unordered_map<std::string, std::string> loadMacroLibrary(const std::string& path);

struct VirtualPadConfig {
    uint16_t                 vid                    = 0x5650;   // defaults if file is missing
    uint16_t                 pid                    = 0x0001;
    std::string              logLevel               = "info";   // trace/debug/info/warn/error
    std::string              locale                 = "en";
    std::vector<std::string> acceptedXboxButtons    = {"a","b","x","y","l1","r1","select","start","home","l3","r3"};
    float                    stickSelectThreshold   = 0.85f;    // normalized [0,1]
    int                      stickHoldMs            = 2000;     // ms held at tope to select direction
};

// Loads virtual pad identity config from a JSON file.
// Returns defaults if the file does not exist.
VirtualPadConfig loadVirtualPadConfig(const std::string& path);

// ── Game profiles ──────────────────────────────────────────────────────────

// A game profile declares button and axis overrides for one or more controllers.
struct GameProfile {
    std::string profile_name;

    struct Override {
        uint16_t vid = 0, pid = 0;
        std::unordered_map<int, ButtonAction>           buttons;       // physical bit -> action
        std::unordered_map<std::string, AxisMapping>    axes;          // source -> whole-axis mapping
        std::unordered_map<std::string, HalfAxisAction> axis_actions;  // "source_pos/neg" -> per-direction action
    };
    std::vector<Override> overrides;
};

// Loads a game profile JSON. Returns a profile with an empty name if the
// file does not exist or has no profile_name field.
GameProfile loadGameProfile(const std::string& path);

// Returns a copy of base with the matching override's buttons applied on top.
// Axes and dpad are unchanged. If no override matches vid/pid, returns base as-is.
ControllerConfig applyProfile(const ControllerConfig& base, const GameProfile& profile);

// ── Component System ─────────────────────────────────────────────────────────

// Builds a PhysicalController from one controllers.json entry.
// Runs in parallel with ControllerConfig — does not affect existing behaviour.
std::vector<PhysicalController> loadPhysicalControllers(const std::string& path);

// ── Pad layouts ─────────────────────────────────────────────────────────────

// Loads all pad layouts from a JSON file.
// Returns an empty vector if the file does not exist.
std::vector<PadLayout> loadPadLayouts(const std::string& path);

// Returns a pointer to the layout with the given id, or nullptr if not found.
const PadLayout* findLayout(const std::vector<PadLayout>& layouts, const std::string& id);

// Serialises all pad layouts back to a JSON file.
// Throws std::runtime_error if the file cannot be written.
void savePadLayouts(const std::string& path, const std::vector<PadLayout>& layouts);
