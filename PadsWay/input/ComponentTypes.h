#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "GamepadState.h"
#include "ControllerConfig.h"   // TouchpadConfig

// ─── Component identifiers ────────────────────────────────────────────────────

enum class ComponentId : uint8_t {
    BtnA, BtnB, BtnX, BtnY,
    BtnLB, BtnRB, BtnL3, BtnR3,
    BtnBack, BtnStart, BtnHome,
    DpadUp, DpadDown, DpadLeft, DpadRight,
    TriggerL, TriggerR,
    LeftXPos,  LeftXNeg,  LeftYPos,  LeftYNeg,
    RightXPos, RightXNeg, RightYPos, RightYNeg,
    Touchpad, Gyro, Accel,
    // 8BitDo extra paddles (not present on standard XInput controllers)
    BtnL4, BtnR4,     // short paddles (L4 / R4)
    BtnLP, BtnRP,     // long  paddles (L5 / R5)
    _Count   // always last — used to dimension the array
};

enum class ButtonId    : uint8_t { A, B, X, Y, LB, RB, L3, R3, Back, Start, Home };
enum class DpadDir     : uint8_t { Up, Down, Left, Right };
enum class TriggerSide : uint8_t { L, R };

enum class StickSlotId : uint8_t {
    LeftXPos,  LeftXNeg,  LeftYPos,  LeftYNeg,
    RightXPos, RightXNeg, RightYPos, RightYNeg
};

enum class GyroHalf    : uint8_t { XPos, XNeg, YPos, YNeg, ZPos, ZNeg };
enum class MouseAxis   : uint8_t { X, Y };
enum class MouseButton : uint8_t { Left, Right, Middle, Forward, Back };

// ─── VirtualTarget ────────────────────────────────────────────────────────────

struct VirtualButton     { ButtonId    id;                 };
struct VirtualDpadDir    { DpadDir     dir;                };
struct VirtualTrigger    { TriggerSide side;               };  // proportional
struct VirtualStickSlot  { StickSlotId slot;               };  // → StickAccumulator
struct VirtualKeyboard   { std::vector<uint8_t> keys;      };
struct VirtualMacro      { std::string name;               };
struct VirtualMouseClick { MouseButton button;             };
struct VirtualMouseMove  { MouseAxis axis; float speed; bool invert = false; };  // proportional
struct VirtualBot        { std::string name;               };
struct VirtualPassthrough{                                 };  // routes to natural equivalent

using VirtualTarget = std::variant<
    VirtualButton,
    VirtualDpadDir,
    VirtualTrigger,
    VirtualStickSlot,
    VirtualKeyboard,
    VirtualMacro,
    VirtualMouseClick,
    VirtualMouseMove,
    VirtualBot,
    VirtualPassthrough
>;

// ─── Range / RangedHalfAxis ───────────────────────────────────────────────────

struct Range {
    float         from;    // [0.0, 1.0]
    float         to;      // [0.0, 1.0]
    VirtualTarget target;
};

struct RangedHalfAxis {
    std::vector<Range> ranges;
    // empty = implicit VirtualPassthrough
};

// Remaps a magnitude in [0, 1] through a deadzone/max pair, see ARCHITECTURE.md "Calibracion
// de entrada": below deadzone reads 0, at/above max reads 1, linear in between. Shared by
// StickAccumulator::flush() (applied to the combined vector's magnitude) and physical triggers
// (applied directly, they're already a scalar). deadzone=0/max=1 is a no-op.
inline float applyDeadzoneMax(float mag, float deadzone, float max) {
    float range = max - deadzone;
    if (range > 1e-6f) return std::clamp((mag - deadzone) / range, 0.0f, 1.0f);
    return mag >= max ? 1.0f : 0.0f;
}

