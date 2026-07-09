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
    enum class GyroPhase { Baseline, Roll, Pitch, Yaw };

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
    // Advances the gyro sub-machine by one phase (Baseline->Roll->Pitch->Yaw).
    // After Yaw, commits the step and moves on to the next component like the other commit* methods.
    void commitGyroPhase();
    // Accumulates one frame of raw-byte samples into m_gyroSamples for the current gyro phase.
    // Rejects frames where a declared HID axis (stick/trigger) drifted past kGyroAxisContamination
    // from its Baseline-start value — likely an accidental touch, not the gyro gesture.
    void sampleGyroFrame();
    // Clears the accumulated samples for one phase so it can be re-captured from scratch
    // (used when the user steps back into a phase to redo it).
    void resetGyroPhaseSamples(GyroPhase phase);
    // Runs the accel/gyro offset classification over the 4 phases captured in m_gyroSamples.
    // Called once, right after the Yaw phase's capture completes.
    ImuCalibrationResult classifyGyro() const;
    // Offsets that stayed quiet during Baseline (real sensor channels, not CRC/padding bytes).
    std::vector<bool> computeAliveOffsets() const;
    // Statistical confidence (0..1) that Roll/Pitch/Yaw's leading candidate offset is a real,
    // repeatable signal rather than noise. See definition in BindingWizard.cpp for the method.
    float gyroConfidence() const;
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
    // Outer index = raw report byte offset (0..rawLen-2), inner index = GyroPhase.
    // Resized to the report's raw length the first time a gyro step starts.
    std::vector<std::array<GyroOffsetStats, 4>> m_gyroSamples;
    int                   m_gyroPhaseFrames = 0;  // frames captured in the current phase
    // True once the user has pressed any button on the controller to start the current phase —
    // sampleGyroFrame() is not called before this, so the user can get into position with both
    // hands on the controller instead of needing the mouse to click a "start" button.
    // Reset to false whenever a phase (re)starts: beginStep(), commitGyroPhase(), phase back-step, goBack().
    bool                  m_gyroPhaseStarted = false;
    RawHIDState           m_gyroAxisBaseline{};    // declared-axis snapshot at Baseline start
    ImuCalibrationResult  m_gyroResult;            // set by classifyGyro(), consumed by saveResult()

    // Roll/Pitch/Yaw live confidence (see gyroConfidence()). Baseline has no gesture to
    // repeat, so it keeps the plain frame-count gate below.
    //
    // This went through several more complicated designs first — discrete repetition
    // detection (frame-to-frame rate, then deviation-from-baseline) feeding a Welch's t-test
    // between the best and second-best candidate's peak. All of them needed the *segmentation*
    // step (deciding where one "repetition" ends and the next begins) to be reliable, and none
    // of it was: a slow smooth swing produces no sharp per-frame delta to key off; an
    // accelerometer settling in a new tilt never decays back to the old Baseline value; and
    // with only 2-3 noisy repetitions, a t-test's variance term dominates and the result gets
    // LESS stable the more (noisy) data comes in, not more.
    //
    // gyroConfidence() below sidesteps all of that: it just watches the SAME running
    // (max - min) amplitude per offset that classifyGyro() already accumulates unconditionally
    // every frame for the final classification (see the loop in sampleGyroFrame() above). No
    // segmentation, no per-repetition anything — whichever offset has swung the most so far
    // simply IS the best candidate, and that comparison only gets more decisive over time
    // (both amplitudes are monotonically non-decreasing), never less.
    std::vector<bool> m_gyroAliveOffsets; // set once when Baseline commits, reused by all 3 phases

    static constexpr int   kGyroMinCaptureFrames  = 270;   // ~4.5s @60fps — min frames before "continue" enables (Baseline only)
    static constexpr float kGyroAxisContamination = 0.15f; // declared-axis drift beyond this discards the frame
    static constexpr float kGyroBaselineNoiseFloor = 800.0f; // raw int16 peak-to-peak allowed while quiet (Baseline)
    static constexpr float kGyroMinSignalAmp        = 1500.0f; // the leading candidate's amplitude must clear this before confidence is anything but 0 — otherwise "confident" just means "confident it's noise"
    static constexpr float kGyroTargetSignalAmp     = 4000.0f; // leading-candidate amplitude that counts as "fully confident" — no runner-up comparison: two accel axes legitimately react similarly to the same tilt, so "beats every other offset by 2.5x" is an unrealistic bar; classifyGyro()'s cross-phase comparison (not this live number) is what actually tells gyro from accel
    static constexpr int   kGyroMinPhaseFrames      = 60;     // ~0.5s @110Hz — floor so one lucky early frame can't claim high confidence instantly
    static constexpr float kGyroConfidenceThreshold = 0.90f; // Roll/Pitch/Yaw auto-advance once gyroConfidence() reaches this

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

    static constexpr float kAxisNoiseFloor = 0.30f;  // below this is drift/noise — resets confirmation
    static constexpr float kAxisThreshold  = 0.45f;  // must exceed this to commit
    static constexpr DWORD kWinmmThreshold = 12000;  // out of 65535
    static constexpr int   kAxisCooldown   = 45;     // ~750ms at 60fps
    static constexpr int   kAxisConfirm    = 6;      // frames axis must dominate before commit (~100ms)
};
