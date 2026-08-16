#pragma once
#include <array>
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
enum class ActionType { Xbox, Analog, Macro, Keyboard, Mouse, MouseMove, Bot };

// ---------------------------------------------------------------------------
// H9 gyro/accel progressive-sweep arming — see MappingEditor.cpp's H9 gyro/accel block.
// Each of the 6 logical directions (up, down, left, right, cw, ccw — fixed index order,
// matches MappingEditor.cpp's kImuDirs array) is tracked independently. Reasons: (1) gyro
// (angular velocity) decays back to ~0 the instant rotation stops even while the controller
// is still held tilted, so a position-based "hold at a modest threshold" can never complete
// for yaw; sustaining near-max angular velocity for a brief instant (i.e. "keep spinning") is
// achievable instead. (2) a shared single-candidate cascade let incidental motion on one axis
// interrupt or steal another axis's in-progress gesture — tracking all 6 independently removes
// that interference. (3) for accel specifically, getting the controller into position to
// rotate it (pick it up, tilt it back) reads as a big, sustained cardinal-direction tilt in
// its own right — reachedMax below disqualifies that from being mistaken for a deliberate
// calibration hold.
// Extracted as a free function (rather than a MappingEditor method) purely so it can run under
// Catch2 without pulling in MappingEditor's D3D11/HWND/PadEngine dependencies — see
// PadsWayTests/tests/test_MappingSelection.cpp.
struct ImuSweepState {
    float confirmTimer = 0.0f;  // accumulates while sustained in [armThresh, nearMaxFrac)
    // Accel-only: set once this ascent has touched near the calibrated max — disqualifies it
    // as "getting into position to rotate" rather than a deliberate partial-tilt hold, until
    // the axis returns to rest. See MappingEditor.cpp's kImuNearMaxFrac for the reasoning.
    bool  reachedMax = false;
};

struct ImuSweepResult {
    int   armedIdx     = -1;  // direction whose confirm hold just completed this frame, or -1
    int   bestDisplay  = -1;  // direction with the most progress (for the progress-bar UI), or -1
    float bestProgress = 0.0f;
};

