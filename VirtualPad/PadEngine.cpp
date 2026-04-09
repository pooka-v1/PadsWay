#include "PadEngine.h"
#include "Log.h"

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

// ---------------------------------------------------------------------------
// Keyboard / mouse helpers
// ---------------------------------------------------------------------------

static WORD keyNameToVK(const std::string& name) {
    if (name == "alt")        return VK_MENU;
    if (name == "ctrl")       return VK_CONTROL;
    if (name == "shift")      return VK_SHIFT;
    if (name == "win")        return VK_LWIN;
    if (name == "tab")        return VK_TAB;
    if (name == "enter")      return VK_RETURN;
    if (name == "esc" || name == "escape") return VK_ESCAPE;
    if (name == "space")      return VK_SPACE;
    if (name == "backspace")  return VK_BACK;
    if (name == "delete")     return VK_DELETE;
    if (name == "insert")     return VK_INSERT;
    if (name == "home_key")   return VK_HOME;
    if (name == "end")        return VK_END;
    if (name == "pageup")     return VK_PRIOR;
    if (name == "pagedown")   return VK_NEXT;
    if (name == "up")         return VK_UP;
    if (name == "down")       return VK_DOWN;
    if (name == "left")       return VK_LEFT;
    if (name == "right")      return VK_RIGHT;
    if (name == "f1")  return VK_F1;  if (name == "f2")  return VK_F2;
    if (name == "f3")  return VK_F3;  if (name == "f4")  return VK_F4;
    if (name == "f5")  return VK_F5;  if (name == "f6")  return VK_F6;
    if (name == "f7")  return VK_F7;  if (name == "f8")  return VK_F8;
    if (name == "f9")  return VK_F9;  if (name == "f10") return VK_F10;
    if (name == "f11") return VK_F11; if (name == "f12") return VK_F12;
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return static_cast<WORD>('A' + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return static_cast<WORD>(c);
        if (c >= '0' && c <= '9') return static_cast<WORD>(c);
    }
    return 0;
}

// press=true  → press all keys in order
// press=false → release all keys in reverse order
static void sendKeyCombo(const std::vector<std::string>& keys, bool press) {
    if (keys.empty()) return;
    std::vector<INPUT> inputs;
    inputs.reserve(keys.size());
    auto addKey = [&](const std::string& k, bool up) {
        WORD vk = keyNameToVK(k);
        if (vk == 0) return;
        INPUT inp = {};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wVk     = vk;
        inp.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        inputs.push_back(inp);
    };
    if (press) {
        for (const auto& k : keys)          addKey(k, false);
    } else {
        for (int i = (int)keys.size()-1; i >= 0; --i) addKey(keys[i], true);
    }
    if (!inputs.empty())
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

static void sendMouseButton(const std::string& btn, bool press) {
    INPUT inp = {};
    inp.type = INPUT_MOUSE;
    if      (btn == "left")   inp.mi.dwFlags = press ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;
    else if (btn == "right")  inp.mi.dwFlags = press ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;
    else if (btn == "middle") inp.mi.dwFlags = press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    else return;
    SendInput(1, &inp, sizeof(INPUT));
}

// ---------------------------------------------------------------------------

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
    m_thread        = std::thread(&PadEngine::threadFunc,  this);
    m_monitorThread = std::thread(&PadEngine::monitorFunc, this);
}

void PadEngine::stop() {
    m_running = false;
    if (m_thread.joinable())        m_thread.join();
    if (m_monitorThread.joinable()) m_monitorThread.join();
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

std::vector<DeviceCandidate> PadEngine::getAvailableDevices() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_availableDevices;
}

void PadEngine::requestSwitch(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= 0 && index < (int)m_availableDevices.size()) {
        m_switchTarget  = m_availableDevices[index];
        m_switchPending.store(true);
    }
}

DeviceCandidate PadEngine::getActiveDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeDevice;
}

GamepadState PadEngine::getLastState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastState;
}

GamepadState PadEngine::getLastVirtualState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastVirtualState;
}

std::vector<PadEvent> PadEngine::pollEvents() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PadEvent> out(m_eventQueue.begin(), m_eventQueue.end());
    m_eventQueue.clear();
    return out;
}

void PadEngine::pushEvent(PadEvent e) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_eventQueue.push_back(std::move(e));
    if (m_eventQueue.size() > 16)
        m_eventQueue.pop_front();
}

void PadEngine::setProfilePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_profilePath = path;
}

std::string PadEngine::getProfilePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profilePath;
}

