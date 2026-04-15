#include "AppWindow.h"

#include <algorithm>
#include <fstream>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include <chrono>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <cstdio>
#include <cmath>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "ui/ActionPanel.h"

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

    try {
        VirtualPadConfig vpCfg = loadVirtualPadConfig("data/virtualpad.json");
        m_acceptedXboxButtons  = vpCfg.acceptedXboxButtons;
        m_stickSelectThreshold = vpCfg.stickSelectThreshold;
        m_stickHoldMs          = vpCfg.stickHoldMs;
    } catch (...) {}  // struct defaults apply if file is missing or malformed

    try { m_padLayouts = loadPadLayouts("data/pad_layouts.json"); } catch (...) {}
    if (m_padLayouts.empty()) {
        try { m_padLayouts = loadPadLayouts("data/pad_layouts.json.bak"); } catch (...) {}
        m_layoutsFromBackup = !m_padLayouts.empty();
    }

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
    m_virtualPadView.load(m_device);
    m_layoutEditor.init(m_device, &m_padLayouts);

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
        if (ImGui::BeginTabItem("Pads"))    { renderPadsTab();    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Layout"))  { renderLayoutTab();  ImGui::EndTabItem(); }
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

    // ── Status indicator ──────────────────────────────────────────────────
    if (connected) {
        ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "●");
        ImGui::SameLine();
        ImGui::Text("Connected — %s", m_engine.getDevice().c_str());
    } else if (phase == EnginePhase::WaitingSelection) {
        ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "●");
        ImGui::SameLine();
        ImGui::Text("Select a controller:");
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
    ImGui::TextDisabled("Status: %s", m_engine.getStatus().c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Device list ───────────────────────────────────────────────────────
    // WaitingSelection uses the candidates snapshot; all other states use the
    // live monitor list so newly connected devices appear without a restart.
    auto availableDevices = m_engine.getAvailableDevices();
    auto candidates       = m_engine.getCandidates();
    DeviceCandidate activeDevice = m_engine.getActiveDevice();

    const std::vector<DeviceCandidate>& displayList =
        (phase == EnginePhase::WaitingSelection && !candidates.empty())
        ? candidates : availableDevices;

    if (displayList.empty()) {
        ImGui::TextDisabled("  No controllers detected");
    } else {
        for (int i = 0; i < (int)displayList.size(); ++i) {
            const auto& dev = displayList[i];
            const ControllerConfig* cfg = findConfig(m_controllerConfigs, dev.vid, dev.pid,
                                                     dev.connectionType);
            const char* displayName = cfg ? cfg->source_name.c_str() : dev.name.c_str();
            const char* src = (dev.source == DeviceCandidate::Source::HID) ? "HID" : "WinMM";

            // Determine if this entry is the currently active device
            bool isActive = (dev.vid == activeDevice.vid && dev.pid == activeDevice.pid
                          && dev.source == activeDevice.source);
            if (isActive && dev.source == DeviceCandidate::Source::HID)
                isActive = (dev.hidPath == activeDevice.hidPath);
            if (isActive && dev.source == DeviceCandidate::Source::WinMM)
                isActive = (dev.port == activeDevice.port);

            if (isActive) {
                ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "  >");
                ImGui::SameLine();
                ImGui::Text("[%s]  %s    VID:%04X  PID:%04X", src, displayName, dev.vid, dev.pid);
            } else {
                ImGui::Text("   ");
                ImGui::SameLine();
                ImGui::Text("[%s]  %s    VID:%04X  PID:%04X", src, displayName, dev.vid, dev.pid);
                ImGui::SameLine();

                char btnLabel[64];
                snprintf(btnLabel, sizeof(btnLabel), "Activar##dev%d", i);

                if (phase == EnginePhase::WaitingSelection) {
                    if (ImGui::SmallButton(btnLabel))
                        m_engine.selectDevice(i);
                } else if (connected) {
                    if (ImGui::SmallButton(btnLabel))
                        m_engine.requestSwitch(i);
                } else {
                    ImGui::BeginDisabled();
                    ImGui::SmallButton(btnLabel);
                    ImGui::EndDisabled();
                }
            }
        }
    }

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
        const ControllerConfig* cfg = findConfig(m_controllerConfigs, hdev.vid, hdev.pid,
                                                 hdev.connectionType);

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

        // Gyroscope (only shown when the device reports IMU data)
        if (m_hidScanState.gyroActive) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Text("Gyroscope");
            ImGui::Separator();
            ImGui::Spacing();
            struct { const char* name; float v; } axes[] = {
                { "X", m_hidScanState.gyroX },
                { "Y", m_hidScanState.gyroY },
                { "Z", m_hidScanState.gyroZ },
            };
            for (auto& a : axes) {
                float dev_f = fabsf(a.v);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                    ImVec4(0.20f + dev_f * 0.60f, 0.55f - dev_f * 0.20f, 0.60f, 1.0f));
                ImGui::Text("%-2s", a.name);
                ImGui::SameLine();
                char ov[12]; snprintf(ov, sizeof(ov), "%+.4f", a.v);
                ImGui::ProgressBar((a.v + 1.0f) * 0.5f, { barW, 18.0f }, ov);
                ImGui::PopStyleColor();
            }
        }

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
        100, 100, 1150, 780,
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

void AppWindow::renderPadsTab() {
    // Physical pad: update layout when the active controller changes, or when
    // the editor has just saved (forceSetLayout bypasses the id-cache guard).
    std::string layoutId = m_engine.getActiveLayoutId();
    if (m_forceLayoutReload || layoutId != m_currentLayoutId) {
        m_currentLayoutId   = layoutId;
        m_forceLayoutReload = false;
        const PadLayout* layout = findLayout(m_padLayouts, layoutId);
        if (layout)
            m_padView.forceSetLayout(*layout);
    }

    // Virtual pad: always xbox_one, loaded once
    if (!m_virtualPadInitialized) {
        const PadLayout* vLayout = findLayout(m_padLayouts, "xbox_one");
        if (vLayout) {
            m_virtualPadView.setLayout(*vLayout);
            m_virtualPadInitialized = true;
        }
    }

    {
      if (!m_mappingActive) {
        ImGui::Spacing();

        ImGui::BeginGroup();
        m_padView.render(m_engine.getLastState());
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 10.0f);
        ImGui::BeginGroup();
        {
            if (!m_arrowRightTex.valid())
                PadView::loadPng(m_device, "images/decorations/ArrowRight.png", m_arrowRightTex);
            const auto& L = m_padView.getLayout();
            constexpr float kArrowSize = 40.0f;
            float push = (L.FrontH + L.TopH) * 0.5f - kArrowSize * 0.5f;
            if (push > 0.0f) ImGui::Dummy({ 0.0f, push });
            if (m_arrowRightTex.valid())
                ImGui::Image((ImTextureID)m_arrowRightTex.srv, { kArrowSize, kArrowSize });
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 10.0f);

        ImGui::BeginGroup();
        m_virtualPadView.render(m_engine.getLastVirtualState());
        ImGui::EndGroup();

            // ── Marquee ───────────────────────────────────────────────────────────────

    for (const auto& ev : m_engine.pollEvents()) {
        MarqueeEntry entry;
        switch (ev.type) {
            case PadEventType::BotToggle:
                entry.type = ev.active ? MarqueeEntryType::BotOn : MarqueeEntryType::BotOff;
                entry.text = std::string("[BOT]   ") + ev.name + (ev.active ? "  ON" : "  OFF");
                break;
            case PadEventType::MacroToggle:
                entry.type = MarqueeEntryType::Macro;
                entry.text = std::string("[MACRO] ") + ev.name + (ev.active ? "  ON" : "  OFF");
                break;
            case PadEventType::KeyboardAction:
                entry.type = MarqueeEntryType::Keyboard;
                entry.text = std::string("[KB]    ") + ev.name;
                break;
            case PadEventType::MouseAction:
                entry.type = MarqueeEntryType::Mouse;
                entry.text = std::string("[MOUSE] ") + ev.name;
                break;
        }
        m_marqueeLines.push_back(entry);
        if (m_marqueeLines.size() > 4) m_marqueeLines.pop_front();
    }

    // 3. Render — always 4 slots so the area height is constant from the first entry
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Colors: Macro=yellow, BotOn=blue, BotOff=naranja, KB=cyan, Mouse=verde claro
    static const ImVec4 kColMacro    = { 1.00f, 0.85f, 0.00f, 1.0f };
    static const ImVec4 kColBotOn    = { 0.30f, 0.60f, 1.00f, 1.0f };
    static const ImVec4 kColBotOff   = { 1.00f, 0.55f, 0.10f, 1.0f };
    static const ImVec4 kColKeyboard = { 0.40f, 0.95f, 0.95f, 1.0f };
    static const ImVec4 kColMouse    = { 0.60f, 0.95f, 0.60f, 1.0f };

    const int n = (int)m_marqueeLines.size();
    for (int slot = 0; slot < 4; ++slot) {
        if (slot < n) {
            const auto& entry = m_marqueeLines[slot];
            ImVec4 col;
            switch (entry.type) {
                case MarqueeEntryType::Macro:    col = kColMacro;    break;
                case MarqueeEntryType::BotOn:    col = kColBotOn;    break;
                case MarqueeEntryType::BotOff:   col = kColBotOff;   break;
                case MarqueeEntryType::Keyboard: col = kColKeyboard; break;
                case MarqueeEntryType::Mouse:    col = kColMouse;    break;
                default:                         col = kColMacro;    break;
            }
            // Fade: slot 0 (oldest) = 0.25 alpha, slot 3 (newest) = 1.0 — fixed scale of 4
            col.w = 0.25f + 0.75f * ((float)(slot + 1) / 4.0f);
            ImGui::TextColored(col, "%s", entry.text.c_str());
        } else {
            // Empty slot — reserve the line height so the layout doesn't jump
            ImGui::Dummy({ 1.0f, ImGui::GetTextLineHeight() });
        }
    }

        // ── Botón Mapear ─────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Mapear##openMapping", { 120.0f, 0.0f }))
            m_mappingActive = true;
      } // !m_mappingActive
    }

    if (m_mappingActive)
        renderMappingSubtab();
}

// ---------------------------------------------------------------------------
// Mapping subtab — helpers
// ---------------------------------------------------------------------------

// imguiKeyToKeyName moved to ui/ActionPanel.cpp

// H9/H6: reads the X/Y float values for a stick component given its stateX field name.
static void readStickXY(const GamepadState& s, const std::string& stateX, float& outX, float& outY) {
    if (stateX == "leftX")  { outX = s.leftX;  outY = s.leftY;  return; }
    if (stateX == "rightX") { outX = s.rightX; outY = s.rightY; return; }
    outX = outY = 0.0f;
}

// H6: returns (xStickId, yStickId) from a stick component's stateX field
static std::pair<std::string,std::string> stickIdsFromStateX(const std::string& stateX) {
    if (stateX == "leftX")  return {"left_x",  "left_y"};
    if (stateX == "rightX") return {"right_x", "right_y"};
    return {"", ""};
}

// Dpad helpers
static std::string dpadDirToState(const PadComponent& c, const std::string& dir) {
    if (dir == "up")    return c.stateUp;
    if (dir == "down")  return c.stateDown;
    if (dir == "left")  return c.stateLeft;
    if (dir == "right") return c.stateRight;
    return {};
}
static std::string dpadDirFromMouse(ImVec2 mouse, float cx, float cy) {
    float dx = mouse.x - cx;
    float dy = mouse.y - cy;
    return (std::abs(dx) >= std::abs(dy))
        ? (dx >= 0 ? "right" : "left")
        : (dy >= 0 ? "down"  : "up");
}

