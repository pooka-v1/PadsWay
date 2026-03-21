#pragma once
#include <windows.h>
#include <d3d11.h>
#include <vector>
#include "PadEngine.h"
#include "PadScanner.h"

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
    int       m_scanSelected  = -1;   // index into m_scanDevices (-1 = none)
    ULONGLONG m_lastScanTime  = 0;    // tick of last auto-refresh
};