std::string PadEngine::getActiveProfileName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeProfileName;
}

std::string PadEngine::getActiveLayoutId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeLayoutId;
}

void PadEngine::setMouseSpeed(float s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mouseSpeed = s;
}

float PadEngine::getMouseSpeed() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mouseSpeed;
}

void PadEngine::reloadConfigs() {
    try {
        auto configs = loadControllerConfigs("data/controllers.json");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_configs = std::move(configs);
        }
        m_configsDirty.store(true);
    } catch (const std::exception& ex) {
        spdlog::warn("reloadConfigs failed: {}", ex.what());
    }
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
// Monitor thread: scans devices every ~2 s and keeps m_availableDevices fresh.
// Runs in parallel with threadFunc. Uses the same scan helpers.
// ---------------------------------------------------------------------------

void PadEngine::monitorFunc() {
    while (m_running) {
        auto winmmEntries = scanPorts();
        auto hidEntries   = HIDScanner::scan();

        std::vector<ControllerConfig> configs;
        uint16_t vVid = 0, vPid = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            configs = m_configs;
            vVid    = m_virtualVid.load();
            vPid    = m_virtualPid.load();
        }

        std::vector<DeviceCandidate> candidates;

        for (auto& e : winmmEntries) {
            if (vVid && e.wMid == vVid && e.wPid == vPid) continue;  // skip virtual pad
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
            candidates.push_back(c);
        }

        for (auto& h : hidEntries) {
            if (vVid && h.vid == vVid && h.pid == vPid) continue;  // skip virtual pad
            const ControllerConfig* cfg = findConfig(configs, h.vid, h.pid, h.connectionType);
            bool inWinMM = false;
            for (auto& e : winmmEntries)
                if (e.wMid == h.vid && e.wPid == h.pid) { inWinMM = true; break; }
            if (!cfg && inWinMM) continue;
            if (cfg && cfg->mode != "hid" && inWinMM) continue;

            DeviceCandidate c;
            c.source         = DeviceCandidate::Source::HID;
            c.hidPath        = h.path;
            c.vid            = h.vid;
            c.pid            = h.pid;
            c.connectionType = h.connectionType;
            c.name           = h.productName.empty()
                ? ("HID " + std::to_string(h.vid) + ":" + std::to_string(h.pid))
                : h.productName;
            candidates.push_back(c);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_availableDevices = candidates;
        }

        // Sleep 2 s in short increments so stop() is responsive
        for (int i = 0; i < 20 && m_running; ++i)
            Sleep(100);
    }
}

// ---------------------------------------------------------------------------
// Background thread: mirrors the original VirtualPad.cpp main() logic.
// ---------------------------------------------------------------------------

