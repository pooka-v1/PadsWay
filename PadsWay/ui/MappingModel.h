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

    // Gyro/accel half-axis actions: "x_pos"/"x_neg"/… (each sensor's own native letter) → action.
    std::unordered_map<std::string, HalfAxisAction> gyroActionEdits;
    std::unordered_map<std::string, HalfAxisAction> accelActionEdits;

    // Simple trigger actions: "l2"/"r2" → ButtonAction.
    std::unordered_map<std::string, ButtonAction>   trigActionEdits;

    // Trigger range edits for L2 and R2.
    std::vector<RangeEdit> trigLRangeEdits;
    std::vector<RangeEdit> trigRRangeEdits;

    // Stick slot assignments: slot key → source name.
    std::unordered_map<std::string, std::string> stickSlotEdits;

    // Bots to start automatically while this profile is active (profile mode only).
    std::vector<std::string> contextBotsEdits;

    // Touchpad Superficie channel mode — per-profile overridable (reverted 2026-08-31; see
    // ARCHITECTURE.md "Touchpad"), like touchGestureActionEdits below. reloadFromConfig() picks it
    // up from whatever ControllerConfig it's given (base or profile-applied via applyProfile()),
    // and saveProfile() now writes a per-field diff against base (MappingModel.cpp).
    TouchpadSurfaceMode touchSurfaceMode = TouchpadSurfaceMode::Unassigned;

    // Analog mode only: which virtual stick the surface drives, "left"/"right"/"" (none).
    // Per-profile overridable, same as touchSurfaceMode above.
    std::string touchAnalogStickTarget;

    // Zones mode only. touchZoneTemplateId/touchZones mirror TouchpadConfig::zoneTemplateId/zones
    // (instance geometry) — per-profile overridable (reverted 2026-09-01, same as touchSurfaceMode
    // above): a profile's touchZoneActionEdits below is keyed by region id, and those ids are only
    // meaningful against whatever zone template/geometry was active when the profile was designed
    // (cross-x-4 uses n/s/e/w, compass-8 uses all 8, etc.) — letting a profile pin its own template
    // means its region-id actions don't silently orphan if Normal mode's template changes later.
    // touchZoneActionEdits mirrors gyroActionEdits/accelActionEdits' shape (region id -> action)
    // but ButtonAction instead of HalfAxisAction, one entry per region with an assigned action.
    std::string touchZoneTemplateId;
    std::vector<TouchZoneRegion> touchZones;
    std::unordered_map<std::string, ButtonAction> touchZoneActionEdits;

    // Movimiento (Gestos) mode only. Same shape/precedent as touchZoneActionEdits above (gesture
    // id -> action), but keyed by the fixed 14-gesture catalog id instead of a data-driven region
    // id — all 14 gestures use this map, twist included (see TouchGestures.h's
    // classifyTwoFingerGesture()). Per-profile overridable, like touchZoneActionEdits.
    std::unordered_map<std::string, ButtonAction> touchGestureActionEdits;

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
    // Returns false if the file couldn't be written (e.g. missing directory) —
    // callers must check this instead of assuming success, since this never throws.
    bool saveProfile(const std::string& path, const std::string& profileName,
                     const ControllerConfig& base);

    // Clear all edit maps (does not reset vid/pid).
    void clear();
};
