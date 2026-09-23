#include "MappingSourceSelector.h"
#include "MappingHelpers.h"
#include "../config/ConfigLoader.h"
#include "../input/ComponentTypes.h"  // applyDeadzoneMaxSigned — see H9 gyro/accel hold-to-arm

// Same value as MappingEditor.cpp's kImuConfirmSec — kept local since it's used only by the
// gyro/accel sweep below, not worth sharing via a header for a single constant.
constexpr float kImuConfirmSec = 0.18f;

// ---------------------------------------------------------------------------
void MappingSourceSelector::update(const PadView& phys, const MappingModel& model,
                                    MappingSelection& sel, const GamepadState& physNow,
                                    float touch1X, float touch1Y, const PadEngine& engine,
                                    const std::vector<ControllerConfig>& configs,
                                    float stickSelectThreshold, int stickHoldMs,
                                    float gyroSelectThreshold, float accelSelectThreshold,
                                    float dt) {
    instantRepickGestureOrZone(model, sel, physNow, touch1X, touch1Y);

    if (sel.h9ErrorTimer > 0.0f)
        sel.h9ErrorTimer -= dt;

    if (sel.physComp < 0 && sel.triggerSrc.empty()) {
        // Paso 1a (stick) is checked FIRST, exactly as in the original nested if/else — Paso 1b/1c
        // (armGenericComponentOrTrigger) only runs on frames where no stick is actively tilted.
        bool stickActive = armStickAxis(phys, sel, physNow, stickSelectThreshold, stickHoldMs, dt);
        if (!stickActive)
            armGenericComponentOrTrigger(phys, model, sel, physNow, touch1X, touch1Y, dt);
        armGyroAccelSweep(phys, sel, physNow, engine, configs,
                           gyroSelectThreshold, accelSelectThreshold, dt);
    }
}

