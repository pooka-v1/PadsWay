#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "TouchZones.h"  // TouchZoneRegion — TouchpadConfig::zones

enum class ButtonActionType  { VirtualButton, Trigger, TriggerPassthrough, Bot, Macro, Keyboard, MouseClick };

// Returns true if s is a stick half-axis slot direction (e.g. "right_x_neg").
inline bool isStickSlotDir(const std::string& s) {
    return s == "left_x_pos"  || s == "left_x_neg"  ||
           s == "left_y_pos"  || s == "left_y_neg"  ||
           s == "right_x_pos" || s == "right_x_neg" ||
           s == "right_y_pos" || s == "right_y_neg";
}

struct ButtonAction {
    ButtonActionType     type      = ButtonActionType::VirtualButton;
    std::string          name;        // virtual button ("a","b",...), bot/macro name
    std::string          physical;    // physical button id ("a","l4","rp"...) — never overwritten by profiles
    std::string          axis;        // trigger only: HID source axis name
    std::string          target;      // trigger only: "l2" or "r2"
    std::string          execution;   // macro only: compact execution string
    std::vector<std::string> keys;    // keyboard only: e.g. ["alt","tab"]
    std::string          mouseButton; // mouse_click only: "left","right","middle"
};

// One action bound to a specific range of a physical trigger's value.
struct TriggerRange {
    float        from      = 0.0f;  // inclusive lower bound [0, 1]
    float        to        = 1.0f;  // inclusive upper bound [0, 1]
    ButtonAction action;
    bool         hasAction = false; // true only if an action was explicitly set
};

enum class HalfAxisActionType {
    VirtualButton,  // { "virtual": "a" }           — standard button
    Dpad,           // { "virtual": "dpad_up" }      — dpad direction
    Trigger,        // { "virtual": "l2"/"r2" }      — virtual trigger
    StickSlot,      // { "virtual": "left_x_pos" }   — stick half-axis slot
    Analog,         // { "type": "analog" }           — proportional stick-to-stick (legacy)
    Keyboard,       // { "type": "keyboard" }
    Macro,          // { "type": "macro" }
    Bot,            // { "type": "bot", "name": "..." } — toggles a bot plugin on/off
    MouseClick,     // { "type": "mouse_click" }      — digital mouse button
    MouseMove,      // { "target": "mouse_x|mouse_y" } — proportional mouse movement
    Ranges          // { "ranges": [...] }             — ranged actions
};

// One action bound to a single half-axis direction.
// Key in ControllerConfig::axis_actions: "<virtual_axis>_pos" or "<virtual_axis>_neg"
// e.g. "left_x_pos", "right_y_neg"
struct HalfAxisAction {
    HalfAxisActionType   type       = HalfAxisActionType::VirtualButton;
    // target: button name (VirtualButton), dpad direction (Dpad), slot name (StickSlot),
    //         trigger name (Trigger), virtual axis (Analog), mouse axis (MouseMove),
    //         macro name (Macro)
    std::string          target;
    std::string          outDir;    // Analog: "pos"|"neg" — which virtual half to drive
    float                threshold = 0.5f;   // digital: activation threshold
    float                scale     = 1.0f;   // Analog: output multiplier
    float                speed     = 15.0f;  // MouseMove: pixels per tick at full deflection
    bool                 invert    = false;  // MouseMove: flip this mapping's own sign — independent
                                              // of axis calibration, so it doesn't affect anything
                                              // else that reads the same physical sensor axis
    std::vector<std::string> keys;           // Keyboard
    std::string          mouseButton;        // MouseClick: "left"|"right"|"middle"
    std::string          execution;          // Macro: compact execution string
    std::vector<TriggerRange> ranges;        // Ranges
};

struct AxisMapping {
    std::string target;
    bool        invert    = false;
    float       speed     = 15.0f;    // mouse_x/mouse_y only: pixels per tick at full deflection
    std::string stickId;              // permanent physical axis ID: "left_x"|"left_y"|"right_x"|"right_y"
    std::string btnNeg;               // btn_dir: virtual button when v < -threshold  (e.g. "l1")
    std::string btnPos;               // btn_dir: virtual button when v > +threshold  (e.g. "r1")
    float       threshold = 0.5f;     // dpad_x/dpad_y/btn_dir activation threshold
};

