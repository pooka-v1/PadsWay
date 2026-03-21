#include "PadEngine.h"

#include <iostream>
#include <cmath>
#include <memory>
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <unordered_map>
#include <string>

#include "input/EightBitDoInputSource.h"
#include "input/HIDScanner.h"
#include "input/HIDInputSource.h"
#include "output/ViGEmOutputAdapter.h"
#include "config/ConfigLoader.h"
#include "bots/LightningBot.h"
#include "macros/Macro.h"
#include "macros/MacroParser.h"

#pragma comment(lib, "ViGEmClient.lib")
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")

// ---------------------------------------------------------------------------
// Internal helpers (not exposed in the header)
// ---------------------------------------------------------------------------

struct JoyEntry {
    UINT    id;
    UINT    axes;
    UINT    buttons;
    WORD    wMid;
    WORD    wPid;
    wchar_t name[MAXPNAMELEN];
};

static std::vector<JoyEntry> scanPorts() {
    std::vector<JoyEntry> result;
    UINT numDevs = joyGetNumDevs();
    for (UINT id = 0; id < numDevs; ++id) {
        JOYINFOEX info = {};
        info.dwSize  = sizeof(JOYINFOEX);
        info.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(id, &info) != JOYERR_NOERROR) continue;

        JoyEntry e = {};
        e.id = id;
        JOYCAPS caps = {};
        if (joyGetDevCaps(id, &caps, sizeof(caps)) == JOYERR_NOERROR) {
            e.axes    = caps.wNumAxes;
            e.buttons = caps.wNumButtons;
            e.wMid    = caps.wMid;
            e.wPid    = caps.wPid;
            wcsncpy_s(e.name, caps.szPname, MAXPNAMELEN);
        } else {
            wcscpy_s(e.name, L"(unknown)");
        }
        result.push_back(e);
    }
    return result;
}

static int findBotBit(const ControllerConfig& cfg, const std::string& botName) {
    for (const auto& [bit, action] : cfg.buttons)
        if (action.type == ButtonActionType::Bot && action.name == botName)
            return bit;
    return 0;
}

// ---------------------------------------------------------------------------
// PadEngine
// ---------------------------------------------------------------------------

PadEngine::PadEngine()  = default;
PadEngine::~PadEngine() { stop(); }

void PadEngine::start() {
    if (m_running.exchange(true)) return;  // already running
    m_thread = std::thread(&PadEngine::threadFunc, this);
}

void PadEngine::stop() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_connected = false;
}

std::string PadEngine::getDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_device;
}

std::string PadEngine::getStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::vector<DeviceCandidate> PadEngine::getCandidates() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_candidates;
}

void PadEngine::selectDevice(int index) {
    m_selectedIndex.store(index);
}

GamepadState PadEngine::getLastState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastState;
}

void PadEngine::setDevice(const std::string& s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = s;
}

void PadEngine::setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = s;
}

// ---------------------------------------------------------------------------
// Background thread: mirrors the original VirtualPad.cpp main() logic.
// ---------------------------------------------------------------------------