// Advances all 6 directions' sweep state by one frame and reports the result. Pure function:
// mutates `sweep` in place, no other side effects.
// Index order for every 6-element array is fixed: {up, down, left, right, cw, ccw}.
// mags[d] can be negative (not yet at the extreme in that direction) — the comparisons below
// just treat that as "below threshold", same as a small positive value.
// isAccelDir = d<4: only the four accel (orientation) directions get the near-max
// "positioning, not a deliberate hold" disqualification (see ImuSweepState::reachedMax) — cw/ccw
// (gyro, angular velocity) have no such concept, sustaining near-max IS the deliberate gesture.
inline ImuSweepResult advanceImuSweep(std::array<ImuSweepState, 6>& sweep,
                                      const float (&mags)[6],
                                      const float (&restThresh)[6],
                                      const float (&armThresh)[6],
                                      float nearMaxFrac, float confirmSec, float dt) {
    ImuSweepResult result;
    // Getting into position to rotate (cw/ccw) pins the controller near the ceiling on
    // whichever of pitch (up/down) or roll (left/right) the wrist happens to tilt through as a
    // side effect — that already disqualifies that axis via reachedMax below, but the same
    // motion nudges the OTHER accel axis past ITS arm threshold without ever reaching its own
    // near-max, so it was arming an unrelated direction mid-spin. Confirmed on real hardware
    // (Pro3 D-mode) for both directions of the coupling. A deliberate pitch/roll hold never
    // coincides with the other axis pinned near max, so block whichever axis's counterpart is
    // pinned — live per-frame check, not sticky like reachedMax, so it re-enables the instant
    // the controller returns to a normal, non-rotating grip.
    bool pitchNearMax = mags[0] >= nearMaxFrac || mags[1] >= nearMaxFrac;
    bool rollNearMax  = mags[2] >= nearMaxFrac || mags[3] >= nearMaxFrac;

    for (int d = 0; d < 6; ++d) {
        ImuSweepState& sw = sweep[d];
        float m = mags[d];
        bool  isAccelDir = d < 4;
        bool  isPitchDir = d == 0 || d == 1;
        bool  isRollDir  = d == 2 || d == 3;
        if (m < restThresh[d]) {
            sw.confirmTimer = 0.0f;
            sw.reachedMax   = false;
        } else if (isAccelDir && m >= nearMaxFrac) {
            // Touched near-max at any point during this ascent — "positioning", not a
            // deliberate hold. Disqualify until back to rest.
            sw.reachedMax   = true;
            sw.confirmTimer = 0.0f;
        } else if (isAccelDir && sw.reachedMax) {
            sw.confirmTimer = 0.0f;  // already disqualified this ascent, even if now dipped back below near-max
        } else if (isRollDir && pitchNearMax) {
            sw.confirmTimer = 0.0f;  // pitch pinned near max — rotation setup, not a deliberate roll hold
        } else if (isPitchDir && rollNearMax) {
            sw.confirmTimer = 0.0f;  // roll pinned near max — rotation setup, not a deliberate pitch hold
        } else if (m >= armThresh[d]) {
            sw.confirmTimer += dt;
            if (sw.confirmTimer >= confirmSec) result.armedIdx = d;
        } else {
            sw.confirmTimer = 0.0f;  // mid-swing, not yet at the extreme
        }
        if (sw.confirmTimer > result.bestProgress) {
            result.bestProgress = sw.confirmTimer;
            result.bestDisplay  = d;
        }
    }
    return result;
}

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
    // Touchpad component split (see MappingEditor::onPhysTouchpadHit): true → left half
    // (Superficie/touch channel) selected, false → right half (Botón/btnTouch channel).
    // Only meaningful while the selected physComp's type is "touchpad".
    bool        touchSurfaceSelected = false;

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
    std::string  botSel;

    // --- Axis-action MouseMove state ---
    float       axisMouseSpeed  = 15.0f;
    std::string axisMouseAxis   = "mouse_x";
    // Gyro/accel-only: per-assignment invert, independent of axis calibration (see
    // gyroMouseAssign in MappingEditor.cpp). Defaults to checked when the Y axis is picked,
    // since pitch->Y is the known case needing the "pointer" convention (aim down = cursor down).
    bool        axisMouseInvert = false;

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

    // --- H9 gyro/accel progressive-sweep arming state ---
    // See the ImuSweepState/advanceImuSweep() comment near the top of this file for the design
    // rationale — moved out to file scope so advanceImuSweep() can be a free function tested in
    // isolation (PadsWayTests/tests/test_MappingSelection.cpp), without pulling in the rest of
    // MappingEditor's D3D11/HWND/PadEngine dependencies.
    std::array<ImuSweepState, 6> h9ImuSweep{};
    // Mirror of whichever direction is currently most advanced (highest confirmTimer), refreshed
    // each frame by MappingEditor purely for the progress-bar/hint display — not itself state-
    // driving (h9ImuSweep above is the source of truth for arming).
    std::string h9HoldGyroDir;
    float       h9HoldGyroTimer = 0.0f;

    // --- Gyro/accel source resolution for the currently armed direction (physComp+stickDir,
    // reused: a "gyro" component with stickDir in {up,down,left,right,cw,ccw}) ---
    bool imuUseAccel       = false;  // current toggle state
    bool imuSourceOverridden = false; // true once the user has touched the toggle by hand

    // Reset everything except macro/key names (those are UI resources, not state).
    void clear() {
        physComp      = -1;
        stickDir.clear();
        stickAsButton = false;
        dpadDir.clear();
        triggerSrc.clear();
        touchSurfaceSelected = false;
        flashComp     = -1;
        flashTimer    = 0.0f;
        flashVirtShort.clear();
        flashSlotKey.clear();
        flashPhysArrowComp = -1;
        flashPhysArrowDir.clear();
        actionType    = ActionType::Xbox;
        captureKeys.clear();
        macroSel.clear();
        botSel.clear();
        h9HoldComp    = -1;
        h9HoldStickDir.clear();
        h9HoldDpadDir.clear();
        h9HoldTimer   = 0.0f;
        h9ErrorTimer  = 0.0f;
        h9PrevPhysState = {};
        h9HoldTriggerSrc.clear();
        h9HoldTriggerTimer = 0.0f;
        h9HoldGyroDir.clear();
        h9HoldGyroTimer = 0.0f;
        h9ImuSweep = {};
        imuUseAccel = false;
        imuSourceOverridden = false;
        axisMouseSpeed  = 15.0f;
        axisMouseAxis   = "mouse_x";
        axisMouseInvert = false;
    }
};
