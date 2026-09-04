#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include "PadView.h"
#include "PadLayout.h"
#include "../input/HIDScanner.h"
#include "../input/RawHIDReader.h"
#include "../imgui/imgui.h"

// Guides the user through binding a physical controller to a layout.
// The wizard replaces the normal 3-panel editor while it is active.
// Call start() to launch, then render() every frame until isActive() returns false.
class BindingWizard {
public:
    void init(ID3D11Device* device,
              const std::string& controllersPath,
              const std::string& stateMapPath);
    void unload();

    // Launch the wizard for the given layout.
    void start(const PadLayout& layout);

    bool isActive() const { return m_state != State::Idle; }

    // Returns true (once) after saveResult() has written controllers.json.
    bool pollSaved() { bool v = m_savedFlag; m_savedFlag = false; return v; }

    // Call every frame while isActive(). Renders the full wizard UI.
    void render();

private:
    // ── State machine ────────────────────────────────────────────────────────
    enum class State {
        Idle,
        SelectController,   // pick connected controller
        NameController,     // edit name + mode toggle
        WarnNoState,        // warn about components with no state entry in state_map
        Binding,            // main binding loop
        Review,             // show all bindings, confirm or restart
    };

    // Sub-phases of the "gyro" component's calibration step (BindStep.mapping.type == "gyro").
    // Roll/Pitch/Yaw: one phase per axis — see REFERENCE.md, "Wizard de calibracion IMU - 4
    // diseno de classifyGyro(): parar-y-contar + voto". Flip: a one-shot "turn the controller
    // upside down" hold right after Baseline that confirms/corrects the normal (accelZ) offset
    // by sign flip before Roll/Pitch/Yaw run, so that offset is claimed and out of the candidate
    // pool instead of relying only on which baseline reading happened to be the biggest — see
    // confirmNormalOffsetFromFlip(). Treated as a plain sequential index elsewhere (+-1 to
    // advance/step back), so the declaration order here IS the wizard order.
    enum class GyroPhase { Baseline, Flip, Roll, Pitch, Yaw };

    // Sub-phases of the "touch_surface" step's discovery sub-machine (BindStep.mapping.type ==
    // "touch_surface"). Same gesture-guided philosophy as the gyro phases above, adapted to the
    // shape of touch data (an activity bit + packed coordinates, not a continuous noisy signal),
    // so there is no round/vote machinery here — see ARCHITECTURE.md, "Wizard — descubrimiento
    // crudo", for the algorithm this implements. Treated as a plain sequential index elsewhere
    // (+-1 to advance/step back), same convention as GyroPhase.
    enum class TouchPhase { Lift, Confirm, RangeX, RangeY };

    // Sub-state within a Roll/Pitch/Yaw phase: move to one extreme of the axis, hold still
    // (auto-detected rest), move to the opposite extreme, hold still again. One round = one full
    // MoveToA->HoldA->MoveToB->HoldB cycle; see updateGyroRound()/finishGyroRound().
    enum class GyroRoundStage { MoveToA, HoldA, MoveToB, HoldB };

    // ── Internal data types ──────────────────────────────────────────────────
    struct DetectedController {
        WORD        vid            = 0;
        WORD        pid            = 0;
        std::string name;
        std::string productName;
        std::string connectionType; // "usb" / "bt" / ""
        std::string path;
    };

    struct StateMapEntry {
        std::string physical;           // "a", "l1", "lp" …
        std::string type;               // button | physical_only | trigger | axis | dpad
        std::string axis_target;        // left_x | trigger_l …
        std::string prompt;             // shown to user during capture
        bool        invert_if_positive = false;
        std::string direction;          // dpad: up | down | left | right
    };

    // One step in the binding sequence
    struct BindStep {
        int         compIndex  = -1;    // index in m_layout.components (-1 = dpad group)
        std::string state;              // component state value
        StateMapEntry mapping;
    };

    // Captured results
    struct ButtonResult {
        int         compIndex;
        int         physIndex;          // 1-based HID/WinMM button index
        std::string physical;
        bool        physicalOnly;
    };