// Superficie channel mode — see ARCHITECTURE.md "Touchpad" section for the full design.
// Mouse (pre-existing delta-to-mouse routing) and Analog (recentered touch position -> a chosen
// virtual stick, TouchpadConfig::analogStickTarget) have real behavior; Gesture/Zones are still
// selectable placeholders until their own implementation tasks land. Unassigned is the device
// default (see TouchpadConfig::surfaceMode's comment) — none of the per-mode routing below ever
// matches it (every one of them is an explicit == check against a specific mode, never a "not
// Unassigned" catch-all), so an unassigned touchpad drives nothing: no cursor movement, no stick,
// no zone/gesture dispatch. The Boton channel (clickTarget) is untouched by this — it isn't
// gated on surfaceMode at all, by design (see PhysicalTouchpad::process()'s comment).
enum class TouchpadSurfaceMode { Unassigned, Mouse, Analog, Gesture, Zones };

inline const char* touchpadSurfaceModeToString(TouchpadSurfaceMode m) {
    switch (m) {
        case TouchpadSurfaceMode::Mouse:   return "mouse";
        case TouchpadSurfaceMode::Analog:  return "analog";
        case TouchpadSurfaceMode::Gesture: return "gesture";
        case TouchpadSurfaceMode::Zones:   return "zones";
        default:                           return "unassigned";
    }
}

inline TouchpadSurfaceMode touchpadSurfaceModeFromString(const std::string& s) {
    if (s == "mouse")   return TouchpadSurfaceMode::Mouse;
    if (s == "analog")  return TouchpadSurfaceMode::Analog;
    if (s == "gesture") return TouchpadSurfaceMode::Gesture;
    if (s == "zones")   return TouchpadSurfaceMode::Zones;
    return TouchpadSurfaceMode::Unassigned;
}

struct TouchpadConfig {
    bool enabled      = false;
    int  dataOffset   = 35;    // byte index of finger-1 data in raw HID report (DS4 USB: 35, see REFERENCE.md)
    int  maxX         = 1919;  // DS4 touchpad horizontal resolution
    int  maxY         = 942;   // DS4 touchpad vertical resolution
    // Sustituye al bool mouseEnabled: route surface movement -> mouse (delta-based) is now just
    // the Mouse case of this enum. Device default; per-profile overridable via applyProfile()
    // (reverted 2026-08-31, see ARCHITECTURE.md "Touchpad") — a profile that doesn't declare its
    // own surface_mode simply inherits whatever this device default already is.
    // Defaults to Unassigned, not Mouse (changed 2026-09-02, see BITACORA.md that date) — Mouse
    // was only ever the default because it used to be the sole mode that existed; an accidental
    // brush of the pad shouldn't move the cursor mid-game before the user has chosen a mode.
    TouchpadSurfaceMode surfaceMode = TouchpadSurfaceMode::Unassigned;
    // Analog mode only: which virtual stick the recentered touch position drives directly,
    // "left"/"right"/"both"/"" (empty = unassigned — surface reads but drives nothing). "both"
    // splits the surface left/right (split-lr-2): whichever finger is on each half drives that
    // half's stick, each recentered on its own half rather than the whole surface. Per-profile
    // overridable, same as surfaceMode above — see ARCHITECTURE.md "Touchpad" -> "Analogico".
    std::string analogStickTarget;

    // Zones mode only. zoneTemplateId records which catalog template (data/touch_zone_templates.json)
    // seeded this instance; zones is the actual per-instance region list, copied from that template
    // and then adjustable (bounds dragged, regions disabled) — never just a read-only reference to
    // the catalog. Empty zones = Zonas not configured yet on this device (surface reads but drives
    // nothing, same inert state as an empty analogStickTarget). Per-profile overridable, same as
    // surfaceMode/analogStickTarget above (reverted 2026-09-01) — a profile's touchZoneActions are
    // keyed by region id, which only means something against whatever template/geometry was active
    // when the profile was built, so letting a profile pin its own zoneTemplateId/zones keeps its
    // region-id actions from orphaning if Normal mode's template changes later — see
    // ARCHITECTURE.md "Touchpad" -> "Zonas".
    std::string zoneTemplateId;
    std::vector<TouchZoneRegion> zones;

