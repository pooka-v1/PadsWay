#pragma once
#include <windows.h>
#include <d3d11.h>
#include <deque>
#include <vector>
#include <future>
#include <atomic>
#include <unordered_map>
#include "PadEngine.h"
#include "input/HIDScanner.h"
#include "input/RawHIDReader.h"
#include <memory>
#include "config/ConfigLoader.h"
#include "GamepadState.h"
#include "ui/PadView.h"
#include "ui/LayoutEditor.h"
#include "ui/MappingEditor.h"

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

    // --- Game profiles ---
    std::vector<std::string> m_profilePaths;   // full paths to discovered profile JSONs
    std::vector<std::string> m_profileNames;   // profile_name from each JSON
    int                      m_profileSelected = 0;  // 0 = none, 1+ = index into lists

    // --- HID live monitor (scanner right panel) ---
    // m_scanDevice holds its own handle — independent of the Engine.
    std::unique_ptr<RawHIDReader> m_scanDevice;
    RawHIDState  m_scanRawState  = {};
    int          m_scanDeviceIdx = -1;  // index of the device currently open in m_scanDevice

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

    // --- Mapping editor (modo mapping en Pads) ---
    PadTexture    m_arrowRightTex;   // arrow used in the normal (non-mapping) pad view
    MappingEditor m_mappingEditor;
};
