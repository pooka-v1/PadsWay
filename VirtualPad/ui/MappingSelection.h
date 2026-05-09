#pragma once
#include <string>
#include <vector>
#include <utility>
#include "../GamepadState.h"

// ---------------------------------------------------------------------------
// ActionType — action panel mode selector.
// Used both for button/axis assignments and inside the trigger range modal.
// Defined here (not inside MappingSelection) so TriggerRangeModal can use it
// without a circular dependency.
// ---------------------------------------------------------------------------
enum class ActionType { Xbox, Analog, Macro, Keyboard, Mouse, MouseMove };

// ---------------------------------------------------------------------------
// MappingSelection — all transient UI selection and interaction state for the
// mapping editor.  This is NOT persisted to disk; it is reset when the mode
// closes or the active controller changes.
//
// What it owns:
//   - Which physical component / direction / trigger is selected
//   - H9 hardware-selection timers and hold state
//   - Action-panel capture state (keys, macro sel)
//   - Flash feedback state
//
// What it does NOT own:
//   - Pending edits (MappingModel)
//   - Rangos modal state (AppWindow, later → RangosModal)
//   - Canvas origins / textures (rendering, AppWindow)
// ---------------------------------------------------------------------------
struct MappingSelection {
    // --- Selected physical component ---
    int         physComp        = -1;   // index into layout.components (-1 = none)
    std::string stickDir;               // "up"/"down"/"left"/"right" or ""
    bool        stickAsButton   = false;// true → stick selected for L3/R3
    std::string dpadDir;                // "up"/"down"/"left"/"right" or ""
    std::string triggerSrc;             // "l2", "r2", or ""

    // --- Flash feedback on virtual pad ---
    int         flashComp       = -1;
    float       flashTimer      = 0.0f;
    std::string flashVirtShort;
    std::string flashSlotKey;        // slot key (e.g. "left_y_pos") for virtual stick-arrow flash

    // --- Flash feedback on physical pad (source analog direction) ---
    int         flashPhysArrowComp = -1;
    std::string flashPhysArrowDir;

    // --- H5 action panel state ---
    ActionType actionType     = ActionType::Xbox;
    std::vector<std::pair<std::string, std::string>> captureKeys; // {json_name, display}
    std::string  macroSel;

    // --- Axis-action MouseMove state ---
    float       axisMouseSpeed  = 15.0f;
    std::string axisMouseAxis   = "mouse_x";

    // --- H9 hardware-mapping hold state ---
    int         h9HoldComp      = -1;
    std::string h9HoldStickDir;
    std::string h9HoldDpadDir;
    float       h9HoldTimer     = 0.0f;
    float       h9ErrorTimer    = 0.0f;
    GamepadState h9PrevPhysState{};

    // --- H9 trigger hold state ---
    std::string h9HoldTriggerSrc;
    float       h9HoldTriggerTimer = 0.0f;

    // Reset everything except macro/key names (those are UI resources, not state).
    void clear() {
        physComp      = -1;
        stickDir.clear();
        stickAsButton = false;
        dpadDir.clear();
        triggerSrc.clear();
        flashComp     = -1;
        flashTimer    = 0.0f;
        flashVirtShort.clear();
        flashSlotKey.clear();
        flashPhysArrowComp = -1;
        flashPhysArrowDir.clear();
        actionType    = ActionType::Xbox;
        captureKeys.clear();
        macroSel.clear();
        h9HoldComp    = -1;
        h9HoldStickDir.clear();
        h9HoldDpadDir.clear();
        h9HoldTimer   = 0.0f;
        h9ErrorTimer  = 0.0f;
        h9PrevPhysState = {};
        h9HoldTriggerSrc.clear();
        h9HoldTriggerTimer = 0.0f;
        axisMouseSpeed = 15.0f;
        axisMouseAxis  = "mouse_x";
    }
};
