#include "AppWindow.h"

#include <algorithm>
#include <chrono>
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

    try {
        m_controllerConfigs = loadControllerConfigs("data/controllers.json");
    } catch (...) {}   // optional — scanner falls back to WinMM names if missing

    // Discover game profiles in data/ (any .json that has a profile_name field)
    m_profilePaths.clear();
    m_profileNames.clear();
    {
        WIN32_FIND_DATAA fd = {};
        HANDLE h = FindFirstFileA("data\\*.json", &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string fname = fd.cFileName;
                if (fname == "controllers.json" || fname == "macros.json" || fname == "virtualpad.json")
                    continue;
                std::string path = "data/" + fname;
                try {
                    GameProfile p = loadGameProfile(path);
                    if (!p.profile_name.empty()) {
                        m_profilePaths.push_back(path);
                        m_profileNames.push_back(p.profile_name);
                    }
                } catch (...) {}
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }

    m_padView.load(m_device);

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
        if (ImGui::BeginTabItem("Pad"))     { renderPadTab();     ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Engine tab
// ---------------------------------------------------------------------------

void AppWindow::renderEngineTab() {
    ImGui::Spacing();

    EnginePhase phase     = m_engine.getPhase();
    bool        connected = m_engine.isConnected();
    bool        running   = m_engine.isRunning();

    if (phase == EnginePhase::WaitingSelection) {
        ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "●");
        ImGui::SameLine();
        ImGui::Text("Multiple controllers detected — select one:");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto candidates = m_engine.getCandidates();
        for (int i = 0; i < (int)candidates.size(); ++i) {
            const auto& dev = candidates[i];
            const ControllerConfig* cfg = findConfig(m_controllerConfigs, dev.vid, dev.pid);
            const char* displayName = cfg ? cfg->source_name.c_str() : dev.name.c_str();
            const char* src = (dev.source == DeviceCandidate::Source::HID) ? "HID" : "WinMM";
            char label[256];
            snprintf(label, sizeof(label), "[%s]  %s    VID:%04X  PID:%04X",
                src, displayName, dev.vid, dev.pid);
            if (ImGui::Button(label))
                m_engine.selectDevice(i);
        }
    } else if (connected) {
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

    // ── Game profile selector ─────────────────────────────────────────────
    ImGui::Text("Game profile:");
    ImGui::SameLine();

    std::vector<const char*> profileItems;
    profileItems.push_back("(none)");
    for (const auto& n : m_profileNames)
        profileItems.push_back(n.c_str());

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("##profile", &m_profileSelected, profileItems.data(), (int)profileItems.size())) {
        if (m_profileSelected == 0)
            m_engine.setProfilePath("");
        else
            m_engine.setProfilePath(m_profilePaths[m_profileSelected - 1]);
    }

    // Show active profile name if the engine is running with one
    std::string activeName = m_engine.getActiveProfileName();
    if (!activeName.empty()) {
        ImGui::SameLine();
        ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.0f }, "(active: %s)", activeName.c_str());
    } else if (connected && m_profileSelected != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(reconnect to apply)");
    }

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
    ULONGLONG now  = GetTickCount64();
    uint16_t  vVid = m_engine.getVirtualVid();
    uint16_t  vPid = m_engine.getVirtualPid();

    // WinMM scan — fast, runs synchronously every second
    if (now - m_lastScanTime > 1000) {
        auto all = PadScanner::scan();
        m_scanDevices.clear();
        for (auto& d : all) {
            if (vVid && d.vid == vVid && d.pid == vPid) continue; // skip virtual pad
            const ControllerConfig* dcfg = findConfig(m_controllerConfigs, d.vid, d.pid);
            if (dcfg && dcfg->mode == "hid") continue;           // HID devices go to HID section
            m_scanDevices.push_back(d);
        }
        m_lastScanTime = now;
        if (m_scanSelected >= (int)m_scanDevices.size()) m_scanSelected = -1;
    }

    // HID scan — slow (opens every HID device), runs on a background thread
    auto kickHidScan = [&]() {
        if (!m_hidScanRunning.exchange(true)) {
            m_lastHidScanTime = now;
            m_hidScanFuture = std::async(std::launch::async, HIDScanner::scan);
        }
    };
    if (now - m_lastHidScanTime > 1000)
        kickHidScan();

    // Apply HID results as soon as the background scan completes
    if (m_hidScanRunning && m_hidScanFuture.valid() &&
        m_hidScanFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto raw = m_hidScanFuture.get();
        // Remove virtual pad and devices already visible via WinMM
        raw.erase(std::remove_if(raw.begin(), raw.end(), [&](const HIDScanner::DeviceInfo& h) {
            if (vVid && h.vid == vVid && h.pid == vPid) return true;
            for (auto& w : m_scanDevices)
                if (w.vid == h.vid && w.pid == h.pid) return true;
            return false;
        }), raw.end());
        m_hidDevices = std::move(raw);
        if (m_hidSelected >= (int)m_hidDevices.size()) m_hidSelected = -1;
        m_hidScanRunning = false;
    }

    ImGui::Spacing();

    // ── Splitter ─────────────────────────────────────────────────────────
    const float splitterW  = 6.0f;
    const float minPanelW  = 120.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    m_scanSplitX = std::clamp(m_scanSplitX, minPanelW, availW - minPanelW - splitterW);

    // ── Left panel: device list ──────────────────────────────────────────
    ImGui::BeginChild("##DeviceList", { m_scanSplitX, 0.0f }, true);

    ImGui::Text("WinMM (%zu)", m_scanDevices.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        auto all = PadScanner::scan();
        m_scanDevices.clear();
        for (auto& d : all) {
            if (vVid && d.vid == vVid && d.pid == vPid) continue;
            const ControllerConfig* dcfg = findConfig(m_controllerConfigs, d.vid, d.pid);
            if (dcfg && dcfg->mode == "hid") continue;
            m_scanDevices.push_back(d);
        }
        m_lastScanTime = now;
        if (m_scanSelected >= (int)m_scanDevices.size()) m_scanSelected = -1;
        kickHidScan();
    }
    ImGui::Separator();

    if (m_scanDevices.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No WinMM devices found.");
    } else {
        for (int i = 0; i < (int)m_scanDevices.size(); ++i) {
            const auto& dev = m_scanDevices[i];
            const ControllerConfig* cfg = findConfig(m_controllerConfigs, dev.vid, dev.pid);
            char label[128];
            if (cfg)
                snprintf(label, sizeof(label), "[%u] %s", dev.port, cfg->source_name.c_str());
            else
                snprintf(label, sizeof(label), "[%u] %ls", dev.port, dev.name);

            bool sel = (m_scanSelected == i);
            if (ImGui::Selectable(label, sel, 0, { 0, 0 })) {
                m_scanSelected = i;
                m_hidSelected  = -1;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("  VID:%04X PID:%04X  %uax %ubtn",
                dev.vid, dev.pid, dev.axes, dev.buttons);
        }
    }

    // ── HID-only devices ──────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Text("HID-only (%zu)", m_hidDevices.size());
    ImGui::Separator();

    if (m_hidDevices.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No HID-only devices found.");
    } else {
        for (int i = 0; i < (int)m_hidDevices.size(); ++i) {
            const auto& dev = m_hidDevices[i];
            const ControllerConfig* cfg = findConfig(m_controllerConfigs, dev.vid, dev.pid);
            char label[128];
            if (cfg)
                snprintf(label, sizeof(label), "[HID] %s", cfg->source_name.c_str());
            else if (!dev.productName.empty())
                snprintf(label, sizeof(label), "[HID] %s", dev.productName.c_str());
            else
                snprintf(label, sizeof(label), "[HID] VID:%04X PID:%04X", dev.vid, dev.pid);

            bool sel = (m_hidSelected == i);
            if (ImGui::Selectable(label, sel, 0, { 0, 0 })) {
                m_hidSelected  = i;
                m_scanSelected = -1;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("  VID:%04X PID:%04X", dev.vid, dev.pid);
        }
    }

    ImGui::EndChild();

    // ── Draggable splitter handle ─────────────────────────────────────────
    ImGui::SameLine();
    ImGui::InvisibleButton("##splitter", { splitterW, -1.0f });
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
        m_scanSplitX += ImGui::GetIO().MouseDelta.x;

    ImGui::SameLine();

    // ── Right panel: input monitor ───────────────────────────────────────
    ImGui::BeginChild("##InputMonitor", { 0.0f, 0.0f }, true);

    // ── HID device live monitor ───────────────────────────────────────────
    if (m_hidSelected >= 0 && m_hidSelected < (int)m_hidDevices.size()) {
        const auto& hdev = m_hidDevices[m_hidSelected];
        const ControllerConfig* cfg = findConfig(m_controllerConfigs, hdev.vid, hdev.pid);

        // Header
        ImGui::Spacing();
        ImGui::Text("%s", hdev.productName.empty() ? "HID Device" : hdev.productName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("VID:%04X PID:%04X", hdev.vid, hdev.pid);
        if (cfg)
            ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "Config: %s", cfg->source_name.c_str());
        else {
            ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "No config");
            ImGui::TextDisabled("Add to controllers.json: vid \"%04X\" pid \"%04X\" mode \"hid\"", hdev.vid, hdev.pid);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Read live state from the engine (avoids competing with it on Bluetooth HID)
        if (!m_engine.isConnected()) {
            ImGui::TextDisabled("Engine not running — start the engine to monitor inputs.");
            ImGui::EndChild();
            return;
        }

        m_hidScanState = m_engine.getLastState();
        // Reconstruct a button mask from the mapped state for the button grid
        DWORD btns = 0;
        if (m_hidScanState.btnA)     btns |= (1u << 1);
        if (m_hidScanState.btnB)     btns |= (1u << 0);
        if (m_hidScanState.btnX)     btns |= (1u << 4);
        if (m_hidScanState.btnY)     btns |= (1u << 3);
        if (m_hidScanState.btnLB)    btns |= (1u << 6);
        if (m_hidScanState.btnRB)    btns |= (1u << 7);
        if (m_hidScanState.btnBack)  btns |= (1u << 10);
        if (m_hidScanState.btnStart) btns |= (1u << 11);
        if (m_hidScanState.btnHome)  btns |= (1u << 12);
        if (m_hidScanState.btnL3)    btns |= (1u << 13);
        if (m_hidScanState.btnR3)    btns |= (1u << 14);
        if (m_hidScanState.btnLP)    btns |= (1u << 15);
        if (m_hidScanState.btnRP)    btns |= (1u << 16);
        if (m_hidScanState.btnL4)    btns |= (1u << 17);
        if (m_hidScanState.btnR4)    btns |= (1u << 18);
        const float barW = ImGui::GetContentRegionAvail().x - 60.0f;

        // ── Buttons ──────────────────────────────────────────────────────
        ImGui::Text("Buttons");
        ImGui::Separator();
        ImGui::Spacing();
        for (int i = 0; i < 19; ++i) {
            bool pressed = (btns & (1u << i)) != 0;
            ImGui::PushStyleColor(ImGuiCol_Button,
                pressed ? ImVec4(0.15f, 0.75f, 0.15f, 1.0f) : ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                pressed ? ImVec4(0.25f, 0.85f, 0.25f, 1.0f) : ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
            char lbl[4]; snprintf(lbl, sizeof(lbl), "%d", i + 1);
            ImGui::Button(lbl, { 34.0f, 34.0f });
            ImGui::PopStyleColor(2);
            if ((i + 1) % 8 != 0) ImGui::SameLine(0.0f, 4.0f);
        }

        // ── Sticks ───────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Sticks");
        ImGui::Separator();
        ImGui::Spacing();

        struct { const char* name; float v; } sticks[] = {
            { "LX", m_hidScanState.leftX  }, { "LY", m_hidScanState.leftY  },
            { "RX", m_hidScanState.rightX }, { "RY", m_hidScanState.rightY },
        };
        for (auto& s : sticks) {
            float dev_f = fabsf(s.v);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                ImVec4(0.20f + dev_f * 0.60f, 0.55f - dev_f * 0.20f, 0.15f, 1.0f));
            ImGui::Text("%-2s", s.name);
            ImGui::SameLine();
            char ov[12]; snprintf(ov, sizeof(ov), "%+.3f", s.v);
            ImGui::ProgressBar((s.v + 1.0f) * 0.5f, { barW, 18.0f }, ov);
            ImGui::PopStyleColor();
        }

        // ── Triggers ─────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Triggers");
        ImGui::Separator();
        ImGui::Spacing();

        struct { const char* name; float v; } triggers[] = {
            { "L2", m_hidScanState.triggerL },
            { "R2", m_hidScanState.triggerR },
        };
        for (auto& t : triggers) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                ImVec4(0.20f + t.v * 0.60f, 0.55f - t.v * 0.20f, 0.15f, 1.0f));
            ImGui::Text("%-2s", t.name);
            ImGui::SameLine();
            char ov[12]; snprintf(ov, sizeof(ov), "%.3f", t.v);
            ImGui::ProgressBar(t.v, { barW, 18.0f }, ov);
            ImGui::PopStyleColor();
        }

        // ── D-pad ─────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("D-pad");
        ImGui::Separator();
        ImGui::Spacing();
        // Convert booleans to a POV value for the existing compass widget
        DWORD pov = JOY_POVCENTERED;
        if      ( m_hidScanState.dpadUp   && !m_hidScanState.dpadRight && !m_hidScanState.dpadLeft)  pov = 0;
        else if ( m_hidScanState.dpadUp   &&  m_hidScanState.dpadRight)                              pov = 4500;
        else if (!m_hidScanState.dpadUp   &&  m_hidScanState.dpadRight && !m_hidScanState.dpadDown)  pov = 9000;
        else if ( m_hidScanState.dpadDown &&  m_hidScanState.dpadRight)                              pov = 13500;
        else if ( m_hidScanState.dpadDown && !m_hidScanState.dpadRight && !m_hidScanState.dpadLeft)  pov = 18000;
        else if ( m_hidScanState.dpadDown &&  m_hidScanState.dpadLeft)                               pov = 22500;
        else if (!m_hidScanState.dpadDown &&  m_hidScanState.dpadLeft  && !m_hidScanState.dpadUp)    pov = 27000;
        else if ( m_hidScanState.dpadUp   &&  m_hidScanState.dpadLeft)                               pov = 31500;
        drawPOVCompass(pov);

        ImGui::EndChild();
        return;
    }

    // ── WinMM device input monitor ────────────────────────────────────────
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

void AppWindow::renderPadTab() {
    ImGui::Spacing();
    m_padView.render(m_engine.getLastState());
}

void AppWindow::cleanup() {
    m_padView.unload();
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
