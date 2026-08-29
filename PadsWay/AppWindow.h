#pragma once
#include <windows.h>
#include <d3d11.h>
#include <deque>
#include <vector>
#include <future>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include "PadEngine.h"
#include "input/HIDScanner.h"
#include "input/DeviceHub.h"
#include <memory>
#include "config/ConfigLoader.h"
#include "GamepadState.h"
#include "ui/PadView.h"
#include "ui/LayoutEditor.h"
#include "ui/MappingEditor.h"
#include "ui/MacroManagerPanel.h"
#include "ui/CalibrationPanel.h"

// Manages the Win32 window, Direct3D 11 device, and ImGui context.
// Call run() from the main thread — it blocks until the window is closed.
class AppWindow {
public:
    // deviceHub outlives AppWindow — owned by main() (PadsWay.cpp), passed by reference.
    AppWindow(PadEngine& engine, DeviceHub& deviceHub);
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
    void renderLayoutTab();

    // Re-scan data/*.json for game profiles and update m_profilePaths/Names.
    void refreshProfileList();

    // --- Win32 window procedure ---
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // --- Engine / device ownership ---
    PadEngine& m_engine;
    DeviceHub& m_deviceHub;

    // --- D3D11 / Win32 ---
    HWND                    m_hwnd              = nullptr;
    ID3D11Device*           m_device            = nullptr;
    ID3D11DeviceContext*    m_context           = nullptr;
    IDXGISwapChain*         m_swapChain         = nullptr;
    ID3D11RenderTargetView* m_renderTarget      = nullptr;
    bool                    m_swapChainOccluded = false;

    // --- Scanner state (used only on the main/render thread) ---
    std::vector<HIDScanner::DeviceInfo> m_hidDevices;
    int       m_hidSelected     = -1;   // index into m_hidDevices (-1 = none)
    ULONGLONG m_lastHidScanTime = 0;    // tick of last HID scan kick-off
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
    float                    m_gyroSelectThreshold  = 0.24f;
    float                    m_accelSelectThreshold = 0.5f;

    // --- Game profiles ---
    std::vector<std::string> m_profilePaths;   // full paths to discovered profile JSONs
    std::vector<std::string> m_profileNames;   // profile_name from each JSON
    int                      m_profileSelected = 0;  // 0 = none, 1+ = index into lists

    // --- Virtual output type selector (combo + inline confirmation) ---
    int                      m_pendingOutputSel  = 0;     // selection awaiting confirmation (0=Xbox, 1=DS4)
    bool                     m_outputConfirmOpen = false;  // inline confirm row is visible

    // --- HID live monitor (scanner right panel) ---
    // The connection itself lives in DeviceHub (shared with the Engine) — this only tracks which
    // path the panel currently shows and whether we opened a watch() for it ourselves, as opposed
    // to the Engine already owning it (see AppWindow::renderScannerTab, ARCHITECTURE.md "DeviceHub").
    int         m_scanDeviceIdx  = -1;    // index into m_hidDevices currently shown (-1 = none)
    std::string m_scanWatchedPath;        // path passed to the last deviceHub.watch()/unwatch()
    bool        m_scanWatchOwned = false; // true if m_scanWatchedPath was opened by us, not the Engine

    // --- Scanner: live gyro/accel block auto-detection (see BITACORA 2026/08/11) ---
    // Locates the contiguous run of raw report bytes that behaves like sensor data instead of
    // relying on RawHIDReader's old hardcoded offset 13 (DS4 USB only). Re-armed whenever the
    // selected device changes (see m_scanDeviceIdx). Runs off DeviceHub's shared raw snapshot for
    // the selected path, populated whether the Engine or our own watch() thread drives the reads.
    static constexpr int kScanImuDetectFrames = 100; // ~1.5-2s @ 60fps
    bool                          m_scanImuDetecting    = false;
    int                           m_scanImuDetectFrames = 0;
    std::vector<std::pair<float,float>> m_scanImuMinMax; // per raw byte offset, min/max seen this window
    std::vector<int>             m_scanImuOffsets;       // up to 6 offsets found; empty = not (yet) detected

    // --- Scanner: live touch block (manual offset, no auto-detection — see SESSION_CONTEXT.md
    // "Wizard") ---
    int m_scanTouchOffset = -1; // manually-tuned offset; -1 = not yet initialised for this device

    // --- Pad layouts ---
    std::vector<PadLayout> m_padLayouts;
    std::string            m_currentLayoutId;   // last layout applied to m_padView

    // --- Marquee ---
    enum class MarqueeEntryType { Macro, BotOn, BotOff, Keyboard, Mouse };
    struct MarqueeEntry { MarqueeEntryType type; std::string text; };

    std::deque<MarqueeEntry> m_marqueeLines;  // max 4 visible entries (oldest first)

    // --- Pad views ---
    PadView m_padView;                          // physical controller
    PadView m_virtualPadView;                   // virtual output (Xbox / DS4)
    std::string m_currentVirtualLayoutId;       // last layout applied to m_virtualPadView; reloaded on output hot-swap
    bool    m_forceLayoutReload     = false;    // set after editor saves; triggers forceSetLayout

    // --- Layout editor ---
    LayoutEditor m_layoutEditor;
    bool         m_layoutEditorInitialized = false;
    bool         m_layoutsFromBackup       = false;  // true when .bak was the fallback

    // --- Mapping editor (modo mapping en Pads) ---
    PadTexture    m_arrowRightTex;   // arrow used in the normal (non-mapping) pad view
    MappingEditor m_mappingEditor;

    // --- Macro manager panel ---
    MacroManagerPanel m_macroManager;

    // --- Calibration panel ---
    CalibrationPanel m_calibrationPanel;
};