// H6: human-readable label for an Xbox button short name
static const char* xboxBtnLabel(const std::string& key) {
    static const std::pair<const char*, const char*> kLabels[] = {
        {"a","A"}, {"b","B"}, {"x","X"}, {"y","Y"},
        {"l1","LB"}, {"r1","RB"}, {"l3","L3"}, {"r3","R3"},
        {"select","Select"}, {"start","Start"}, {"home","Home"},
    };
    for (auto& [k, v] : kLabels) if (key == k) return v;
    return key.c_str();
}

static bool isStateActive(const GamepadState& s, const std::string& n) {
    if (n == "btnA")     return s.btnA;
    if (n == "btnB")     return s.btnB;
    if (n == "btnX")     return s.btnX;
    if (n == "btnY")     return s.btnY;
    if (n == "btnLB")    return s.btnLB;
    if (n == "btnRB")    return s.btnRB;
    if (n == "btnBack")  return s.btnBack;
    if (n == "btnStart") return s.btnStart;
    if (n == "btnHome")  return s.btnHome;
    if (n == "btnL3")    return s.btnL3;
    if (n == "btnR3")    return s.btnR3;
    if (n == "btnL4")    return s.btnL4;
    if (n == "btnR4")    return s.btnR4;
    if (n == "btnLP")    return s.btnLP;
    if (n == "btnRP")    return s.btnRP;
    if (n == "btnTouch") return s.btnTouch;
    if (n == "dpadUp")   return s.dpadUp;
    if (n == "dpadDown") return s.dpadDown;
    if (n == "dpadLeft") return s.dpadLeft;
    if (n == "dpadRight")return s.dpadRight;
    return false;
}

static const std::pair<const char*, const char*> kBtnNames[] = {
    {"a",         "btnA"},    {"b",         "btnB"},
    {"x",         "btnX"},    {"y",         "btnY"},
    {"l1",        "btnLB"},   {"r1",        "btnRB"},
    {"select",    "btnBack"}, {"start",     "btnStart"},
    {"home",      "btnHome"}, {"l3",        "btnL3"},
    {"r3",        "btnR3"},   {"l4",        "btnL4"},
    {"r4",        "btnR4"},   {"lp",        "btnLP"},
    {"rp",        "btnRP"},   {"touch_btn", "btnTouch"},
    {"dpad_up",   "dpadUp"},  {"dpad_down", "dpadDown"},
    {"dpad_left", "dpadLeft"},{"dpad_right","dpadRight"},
};
static std::string shortToState(const std::string& s) {
    for (auto& [k, v] : kBtnNames) if (k == s) return v;
    return s;
}
static std::string stateToShort(const std::string& s) {
    for (auto& [k, v] : kBtnNames) if (v == s) return k;
    return s;
}
static int findCompByState(const PadLayout& layout, const std::string& stateName) {
    for (int i = 0; i < (int)layout.components.size(); ++i) {
        const PadComponent& c = layout.components[i];
        if (c.state == stateName) return i;
        // L3/R3: buscar por stateClick en sticks
        if (c.type == "stick" && c.stateClick == stateName) return i;
        // Dpad: "dpadUp/Down/Left/Right" pertenecen al componente de tipo dpad
        if (c.type == "dpad" && stateName.rfind("dpad", 0) == 0) return i;
    }
    return -1;
}
static void activateState(GamepadState& s, const std::string& name) {
    if      (name == "btnA")      s.btnA      = true;
    else if (name == "btnB")      s.btnB      = true;
    else if (name == "btnX")      s.btnX      = true;
    else if (name == "btnY")      s.btnY      = true;
    else if (name == "btnLB")     s.btnLB     = true;
    else if (name == "btnRB")     s.btnRB     = true;
    else if (name == "btnL3")     s.btnL3     = true;
    else if (name == "btnR3")     s.btnR3     = true;
    else if (name == "btnBack")   s.btnBack   = true;
    else if (name == "btnStart")  s.btnStart  = true;
    else if (name == "btnHome")   s.btnHome   = true;
    else if (name == "btnTouch")  s.btnTouch  = true;
    else if (name == "btnL4")     s.btnL4     = true;
    else if (name == "btnR4")     s.btnR4     = true;
    else if (name == "btnLP")     s.btnLP     = true;
    else if (name == "btnRP")     s.btnRP     = true;
    else if (name == "triggerL")  s.triggerL  = 1.0f;
    else if (name == "triggerR")  s.triggerR  = 1.0f;
    else if (name == "dpadUp")    s.dpadUp    = true;
    else if (name == "dpadDown")  s.dpadDown  = true;
    else if (name == "dpadLeft")  s.dpadLeft  = true;
    else if (name == "dpadRight") s.dpadRight = true;
}

// ---------------------------------------------------------------------------
// Mapping subtab
// ---------------------------------------------------------------------------

void AppWindow::reloadMappingEdits() {
    m_mappingEdits.clear();
    m_h5ActionEdits.clear();
    m_h6AxisEdits.clear();
    m_trigActionEdits.clear();
    m_trigLRangeEdits.clear();
    m_trigRRangeEdits.clear();
    m_selTriggerSrc.clear();
    m_h9HoldTriggerSrc.clear();
    m_h9HoldTriggerTimer = 0.0f;
    for (const auto& cfg : m_controllerConfigs) {
        if (cfg.vid == m_mappingActiveVid && cfg.pid == m_mappingActivePid) {
            for (const auto& [idx, action] : cfg.buttons) {
                if (action.physical.empty()) continue;
                switch (action.type) {
                case ButtonActionType::VirtualButton:
                    if (!action.name.empty() && action.physical != action.name)
                        m_mappingEdits[action.physical] = action.name;
                    break;
                case ButtonActionType::Keyboard:
                case ButtonActionType::MouseClick:
                case ButtonActionType::Macro:
                case ButtonActionType::Trigger:
                    m_h5ActionEdits[action.physical] = action;
                    break;
                default: break;
                }
            }
            for (const auto& [dir, vShort] : cfg.dpadRemap)
                m_mappingEdits["dpad_" + dir] = vShort;
            for (const auto& [dir, action] : cfg.dpadActions)
                m_h5ActionEdits["dpad_" + dir] = action;
            if (cfg.triggerLHasAction) m_trigActionEdits["l2"] = cfg.triggerLAction;
            if (cfg.triggerRHasAction) m_trigActionEdits["r2"] = cfg.triggerRAction;
            // Load range edits
            auto loadRanges = [](const std::vector<TriggerRange>& src,
                                  std::vector<RangeEdit>& dst) {
                dst.clear();
                for (const auto& r : src) {
                    RangeEdit re;
                    re.from      = r.from;
                    re.to        = r.to;
                    re.action    = r.action;
                    re.hasAction = r.hasAction;
                    dst.push_back(re);
                }
            };
            loadRanges(cfg.triggerLRanges, m_trigLRangeEdits);
            loadRanges(cfg.triggerRRanges, m_trigRRangeEdits);
            break;
        }
    }
}