    struct AxisResult {
        std::string source;             // "hid_x" / "dwXpos"
        std::string target;             // "left_x" / "trigger_l"
        bool        invert;
        bool        isAnalogDpad = false; // axis belongs to an analog_dpad component
    };

    // Running min/max/sum for one candidate raw-report byte offset during one gyro phase.
    struct GyroOffsetStats {
        float sum   = 0.0f;
        float minV  = 0.0f;
        float maxV  = 0.0f;
        int   count = 0;
    };

    // Classified result of the gyro/accel calibration sub-machine. -1 = axis not found
    // (either the device has no accelerometer, like the DS4, or classification failed).
    struct ImuCalibrationResult {
        bool ok           = false;
        int  gyroXOffset  = -1;  // pitch
        int  gyroYOffset  = -1;  // yaw
        int  gyroZOffset  = -1;  // roll
        int  accelXOffset = -1;  // lateral
        int  accelYOffset = -1;  // frontal
        int  accelZOffset = -1;  // normal (gravity)
        // Sign fixup per axis — see REFERENCE.md, "Inversion de ejes IMU - propuesta".
        bool gyroXInvert  = false;
        bool gyroYInvert  = false;
        bool gyroZInvert  = false;
        bool accelXInvert = false;
        bool accelYInvert = false;
        bool accelZInvert = false;
    };

    // Result of the touch surface discovery sub-machine. dataOffset=-1 = discovery never
    // completed (skipped, or Lift/Confirm failed) — saveResult() leaves the existing "touchpad"
    // JSON block untouched in that case.
    struct TouchSurfaceResult {
        bool ok         = false;
        int  dataOffset = -1;
        int  maxX       = 0;
        int  maxY       = 0;
    };

    // ── Render sub-methods ───────────────────────────────────────────────────
    void renderSelectController();
    void renderNameController();
    void renderWarnNoState();
    void renderBinding();
    void renderReview();
    void renderCanvas(int highlightComp);