// Signed version of applyDeadzoneMax() for one gyro/accel axis reading in [-1, 1] — takes the
// magnitude through the same deadzone/max remap and restores the sign. Unlike sticks/triggers,
// `max` isn't a physical ceiling here (the sensor has no mechanical stop): max<1 boosts
// sensitivity (full output before the raw signal saturates), max>1 softens it (the raw signal
// alone never quite reaches full output) — see ARCHITECTURE.md "Calibracion de entrada".
inline float applyDeadzoneMaxSigned(float value, float deadzone, float max) {
    float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * applyDeadzoneMax(std::fabs(value), deadzone, max);
}

// Deadzone/max pair for one gyro or accel axis — see applyDeadzoneMaxSigned(). Mirrors
// ImuConfig's per-axis calibration fields (ControllerConfig.h) without duplicating its
// offset/scale/invert fields, which HIDInputSource owns.
struct ImuAxisCalibration {
    float deadzone = 0.0f;
    float max      = 1.0f;
};

// ─── Accumulators ─────────────────────────────────────────────────────────────

struct StickAccumulator {
    float xPos = 0, xNeg = 0, yPos = 0, yNeg = 0;

    // calib: radial deadzone/max applied to the combined vector — a single pair for both axes
    // so a stick feels the same in every direction. Default {0, 1} is a no-op, so existing
    // callers are unaffected.
    void flush(float& outX, float& outY, const StickCalibration& calib = {}) const {
        float vx = xPos - xNeg, vy = yPos - yNeg;
        float mag = std::sqrt(vx * vx + vy * vy);
        if (mag > 1.0f) { vx /= mag; vy /= mag; mag = 1.0f; }

        float shaped = applyDeadzoneMax(mag, calib.deadzone, calib.max);
        float scale  = mag > 1e-6f ? shaped / mag : 0.0f;
        outX = vx * scale;
        outY = vy * scale;
    }
};

struct GyroAccumulator {
    float xPos = 0, xNeg = 0;
    float yPos = 0, yNeg = 0;
    float zPos = 0, zNeg = 0;

    void flush(float& outX, float& outY, float& outZ) const {
        outX = std::clamp(xPos - xNeg, -1.0f, 1.0f);
        outY = std::clamp(yPos - yNeg, -1.0f, 1.0f);
        outZ = std::clamp(zPos - zNeg, -1.0f, 1.0f);
    }
};

// ─── Physical component types ─────────────────────────────────────────────────

// Process signature convention:
//   PhysicalButton / PhysicalDpadDir  → pressed: physical activation state this frame
//   PhysicalTrigger / PhysicalAnalogDir → value: normalized magnitude [0.0, 1.0]
//   PhysicalTouchpad / PhysicalGyro   → physical: full physical GamepadState snapshot
// PhysicalController extracts the right physical value per ComponentId before dispatching.

struct PhysicalButton {
    uint8_t       bit;      // position in HID report (1-based)
    VirtualTarget target;

    void process(bool pressed, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& gyro) const;
};

struct PhysicalDpadDir {
    DpadDir       dir;
    VirtualTarget target;

    void process(bool active, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& gyro) const;
};

struct PhysicalTrigger {
    TriggerSide    side;
    RangedHalfAxis axis;

    void process(float value, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& gyro) const;
};

struct PhysicalAnalogDir {
    StickSlotId    slot;    // physical position: LeftXPos, RightYNeg, ...
    RangedHalfAxis axis;

    void process(float value, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& gyro) const;
};

// Placeholder — will grow with gestures, touch zones, two-finger combos, etc.
// The touchpad click button goes in components[ComponentId::BtnHome] or similar, NOT here.
struct PhysicalTouchpad {
    TouchpadConfig cfg;

    void process(const GamepadState& physical, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& gyro) const;
};

struct PhysicalGyro {
    RangedHalfAxis xPos, xNeg;
    RangedHalfAxis yPos, yNeg;
    RangedHalfAxis zPos, zNeg;

    void process(const GamepadState& physical, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& gyro) const;
};

