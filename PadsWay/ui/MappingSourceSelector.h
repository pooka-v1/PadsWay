#pragma once
#include "PadView.h"
#include "MappingModel.h"
#include "MappingSelection.h"
#include "../PadEngine.h"

// ---------------------------------------------------------------------------
// MappingSourceSelector — H9 Paso 1: decides which physical component becomes the mapping
// source (stick/button/dpad/touchpad/trigger/gyro-accel) while none is selected yet, plus the
// 2 instant re-pick adjustments for Movimiento/Zonas that apply even once the touchpad already
// IS the selected source. Extracted verbatim from MappingEditor::render() (Tarea 3, part 1) —
// see SESSION_CONTEXT.md/BITACORA.md for the extraction record.
//
// Deliberately stateless: everything it reads/writes lives in MappingSelection (passed by
// reference) or is passed in already computed (physNow, calibrated touch1X/Y, dt). H9 Paso 2
// (writing the actual mapping into MappingModel once a source is selected) stays in
// MappingEditor::render(), unchanged — it is far more coupled to MappingEditor's own private
// helpers (resolveImuTargetMap/clearImuOtherMap) and to MappingModel's edit maps, and extracting
// it is left for a follow-up task (Tarea 3b) to keep this extraction low-risk.
// ---------------------------------------------------------------------------
class MappingSourceSelector {
public:
    static void update(const PadView& phys, const MappingModel& model, MappingSelection& sel,
                        const GamepadState& physNow, float touch1X, float touch1Y,
                        const PadEngine& engine, const std::vector<ControllerConfig>& configs,
                        float stickSelectThreshold, int stickHoldMs,
                        float gyroSelectThreshold, float accelSelectThreshold, float dt);

private:
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