    // ── Wizard logic ─────────────────────────────────────────────────────────
    void scanControllers();
    void loadStateMap();
    void buildSteps();
    void beginStep();
    void commitButton(int physIndex);
    void commitAxis(const std::string& source, bool invert);
    void commitDpad(const std::string& dpadType);
    // Advances the gyro sub-machine out of Baseline: runs computeGyroCandidatePool() (the old
    // "which offsets are alive" block search + normal-axis pick, done once) and, if a usable
    // candidate pool was found, moves to GyroPhase::Roll. Roll/Pitch/Yaw are NOT committed by
    // this function — they advance automatically via finishGyroRound() as rounds converge.
    void commitGyroPhase();
    // Accumulates one frame of raw-byte samples into m_gyroSamples for the current gyro phase
    // (used later by classifyGyro()'s cross-axis amplitude scoring). Rejects frames where a
    // declared HID axis (stick/trigger) drifted past kGyroAxisContamination from its
    // Baseline-start value — likely an accidental touch, not the gyro gesture. Returns true and
    // fills outRaw (if non-null) with this frame's decoded per-offset values when a frame was
    // actually captured, so updateGyroRound() can reuse them without a second HID read.
    bool sampleGyroFrame(std::vector<float>* outRaw = nullptr);
    // Advances the Move/Hold sub-state machine for the current axis phase by one frame of
    // already-decoded raw offset values (see sampleGyroFrame()'s outRaw). Detects "moved enough"
    // (MoveToA/MoveToB) and "settled" (HoldA/HoldB) transitions; calls finishGyroRound() once
    // HoldB settles.
    void updateGyroRound(const std::vector<float>& raw);
    // Closes out one round given HoldB's stable reading (restB, one value per m_gyroCandidates
    // index): computes each candidate's hold-delta (restB vs m_gyroRestA) and this round's move
    // amplitude, casts this round's accel/gyro votes, and either confirms the axis winner
    // (2-vote lead reached), starts another round, or fails the whole gyro step out
    // (kGyroMaxRounds reached without a winner).
    void finishGyroRound(const std::vector<float>& restB);
    // Runs Step 1-3 of the old classifyGyro() (find the longest run of alive offsets, trim to
    // 6/3, pick the gravity/"normal" axis) once, right when Baseline commits. Fills
    // m_gyroCandidates/m_gyroHasAccel/m_gyroNormalOffset. Leaves m_gyroCandidates empty if no
    // usable run was found. The normal-axis pick made here is only a first guess (largest
    // Baseline magnitude) — confirmNormalOffsetFromFlip() gets the final say once Flip commits.
    void computeGyroCandidatePool();
    // Confirms or corrects computeGyroCandidatePool()'s normal-offset guess using the Flip
    // phase: a gyro axis reads ~0 in both rest orientations, and the other two accel axes read
    // ~0 in both too (a clean flip doesn't tilt them) — only the true normal axis swings hard
    // and flips sign (+g -> -g) between Baseline and Flip. Falls back to the existing guess,
    // unchanged, if no candidate shows a swing past kGyroMinSignalAmp (e.g. the user didn't
    // actually flip the controller). Rebuilds m_gyroCandidates around whichever offset wins so
    // it is claimed and excluded before Roll/Pitch/Yaw start.
    void confirmNormalOffsetFromFlip();
    // Commits the gyro BindStep and advances to the next component, same bookkeeping the other
    // commit* methods do (overlay label, cooldown, ++m_currentStep, beginStep()). Shared by the
    // success path (all 3 axes converged) and the 2 failure paths (no usable candidate pool,
    // kGyroMaxRounds reached without a winner) — all three just differ in what m_gyroResult holds
    // going in.
    void finishGyroStep();
    // Resets the per-round scratch state (rest streak, HoldA reading, this-round move amplitude)
    // and, when starting a fresh axis (not just a fresh round within one), the vote tallies and
    // round counter too.
    void resetGyroRoundState(bool clearVotes);
    // top-second vote count in `votes` ("ventaja"). Optionally returns the winning candidate's
    // index (into m_gyroCandidates) via outTopCandidate.
    static int voteLead(const std::vector<int>& votes, int* outTopCandidate = nullptr);
    // Runs the final Step 5/6 of classifyGyro() (unchanged greedy "claimed -> removed" slot
    // assignment) over the per-axis winners recorded in m_gyroAxisGyroOffset/m_gyroAxisAccelOffset.
    // Called once, right after Yaw's axis winner is confirmed.
    ImuCalibrationResult classifyGyro() const;
    // Offsets that stayed quiet during Baseline (real sensor channels, not CRC/padding bytes).
    std::vector<bool> computeAliveOffsets() const;
    // Statistical confidence (0..1) for the current axis: min of the gyro-vote lead and (when
    // this axis also votes accel) the accel-vote lead, each as a fraction of kGyroRoundVoteLead.
    // 0 before at least 2 rounds have completed — see definition in BindingWizard.cpp.
    float axisConfidence() const;

    // ── Touch surface discovery (dataOffset/maxX/maxY) ─────────────────────────────────────
    // Resets the whole touch_surface sub-machine to TouchPhase::Lift — shared by beginStep()
    // (first entry) and goBack() (re-entering a completed/failed step), same scratch state
    // either way.
    void resetTouchSurfaceState();
    // Accumulates one frame into the current TouchPhase's candidate/score data (see the .cpp for
    // the per-phase byte-level logic). Returns true when a frame was actually read.
    bool sampleTouchFrame();
    // "Continuar": evaluates the phase that just finished capturing and advances to the next one,
    // or sets m_touchPoolFailed when Lift found no candidates or Confirm found no winner (mirrors
    // computeGyroCandidatePool()'s empty-pool failure).
    void commitTouchPhase();
    // Shared tail of the touch_surface BindStep (success or failure), same bookkeeping as
    // finishGyroStep(): overlay label, cooldown, ++m_currentStep, beginStep().
    void finishTouchSurfaceStep();

    void skipStep();
    void goBack();
    void cancel();
    void saveResult();

