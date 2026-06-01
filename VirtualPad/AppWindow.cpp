#include "AppWindow.h"
#include "Log.h"

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

    // Load virtualpad.json early to get font_size before ImGui font init.
    VirtualPadConfig vpCfgEarly;
    try { vpCfgEarly = loadVirtualPadConfig("data/virtualpad.json"); } catch (...) {}

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
        m_controllerConfigs = loadControllerConfigs("data/controllers.json");
    } catch (...) {}   // optional â€" scanner falls back to WinMM names if missing

    std::string locale = "en";
    try {
        VirtualPadConfig vpCfg = loadVirtualPadConfig("data/virtualpad.json");
        m_acceptedXboxButtons  = vpCfg.acceptedXboxButtons;
        m_stickSelectThreshold = vpCfg.stickSelectThreshold;
        m_stickHoldMs          = vpCfg.stickHoldMs;
        locale = vpCfg.locale;
    } catch (...) {}  // struct defaults apply if file is missing or malformed
    Strings::load(locale);

    try { m_padLayouts = loadPadLayouts("data/pad_layouts.json"); } catch (...) {}
    if (m_padLayouts.empty()) {
        try { m_padLayouts = loadPadLayouts("data/pad_layouts.json.bak"); } catch (...) {}
        m_layoutsFromBackup = !m_padLayouts.empty();
    }

    // Discover game profiles
    refreshProfileList();

    m_padView.load(m_device);
    m_virtualPadView.load(m_device);
    m_layoutEditor.init(m_device, &m_padLayouts);
    m_mappingEditor.init(m_device, &m_engine, m_padLayouts,
                         m_acceptedXboxButtons, m_stickSelectThreshold, m_stickHoldMs);
    m_mappingEditor.setConfigs(m_controllerConfigs);
    m_macroManager.init(m_device);

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
        ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.0f }, tr("engine.profile_active"), activeName.c_str());
    } else if (connected && m_profileSelected != 0) {
        ImGui::SameLine();
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
        if (m_scanDevice) {
            stopScanReaderThread();
            m_scanDevice.reset();
            { std::lock_guard<std::mutex> lk(m_scanRawMutex); m_scanRawState = {}; }
            m_scanDeviceIdx = -1;
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("scanner.hint"));
        ImGui::EndChild();
        return;
    }

    const auto& hdev = m_hidDevices[m_hidSelected];
    const ControllerConfig* cfg = findConfig(m_controllerConfigs, hdev.vid, hdev.pid,
                                             hdev.connectionType);

    // Open/close m_scanDevice when the selection changes
    if (m_hidSelected != m_scanDeviceIdx) {
        stopScanReaderThread();     // stop before resetting device handle
        m_scanDevice.reset();
        { std::lock_guard<std::mutex> lk(m_scanRawMutex); m_scanRawState = {}; }
        m_scanDeviceIdx = m_hidSelected;
        char nameLabel[64];
        snprintf(nameLabel, sizeof(nameLabel), "VID:%04X PID:%04X", hdev.vid, hdev.pid);
        m_scanDevice = std::make_unique<RawHIDReader>(
            hdev.path,
            hdev.productName.empty() ? nameLabel : hdev.productName);
        startScanReaderThread();    // background thread for event-driven devices
    }

    // Read new data. For devices using XInput/Xbox HID filter the driver only
    // delivers reports to the first opener (the engine). When the selected device
    // is the active engine device, read from the engine instead of the raw handle.
    DeviceCandidate activeDevice = m_engine.getActiveDevice();
    bool useEngineData = (!activeDevice.hidPath.empty() &&
                          activeDevice.hidPath == hdev.path);
    if (useEngineData) {
        // Engine owns the device handle; background thread can't read independently.
        // Use the engine's stored raw state (buttons + hat + axes) instead.
        m_scanDataFromEngine = true;
        std::lock_guard<std::mutex> lk(m_scanRawMutex);
        m_scanRawState.buttonMask = m_engine.getLastRawButtonMask();
        m_scanRawState.hat        = m_engine.getLastRawHat();
        GamepadState phys = m_engine.getLastState();
        m_scanRawState.axisX  = phys.leftX;
        m_scanRawState.axisY  = phys.leftY;
        m_scanRawState.axisZ  = phys.triggerL;
        m_scanRawState.axisRx = phys.rightX;
        m_scanRawState.axisRy = phys.rightY;
        m_scanRawState.axisRz = phys.triggerR;
    } else {
        m_scanDataFromEngine = false;  // background thread can write freely
        if (m_scanDevice && !m_scanDevice->isOpen()) {
            // Device disconnected — stop background thread and clean up.
            stopScanReaderThread();
            m_scanDevice.reset();
            { std::lock_guard<std::mutex> lk(m_scanRawMutex); m_scanRawState = {}; }
            m_scanDeviceIdx = -1;
        }
    }
    // Background thread handles reading for non-engine devices; render thread just reads the shared state.

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
    if (!useEngineData && (!m_scanDevice || !m_scanDevice->isOpen())) {
        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", tr("scanner.disconnected"));
        ImGui::EndChild();
        return;
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Take a snapshot of the raw state under the mutex so the render thread is consistent.
    RawHIDState snap;
    { std::lock_guard<std::mutex> lk(m_scanRawMutex); snap = m_scanRawState; }

    const float barW = ImGui::GetContentRegionAvail().x - 60.0f;

    // Buttons — raw HID button numbers 1-32
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

    // Axes — raw HID axis values [-1, 1]
    ImGui::Spacing();
    ImGui::Spacing();
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

    // Gyro (raw bytes offset 13 — DS4 USB only; other controllers may show noise)
    if (snap.gyroRawValid) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Gyro (IMU)");
        ImGui::Separator();
        ImGui::Spacing();
        struct { const char* name; float v; } gyros[] = {
            { "Gx", snap.gyroRawX },
            { "Gy", snap.gyroRawY },
            { "Gz", snap.gyroRawZ },
        };
        for (auto& g : gyros) {
            float dev_f = fabsf(g.v);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                ImVec4(0.20f + dev_f * 0.60f, 0.55f - dev_f * 0.20f, 0.15f, 1.0f));
            ImGui::Text("%-5s", g.name);
            ImGui::SameLine();
            char ov[12]; snprintf(ov, sizeof(ov), "%+.3f", g.v);
            ImGui::ProgressBar((g.v + 1.0f) * 0.5f, { barW, 18.0f }, ov);
            ImGui::PopStyleColor();
        }
    }

    // Hat switch — raw hat value → compass widget
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text("%s", tr("scanner.hat"));
    ImGui::Separator();
    ImGui::Spacing();
    DWORD pov = JOY_POVCENTERED;
    if (snap.hat < 8)
        pov = snap.hat * 4500;
    drawPOVCompass(pov);

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

