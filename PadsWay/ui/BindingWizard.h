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
    // One phase per axis after Baseline — see REFERENCE.md, "Wizard de calibracion IMU - 4 diseno
    // de classifyGyro(): parar-y-contar + voto". Treated as a plain sequential index elsewhere
    // (+-1 to advance/step back), so the declaration order here IS the wizard order.
    enum class GyroPhase { Baseline, Roll, Pitch, Yaw };

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
    // usable run was found.
    void computeGyroCandidatePool();
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
    void skipStep();
    void goBack();
    void cancel();
    void saveResult();

    // ── Input capture ────────────────────────────────────────────────────────
    // Shared "Continuar" control for the gyro sub-phases: draws the button (disabled until
    // canAdvance) and also accepts any controller button press as a confirm. Returns true once
    // the user has confirmed either way.
    bool renderGyroAdvanceControl(bool canAdvance);
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

    // ── Gyro/IMU calibration capture ────────────────────────────────────────
    static constexpr int kGyroPhaseCount = 4; // Baseline + Roll + Pitch + Yaw
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