    // ── Input capture ────────────────────────────────────────────────────────
    // Shared "Continuar" control for the gyro and touch_surface sub-phases: draws the button
    // (disabled until canAdvance) and, when acceptAnyButton is true (the default), also accepts
    // any controller button press as a confirm — except excludePhysIndex (1-based physIndex,
    // -1 = none excluded), for a phase where ONE specific button can fire as a side effect of the
    // gesture itself (touch_surface's RangeX/RangeY and the touchpad's own click — see the .cpp
    // for why). Returns true once the user has confirmed either way.
    bool renderPhaseAdvanceControl(bool canAdvance, bool acceptAnyButton = true, int excludePhysIndex = -1);
    // Returns true and sets outIndex (1-based) when a new button press is detected.
    bool captureButton(int& outIndex);
    // Returns true when an axis moved past threshold; sets source and invert.
    bool captureAxis(std::string& outSource, bool& outInvert, bool invertIfPositive);
    // Returns true when hat / POV movement is detected; sets dpadType.
    bool captureDpad(std::string& outDpadType);

    void openReader();
    void closeReader();
    void snapshotBaseline();

    // Returns a GamepadState with the current step's component shown as active.
    GamepadState buildFakeState() const;

    // Loads the 4 directional arrow textures from images/decorations/.
    void loadArrows();

    // ── State ────────────────────────────────────────────────────────────────
    State         m_state  = State::Idle;
    ID3D11Device* m_device = nullptr;
    std::string   m_controllersPath;
    std::string   m_stateMapPath;

    PadLayout m_layout;
    PadView   m_canvasView;
    ImVec2    m_canvasOrigin = {};

    std::vector<DetectedController> m_controllers;
    int    m_selectedCtrl  = -1;

    char   m_nameBuf[128]     = {};
    bool   m_saveWithConnection = false; // save connection:"usb"/"bt" (specific) vs generic

    std::unordered_map<std::string, StateMapEntry> m_stateMap;
    std::vector<BindStep> m_steps;
    int    m_currentStep   = 0;
    int    m_noStateCount  = 0;   // components skipped because no state_map entry

    std::vector<ButtonResult> m_boundButtons;
    std::vector<AxisResult>   m_boundAxes;
    bool        m_hasDpad   = false;
    std::string m_dpadType;

    // Current phase of the "gyro" step's sub-machine, reset in beginStep() when that step starts.
    GyroPhase m_gyroPhase = GyroPhase::Baseline;
    bool      m_hasGyroStep = false; // true if the layout has a "gyro" component (set by buildSteps())
    // True when computeGyroCandidatePool() found no usable block of alive offsets right after
    // Baseline — holds the step on an explicit failure message (wizard.gyro_pool_failed) instead
    // of silently auto-advancing to the next component, so the user actually sees why the gyro
    // step got skipped. Cleared by its own "Continuar" (-> finishGyroStep()) or by any of the
    // normal gyro-state resets (beginStep(), goBack()'s gyro branch).
    bool      m_gyroPoolFailed = false;

