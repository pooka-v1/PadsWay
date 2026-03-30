#pragma once
#include <windows.h>
#include <d3d11.h>
#include <deque>
#include <vector>
#include <future>
#include <atomic>
#include "PadEngine.h"
#include "PadScanner.h"
#include "input/HIDScanner.h"
#include "config/ConfigLoader.h"
#include "GamepadState.h"
#include "ui/PadView.h"

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
};