// ---------------------------------------------------------------------------
void MappingSourceSelector::instantRepickGestureOrZone(const MappingModel& model,
                                                        MappingSelection& sel,
                                                        const GamepadState& physNow,
                                                        float touch1X, float touch1Y) {
    // Movimiento (Gestos): pick the SPECIFIC gesture while already INSIDE the picker panel —
    // covers every way the touchpad can already be the selected source (mouse click on the
    // touchpad body, a prior gesture that only armed the surface, H9 Paso 1 below from a fresh
    // -1 state, etc.), not just the "nothing selected yet" case the block below handles. Runs
    // unconditionally, outside the `physComp < 0` H9 gate below — that gate exists to arbitrate
    // WHICH physical thing becomes the source when nothing is picked yet; once the touchpad
    // already IS the source, picking a specific gesture is a separate step (the 14-icon grid),
    // and a real gesture should do exactly what clicking an icon does there. Found 2026/08/26:
    // without this, any gesture made while the grid was already open (touchpad pre-selected via
    // mouse) was silently ignored — confirmed with real hardware, dozens of gestures classified
    // correctly by HIDInputSource but never reaching touchGestureSelected.
    if (model.touchSurfaceMode == TouchpadSurfaceMode::Gesture &&
        sel.physComp >= 0 && sel.touchSurfaceSelected &&
        sel.touchGestureSelected.empty() && !physNow.touchGestureFired.empty()) {
        sel.touchGestureSelected = physNow.touchGestureFired;
        sel.actionType = ActionType::Xbox;
        sel.captureKeys.clear(); sel.macroSel.clear(); sel.botSel.clear();
    }
    // Zonas: same gap, same fix — switch to whichever region the finger is currently over,
    // INSTANTLY, while already inside the picker (physComp >= 0, touchSurfaceSelected), mirroring
    // onPhysTouchpadHit's mouse-click behavior (which always re-picks on click, live in
    // TouchZones.h below via hitTestTouchZone). Touch could only ever resolve a region through
    // the physComp<0 hold gate further down, so once a region was already selected — by mouse OR
    // by an earlier touch — touching a DIFFERENT region did nothing (found 2026/08/26, reported
    // with real hardware: touched the top-left region while top-left was already selected from a
    // previous run, top-right did nothing).
    if (model.touchSurfaceMode == TouchpadSurfaceMode::Zones && !model.touchZones.empty() &&
        sel.physComp >= 0 && sel.touchSurfaceSelected && physNow.touch1Active) {
        const TouchZoneRegion* hit = hitTestTouchZone(model.touchZones, touch1X, touch1Y);
        if (hit && hit->id != sel.touchZoneRegionSelected) {
            sel.touchZoneRegionSelected = hit->id;
            sel.actionType = ActionType::Xbox;
            sel.captureKeys.clear(); sel.macroSel.clear(); sel.botSel.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Paso 1a: stick tilted past threshold, held for stickHoldMs -> select that axis. Returns true
// whenever a stick is actively tilted past threshold THIS frame (regardless of whether the hold
// timer has completed yet) — the original code used that same condition
// (`activeStickComp >= 0`) to decide whether Paso 1b/1c (armGenericComponentOrTrigger) runs at
// all this frame; see the call site in update().
bool MappingSourceSelector::armStickAxis(const PadView& phys, MappingSelection& sel,
                                          const GamepadState& physNow,
                                          float stickSelectThreshold, int stickHoldMs, float dt) {
    const auto& physComps = phys.getLayout().components;
    int         activeStickComp = -1;
    std::string activeStickDir;
    for (int i = 0; i < (int)physComps.size(); ++i) {
        const PadComponent& c = physComps[i];
        if (c.type != "stick") continue;
        float x = 0.0f, y = 0.0f;
        readStickXY(physNow, c.stateX, x, y);
        std::string dir;
        if      (y >=  stickSelectThreshold) dir = "up";
        else if (y <= -stickSelectThreshold) dir = "down";
        else if (x <= -stickSelectThreshold) dir = "left";
        else if (x >=  stickSelectThreshold) dir = "right";
        if (!dir.empty()) { activeStickComp = i; activeStickDir = dir; break; }
    }

    if (activeStickComp < 0) return false;

    if (sel.h9HoldComp != activeStickComp || sel.h9HoldStickDir != activeStickDir) {
        sel.h9HoldComp      = activeStickComp;
        sel.h9HoldStickDir  = activeStickDir;
        sel.h9HoldTimer     = 0.0f;
    } else {
        sel.h9HoldTimer += dt;
        if (sel.h9HoldTimer >= stickHoldMs / 1000.0f) {
            sel.physComp      = activeStickComp;
            sel.stickDir      = activeStickDir;
            sel.stickAsButton = false;
            sel.h9HoldComp    = -1;
            sel.h9HoldStickDir.clear();
            sel.h9HoldTimer   = 0.0f;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Paso 1b (button/stick-click/dpad/touchpad held 1s) + Paso 1c (trigger held 2s), nested exactly
// as in the original code: 1c only runs when 1b's scan finds nothing. Only called from update()
// on frames where armStickAxis found no active stick tilt — the first check below (a stale
// stick-hold that just broke) mirrors the original code's own nested handling of that case.
void MappingSourceSelector::armGenericComponentOrTrigger(const PadView& phys,
                                                          const MappingModel& model,
                                                          MappingSelection& sel,
                                                          const GamepadState& physNow,
                                                          float touch1X, float touch1Y, float dt) {
    const auto& physComps = phys.getLayout().components;

    // ── Paso 1b: boton mantenido 1s -> seleccionarlo ── (gyro/accel arming is handled
    // independently below, after this if/else chain — see the per-direction progressive-sweep
    // block in armGyroAccelSweep.)
    if (!sel.h9HoldStickDir.empty()) {
        sel.h9HoldComp = -1;
        sel.h9HoldStickDir.clear();
        sel.h9HoldDpadDir.clear();
        sel.h9HoldTouchZoneRegion.clear();
        sel.h9HoldTimer = 0.0f;
        return;
    }

    int  activeComp          = -1;
    bool activeIsStickBtn    = false;
    bool activeIsTouchSurface = false;
    std::string activeDpadDir;
    std::string activeTouchZoneRegion;
    for (int i = 0; i < (int)physComps.size(); ++i) {
        const PadComponent& c = physComps[i];
        if (c.type == "button" && isStateActive(physNow, c.state)) {
            activeComp = i; activeIsStickBtn = false; break;
        }
        if (c.type == "stick" && !c.stateClick.empty() &&
            isStateActive(physNow, c.stateClick)) {
            activeComp = i; activeIsStickBtn = true; break;
        }
        if (c.type == "dpad") {
            for (const char* d : {"up","down","left","right"}) {
                std::string st = dpadDirToState(c, d);
                if (!st.empty() && isStateActive(physNow, st)) {
                    activeComp = i; activeDpadDir = d; break;
                }
            }
            if (activeComp >= 0) break;
        }
        if (c.type == "touchpad") {
            // Boton (physical click, c.state == "btnTouch") takes priority over Superficie
            // (just touching, touch1Active) — a real click implies the finger is already
            // touching, so a firmer press is the more deliberate signal. See
            // MappingEditor.cpp's onPhysTouchpadHit for the mouse-click equivalent of this same
            // left/right-half split.
            if (isStateActive(physNow, c.state)) {
                activeComp = i; activeIsTouchSurface = false; break;
            }
            // Movimiento (Gestos): a recognized gesture commits INSTANTLY here, fully (physComp +
            // touchSurfaceSelected + touchGestureSelected all at once) instead of going through
            // the activeComp/h9HoldTimer arm-then-wait dance below. Two reasons: (1) the
            // classifier already requires a deliberate minimum travel distance before firing at
            // all, a stronger intent filter than "sat still for 1s"; (2) the 6 two-finger
            // gestures only ever fire as a single-frame pulse AT RELEASE — by then
            // physNow.touch1Active is already false, so they could never satisfy a hold gate
            // anyway. Must NOT just set touchGestureSelected alone and leave physComp untouched —
            // that was tried first and found to be a real bug with real hardware (2026/08/26):
            // the generic touch1Active branch right below kept running in parallel (nothing had
            // closed the outer physComp<0 guard), so its own independent 1s hold could commit
            // LATER and pop the panel open showing whatever gesture id happened to be sitting in
            // touchGestureSelected at that point — not necessarily the one just performed.
            // Setting physComp here closes that guard immediately, so the branch below never
            // gets a chance to race this.
            if (model.touchSurfaceMode == TouchpadSurfaceMode::Gesture &&
                sel.touchGestureSelected.empty() &&
                !physNow.touchGestureFired.empty()) {
                sel.physComp = i;
                sel.touchSurfaceSelected = true;
                sel.touchGestureSelected = physNow.touchGestureFired;
                sel.actionType = ActionType::Xbox;
                sel.captureKeys.clear(); sel.macroSel.clear(); sel.botSel.clear();
                sel.h9HoldComp = -1; sel.h9HoldDpadDir.clear();
                sel.h9HoldTouchZoneRegion.clear(); sel.h9HoldTimer = 0.0f;
                break;
            }
            if (physNow.touch1Active) {
                // Zonas: resolve which region the finger is over, same hit-test
                // onPhysTouchpadHit uses for the mouse-click path — a touch outside every region
                // (shouldn't happen for a full-coverage template) counts as no touch for
                // hold-selection purposes.
                if (model.touchSurfaceMode == TouchpadSurfaceMode::Zones &&
                    !model.touchZones.empty()) {
                    const TouchZoneRegion* hit =
                        hitTestTouchZone(model.touchZones, touch1X, touch1Y);
                    if (hit) {
                        activeComp = i; activeIsTouchSurface = true;
                        activeTouchZoneRegion = hit->id; break;
                    }
                } else {
                    activeComp = i; activeIsTouchSurface = true; break;
                }
            }
        }
    }
    if (activeComp >= 0) {
        if (sel.h9HoldComp != activeComp) {
            sel.h9HoldComp    = activeComp;
            sel.h9HoldDpadDir = activeDpadDir;
            sel.h9HoldTouchZoneRegion = activeTouchZoneRegion;
            sel.h9HoldTimer   = 0.0f;
        } else {
            sel.h9HoldDpadDir = activeDpadDir;
            sel.h9HoldTouchZoneRegion = activeTouchZoneRegion;
            sel.h9HoldTimer += dt;
            if (sel.h9HoldTimer >= 1.0f) {
                sel.physComp      = activeComp;
                sel.stickAsButton = activeIsStickBtn;
                sel.dpadDir        = activeDpadDir;
                sel.touchSurfaceSelected = activeIsTouchSurface;
                sel.touchZoneRegionSelected = activeTouchZoneRegion;
                sel.actionType    = ActionType::Xbox;
                sel.h9HoldComp    = -1;
                sel.h9HoldDpadDir.clear();
                sel.h9HoldTouchZoneRegion.clear();
                sel.h9HoldTimer   = 0.0f;
            }
        }
        return;
    }

    sel.h9HoldComp    = -1;
    sel.h9HoldDpadDir.clear();
    sel.h9HoldTouchZoneRegion.clear();
    sel.h9HoldTimer   = 0.0f;
    // ── Paso 1c: gatillo al tope 2s -> seleccionar como fuente ──
    constexpr float kTrigSelThresh = 0.75f;
    if (physNow.triggerL > kTrigSelThresh || physNow.triggerR > kTrigSelThresh) {
        std::string tSrc = (physNow.triggerL >= physNow.triggerR) ? "l2" : "r2";
        if (sel.h9HoldTriggerSrc != tSrc) {
            sel.h9HoldTriggerSrc   = tSrc;
            sel.h9HoldTriggerTimer = 0.0f;
        } else {
            sel.h9HoldTriggerTimer += dt;
            if (sel.h9HoldTriggerTimer >= 2.0f) {
                sel.triggerSrc         = tSrc;
                sel.actionType         = ActionType::Xbox;
                sel.captureKeys.clear();
                sel.macroSel.clear();
                sel.botSel.clear();
                sel.h9HoldTriggerSrc.clear();
                sel.h9HoldTriggerTimer = 0.0f;
            }
        }
    } else {
        sel.h9HoldTriggerSrc.clear();
        sel.h9HoldTriggerTimer = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Paso 1a-bis: gyro/accel progressive sweep from rest to a sustained extreme -> select that
// direction. Runs unconditionally alongside 1a/1b/1c (never competes with them for the same
// candidate slot) — see MappingSelection.h's ImuSweepState/advanceImuSweep for the rationale.
void MappingSourceSelector::armGyroAccelSweep(const PadView& phys, MappingSelection& sel,
                                              const GamepadState& physNow, const PadEngine& engine,
                                              const std::vector<ControllerConfig>& configs,
                                              float gyroSelectThreshold, float accelSelectThreshold,
                                              float dt) {
    const auto& physComps = phys.getLayout().components;
    int gyroCompIdx = -1;
    for (int i = 0; i < (int)physComps.size(); ++i)
        if (physComps[i].type == "gyro") { gyroCompIdx = i; break; }

    if (gyroCompIdx >= 0 && sel.physComp < 0) {
        // Accel's rest threshold is higher than gyro's: just holding the controller normally (to
        // be able to rotate it at all) already tilts it back a fair amount, and that alone
        // shouldn't count as "left rest". Gyro (angular velocity) doesn't have this problem — it
        // reads ~0 whenever the controller isn't actively rotating, static tilt included, so its
        // rest threshold stays low.
        constexpr float kImuAccelRestThresh = 0.45f;
        constexpr float kImuGyroRestThresh  = 0.20f;
        float restThresh[6] = { kImuAccelRestThresh, kImuAccelRestThresh,
                                 kImuAccelRestThresh, kImuAccelRestThresh,
                                 kImuGyroRestThresh,  kImuGyroRestThresh };
        static const char* kImuDirs[6] = { "up", "down", "left", "right", "cw", "ccw" };

        // Accel-only: getting the controller into position to rotate it tilts it back in a
        // single continuous motion that usually overshoots close to the sensor's calibrated
        // ceiling — a deliberate "hold this tilt" calibration gesture rarely does, since there's
        // no reason to push all the way to the mechanical/sensor limit just to hold a cardinal
        // direction. So: if an ascent ever touches near-max, it's disqualified as "positioning"
        // (not armed) until the axis returns to rest; only an ascent that stays in the
        // [armThresh, nearMax) band for the whole confirm window counts as a deliberate hold.
        constexpr float kImuNearMaxFrac = 0.90f;

        // gyroSelectThreshold/accelSelectThreshold are calibrated-space constants, but physNow
        // carries the RAW pre-calibration reading — shape it through the active device's own
        // per-axis deadzone/max first (see the identical reasoning that used to live here for
        // the old design).
        DeviceCandidate dev = engine.getActiveDevice();
        const ControllerConfig* activeCfg =
            findConfig(configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);
        float accelX = physNow.accelX, accelY = physNow.accelY, gyroY = physNow.gyroY;
        if (activeCfg) {
            const auto& imu = activeCfg->imu;
            accelX = applyDeadzoneMaxSigned(accelX, imu.accelXDeadzone, imu.accelXMax);
            accelY = applyDeadzoneMaxSigned(accelY, imu.accelYDeadzone, imu.accelYMax);
            gyroY  = applyDeadzoneMaxSigned(gyroY,  imu.gyroYDeadzone,  imu.gyroYMax);
        }

        // Magnitude in the direction of travel (can be negative — that's fine, the rest/arm
        // comparisons below treat it as "not there yet") for each of the 6 directions, paired
        // with the threshold that counts as "reached the extreme".
        float mags[6]      = { accelY, -accelY, -accelX, accelX, gyroY, -gyroY };
        float armThresh[6] = { accelSelectThreshold, accelSelectThreshold,
                                accelSelectThreshold, accelSelectThreshold,
                                gyroSelectThreshold,  gyroSelectThreshold };

        // Core state transition lives in advanceImuSweep() (MappingSelection.h) so it can run
        // under Catch2 without pulling in D3D11/HWND/PadEngine — see
        // PadsWayTests/tests/test_MappingSelection.cpp.
        ImuSweepResult sweep = advanceImuSweep(sel.h9ImuSweep, mags, restThresh,
                                               armThresh, kImuNearMaxFrac,
                                               kImuConfirmSec, dt);
        int   armedIdx     = sweep.armedIdx;
        int   bestDisplay  = sweep.bestDisplay;
        float bestProgress = sweep.bestProgress;

        if (armedIdx >= 0) {
            sel.physComp      = gyroCompIdx;
            sel.stickDir      = kImuDirs[armedIdx];
            sel.stickAsButton = false;
            sel.actionType    = ActionType::Xbox;
            sel.imuUseAccel = false; sel.imuSourceOverridden = false;
            sel.h9ImuSweep   = {};  // force a fresh rest-to-max sweep for the next gesture
            sel.h9HoldGyroDir.clear();
            sel.h9HoldGyroTimer = 0.0f;
        } else if (bestDisplay >= 0) {
            sel.h9HoldGyroDir   = kImuDirs[bestDisplay];
            sel.h9HoldGyroTimer = bestProgress;
        } else {
            sel.h9HoldGyroDir.clear();
            sel.h9HoldGyroTimer = 0.0f;
        }
    } else {
        sel.h9HoldGyroDir.clear();
        sel.h9HoldGyroTimer = 0.0f;
    }
}

// ---------------------------------------------------------------------------
void MappingSourceSelector::assign(const PadView& phys, const PadView& virt, MappingModel& model,
                                    MappingSelection& sel, const GamepadState& physNow,
                                    const std::vector<std::string>& acceptedXbox,
                                    float stickSelectThreshold) {
    if (sel.physComp >= 0 && sel.actionType == ActionType::Xbox) {
        assignNonTriggerTarget(phys, virt, model, sel, physNow, acceptedXbox, stickSelectThreshold);
    } else if (!sel.triggerSrc.empty() && sel.actionType == ActionType::Xbox) {
        assignTriggerTarget(phys, virt, model, sel, physNow, acceptedXbox, stickSelectThreshold);
    }
}

// ---------------------------------------------------------------------------
void MappingSourceSelector::assignNonTriggerTarget(const PadView& phys, const PadView& virt,
                                                    MappingModel& model, MappingSelection& sel,
                                                    const GamepadState& physNow,
                                                    const std::vector<std::string>& acceptedXbox,
                                                    float stickSelectThreshold) {
    const auto& physComps = phys.getLayout().components;
    // Xbox mode: detect rising edge on physical input -> assign virtual button
    const PadComponent& selPhysComp = physComps[sel.physComp];
    // Gyro/accel source: target maps live in gyroActionEdits/accelActionEdits (resolved
    // per-direction via resolveImuTargetMap), never in axisActionEdits — that map is
    // keyed by stick slot ids derived from selPhysComp.stateX, which gyro components
    // don't have. Mirrors the mouse-click path (onVirtHitGyroAction).
    bool isGyroSource = selPhysComp.type == "gyro";
    // Touch zone/gesture source: target maps live in touchZoneActionEdits/
    // touchGestureActionEdits, never in buttonEdits/actionEdits — keyed by region/gesture
    // id (sel.touchZoneRegionSelected/touchGestureSelected), which selPhysComp.state
    // (a "touchpad" component's own state, e.g. btnTouch) has no relation to. Mirrors the
    // mouse-click path (onVirtHitTouchZone/onVirtHitTouchGesture in MappingEditor.cpp).
    // IMPORTANT — see also that comment: every H9 "Paso 2" site below (button/dpad target,
    // trigger target, StickSlot target) needs its OWN touch branch, same as isGyroSource
    // needed one in each when gyro was added (BITACORA.md, 2026/08/05, bugs 1/4/5) — the
    // mouse-click path and this physical-press path are two independent implementations of
    // the same assignment; a new source type must be wired into BOTH or one silently stops
    // working while the other keeps compiling fine.
    bool isTouchZoneSource = sel.touchSurfaceSelected &&
        model.touchSurfaceMode == TouchpadSurfaceMode::Zones &&
        !sel.touchZoneRegionSelected.empty();
    bool isTouchGestureSource = sel.touchSurfaceSelected &&
        model.touchSurfaceMode == TouchpadSurfaceMode::Gesture &&
        !sel.touchGestureSelected.empty();
    // Touch editor's own mode navigator: at the TOP level only (Superficie selected, no
    // specific zone/gesture drilled into yet — the 4 mode buttons are the only thing
    // showing), the A button cycles Ratón -> Analógico -> Gestos -> Zonas -> Ratón instead
    // of falling into the generic Botón-channel assignment below (which would otherwise
    // bind the touchpad's click to A and close the editor — physShort there is always
    // "btnTouch", unrelated to which half was picked). The moment a specific zone/gesture
    // is selected, A goes back to being a normal assignable target — same physical button,
    // different meaning depending on how deep into the touch editor you are. User's idea,
    // 2026/08/27 (see SESSION_CONTEXT.md "selector físico").
    bool isTouchTopLevelNav = sel.touchSurfaceSelected &&
        selPhysComp.type == "touchpad" &&
        sel.touchZoneRegionSelected.empty() && sel.touchGestureSelected.empty();
    std::string selState;
    if (sel.stickAsButton)
        selState = selPhysComp.stateClick;
    else if (selPhysComp.type == "dpad" && !sel.dpadDir.empty())
        selState = dpadDirToState(selPhysComp, sel.dpadDir);
    else
        selState = selPhysComp.state;
    std::string physShort = stateToShort(selState);

    std::vector<std::string> candidateStates;
    for (int i = 0; i < (int)physComps.size(); ++i) {
        const PadComponent& c = physComps[i];
        if (c.type == "button" && !c.state.empty())
            candidateStates.push_back(c.state);
        else if (c.type == "stick" && !c.stateClick.empty())
            candidateStates.push_back(c.stateClick);
        else if (c.type == "dpad") {
            for (const char* d : {"up","down","left","right"}) {
                std::string st = dpadDirToState(c, d);
                if (!st.empty()) candidateStates.push_back(st);
            }
        }
    }
    for (const auto& compState : candidateStates) {
        bool wasActive = isStateActive(sel.h9PrevPhysState, compState);
        bool isActive  = isStateActive(physNow, compState);
        if (!isActive || wasActive) continue;

        std::string virtShort = stateToShort(compState);
        bool valid = false;
        for (const auto& s : acceptedXbox) if (virtShort == s) { valid = true; break; }

        // ── Touch editor mode navigator — see isTouchTopLevelNav above. Checked before
        // the touch zone/gesture and generic Botón paths below, so A never falls into
        // either while at the top level. ──
        if (isTouchTopLevelNav && virtShort == "a") {
            // 5th stop, "Sin asignar" (see BITACORA.md 2026/09/02) — user's explicit call:
            // reachable from the pad alone, not just via the Mapeador's Limpiar button.
            static const TouchpadSurfaceMode kTouchModeCycle[] = {
                TouchpadSurfaceMode::Mouse, TouchpadSurfaceMode::Analog,
                TouchpadSurfaceMode::Gesture, TouchpadSurfaceMode::Zones,
                TouchpadSurfaceMode::Unassigned,
            };
            constexpr int kCycleLen = sizeof(kTouchModeCycle) / sizeof(kTouchModeCycle[0]);
            int cycleIdx = 0;
            for (int k = 0; k < kCycleLen; ++k)
                if (kTouchModeCycle[k] == model.touchSurfaceMode) { cycleIdx = k; break; }
            model.touchSurfaceMode = kTouchModeCycle[(cycleIdx + 1) % kCycleLen];
            if (model.touchSurfaceMode == TouchpadSurfaceMode::Analog &&
                model.touchAnalogStickTarget.empty())
                model.touchAnalogStickTarget = "left";
            break;
        }

        // ── Touch zone/gesture source → VirtualButton target. Mirrors onVirtHitTouchZone/
        // onVirtHitTouchGesture's plain-button branch — see the isTouchZoneSource comment
        // above for why this can't share the buttonEdits/actionEdits path below. ──
        if (isTouchZoneSource || isTouchGestureSource) {
            if (valid) {
                const std::string& srcId = isTouchZoneSource ? sel.touchZoneRegionSelected
                                                              : sel.touchGestureSelected;
                auto& editsMap = isTouchZoneSource ? model.touchZoneActionEdits
                                                    : model.touchGestureActionEdits;
                auto it = editsMap.find(srcId);
                bool already = (it != editsMap.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == virtShort);
                if (already) {
                    editsMap.erase(srcId);
                    sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = srcId; act.name = virtShort;
                    editsMap[srcId] = act;
                    int flashComp = findCompByState(virt.getLayout(), shortToState(virtShort));
                    sel.flashComp      = flashComp;
                    sel.flashTimer     = 0.5f;
                    sel.flashVirtShort = virtShort;
                }
            } else {
                sel.h9ErrorTimer = 2.0f;
            }
            sel.physComp = -1;
            sel.touchSurfaceSelected = false;
            sel.touchZoneRegionSelected.clear();
            sel.touchGestureSelected.clear();
            sel.actionType = ActionType::Xbox;
            break;
        }

        // ── Axis-action mode: stick/gyro direction seleccionada → asignar VirtualButton/Dpad ──
        if (!sel.stickDir.empty() && isGyroSource) {
            bool isDpad9 = (virtShort.rfind("dpad_", 0) == 0);
            if (valid || isDpad9) {
                HalfAxisAction ha;
                if (isDpad9) {
                    ha.type = HalfAxisActionType::Dpad;
                    ha.target = virtShort.substr(5); // "up"/"down"/"left"/"right"
                } else {
                    ha.type = HalfAxisActionType::VirtualButton;
                    ha.target = virtShort;
                }
                std::string key;
                auto& map = resolveImuTargetMap(sel, model, sel.stickDir, key);
                auto it = map.find(key);
                bool alreadyAssigned = (it != map.end() && it->second.type == ha.type &&
                                         it->second.target == ha.target);
                if (alreadyAssigned) {
                    map.erase(key);
                    sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                } else {
                    clearImuOtherMap(model, sel.stickDir, &map == &model.accelActionEdits);
                    map[key] = ha;
                    if (!isDpad9) {
                        int fc = findCompByState(virt.getLayout(), shortToState(virtShort));
                        sel.flashComp = fc; sel.flashTimer = 0.5f;
                        sel.flashVirtShort = shortToState(virtShort);
                    } else {
                        sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                    }
                }
                sel.flashPhysArrowComp = sel.physComp;
                sel.flashPhysArrowDir  = sel.stickDir;
                if (sel.flashTimer < 1.0f) sel.flashTimer = 1.0f;
                sel.physComp = -1; sel.stickDir.clear();
                sel.stickAsButton = false; sel.actionType = ActionType::Xbox;
            } else {
                sel.h9ErrorTimer = 2.0f;
            }
            break;
        } else if (!sel.stickDir.empty()) {
            auto [xId, yId] = stickIdsFromStateX(selPhysComp.stateX);
            std::string axisKey;
            if      (sel.stickDir == "up")    axisKey = yId + "_pos";
            else if (sel.stickDir == "down")  axisKey = yId + "_neg";
            else if (sel.stickDir == "right") axisKey = xId + "_pos";
            else if (sel.stickDir == "left")  axisKey = xId + "_neg";
            bool isDpad9 = (virtShort.rfind("dpad_", 0) == 0);
            if ((valid || isDpad9) && !axisKey.empty()) {
                HalfAxisAction axisAction;
                if (isDpad9) {
                    axisAction.type = HalfAxisActionType::Dpad;
                    axisAction.target = virtShort.substr(5); // "up"/"down"/"left"/"right"
                } else {
                    axisAction.type = HalfAxisActionType::VirtualButton;
                    axisAction.target = virtShort;
                }
                auto axisEditIt = model.axisActionEdits.find(axisKey);
                bool alreadyAssigned = (axisEditIt != model.axisActionEdits.end() &&
                                 axisEditIt->second.type == axisAction.type &&
                                 axisEditIt->second.target == axisAction.target);
                if (alreadyAssigned) {
                    model.axisActionEdits.erase(axisKey);
                    sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                } else {
                    model.axisActionEdits[axisKey] = axisAction;
                    if (!isDpad9) {
                        int fc = findCompByState(virt.getLayout(), shortToState(virtShort));
                        sel.flashComp = fc; sel.flashTimer = 0.5f;
                        sel.flashVirtShort = shortToState(virtShort);
                    } else {
                        sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                    }
                }
                sel.flashPhysArrowComp = sel.physComp;
                sel.flashPhysArrowDir  = sel.stickDir;
                if (sel.flashTimer < 1.0f) sel.flashTimer = 1.0f;
                sel.physComp = -1; sel.stickDir.clear();
                sel.stickAsButton = false; sel.actionType = ActionType::Xbox;
            } else {
                sel.h9ErrorTimer = 2.0f;
            }
            break;
        }

        if (valid) {
            if (!physShort.empty()) {
                model.actionEdits.erase(physShort);
                auto it = model.buttonEdits.find(physShort);
                bool alreadyAssigned = (it != model.buttonEdits.end() && it->second == virtShort);
                model.buttonEdits[physShort] = alreadyAssigned ? "" : virtShort;
                int flashComp = findCompByState(virt.getLayout(), shortToState(virtShort));
                sel.flashComp      = alreadyAssigned ? -1 : flashComp;
                sel.flashTimer     = alreadyAssigned ? 0.0f : 0.5f;
                sel.flashVirtShort = alreadyAssigned ? "" : virtShort;
            }
            sel.physComp    = -1;
            sel.stickAsButton = false;
            sel.dpadDir.clear();
            sel.actionType = ActionType::Xbox;
        } else {
            bool hasAssignment = model.actionEdits.count(physShort) > 0 ||
                (model.buttonEdits.count(physShort) && !model.buttonEdits.at(physShort).empty());
            if (hasAssignment && !physShort.empty()) {
                model.buttonEdits[physShort] = "";
                model.actionEdits.erase(physShort);
                sel.physComp    = -1;
                sel.stickAsButton = false;
                sel.dpadDir.clear();
                sel.actionType = ActionType::Xbox;
            } else {
                sel.h9ErrorTimer = 2.0f;
            }
        }
        break;
    }

    // Physical L2/R2 → asignar componente seleccionado como gatillo virtual
    {
        constexpr float kTrigThresh = 0.5f;
        auto doAxisTrigAssign = [&](const std::string& trigTarget, const std::string& trigState) {
            if (isGyroSource) {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Trigger; ha.target = trigTarget;
                std::string key;
                auto& map = resolveImuTargetMap(sel, model, sel.stickDir, key);
                auto it = map.find(key);
                bool already = (it != map.end() && it->second.type == ha.type &&
                                it->second.target == ha.target);
                if (already) {
                    map.erase(key);
                    sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                } else {
                    clearImuOtherMap(model, sel.stickDir, &map == &model.accelActionEdits);
                    map[key] = ha;
                    sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                    sel.flashTimer     = 1.0f;
                    sel.flashVirtShort = trigState;
                    sel.flashPhysArrowComp = sel.physComp;
                    sel.flashPhysArrowDir  = sel.stickDir;
                }
                sel.physComp = -1; sel.stickDir.clear();
                sel.stickAsButton = false; sel.actionType = ActionType::Xbox;
                return;
            }
            auto [xId, yId] = stickIdsFromStateX(selPhysComp.stateX);
            std::string axisKey;
            if      (sel.stickDir == "up")    axisKey = yId + "_pos";
            else if (sel.stickDir == "down")  axisKey = yId + "_neg";
            else if (sel.stickDir == "right") axisKey = xId + "_pos";
            else if (sel.stickDir == "left")  axisKey = xId + "_neg";
            if (axisKey.empty()) return;
            HalfAxisAction axisAction;
            axisAction.type = HalfAxisActionType::Trigger;
            axisAction.target = trigTarget;
            auto axisEditIt = model.axisActionEdits.find(axisKey);
            bool alreadyAssigned = (axisEditIt != model.axisActionEdits.end() &&
                             axisEditIt->second.type == axisAction.type &&
                             axisEditIt->second.target == axisAction.target);
            if (alreadyAssigned) {
                model.axisActionEdits.erase(axisKey);
                sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
            } else {
                model.axisActionEdits[axisKey] = axisAction;
                sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                sel.flashTimer     = 1.0f;
                sel.flashVirtShort = trigState;
                sel.flashPhysArrowComp = sel.physComp;
                sel.flashPhysArrowDir  = sel.stickDir;
            }
            sel.physComp = -1; sel.stickDir.clear();
            sel.stickAsButton = false; sel.actionType = ActionType::Xbox;
        };
        auto doTrigAssign = [&](const std::string& trigTarget, const std::string& trigState) {
            // Touch zone/gesture source → Trigger target. Mirrors onVirtHitTouchZone/
            // onVirtHitTouchGesture's trigger branch — see isTouchZoneSource's comment
            // above; physShort is meaningless for a touchpad component, so this must be
            // handled before the physShort.empty() early-return below.
            if (isTouchZoneSource || isTouchGestureSource) {
                const std::string& srcId = isTouchZoneSource ? sel.touchZoneRegionSelected
                                                              : sel.touchGestureSelected;
                auto& editsMap = isTouchZoneSource ? model.touchZoneActionEdits
                                                    : model.touchGestureActionEdits;
                auto it = editsMap.find(srcId);
                bool already = (it != editsMap.end() &&
                                it->second.type == ButtonActionType::Trigger &&
                                it->second.target == trigTarget);
                if (already) {
                    editsMap.erase(srcId);
                    sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::Trigger; act.physical = srcId; act.target = trigTarget;
                    editsMap[srcId] = act;
                    sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                    sel.flashTimer     = 0.5f;
                    sel.flashVirtShort = trigState;
                }
                sel.physComp = -1;
                sel.touchSurfaceSelected = false;
                sel.touchZoneRegionSelected.clear();
                sel.touchGestureSelected.clear();
                sel.actionType = ActionType::Xbox;
                return;
            }
            if (physShort.empty()) return;
            auto trigAssignIt = model.actionEdits.find(physShort);
            bool already = (trigAssignIt != model.actionEdits.end() &&
                            trigAssignIt->second.type == ButtonActionType::Trigger &&
                            trigAssignIt->second.target == trigTarget);
            if (already) {
                model.actionEdits.erase(physShort);
                sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
            } else {
                ButtonAction act;
                act.type = ButtonActionType::Trigger; act.physical = physShort; act.target = trigTarget;
                model.actionEdits[physShort] = act;
                model.buttonEdits.erase(physShort);
                sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                sel.flashTimer     = 0.5f;
                sel.flashVirtShort = trigState;
            }
            sel.physComp = -1; sel.stickAsButton = false;
            sel.dpadDir.clear(); sel.actionType = ActionType::Xbox;
        };
        if (physNow.triggerL > kTrigThresh && sel.h9PrevPhysState.triggerL <= kTrigThresh) {
            if (!sel.stickDir.empty()) doAxisTrigAssign("l2", "triggerL");
            else doTrigAssign("l2", "triggerL");
        } else if (physNow.triggerR > kTrigThresh && sel.h9PrevPhysState.triggerR <= kTrigThresh) {
            if (!sel.stickDir.empty()) doAxisTrigAssign("r2", "triggerR");
            else doTrigAssign("r2", "triggerR");
        }
    }

    // Analog stick tilt → assign source to a stick slot destination.
    // physShort is empty for stick and touch zone/gesture sources (state field unused for
    // either), so also check stickDir/isTouchZoneSource/isTouchGestureSource.
    if (sel.physComp >= 0 && (!physShort.empty() || !sel.stickDir.empty() ||
                                 isTouchZoneSource || isTouchGestureSource)) {
        const auto& virtComps = virt.getLayout().components;
        for (int i = 0; i < (int)virtComps.size(); ++i) {
            if (virtComps[i].type != "stick") continue;
            // Rising-edge: require stick to have been below threshold last frame.
            // Prevents immediate fire when physComp is set while a stick is held.
            float prevSx = 0.0f, prevSy = 0.0f;
            readStickXY(sel.h9PrevPhysState, virtComps[i].stateX, prevSx, prevSy);
            if (prevSx >=  stickSelectThreshold || prevSx <= -stickSelectThreshold ||
                prevSy >=  stickSelectThreshold || prevSy <= -stickSelectThreshold) continue;
            float sx = 0.0f, sy = 0.0f;
            readStickXY(physNow, virtComps[i].stateX, sx, sy);
            std::string slotDir;
            if      (sy >=  stickSelectThreshold) slotDir = "up";
            else if (sy <= -stickSelectThreshold) slotDir = "down";
            else if (sx >=  stickSelectThreshold) slotDir = "right";
            else if (sx <= -stickSelectThreshold) slotDir = "left";
            if (slotDir.empty()) continue;

            auto [vxId, vyId] = stickIdsFromStateX(virtComps[i].stateX);
            std::string slotKey;
            if      (slotDir == "up")    slotKey = vyId + "_pos";
            else if (slotDir == "down")  slotKey = vyId + "_neg";
            else if (slotDir == "right") slotKey = vxId + "_pos";
            else if (slotDir == "left")  slotKey = vxId + "_neg";

            if (!sel.stickDir.empty() && isGyroSource) {
                // Gyro/accel source: assign StickSlot target via resolveImuTargetMap.
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                std::string key;
                auto& map = resolveImuTargetMap(sel, model, sel.stickDir, key);
                auto it = map.find(key);
                bool alreadyAssigned = (it != map.end() && it->second.type == ha.type &&
                                         it->second.target == ha.target);
                if (alreadyAssigned) {
                    map.erase(key);
                    sel.flashSlotKey.clear(); sel.flashTimer = 0.0f; sel.flashComp = -1;
                } else {
                    clearImuOtherMap(model, sel.stickDir, &map == &model.accelActionEdits);
                    map[key] = ha;
                    sel.flashSlotKey = slotKey; sel.flashTimer = 1.0f; sel.flashComp = -1;
                    sel.flashPhysArrowComp = sel.physComp;
                    sel.flashPhysArrowDir  = sel.stickDir;
                }
                sel.physComp = -1; sel.stickDir.clear();
                sel.stickAsButton = false; sel.actionType = ActionType::Xbox;
            } else if (!sel.stickDir.empty()) {
                // Axis-action source: assign StickSlot target.
                auto [xId, yId] = stickIdsFromStateX(selPhysComp.stateX);
                std::string axisKey;
                if      (sel.stickDir == "up")    axisKey = yId + "_pos";
                else if (sel.stickDir == "down")  axisKey = yId + "_neg";
                else if (sel.stickDir == "right") axisKey = xId + "_pos";
                else if (sel.stickDir == "left")  axisKey = xId + "_neg";
                if (!axisKey.empty()) {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                    auto axisEditIt = model.axisActionEdits.find(axisKey);
                    bool alreadyAssigned = (axisEditIt != model.axisActionEdits.end() &&
                                     axisEditIt->second.type == ha.type &&
                                     axisEditIt->second.target == ha.target);
                    if (alreadyAssigned) {
                        model.axisActionEdits.erase(axisKey);
                        sel.flashSlotKey.clear(); sel.flashTimer = 0.0f; sel.flashComp = -1;
                    } else {
                        model.axisActionEdits[axisKey] = ha;
                        sel.flashSlotKey = slotKey; sel.flashTimer = 1.0f; sel.flashComp = -1;
                        sel.flashPhysArrowComp = sel.physComp;
                        sel.flashPhysArrowDir  = sel.stickDir;
                    }
                    sel.physComp = -1; sel.stickDir.clear();
                    sel.stickAsButton = false; sel.actionType = ActionType::Xbox;
                }
            } else if (isTouchZoneSource || isTouchGestureSource) {
                // Touch zone/gesture source → StickSlot target. Mirrors
                // onVirtHitTouchZone/onVirtHitTouchGesture's arrow-hit branch — see
                // isTouchZoneSource's comment above.
                const std::string& srcId = isTouchZoneSource ? sel.touchZoneRegionSelected
                                                              : sel.touchGestureSelected;
                auto& editsMap = isTouchZoneSource ? model.touchZoneActionEdits
                                                    : model.touchGestureActionEdits;
                auto it = editsMap.find(srcId);
                bool already = (it != editsMap.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == slotKey);
                if (already) {
                    editsMap.erase(srcId);
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = srcId; act.name = slotKey;
                    editsMap[srcId] = act;
                }
                sel.physComp = -1;
                sel.touchSurfaceSelected = false;
                sel.touchZoneRegionSelected.clear();
                sel.touchGestureSelected.clear();
                sel.actionType = ActionType::Xbox;
            } else {
                auto it = model.buttonEdits.find(physShort);
                if (it != model.buttonEdits.end() && it->second == slotKey) {
                    model.buttonEdits.erase(physShort);
                } else {
                    model.actionEdits.erase(physShort);
                    model.buttonEdits[physShort] = slotKey;
                }
                sel.physComp = -1; sel.stickAsButton = false;
                sel.dpadDir.clear(); sel.actionType = ActionType::Xbox;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
void MappingSourceSelector::assignTriggerTarget(const PadView& phys, const PadView& virt,
                                                 MappingModel& model, MappingSelection& sel,
                                                 const GamepadState& physNow,
                                                 const std::vector<std::string>& acceptedXbox,
                                                 float stickSelectThreshold) {
    const auto& physComps = phys.getLayout().components;
    // Paso 2 — gatillo como fuente: asignar target por botón/gatillo físico
    std::vector<std::string> candStates;
    for (int i = 0; i < (int)physComps.size(); ++i) {
        const PadComponent& c = physComps[i];
        if (c.type == "button" && !c.state.empty())
            candStates.push_back(c.state);
        else if (c.type == "stick" && !c.stateClick.empty())
            candStates.push_back(c.stateClick);
        else if (c.type == "dpad") {
            for (const char* d : {"up","down","left","right"}) {
                std::string st = dpadDirToState(c, d);
                if (!st.empty()) candStates.push_back(st);
            }
        }
    }
    for (const auto& cState : candStates) {
        if (!isStateActive(physNow, cState) || isStateActive(sel.h9PrevPhysState, cState)) continue;
        std::string vShort = stateToShort(cState);
        bool valid = false;
        for (const auto& s : acceptedXbox) if (vShort == s) { valid = true; break; }
        if (!valid) { sel.h9ErrorTimer = 2.0f; break; }
        auto it = model.trigActionEdits.find(sel.triggerSrc);
        bool already = (it != model.trigActionEdits.end() &&
                        it->second.type == ButtonActionType::VirtualButton &&
                        it->second.name == vShort);
        if (already) {
            model.trigActionEdits.erase(sel.triggerSrc);
            sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
        } else {
            ButtonAction act;
            act.type = ButtonActionType::VirtualButton; act.physical = sel.triggerSrc; act.name = vShort;
            model.trigActionEdits[sel.triggerSrc] = act;
            sel.flashComp = findCompByState(virt.getLayout(), shortToState(vShort));
            sel.flashTimer = 0.5f; sel.flashVirtShort = shortToState(vShort);
        }
        sel.triggerSrc.clear(); sel.actionType = ActionType::Xbox;
        break;
    }
    // Virtual stick tilt → assign trigger source to a stick slot.
    if (!sel.triggerSrc.empty()) {
        const auto& virtComps = virt.getLayout().components;
        for (int i = 0; i < (int)virtComps.size(); ++i) {
            if (virtComps[i].type != "stick") continue;
            float sx = 0.0f, sy = 0.0f;
            readStickXY(physNow, virtComps[i].stateX, sx, sy);
            std::string slotDir;
            if      (sy >=  stickSelectThreshold) slotDir = "up";
            else if (sy <= -stickSelectThreshold) slotDir = "down";
            else if (sx >=  stickSelectThreshold) slotDir = "right";
            else if (sx <= -stickSelectThreshold) slotDir = "left";
            if (slotDir.empty()) continue;

            auto [vxId, vyId] = stickIdsFromStateX(virtComps[i].stateX);
            std::string slotKey;
            if      (slotDir == "up")    slotKey = vyId + "_pos";
            else if (slotDir == "down")  slotKey = vyId + "_neg";
            else if (slotDir == "right") slotKey = vxId + "_pos";
            else if (slotDir == "left")  slotKey = vxId + "_neg";

            auto it = model.trigActionEdits.find(sel.triggerSrc);
            bool already = (it != model.trigActionEdits.end() &&
                            it->second.type == ButtonActionType::VirtualButton &&
                            it->second.name == slotKey);
            if (already) {
                model.trigActionEdits.erase(sel.triggerSrc);
            } else {
                auto& ranges = (sel.triggerSrc == "l2") ? model.trigLRangeEdits
                                                           : model.trigRRangeEdits;
                ranges.clear();
                ButtonAction act;
                act.type = ButtonActionType::VirtualButton;
                act.name = slotKey;
                model.trigActionEdits[sel.triggerSrc] = act;
            }
            sel.triggerSrc.clear(); sel.actionType = ActionType::Xbox;
            break;
        }
    }

    // Trigger press: rising edge → TriggerPassthrough target
    {
        constexpr float kTrigThresh2 = 0.5f;
        auto doTrigTgtAssign = [&](const std::string& trigTarget, const std::string& trigState) {
            auto it = model.trigActionEdits.find(sel.triggerSrc);
            bool already = (it != model.trigActionEdits.end() &&
                            it->second.type == ButtonActionType::TriggerPassthrough &&
                            it->second.target == trigTarget);
            if (already) {
                model.trigActionEdits.erase(sel.triggerSrc);
                sel.flashComp = -1; sel.flashTimer = 0.0f; sel.flashVirtShort.clear();
            } else {
                ButtonAction act;
                act.type = ButtonActionType::TriggerPassthrough; act.physical = sel.triggerSrc;
                act.target = trigTarget;
                model.trigActionEdits[sel.triggerSrc] = act;
                sel.flashComp = findCompByState(virt.getLayout(), trigState);
                sel.flashTimer = 0.5f; sel.flashVirtShort = trigState;
            }
            sel.triggerSrc.clear(); sel.actionType = ActionType::Xbox;
        };
        if (!sel.triggerSrc.empty()) {
            if (physNow.triggerL > kTrigThresh2 && sel.h9PrevPhysState.triggerL <= kTrigThresh2)
                doTrigTgtAssign("l2", "triggerL");
            else if (physNow.triggerR > kTrigThresh2 && sel.h9PrevPhysState.triggerR <= kTrigThresh2)
                doTrigTgtAssign("r2", "triggerR");
        }
    }
}
