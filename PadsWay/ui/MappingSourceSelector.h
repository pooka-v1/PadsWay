#pragma once
#include "PadView.h"
#include "MappingModel.h"
#include "MappingSelection.h"
#include "../PadEngine.h"

// ---------------------------------------------------------------------------
// MappingSourceSelector — the full H9 state machine, extracted verbatim from
// MappingEditor::render() across two tasks (Tarea 3, parts 1 and 2 / "3b") — see
// SESSION_CONTEXT.md/BITACORA.md for the extraction record.
//
// - update() — H9 Paso 1: decides which physical component becomes the mapping source
//   (stick/button/dpad/touchpad/trigger/gyro-accel) while none is selected yet, plus the 2
//   instant re-pick adjustments for Movimiento/Zonas that apply even once the touchpad already
//   IS the selected source.
// - assign() — H9 Paso 2: once a source IS already selected, detects a rising edge on physical
//   input and writes the actual mapping into MappingModel.
//
// Deliberately stateless: everything it reads/writes lives in MappingSelection/MappingModel
// (passed by reference) or is passed in already computed (physNow, calibrated touch1X/Y, dt).
// The 3 gyro/accel source-resolution helpers this class's assign() shares with MappingEditor's
// own action-panel/modal code (resolveImuTargetMap/clearImuOtherMap/assignImuAction) live as free
// functions in MappingSelection.h, not as methods of either class, precisely to avoid a
// dependency in either direction.
// ---------------------------------------------------------------------------
class MappingSourceSelector {
public:
    static void update(const PadView& phys, const MappingModel& model, MappingSelection& sel,
                        const GamepadState& physNow, float touch1X, float touch1Y,
                        const PadEngine& engine, const std::vector<ControllerConfig>& configs,
                        float stickSelectThreshold, int stickHoldMs,
                        float gyroSelectThreshold, float accelSelectThreshold, float dt);

    // H9 Paso 2 (Tarea 3b): once a source is already selected, detect a rising edge on physical
    // input and write the actual mapping into MappingModel. Call only when update() above did NOT
    // arm a new selection this same frame (see MappingEditor::render()'s paso1Gate) - mirrors the
    // original if/else-if mutual exclusion between Paso 1 and Paso 2. No `dt` needed - Paso 2 does
    // rising-edge detection only, no timers.
    static void assign(const PadView& phys, const PadView& virt, MappingModel& model,
                        MappingSelection& sel, const GamepadState& physNow,
                        const std::vector<std::string>& acceptedXbox,
                        float stickSelectThreshold);

private:
    // Paso 2, non-trigger source (m_sel.physComp >= 0): rising-edge dispatch to VirtualButton/
    // Dpad/StickSlot targets, covering generic button/dpad/stick-click, stick-axis, gyro/accel,
    // and touch zone/gesture sources. Also handles the physical L2/R2 -> trigger-target and
    // virtual-stick-tilt -> stick-slot-target assignment paths.
    static void assignNonTriggerTarget(const PadView& phys, const PadView& virt, MappingModel& model,
                                        MappingSelection& sel, const GamepadState& physNow,
                                        const std::vector<std::string>& acceptedXbox,
                                        float stickSelectThreshold);

    // Paso 2, trigger source (m_sel.triggerSrc non-empty): much simpler than the non-trigger
    // branch - no gyro, no touch, writes only into MappingModel::trigActionEdits.
    static void assignTriggerTarget(const PadView& phys, const PadView& virt, MappingModel& model,
                                     MappingSelection& sel, const GamepadState& physNow,
                                     const std::vector<std::string>& acceptedXbox,
                                     float stickSelectThreshold);

    // Movimiento/Zonas instant re-pick — runs unconditionally, even with physComp >= 0 already
    // selected (once the touchpad already IS the source, picking a different gesture/region is a
    // separate step from arbitrating WHICH physical thing becomes the source in the first place).
    static void instantRepickGestureOrZone(const MappingModel& model, MappingSelection& sel,
                                            const GamepadState& physNow,
                                            float touch1X, float touch1Y);

    // Paso 1a: stick tilted past threshold, held for m_stickHoldMs -> select that axis. Returns
    // true whenever a stick is actively tilted past threshold this frame — used by update() to
    // decide whether Paso 1b/1c (armGenericComponentOrTrigger) runs at all this frame, mirroring
    // the original code's `if (activeStickComp >= 0) {...} else {...}` gate.
    static bool armStickAxis(const PadView& phys, MappingSelection& sel,
                              const GamepadState& physNow,
                              float stickSelectThreshold, int stickHoldMs, float dt);

    // Paso 1b: button/stick-click/dpad/touchpad held 1s -> select it. Falls through to Paso 1c
    // (trigger held 2s) when the scan finds nothing — same nesting as the original code, not a
    // sibling branch, since the trigger check must only run when nothing else is arming.
    static void armGenericComponentOrTrigger(const PadView& phys, const MappingModel& model,
                                              MappingSelection& sel, const GamepadState& physNow,
                                              float touch1X, float touch1Y, float dt);

    // Paso 1a-bis: gyro/accel progressive sweep from rest to a sustained extreme -> select that
    // direction. Runs unconditionally alongside 1a/1b/1c (never competes with them for the same
    // candidate slot) — see MappingSelection.h's ImuSweepState/advanceImuSweep for the rationale.
    static void armGyroAccelSweep(const PadView& phys, MappingSelection& sel,
                                   const GamepadState& physNow, const PadEngine& engine,
                                   const std::vector<ControllerConfig>& configs,
                                   float gyroSelectThreshold, float accelSelectThreshold, float dt);
};
