#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
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
    void skipStep();
    void goBack();
    void cancel();
    void saveResult();

    // ── Input capture ────────────────────────────────────────────────────────
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