void PadEngine::threadFunc() {
    m_phase.store(EnginePhase::Scanning);
    setStatus("Scanning for devices...");
    spdlog::info("=== VirtualPad — device init ===");

    m_hidHide.addSelfToWhitelist();

    // --- One-time init: configs (shared with monitor thread) ---
    std::vector<ControllerConfig> configs;
    try {
        configs = loadControllerConfigs("data/controllers.json");
        { std::lock_guard<std::mutex> lock(m_mutex); m_configs = configs; }
    } catch (const std::exception& ex) {
        spdlog::error("Error loading config: {}", ex.what());
        setStatus(std::string("Config error: ") + ex.what());
        m_running = false;
        return;
    }

    // --- One-time init: macro library ---
    std::unordered_map<std::string, std::string> macroLibrary;
    try {
        macroLibrary = loadMacroLibrary("data/macros.json");
        if (!macroLibrary.empty())
            printf("Macro library loaded: %zu macros.\n", macroLibrary.size());
    } catch (const std::exception& ex) {
        spdlog::warn("Could not load macro library: {}", ex.what());
    }

    // --- One-time init: ViGEm (persists through device switches) ---
    VirtualPadConfig vpCfg;
    try {
        vpCfg = loadVirtualPadConfig("data/virtualpad.json");
    } catch (const std::exception& ex) {
        spdlog::warn("Could not load virtualpad.json: {} — using defaults.", ex.what());
    }
    m_virtualVid.store(vpCfg.vid);
    m_virtualPid.store(vpCfg.pid);
    spdlog::debug("[PadEngine] Virtual pad identity: VID:{:04X} PID:{:04X}", vpCfg.vid, vpCfg.pid);

    setStatus("Connecting to ViGEm...");
    auto output = std::make_unique<ViGEmOutputAdapter>(vpCfg.vid, vpCfg.pid);
    if (!output->isReady()) {
        spdlog::error("Aborting: could not create virtual pad.");
        setStatus("ViGEm error — is the driver installed?");
        m_running = false;
        return;
    }

    // preSelected: set when a hot-switch is requested; skips the scan loop on next iteration.
    DeviceCandidate preSelected;

    // =========================================================================
    // Outer loop — re-entered on each device switch
    // =========================================================================
    while (m_running) {
        m_switchPending.store(false);

        // ── Device selection ─────────────────────────────────────────────────
        DeviceCandidate selected;

        if (preSelected.vid != 0) {
            // Hot-switch: bypass scan and go straight to configure
            selected    = preSelected;
            preSelected = {};
            spdlog::info("[Switch] Using pre-selected device: {} [VID={:04X} PID={:04X}]",
                selected.name, selected.vid, selected.pid);
        } else {
            // Normal startup scan loop
            m_phase.store(EnginePhase::Scanning);
            while (m_running && selected.vid == 0) {
                // Pick up any config updates written by the BindingWizard
                { std::lock_guard<std::mutex> lock(m_mutex); configs = m_configs; }
                auto winmmEntries = scanPorts();
                auto hidEntries   = HIDScanner::scan();

                std::vector<DeviceCandidate> allCandidates;

                for (auto& e : winmmEntries) {
                    if (vpCfg.vid && e.wMid == vpCfg.vid && e.wPid == vpCfg.pid) continue;
                    const ControllerConfig* c = findConfig(configs, e.wMid, e.wPid);
                    if (c && c->mode == "hid") continue;
                    DeviceCandidate dc;
                    dc.source  = DeviceCandidate::Source::WinMM;
                    dc.port    = e.id;
                    dc.vid     = e.wMid;
                    dc.pid     = e.wPid;
                    dc.axes    = e.axes;
                    dc.buttons = e.buttons;
                    char narrow[MAXPNAMELEN];
                    WideCharToMultiByte(CP_UTF8, 0, e.name, -1, narrow, sizeof(narrow), nullptr, nullptr);
                    dc.name = narrow;
                    allCandidates.push_back(dc);
                }

                for (auto& h : hidEntries) {
                    if (vpCfg.vid && h.vid == vpCfg.vid && h.pid == vpCfg.pid) continue;
                    const ControllerConfig* c = findConfig(configs, h.vid, h.pid, h.connectionType);
                    bool inWinMM = false;
                    for (auto& e : winmmEntries)
                        if (e.wMid == h.vid && e.wPid == h.pid) { inWinMM = true; break; }
                    if (!c && inWinMM) continue;
                    if (c && c->mode != "hid" && inWinMM) continue;
                    DeviceCandidate dc;
                    dc.source         = DeviceCandidate::Source::HID;
                    dc.hidPath        = h.path;
                    dc.vid            = h.vid;
                    dc.pid            = h.pid;
                    dc.connectionType = h.connectionType;
                    dc.name           = h.productName.empty()
                        ? ("HID " + std::to_string(h.vid) + ":" + std::to_string(h.pid))
                        : h.productName;
                    allCandidates.push_back(dc);
                }

                spdlog::debug("[Scan] WinMM: {} HID: {} Candidates: {}",
                    winmmEntries.size(), hidEntries.size(), allCandidates.size());
                for (auto& h : hidEntries)
                    spdlog::debug("[HID found] VID:{:04X} PID:{:04X} '{}' path={}",
                        h.vid, h.pid, h.productName, h.path);
                for (auto& dc : allCandidates)
                    spdlog::debug("[Candidate] [{}] VID:{:04X} PID:{:04X} '{}'",
                        dc.source == DeviceCandidate::Source::HID ? "HID" : "WinMM",
                        dc.vid, dc.pid, dc.name);

                if (allCandidates.empty()) {
                    setStatus("No device found — connect controller");
                    Sleep(500);
                    continue;
                }

                if (allCandidates.size() == 1) {
                    selected = allCandidates[0];
                    spdlog::info("Auto-selected [{}]: {} [VID={:04X} PID={:04X}]",
                        selected.source == DeviceCandidate::Source::HID ? "HID" : "WinMM",
                        selected.name, selected.vid, selected.pid);
                    setStatus(std::string("Auto-selected: ") + selected.name);
                } else {
                    { std::lock_guard<std::mutex> lock(m_mutex); m_candidates = allCandidates; }
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
                    spdlog::info("User selected [{}]: {} [VID={:04X} PID={:04X}]",
                        selected.source == DeviceCandidate::Source::HID ? "HID" : "WinMM",
                        selected.name, selected.vid, selected.pid);
                }
            }
        }

        if (!m_running) break;
        m_phase.store(EnginePhase::Configuring);

        // ── Configure ────────────────────────────────────────────────────────
        { std::lock_guard<std::mutex> lock(m_mutex); m_activeDevice = selected; }

        const ControllerConfig* cfgBase = findConfig(configs, selected.vid, selected.pid,
                                                     selected.connectionType);
        if (!cfgBase) {
            spdlog::error("No config for VID={:04X} PID={:04X} ({}) — add to controllers.json.",
                selected.vid, selected.pid, selected.name);
            setStatus("No config for this device — rescanning");
            preSelected = {};
            m_phase.store(EnginePhase::Scanning);
            Sleep(2000);
            continue;  // back to scan
        }
        spdlog::info("Config loaded: {}", cfgBase->source_name);
        setDevice(cfgBase->source_name);
        { std::lock_guard<std::mutex> lock(m_mutex); m_activeLayoutId = cfgBase->layout_id; }

        ControllerConfig effectiveCfg = *cfgBase;
        {
            std::string profilePath = getProfilePath();
            if (!profilePath.empty()) {
                try {
                    GameProfile profile = loadGameProfile(profilePath);
                    effectiveCfg = applyProfile(*cfgBase, profile);
                    { std::lock_guard<std::mutex> lock(m_mutex); m_activeProfileName = profile.profile_name; }
                    spdlog::info("Game profile '{}' applied.", profile.profile_name);
                } catch (const std::exception& ex) {
                    spdlog::warn("Could not load game profile: {}", ex.what());
                }
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_activeProfileName.clear();
            }
        }
        const ControllerConfig* cfg = &effectiveCfg;

        std::unique_ptr<IInputSource> input;
        if (selected.source == DeviceCandidate::Source::HID)
            input = std::make_unique<HIDInputSource>(selected.hidPath, *cfg);
        else
            input = std::make_unique<EightBitDoInputSource>(selected.port, *cfg);

        if (!input->isConnected()) {
            spdlog::error("Failed to open input device — rescanning.");
            setStatus("Failed to open input device — rescanning");
            preSelected = {};
            m_phase.store(EnginePhase::Scanning);
            Sleep(1000);
            continue;
        }

        // Release the previous device from HidHide, then hide the new one
        m_hidHide.unhideDevice();
        m_hidHide.hideDevice(selected.vid, selected.pid);

        // ── Macros (re-initialised per device / profile) ─────────────────────
        int lightningBotBit = 0;
        std::unordered_map<int, Macro>       macros;
        std::unordered_map<int, bool>        macroPrevBtn;
        std::unordered_map<int, std::string> macroNames;
        std::unordered_map<int, int>         macroRotCount;
        std::unordered_map<int, float>       macroLastRX;
        std::unordered_map<int, float>       macroLastRY;
        std::unordered_map<int, bool>        kbPrevBtn;
        std::unordered_map<int, bool>        mousePrevBtn;

        auto initMacros = [&]() {
            macros.clear();      macroPrevBtn.clear(); macroNames.clear();
            macroRotCount.clear(); macroLastRX.clear(); macroLastRY.clear();
            kbPrevBtn.clear();   mousePrevBtn.clear();
            for (const auto& [bit, action] : cfg->buttons) {
                if (action.type == ButtonActionType::Keyboard)   kbPrevBtn[bit]    = false;
                if (action.type == ButtonActionType::MouseClick) mousePrevBtn[bit] = false;
            }
            lightningBotBit = findBotBit(*cfg, "LightningBot");
            if (lightningBotBit > 0)
                spdlog::info("LightningBot assigned to button {}.", lightningBotBit);
            for (const auto& [bit, action] : cfg->buttons) {
                if (action.type != ButtonActionType::Macro) continue;
                std::string execution = action.execution;
                if (execution.empty()) {
                    auto it = macroLibrary.find(action.name);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' (button {}) not found in library.", action.name, bit);
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
                    spdlog::info("Macro '{}' assigned to button {}.", action.name, bit);
                } catch (const std::exception& ex) {
                    spdlog::error("Error parsing macro '{}': {}", action.name, ex.what());
                }
            }
        };
        initMacros();

        LightningBot bot;
        std::string currentProfilePath = getProfilePath();

        // ── Main run loop ─────────────────────────────────────────────────────
        setStatus("Running");
        m_connected = true;
        m_phase.store(EnginePhase::Running);
        spdlog::info("Forwarding input. Close the window to exit.");

    GamepadState state;
    bool         botBtnPrev    = false;
    bool         btnAPrev      = false;
    bool         mouseWasMoving = false;
    float        mouseAccumX   = 0.0f;
    float        mouseAccumY   = 0.0f;

    while (m_running && !m_switchPending.load()) {
        // Profile hot-swap: detect change and re-apply without reopening the device
        std::string newProfile = getProfilePath();
        if (newProfile != currentProfilePath) {
            currentProfilePath = newProfile;
            effectiveCfg = *cfgBase;
            if (!currentProfilePath.empty()) {
                try {
                    GameProfile profile = loadGameProfile(currentProfilePath);
                    effectiveCfg = applyProfile(*cfgBase, profile);
                    { std::lock_guard<std::mutex> lock(m_mutex); m_activeProfileName = profile.profile_name; }
                    spdlog::info("Game profile '{}' applied (hot-swap).", profile.profile_name);
                } catch (const std::exception& ex) {
                    spdlog::warn("Could not apply game profile: {}", ex.what());
                }
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_activeProfileName.clear();
            }
            input->setConfig(*cfg);   // cfg == &effectiveCfg, now updated
            if (bot.isActive()) bot.toggle();
            botBtnPrev  = false;
            btnAPrev    = false;
            mouseAccumX = 0.0f;
            mouseAccumY = 0.0f;
            initMacros();
        }

        // Button-mapping hot-reload: pick up controllers.json changes saved by the mapping editor.
        if (m_configsDirty.exchange(false)) {
            { std::lock_guard<std::mutex> lock(m_mutex); configs = m_configs; }
            // cfgBase pointed into the old configs — re-find it in the refreshed copy.
            cfgBase = findConfig(configs, selected.vid, selected.pid, selected.connectionType);
            if (cfgBase) {
                effectiveCfg = *cfgBase;
                if (!currentProfilePath.empty()) {
                    try {
                        GameProfile profile = loadGameProfile(currentProfilePath);
                        effectiveCfg = applyProfile(*cfgBase, profile);
                    } catch (...) {}
                }
                input->setConfig(*cfg);  // push updated button map to active input source
            }
        }


        if (!input->isConnected()) {
            if (m_connected) {
                spdlog::warn("[{}] disconnected. Waiting...", cfg->source_name);
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
            { std::lock_guard<std::mutex> lock(m_mutex); m_lastState = input->getPhysicalState(); }
            // Bot and macro toggle detection uses the button mask from the read just performed
            DWORD btns = input->getLastButtonMask();

            if (lightningBotBit > 0) {
                bool pressed = (btns & (1u << (lightningBotBit - 1))) != 0;
                if (pressed && !botBtnPrev) {
                    bot.toggle();
                    spdlog::info("[BOT] Lightning bot {}", bot.isActive() ? "ON" : "OFF");
                    pushEvent({ PadEventType::BotToggle, "LightningBot", bot.isActive() });
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
                    spdlog::info("[MACRO][{}] '{}' {}", GetTickCount64(), macroNames[bit],
                           macro.isActive() ? "ON" : "OFF");
                    pushEvent({ PadEventType::MacroToggle, macroNames[bit], macro.isActive() });
                }
                if (!pressed && prev)
                    if (macro.getMode() == MacroRepeatMode::UntilRelease)
                        macro.stop();
                prev = pressed;
            }

            if (state.btnA && !btnAPrev)
                spdlog::info("[MAN][{}] Manual A press", GetTickCount64()); // TODO: remove when no longer needed
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
                        spdlog::debug("[MACRO][{}] '{}' lap={}", GetTickCount64(), macroNames[bit], macroRotCount[bit]);
                    }
                    macroLastRX[bit] = state.rightX;
                    macroLastRY[bit] = state.rightY;
                }

                if (wasActive && !macro.isActive()) {
                    spdlog::info("[MACRO][{}] '{}' AUTO-OFF (laps: {})", GetTickCount64(), macroNames[bit], macroRotCount[bit]);
                    pushEvent({ PadEventType::MacroToggle, macroNames[bit], false });
                }
            }

            // --- Keyboard actions (edge-triggered) ---
            for (auto& [bit, prev] : kbPrevBtn) {
                bool pressed = (btns & (1u << (bit - 1))) != 0;
                const auto& action = cfg->buttons.at(bit);
                if (pressed && !prev) {
                    sendKeyCombo(action.keys, true);
                    spdlog::debug("[KB] button {} down", bit);
                    std::string combo;
                    for (const auto& k : action.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                    pushEvent({ PadEventType::KeyboardAction, combo, true });
                }
                if (!pressed && prev) { sendKeyCombo(action.keys, false); spdlog::debug("[KB] button {} up", bit); }
                prev = pressed;
            }

            // --- Mouse click actions (edge-triggered) ---
            for (auto& [bit, prev] : mousePrevBtn) {
                bool pressed = (btns & (1u << (bit - 1))) != 0;
                if (pressed != prev) {
                    const std::string& btn = cfg->buttons.at(bit).mouseButton;
                    sendMouseButton(btn, pressed);
                    spdlog::debug("[MOUSE] button {} {}", bit, pressed ? "down" : "up");
                    if (pressed)
                        pushEvent({ PadEventType::MouseAction, btn + " click", true });
                }
                prev = pressed;
            }

            // --- Mouse movement (continuous, sub-pixel accumulator) ---
            constexpr float kMouseDeadZone = 0.12f;
            float mx = (fabsf(state.mouseX) > kMouseDeadZone) ? state.mouseX : 0.0f;
            float my = (fabsf(state.mouseY) > kMouseDeadZone) ? state.mouseY : 0.0f;
            bool mouseIsMoving = (mx != 0.0f || my != 0.0f);
            if (mouseIsMoving && !mouseWasMoving)
                pushEvent({ PadEventType::MouseAction, "move", true });
            mouseWasMoving = mouseIsMoving;
            if (mouseIsMoving) {
                float speed = getMouseSpeed();
                mouseAccumX += mx * speed;
                mouseAccumY += my * speed;
                LONG dx = static_cast<LONG>(mouseAccumX);
                LONG dy = static_cast<LONG>(mouseAccumY);
                if (dx != 0 || dy != 0) {
                    mouseAccumX -= static_cast<float>(dx);
                    mouseAccumY -= static_cast<float>(dy);
                    INPUT inp = {};
                    inp.type       = INPUT_MOUSE;
                    inp.mi.dwFlags = MOUSEEVENTF_MOVE;
                    inp.mi.dx      = dx;
                    inp.mi.dy      = dy;
                    SendInput(1, &inp, sizeof(INPUT));
                }
            }

            // --- Touchpad delta mouse (no dead zone — real finger movement, not velocity) ---
            if (cfg->touchpad.mouseEnabled &&
                (state.touchDeltaX != 0.0f || state.touchDeltaY != 0.0f)) {
                constexpr float kTouchpadScale = 1.5f;
                mouseAccumX += state.touchDeltaX * kTouchpadScale;
                mouseAccumY += state.touchDeltaY * kTouchpadScale;
                LONG dx = static_cast<LONG>(mouseAccumX);
                LONG dy = static_cast<LONG>(mouseAccumY);
                if (dx != 0 || dy != 0) {
                    mouseAccumX -= static_cast<float>(dx);
                    mouseAccumY -= static_cast<float>(dy);
                    INPUT inp = {};
                    inp.type       = INPUT_MOUSE;
                    inp.mi.dwFlags = MOUSEEVENTF_MOVE;
                    inp.mi.dx      = dx;
                    inp.mi.dy      = dy;
                    SendInput(1, &inp, sizeof(INPUT));
                }
            }

            { std::lock_guard<std::mutex> lock(m_mutex); m_lastVirtualState = state; }
            output->update(state);
        }

        Sleep(8);
    }

        // ── End of run loop ───────────────────────────────────────────────────
        m_connected = false;

        if (!m_running) break;  // normal stop — exit outer loop

        // Switch was requested: read the target and pre-select it for next iteration
        DeviceCandidate target;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            target = m_switchTarget;
        }
        m_switchPending.store(false);

        if (target.vid == 0) {
            spdlog::warn("[Switch] Target device no longer valid — rescanning.");
        } else {
            spdlog::info("[Switch] Switching to: {} [VID={:04X} PID={:04X}]",
                target.name, target.vid, target.pid);
            preSelected = target;
        }
        // Loop back to outer while — preSelected drives the next configure phase
    }
    // =========================================================================
    // End outer loop
    // =========================================================================

    m_hidHide.unhideDevice();
    m_connected = false;
    { std::lock_guard<std::mutex> lock(m_mutex); m_activeDevice = {}; }
    m_phase.store(EnginePhase::Stopped);
    setStatus("Stopped");
    spdlog::info("[PadEngine] thread stopped.");
}
