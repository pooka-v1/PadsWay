#define NOMINMAX
#include "AppWindow.h"
#include "Log.h"
#include "Paths.h"

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
#include "config/Strings.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AppWindow::AppWindow(PadEngine& engine, DeviceHub& deviceHub)
    : m_engine(engine), m_deviceHub(deviceHub) {}

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

    // Load virtualpad.json early to get font_size before ImGui font init.
    VirtualPadConfig vpCfgEarly;
    try { vpCfgEarly = loadVirtualPadConfig(Paths::userData("data/virtualpad.json")); } catch (...) {}

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;

    // Load Segoe UI with an extended glyph range for full Unicode coverage.
    // Covers Latin, Spanish/French accents, arrows, and general punctuation.
    // Falls back to ImGui's built-in bitmap font if the file is not found.
    {
        static const ImWchar kRanges[] = {
            0x0020, 0x00FF,  // Basic Latin + Latin-1 Supplement (accented chars, ñ, etc.)
            0x2000, 0x206F,  // General Punctuation
            0x2190, 0x21FF,  // Arrows (←→↑↓ etc.)
            0,
        };
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", vpCfgEarly.fontSize, nullptr, kRanges);
        if (!font)
            io.Fonts->AddFontDefault();
    }

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
        m_controllerConfigs = loadControllerConfigs(Paths::userData("data/controllers.json"));
    } catch (...) {}   // tolerate a corrupt/unreadable controllers.json; start with none

    std::string locale = "en";
    try {
        VirtualPadConfig vpCfg = loadVirtualPadConfig(Paths::userData("data/virtualpad.json"));
        m_acceptedXboxButtons  = vpCfg.acceptedXboxButtons;
        m_stickSelectThreshold = vpCfg.stickSelectThreshold;
        m_stickHoldMs          = vpCfg.stickHoldMs;
        m_gyroSelectThreshold  = vpCfg.gyroSelectThreshold;
        m_accelSelectThreshold = vpCfg.accelSelectThreshold;
        locale = vpCfg.locale;
    } catch (...) {}  // struct defaults apply if file is missing or malformed
    Strings::load(locale);

    try { m_padLayouts = loadPadLayouts(Paths::userData("data/pad_layouts.json")); } catch (...) {}
    if (m_padLayouts.empty()) {
        try { m_padLayouts = loadPadLayouts(Paths::userData("data/pad_layouts.json.bak")); } catch (...) {}
        m_layoutsFromBackup = !m_padLayouts.empty();
    }

    // Discover game profiles
    refreshProfileList();

    m_padView.load(m_device);
    m_virtualPadView.load(m_device);
    m_layoutEditor.init(m_device, &m_padLayouts, Paths::userData("data/pad_layouts.json"));
    m_mappingEditor.init(m_device, &m_engine, m_padLayouts,
                         m_acceptedXboxButtons, m_stickSelectThreshold, m_stickHoldMs,
                         m_gyroSelectThreshold, m_accelSelectThreshold);
    m_mappingEditor.setConfigs(m_controllerConfigs);
    m_macroManager.init(m_device);
    m_calibrationPanel.init(&m_engine);

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
// renderFrame â€" full-screen canvas with tab bar
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
        if (ImGui::BeginTabItem(tr("tab.engine")))  { renderEngineTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(tr("tab.scanner"))) { renderScannerTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(tr("tab.pads")))    { renderPadsTab();    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(tr("tab.layout")))  { renderLayoutTab();  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    // Output-type confirmation modal — drawn here at canvas level (NOT inside a tab)
    // so BeginPopupModal is submitted every frame; a modal nested in a BeginTabItem can
    // ghost-freeze the whole app if the tab stops emitting for a frame.
    if (m_outputConfirmOpen && !ImGui::IsPopupOpen("output_switch_confirm"))
        ImGui::OpenPopup("output_switch_confirm");
    if (ImGui::BeginPopupModal("output_switch_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", tr("engine.output_warn"));
        ImGui::Spacing();
        if (ImGui::Button(tr("engine.output_confirm"), { 120.0f, 0.0f })) {
            VirtualOutputType t = (m_pendingOutputSel == 1)
                ? VirtualOutputType::DualShock : VirtualOutputType::Xbox;
            m_engine.requestOutputType(t);   // the engine persists output_type only if the switch succeeds
            m_outputConfirmOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("engine.output_cancel"), { 120.0f, 0.0f })) {
            m_outputConfirmOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

    // â"€â"€ Status indicator â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    if (connected) {
        ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "\xe2\x97\x8f");
        ImGui::SameLine();
        ImGui::Text(tr("engine.connected"), m_engine.getDevice().c_str());
    } else if (phase == EnginePhase::WaitingSelection) {
        ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "\xe2\x97\x8f");
        ImGui::SameLine();
        ImGui::Text("%s", tr("engine.select_ctrl"));
    } else if (running) {
        ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "\xe2\x97\x8f");
        ImGui::SameLine();
        ImGui::Text("%s", tr("engine.waiting"));
    } else {
        ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "\xe2\x97\x8f");
        ImGui::SameLine();
        ImGui::Text("%s", tr("engine.stopped"));
    }

    ImGui::Spacing();
    ImGui::TextDisabled(tr("engine.status"), m_engine.getStatus().c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // â"€â"€ Device list â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    // WaitingSelection uses the candidates snapshot; all other states use the
    // live monitor list so newly connected devices appear without a restart.
    auto availableDevices = m_engine.getAvailableDevices();
    auto candidates       = m_engine.getCandidates();
    DeviceCandidate activeDevice = m_engine.getActiveDevice();

    const std::vector<DeviceCandidate>& displayList =
        (phase == EnginePhase::WaitingSelection && !candidates.empty())
        ? candidates : availableDevices;

    if (displayList.empty()) {
        ImGui::TextDisabled("%s", tr("engine.no_ctrl"));
    } else {
        for (int i = 0; i < (int)displayList.size(); ++i) {
            const auto& dev = displayList[i];
            const ControllerConfig* cfg = findConfig(m_controllerConfigs, dev.vid, dev.pid,
                                                     dev.connectionType, "", dev.name);
            // Show hardware name; config source_name in gray when it differs.
            const std::string& hwName  = dev.name;
            bool isActive = (dev.vid == activeDevice.vid && dev.pid == activeDevice.pid
                          && dev.hidPath == activeDevice.hidPath);

            if (isActive) {
                ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "  >");
                ImGui::SameLine();
                ImGui::Text("[HID]  %s    VID:%04X  PID:%04X", hwName.c_str(), dev.vid, dev.pid);
            } else {
                ImGui::Text("   ");
                ImGui::SameLine();
                ImGui::Text("[HID]  %s    VID:%04X  PID:%04X", hwName.c_str(), dev.vid, dev.pid);
                ImGui::SameLine();

                char btnLabel[64];
                snprintf(btnLabel, sizeof(btnLabel), "%s##dev%d", tr("btn.activate"), i);

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

    // â"€â"€ Game profile selector â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    ImGui::Text("%s", tr("engine.profile"));
    ImGui::SameLine();

    std::vector<const char*> profileItems;
    profileItems.push_back(tr("engine.no_profile"));
    for (const auto& n : m_profileNames)
        profileItems.push_back(n.c_str());

    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("##profile", &m_profileSelected, profileItems.data(), (int)profileItems.size())) {
        if (m_profileSelected == 0)
            m_engine.setProfilePath("");
        else
            m_engine.setProfilePath(m_profilePaths[m_profileSelected - 1]);
    }

    // Output type selector continues on the SAME row as the profile selector.
    ImGui::SameLine();

    // ── Virtual output type selector (Xbox / DualShock), hot-swapped ─────────
    // Picking a different type just raises m_outputConfirmOpen; the confirmation modal
    // is drawn in renderFrame at canvas level (never inside this tab) so it is always
    // submitted every frame and can't ghost-freeze the app.
    ImGui::Text("%s", tr("engine.output_type"));
    ImGui::SameLine();
    const char* outputItems[] = { tr("engine.output_xbox"), tr("engine.output_ds4") };
    int liveType = (m_engine.getOutputType() == VirtualOutputType::DualShock) ? 1 : 0;
    // While confirming, show the user's pick; otherwise mirror the engine's live type.
    int outputShown = m_outputConfirmOpen ? m_pendingOutputSel : liveType;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("##outputtype", &outputShown, outputItems, 2)) {
        m_pendingOutputSel  = outputShown;
        m_outputConfirmOpen = (outputShown != liveType);  // nothing to confirm if back to current
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("engine.output_tooltip"));

    // Spinner while the engine tears down and rebuilds the virtual target.
    if (m_engine.isOutputSwitchPending()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("engine.output_switching"));
    }

    // Status line under the selectors row: active profile / reconnect hint.
    std::string activeName = m_engine.getActiveProfileName();
    if (!activeName.empty()) {
        ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.0f }, tr("engine.profile_active"), activeName.c_str());
    } else if (connected && m_profileSelected != 0) {
        ImGui::TextDisabled("%s", tr("engine.reconnect"));
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("engine.console_hint"));
    ImGui::TextDisabled("%s", tr("engine.close_hint"));
}

