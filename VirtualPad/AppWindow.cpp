#include "AppWindow.h"

#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <cstdio>
#include <cmath>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AppWindow::AppWindow(PadEngine& engine)
    : m_engine(engine) {}

AppWindow::~AppWindow() {
    cleanup();
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

int AppWindow::run() {
    ImGui_ImplWin32_EnableDpiAwareness();

    if (!initWindow()) return 1;
    if (!initD3D())    { DestroyWindow(m_hwnd); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style       = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.TabRounding       = 3.0f;
    style.FramePadding      = { 6.0f, 4.0f };
    style.ItemSpacing       = { 8.0f, 6.0f };

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    m_engine.start();

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (m_swapChainOccluded && m_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        m_swapChainOccluded = false;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        renderFrame();

        ImGui::Render();

        const float clearColor[4] = { 0.10f, 0.10f, 0.11f, 1.00f };
        m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_context->ClearRenderTargetView(m_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = m_swapChain->Present(1, 0);
        if (hr == DXGI_STATUS_OCCLUDED) m_swapChainOccluded = true;
    }

    m_engine.stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup();
    return 0;
}

// ---------------------------------------------------------------------------
// renderFrame — full-screen canvas with tab bar
// ---------------------------------------------------------------------------

void AppWindow::renderFrame() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ 0.0f, 0.0f });
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##canvas", nullptr,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Engine"))  { renderEngineTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Scanner")) { renderScannerTab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Engine tab
// ---------------------------------------------------------------------------

void AppWindow::renderEngineTab() {
    ImGui::Spacing();

    bool connected = m_engine.isConnected();
    bool running   = m_engine.isRunning();

    if (connected) {
        ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "●");
        ImGui::SameLine();
        ImGui::Text("Connected — %s", m_engine.getDevice().c_str());
    } else if (running) {
        ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "●");
        ImGui::SameLine();
        ImGui::Text("Waiting for device...");
    } else {
        ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "●");
        ImGui::SameLine();
        ImGui::Text("Engine stopped");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Status:");
    ImGui::SameLine();
    ImGui::Text("%s", m_engine.getStatus().c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Console window shows full log output.");
    ImGui::TextDisabled("Close this window to exit.");
}

// ---------------------------------------------------------------------------
// Scanner tab — helpers
// ---------------------------------------------------------------------------

// Converts a raw WinMM axis value [0..65535] to normalised float [-1..1].
static float normalizeAxis(DWORD v) {
    float n = (static_cast<float>(v) - 32767.5f) / 32767.5f;
    if (n < -1.0f) n = -1.0f;
    if (n >  1.0f) n =  1.0f;
    return n;
}

// Returns a human-readable POV direction string.
static const char* povDirection(DWORD pov) {
    if (pov == JOY_POVCENTERED) return "Center";
    if (pov <  2250)            return "N";
    if (pov <  6750)            return "NE";
    if (pov < 11250)            return "E";
    if (pov < 15750)            return "SE";
    if (pov < 20250)            return "S";
    if (pov < 24750)            return "SW";
    if (pov < 29250)            return "W";
    if (pov < 33750)            return "NW";
    return "N";
}

