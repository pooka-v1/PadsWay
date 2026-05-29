#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../input/ControllerConfig.h"
#include "../config/ConfigLoader.h"

// ---------------------------------------------------------------------------
// RangeEdit — working copy of a trigger range while the editor is open.
// Mirrors TriggerRange but decoupled from the parsed config.
// ---------------------------------------------------------------------------
struct RangeEdit {
    float        from      = 0.0f;
    float        to        = 1.0f;
    ButtonAction action;
    bool         hasAction = false;
};

// ---------------------------------------------------------------------------
// MappingModel — owns all pending mapping edits for the active controller.
//
// Responsibilities:
//   - Load edits from a ControllerConfig (reload)
//   - Serialize edits back to controllers.json (save)
//   - Expose edit maps so the UI can read/write them directly
//
// What it does NOT own:
//   - UI selection state (selected component, capture keys, etc.)
//   - Engine/config reload after save  ← AppWindow wrapper handles this
// ---------------------------------------------------------------------------
class MappingModel {
public:
    // Identity of the controller whose data is currently loaded.
    uint16_t vid = 0;
    uint16_t pid = 0;

    // Button remapping: physShort → virtShort (Xbox).
    std::unordered_map<std::string, std::string>    buttonEdits;

    // Non-Xbox button actions: physShort → ButtonAction (Keyboard/Mouse/Macro/Trigger).
    std::unordered_map<std::string, ButtonAction>   actionEdits;

    // Whole-axis remapping: stickId → AxisMapping.
    std::unordered_map<std::string, AxisMapping>    axisEdits;

    // Half-axis / dpad-direction actions: source key → HalfAxisAction.
    std::unordered_map<std::string, HalfAxisAction> axisActionEdits;

    // Simple trigger actions: "l2"/"r2" → ButtonAction.
    std::unordered_map<std::string, ButtonAction>   trigActionEdits;

    // Trigger range edits for L2 and R2.
    std::vector<RangeEdit> trigLRangeEdits;
    std::vector<RangeEdit> trigRRangeEdits;

    // Stick slot assignments: slot key → source name.
    std::unordered_map<std::string, std::string> stickSlotEdits;

    // Populate edits from the matching config entry (vid/pid must be set first).
    void reload(const std::vector<ControllerConfig>& configs);

    // Populate edits directly from a pre-resolved config (no file I/O).
    void reloadFromConfig(const ControllerConfig& cfg);

    // Populate edits from base config with profile overrides applied on top.
    // Sets vid/pid from base.
    void loadProfile(const ControllerConfig& base, const GameProfile& profile);

    // Serialize all edits to controllers.json.
    // Throws on JSON parse errors; does NOT reload engine configs.
    void save(const std::string& path);

    // Write only the delta (buttons that differ from base) to a profile JSON file.
    // Preserves existing overrides for other controllers in the file.
    void saveProfile(const std::string& path, const std::string& profileName,
                     const ControllerConfig& base);

    // Clear all edit maps (does not reset vid/pid).
    void clear();
};