void AppWindow::renderMappingSubtab() {
    // ── Pre-populate edits cuando cambia el mando activo ─────────────────────
    DeviceCandidate dev = m_engine.getActiveDevice();
    if (dev.vid != m_mappingActiveVid || dev.pid != m_mappingActivePid) {
        m_mappingActiveVid   = dev.vid;
        m_mappingActivePid   = dev.pid;
        m_mappingSelPhysComp = -1;
        reloadMappingEdits();
    }

    ImGui::Spacing();
    ImVec2 mouse        = ImGui::GetIO().MousePos;
    bool   mouseClicked = ImGui::IsMouseClicked(0);
    float  dt           = ImGui::GetIO().DeltaTime;

    // ── H9: lógica de mapping desde el mando ─────────────────────────────────
    GamepadState physNow = m_engine.getLastState();
    {
        const auto& physComps = m_padView.getLayout().components;

        if (m_h9ErrorTimer > 0.0f)
            m_h9ErrorTimer -= dt;

        if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty()) {
            // ── Paso 1a: stick al tope → seleccionar eje ──────────────────────
            int         activeStickComp = -1;
            std::string activeStickDir;
            for (int i = 0; i < (int)physComps.size(); ++i) {
                const PadComponent& c = physComps[i];
                if (c.type != "stick") continue;
                float x = 0.0f, y = 0.0f;
                readStickXY(physNow, c.stateX, x, y);
                std::string dir;
                if      (y >=  m_stickSelectThreshold) dir = "up";
                else if (y <= -m_stickSelectThreshold) dir = "down";
                else if (x <= -m_stickSelectThreshold) dir = "left";
                else if (x >=  m_stickSelectThreshold) dir = "right";
                if (!dir.empty()) { activeStickComp = i; activeStickDir = dir; break; }
            }

            if (activeStickComp >= 0) {
                if (m_h9HoldComp != activeStickComp || m_h9HoldStickDir != activeStickDir) {
                    m_h9HoldComp      = activeStickComp;
                    m_h9HoldStickDir  = activeStickDir;
                    m_h9HoldTimer     = 0.0f;
                } else {
                    m_h9HoldTimer += dt;
                    if (m_h9HoldTimer >= m_stickHoldMs / 1000.0f) {
                        m_mappingSelPhysComp = activeStickComp;
                        m_selStickDir        = activeStickDir;
                        m_selStickAsButton   = false;
                        m_h9HoldComp         = -1;
                        m_h9HoldStickDir.clear();
                        m_h9HoldTimer        = 0.0f;
                    }
                }
            } else {
                // ── Paso 1b: botón mantenido 1s → seleccionarlo ──
                // Incluye botones normales y L3/R3 (stateClick de sticks)
                if (!m_h9HoldStickDir.empty()) {
                    m_h9HoldComp = -1;
                    m_h9HoldStickDir.clear();
                    m_h9HoldDpadDir.clear();
                    m_h9HoldTimer = 0.0f;
                } else {
                    int  activeComp       = -1;
                    bool activeIsStickBtn = false;
                    std::string activeDpadDir;
                    for (int i = 0; i < (int)physComps.size(); ++i) {
                        const PadComponent& c = physComps[i];
                        if (c.type == "button" && isStateActive(physNow, c.state)) {
                            activeComp = i; activeIsStickBtn = false; break;
                        }
                        if (c.type == "stick" && !c.stateClick.empty() &&
                            isStateActive(physNow, c.stateClick)) {
                            activeComp = i; activeIsStickBtn = true; break;
                        }
                        if (c.type == "dpad") {
                            for (const char* d : {"up","down","left","right"}) {
                                std::string st = dpadDirToState(c, d);
                                if (!st.empty() && isStateActive(physNow, st)) {
                                    activeComp = i; activeDpadDir = d; break;
                                }
                            }
                            if (activeComp >= 0) break;
                        }
                    }
                    if (activeComp >= 0) {
                        if (m_h9HoldComp != activeComp) {
                            m_h9HoldComp      = activeComp;
                            m_h9HoldDpadDir   = activeDpadDir;
                            m_h9HoldTimer     = 0.0f;
                        } else {
                            m_h9HoldDpadDir = activeDpadDir;  // actualizar dir si cambia
                            m_h9HoldTimer += dt;
                            if (m_h9HoldTimer >= 1.0f) {
                                m_mappingSelPhysComp = activeComp;
                                m_selStickAsButton   = activeIsStickBtn;
                                m_selDpadDir         = activeDpadDir;
                                m_h5ActionType = H5ActionType::Xbox;
                                m_h9HoldComp    = -1;
                                m_h9HoldDpadDir.clear();
                                m_h9HoldTimer   = 0.0f;
                            }
                        }
                    } else {
                        m_h9HoldComp    = -1;
                        m_h9HoldDpadDir.clear();
                        m_h9HoldTimer   = 0.0f;
                        // ── Paso 1c: gatillo al tope 2s → seleccionar como fuente ──
                        constexpr float kTrigSelThresh = 0.75f;
                        if (physNow.triggerL > kTrigSelThresh || physNow.triggerR > kTrigSelThresh) {
                            std::string tSrc = (physNow.triggerL >= physNow.triggerR) ? "l2" : "r2";
                            if (m_h9HoldTriggerSrc != tSrc) {
                                m_h9HoldTriggerSrc  = tSrc;
                                m_h9HoldTriggerTimer = 0.0f;
                            } else {
                                m_h9HoldTriggerTimer += dt;
                                if (m_h9HoldTriggerTimer >= 2.0f) {
                                    m_selTriggerSrc      = tSrc;
                                    m_h5ActionType       = H5ActionType::Xbox;
                                    m_h5CaptureKeys.clear();
                                    m_h5MacroSel.clear();
                                    m_h9HoldTriggerSrc.clear();
                                    m_h9HoldTriggerTimer = 0.0f;
                                }
                            }
                        } else {
                            m_h9HoldTriggerSrc.clear();
                            m_h9HoldTriggerTimer = 0.0f;
                        }
                    }
                }
            }
        } else if (m_mappingSelPhysComp >= 0 && m_h5ActionType == H5ActionType::Xbox) {
            // Paso 2 (solo modo Xbox): detectar rising edge → asignar botón virtual
            const PadComponent& selComp2 = physComps[m_mappingSelPhysComp];
            std::string selState;
            if (m_selStickAsButton)
                selState = selComp2.stateClick;                        // L3/R3
            else if (selComp2.type == "dpad" && !m_selDpadDir.empty())
                selState = dpadDirToState(selComp2, m_selDpadDir);     // dpad direction
            else
                selState = selComp2.state;                             // botón normal
            std::string physShort = stateToShort(selState);

            // Iterar todos los "estados de botón" posibles: button, stateClick de sticks, dpad dirs
            // Construimos una lista plana de states candidatos
            std::vector<std::string> candidateStates;
            for (int i = 0; i < (int)physComps.size(); ++i) {
                const PadComponent& c = physComps[i];
                if (c.type == "button" && !c.state.empty())
                    candidateStates.push_back(c.state);
                else if (c.type == "stick" && !c.stateClick.empty())
                    candidateStates.push_back(c.stateClick);
                else if (c.type == "dpad") {
                    for (const char* d : {"up","down","left","right"}) {
                        std::string st = dpadDirToState(c, d);
                        if (!st.empty()) candidateStates.push_back(st);
                    }
                }
            }
            for (const auto& compState : candidateStates) {
                bool wasActive = isStateActive(m_h9PrevPhysState, compState);
                bool isActive  = isStateActive(physNow, compState);
                if (!isActive || wasActive) continue;  // solo rising edge

                std::string virtShort = stateToShort(compState);
                bool valid = false;
                for (const auto& s : m_acceptedXboxButtons) if (virtShort == s) { valid = true; break; }

                if (valid) {
                    if (!physShort.empty()) {
                        m_h5ActionEdits.erase(physShort);
                        auto it = m_mappingEdits.find(physShort);
                        bool alreadyAssigned = (it != m_mappingEdits.end() && it->second == virtShort);
                        m_mappingEdits[physShort] = alreadyAssigned ? "" : virtShort;
                        int flashComp = findCompByState(m_virtualPadView.getLayout(), shortToState(virtShort));
                        m_mappingFlashComp       = alreadyAssigned ? -1 : flashComp;
                        m_mappingFlashTimer      = alreadyAssigned ? 0.0f : 0.5f;
                        m_mappingFlashVirtShort  = alreadyAssigned ? "" : virtShort;
                    }
                    m_mappingSelPhysComp = -1;
                    m_selStickAsButton   = false;
                    m_selDpadDir.clear();
                    m_h5ActionType = H5ActionType::Xbox;
                } else {
                    // Botón sin equivalente Xbox — si el fuente tiene asignación, la borramos.
                    // Si no tiene nada, mostramos error.
                    bool hasAssignment = m_h5ActionEdits.count(physShort) > 0 ||
                        (m_mappingEdits.count(physShort) && !m_mappingEdits.at(physShort).empty());
                    if (hasAssignment && !physShort.empty()) {
                        m_mappingEdits[physShort] = "";  // marca la entrada como "borrada" para que save la procese
                        m_h5ActionEdits.erase(physShort);
                        m_mappingSelPhysComp = -1;
                        m_selStickAsButton   = false;
                        m_selDpadDir.clear();
                        m_h5ActionType = H5ActionType::Xbox;
                    } else {
                        m_h9ErrorTimer = 2.0f;
                    }
                }
                break;
            }

            // Physical L2/R2 → asignar componente seleccionado como gatillo virtual
            {
                constexpr float kTrigThresh = 0.5f;
                auto doTrigAssign = [&](const std::string& trigTarget, const std::string& trigState) {
                    if (physShort.empty()) return;
                    auto h5it = m_h5ActionEdits.find(physShort);
                    bool already = (h5it != m_h5ActionEdits.end() &&
                                    h5it->second.type == ButtonActionType::Trigger &&
                                    h5it->second.target == trigTarget);
                    if (already) {
                        m_h5ActionEdits.erase(physShort);
                        m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                    } else {
                        ButtonAction act;
                        act.type = ButtonActionType::Trigger; act.physical = physShort; act.target = trigTarget;
                        m_h5ActionEdits[physShort] = act;
                        m_mappingEdits.erase(physShort);
                        m_mappingFlashComp      = findCompByState(m_virtualPadView.getLayout(), trigState);
                        m_mappingFlashTimer     = 0.5f;
                        m_mappingFlashVirtShort = trigState;
                    }
                    m_mappingSelPhysComp = -1; m_selStickAsButton = false;
                    m_selDpadDir.clear(); m_h5ActionType = H5ActionType::Xbox;
                };
                if (physNow.triggerL > kTrigThresh && m_h9PrevPhysState.triggerL <= kTrigThresh)
                    doTrigAssign("l2", "triggerL");
                else if (physNow.triggerR > kTrigThresh && m_h9PrevPhysState.triggerR <= kTrigThresh)
                    doTrigAssign("r2", "triggerR");
            }
        } else if (!m_selTriggerSrc.empty() && m_h5ActionType == H5ActionType::Xbox) {
            // Paso 2 — gatillo como fuente: asignar target por botón/gatillo físico
            // Buttons: rising edge → VirtualButton target
            std::vector<std::string> candStates;
            for (int i = 0; i < (int)physComps.size(); ++i) {
                const PadComponent& c = physComps[i];
                if (c.type == "button" && !c.state.empty())
                    candStates.push_back(c.state);
                else if (c.type == "stick" && !c.stateClick.empty())
                    candStates.push_back(c.stateClick);
                else if (c.type == "dpad") {
                    for (const char* d : {"up","down","left","right"}) {
                        std::string st = dpadDirToState(c, d);
                        if (!st.empty()) candStates.push_back(st);
                    }
                }
            }
            for (const auto& cState : candStates) {
                if (!isStateActive(physNow, cState) || isStateActive(m_h9PrevPhysState, cState)) continue;
                std::string vShort = stateToShort(cState);
                bool valid = false;
                for (const auto& s : m_acceptedXboxButtons) if (vShort == s) { valid = true; break; }
                if (!valid) { m_h9ErrorTimer = 2.0f; break; }
                auto it = m_trigActionEdits.find(m_selTriggerSrc);
                bool already = (it != m_trigActionEdits.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == vShort);
                if (already) {
                    m_trigActionEdits.erase(m_selTriggerSrc);
                    m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = m_selTriggerSrc; act.name = vShort;
                    m_trigActionEdits[m_selTriggerSrc] = act;
                    m_mappingFlashComp = findCompByState(m_virtualPadView.getLayout(), shortToState(vShort));
                    m_mappingFlashTimer = 0.5f; m_mappingFlashVirtShort = shortToState(vShort);
                }
                m_selTriggerSrc.clear(); m_h5ActionType = H5ActionType::Xbox;
                break;
            }
            // Trigger press: rising edge → TriggerPassthrough target
            {
                constexpr float kTrigThresh2 = 0.5f;
                auto doTrigTgtAssign = [&](const std::string& trigTarget, const std::string& trigState) {
                    auto it = m_trigActionEdits.find(m_selTriggerSrc);
                    bool already = (it != m_trigActionEdits.end() &&
                                    it->second.type == ButtonActionType::TriggerPassthrough &&
                                    it->second.target == trigTarget);
                    if (already) {
                        m_trigActionEdits.erase(m_selTriggerSrc);
                        m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                    } else {
                        ButtonAction act;
                        act.type = ButtonActionType::TriggerPassthrough; act.physical = m_selTriggerSrc;
                        act.target = trigTarget;
                        m_trigActionEdits[m_selTriggerSrc] = act;
                        m_mappingFlashComp = findCompByState(m_virtualPadView.getLayout(), trigState);
                        m_mappingFlashTimer = 0.5f; m_mappingFlashVirtShort = trigState;
                    }
                    m_selTriggerSrc.clear(); m_h5ActionType = H5ActionType::Xbox;
                };
                if (!m_selTriggerSrc.empty()) {
                    if (physNow.triggerL > kTrigThresh2 && m_h9PrevPhysState.triggerL <= kTrigThresh2)
                        doTrigTgtAssign("l2", "triggerL");
                    else if (physNow.triggerR > kTrigThresh2 && m_h9PrevPhysState.triggerR <= kTrigThresh2)
                        doTrigTgtAssign("r2", "triggerR");
                }
            }
        }

        m_h9PrevPhysState = physNow;
    }

    // ── Construir estados de display ──────────────────────────────────────────
    m_mappingFlashTimer -= dt;
    if (m_mappingFlashTimer <= 0.0f) { m_mappingFlashComp = -1; m_mappingFlashVirtShort.clear(); }

    GamepadState physDisplay{};
    GamepadState virtDisplay{};
    // Durante H9 hold (antes de completar la selección): iluminar el componente que se mantiene pulsado
    if (m_mappingSelPhysComp < 0 && m_h9HoldComp >= 0) {
        const auto& physComps = m_padView.getLayout().components;
        if (m_h9HoldComp < (int)physComps.size()) {
            const PadComponent& heldComp = physComps[m_h9HoldComp];
            if (heldComp.type == "button")
                activateState(physDisplay, heldComp.state);
            else if (heldComp.type == "stick" && !m_h9HoldStickDir.empty())
                ; // stick en modo eje: no iluminar (las flechas de selección lo indicarán)
            else if (heldComp.type == "stick")
                activateState(physDisplay, heldComp.stateClick);  // L3/R3 hold
            else if (heldComp.type == "dpad" && !m_h9HoldDpadDir.empty()) {
                std::string dpadState = dpadDirToState(heldComp, m_h9HoldDpadDir);
                activateState(physDisplay, dpadState);
            }
        }
    }
    if (m_mappingSelPhysComp >= 0) {
        const auto& physComps = m_padView.getLayout().components;
        if (m_mappingSelPhysComp < (int)physComps.size()) {
            const PadComponent& selComp = physComps[m_mappingSelPhysComp];
            // Helper: activa trigger virtual si physShort tiene asignación de tipo Trigger
            auto activateTriggerIfAssigned = [&](const std::string& physShort) -> bool {
                auto h5trig = m_h5ActionEdits.find(physShort);
                if (h5trig != m_h5ActionEdits.end() && h5trig->second.type == ButtonActionType::Trigger) {
                    activateState(virtDisplay, h5trig->second.target == "l2" ? "triggerL" : "triggerR");
                    return true;
                }
                return false;
            };
            if (selComp.type == "button") {
                const std::string& physState = selComp.state;
                activateState(physDisplay, physState);
                std::string physShort = stateToShort(physState);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_mappingEdits.find(physShort);
                    std::string virtShort = (it != m_mappingEdits.end()) ? it->second : physShort;
                    activateState(virtDisplay, shortToState(virtShort));
                }
            } else if (selComp.type == "stick" && m_selStickAsButton) {
                // L3/R3: mostrar el click del stick iluminado
                activateState(physDisplay, selComp.stateClick);
                std::string physShort = stateToShort(selComp.stateClick);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_mappingEdits.find(physShort);
                    std::string virtShort = (it != m_mappingEdits.end()) ? it->second : physShort;
                    activateState(virtDisplay, shortToState(virtShort));
                }
            } else if (selComp.type == "stick") {
                // Modo eje: physDisplay a cero (las flechas ya indican el estado)
                (void)selComp;
            } else if (selComp.type == "dpad" && !m_selDpadDir.empty()) {
                std::string dpadState = dpadDirToState(selComp, m_selDpadDir);
                activateState(physDisplay, dpadState);
                std::string physShort = stateToShort(dpadState);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_mappingEdits.find(physShort);
                    std::string virtShort = (it != m_mappingEdits.end()) ? it->second : physShort;
                    activateState(virtDisplay, shortToState(virtShort));
                }
            }
        }
    }
    if (!m_selTriggerSrc.empty()) {
        // Trigger source selected: light up the physical trigger
        if (m_selTriggerSrc == "l2") physDisplay.triggerL = 1.0f;
        else                          physDisplay.triggerR = 1.0f;
        // Show assigned action in virtDisplay
        auto it = m_trigActionEdits.find(m_selTriggerSrc);
        if (it != m_trigActionEdits.end()) {
            const ButtonAction& act = it->second;
            if (act.type == ButtonActionType::TriggerPassthrough) {
                activateState(virtDisplay, act.target == "l2" ? "triggerL" : "triggerR");
            } else if (act.type == ButtonActionType::VirtualButton) {
                activateState(virtDisplay, shortToState(act.name));
            }
        }
    }
    // Flash de confirmación: ilumina el botón virtual recién asignado
    if (m_mappingFlashComp >= 0 && !m_mappingFlashVirtShort.empty())
        activateState(virtDisplay, shortToState(m_mappingFlashVirtShort));

    // ── Pad físico ────────────────────────────────────────────────────────────
    ImGui::BeginGroup();
    m_mappingPhysOrigin = ImGui::GetCursorScreenPos();
    m_padView.render(physDisplay);
    m_padView.renderStickArrows(m_mappingPhysOrigin, m_mappingSelPhysComp, m_selStickDir);
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextDisabled("Físico");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();
    {
        if (!m_arrowRightTex.valid())
            PadView::loadPng(m_device, "images/decorations/ArrowRight.png", m_arrowRightTex);
        const auto& L = m_padView.getLayout();
        constexpr float kArrowSize = 40.0f;
        float push = (L.FrontH + L.TopH) * 0.5f - kArrowSize * 0.5f;
        if (push > 0.0f) ImGui::Dummy({ 0.0f, push });
        if (m_arrowRightTex.valid())
            ImGui::Image((ImTextureID)m_arrowRightTex.srv, { kArrowSize, kArrowSize });
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);

    ImGui::BeginGroup();
    m_mappingVirtOrigin = ImGui::GetCursorScreenPos();
    m_virtualPadView.render(virtDisplay);
    m_virtualPadView.renderStickArrows(m_mappingVirtOrigin, -1, "");
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextDisabled("Virtual (Xbox One)");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();

    // ── Marcos de foco y texto instruccional ──────────────────────────────────
    {
        constexpr ImU32 kFrameColor = IM_COL32(255, 220, 0, 200);
        constexpr float kThickness  = 2.5f;
        constexpr float kPad        = 4.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const auto& physL = m_padView.getLayout();
        const auto& virtL = m_virtualPadView.getLayout();
        float physH = physL.FrontH + physL.TopH;
        float virtH = virtL.FrontH + virtL.TopH;

        if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty()) {
            // Reposo: marco alrededor del físico
            ImVec2 rMin = { m_mappingPhysOrigin.x - kPad, m_mappingPhysOrigin.y - kPad };
            ImVec2 rMax = { m_mappingPhysOrigin.x + physL.W + kPad, m_mappingPhysOrigin.y + physH + kPad };
            dl->AddRect(rMin, rMax, kFrameColor, 4.0f, 0, kThickness);
        } else {
            // Paso 2: marco alrededor del virtual
            ImVec2 rMin = { m_mappingVirtOrigin.x - kPad, m_mappingVirtOrigin.y - kPad };
            ImVec2 rMax = { m_mappingVirtOrigin.x + virtL.W + kPad, m_mappingVirtOrigin.y + virtH + kPad };
            dl->AddRect(rMin, rMax, kFrameColor, 4.0f, 0, kThickness);
        }
    }

    // Texto instruccional centrado debajo de los mandos
    ImGui::Spacing();
    {
        const char* msg;
        ImVec4      col = { 1.0f, 0.86f, 0.0f, 1.0f };  // amarillo
        if (m_h9ErrorTimer > 0.0f) {
            msg = "Ese botón no tiene equivalente Xbox — elige otro";
            col = { 1.0f, 0.3f, 0.3f, 1.0f };  // rojo
        } else if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty() && !m_h9HoldTriggerSrc.empty()) {
            msg = "Mant\xC3\xA9n el gatillo al tope para seleccionarlo como fuente";
        } else if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty() && m_h9HoldComp >= 0) {
            msg = m_h9HoldStickDir.empty()
                ? "Mant\xC3\xA9n pulsado para seleccionar"
                : "Mant\xC3\xA9n el stick al tope para seleccionar direcci\xC3\xB3n";
        } else if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty()) {
            msg = "Elige el bot\xC3\xB3n, cruceta, stick o gatillo que quieres reasignar";
        } else if (!m_selTriggerSrc.empty()) {
            if (m_h5ActionType == H5ActionType::Keyboard)
                msg = m_h5CaptureKeys.empty()
                    ? "Pulsa las teclas del combo  (L1+R1 o A+B para cancelar)"
                    : "Pulsa m\xC3\xA1s teclas o haz clic en Asignar";
            else if (m_h5ActionType == H5ActionType::Xbox)
                msg = "Haz clic en el bot\xC3\xB3n o gatillo virtual  \xE2\x80\x94  o pulsa el bot\xC3\xB3n f\xC3\xADsico";
            else
                msg = "Elige la acci\xC3\xB3n del gatillo en el panel";
        } else if (m_mappingSelPhysComp >= 0 &&
                   m_padView.getLayout().components[m_mappingSelPhysComp].type == "stick" &&
                   (!m_selStickAsButton || m_h5ActionType == H5ActionType::Xbox)) {
            // Stick en modo eje (dirección), o en modo botón pero solo si estamos en Xbox
            if (m_selStickAsButton)
                msg = "Elige en el virtual o pulsa el bot\xC3\xB3n f\xC3\xADsico que quieras asignarle";
            else
                msg = "Haz clic en el stick o cruceta virtual al que quieres asignar";
        } else if (m_h5ActionType == H5ActionType::Keyboard) {
            msg = m_h5CaptureKeys.empty()
                ? "Pulsa las teclas del combo  (L1+R1 o A+B para cancelar)"
                : "Pulsa m\xC3\xA1s teclas o haz clic en Asignar";
        } else {
            msg = "Elige bot\xC3\xB3n, stick o cruceta en el virtual  \xE2\x80\x94  o pulsa el bot\xC3\xB3n f\xC3\xADsico";
        }

        float availW = m_mappingVirtOrigin.x + m_virtualPadView.getLayout().W - m_mappingPhysOrigin.x;
        ImGui::SetWindowFontScale(1.35f);
        float textW  = ImGui::CalcTextSize(msg).x;
        float offsetX = (availW - textW) * 0.5f;
        if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::TextColored(col, "%s", msg);
        ImGui::SetWindowFontScale(1.0f);

        // Barra de progreso del hold
        if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty() && m_h9HoldComp >= 0 && m_h9HoldTimer > 0.0f) {
            constexpr float kBarW = 160.0f;
            float barOffX = (availW - kBarW) * 0.5f;
            if (barOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barOffX);
            float holdSec = m_h9HoldStickDir.empty() ? 1.0f : (m_stickHoldMs / 1000.0f);
            ImGui::ProgressBar(m_h9HoldTimer / holdSec, { kBarW, 6.0f }, "");
        }
        // Barra de progreso hold de gatillo
        if (m_mappingSelPhysComp < 0 && m_selTriggerSrc.empty() &&
            !m_h9HoldTriggerSrc.empty() && m_h9HoldTriggerTimer > 0.0f) {
            constexpr float kBarW = 160.0f;
            float barOffX = (availW - kBarW) * 0.5f;
            if (barOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barOffX);
            ImGui::ProgressBar(m_h9HoldTriggerTimer / 2.0f, { kBarW, 6.0f }, "");
        }
    }

    // ── H5/H6: UI específica según el tipo de componente seleccionado ──────────
    if (m_mappingSelPhysComp >= 0) {
        const auto& physComps = m_padView.getLayout().components;
        const std::string& selType = physComps[m_mappingSelPhysComp].type;
        ImGui::Spacing();
        float availW = m_mappingVirtOrigin.x + m_virtualPadView.getLayout().W - m_mappingPhysOrigin.x;

    if (selType != "stick" || m_selStickAsButton) {
        // ── H5: botón seleccionado (incluye L3/R3) ─────────────────────────
        // physShortSel: clave corta del componente físico seleccionado ("a", "l3", "dpad_up", etc.)
        const auto& selPhysComp = physComps[m_mappingSelPhysComp];
        const std::string physShortSel = (selType == "stick" && m_selStickAsButton)
            ? stateToShort(selPhysComp.stateClick)
            : (selType == "dpad")
                ? stateToShort(dpadDirToState(selPhysComp, m_selDpadDir))
                : stateToShort(selPhysComp.state);

        float btnW   = 90.0f;
        float totalW = btnW * 4 + ImGui::GetStyle().ItemSpacing.x * 3;
        float offX   = (availW - totalW) * 0.5f;
        if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);

        // Botones de tipo — el activo se muestra resaltado
        auto typeBtn = [&](const char* label, H5ActionType type) {
            bool sel = (m_h5ActionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_h5ActionType = type;
                m_h5CaptureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        typeBtn("Xbox##t0",    H5ActionType::Xbox);    ImGui::SameLine();
        typeBtn("Macro##t1",   H5ActionType::Macro);   ImGui::SameLine();
        typeBtn("Teclado##t2", H5ActionType::Keyboard); ImGui::SameLine();
        typeBtn("Ratón##t3",   H5ActionType::Mouse);

        // ── UI por tipo ───────────────────────────────────────────────────────
        ImGui::Spacing();

        if (m_h5ActionType == H5ActionType::Macro) {
            if (!m_h5MacroNamesLoaded) {
                m_h5MacroNames.clear();
                try {
                    std::ifstream f("data/macros.json");
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k,v] : j.items()) m_h5MacroNames.push_back(k); }
                } catch (...) {}
                m_h5MacroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_h5", m_h5MacroSel, m_h5MacroNames, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type     = ButtonActionType::Macro;
                    act.physical = physShortSel;
                    act.name     = m_h5MacroSel;
                    m_h5ActionEdits[physShortSel] = act;
                    m_mappingEdits.erase(physShortSel);
                }
                m_mappingSelPhysComp = -1;
                m_selStickAsButton   = false;
                m_selDpadDir.clear();
                m_h5ActionType = H5ActionType::Xbox;
                m_h5MacroSel.clear();
            }

        } else if (m_h5ActionType == H5ActionType::Keyboard) {
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) {
                m_h5ActionType = H5ActionType::Xbox;
                m_h5CaptureKeys.clear();
            } else if (ActionPanel::renderKeyboardCapture("kb_h5", m_h5CaptureKeys, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type     = ButtonActionType::Keyboard;
                    act.physical = physShortSel;
                    for (const auto& p : m_h5CaptureKeys) act.keys.push_back(p.first);
                    m_h5ActionEdits[physShortSel] = act;
                    m_mappingEdits.erase(physShortSel);
                }
                m_mappingSelPhysComp = -1;
                m_selStickAsButton   = false;
                m_selDpadDir.clear();
                m_h5ActionType = H5ActionType::Xbox;
                m_h5CaptureKeys.clear();
            }

        } else if (m_h5ActionType == H5ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_h5", mbResult, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type        = ButtonActionType::MouseClick;
                    act.physical    = physShortSel;
                    act.mouseButton = mbResult;
                    m_h5ActionEdits[physShortSel] = act;
                    m_mappingEdits.erase(physShortSel);
                }
                m_mappingSelPhysComp = -1;
                m_selStickAsButton   = false;
                m_selDpadDir.clear();
                m_h5ActionType = H5ActionType::Xbox;
            }
        }
    } // else (selType == "button", H5)
    } // if (m_mappingSelPhysComp >= 0)

    // ── H7: UI para gatillo como fuente ──────────────────────────────────────
    if (!m_selTriggerSrc.empty()) {
        ImGui::Spacing();
        float availW = m_mappingVirtOrigin.x + m_virtualPadView.getLayout().W - m_mappingPhysOrigin.x;

        // Header: "L2 →" o "R2 →"
        {
            const char* lbl = (m_selTriggerSrc == "l2") ? "L2 \xe2\x86\x92" : "R2 \xe2\x86\x92";
            float hdrW = ImGui::CalcTextSize(lbl).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - hdrW) * 0.5f);
            ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl);
        }

        // Botones de tipo
        float btnW   = 90.0f;
        float totalW = btnW * 5 + ImGui::GetStyle().ItemSpacing.x * 4;
        float offX   = (availW - totalW) * 0.5f;
        if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
        auto typeBtn7 = [&](const char* label, H5ActionType type) {
            bool sel = (m_h5ActionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_h5ActionType = type;
                m_h5CaptureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        typeBtn7("Xbox/Anal.##h7t0", H5ActionType::Xbox);    ImGui::SameLine();
        typeBtn7("Macro##h7t1",      H5ActionType::Macro);   ImGui::SameLine();
        typeBtn7("Teclado##h7t2",    H5ActionType::Keyboard); ImGui::SameLine();
        typeBtn7("Rat\xC3\xB3n##h7t3", H5ActionType::Mouse); ImGui::SameLine();
        // Rangos button — opens the zones modal
        {
            const std::vector<RangeEdit>& curRanges = (m_selTriggerSrc == "l2") ? m_trigLRangeEdits : m_trigRRangeEdits;
            bool hasRanges = !curRanges.empty();
            if (hasRanges) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("Rangos##h7t4", { btnW, 0.0f })) {
                m_rangosForTrigger  = m_selTriggerSrc;
                m_rangosWork        = curRanges;
                m_rangosSelSect     = -1;
                m_rangosActType     = H5ActionType::Xbox;
                m_rangosCaptureKeys.clear();
                m_rangosMacroSel.clear();
                m_rangosXboxSel     = -1;
                // Neutralize the H7 background panel so it doesn't capture
                // keyboard input or show mouse buttons while the modal is open.
                m_h5ActionType = H5ActionType::Xbox;
                m_h5CaptureKeys.clear();
                m_h5MacroSel.clear();
                // Default: if empty, start with 1 section (whole range)
                if (m_rangosWork.empty()) {
                    RangeEdit re;
                    re.from = 0.1f; re.to = 1.0f;
                    m_rangosWork.push_back(re);
                }
                m_rangosModalOpen = true;
            }
            if (hasRanges) ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        if (m_h5ActionType == H5ActionType::Macro) {
            if (!m_h5MacroNamesLoaded) {
                m_h5MacroNames.clear();
                try {
                    std::ifstream f("data/macros.json");
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k, v] : j.items()) m_h5MacroNames.push_back(k); }
                } catch (...) {}
                m_h5MacroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_h7", m_h5MacroSel, m_h5MacroNames, availW)) {
                ButtonAction act;
                act.type     = ButtonActionType::Macro;
                act.physical = m_selTriggerSrc;
                act.name     = m_h5MacroSel;
                m_trigActionEdits[m_selTriggerSrc] = act;
                m_selTriggerSrc.clear();
                m_h5ActionType = H5ActionType::Xbox;
                m_h5MacroSel.clear();
            }

        } else if (m_h5ActionType == H5ActionType::Keyboard) {
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) { m_h5ActionType = H5ActionType::Xbox; m_h5CaptureKeys.clear(); }
            else if (ActionPanel::renderKeyboardCapture("kb_h7", m_h5CaptureKeys, availW)) {
                ButtonAction act;
                act.type     = ButtonActionType::Keyboard;
                act.physical = m_selTriggerSrc;
                for (const auto& p : m_h5CaptureKeys) act.keys.push_back(p.first);
                m_trigActionEdits[m_selTriggerSrc] = act;
                m_selTriggerSrc.clear();
                m_h5ActionType = H5ActionType::Xbox;
                m_h5CaptureKeys.clear();
            }

        } else if (m_h5ActionType == H5ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_h7", mbResult, availW)) {
                ButtonAction act;
                act.type        = ButtonActionType::MouseClick;
                act.physical    = m_selTriggerSrc;
                act.mouseButton = mbResult;
                m_trigActionEdits[m_selTriggerSrc] = act;
                m_selTriggerSrc.clear();
                m_h5ActionType = H5ActionType::Xbox;
            }
        }
    } // if (!m_selTriggerSrc.empty())

    // ── Modal Rangos ──────────────────────────────────────────────────────────
    renderRangosModal();

    // ── Gestión de clicks ─────────────────────────────────────────────────────
    // Skip click handling while any popup modal is open: IsMouseClicked(0) is
    // global and would otherwise process clicks that belong to the modal.
    if (mouseClicked && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        // Arrows have priority over the stick body hit-test on the physical pad
        std::string arrowDir;
        int arrowComp = m_padView.hitTestStickArrow(mouse, m_mappingPhysOrigin, arrowDir);
        if (arrowComp >= 0) {
            // Flecha → modo eje. Toggle si la misma flecha.
            if (m_mappingSelPhysComp == arrowComp && m_selStickDir == arrowDir && !m_selStickAsButton) {
                m_mappingSelPhysComp = -1;
                m_selStickDir.clear();
                m_selStickAsButton = false;
            } else {
                m_mappingSelPhysComp = arrowComp;
                m_selStickDir        = arrowDir;
                m_selStickAsButton   = false;
                m_h5CaptureKeys.clear();
                m_h5MacroSel.clear();
            }
        } else {

        int physHit = m_padView.hitTest(mouse, m_mappingPhysOrigin);
        if (physHit >= 0) {
            const std::string& hitType = m_padView.getLayout().components[physHit].type;
            if (hitType == "button") {
                const std::string& hitState = m_padView.getLayout().components[physHit].state;
                if (hitState == "triggerL" || hitState == "triggerR") {
                    // Click en gatillo físico → seleccionar como fuente (toggle)
                    std::string trigSrc = (hitState == "triggerL") ? "l2" : "r2";
                    if (m_selTriggerSrc == trigSrc) {
                        m_selTriggerSrc.clear();
                        m_h5ActionType = H5ActionType::Xbox;
                        m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
                    } else {
                        m_selTriggerSrc      = trigSrc;
                        m_mappingSelPhysComp = -1;
                        m_h5ActionType = H5ActionType::Xbox;
                        m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
                    }
                } else if (physHit == m_mappingSelPhysComp) {
                    m_mappingSelPhysComp = -1;
                    m_h5ActionType = H5ActionType::Xbox;
                    m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
                } else {
                    m_mappingSelPhysComp = physHit;
                    m_selTriggerSrc.clear();
                    m_h5ActionType = H5ActionType::Xbox;
                    m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
                }
            } else if (hitType == "stick") {
                // Clic en el cuerpo del stick → modo botón (L3/R3)
                if (physHit == m_mappingSelPhysComp && m_selStickAsButton) {
                    m_mappingSelPhysComp = -1;
                    m_selStickAsButton   = false;
                } else {
                    m_mappingSelPhysComp = physHit;
                    m_selStickAsButton   = true;
                    m_selStickDir.clear();
                    m_h5ActionType = H5ActionType::Xbox;
                    m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
                }
            } else if (hitType == "dpad") {
                // Clic en cruceta → determinar dirección por posición del ratón
                const PadComponent& dc = m_padView.getLayout().components[physHit];
                std::string dir = dpadDirFromMouse(mouse,
                    m_mappingPhysOrigin.x + dc.cx,
                    m_mappingPhysOrigin.y + dc.cy);
                if (physHit == m_mappingSelPhysComp && m_selDpadDir == dir) {
                    m_mappingSelPhysComp = -1;
                    m_selDpadDir.clear();
                } else {
                    m_mappingSelPhysComp = physHit;
                    m_selTriggerSrc.clear();
                    m_selDpadDir         = dir;
                    m_h5ActionType = H5ActionType::Xbox;
                    m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
                }
            }
        } else if (m_mappingSelPhysComp >= 0) {
            const std::string& selType2 = m_padView.getLayout().components[m_mappingSelPhysComp].type;
            // Botón / L3/R3 / dirección de cruceta → asignar a virtual
            if ((selType2 == "button" || (selType2 == "stick" && m_selStickAsButton)
                 || selType2 == "dpad")
                && m_h5ActionType == H5ActionType::Xbox) {
                int virtHit = m_virtualPadView.hitTest(mouse, m_mappingVirtOrigin);
                if (virtHit >= 0) {
                    const auto& virtComp  = m_virtualPadView.getLayout().components[virtHit];
                    const auto& physComps = m_padView.getLayout().components;
                    // physShort: según tipo de componente físico seleccionado
                    const auto& selPC = physComps[m_mappingSelPhysComp];
                    std::string physShort;
                    if (selType2 == "stick")
                        physShort = stateToShort(selPC.stateClick);
                    else if (selType2 == "dpad")
                        physShort = stateToShort(dpadDirToState(selPC, m_selDpadDir));
                    else
                        physShort = stateToShort(selPC.state);
                    // virtShort: botón virtual, stateClick de stick virtual, o dirección de dpad virtual
                    std::string virtShort;
                    if (virtComp.type == "button")
                        virtShort = stateToShort(virtComp.state);
                    else if (virtComp.type == "stick" && !virtComp.stateClick.empty())
                        virtShort = stateToShort(virtComp.stateClick);
                    else if (virtComp.type == "dpad") {
                        std::string vdir = dpadDirFromMouse(mouse,
                            m_mappingVirtOrigin.x + virtComp.cx,
                            m_mappingVirtOrigin.y + virtComp.cy);
                        virtShort = stateToShort(dpadDirToState(virtComp, vdir));
                    }
                    if (!physShort.empty() && !virtShort.empty()) {
                        // Gatillo virtual (L2/R2): guardar como ButtonActionType::Trigger
                        if (virtShort == "triggerL" || virtShort == "triggerR") {
                            std::string trigTarget = (virtShort == "triggerL") ? "l2" : "r2";
                            auto h5it = m_h5ActionEdits.find(physShort);
                            bool already = (h5it != m_h5ActionEdits.end() &&
                                            h5it->second.type == ButtonActionType::Trigger &&
                                            h5it->second.target == trigTarget);
                            if (already) {
                                m_h5ActionEdits.erase(physShort);
                                m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                            } else {
                                ButtonAction act;
                                act.type = ButtonActionType::Trigger; act.physical = physShort; act.target = trigTarget;
                                m_h5ActionEdits[physShort] = act;
                                m_mappingEdits.erase(physShort);
                                m_mappingFlashComp      = virtHit;
                                m_mappingFlashTimer     = 0.5f;
                                m_mappingFlashVirtShort = virtShort;
                            }
                        } else {
                            m_h5ActionEdits.erase(physShort);
                            auto it = m_mappingEdits.find(physShort);
                            bool alreadyAssigned = (it != m_mappingEdits.end() && it->second == virtShort);
                            m_mappingEdits[physShort] = alreadyAssigned ? "" : virtShort;
                            m_mappingFlashComp      = alreadyAssigned ? -1 : virtHit;
                            m_mappingFlashTimer     = alreadyAssigned ?  0.0f : 0.5f;
                            m_mappingFlashVirtShort = alreadyAssigned ? "" : virtShort;
                        }
                    }
                    m_mappingSelPhysComp = -1;
                    m_selStickAsButton   = false;
                    m_selDpadDir.clear();
                }
            } else if (selType2 == "stick") {
                int virtHit = m_virtualPadView.hitTest(mouse, m_mappingVirtOrigin);
                if (virtHit >= 0) {
                    const auto& virtComps = m_virtualPadView.getLayout().components;
                    const std::string& virtType = virtComps[virtHit].type;
                    const auto& physComps = m_padView.getLayout().components;
                    auto [xId, yId] = stickIdsFromStateX(physComps[m_mappingSelPhysComp].stateX);

                    if (virtType == "stick" && !xId.empty()) {
                        // Stick → virtual stick
                        auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtHit].stateX);
                        if (!vxId.empty()) {
                            for (const auto& cfg : m_controllerConfigs) {
                                if (cfg.vid != m_mappingActiveVid || cfg.pid != m_mappingActivePid) continue;
                                for (const auto& [src, mapping] : cfg.axes) {
                                    std::string sid = mapping.stickId.empty() ? mapping.target : mapping.stickId;
                                    if (sid == xId || sid == yId) {
                                        AxisMapping edit = mapping;
                                        edit.stickId = sid;
                                        edit.btnNeg  = edit.btnPos = "";
                                        edit.target  = (sid == xId) ? vxId : vyId;
                                        m_h6AxisEdits[sid] = edit;
                                    }
                                }
                                break;
                            }
                            m_mappingSelPhysComp = -1;
                            m_selStickDir.clear();
                        }
                    } else if (virtType == "dpad" && !xId.empty()) {
                        // Stick → cruceta
                        auto buildDpadEdit = [&](const std::string& id, const std::string& tgt) {
                            AxisMapping edit;
                            edit.stickId = id;
                            edit.target  = tgt;
                            for (const auto& cfg : m_controllerConfigs) {
                                if (cfg.vid != m_mappingActiveVid || cfg.pid != m_mappingActivePid) continue;
                                for (const auto& [src, mapping] : cfg.axes) {
                                    std::string sid = mapping.stickId.empty() ? mapping.target : mapping.stickId;
                                    if (sid == id) { edit.invert = mapping.invert; break; }
                                }
                                break;
                            }
                            m_h6AxisEdits[id] = edit;
                        };
                        buildDpadEdit(xId, "dpad_x");
                        if (!yId.empty()) buildDpadEdit(yId, "dpad_y");
                        m_mappingSelPhysComp = -1;
                        m_selStickDir.clear();
                    }
                }
            }
        } else if (!m_selTriggerSrc.empty() && m_h5ActionType == H5ActionType::Xbox) {
            // Gatillo como fuente: clic en virtual → asignar target
            int virtHit = m_virtualPadView.hitTest(mouse, m_mappingVirtOrigin);
            if (virtHit >= 0) {
                const auto& virtComp = m_virtualPadView.getLayout().components[virtHit];
                ButtonAction act;
                act.physical = m_selTriggerSrc;
                bool assigned = false;
                if (virtComp.type == "button") {
                    const std::string& vState = virtComp.state;
                    if (vState == "triggerL" || vState == "triggerR") {
                        // Analog passthrough
                        std::string trigTarget = (vState == "triggerL") ? "l2" : "r2";
                        auto it = m_trigActionEdits.find(m_selTriggerSrc);
                        bool already = (it != m_trigActionEdits.end() &&
                                        it->second.type == ButtonActionType::TriggerPassthrough &&
                                        it->second.target == trigTarget);
                        if (already) {
                            m_trigActionEdits.erase(m_selTriggerSrc);
                            m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                        } else {
                            act.type = ButtonActionType::TriggerPassthrough; act.target = trigTarget;
                            m_trigActionEdits[m_selTriggerSrc] = act;
                            m_mappingFlashComp = virtHit; m_mappingFlashTimer = 0.5f;
                            m_mappingFlashVirtShort = vState;
                        }
                        assigned = true;
                    } else {
                        std::string vShort = stateToShort(vState);
                        if (!vShort.empty()) {
                            auto it = m_trigActionEdits.find(m_selTriggerSrc);
                            bool already = (it != m_trigActionEdits.end() &&
                                            it->second.type == ButtonActionType::VirtualButton &&
                                            it->second.name == vShort);
                            if (already) {
                                m_trigActionEdits.erase(m_selTriggerSrc);
                                m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                            } else {
                                act.type = ButtonActionType::VirtualButton; act.name = vShort;
                                m_trigActionEdits[m_selTriggerSrc] = act;
                                m_mappingFlashComp = virtHit; m_mappingFlashTimer = 0.5f;
                                m_mappingFlashVirtShort = vState;
                            }
                            assigned = true;
                        }
                    }
                } else if (virtComp.type == "stick" && !virtComp.stateClick.empty()) {
                    std::string vShort = stateToShort(virtComp.stateClick);
                    auto it = m_trigActionEdits.find(m_selTriggerSrc);
                    bool already = (it != m_trigActionEdits.end() &&
                                    it->second.type == ButtonActionType::VirtualButton &&
                                    it->second.name == vShort);
                    if (already) {
                        m_trigActionEdits.erase(m_selTriggerSrc);
                        m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                    } else {
                        act.type = ButtonActionType::VirtualButton; act.name = vShort;
                        m_trigActionEdits[m_selTriggerSrc] = act;
                        m_mappingFlashComp = virtHit; m_mappingFlashTimer = 0.5f;
                        m_mappingFlashVirtShort = virtComp.stateClick;
                    }
                    assigned = true;
                } else if (virtComp.type == "dpad") {
                    std::string vdir = dpadDirFromMouse(mouse,
                        m_mappingVirtOrigin.x + virtComp.cx,
                        m_mappingVirtOrigin.y + virtComp.cy);
                    if (!vdir.empty()) {
                        std::string vShort = "dpad_" + vdir;  // "dpad_up/down/left/right"
                        auto it = m_trigActionEdits.find(m_selTriggerSrc);
                        bool already = (it != m_trigActionEdits.end() &&
                                        it->second.type == ButtonActionType::VirtualButton &&
                                        it->second.name == vShort);
                        if (already) {
                            m_trigActionEdits.erase(m_selTriggerSrc);
                            m_mappingFlashComp = -1; m_mappingFlashTimer = 0.0f; m_mappingFlashVirtShort.clear();
                        } else {
                            act.type = ButtonActionType::VirtualButton; act.name = vShort;
                            m_trigActionEdits[m_selTriggerSrc] = act;
                            m_mappingFlashComp = virtHit; m_mappingFlashTimer = 0.5f;
                            m_mappingFlashVirtShort = shortToState(vShort);
                        }
                        assigned = true;
                    }
                }
                if (assigned) {
                    m_selTriggerSrc.clear();
                    m_h5ActionType = H5ActionType::Xbox;
                }
            }
        }
        } // end else (no arrow hit)
    }

    // ── Guardar / Cancelar ────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Guardar##mapSave", { 120.0f, 0.0f })) {
        saveMappingEdits();
        m_mappingActive = false;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.35f, 0.35f, 0.35f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.45f, 0.45f, 0.45f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.25f, 0.25f, 0.25f, 1.0f });
    if (ImGui::Button("Cancelar##mapCancel", { 100.0f, 0.0f })) {
        m_mappingSelPhysComp = -1;
        m_selStickDir.clear();
        m_selStickAsButton   = false;
        m_selDpadDir.clear();
        m_selTriggerSrc.clear();
        m_h5ActionType = H5ActionType::Xbox;
        m_h5CaptureKeys.clear(); m_h5MacroSel.clear();
        reloadMappingEdits();
        m_mappingActive = false;
    }
    ImGui::PopStyleColor(3);
}

