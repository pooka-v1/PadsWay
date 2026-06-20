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
    Touchpad, Gyro,
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
struct VirtualMouseMove  { MouseAxis axis; float speed;    };  // proportional
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

// ─── Accumulators ─────────────────────────────────────────────────────────────

struct StickAccumulator {
    float xPos = 0, xNeg = 0, yPos = 0, yNeg = 0;

    void flush(float& outX, float& outY) const {
        float vx = xPos - xNeg, vy = yPos - yNeg;
        float mag = std::sqrt(vx * vx + vy * vy);
        if (mag > 1.0f) { vx /= mag; vy /= mag; }
        outX = vx;
        outY = vy;
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

using PhysicalComponent = std::variant<
    PhysicalButton,
    PhysicalDpadDir,
    PhysicalTrigger,
    PhysicalAnalogDir,
    PhysicalTouchpad,
    PhysicalGyro
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