void AppWindow::refreshProfileList() {
    m_profilePaths.clear();
    m_profileNames.clear();
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA("data\\profiles\\*.json", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string path = "data/profiles/" + std::string(fd.cFileName);
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

    // Virtual pad: always xbox_one, loaded once
    if (!m_virtualPadInitialized) {
        const PadLayout* vLayout = findLayout(m_padLayouts, "xbox_one");
        if (vLayout) {
            m_virtualPadView.setLayout(*vLayout);
            m_virtualPadInitialized = true;
        }
    }

    {
      if (!m_mappingEditor.isActive() && !m_macroManager.isActive()) {
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

      } // !m_mappingEditor.isActive() && !m_macroManager.isActive()
    }

    if (m_mappingEditor.isActive()) {
        m_mappingEditor.render(m_padView, m_virtualPadView);
        if (!m_mappingEditor.isActive())
            m_engine.setEditorOpen(false);
        if (m_mappingEditor.pollConfigsSaved()) {
            m_controllerConfigs = loadControllerConfigs("data/controllers.json");
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
        m_controllerConfigs = loadControllerConfigs("data/controllers.json");
        m_engine.reloadConfigs();
        m_forceLayoutReload = true;
    }

    if (m_layoutEditor.pollLayoutSaved()) {
        m_forceLayoutReload     = true;
        m_virtualPadInitialized = false;
    }
}

// ---------------------------------------------------------------------------

void AppWindow::startScanReaderThread() {
    m_scanReaderStop     = false;
    m_scanDataFromEngine = false;
    m_scanReaderThread = std::thread([this] {
        RawHIDState local {};  // persists last known state across reads
        while (!m_scanReaderStop) {
            if (m_scanDevice && m_scanDevice->isOpen()) {
                m_scanDevice->read(local, 20);  // blocking; keeps last state on timeout
                if (!m_scanDataFromEngine) {    // don't overwrite when engine owns the state
                    std::lock_guard<std::mutex> lk(m_scanRawMutex);
                    m_scanRawState = local;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    });
}

void AppWindow::stopScanReaderThread() {
    m_scanReaderStop = true;
    if (m_scanReaderThread.joinable())
        m_scanReaderThread.join();
}

void AppWindow::cleanup() {
    stopScanReaderThread();
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