// Draws a 3×3 compass widget showing the active POV direction.
static void drawPOVCompass(DWORD pov) {
    // Map POV to (col, row): N=top-center, E=mid-right, etc.
    struct Dir { int col, row; const char* label; DWORD minVal, maxVal; };
    static const Dir dirs[] = {
        { 1, 0, "N",  33750, 65535 }, { 1, 0, "N",  0,     2249  },
        { 2, 0, "NE", 2250,  6749  },
        { 2, 1, "E",  6750,  11249 },
        { 2, 2, "SE", 11250, 15749 },
        { 1, 2, "S",  15750, 20249 },
        { 0, 2, "SW", 20250, 24749 },
        { 0, 1, "W",  24750, 29249 },
        { 0, 0, "NW", 29250, 33749 },
    };

    // Determine active cell
    int activeCol = -1, activeRow = -1;
    if (pov != JOY_POVCENTERED) {
        for (const auto& d : dirs) {
            bool match = (d.minVal <= d.maxVal)
                ? (pov >= d.minVal && pov <= d.maxVal)
                : (pov >= d.minVal || pov <= d.maxVal);
            if (match) { activeCol = d.col; activeRow = d.row; break; }
        }
    }

    const float cellSize = 22.0f;
    const float pad      = 2.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    static const char* labels[3][3] = {
        { "NW", "N", "NE" },
        { "W",  " ", "E"  },
        { "SW", "S", "SE" },
    };

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            ImVec2 tl = { origin.x + col * (cellSize + pad), origin.y + row * (cellSize + pad) };
            ImVec2 br = { tl.x + cellSize, tl.y + cellSize };

            bool active = (col == activeCol && row == activeRow);
            ImU32 bg = active
                ? IM_COL32(50, 200, 50, 255)
                : (col == 1 && row == 1 ? IM_COL32(60, 60, 65, 255)
                                        : IM_COL32(40, 40, 44, 255));

            dl->AddRectFilled(tl, br, bg, 3.0f);
            dl->AddRect(tl, br, IM_COL32(80, 80, 85, 255), 3.0f);

            const char* lbl = labels[row][col];
            ImVec2 textSize = ImGui::CalcTextSize(lbl);
            ImVec2 textPos = {
                tl.x + (cellSize - textSize.x) * 0.5f,
                tl.y + (cellSize - textSize.y) * 0.5f
            };
            dl->AddText(textPos, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 165, 255), lbl);
        }
    }

    // Advance cursor past the compass
    ImGui::Dummy({ 3 * (cellSize + pad), 3 * (cellSize + pad) });
}

// ---------------------------------------------------------------------------
// Scanner tab — main render
// ---------------------------------------------------------------------------