    // ── Gyro/IMU calibration capture ────────────────────────────────────────
    static constexpr int kGyroPhaseCount = 5; // Baseline + Flip + Roll + Pitch + Yaw
    // Outer index = raw report byte offset (0..rawLen-2), inner index = GyroPhase. Accumulated
    // unconditionally every frame regardless of round sub-stage — used both by
    // computeAliveOffsets()/computeGyroCandidatePool() (Baseline slot) and by classifyGyro()'s
    // final cross-axis amplitude scoring (Roll/Pitch/Yaw slots).
    // Resized to the report's raw length the first time a gyro step starts.
    std::vector<std::array<GyroOffsetStats, kGyroPhaseCount>> m_gyroSamples;
    int                   m_gyroPhaseFrames = 0;  // frames captured in the current phase
    // True once the user has pressed any button on the controller to start the current phase —
    // sampleGyroFrame() is not called before this, so the user can get into position with both
    // hands on the controller instead of needing the mouse to click a "start" button.
    // Reset to false whenever a phase (re)starts: beginStep(), commitGyroPhase(), phase back-step, goBack().
    bool                  m_gyroPhaseStarted = false;
    // True while waiting to see every button released at least once before the "press any button
    // to start" prompt will honor a press. Without this, a button still physically held from
    // CONFIRMING THE PREVIOUS STEP (or a stray bump while the hand repositions) can register as
    // the start press instantly, skipping the prompt the user never got a chance to read. Set
    // alongside every m_gyroPhaseStarted = false; cleared the first frame buttonMask reads 0.
    bool                  m_gyroAwaitingRelease = false;
    RawHIDState           m_gyroAxisBaseline{};    // declared-axis snapshot at Baseline start
    // Diagnostic counters for a Baseline that never advances (2026/07/11 DS4 investigation) —
    // tells apart "HID read never valid" from "every frame contaminated" without flooding the
    // log every frame. Reset alongside m_gyroPhaseFrames in resetGyroRoundState().
    int                   m_gyroBaselineMisses = 0;
    int                   m_gyroBaselineContaminated = 0;
    ImuCalibrationResult  m_gyroResult;            // set by classifyGyro(), consumed by saveResult()

    // ── Candidate pool (computeGyroCandidatePool(), run once when Baseline commits) ────────
    std::vector<int> m_gyroCandidates;       // raw offsets still competing for a gyro/accel role
    bool             m_gyroHasAccel     = false;
    int              m_gyroNormalOffset = -1; // gravity axis, excluded from m_gyroCandidates

    // ── Per-round stop-and-count state (2026/07/24 4th design) ─────────────────────────────
    // See REFERENCE.md, "Wizard de calibracion IMU - 4 diseno de classifyGyro()", for the full
    // algorithm. Round machinery is scoped to the CURRENT axis phase (Roll/Pitch/Yaw); reset by
    // resetGyroRoundState() when a fresh axis (or a redo of the current one) starts.
    GyroRoundStage m_gyroRoundStage = GyroRoundStage::MoveToA;
    int            m_gyroRound      = 1; // 1-based round index within the current axis phase

    // Rest-window detector: running min/max/sum per candidate (index into m_gyroCandidates)
    // since the streak last broke (some candidate's peak-to-peak inside the streak exceeded
    // kGyroBaselineNoiseFloor). A streak that survives kGyroRestMinFrames frames unbroken means
    // the controller has settled — its mean IS the stable reading for that hold.
    std::vector<GyroOffsetStats> m_gyroRestStreak;
    int                          m_gyroRestStreakFrames = 0;
    std::vector<float>           m_gyroRestA;            // stable reading captured at HoldA

    // Movement amplitude for THIS round only (MoveToA + MoveToB combined), reset only at round
    // start — decides at HoldB whether a candidate "reacted for real" during the move (gyro-vote
    // eligibility, see finishGyroRound()).
    std::vector<GyroOffsetStats> m_gyroRoundMoveAmp;
    // Movement amplitude for the CURRENT leg only (MoveToA or MoveToB), reset every time a Move
    // sub-stage starts — decides "moved enough to leave the hold" (Move->Hold transition). Kept
    // separate from m_gyroRoundMoveAmp: that one must span the whole round for scoring, this one
    // must reset per leg or the round's already-cleared MoveToA amplitude would make MoveToB
    // transition to HoldB instantly, without the user having moved back at all.
    std::vector<GyroOffsetStats> m_gyroLegMoveAmp;
    // Mean raw value per candidate during the MoveToA leg only, snapshotted from
    // m_gyroLegMoveAmp right before it gets reset for MoveToB (updateGyroRound()'s
    // HoldA->MoveToB transition) — the reset would otherwise erase the only signal that tells
    // sign detection which way the axis moved during "toward direction A" (see finishGyroRound()).
    std::vector<float>           m_gyroMoveASignMean;

    // Vote tallies for the axis currently being captured, one slot per m_gyroCandidates index.
    // Persist across rounds of the SAME axis; cleared when a new axis phase starts.
    std::vector<int> m_gyroGyroVotes;
    std::vector<int> m_gyroAccelVotes; // only meaningful for Roll/Pitch — Yaw doesn't move gravity