// Accelerometer (gravity/orientation) — same shape as PhysicalGyro, but the letter of each axis
// does NOT share gyro's pitch/yaw/roll semantics: accelX = lateral tilt (roll-equivalent),
// accelY = frontal tilt (pitch-equivalent), accelZ = normal/gravity (face up/down at rest, no
// rotational gesture of its own). See BindingWizard.cpp classifyGyro()/finishGyroRound() and
// REFERENCE.md. GyroAccumulator is reused here too — it accumulates any 3-axis half-axis
// passthrough, not something gyro-specific.
struct PhysicalAccel {
    RangedHalfAxis xPos, xNeg;
    RangedHalfAxis yPos, yNeg;
    RangedHalfAxis zPos, zNeg;

    void process(const GamepadState& physical, GamepadState& out,
                 StickAccumulator& left, StickAccumulator& right, GyroAccumulator& accel) const;
};

using PhysicalComponent = std::variant<
    PhysicalButton,
    PhysicalDpadDir,
    PhysicalTrigger,
    PhysicalAnalogDir,
    PhysicalTouchpad,
    PhysicalGyro,
    PhysicalAccel
>;

// ─── Modifier mask ────────────────────────────────────────────────────────────

// Bit i corresponds to the modifier at index i in PhysicalController::modifierSources.
// uint8_t supports up to 8 modifiers (256 combinations).
using ModifierMask = uint8_t;
constexpr ModifierMask kModNone = 0x00;

// ─── PhysicalController ───────────────────────────────────────────────────────

static constexpr size_t kComponentCount = static_cast<size_t>(ComponentId::_Count);

struct PhysicalController {
    std::string name;
    uint16_t    vid = 0;
    uint16_t    pid = 0;

    // Radial deadzone/max applied to the combined virtual stick output in process() — see
    // ARCHITECTURE.md "Calibracion de entrada". Set from ControllerConfig::leftStickCalib/
    // rightStickCalib by rebuildPhysicalControllerFromConfig().
    StickCalibration leftStickCalib;
    StickCalibration rightStickCalib;

    // Deadzone/max applied to the physical trigger's own [0,1] reading in process() — set from
    // ControllerConfig::triggerLCalib/triggerRCalib by rebuildPhysicalControllerFromConfig().
    TriggerCalibration triggerLCalib;
    TriggerCalibration triggerRCalib;

    // Deadzone/gain applied to each gyro/accel axis before PhysicalGyro/PhysicalAccel see it —
    // set from ControllerConfig::imu by rebuildPhysicalControllerFromConfig().
    ImuAxisCalibration gyroXCalib, gyroYCalib, gyroZCalib;
    ImuAxisCalibration accelXCalib, accelYCalib, accelZCalib;

    // Which ComponentIds act as modifiers (order defines the bit in ModifierMask).
    // Any component type is valid: button, dpad direction, trigger, analog dir, touchpad, gyro.
    // Analog sources (trigger, analog dir) use a configurable threshold to determine "active".
    // A modifier source can simultaneously have its own VirtualTarget in the base layer.
    std::vector<ComponentId> modifierSources;

    // Base layer (always present)
    std::array<std::optional<PhysicalComponent>, kComponentCount> baseLayer;

    // Per-modifier-combination overrides. Only explicitly defined combinations exist in the map.
    // Example with LP=bit0, RP=bit1:
    //   mask 0x01 → LP held
    //   mask 0x02 → RP held
    //   mask 0x03 → LP+RP held simultaneously
    std::map<ModifierMask, std::array<std::optional<PhysicalComponent>, kComponentCount>> modifierLayers;

    // Two-pass processing:
    //   Pass 1: evaluate modifierSources → build active ModifierMask
    //   Pass 2: for each ComponentId, resolve from modifierLayers[mask] first, then baseLayer
    // physical: GamepadState populated by the input source with raw physical values.
    // output:   GamepadState to receive the remapped virtual values.
    void process(const GamepadState& physical, GamepadState& output) const;

    std::optional<PhysicalComponent>& operator[](ComponentId id) {
        return baseLayer[static_cast<size_t>(id)];
    }
    const std::optional<PhysicalComponent>& operator[](ComponentId id) const {
        return baseLayer[static_cast<size_t>(id)];
    }
};