    // Calibration for the raw touch position (Calibracion panel), independent per axis — the
    // pad has no radial shape to speak of (even a round one, e.g. Steam Controller, is read as
    // two independent linear axes internally). Values are magnitude-from-pad-center (0.5, 0.5 in
    // the normalized [0,1] touch1X/touch1Y space): 1 = edge. No deadzone — deliberately, unlike
    // ImuConfig's per-axis pairs: a "dead center" only ever makes sense for Zonas, and a zone
    // template with an unassigned central region already gives exactly that (see BITACORA.md
    // 2026/09/02) without a second mechanism doing the same job. max can exceed 1 (same headroom
    // the gyro/accel widgets allow) to compensate for a wizard maxX/maxY that landed a few % short
    // of the true physical edge — a raw reading past the nominal edge is harmless, it only means
    // the calibrated range now reaches all the way out. Consumers: Analogico/Zonas remap position
    // through this (ComponentTypes.cpp), Raton refuses to move the cursor past it (PadEngine.cpp),
    // Gestos divides its raw travel by it so a smaller usable range reads as more sensitive
    // (HIDInputSource.cpp).
    float xMax = 1.0f;
    float yMax = 1.0f;
};

struct ImuConfig {
    bool  enabled      = false;

    // Gyroscope (angular velocity). Independent per-axis byte offsets — some
    // controllers (8BitDo Pro 3) do not use the DS4's contiguous X,Y,Z order.
    int   gyroXOffset  = 13;   // pitch
    int   gyroYOffset  = 15;   // yaw
    int   gyroZOffset  = 17;   // roll
    float gyroScale    = 1.0f / 32768.0f;  // int16 raw → normalized [-1..1]
    // Sign fixup, set by the wizard's calibration: true when the raw reading decreases while
    // rotating toward the axis's canonical positive direction (right/forward/clockwise) and
    // needs flipping. See REFERENCE.md, "Inversion de ejes IMU - propuesta".
    bool  gyroXInvert  = false;
    bool  gyroYInvert  = false;
    bool  gyroZInvert  = false;

    // Per-axis calibration (mirrored bar around 0 in the Calibracion UI, see ARCHITECTURE.md
    // "Calibracion de entrada"). deadzone: fraction of the normalized [-1,1] reading below which
    // the axis is treated as 0. max: same threshold role as StickCalibration/TriggerCalibration's
    // `max` (the raw reading at which output already saturates to +-1.0), except it isn't
    // clamped to <=1.0 here — the sensor has no mechanical stop, so max<1.0 boosts sensitivity
    // and max>1.0 softens it (the raw ceiling alone doesn't reach full output).
    float gyroXDeadzone = 0.0f;
    float gyroYDeadzone = 0.0f;
    float gyroZDeadzone = 0.0f;
    float gyroXMax      = 1.0f;
    float gyroYMax      = 1.0f;
    float gyroZMax      = 1.0f;

    // Accelerometer (gravity/orientation). -1 = axis not present on this device.
    int   accelXOffset = -1;
    int   accelYOffset = -1;
    int   accelZOffset = -1;
    float accelScale   = 1.0f / 32768.0f;
    bool  accelXInvert = false;
    bool  accelYInvert = false;
    bool  accelZInvert = false;

    // Same shape as the gyro calibration fields above.
    float accelXDeadzone = 0.0f;
    float accelYDeadzone = 0.0f;
    float accelZDeadzone = 0.0f;
    float accelXMax      = 1.0f;
    float accelYMax      = 1.0f;
    float accelZMax      = 1.0f;
};

// Radial deadzone/max for one analog stick (shared between its X and Y half-axes — see
// ARCHITECTURE.md "Calibracion de entrada": calibrating both axes together keeps the feel
// symmetric between directions). No gain: max is the stick's mechanical travel limit, output
// can never exceed 1.0.
struct StickCalibration {
    float deadzone = 0.0f;  // [0, 1) of the radial magnitude below which the stick reads 0
    float max      = 1.0f;  // (0, 1] radial magnitude that already saturates output to 1.0
};

// Deadzone/max for one physical trigger's proportional [0,1] reading. No gain, same reasoning
// as StickCalibration: the trigger's own travel is the ceiling.
struct TriggerCalibration {
    float deadzone = 0.0f;
    float max      = 1.0f;
};