    // Winning candidate offset per axis (raw report offset, -1 = not decided yet). Index
    // 0/1/2 = Roll/Pitch/Yaw. Filled by finishGyroRound() once an axis's votes converge, consumed
    // by classifyGyro()'s final cross-axis reconciliation.
    int m_gyroAxisGyroOffset[3]  = { -1, -1, -1 };
    int m_gyroAxisAccelOffset[3] = { -1, -1, -1 };
    // Sign of each axis's winning candidate, filled alongside m_gyroAxisGyroOffset/
    // m_gyroAxisAccelOffset (same convergence point) — see finishGyroRound().
    bool m_gyroAxisGyroInvert[3]  = { false, false, false };
    bool m_gyroAxisAccelInvert[3] = { false, false, false };

    static constexpr int   kGyroMinCaptureFrames  = 270;   // ~4.5s @60fps — min frames before "continue" enables (Baseline only)
    // Flip only needs a stable mean for the 6 offsets Baseline already found (no exploration
    // needed, unlike Baseline itself), so it gets a much shorter timer.
    static constexpr int   kGyroFlipMinCaptureFrames = 90; // ~1.5s @60fps — min frames before "continue" enables (Flip only)
    static constexpr float kGyroAxisContamination = 0.15f; // declared-axis drift beyond this discards the frame
    // Raw int16 peak-to-peak allowed while quiet. Three uses: Baseline's own quiet check, the
    // rest-streak break check (m_gyroRestStreak), and the hold-delta typing threshold in
    // finishGyroRound() (a real accelerometer's two rest readings differ by much more than this).
    static constexpr float kGyroBaselineNoiseFloor = 800.0f;
    // Upper bound for computeGyroCandidatePool()'s single-offset bridge (see its definition) —
    // a borderline sensor offset having a noisy Baseline capture, not a genuinely dead byte.
    // 1.5x kGyroBaselineNoiseFloor: comfortably covers the documented DS4 case (957.0, still
    // well under this) while staying far below what a real CRC/counter byte reads (those change
    // by design every frame, not just noisily — thousands of counts over a multi-second capture).
    static constexpr float kGyroBorderlineNoiseFloor = 1200.0f;
    // Amplitude floor for "this is a real signal, not noise" — gates both "moved enough to leave
    // a hold" (MoveToA/MoveToB -> HoldA/HoldB) and "reacted for real during the move" (gyro-vote
    // eligibility in finishGyroRound()).
    static constexpr float kGyroMinSignalAmp = 1500.0f;
    // Frame floor before kGyroMinSignalAmp is allowed to end a Move leg — see updateGyroRound()
    // for why (a gyro's angular-rate spike at motion onset can clear the amplitude bar long
    // before the controller reaches the intended extreme).
    static constexpr int   kGyroMoveMinFrames = 60; // ~0.5-1s depending on report rate
    // Floor for a hold streak to count as "settled" (renamed from the old Fast-phase constant of
    // the same value — the Slow/Fast split it belonged to no longer exists).
    static constexpr int   kGyroRestMinFrames = 60; // ~0.5-1s depending on report rate
    static constexpr int   kGyroRoundVoteLead = 2;  // vote lead over the runner-up needed to confirm an axis winner
    static constexpr int   kGyroMaxRounds     = 6;  // hard cap per axis; no winner by then -> classify FAILED

    // ── Touch surface discovery capture (touch_surface step) ───────────────────────────────
    bool       m_hasTouchSurfaceStep = false; // true if the layout has a "touchpad" component (set by buildSteps())
    TouchPhase m_touchPhase          = TouchPhase::Lift;
    // Same "press any button to start" gate as the gyro phases (m_gyroPhaseStarted/
    // m_gyroAwaitingRelease) — separate members because a layout with both a gyro and a touchpad
    // component runs both steps in the same wizard pass. Only used by Lift/Confirm; RangeX/RangeY
    // start capturing immediately (the touch-and-slide gesture itself is the "start").
    bool       m_touchPhaseStarted    = false;
    bool       m_touchAwaitingRelease = false;
    int        m_touchPhaseFrames     = 0;    // frames captured in the current phase
    // True when Lift found no candidate offsets, or Confirm found no candidate whose edge count
    // landed within kTouchConfirmTapTolerance of kTouchConfirmTargetTaps — holds the step on an
    // explicit failure message (wizard.touch_surface_lift_failed / _confirm_failed) instead of
    // silently skipping, same reasoning as m_gyroPoolFailed.
    bool       m_touchPoolFailed = false;

