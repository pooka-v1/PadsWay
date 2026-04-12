#pragma once
#include <windows.h>
#include <d3d11.h>
#include <deque>
#include <vector>
#include <future>
#include <atomic>
#include <unordered_map>
#include "PadEngine.h"
#include "PadScanner.h"
#include "input/HIDScanner.h"
#include "config/ConfigLoader.h"
#include "GamepadState.h"
#include "ui/PadView.h"
#include "ui/LayoutEditor.h"

// Manages the Win32 window, Direct3D 11 device, and ImGui context.
// Call run() from the main thread — it blocks until the window is closed.
class AppWindow {
public:
    explicit AppWindow(PadEngine& engine);
    ~AppWindow();

    // Creates the window, initialises D3D11 + ImGui, starts the engine,
    // runs the message + render loop, then cleans everything up.
    // Returns the process exit code (0 = normal exit).
    int run();

private:
    // --- Initialisation / cleanup ---
    bool initWindow();
    bool initD3D();
    void cleanup();
    void createRenderTarget();
    void cleanupRenderTarget();

    // --- Per-frame rendering ---
    void renderFrame();
    void renderEngineTab();
    void renderScannerTab();
    void renderPadsTab();
    void renderMappingSubtab();
    void renderLayoutTab();

    // --- Win32 window procedure ---
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // --- Engine ---
    PadEngine& m_engine;

    // --- D3D11 / Win32 ---
    HWND                    m_hwnd              = nullptr;
    ID3D11Device*           m_device            = nullptr;
    ID3D11DeviceContext*    m_context           = nullptr;
    IDXGISwapChain*         m_swapChain         = nullptr;
    ID3D11RenderTargetView* m_renderTarget      = nullptr;
    bool                    m_swapChainOccluded = false;

    // --- Scanner state (used only on the main/render thread) ---
    std::vector<PadScanner::DeviceInfo> m_scanDevices;
    std::vector<HIDScanner::DeviceInfo> m_hidDevices;
    int       m_scanSelected  = -1;   // index into m_scanDevices (-1 = none)
    int       m_hidSelected   = -1;   // index into m_hidDevices  (-1 = none)
    ULONGLONG m_lastScanTime  = 0;    // tick of last WinMM refresh
    ULONGLONG m_lastHidScanTime = 0;  // tick of last HID scan kick-off
    float     m_scanSplitX    = 340.0f; // width of the left (device list) panel

    // --- Async HID scan (runs on a background thread to avoid blocking render) ---
    std::future<std::vector<HIDScanner::DeviceInfo>> m_hidScanFuture;
    std::atomic<bool> m_hidScanRunning { false };

    // --- Controller configs (for friendly name lookup in the scanner) ---
    std::vector<ControllerConfig> m_controllerConfigs;

    // --- VirtualPad config (loaded once at startup) ---
    std::vector<std::string> m_acceptedXboxButtons;
    float                    m_stickSelectThreshold = 0.85f;
    int                      m_stickHoldMs          = 2000;

    // --- Game profiles ---
    std::vector<std::string> m_profilePaths;   // full paths to discovered profile JSONs
    std::vector<std::string> m_profileNames;   // profile_name from each JSON
    int                      m_profileSelected = 0;  // 0 = none, 1+ = index into lists

    // --- HID live monitor (scanner right panel for HID devices) ---
    // Uses the engine's last read state — avoids competing with the engine on BT HID.
    GamepadState m_hidScanState = {};

    // --- Pad layouts ---
    std::vector<PadLayout> m_padLayouts;
    std::string            m_currentLayoutId;   // last layout applied to m_padView

    // --- Marquee ---
    enum class MarqueeEntryType { Macro, BotOn, BotOff, Keyboard, Mouse };
    struct MarqueeEntry { MarqueeEntryType type; std::string text; };

    std::deque<MarqueeEntry> m_marqueeLines;  // max 4 visible entries (oldest first)

    // --- Pad views ---
    PadView m_padView;                          // physical controller
    PadView m_virtualPadView;                   // virtual Xbox One output
    bool    m_virtualPadInitialized = false;    // xbox_one layout loaded once
    bool    m_forceLayoutReload     = false;    // set after editor saves; triggers forceSetLayout

    // --- Layout editor ---
    LayoutEditor m_layoutEditor;
    bool         m_layoutEditorInitialized = false;
    bool         m_layoutsFromBackup       = false;  // true when .bak was the fallback

    // --- Mapping editor (subtab [Mapear] en Pads) ---
    int      m_mappingSelPhysComp  = -1;  // componente físico seleccionado (-1 = ninguno)
    ImVec2   m_mappingPhysOrigin   = {};  // canvas origin del pad físico (capturado cada frame)
    ImVec2   m_mappingVirtOrigin   = {};  // canvas origin del pad virtual
    uint16_t m_mappingActiveVid    = 0;   // VID del mando activo al cargar edits
    uint16_t m_mappingActivePid    = 0;   // PID del mando activo al cargar edits
    int         m_mappingFlashComp    = -1;   // componente virtual en flash de confirmación (-1 = ninguno)
    float       m_mappingFlashTimer   = 0.0f; // segundos restantes del flash
    std::string m_mappingFlashVirtShort;      // virtShort asignado (para dpad y otros sin .state)
    std::unordered_map<std::string, std::string>    m_mappingEdits;     // physShort → virtShort (Xbox)
    std::unordered_map<std::string, ButtonAction>   m_h5ActionEdits;    // physShort → acción H5
    // H5 — action type selector (buttons and half-axis directions)
    enum class H5ActionType { Xbox, Analog, Macro, Keyboard, Mouse };
    H5ActionType             m_h5ActionType         = H5ActionType::Xbox;
    std::vector<std::pair<std::string,std::string>> m_h5CaptureKeys; // {json_name, display}
    std::string              m_h5MacroSel;           // macro seleccionada en el combo
    std::vector<std::string> m_h5MacroNames;         // nombres cargados de macros.json
    bool                     m_h5MacroNamesLoaded = false;
    // H9 — hardware mapping (hold to select, press to assign)
    int          m_h9HoldComp     = -1;   // component being held for selection (-1 = none)
    std::string  m_h9HoldStickDir;       // direction being held for stick ("up/down/left/right/center" or "")
    std::string  m_h9HoldDpadDir;        // dpad direction being held ("up/down/left/right" or "")
    float        m_h9HoldTimer   = 0.0f; // seconds held so far
    float        m_h9ErrorTimer  = 0.0f; // seconds left to show invalid-target error
    GamepadState m_h9PrevPhysState{};    // previous frame physical state for edge detection
    PadTexture m_arrowRightTex;
    // H6 — whole-axis mapping
    std::unordered_map<std::string, AxisMapping> m_h6AxisEdits;  // stickId → pending edit
    // H6 — half-axis and dpad-direction actions
    // Key: axis source + "_pos"/"_neg" (e.g. "hid_x_pos") for sticks,
    //      or dpad direction name (e.g. "dpad_up") for cruceta directions
    std::unordered_map<std::string, HalfAxisAction> m_axisActionEdits;
    std::string  m_selStickDir;          // selected direction for axis mapping: "up","down","left","right",""
    bool         m_selStickAsButton = false;  // true → stick selected for L3/R3 button assignment
    std::string  m_selDpadDir;           // selected dpad direction: "up","down","left","right",""
    void saveMappingEdits();
    void reloadMappingEdits();
};