struct ControllerConfig {
    uint16_t    vid = 0;
    uint16_t    pid = 0;
    std::string source_name;
    std::string mode;
    std::string connection;    // "usb" / "bt" / "" = match any
    std::string product_name;  // BT/HID product name filter — partial match, case-insensitive

    std::unordered_map<int, ButtonAction>           buttons;       // physical bit (1-indexed) -> action
    std::unordered_map<std::string, AxisMapping>    axes;          // HID source name -> whole-axis mapping
    std::unordered_map<std::string, HalfAxisAction> axis_actions;  // "left_x_pos"/"right_y_neg"/... -> per-direction action
    // Gyro/accel half-axis actions. Keys "x_pos"/"x_neg"/"y_pos"/"y_neg"/"z_pos"/"z_neg" use each
    // sensor's own native letter (gyroX=pitch/gyroY=yaw/gyroZ=roll vs accelX=lateral/accelY=frontal/
    // accelZ=normal — they do NOT share the same letter-to-gesture mapping, see BindingWizard.cpp
    // classifyGyro()/finishGyroRound()). The Mapper UI translates arrow direction -> the right key
    // per sensor; this struct just stores whatever was assigned, generically.
    std::unordered_map<std::string, HalfAxisAction> gyro_actions;
    std::unordered_map<std::string, HalfAxisAction> accel_actions;
    std::unordered_map<std::string, std::string>    dpadRemap;     // "up"/"down"/"left"/"right" -> virtual short name
    std::unordered_map<std::string, ButtonAction>   dpadActions;   // "up"/"down"/"left"/"right" -> keyboard/mouse/macro action
    // Touchpad Zonas: region id (TouchZoneRegion::id, from touchpad.zones) -> action. Same
    // ButtonAction vocabulary as dpadActions (VirtualButton/Trigger/Bot/Macro/Keyboard/MouseClick),
    // dispatched the same way — a touch zone is just a digital source with a dynamic id set instead
    // of dpadActions' fixed 4 directions. See ARCHITECTURE.md "Touchpad" -> "Zonas".
    std::unordered_map<std::string, ButtonAction>   touchZoneActions;
    // Touchpad Movimiento (Gestos): gesture id (see kGestureIcons in MappingEditor.cpp, e.g.
    // "up"/"pinch_close"/"twist_up_down"/...) -> action. Same ButtonAction vocabulary/dispatch as
    // touchZoneActions above — all 14 gestures use it, including the 2 twist ones: a twist is a
    // one-shot release classification too (two fingers moving in opposite vertical directions),
    // not a continuous signal, see TouchGestures.h's classifyTwoFingerGesture(). See
    // ARCHITECTURE.md "Touchpad" -> "Movimiento".
    std::unordered_map<std::string, ButtonAction>   touchGestureActions;
    std::string    dpad;
    std::string    layout_id;  // references an entry in data/pad_layouts.json; empty = use defaults
    TouchpadConfig touchpad;
    ImuConfig      imu;

    // Physical input calibration (deadzone/max) — see ARCHITECTURE.md "Calibracion de entrada".
    StickCalibration   leftStickCalib;
    StickCalibration   rightStickCalib;
    TriggerCalibration triggerLCalib;
    TriggerCalibration triggerRCalib;

    // Physical trigger → action mapping (physical trigger as source)
    ButtonAction   triggerLAction;
    ButtonAction   triggerRAction;
    bool           triggerLHasAction = false;
    bool           triggerRHasAction = false;
    // Physical trigger → ranged actions (if non-empty, overrides the simple action above)
    std::vector<TriggerRange> triggerLRanges;
    std::vector<TriggerRange> triggerRRanges;

    // Stick slot assignments: slot key → source name.
    // Slot keys: "left_x_pos", "left_x_neg", "left_y_pos", "left_y_neg",
    //            "right_x_pos", "right_x_neg", "right_y_pos", "right_y_neg".
    // Sources: physShort ("a","b",...), "dpad_up/down/left/right", "l2", "r2".
    // Undefined slots fall through to the axes mapping for that axis.
    // Trigger sources without ranges: analog (0..1). With ranges: digital (0 or 1).
    std::unordered_map<std::string, std::vector<std::string>> stickSlots; // slot → [sources] (OR: any active drives the slot)

    // Bots that start automatically when this device is active (by name, matching a loaded DLL).
    std::vector<std::string> context_bots;
};