void PadEngine::threadFunc() {
    m_phase.store(EnginePhase::Scanning);
    setStatus("Scanning for devices...");
    std::cout << "\n=== VirtualPad — device init ===\n";

    // --- Step 1: Load configs early so the scan can route devices correctly ---
    // (Devices with mode="hid" must use HIDInputSource even if they appear in WinMM,
    //  because joyGetPosEx returns no data for them.)
    std::vector<ControllerConfig> configs;
    try {
        configs = loadControllerConfigs("data/controllers.json");
    } catch (const std::exception& ex) {
        std::cerr << "Error loading config: " << ex.what() << "\n";
        setStatus(std::string("Config error: ") + ex.what());
        m_running = false;
        return;
    }

    // --- Step 2: Find the real controller BEFORE creating the virtual one ---
    DeviceCandidate selected;

    while (m_running && selected.vid == 0) {
        // Scan both WinMM and HID
        auto winmmEntries = scanPorts();
        auto hidEntries   = HIDScanner::scan();

        std::vector<DeviceCandidate> allCandidates;

        // WinMM entries: skip devices that have a "hid" mode config
        // (those need HIDInputSource even if they appear in WinMM)
        for (auto& e : winmmEntries) {
            const ControllerConfig* cfg = findConfig(configs, e.wMid, e.wPid);
            if (cfg && cfg->mode == "hid") continue;

            DeviceCandidate c;
            c.source  = DeviceCandidate::Source::WinMM;
            c.port    = e.id;
            c.vid     = e.wMid;
            c.pid     = e.wPid;
            c.axes    = e.axes;
            c.buttons = e.buttons;
            char narrow[MAXPNAMELEN];
            WideCharToMultiByte(CP_UTF8, 0, e.name, -1, narrow, sizeof(narrow), nullptr, nullptr);
            c.name = narrow;
            allCandidates.push_back(c);
        }

        // HID entries: include if has "hid" config, or if not covered by WinMM (for discovery)
        for (auto& h : hidEntries) {
            const ControllerConfig* cfg = findConfig(configs, h.vid, h.pid);
            bool inWinMM = false;
            for (auto& e : winmmEntries)
                if (e.wMid == h.vid && e.wPid == h.pid) { inWinMM = true; break; }

            if (!cfg && inWinMM) continue; // unknown device already in WinMM — skip duplicate
            if (cfg && cfg->mode != "hid" && inWinMM) continue; // WinMM handles it

            DeviceCandidate c;
            c.source  = DeviceCandidate::Source::HID;
            c.hidPath = h.path;
            c.vid     = h.vid;
            c.pid     = h.pid;
            c.name    = h.productName.empty()
                ? ("HID " + std::to_string(h.vid) + ":" + std::to_string(h.pid))
                : h.productName;
            allCandidates.push_back(c);
        }

        printf("[Scan] WinMM: %zu entries, HID: %zu entries, Candidates: %zu\n",
               winmmEntries.size(), hidEntries.size(), allCandidates.size());
        for (auto& h : hidEntries)
            printf("[HID found] VID:%04X PID:%04X '%s' path=%s\n",
                   h.vid, h.pid, h.productName.c_str(), h.path.c_str());
        for (auto& c : allCandidates)
            printf("[Candidate] [%s] VID:%04X PID:%04X '%s'\n",
                   c.source == DeviceCandidate::Source::HID ? "HID" : "WinMM",
                   c.vid, c.pid, c.name.c_str());

        if (allCandidates.empty()) {
            setStatus("No device found — connect controller");
            Sleep(500);
            continue;
        }

        if (allCandidates.size() == 1) {
            selected = allCandidates[0];
            printf("Auto-selected [%s]: %s [VID=%04X PID=%04X]\n",
                selected.source == DeviceCandidate::Source::HID ? "HID" : "WinMM",
                selected.name.c_str(), selected.vid, selected.pid);
            setStatus(std::string("Auto-selected: ") + selected.name);
        } else {
            // Multiple devices — ask the user
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_candidates = allCandidates;
            }
            m_selectedIndex.store(-1);
            m_phase.store(EnginePhase::WaitingSelection);
            setStatus("Multiple controllers detected — select one in the Engine tab");

            while (m_running && m_selectedIndex.load() < 0)
                Sleep(50);

            if (!m_running) return;

            int idx = m_selectedIndex.load();
            if (idx < 0 || idx >= (int)allCandidates.size()) {
                m_phase.store(EnginePhase::Scanning);
                setStatus("Invalid selection — rescanning...");
                continue;
            }
            selected = allCandidates[idx];
            printf("User selected [%s]: %s [VID=%04X PID=%04X]\n",
                selected.source == DeviceCandidate::Source::HID ? "HID" : "WinMM",
                selected.name.c_str(), selected.vid, selected.pid);
        }
    }

    if (!m_running) return;

    m_phase.store(EnginePhase::Configuring);

    // --- Step 3: Load macros and find config for selected device ---
    setStatus("Loading config...");
    std::unordered_map<std::string, std::string> macroLibrary;
    try {
        macroLibrary = loadMacroLibrary("data/macros.json");
        if (!macroLibrary.empty())
            printf("Macro library loaded: %zu macros.\n", macroLibrary.size());
    } catch (const std::exception& ex) {
        fprintf(stderr, "Warning: could not load macro library: %s\n", ex.what());
    }

    const ControllerConfig* cfg = findConfig(configs, selected.vid, selected.pid);
    if (!cfg) {
        fprintf(stderr,
            "No config found for VID=%04X PID=%04X (%s).\n"
            "Add an entry to data/controllers.json and restart.\n",
            selected.vid, selected.pid, selected.name.c_str());
        setStatus("No config for this device");
        m_running = false;
        return;
    }
    printf("Config loaded: %s\n", cfg->source_name.c_str());
    setDevice(cfg->source_name);

    // --- Factory: pick the right IInputSource based on the config mode ---
    std::unique_ptr<IInputSource> input;
    if (selected.source == DeviceCandidate::Source::HID) {
        input = std::make_unique<HIDInputSource>(selected.hidPath, *cfg);
    } else {
        input = std::make_unique<EightBitDoInputSource>(selected.port, *cfg);
    }
    if (!input->isConnected()) {
        fprintf(stderr, "Failed to open input device.\n");
        setStatus("Failed to open input device");
        m_running = false;
        return;
    }

    // --- Step 3: Initialize ViGEm after the real port is secured ---
    // Load virtual pad identity (VID/PID) from config so the scanner can filter it out
    VirtualPadConfig vpCfg;
    try {
        vpCfg = loadVirtualPadConfig("data/virtualpad.json");
    } catch (const std::exception& ex) {
        fprintf(stderr, "Warning: could not load virtualpad.json: %s — using defaults.\n", ex.what());
    }
    m_virtualVid.store(vpCfg.vid);
    m_virtualPid.store(vpCfg.pid);
    printf("[PadEngine] Virtual pad identity: VID:%04X PID:%04X\n", vpCfg.vid, vpCfg.pid);

    setStatus("Connecting to ViGEm...");
    ViGEmOutputAdapter output(vpCfg.vid, vpCfg.pid);
    LightningBot       bot;
    if (!output.isReady()) {
        std::cerr << "Aborting: could not create virtual pad.\n";
        setStatus("ViGEm error — is the driver installed?");
        m_running = false;
        return;
    }

    int lightningBotBit = findBotBit(*cfg, "LightningBot");
    if (lightningBotBit > 0)
        printf("LightningBot assigned to button %d.\n", lightningBotBit);

    // --- Step 4: Parse macros ---
    std::unordered_map<int, Macro>       macros;
    std::unordered_map<int, bool>        macroPrevBtn;
    std::unordered_map<int, std::string> macroNames;
    std::unordered_map<int, int>         macroRotCount;
    std::unordered_map<int, float>       macroLastRX;
    std::unordered_map<int, float>       macroLastRY;

    for (const auto& [bit, action] : cfg->buttons) {
        if (action.type != ButtonActionType::Macro) continue;
        std::string execution = action.execution;
        if (execution.empty()) {
            auto it = macroLibrary.find(action.name);
            if (it == macroLibrary.end()) {
                fprintf(stderr, "Warning: macro '%s' (button %d) not found in library — skipped.\n",
                        action.name.c_str(), bit);
                continue;
            }
            execution = it->second;
        }
        try {
            Macro m;
            MacroParser::parse(execution, m);
            macros[bit]        = std::move(m);
            macroPrevBtn[bit]  = false;
            macroNames[bit]    = action.name;
            macroRotCount[bit] = 0;
            macroLastRX[bit]   = 0.0f;
            macroLastRY[bit]   = 0.0f;
            printf("Macro '%s' assigned to button %d.\n", action.name.c_str(), bit);
        } catch (const std::exception& ex) {
            fprintf(stderr, "Error parsing macro '%s': %s\n", action.name.c_str(), ex.what());
        }
    }

    // --- Main loop ---
    setStatus("Running");
    m_connected = true;
    std::cout << "Forwarding input. Close the window to exit.\n\n";

    GamepadState state;
    bool         botBtnPrev = false;
    bool         btnAPrev   = false;

    while (m_running) {
        if (!input->isConnected()) {
            if (m_connected) {
                printf("\r[%s] disconnected. Waiting...    ", cfg->source_name.c_str());
                m_connected = false;
                setStatus("Device disconnected — waiting...");
            }
            Sleep(500);
            continue;
        }

        if (!m_connected) {
            m_connected = true;
            setStatus("Running");
        }

        if (input->read(state)) {
            { std::lock_guard<std::mutex> lock(m_mutex); m_lastState = state; }
            // Bot and macro toggle detection uses the button mask from the read just performed
            DWORD btns = input->getLastButtonMask();

            if (lightningBotBit > 0) {
                bool pressed = (btns & (1u << (lightningBotBit - 1))) != 0;
                if (pressed && !botBtnPrev) {
                    bot.toggle();
                    printf("\n[BOT] Lightning bot %s\n", bot.isActive() ? "ON" : "OFF");
                }
                botBtnPrev = pressed;
            }

            for (auto& [bit, macro] : macros) {
                bool pressed = (btns & (1u << (bit - 1))) != 0;
                bool& prev   = macroPrevBtn[bit];

                if (pressed && !prev) {
                    if (macro.getMode() == MacroRepeatMode::UntilRelease)
                        macro.start();
                    else
                        macro.toggle();
                    if (macro.isActive()) {
                        macroRotCount[bit] = 0;
                        macroLastRX[bit]   = 0.0f;
                        macroLastRY[bit]   = 0.0f;
                    }
                    printf("\n[MACRO][%llu] '%s' %s\n",
                           GetTickCount64(), macroNames[bit].c_str(),
                           macro.isActive() ? "ON" : "OFF");
                }
                if (!pressed && prev)
                    if (macro.getMode() == MacroRepeatMode::UntilRelease)
                        macro.stop();
                prev = pressed;
            }

            if (state.btnA && !btnAPrev)
                printf("\n[MAN][%llu] Manual A press\n", GetTickCount64());
            btnAPrev = state.btnA;

            bool botPressed = bot.consumePressA();
            if (botPressed) state.btnA = true;

            for (auto& [bit, macro] : macros) {
                bool wasActive = macro.isActive();
                macro.tick(state);

                if (macro.isActive()
                    && (state.rightX != macroLastRX[bit] || state.rightY != macroLastRY[bit])) {
                    bool atNorth    = (fabsf(state.rightX) < 0.1f && state.rightY > 0.9f);
                    bool wasAtNorth = (fabsf(macroLastRX[bit]) < 0.1f && macroLastRY[bit] > 0.9f);
                    if (atNorth && !wasAtNorth) {
                        macroRotCount[bit]++;
                        printf("[MACRO][%llu] '%s' lap=%d\n",
                               GetTickCount64(), macroNames[bit].c_str(), macroRotCount[bit]);
                    }
                    macroLastRX[bit] = state.rightX;
                    macroLastRY[bit] = state.rightY;
                }

                if (wasActive && !macro.isActive())
                    printf("\n[MACRO][%llu] '%s' AUTO-OFF (laps: %d)\n",
                           GetTickCount64(), macroNames[bit].c_str(), macroRotCount[bit]);
            }

            output.update(state);
        }

        Sleep(8);
    }

    m_connected = false;
    m_phase.store(EnginePhase::Stopped);
    setStatus("Stopped");
    std::cout << "\n[PadEngine] thread stopped.\n";
}