// ---------------------------------------------------------------------------
// Scanner tab â€" helpers
// ---------------------------------------------------------------------------


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

// Draws a 3Ã—3 compass widget showing the active POV direction.
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
// Scanner tab â€" main render
// ---------------------------------------------------------------------------

void AppWindow::renderScannerTab() {
    ULONGLONG now  = GetTickCount64();
    uint16_t  vVid = m_engine.getVirtualVid();
    uint16_t  vPid = m_engine.getVirtualPid();

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
        // Remove virtual pad
        raw.erase(std::remove_if(raw.begin(), raw.end(), [&](const HIDScanner::DeviceInfo& h) {
            return vVid && h.vid == vVid && h.pid == vPid;
        }), raw.end());
        m_hidDevices = std::move(raw);
        if (m_hidSelected >= (int)m_hidDevices.size()) m_hidSelected = -1;
        m_hidScanRunning = false;
    }

    ImGui::Spacing();

    // â"€â"€ Splitter â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    const float splitterW  = 6.0f;
    const float minPanelW  = 120.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    m_scanSplitX = std::clamp(m_scanSplitX, minPanelW, availW - minPanelW - splitterW);

    // â"€â"€ Left panel: device list â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    ImGui::BeginChild("##DeviceList", { m_scanSplitX, 0.0f }, true);

    ImGui::Text("HID(% zu)", m_hidDevices.size());
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("btn.refresh")))
        kickHidScan();
    ImGui::Separator();

    if (m_hidDevices.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("scanner.no_devices"));
    } else {
        for (int i = 0; i < (int)m_hidDevices.size(); ++i) {
            const auto& dev = m_hidDevices[i];
            const ControllerConfig* cfg = findConfig(m_controllerConfigs, dev.vid, dev.pid);
            // Always show the raw device name so we can see what the hardware reports.
            const std::string& rawName = dev.productName;
            char label[128];
            if (!rawName.empty())
                snprintf(label, sizeof(label), "[HID] %s", rawName.c_str());
            else
                snprintf(label, sizeof(label), "[HID] VID:%04X PID:%04X", dev.vid, dev.pid);

            bool sel = (m_hidSelected == i);
            if (ImGui::Selectable(label, sel, 0, { 0, 0 }))
                m_hidSelected = i;
            ImGui::SameLine();
            if (cfg)
                ImGui::TextDisabled("  %s  VID:%04X PID:%04X", cfg->source_name.c_str(), dev.vid, dev.pid);
            else
                ImGui::TextDisabled("  VID:%04X PID:%04X", dev.vid, dev.pid);
        }
    }

    ImGui::EndChild();

    // â"€â"€ Draggable splitter handle â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    ImGui::SameLine();
    ImGui::InvisibleButton("##splitter", { splitterW, -1.0f });
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
        m_scanSplitX += ImGui::GetIO().MouseDelta.x;

    ImGui::SameLine();

    // â"€â"€ Right panel: input monitor â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    ImGui::BeginChild("##InputMonitor", { 0.0f, 0.0f }, true);

    // â"€â"€ HID device live monitor â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    if (m_hidSelected < 0 || m_hidSelected >= (int)m_hidDevices.size()) {
        if (m_scanWatchOwned) m_deviceHub.unwatch(m_scanWatchedPath);
        m_scanWatchOwned = false;
        m_scanWatchedPath.clear();
        m_scanDeviceIdx  = -1;
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("scanner.hint"));
        ImGui::EndChild();
        return;
    }

    const auto& hdev = m_hidDevices[m_hidSelected];
    const ControllerConfig* cfg = findConfig(m_controllerConfigs, hdev.vid, hdev.pid,
                                             hdev.connectionType);

    // Selection changed — re-arm the IMU block detector for the new device.
    if (m_hidSelected != m_scanDeviceIdx) {
        if (m_scanWatchOwned) m_deviceHub.unwatch(m_scanWatchedPath);
        m_scanWatchOwned  = false;
        m_scanDeviceIdx   = m_hidSelected;
        m_scanWatchedPath = hdev.path;

        m_scanImuOffsets.clear();
        m_scanImuMinMax.clear();
        m_scanImuDetectFrames = 0;
        m_scanImuDetecting    = true;

        m_scanTouchOffset = (cfg && cfg->touchpad.enabled) ? cfg->touchpad.dataOffset : 35;
    }

    // The connection itself lives in DeviceHub, shared with the engine — no independent handle
    // here anymore. If the engine already owns this exact device we just read its snapshot
    // (kept fresh by the engine's own reads); otherwise we watch it ourselves. Re-checked every
    // frame so the panel follows the engine picking up/dropping this device live, without ever
    // holding a second, conflicting handle open to it (see ARCHITECTURE.md, "DeviceHub").
    DeviceCandidate activeDevice  = m_engine.getActiveDevice();
    bool            isEngineOwned = (!activeDevice.hidPath.empty() && activeDevice.hidPath == hdev.path);
    if (isEngineOwned && m_scanWatchOwned) {
        m_deviceHub.unwatch(m_scanWatchedPath);
        m_scanWatchOwned = false;
    } else if (!isEngineOwned && !m_scanWatchOwned) {
        char nameLabel[64];
        snprintf(nameLabel, sizeof(nameLabel), "VID:%04X PID:%04X", hdev.vid, hdev.pid);
        m_deviceHub.watch(hdev.path, hdev.productName.empty() ? nameLabel : hdev.productName);
        m_scanWatchOwned = true;
    }

    // Header
    ImGui::Spacing();
    ImGui::Text("%s", hdev.productName.empty() ? tr("scanner.default_name") : hdev.productName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("VID: % 04X PID : % 04X", hdev.vid, hdev.pid);
    if (cfg)
        ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "Config: %s", cfg->source_name.c_str());
    else {
        ImGui::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "%s", tr("scanner.no_config"));
        ImGui::TextDisabled("Add to controllers.json: vid \"%04X\" pid \"%04X\" mode \"hid\"",
                            hdev.vid, hdev.pid);
    }
    if (!m_deviceHub.isOpen(hdev.path)) {
        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", tr("scanner.disconnected"));
        ImGui::EndChild();
        return;
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RawHIDState snap = m_deviceHub.snapshot(hdev.path).raw;

    // ── IMU block auto-detection (see BITACORA 2026/08/11) ──────────────────────────────
    // Same "alive offset" test the wizard uses for Baseline (REFERENCE.md, "Wizard de
    // calibracion IMU"): amp>0 rules out a constant padding byte, amp<noiseFloor rules out a
    // CRC/counter byte that cycles through its whole range every frame. Unlike the wizard's
    // guided Baseline this window isn't a controlled "hold still" — the user may be moving the
    // controller while browsing Scanner — so the floor is looser and this is less certain;
    // needs real-hardware validation. Works the same whether the engine or DeviceHub's own watch
    // thread is driving the reads — both populate the same shared raw snapshot.
    if (m_scanImuDetecting && !snap.raw.empty()) {
        int n = static_cast<int>(snap.raw.size()) - 1;
        if (static_cast<int>(m_scanImuMinMax.size()) != n)
            m_scanImuMinMax.assign(n, { 0.0f, 0.0f });
        for (int o = 0; o < n; ++o) {
            int16_t raw = static_cast<int16_t>(
                static_cast<uint8_t>(snap.raw[o]) | (static_cast<uint16_t>(snap.raw[o + 1]) << 8));
            float v = static_cast<float>(raw);
            if (m_scanImuDetectFrames == 0) m_scanImuMinMax[o] = { v, v };
            else {
                m_scanImuMinMax[o].first  = std::min(m_scanImuMinMax[o].first,  v);
                m_scanImuMinMax[o].second = std::max(m_scanImuMinMax[o].second, v);
            }
        }
        ++m_scanImuDetectFrames;

        if (m_scanImuDetectFrames >= kScanImuDetectFrames) {
            constexpr float kNoiseFloor = 6000.0f; // looser than the wizard's 800 — uncontrolled window
            std::vector<bool> alive(n, false);
            for (int o = 0; o < n; ++o) {
                float amp = m_scanImuMinMax[o].second - m_scanImuMinMax[o].first;
                alive[o] = amp > 0.0f && amp < kNoiseFloor;
            }
            // Longest run of alive offsets spaced 2 bytes apart, capped to 6 (accel+gyro) —
            // simpler than the wizard's computeGyroCandidatePool() (no borderline-bridging, no
            // trim-from-flattest-end for a run >6): good enough for a live glance, not a
            // calibration source.
            int bestStart = -1, bestLen = 0;
            for (int s = 0; s < n; ++s) {
                if (!alive[s]) continue;
                if (s >= 2 && alive[s - 2]) continue;
                int len = 0;
                while (s + len * 2 < n && alive[s + len * 2]) ++len;
                if (len > bestLen) { bestLen = len; bestStart = s; }
            }
            if (bestLen >= 3) {
                int runLen = std::min(bestLen, 6);
                m_scanImuOffsets.clear();
                for (int k = 0; k < runLen; ++k) m_scanImuOffsets.push_back(bestStart + k * 2);
            }
            m_scanImuDetecting = false;
        }
    }

    // ── D-pad/hat compass + buttons, side by side ────────────────────────────────────────
    // The hat compass used to render at the very bottom of the tab — moved here, first
    // component, to the left of the button grid (2026/08/11 redesign).
    ImGui::BeginGroup();
    ImGui::Text("%s", tr("scanner.hat"));
    ImGui::Separator();
    ImGui::Spacing();
    {
        DWORD pov = JOY_POVCENTERED;
        if (snap.hat < 8)
            pov = snap.hat * 4500;
        drawPOVCompass(pov);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 16.0f);

    ImGui::BeginGroup();
    ImGui::Text("%s", tr("scanner.buttons"));
    ImGui::Separator();
    ImGui::Spacing();
    for (int i = 0; i < 32; ++i) {
        bool pressed = (snap.buttonMask & (1u << i)) != 0;
        ImGui::PushStyleColor(ImGuiCol_Button,
            pressed ? ImVec4(0.15f, 0.75f, 0.15f, 1.0f) : ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            pressed ? ImVec4(0.25f, 0.85f, 0.25f, 1.0f) : ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
        char lbl[4]; snprintf(lbl, sizeof(lbl), "%d", i + 1);
        ImGui::Button(lbl, { 34.0f, 34.0f });
        ImGui::PopStyleColor(2);
        if ((i + 1) % 16 != 0) ImGui::SameLine(0.0f, 4.0f);
    }
    ImGui::EndGroup();

    // ── Axes | vertical divider (same technique as CalibrationPanel) | 6-value IMU block ──
    ImGui::Spacing();
    ImGui::Spacing();
    ImVec2 axesRowStart = ImGui::GetCursorScreenPos();
    float  fullW        = ImGui::GetContentRegionAvail().x;
    constexpr float kAxesImuGap = 30.0f;
    float axesW = fullW * 0.55f - kAxesImuGap;
    float barW  = axesW - 60.0f;

    ImGui::BeginGroup();
    ImGui::Text("%s", tr("scanner.axes"));
    ImGui::Separator();
    ImGui::Spacing();
    struct { const char* name; float v; } axes[] = {
        { "X",     snap.axisX     },
        { "Y",     snap.axisY     },
        { "Z",     snap.axisZ     },
        { "Rx",    snap.axisRx    },
        { "Ry",    snap.axisRy    },
        { "Rz",    snap.axisRz    },
        { "Brake", snap.axisBrake },
        { "Accel", snap.axisAccel },
    };
    for (auto& a : axes) {
        float dev_f = fabsf(a.v);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            ImVec4(0.20f + dev_f * 0.60f, 0.55f - dev_f * 0.20f, 0.15f, 1.0f));
        ImGui::Text("%-5s", a.name);
        ImGui::SameLine();
        char ov[12]; snprintf(ov, sizeof(ov), "%+.3f", a.v);
        ImGui::ProgressBar((a.v + 1.0f) * 0.5f, { barW, 18.0f }, ov);
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    float axesBottom = ImGui::GetItemRectMax().y;

    ImGui::SetCursorScreenPos({ axesRowStart.x + axesW + kAxesImuGap * 2.0f, axesRowStart.y });
    ImGui::BeginGroup();
    ImGui::Text("%s", tr("scanner.imu_title"));
    ImGui::Separator();
    ImGui::Spacing();
    if (m_scanImuDetecting) {
        int pct = std::min(100, (m_scanImuDetectFrames * 100) / kScanImuDetectFrames);
        ImGui::TextDisabled(tr("scanner.imu_detecting"), pct);
    } else if (m_scanImuOffsets.empty()) {
        ImGui::TextDisabled("%s", tr("scanner.imu_not_found"));
    } else {
        // Raw int16, normalised to [0,1] (not [-1,1] like the declared axes above) so a
        // gyro axis sits near the middle (~0 at rest) and an accel axis sitting off-centre
        // toward one end reads as "this one is gravity" at a glance — same idea as the flip
        // gesture in the wizard, just eyeballed instead of auto-classified.
        float imuBarW = fullW - axesW - kAxesImuGap * 2.0f - 60.0f;
        for (size_t k = 0; k < m_scanImuOffsets.size(); ++k) {
            int o = m_scanImuOffsets[k];
            int16_t raw = 0;
            if (o + 1 < static_cast<int>(snap.raw.size()))
                raw = static_cast<int16_t>(
                    static_cast<uint8_t>(snap.raw[o]) | (static_cast<uint16_t>(snap.raw[o + 1]) << 8));
            float norm = (static_cast<float>(raw) + 32768.0f) / 65536.0f;
            char lbl[8]; snprintf(lbl, sizeof(lbl), "#%d", static_cast<int>(k) + 1);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
            ImGui::Text("%-4s", lbl);
            ImGui::SameLine();
            char ov[16]; snprintf(ov, sizeof(ov), "%.3f", norm);
            ImGui::ProgressBar(norm, { imuBarW, 18.0f }, ov);
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndGroup();
    float imuBottom = ImGui::GetItemRectMax().y;

    float sepX      = axesRowStart.x + axesW + kAxesImuGap;
    float sepBottom = std::max(axesBottom, imuBottom);
    ImGui::GetWindowDrawList()->AddLine({ sepX, axesRowStart.y }, { sepX, sepBottom },
                                        IM_COL32(90, 100, 120, 140), 1.5f);
    ImGui::SetCursorScreenPos({ axesRowStart.x, sepBottom });

    // Touch + Raw bytes run tighter than the rest of the tab — they're dense diagnostic dumps,
    // not a handful of controls, and the default vertical rhythm (ItemSpacing.y=6, set up near
    // the top of AppWindow.cpp) was tall enough to force the InputMonitor child to scroll once
    // both blocks were in. Popped again right before EndChild().
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2.0f });

    // ── Touch: manual-offset live decode ─────────────────────────────────────────────────
    // No auto-detection here (unlike the IMU block above) — an "active" bit doesn't drift like
    // a sensor value, so there's no amplitude heuristic to lean on. The user tunes the offset by
    // hand while touching the pad, same as they already did to pin down data_offset=35 for the
    // DS4 (BITACORA.md, 2026/08/17). Fixed at 2 slots (DS4's 2 fingers, and the best starting
    // guess for the DualSense). All slots share a SINGLE text line (not one line per active
    // touch) so the row height never changes as fingers land/lift — otherwise every touch/
    // release would shift the Raw bytes block below up and down.
    ImGui::Spacing();
    ImGui::Text("%s", tr("scanner.touch_title"));
    ImGui::Separator();
    ImGui::Spacing();
    constexpr int kScanTouchSlots = 2;
    int maxTouchOffset = std::max(0, static_cast<int>(snap.raw.size()) - kScanTouchSlots * 4);
    m_scanTouchOffset  = std::clamp(m_scanTouchOffset, 0, maxTouchOffset);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt(tr("scanner.touch_offset"), &m_scanTouchOffset);
    m_scanTouchOffset = std::clamp(m_scanTouchOffset, 0, maxTouchOffset);

    if (static_cast<int>(snap.raw.size()) < m_scanTouchOffset + kScanTouchSlots * 4) {
        ImGui::TextDisabled("%s", tr("scanner.touch_no_data"));
    } else {
        std::string touchLine;
        bool        anyActive = false;
        for (int i = 0; i < kScanTouchSlots; ++i) {
            int     o  = m_scanTouchOffset + i * 4;
            uint8_t b0 = snap.raw[o], b1 = snap.raw[o + 1], b2 = snap.raw[o + 2], b3 = snap.raw[o + 3];
            if ((b0 & 0x80) != 0) continue; // bit7 set = not touching
            anyActive = true;
            int x = b1 | ((b2 & 0x0F) << 8);
            int y = ((b2 & 0xF0) >> 4) | (b3 << 4);
            char seg[48];
            snprintf(seg, sizeof(seg), "Touch %d: X=%4d  Y=%4d", i + 1, x, y);
            if (!touchLine.empty()) touchLine += "    ";
            touchLine += seg;
        }
        if (!anyActive) touchLine = tr("scanner.touch_none");
        ImGui::TextColored(anyActive ? ImVec4{ 0.3f, 1.0f, 0.3f, 1.0f } : ImVec4{ 0.6f, 0.6f, 0.6f, 1.0f },
                            "%s", touchLine.c_str());
    }

    // ── Raw bytes: full report dump, enumerated in index/value row pairs ────────────────
    // No decoding, no guessing at an offset — every byte of the raw HID report, indexed, so an
    // unknown device's layout (DualSense, Steam Controller...) can be read straight off the
    // screen while touching/pressing things, instead of hunting for it one offset at a time
    // (see SESSION_CONTEXT.md "Wizard", discussion 2026/08/29). Same bytes the [HID][raw] trace
    // (HIDInputSource.cpp) already logs to file — this is just the live, always-visible version.
    // Indices are decimal (not the usual hex-dump convention) to match the decimal "Offset"
    // field of the Touch block above, so a byte found here can be typed there directly. Laid
    // out as an index row followed by its value row (32 columns per pair, not one row per 16
    // bytes with a leading offset label) — easier to read a specific column straight down.
    ImGui::Spacing();
    ImGui::Text("%s", tr("scanner.raw_title"));
    ImGui::Separator();
    ImGui::Spacing();
    {
        constexpr int kBytesPerBlock = 32;
        // Index and value share the exact same format — 2 digits, zero-padded — so a column
        // lines up visually between its label and its value with no width mismatch to eyeball.
        int n = static_cast<int>(snap.raw.size());
        for (int blockStart = 0; blockStart < n; blockStart += kBytesPerBlock) {
            int blockEnd = std::min(blockStart + kBytesPerBlock, n);
            std::string idxLine, valLine;
            for (int i = blockStart; i < blockEnd; ++i) {
                char idxTok[8]; snprintf(idxTok, sizeof(idxTok), "%02d ", i);
                idxLine += idxTok;
                char valTok[8]; snprintf(valTok, sizeof(valTok), "%02X ", snap.raw[i]);
                valLine += valTok;
            }
            ImGui::TextDisabled("%s", idxLine.c_str());
            ImGui::TextUnformatted(valLine.c_str());
            ImGui::Spacing();
        }
    }

    ImGui::PopStyleVar(); // ItemSpacing pushed before the Touch block above

    ImGui::EndChild();
}


// ---------------------------------------------------------------------------
// Window / D3D11 initialisation
// ---------------------------------------------------------------------------

bool AppWindow::initWindow() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Load the app icon embedded via PadsWay.rc (ICON resource id 1) at both the
    // large (Alt-Tab) and small (title-bar) sizes. The exe's embedded icon already
    // drives the taskbar/Explorer, but the window's own title-bar icon is read from
    // the window class — without this it falls back to the generic default.
    HICON hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = hIconBig;
    wc.hIconSm       = hIconSmall;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PadsWayWindow";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0, L"PadsWayWindow", L"PadsWay",
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

void AppWindow::refreshProfileList() {
    m_profilePaths.clear();
    m_profileNames.clear();
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(Paths::userData("data/profiles/*.json").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string path = Paths::userData("data/profiles/") + std::string(fd.cFileName);
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

    // Virtual pad: layout follows the active output type (Xbox / DS4), reloaded on hot-swap.
    // The internal model is always Xbox; the DS4 layout just renders the same state with PS
    // glyphs (its components carry the Xbox "state" binding) and omits touch/gyro (not emitted).
    const char* wantVirtualLayout =
        (m_engine.getOutputType() == VirtualOutputType::DualShock)
            ? "dualshock4_virtual" : "xbox_one";
    if (m_currentVirtualLayoutId != wantVirtualLayout) {
        const PadLayout* vLayout = findLayout(m_padLayouts, wantVirtualLayout);
        if (vLayout) {
            m_virtualPadView.setLayout(*vLayout);
            m_currentVirtualLayoutId = wantVirtualLayout;
        }
    }

    {
      if (!m_mappingEditor.isActive() && !m_macroManager.isActive() && !m_calibrationPanel.isActive()) {
        ImGui::Spacing();

        if (!m_engine.isConnected()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("engine.waiting"));
        } else {
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
        } // isConnected

        // ── Botones Mapper / Perfiles / Macros ────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button(trid("mapper.title", "openMapping").c_str(), { 120.0f, 0.0f })) {
            m_mappingEditor.setConfigs(m_controllerConfigs);
            m_mappingEditor.activate();
            m_engine.setEditorOpen(true);
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(trid("profiles.title", "openProfiles").c_str(), { 140.0f, 0.0f })) {
            m_mappingEditor.setConfigs(m_controllerConfigs);
            int presel = m_profileSelected > 0 ? m_profileSelected - 1 : -1;
            m_mappingEditor.activateProfile(m_profilePaths, m_profileNames, presel);
            m_engine.setEditorOpen(true);
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(trid("macros.title", "openMacros").c_str(), { 100.0f, 0.0f }))
            m_macroManager.activate();
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(trid("calibration.title", "openCalibration").c_str(), { 120.0f, 0.0f })) {
            m_calibrationPanel.setConfigs(m_controllerConfigs);
            m_calibrationPanel.activate();
        }

            // ── Marquee ───────────────────────────────────────────────────────────

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

    // 3. Render â€" always 4 slots so the area height is constant from the first entry
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
            // Fade: slot 0 (oldest) = 0.25 alpha, slot 3 (newest) = 1.0 â€" fixed scale of 4
            col.w = 0.25f + 0.75f * ((float)(slot + 1) / 4.0f);
            ImGui::TextColored(col, "%s", entry.text.c_str());
        } else {
            // Empty slot â€" reserve the line height so the layout doesn't jump
            ImGui::Dummy({ 1.0f, ImGui::GetTextLineHeight() });
        }
    }

      } // !m_mappingEditor.isActive() && !m_macroManager.isActive() && !m_calibrationPanel.isActive()
    }

    if (m_mappingEditor.isActive()) {
        m_mappingEditor.render(m_padView, m_virtualPadView);
        if (!m_mappingEditor.isActive())
            m_engine.setEditorOpen(false);
        if (m_mappingEditor.pollConfigsSaved()) {
            m_controllerConfigs = loadControllerConfigs(Paths::userData("data/controllers.json"));
            m_mappingEditor.setConfigs(m_controllerConfigs);
        }
        if (m_mappingEditor.pollProfileListChanged()) {
            refreshProfileList();
            m_mappingEditor.updateProfileList(m_profilePaths, m_profileNames);
        }
    }

    if (m_macroManager.isActive())
        m_macroManager.render();
    if (m_macroManager.pollMacrosSaved())
        m_engine.reloadMacros();

    if (m_calibrationPanel.isActive())
        m_calibrationPanel.render();
    if (m_calibrationPanel.pollCalibrationSaved()) {
        m_controllerConfigs = loadControllerConfigs(Paths::userData("data/controllers.json"));
        m_calibrationPanel.setConfigs(m_controllerConfigs);
        m_engine.reloadConfigs();
    }
}