    // Lift: per raw-report-byte-offset, true while that byte has read bit7==1 (not touching) in
    // EVERY sampled frame so far. Resized to the report's raw length and reset to all-true the
    // first time Lift starts (resetTouchSurfaceState()).
    std::vector<bool> m_touchLiftAlive;
    // Running min/max of the FULL byte value (not just bit7) per offset during Lift — a gyro/
    // accel raw byte still carries sensor noise even holding the controller still, so it almost
    // never reads bit-for-bit constant the way the touch flag's fixed idle value does; requiring
    // peak-to-peak <= kTouchLiftNoiseFloor at commitTouchPhase() weeds those out before Confirm
    // ever sees them (confirmed for real 2026/08/30: Confirm kept failing to find a winner while
    // the user was actively sliding their finger the whole time — a near-still gyro/accel byte
    // had snuck into the Lift candidate pool and was competing for the win).
    std::vector<uint8_t> m_touchLiftMin;
    std::vector<uint8_t> m_touchLiftMax;
    // Confirm: per raw-report-byte-offset, how many clean "not touching -> touching" transitions
    // that byte showed (only meaningful for offsets still alive out of Lift). The user taps and
    // releases repeatedly, each tap at a clearly different spot on the pad (a corner-to-corner
    // diagonal is suggested) instead of holding, sliding, or tapping the same spot — a duty-cycle/
    // fraction score turned out not to separate the true byte from Lift survivors reliably
    // (confirmed for real 2026/08/30, sliding continuously still left ambiguous candidates), and
    // "whichever taps the most" wasn't reliable either (confirmed for real 2026/08/30, intento 4:
    // a position byte inside the same touch block out-scored the real one because finger jitter
    // mid-hold straddled ITS OWN bit7 boundary a couple of extra times within a single physical
    // tap). Spreading the taps across very different positions is what actually separates them:
    // the real activity byte reads exactly one clean edge per physical tap no matter where it
    // lands (it is position-independent), while a position-dependent byte's own bit7 only reads
    // "touching" for SOME of those positions, so its total edge count drifts away from the
    // requested count across a genuinely varied diagonal — see kTouchConfirmTargetTaps.
    std::vector<int>  m_touchConfirmEdges;
    // Previous frame's "touching" (bit7==0) state per offset, to detect the rising edge above.
    std::vector<bool> m_touchConfirmPrevTouching;
    int        m_touchDataOffset = -1; // Confirm's winning offset, consumed by RangeX/RangeY and saveResult()
    int        m_touchRangeMaxX  = 0;  // running max X seen in RangeX while touching
    int        m_touchRangeMaxY  = 0;  // running max Y seen in RangeY while touching
    int        m_touchRangeTouchedFrames = 0; // frames with an active touch this RangeX/RangeY phase
    // Physical HID button index (1-based, m_boundButtons' ButtonResult::physIndex) of the
    // touchpad's own click, resolved from the preceding click BindStep once touch_surface starts
    // (resetTouchSurfaceState()). Excluded from RangeX/RangeY's "any button confirms" check —
    // sliding a finger into the pad's edge can trigger this exact click by accident, which must
    // NOT be allowed to end the phase before the finger actually gets there (confirmed for real
    // 2026/08/30: max_x/max_y came in short). -1 if the click step was skipped.
    int        m_touchClickPhysIndex = -1;
    TouchSurfaceResult m_touchSurfaceResult; // set by commitTouchPhase() on RangeY success, consumed by saveResult()