void AppWindow::renderScannerTab() {
    // Auto-refresh device list every second
    ULONGLONG now = GetTickCount64();
    if (now - m_lastScanTime > 1000) {
        m_scanDevices  = PadScanner::scan();
        m_lastScanTime = now;
        if (m_scanSelected >= (int)m_scanDevices.size())
            m_scanSelected = -1;
    }

    ImGui::Spacing();

    // ── Left panel: device list ──────────────────────────────────────────
    float leftWidth = 340.0f;
    ImGui::BeginChild("##DeviceList", { leftWidth, 0.0f }, true);

    ImGui::Text("Devices (%zu)", m_scanDevices.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        m_scanDevices  = PadScanner::scan();
        m_lastScanTime = GetTickCount64();
        if (m_scanSelected >= (int)m_scanDevices.size())
            m_scanSelected = -1;
    }
    ImGui::Separator();

    if (m_scanDevices.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No devices found.");
        ImGui::TextDisabled("Connect a controller and click Refresh.");
    } else {
        for (int i = 0; i < (int)m_scanDevices.size(); ++i) {
            const auto& dev = m_scanDevices[i];
            char label[128];
            snprintf(label, sizeof(label), "[%u] %ls", dev.port, dev.name);

            bool selected = (m_scanSelected == i);
            if (ImGui::Selectable(label, selected, 0, { 0, 0 }))
                m_scanSelected = i;

            // Show VID/PID + capabilities as a subtitle
            ImGui::SameLine();
            ImGui::TextDisabled("  VID:%04X PID:%04X  %uax %ubtn",
                dev.vid, dev.pid, dev.axes, dev.buttons);
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // ── Right panel: input monitor ───────────────────────────────────────
    ImGui::BeginChild("##InputMonitor", { 0.0f, 0.0f }, true);

    if (m_scanSelected < 0 || m_scanSelected >= (int)m_scanDevices.size()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Select a device on the left to monitor its inputs.");
        ImGui::EndChild();
        return;
    }

    const auto& dev = m_scanDevices[m_scanSelected];
    auto raw = PadScanner::readRaw(dev.port);

    if (!raw.valid) {
        ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "Read failed — device disconnected?");
        ImGui::EndChild();
        return;
    }

    // ── Buttons ──────────────────────────────────────────────────────────
    ImGui::Text("Buttons (%u)", dev.buttons);
    ImGui::Separator();
    ImGui::Spacing();

    const UINT maxButtons = (dev.buttons < 32) ? dev.buttons : 32;
    const int  buttonsPerRow = 8;

    for (UINT i = 0; i < maxButtons; ++i) {
        bool pressed = (raw.buttons & (1u << i)) != 0;

        ImGui::PushStyleColor(ImGuiCol_Button,
            pressed ? ImVec4(0.15f, 0.75f, 0.15f, 1.0f)
                    : ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            pressed ? ImVec4(0.25f, 0.85f, 0.25f, 1.0f)
                    : ImVec4(0.28f, 0.28f, 0.30f, 1.0f));

        char label[6];
        snprintf(label, sizeof(label), "%u", i + 1);
        ImGui::Button(label, { 34.0f, 34.0f });
        ImGui::PopStyleColor(2);

        if ((i + 1) % buttonsPerRow != 0 && i + 1 < maxButtons)
            ImGui::SameLine(0.0f, 4.0f);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── Axes ─────────────────────────────────────────────────────────────
    ImGui::Text("Axes (%u)", dev.axes);
    ImGui::Separator();
    ImGui::Spacing();

    struct { const char* name; DWORD value; } axes[] = {
        { "X", raw.xpos }, { "Y", raw.ypos }, { "Z", raw.zpos },
        { "R", raw.rpos }, { "U", raw.upos }, { "V", raw.vpos },
    };

    const UINT numAxes = (dev.axes < 6) ? dev.axes : 6;
    const float barWidth = ImGui::GetContentRegionAvail().x - 40.0f;

    for (UINT i = 0; i < numAxes; ++i) {
        float v   = normalizeAxis(axes[i].value);
        float bar = (v + 1.0f) * 0.5f;  // remap [-1,1] → [0,1] for ProgressBar

        // Colour: neutral grey at center, shifts toward orange as it deviates
        float dev_f = fabsf(v);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            ImVec4(0.20f + dev_f * 0.60f, 0.55f - dev_f * 0.20f, 0.15f, 1.0f));

        ImGui::Text("%-2s", axes[i].name);
        ImGui::SameLine();

        char overlay[12];
        snprintf(overlay, sizeof(overlay), "%+.3f", v);
        ImGui::ProgressBar(bar, { barWidth, 18.0f }, overlay);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── POV ──────────────────────────────────────────────────────────────
    ImGui::Text("POV");
    ImGui::Separator();
    ImGui::Spacing();

    drawPOVCompass(raw.pov);
    ImGui::SameLine(0.0f, 16.0f);

    // Text annotation next to the compass
    ImGui::BeginGroup();
    ImGui::Spacing();
    if (raw.pov == JOY_POVCENTERED) {
        ImGui::TextDisabled("Center");
    } else {
        ImGui::Text("%s  (%u°)", povDirection(raw.pov), raw.pov / 100);
    }
    ImGui::EndGroup();

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Window / D3D11 initialisation
// ---------------------------------------------------------------------------

bool AppWindow::initWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VirtualPadWindow";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0, L"VirtualPadWindow", L"VirtualPad",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1000, 650,
        nullptr, nullptr, wc.hInstance, this);

    return m_hwnd != nullptr;
}

bool AppWindow::initD3D() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, 2, D3D11_SDK_VERSION,
        &sd, &m_swapChain, &m_device, &featureLevel, &m_context);

    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            featureLevels, 2, D3D11_SDK_VERSION,
            &sd, &m_swapChain, &m_device, &featureLevel, &m_context);

    if (FAILED(hr)) return false;

    createRenderTarget();
    return true;
}

void AppWindow::createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
        backBuffer->Release();
    }
}

void AppWindow::cleanupRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

void AppWindow::cleanup() {
    cleanupRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context)   { m_context->Release();   m_context   = nullptr; }
    if (m_device)    { m_device->Release();     m_device    = nullptr; }
    if (m_hwnd)      { DestroyWindow(m_hwnd);   m_hwnd      = nullptr; }
    UnregisterClassW(L"VirtualPadWindow", GetModuleHandle(nullptr));
}

// ---------------------------------------------------------------------------
// Win32 window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    AppWindow* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    case WM_SIZE:
        if (self && self->m_device && wParam != SIZE_MINIMIZED) {
            self->cleanupRenderTarget();
            self->m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                             DXGI_FORMAT_UNKNOWN, 0);
            self->createRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