// ---------------------------------------------------------------------------
// Layout tab
// ---------------------------------------------------------------------------

void AppWindow::renderLayoutTab() {
    ImGui::Spacing();

    if (m_layoutsFromBackup) {
        ImGui::TextColored({ 1.0f, 0.7f, 0.1f, 1.0f },
            "%s", tr("layout.backup_warning"));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    m_layoutEditor.render();

    if (m_layoutEditor.pollControllersSaved()) {
        m_controllerConfigs = loadControllerConfigs(Paths::userData("data/controllers.json"));
        m_engine.reloadConfigs();
        m_forceLayoutReload = true;
    }

    if (m_layoutEditor.pollLayoutSaved()) {
        m_forceLayoutReload = true;
        m_currentVirtualLayoutId.clear();   // force virtual layout reload next frame
    }
}

// ---------------------------------------------------------------------------

void AppWindow::cleanup() {
    if (m_scanWatchOwned) m_deviceHub.unwatch(m_scanWatchedPath);
    m_mappingEditor.unload();
    m_layoutEditor.unload();
    m_virtualPadView.unload();
    m_padView.unload();
    m_arrowRightTex.release();
    cleanupRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context)   { m_context->Release();   m_context   = nullptr; }
    if (m_device)    { m_device->Release();     m_device    = nullptr; }
    if (m_hwnd)      { DestroyWindow(m_hwnd);   m_hwnd      = nullptr; }
    UnregisterClassW(L"PadsWayWindow", GetModuleHandle(nullptr));
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