    static constexpr int   kTouchLiftMinFrames    = 90;  // ~1.5s @60fps — no-touch settle window
    // Byte-value peak-to-peak allowed during Lift for a candidate to stay alive — see
    // m_touchLiftMin/m_touchLiftMax's comment. Tight on purpose: the true touch flag's idle value
    // is hardware-fixed (0 jitter expected), while even a dead-still sensor byte's own noise floor
    // is far above this (kGyroBaselineNoiseFloor=800 on the full int16 it belongs to).
    static constexpr int   kTouchLiftNoiseFloor   = 3;
    static constexpr int   kTouchConfirmMinFrames = 150; // ~2.5s @60fps — floor before Continue is clickable, gives room for a few taps
    // RangeX/RangeY have no percentage/timer gate at all — the user slides into the edge as many
    // times as they want, watching the live max readout, and confirms with any button (except the
    // touchpad's own click, see m_touchClickPhysIndex) whenever they're satisfied. This is just
    // the floor before Continue is even clickable, so a literal zero-frame tap can't confirm.
    static constexpr int   kTouchRangeMinFrames   = 1;
    // Confirm asks the user for exactly this many taps (see wizard.touch_surface_confirm), spread
    // across clearly different points of the pad. The winning offset is the lowest-numbered
    // candidate whose edge count (m_touchConfirmEdges) falls within +-kTouchConfirmTapTolerance of
    // this target — not "whichever has the most" (see m_touchConfirmEdges' comment for why that
    // failed for real). Preferring the lowest offset on a tie matches the DS4/DualShock4 touch
    // block layout, where the activity byte is always the FIRST of the 4 (see ARCHITECTURE.md,
    // "Wizard — descubrimiento crudo") — both wrong offsets seen so far (36, 37) were higher than
    // the real one (35).
    static constexpr int   kTouchConfirmTargetTaps     = 5;
    static constexpr int   kTouchConfirmTapTolerance   = 1;

    // Overlay: compIndex → display label (button number or axis name)
    std::unordered_map<int, std::string> m_overlayLabels;

    // Raw reader
    std::unique_ptr<RawHIDReader> m_hidReader;
    DWORD          m_prevButtonMask  = 0;
    RawHIDState    m_axisBaseline    = {};

    int  m_stepCooldown = 0;  // frames to wait after an axis/trigger commit before detecting again
    bool m_savedFlag    = false;

    // Axis confirmation state — require sustained movement before committing
    int   m_axisConfirmCount = 0;
    int   m_axisConfirmBest  = -1;
    float m_axisConfirmSum   = 0.0f;  // sum of signed deltas for direction averaging
    // Last successfully-read HID state for axis capture.
    // On event-driven devices (Zero 2 D-mode) the device only sends a report on state change,
    // so subsequent reads timeout and return nothing — this persists the last known state
    // across frames so the 6-frame confirmation window can complete.
    RawHIDState m_axisLastRead{};

    // Directional arrow textures for axis step feedback
    PadTexture m_arrowLeft;
    PadTexture m_arrowRight;
    PadTexture m_arrowUp;
    PadTexture m_arrowDown;

    // Gyroscope widget arrows (same assets as PadView's gyro widget, see loadArrows()) —
    // lit during the gyro step's Roll/Pitch/Yaw MoveToA/MoveToB to show which gesture is
    // being requested, instead of the generic axis arrows above.
    PadTexture m_gyroArrowN, m_gyroArrowS, m_gyroArrowE, m_gyroArrowW;
    PadTexture m_gyroArrowCW, m_gyroArrowCCW;

    static constexpr float kAxisNoiseFloor = 0.30f;  // below this is drift/noise — resets confirmation
    static constexpr float kAxisThreshold  = 0.45f;  // must exceed this to commit
    static constexpr DWORD kWinmmThreshold = 12000;  // out of 65535
    static constexpr int   kAxisCooldown   = 45;     // ~750ms at 60fps
    static constexpr int   kAxisConfirm    = 6;      // frames axis must dominate before commit (~100ms)
};