// ---------------------------------------------------------------------------
// renderRangosModal — modal para edición de zonas de gatillo
// ---------------------------------------------------------------------------

void AppWindow::renderRangosModal() {
    if (!m_rangosModalOpen) return;
    ImGui::OpenPopup("Rangos de gatillo##rangosModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 600.0f, 0.0f });

    if (!ImGui::BeginPopupModal("Rangos de gatillo##rangosModal", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;

    // Static list of assignable Xbox / dpad choices
    struct XboxChoice { const char* display; const char* name; };
    static const XboxChoice kChoices[] = {
        {"A","a"},{"B","b"},{"X","x"},{"Y","y"},
        {"L1","l1"},{"R1","r1"},{"Select","select"},{"Start","start"},{"Home","home"},
        {"L3","l3"},{"R3","r3"},
        {"Cruceta Arriba","up"},{"Cruceta Abajo","down"},
        {"Cruceta Izq","left"},{"Cruceta Der","right"},
    };
    static const int kNChoices = 15;

    // ── Header ───────────────────────────────────────────────────────────────
    const char* hdr = (m_rangosForTrigger == "l2")
        ? "L2  \xe2\x86\x92  Zonas de recorrido"
        : "R2  \xe2\x86\x92  Zonas de recorrido";
    ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", hdr);
    ImGui::Spacing();

    // ── Barra visual de rangos ────────────────────────────────────────────────
    {
        int n = (int)m_rangosWork.size();
        float barW  = ImGui::GetContentRegionAvail().x - 4.0f;
        float barH  = 28.0f;
        ImVec2 barMin = { ImGui::GetCursorScreenPos().x + 2.0f, ImGui::GetCursorScreenPos().y };
        ImVec2 barMax = { barMin.x + barW, barMin.y + barH };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(barMin, barMax, IM_COL32(40,40,40,255), 4.0f);

        for (int i = 0; i < n; ++i) {
            float t0 = (m_rangosWork[i].from - 0.1f) / 0.9f;  // normalize to [0,1] in bar
            float t1 = (m_rangosWork[i].to   - 0.1f) / 0.9f;
            t0 = std::clamp(t0, 0.0f, 1.0f);
            t1 = std::clamp(t1, 0.0f, 1.0f);
            ImVec2 r0 = { barMin.x + t0 * barW + 1.0f, barMin.y + 1.0f };
            ImVec2 r1 = { barMin.x + t1 * barW - 1.0f, barMax.y - 1.0f };
            ImU32 col = (i == m_rangosSelSect)
                ? IM_COL32(255,180,0,220)
                : (m_rangosWork[i].hasAction ? IM_COL32(60,160,80,200) : IM_COL32(80,80,120,180));
            dl->AddRectFilled(r0, r1, col, 3.0f);
            // Range label
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f\xe2\x80\x93%.2f", m_rangosWork[i].from, m_rangosWork[i].to);
            ImVec2 textSz = ImGui::CalcTextSize(buf);
            float cx = (r0.x + r1.x) * 0.5f - textSz.x * 0.5f;
            float cy = (r0.y + r1.y) * 0.5f - textSz.y * 0.5f;
            if (cx >= r0.x && cx + textSz.x <= r1.x)
                dl->AddText({ cx, cy }, IM_COL32(230,230,230,255), buf);
            // Section border
            if (i > 0)
                dl->AddLine({ barMin.x + t0 * barW, barMin.y }, { barMin.x + t0 * barW, barMax.y },
                             IM_COL32(200,200,200,160), 1.5f);
        }
        dl->AddRect(barMin, barMax, IM_COL32(150,150,150,200), 4.0f);

        // Click detection on bar
        ImGui::InvisibleButton("##rangeBar", { barW + 4.0f, barH });
        if (ImGui::IsItemClicked()) {
            float mx = ImGui::GetIO().MousePos.x - barMin.x;
            float normPos = mx / barW;  // 0..1
            float trigPos = normPos * 0.9f + 0.1f;   // 0.1..1.0
            for (int i = 0; i < n; ++i) {
                if (trigPos >= m_rangosWork[i].from && trigPos <= m_rangosWork[i].to) {
                    m_rangosSelSect = (m_rangosSelSect == i) ? -1 : i;
                    m_rangosActType     = H5ActionType::Xbox;
                    m_rangosCaptureKeys.clear();
                    m_rangosMacroSel.clear();
                    m_rangosXboxSel     = -1;
                    // Pre-select existing action type
                    if (m_rangosSelSect >= 0 && m_rangosWork[i].hasAction) {
                        const auto& act = m_rangosWork[i].action;
                        if (act.type == ButtonActionType::Macro)      m_rangosActType = H5ActionType::Macro;
                        else if (act.type == ButtonActionType::Keyboard)  m_rangosActType = H5ActionType::Keyboard;
                        else if (act.type == ButtonActionType::MouseClick) m_rangosActType = H5ActionType::Mouse;
                        else m_rangosActType = H5ActionType::Xbox;
                    }
                    break;
                }
            }
        }
    }
    ImGui::Spacing();

    // ── Botón Partición ───────────────────────────────────────────────────────
    {
        int n = (int)m_rangosWork.size();
        bool canAdd = (n < 10);
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button(n == 1 ? "Partici\xC3\xB3n (crear 2 zonas)" : "Partici\xC3\xB3n (a\xC3\xB1\xC3\xA1\xC4\x80\xC3\xBDir zona)", { 0.0f, 0.0f })) {
            // Add one more equal section — clear all actions
            int newN = n + 1;
            m_rangosWork.clear();
            for (int i = 0; i < newN; ++i) {
                RangeEdit re;
                re.from = 0.1f + i       * 0.9f / (float)newN;
                re.to   = 0.1f + (i + 1) * 0.9f / (float)newN;
                // clamp last to exactly 1.0
                if (i == newN - 1) re.to = 1.0f;
                m_rangosWork.push_back(re);
            }
            m_rangosSelSect = -1;
            m_rangosActType = H5ActionType::Xbox;
            m_rangosCaptureKeys.clear();
            m_rangosMacroSel.clear();
        }
        if (!canAdd) ImGui::EndDisabled();
        ImGui::SameLine();
        // Remove section button
        bool canRemove = (n > 1);
        if (!canRemove) ImGui::BeginDisabled();
        if (ImGui::Button("Quitar zona##rangeRm", { 0.0f, 0.0f }) && canRemove && m_rangosSelSect >= 0) {
            m_rangosWork.erase(m_rangosWork.begin() + m_rangosSelSect);
            // Recalculate from/to equally (preserve actions per index)
            int newN = (int)m_rangosWork.size();
            for (int i = 0; i < newN; ++i) {
                m_rangosWork[i].from = 0.1f + i       * 0.9f / (float)newN;
                m_rangosWork[i].to   = 0.1f + (i + 1) * 0.9f / (float)newN;
                if (i == newN - 1) m_rangosWork[i].to = 1.0f;
            }
            m_rangosSelSect = -1;
        }
        if (!canRemove) ImGui::EndDisabled();
        ImGui::SameLine();
        // Clear section action button
        if (m_rangosSelSect >= 0 && m_rangosWork[m_rangosSelSect].hasAction) {
            if (ImGui::Button("Borrar acci\xC3\xB3n##rangeClear")) {
                m_rangosWork[m_rangosSelSect].hasAction = false;
                m_rangosWork[m_rangosSelSect].action = ButtonAction{};
                m_rangosActType = H5ActionType::Xbox;
                m_rangosCaptureKeys.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d/10 zonas)", (int)m_rangosWork.size());
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ── Panel de asignación para sección seleccionada ─────────────────────────
    if (m_rangosSelSect < 0) {
        ImGui::TextDisabled("Haz clic en una zona de la barra para asignarle una acci\xC3\xB3n.");
    } else {
        ImGui::Text("Zona %d  (%.2f \xe2\x80\x93 %.2f):",
                    m_rangosSelSect + 1,
                    m_rangosWork[m_rangosSelSect].from,
                    m_rangosWork[m_rangosSelSect].to);
        ImGui::Spacing();

        // Type buttons (no Trigger / TriggerPassthrough in ranges)
        float bW = 85.0f;
        float sp = ImGui::GetStyle().ItemSpacing.x;
        float totalBtnW = bW * 4 + sp * 3;
        float offBX = (ImGui::GetContentRegionAvail().x - totalBtnW) * 0.5f;
        if (offBX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offBX);
        auto rTypeBtn = [&](const char* lbl, H5ActionType t) {
            bool sel = (m_rangosActType == t);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(lbl, { bW, 0.0f })) { m_rangosActType = t; m_rangosCaptureKeys.clear(); m_rangosMacroSel.clear(); m_rangosXboxSel = -1; }
            if (sel) ImGui::PopStyleColor();
        };
        rTypeBtn("Xbox##rt0", H5ActionType::Xbox);    ImGui::SameLine();
        rTypeBtn("Macro##rt1", H5ActionType::Macro);  ImGui::SameLine();
        rTypeBtn("Teclado##rt2", H5ActionType::Keyboard); ImGui::SameLine();
        rTypeBtn("Rat\xC3\xB3n##rt3", H5ActionType::Mouse);

        ImGui::Spacing();

        if (m_rangosActType == H5ActionType::Xbox) {
            // Dropdown with Xbox button / dpad choices
            float cW = 220.0f;
            float cOff = (ImGui::GetContentRegionAvail().x - cW - sp - 80.0f) * 0.5f;
            if (cOff > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cOff);
            ImGui::SetNextItemWidth(cW);
            // Current value label
            const char* preview = (m_rangosXboxSel >= 0 && m_rangosXboxSel < kNChoices)
                ? kChoices[m_rangosXboxSel].display
                : "-- elige bot\xC3\xB3n --";
            // If this section already has an Xbox/dpad action, pre-select it
            if (m_rangosXboxSel < 0 && m_rangosWork[m_rangosSelSect].hasAction) {
                const auto& act = m_rangosWork[m_rangosSelSect].action;
                if (act.type == ButtonActionType::VirtualButton) {
                    for (int ci = 0; ci < kNChoices; ++ci) {
                        if (act.name == kChoices[ci].name) { m_rangosXboxSel = ci; break; }
                    }
                    if (m_rangosXboxSel >= 0) preview = kChoices[m_rangosXboxSel].display;
                }
            }
            if (ImGui::BeginCombo("##rangesXbox", preview)) {
                for (int ci = 0; ci < kNChoices; ++ci) {
                    bool sel = (m_rangosXboxSel == ci);
                    if (ImGui::Selectable(kChoices[ci].display, sel)) m_rangosXboxSel = ci;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            bool canAssign = (m_rangosXboxSel >= 0);
            if (!canAssign) ImGui::BeginDisabled();
            if (ImGui::Button("Asignar##rxbAssign", { 80.0f, 0.0f }) && canAssign) {
                ButtonAction act;
                act.type = ButtonActionType::VirtualButton;
                act.name = kChoices[m_rangosXboxSel].name;
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
            }
            if (!canAssign) ImGui::EndDisabled();
            // Show current assignment
            if (m_rangosWork[m_rangosSelSect].hasAction &&
                m_rangosWork[m_rangosSelSect].action.type == ButtonActionType::VirtualButton) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_rangosWork[m_rangosSelSect].action.name.c_str());
            }

        } else if (m_rangosActType == H5ActionType::Macro) {
            if (!m_h5MacroNamesLoaded) {
                m_h5MacroNames.clear();
                try {
                    std::ifstream f("data/macros.json");
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k,v] : j.items()) m_h5MacroNames.push_back(k); }
                } catch (...) {}
                m_h5MacroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_rng", m_rangosMacroSel, m_h5MacroNames,
                                              ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type = ButtonActionType::Macro;
                act.name = m_rangosMacroSel;
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
            }

        } else if (m_rangosActType == H5ActionType::Keyboard) {
            if (ActionPanel::renderKeyboardCapture("kb_rng", m_rangosCaptureKeys,
                                                   ImGui::GetContentRegionAvail().x, true)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard;
                for (const auto& p : m_rangosCaptureKeys) act.keys.push_back(p.first);
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
                m_rangosCaptureKeys.clear();
            }
            // Show existing assignment when capture is empty
            if (m_rangosWork[m_rangosSelSect].hasAction &&
                m_rangosWork[m_rangosSelSect].action.type == ButtonActionType::Keyboard &&
                m_rangosCaptureKeys.empty()) {
                std::string ex;
                for (const auto& k : m_rangosWork[m_rangosSelSect].action.keys) { if (!ex.empty()) ex += "+"; ex += k; }
                ImGui::TextDisabled("  actual: %s", ex.c_str());
            }

        } else if (m_rangosActType == H5ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_rng", mbResult,
                                                ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type        = ButtonActionType::MouseClick;
                act.mouseButton = mbResult;
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
            }
            if (m_rangosWork[m_rangosSelSect].hasAction &&
                m_rangosWork[m_rangosSelSect].action.type == ButtonActionType::MouseClick) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_rangosWork[m_rangosSelSect].action.mouseButton.c_str());
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Aceptar / Cancelar ────────────────────────────────────────────────────
    float btnW2 = 100.0f;
    float dialogW = ImGui::GetContentRegionAvail().x;
    float btnOff = (dialogW - btnW2 * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (btnOff > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnOff);
    if (ImGui::Button("Aceptar##rangosOk", { btnW2, 0.0f })) {
        // Apply working copy to the permanent edits
        if (m_rangosForTrigger == "l2")
            m_trigLRangeEdits = m_rangosWork;
        else
            m_trigRRangeEdits = m_rangosWork;
        // Clear the conflicting simple action so ranges take effect on save
        m_trigActionEdits.erase(m_rangosForTrigger);
        // Deselect trigger source → focus returns to physical pad,
        // same behaviour as any other completed H7 assignment.
        // Also prevents H7 mouse/keyboard buttons from staying active
        // after the modal closes (avoids accidental re-assignment).
        m_selTriggerSrc.clear();
        m_h5ActionType = H5ActionType::Xbox;
        m_h5CaptureKeys.clear();
        m_h5MacroSel.clear();
        m_rangosModalOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar##rangosCan", { btnW2, 0.0f })) {
        m_rangosModalOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// saveMappingEdits — escribe m_mappingEdits en controllers.json
// ---------------------------------------------------------------------------

void AppWindow::saveMappingEdits() {
    try {
        const std::string path = "data/controllers.json";
        json root;
        {
            std::ifstream f(path);
            if (f.is_open()) root = json::parse(f);
        }
        if (!root.contains("controllers") || !root["controllers"].is_array()) return;

        char vidStr[8], pidStr[8];
        snprintf(vidStr, sizeof(vidStr), "%04X", m_mappingActiveVid);
        snprintf(pidStr, sizeof(pidStr), "%04X", m_mappingActivePid);

        for (auto& ctrl : root["controllers"]) {
            if (ctrl.value("vid","") != std::string(vidStr) ||
                ctrl.value("pid","") != std::string(pidStr)) continue;
            if (!ctrl.contains("buttons")) continue;

            // Recoger cambios primero para no modificar mientras iteramos
            std::vector<std::pair<std::string, json>> changes;
            for (auto& [key, btn] : ctrl["buttons"].items()) {
                std::string physShort;
                if (btn.is_string())
                    physShort = btn.get<std::string>();
                else if (btn.is_object() && btn.contains("physical"))
                    physShort = btn["physical"].get<std::string>();
                else continue;

                json newBtn = btn.is_object() ? btn : json::object();
                if (!btn.is_object()) newBtn["physical"] = physShort;
                bool changed = false;

                // H5 action overrides Xbox remap
                auto h5it = m_h5ActionEdits.find(physShort);
                if (h5it != m_h5ActionEdits.end()) {
                    const ButtonAction& act = h5it->second;
                    newBtn.erase("virtual");
                    if (act.type == ButtonActionType::Keyboard) {
                        newBtn["type"] = "keyboard";
                        newBtn.erase("name");
                        json keysArr = json::array();
                        for (const auto& k : act.keys) keysArr.push_back(k);
                        newBtn["keys"] = keysArr;
                    } else if (act.type == ButtonActionType::MouseClick) {
                        newBtn["type"]   = "mouse_click";
                        newBtn["button"] = act.mouseButton;
                        newBtn.erase("name"); newBtn.erase("keys");
                    } else if (act.type == ButtonActionType::Macro) {
                        newBtn["type"] = "macro";
                        newBtn["name"] = act.name;
                        newBtn.erase("keys"); newBtn.erase("button");
                    } else if (act.type == ButtonActionType::Trigger) {
                        newBtn["type"]   = "trigger";
                        newBtn["target"] = act.target;
                        newBtn.erase("virtual"); newBtn.erase("name");
                        newBtn.erase("keys");    newBtn.erase("button");
                    }
                    changed = true;
                } else {
                    auto it = m_mappingEdits.find(physShort);
                    if (it != m_mappingEdits.end()) {
                        // Limpiar campos H5 si los hubiera de antes
                        newBtn.erase("type"); newBtn.erase("target");
                        newBtn.erase("keys"); newBtn.erase("button"); newBtn.erase("name");
                        if (it->second.empty())
                            newBtn.erase("virtual");
                        else
                            newBtn["virtual"] = it->second;
                        changed = true;
                    }
                }
                if (changed) changes.push_back({ key, std::move(newBtn) });
            }
            for (auto& [key, val] : changes)
                ctrl["buttons"][key] = val;

            // Dpad remapping: save into "dpad_remap" object (Xbox strings) and H5 actions (objects)
            {
                json dpadRemapJson = json::object();
                for (const char* dir : {"up", "down", "left", "right"}) {
                    std::string key = std::string("dpad_") + dir;
                    // H5 action takes priority over Xbox remap
                    auto h5it = m_h5ActionEdits.find(key);
                    if (h5it != m_h5ActionEdits.end()) {
                        const ButtonAction& act = h5it->second;
                        json actJson = json::object();
                        actJson["physical"] = key;
                        if (act.type == ButtonActionType::Keyboard) {
                            actJson["type"] = "keyboard";
                            json keysArr = json::array();
                            for (const auto& k : act.keys) keysArr.push_back(k);
                            actJson["keys"] = keysArr;
                        } else if (act.type == ButtonActionType::MouseClick) {
                            actJson["type"]   = "mouse_click";
                            actJson["button"] = act.mouseButton;
                        } else if (act.type == ButtonActionType::Macro) {
                            actJson["type"] = "macro";
                            actJson["name"] = act.name;
                        } else if (act.type == ButtonActionType::Trigger) {
                            actJson["type"]   = "trigger";
                            actJson["target"] = act.target;
                        }
                        dpadRemapJson[dir] = std::move(actJson);
                    } else {
                        auto it = m_mappingEdits.find(key);
                        if (it != m_mappingEdits.end() && !it->second.empty())
                            dpadRemapJson[dir] = it->second;
                    }
                }
                if (dpadRemapJson.empty())
                    ctrl.erase("dpad_remap");
                else
                    ctrl["dpad_remap"] = std::move(dpadRemapJson);
            }

            // H7: save trigger_actions
            {
                // Helper: serialize a ButtonAction to JSON object
                auto actToJson = [&](const ButtonAction& act) {
                    json j = json::object();
                    if (act.type == ButtonActionType::TriggerPassthrough) {
                        j["type"]   = "trigger_passthrough";
                        j["target"] = act.target;
                    } else if (act.type == ButtonActionType::VirtualButton) {
                        j["virtual"] = act.name;
                    } else if (act.type == ButtonActionType::Keyboard) {
                        j["type"] = "keyboard";
                        json arr = json::array();
                        for (const auto& k : act.keys) arr.push_back(k);
                        j["keys"] = arr;
                    } else if (act.type == ButtonActionType::MouseClick) {
                        j["type"]   = "mouse_click";
                        j["button"] = act.mouseButton;
                    } else if (act.type == ButtonActionType::Macro) {
                        j["type"] = "macro";
                        j["name"] = act.name;
                    } else if (act.type == ButtonActionType::Trigger) {
                        j["type"]   = "trigger";
                        j["target"] = act.target;
                    }
                    return j;
                };

                auto buildTrigSideJson = [&](const std::string& key,
                                              const std::vector<RangeEdit>& ranges) {
                    json result;  // default: null
                    // Simple action always wins over ranges (user reassigned from main UI).
                    auto it = m_trigActionEdits.find(key);
                    if (it != m_trigActionEdits.end()) {
                        result = actToJson(it->second);
                    } else if (!ranges.empty()) {
                        if (ranges.size() == 1) {
                            // 1 rango = acción directa (o passthrough si no tiene acción)
                            if (ranges[0].hasAction)
                                result = actToJson(ranges[0].action);
                            // sin acción → null → sin trigger_actions → passthrough analógico
                        } else {
                            json side = json::object();
                            json arr  = json::array();
                            for (const auto& re : ranges) {
                                json r;
                                r["from"] = re.from;
                                r["to"]   = re.to;
                                if (re.hasAction)
                                    r["action"] = actToJson(re.action);
                                arr.push_back(r);
                            }
                            side["ranges"] = arr;
                            result = side;
                        }
                    }
                    return result;
                };

                json taJson = json::object();
                json lSide  = buildTrigSideJson("l2", m_trigLRangeEdits);
                json rSide  = buildTrigSideJson("r2", m_trigRRangeEdits);
                if (!lSide.is_null()) taJson["l2"] = lSide;
                if (!rSide.is_null()) taJson["r2"] = rSide;

                if (taJson.empty())
                    ctrl.erase("trigger_actions");
                else
                    ctrl["trigger_actions"] = taJson;
            }

            // H6: save axis (stick) remapping
            if (!m_h6AxisEdits.empty() && ctrl.contains("axes")) {
                for (auto& [source, axisJson] : ctrl["axes"].items()) {
                    // Determine stickId for this axis entry
                    std::string sid = axisJson.value("stick_id", std::string{});
                    if (sid.empty()) {
                        std::string t = axisJson.value("target", std::string{});
                        if (t == "left_x"  || t == "left_y"  ||
                            t == "right_x" || t == "right_y") sid = t;
                    }
                    auto eit = m_h6AxisEdits.find(sid);
                    if (eit == m_h6AxisEdits.end()) continue;
                    const AxisMapping& em = eit->second;
                    axisJson["target"]   = em.target;
                    axisJson["stick_id"] = em.stickId;
                    if (em.target == "btn_dir") {
                        if (!em.btnNeg.empty()) axisJson["btn_neg"] = em.btnNeg;
                        else                    axisJson.erase("btn_neg");
                        if (!em.btnPos.empty()) axisJson["btn_pos"] = em.btnPos;
                        else                    axisJson.erase("btn_pos");
                    } else {
                        axisJson.erase("btn_neg");
                        axisJson.erase("btn_pos");
                    }
                }
            }

            break;
        }

        // Validar antes de escribir
        std::string dumped = root.dump(2);
        json::parse(dumped);  // lanza si el JSON generado es inválido

        // Escribir a fichero temporal y hacer rename atómico
        std::string tmpPath = path + ".tmp";
        {
            std::ofstream tmp(tmpPath);
            if (!tmp.is_open()) return;
            tmp << dumped;
        }  // destructor flushes + close

        MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);

        m_controllerConfigs = loadControllerConfigs(path);
        m_engine.reloadConfigs();
    } catch (const std::exception&) {}
}

// ---------------------------------------------------------------------------
// Layout tab
// ---------------------------------------------------------------------------

void AppWindow::renderLayoutTab() {
    ImGui::Spacing();

    if (m_layoutsFromBackup) {
        ImGui::TextColored({ 1.0f, 0.7f, 0.1f, 1.0f },
            "AVISO: pad_layouts.json fallo al cargar. Usando copia de seguridad (.bak).");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    m_layoutEditor.render();

    if (m_layoutEditor.pollControllersSaved()) {
        m_controllerConfigs = loadControllerConfigs("data/controllers.json");
        m_engine.reloadConfigs();
    }

    if (m_layoutEditor.pollLayoutSaved()) {
        m_forceLayoutReload     = true;
        m_virtualPadInitialized = false;
    }
}

// ---------------------------------------------------------------------------

void AppWindow::cleanup() {
    m_layoutEditor.unload();
    m_virtualPadView.unload();
    m_padView.unload();
    m_arrowRightTex.release();
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
