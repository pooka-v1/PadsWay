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
// Matches on VID+PID (required). Optional discriminators add to a score;
// entries that declare a discriminator but don't match are skipped entirely.
//   connection  (+2): "usb"/"bt" — transport type
//   product_name(+2): partial case-insensitive match against the device's HID name
//   sourceName  (+1): exact match, used only by the wizard for re-pair
const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid,
                                   const std::string& connection   = "",
                                   const std::string& sourceName   = "",
                                   const std::string& productName  = "");

// Loads macro library from a JSON file (name -> execution string).
// Returns an empty map if the file does not exist.
std::unordered_map<std::string, std::string> loadMacroLibrary(const std::string& path);

// Saves macro library to a JSON file. Entries are written in the order supplied.
// Throws std::runtime_error if the file cannot be written.
void saveMacroLibrary(const std::string& path,
                      const std::vector<std::pair<std::string, std::string>>& macros);

struct VirtualPadConfig {
    uint16_t                 vid                    = 0x5650;   // defaults if file is missing
    uint16_t                 pid                    = 0x0001;
    std::string              logLevel               = "info";   // trace/debug/info/warn/error
    std::string              locale                 = "en";
    float                    fontSize               = 17.0f;    // ImGui font size in pixels
    std::vector<std::string> acceptedXboxButtons    = {"a","b","x","y","l1","r1","select","start","home","l3","r3"};
    float                    stickSelectThreshold   = 0.85f;    // normalized [0,1]
    int                      stickHoldMs            = 2000;     // ms held at tope to select direction
};

// Loads virtual pad identity config from a JSON file.
// Returns defaults if the file does not exist.
VirtualPadConfig loadVirtualPadConfig(const std::string& path);

// ── Game profiles ──────────────────────────────────────────────────────────

// A game profile declares controller-agnostic button overrides.
// Keys are virtual Xbox output names (a, b, x, y, l1, r1, l2, r2, l3, r3,
// start, select, home, dpad_up/down/left/right). The profile applies to any
// controller: overrides for virtual buttons the controller does not produce
// are silently ignored.
struct GameProfile {
    std::string profile_name;
    std::unordered_map<std::string, ButtonAction> buttons;  // virtual Xbox name -> action
    std::unordered_map<std::string, AxisMapping>  axes;     // virtual axis name (right_x, left_y…) -> new mapping
};

// Loads a game profile JSON. Returns a profile with an empty name if the
// file does not exist or has no profile_name field.
GameProfile loadGameProfile(const std::string& path);

// Returns a copy of base with the profile's button overrides applied.
// Resolves each virtual Xbox name to the bit that produces it on this controller.
// Overrides for virtual buttons not produced by this controller are silently ignored.
ControllerConfig applyProfile(const ControllerConfig& base, const GameProfile& profile);

// Updates the button layer of pc to match cfg's button type assignments.
// Axes, triggers, and analog components are not modified.
// Call after applyProfile() to ensure the Component System reflects profile overrides.
void rebuildPhysicalControllerButtons(PhysicalController& pc, const ControllerConfig& cfg);

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
