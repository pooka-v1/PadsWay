#include "PadEngine.h"
#include "Log.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

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

// Set a virtual button in GamepadState by short name (a/b/x/y/l1/r1/… and dpad up/down/left/right).
static void applyVirtualBtnByName(GamepadState& state, const std::string& name, bool pressed) {
    if (!pressed) return;
    if      (name == "a")      state.btnA     = true;
    else if (name == "b")      state.btnB     = true;
    else if (name == "x")      state.btnX     = true;
    else if (name == "y")      state.btnY     = true;
    else if (name == "l1")     state.btnLB    = true;
    else if (name == "r1")     state.btnRB    = true;
    else if (name == "select") state.btnBack  = true;
    else if (name == "start")  state.btnStart = true;
    else if (name == "home")   state.btnHome  = true;
    else if (name == "l3")     state.btnL3    = true;
    else if (name == "r3")     state.btnR3    = true;
    else if (name == "l4")     state.btnL4    = true;
    else if (name == "r4")     state.btnR4    = true;
    else if (name == "lp")     state.btnLP    = true;
    else if (name == "rp")     state.btnRP    = true;
    else if (name == "up"    || name == "dpad_up")    state.dpadUp   = true;
    else if (name == "down"  || name == "dpad_down")  state.dpadDown = true;
    else if (name == "left"  || name == "dpad_left")  state.dpadLeft = true;
    else if (name == "right" || name == "dpad_right") state.dpadRight = true;
    else if (name == "left_y_pos")  state.leftY  =  1.0f;
    else if (name == "left_y_neg")  state.leftY  = -1.0f;
    else if (name == "left_x_pos")  state.leftX  =  1.0f;
    else if (name == "left_x_neg")  state.leftX  = -1.0f;
    else if (name == "right_y_pos") state.rightY =  1.0f;
    else if (name == "right_y_neg") state.rightY = -1.0f;
    else if (name == "right_x_pos") state.rightX =  1.0f;
    else if (name == "right_x_neg") state.rightX = -1.0f;
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
    else if (btn == "x1") { inp.mi.dwFlags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; inp.mi.mouseData = XBUTTON1; }
    else if (btn == "x2") { inp.mi.dwFlags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; inp.mi.mouseData = XBUTTON2; }
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
        auto hidEntries = HIDScanner::scan();

        std::vector<ControllerConfig> configs;
        uint16_t vVid = 0, vPid = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            configs = m_configs;
            vVid    = m_virtualVid.load();
            vPid    = m_virtualPid.load();
        }

        std::vector<DeviceCandidate> candidates;
        for (auto& h : hidEntries) {
            if (vVid && h.vid == vVid && h.pid == vPid) continue;
            const ControllerConfig* cfg = findConfig(configs, h.vid, h.pid, h.connectionType);
            if (!cfg || cfg->mode != "hid") continue;
            DeviceCandidate c;
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
    std::vector<ControllerConfig>    configs;
    std::vector<PhysicalController>  physCtrls;
    try {
        configs   = loadControllerConfigs("data/controllers.json");
        physCtrls = loadPhysicalControllers("data/controllers.json");
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
                auto hidEntries = HIDScanner::scan();

                std::vector<DeviceCandidate> allCandidates;
                for (auto& h : hidEntries) {
                    if (vpCfg.vid && h.vid == vpCfg.vid && h.pid == vpCfg.pid) continue;
                    const ControllerConfig* c = findConfig(configs, h.vid, h.pid, h.connectionType);
                    if (!c || c->mode != "hid") continue;
                    DeviceCandidate dc;
                    dc.hidPath        = h.path;
                    dc.vid            = h.vid;
                    dc.pid            = h.pid;
                    dc.connectionType = h.connectionType;
                    dc.name           = h.productName.empty()
                        ? ("HID " + std::to_string(h.vid) + ":" + std::to_string(h.pid))
                        : h.productName;
                    allCandidates.push_back(dc);
                }

                spdlog::trace("[Scan] HID: {} Candidates: {}", hidEntries.size(), allCandidates.size());
                for (auto& dc : allCandidates)
                    spdlog::trace("[Candidate] VID:{:04X} PID:{:04X} '{}'", dc.vid, dc.pid, dc.name);

                if (allCandidates.empty()) {
                    setStatus("No device found — connect controller");
                    Sleep(500);
                    continue;
                }

                if (allCandidates.size() == 1) {
                    selected = allCandidates[0];
                    spdlog::info("Auto-selected: {} [VID={:04X} PID={:04X}]",
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
                    spdlog::info("User selected: {} [VID={:04X} PID={:04X}]",
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

        auto input = std::make_unique<HIDInputSource>(selected.hidPath, *cfg);

        // Inject PhysicalController for component-system processing (P4).
        // Rebuild button layer from effectiveCfg so profile overrides are reflected.
        {
            auto it = std::find_if(physCtrls.begin(), physCtrls.end(),
                [&](const PhysicalController& pc) {
                    return pc.vid == selected.vid && pc.pid == selected.pid;
                });
            if (it != physCtrls.end()) {
                PhysicalController pc = *it;
                rebuildPhysicalControllerButtons(pc, effectiveCfg);
                input->setPhysicalController(pc);
                spdlog::info("PhysicalController injected for {:04X}:{:04X}", selected.vid, selected.pid);
            }
        }

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

        // Axis-action equivalents (keyed by "source_pos"/"source_neg")
        std::unordered_map<std::string, Macro>       axisMacros;
        std::unordered_map<std::string, bool>        axisMacroPrev;
        std::unordered_map<std::string, std::string> axisMacroNames;
        std::unordered_map<std::string, bool>        axisKbPrev;
        std::unordered_map<std::string, bool>        axisMousePrev;
        // Axis Ranges: prev active ButtonAction per key (nullopt = nothing was active)
        std::unordered_map<std::string, std::optional<ButtonAction>> axisRangePrev;
        // Axis Range macros: composite key = "axis_key|macro_name"
        std::unordered_map<std::string, Macro> axisRangeMacros;
        std::unordered_map<std::string, bool>  axisRangeMacroOk;

        // Dpad H5 actions (keyed by "up"/"down"/"left"/"right")
        std::unordered_map<std::string, Macro>       dpadMacros;
        std::unordered_map<std::string, bool>        dpadMacroPrev;
        std::unordered_map<std::string, std::string> dpadMacroNames;
        std::unordered_map<std::string, bool>        dpadKbPrev;
        std::unordered_map<std::string, bool>        dpadMousePrev;

        // Trigger-as-source state
        float trigLPrev = 0.0f;           // previous frame physical trigger L value
        float trigRPrev = 0.0f;           // previous frame physical trigger R value
        Macro trigLMacro;                 // macro for simple triggerLAction
        Macro trigRMacro;                 // macro for simple triggerRAction
        bool  trigLMacroOk  = false;      // true = trigLMacro parsed and valid
        bool  trigRMacroOk  = false;
        bool  trigLKbPrev   = false;      // previous active state for keyboard trigger L
        bool  trigRKbPrev   = false;
        bool  trigLMousPrev = false;      // previous active state for mouse trigger L
        bool  trigRMousPrev = false;
        // Ranged trigger state (indexed by range position in triggerLRanges/triggerRRanges)
        // uint8_t instead of bool to avoid std::vector<bool> proxy reference issues
        std::vector<uint8_t> trigLRangePrev;
        std::vector<uint8_t> trigRRangePrev;
        std::vector<Macro>   trigLRangeMacros;
        std::vector<Macro>   trigRRangeMacros;
        std::vector<uint8_t> trigLRangeMacroOk;
        std::vector<uint8_t> trigRRangeMacroOk;

        auto initMacros = [&]() {
            macros.clear();      macroPrevBtn.clear(); macroNames.clear();
            macroRotCount.clear(); macroLastRX.clear(); macroLastRY.clear();
            kbPrevBtn.clear();   mousePrevBtn.clear();
            axisMacros.clear();  axisMacroPrev.clear(); axisMacroNames.clear();
            axisKbPrev.clear();  axisMousePrev.clear();
            dpadMacros.clear();  dpadMacroPrev.clear(); dpadMacroNames.clear();
            dpadKbPrev.clear();  dpadMousePrev.clear();
            for (const auto& [bit, action] : cfg->buttons) {
                if (action.type == ButtonActionType::Keyboard)   kbPrevBtn[bit]    = false;
                if (action.type == ButtonActionType::MouseClick) mousePrevBtn[bit] = false;
            }
            for (const auto& [dir, action] : cfg->dpadActions) {
                if (action.type == ButtonActionType::Keyboard)   dpadKbPrev[dir]    = false;
                if (action.type == ButtonActionType::MouseClick) dpadMousePrev[dir] = false;
            }
            axisRangePrev.clear();
            axisRangeMacros.clear();
            axisRangeMacroOk.clear();
            for (const auto& [key, action] : cfg->axis_actions) {
                if (action.type == HalfAxisActionType::Keyboard)   axisKbPrev[key]    = false;
                if (action.type == HalfAxisActionType::MouseClick) axisMousePrev[key] = false;
                if (action.type == HalfAxisActionType::Ranges) {
                    axisRangePrev[key] = std::nullopt;
                    for (const auto& r : action.ranges) {
                        if (!r.hasAction || r.action.type != ButtonActionType::Macro) continue;
                        std::string mkey = key + "|" + r.action.name;
                        auto it = macroLibrary.find(r.action.name);
                        if (it == macroLibrary.end()) {
                            spdlog::warn("Macro '{}' (axis range {}) not found.", r.action.name, key);
                            axisRangeMacroOk[mkey] = false;
                            continue;
                        }
                        try {
                            Macro m;
                            MacroParser::parse(it->second, m);
                            axisRangeMacros[mkey]  = std::move(m);
                            axisRangeMacroOk[mkey] = true;
                        } catch (...) {
                            spdlog::warn("Failed to parse macro '{}' (axis range {}).", r.action.name, key);
                            axisRangeMacroOk[mkey] = false;
                        }
                    }
                }
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
            // Dpad H5 macros
            for (const auto& [dir, action] : cfg->dpadActions) {
                if (action.type != ButtonActionType::Macro) continue;
                std::string execution = action.execution;
                if (execution.empty()) {
                    auto it = macroLibrary.find(action.name);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' (dpad {}) not found in library.", action.name, dir);
                        continue;
                    }
                    execution = it->second;
                }
                try {
                    Macro m;
                    MacroParser::parse(execution, m);
                    dpadMacros[dir]     = std::move(m);
                    dpadMacroPrev[dir]  = false;
                    dpadMacroNames[dir] = action.name;
                    spdlog::info("Macro '{}' assigned to dpad {}.", action.name, dir);
                } catch (const std::exception& ex) {
                    spdlog::error("Error parsing macro '{}': {}", action.name, ex.what());
                }
            }

            // Axis-direction macros
            for (const auto& [key, action] : cfg->axis_actions) {
                if (action.type != HalfAxisActionType::Macro) continue;
                std::string execution = action.execution;
                if (execution.empty()) {
                    auto it = macroLibrary.find(action.target);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' (axis {}) not found in library.", action.target, key);
                        continue;
                    }
                    execution = it->second;
                }
                try {
                    Macro m;
                    MacroParser::parse(execution, m);
                    axisMacros[key]     = std::move(m);
                    axisMacroPrev[key]  = false;
                    axisMacroNames[key] = action.target;
                    spdlog::info("Macro '{}' assigned to axis direction {}.", action.target, key);
                } catch (const std::exception& ex) {
                    spdlog::error("Error parsing macro '{}': {}", action.target, ex.what());
                }
            }

            // Trigger-as-source state reset
            trigLPrev = trigRPrev = 0.0f;
            trigLKbPrev = trigRKbPrev = trigLMousPrev = trigRMousPrev = false;
            trigLMacroOk = trigRMacroOk = false;
            // Simple trigger macros
            auto initTrigMacro = [&](const ButtonAction& act, Macro& mac, bool& ok) {
                ok = false;
                if (act.type != ButtonActionType::Macro) return;
                std::string execution = act.execution;
                if (execution.empty()) {
                    auto it = macroLibrary.find(act.name);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' (trigger) not found in library.", act.name);
                        return;
                    }
                    execution = it->second;
                }
                try {
                    MacroParser::parse(execution, mac);
                    ok = true;
                    spdlog::info("Macro '{}' assigned to trigger.", act.name);
                } catch (const std::exception& ex) {
                    spdlog::error("Error parsing trigger macro '{}': {}", act.name, ex.what());
                }
            };
            if (cfg->triggerLHasAction) initTrigMacro(cfg->triggerLAction, trigLMacro, trigLMacroOk);
            if (cfg->triggerRHasAction) initTrigMacro(cfg->triggerRAction, trigRMacro, trigRMacroOk);
            // Ranged trigger macros
            auto initRangeMacros = [&](const std::vector<TriggerRange>& ranges,
                                       std::vector<Macro>& macs, std::vector<uint8_t>& ok,
                                       std::vector<uint8_t>& prev) {
                macs.clear(); ok.clear(); prev.clear();
                for (const auto& r : ranges) {
                    prev.push_back(0);
                    if (r.action.type == ButtonActionType::Macro) {
                        auto it = macroLibrary.find(r.action.name);
                        if (it != macroLibrary.end()) {
                            Macro m;
                            try {
                                MacroParser::parse(it->second, m);
                                macs.push_back(std::move(m));
                                ok.push_back(1);
                                continue;
                            } catch (...) {}
                        }
                    }
                    macs.push_back({});
                    ok.push_back(0);
                }
            };
            initRangeMacros(cfg->triggerLRanges, trigLRangeMacros, trigLRangeMacroOk, trigLRangePrev);
            initRangeMacros(cfg->triggerRRanges, trigRRangeMacros, trigRRangeMacroOk, trigRRangePrev);
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
    bool         mouseWasMoving    = false;
    float        mouseAccumX       = 0.0f;
    float        mouseAccumY       = 0.0f;
    bool         lostDevice        = false;  // set when device disconnects unexpectedly
    int          reconnectTries    = 0;

    while (m_running && !m_switchPending.load()) {
        // Profile hot-swap: detect change or explicit reload request.
        std::string newProfile = getProfilePath();
        if (newProfile != currentProfilePath || m_profileDirty.exchange(false)) {
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
            // Rebuild PhysicalController button layer to reflect profile's type overrides.
            {
                auto it = std::find_if(physCtrls.begin(), physCtrls.end(),
                    [&](const PhysicalController& pc) {
                        return pc.vid == selected.vid && pc.pid == selected.pid;
                    });
                if (it != physCtrls.end()) {
                    PhysicalController pc = *it;
                    rebuildPhysicalControllerButtons(pc, effectiveCfg);
                    input->setPhysicalController(pc);
                }
            }
            if (bot.isActive()) bot.toggle();
            botBtnPrev  = false;
            mouseAccumX = 0.0f;
            mouseAccumY = 0.0f;
            initMacros();
        }

        // Macro library hot-reload: pick up macros.json changes saved by the macro manager.
        if (m_macroLibDirty.exchange(false)) {
            try {
                macroLibrary = loadMacroLibrary("data/macros.json");
                spdlog::info("Macro library reloaded: {} macros.", macroLibrary.size());
            } catch (const std::exception& ex) {
                spdlog::warn("Macro library reload failed: {}", ex.what());
            }
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
                initMacros();            // re-init KB/mouse edge state + re-parse macros

                // Re-inject PhysicalController so the component system picks up mapping changes.
                try {
                    physCtrls = loadPhysicalControllers("data/controllers.json");
                    auto it = std::find_if(physCtrls.begin(), physCtrls.end(),
                        [&](const PhysicalController& pc) {
                            return pc.vid == selected.vid && pc.pid == selected.pid;
                        });
                    if (it != physCtrls.end())
                        input->setPhysicalController(*it);
                } catch (const std::exception& ex) {
                    spdlog::warn("PhysicalController hot-reload failed: {}", ex.what());
                }
            }
        }


        if (!input->isConnected()) {
            if (m_connected) {
                spdlog::warn("[{}] disconnected. Reconnecting...", cfg->source_name);
                m_connected = false;
                { std::lock_guard<std::mutex> lock(m_mutex); m_activeLayoutId.clear(); }
                setStatus("Device disconnected — reconnecting...");
            }
            if (++reconnectTries <= 3) {
                Sleep(500);
                continue;
            }
            lostDevice = true;
            reconnectTries = 0;
            break;
        }
        reconnectTries = 0;

        if (!m_connected) {
            m_connected = true;
            setStatus("Running");
        }

        if (input->read(state)) {
            { std::lock_guard<std::mutex> lock(m_mutex); m_lastState = input->getPhysicalState(); }
            const bool editorOpen = m_editorOpen.load();
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

                if (!editorOpen) {
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
                }
                prev = pressed;
            }

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

            // --- Axis-direction Macro / Keyboard / Mouse (edge-triggered) ---
            {
                auto activeAA = input->getActiveAxisActions();
                std::unordered_set<std::string> activeAASet(activeAA.begin(), activeAA.end());

                for (auto& [key, macro] : axisMacros) {
                    bool active = activeAASet.count(key) > 0;
                    bool& prev  = axisMacroPrev[key];
                    if (active && !prev) {
                        if (macro.getMode() == MacroRepeatMode::UntilRelease)
                            macro.start();
                        else
                            macro.toggle();
                        if (macro.isActive())
                            spdlog::info("[MACRO][AXIS] '{}' ON", axisMacroNames[key]);
                        pushEvent({ PadEventType::MacroToggle, axisMacroNames[key], macro.isActive() });
                    }
                    if (!active && prev)
                        if (macro.getMode() == MacroRepeatMode::UntilRelease)
                            macro.stop();
                    prev = active;
                    macro.tick(state);
                }

                for (auto& [key, prev] : axisKbPrev) {
                    bool active = activeAASet.count(key) > 0;
                    const HalfAxisAction& action = cfg->axis_actions.at(key);
                    if (active && !prev) {
                        sendKeyCombo(action.keys, true);
                        std::string combo;
                        for (const auto& k : action.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                        pushEvent({ PadEventType::KeyboardAction, combo, true });
                    }
                    if (!active && prev) sendKeyCombo(action.keys, false);
                    prev = active;
                }

                for (auto& [key, prev] : axisMousePrev) {
                    bool active = activeAASet.count(key) > 0;
                    if (active != prev) {
                        const std::string& btn = cfg->axis_actions.at(key).mouseButton;
                        sendMouseButton(btn, active);
                        if (active)
                            pushEvent({ PadEventType::MouseAction, btn + " click", true });
                    }
                    prev = active;
                }

                // Axis Ranges: Keyboard / MouseClick / Macro edge-triggered per range action
                {
                    const auto& rangeActions = input->getActiveAxisRangeActions();
                    for (auto& [key, prev] : axisRangePrev) {
                        auto it = rangeActions.find(key);
                        bool isActive = (it != rangeActions.end());
                        bool changed  = isActive
                            ? (!prev.has_value() ||
                               prev->type        != it->second.type        ||
                               prev->name        != it->second.name        ||
                               prev->mouseButton != it->second.mouseButton ||
                               prev->keys        != it->second.keys)
                            : prev.has_value();

                        if (!changed) continue;

                        // Release previous action
                        if (prev.has_value()) {
                            if (prev->type == ButtonActionType::Keyboard)
                                sendKeyCombo(prev->keys, false);
                            else if (prev->type == ButtonActionType::MouseClick)
                                sendMouseButton(prev->mouseButton, false);
                            else if (prev->type == ButtonActionType::Macro) {
                                auto mit = axisRangeMacros.find(key + "|" + prev->name);
                                if (mit != axisRangeMacros.end() && axisRangeMacroOk[key + "|" + prev->name])
                                    if (mit->second.getMode() == MacroRepeatMode::UntilRelease)
                                        mit->second.stop();
                            }
                        }
                        // Activate new action
                        if (isActive) {
                            const ButtonAction& cur = it->second;
                            if (cur.type == ButtonActionType::Keyboard) {
                                sendKeyCombo(cur.keys, true);
                                std::string combo;
                                for (const auto& k : cur.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                                pushEvent({ PadEventType::KeyboardAction, combo, true });
                            } else if (cur.type == ButtonActionType::MouseClick) {
                                sendMouseButton(cur.mouseButton, true);
                                pushEvent({ PadEventType::MouseAction, cur.mouseButton + " click", true });
                            } else if (cur.type == ButtonActionType::Macro) {
                                std::string mkey = key + "|" + cur.name;
                                auto mit = axisRangeMacros.find(mkey);
                                if (mit != axisRangeMacros.end() && axisRangeMacroOk[mkey]) {
                                    if (mit->second.getMode() == MacroRepeatMode::UntilRelease)
                                        mit->second.start();
                                    else
                                        mit->second.toggle();
                                    pushEvent({ PadEventType::MacroToggle, cur.name, mit->second.isActive() });
                                }
                            }
                            prev = cur;
                        } else {
                            prev = std::nullopt;
                        }
                    }
                    // Tick active axis range macros every frame
                    for (auto& [mkey, macro] : axisRangeMacros)
                        macro.tick(state);
                }
            }

            // --- Dpad H5 actions (Macro / Keyboard / Mouse, edge-triggered) ---
            // Helper: get dpad active state by direction string.
            // Reads PHYSICAL state so axis_action virtual dpad outputs don't trigger
            // dpadActions (which are meant for physical dpad remapping only).
            auto dpadActive = [&](const std::string& dir) -> bool {
                const GamepadState& phys = input->getPhysicalState();
                if (dir == "up")    return phys.dpadUp;
                if (dir == "down")  return phys.dpadDown;
                if (dir == "left")  return phys.dpadLeft;
                if (dir == "right") return phys.dpadRight;
                return false;
            };
            for (auto& [dir, macro] : dpadMacros) {
                bool active = dpadActive(dir);
                bool& prev  = dpadMacroPrev[dir];
                if (active && !prev) {
                    if (macro.getMode() == MacroRepeatMode::UntilRelease) macro.start();
                    else macro.toggle();
                    if (macro.isActive())
                        spdlog::info("[MACRO][DPAD] '{}' ON", dpadMacroNames[dir]);
                    pushEvent({ PadEventType::MacroToggle, dpadMacroNames[dir], macro.isActive() });
                }
                if (!active && prev)
                    if (macro.getMode() == MacroRepeatMode::UntilRelease) macro.stop();
                prev = active;
                macro.tick(state);
                if (active) {
                    if (dir == "up")    state.dpadUp    = false;
                    if (dir == "down")  state.dpadDown  = false;
                    if (dir == "left")  state.dpadLeft  = false;
                    if (dir == "right") state.dpadRight = false;
                }
            }
            for (auto& [dir, prev] : dpadKbPrev) {
                bool active = dpadActive(dir);
                const ButtonAction& action = cfg->dpadActions.at(dir);
                if (active && !prev) {
                    sendKeyCombo(action.keys, true);
                    std::string combo;
                    for (const auto& k : action.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                    pushEvent({ PadEventType::KeyboardAction, combo, true });
                }
                if (!active && prev) sendKeyCombo(action.keys, false);
                prev = active;
                if (active) {
                    if (dir == "up")    state.dpadUp    = false;
                    if (dir == "down")  state.dpadDown  = false;
                    if (dir == "left")  state.dpadLeft  = false;
                    if (dir == "right") state.dpadRight = false;
                }
            }
            for (auto& [dir, prev] : dpadMousePrev) {
                bool active = dpadActive(dir);
                if (active != prev) {
                    const std::string& btn = cfg->dpadActions.at(dir).mouseButton;
                    sendMouseButton(btn, active);
                    if (active)
                        pushEvent({ PadEventType::MouseAction, btn + " click", true });
                }
                prev = active;
                if (active) {
                    if (dir == "up")    state.dpadUp    = false;
                    if (dir == "down")  state.dpadDown  = false;
                    if (dir == "left")  state.dpadLeft  = false;
                    if (dir == "right") state.dpadRight = false;
                }
            }
            // Dpad direction → virtual trigger (L2/R2)
            for (const auto& [dir, action] : cfg->dpadActions) {
                if (action.type != ButtonActionType::Trigger) continue;
                bool active = dpadActive(dir);
                if (active) {
                    if      (action.target == "l2") state.triggerL = 1.0f;
                    else if (action.target == "r2") state.triggerR = 1.0f;
                    if (dir == "up")    state.dpadUp    = false;
                    if (dir == "down")  state.dpadDown  = false;
                    if (dir == "left")  state.dpadLeft  = false;
                    if (dir == "right") state.dpadRight = false;
                }
            }

            // --- Trigger-as-source actions ---
            constexpr float kTrigActThresh = 0.1f;  // activation threshold for digital targets
            // Helper: apply a single ButtonAction driven by a float trigger value.
            // physVal is the raw physical trigger value [0..1].
            // prevActive is the per-action edge-detect flag (modified in place).
            // After the call, the source trigger value in state has been routed/cleared.
            auto applyTrigAct = [&](float physVal, const ButtonAction& act,
                                     bool& kbPrev, bool& mousPrev, Macro& mac, bool macOk,
                                     float& srcTrig) {
                bool active = (physVal > kTrigActThresh);
                switch (act.type) {
                case ButtonActionType::TriggerPassthrough: {
                    // Cross-passthrough only: only consume source when routing to the OTHER trigger.
                    // Same-trigger (R2→R2 or L2→L2) = identity, leave srcTrig untouched.
                    bool srcIsR2 = (&srcTrig == &state.triggerR);
                    if (act.target == "r2" && !srcIsR2) {
                        state.triggerR = (physVal > state.triggerR ? physVal : state.triggerR);
                        srcTrig = 0.0f;  // consume L2
                    } else if (act.target == "l2" && srcIsR2) {
                        state.triggerL = (physVal > state.triggerL ? physVal : state.triggerL);
                        srcTrig = 0.0f;  // consume R2
                    }
                    // same-trigger: no-op, value passes through unchanged
                    break;
                }
                case ButtonActionType::VirtualButton:
                    applyVirtualBtnByName(state, act.name, active);
                    srcTrig = 0.0f;
                    break;
                case ButtonActionType::Keyboard:
                    if (active != kbPrev) {
                        sendKeyCombo(act.keys, active);
                        if (active) {
                            std::string combo;
                            for (const auto& k : act.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                            pushEvent({ PadEventType::KeyboardAction, combo, true });
                        }
                        kbPrev = active;
                    }
                    srcTrig = 0.0f;
                    break;
                case ButtonActionType::MouseClick:
                    if (active != mousPrev) {
                        sendMouseButton(act.mouseButton, active);
                        if (active) pushEvent({ PadEventType::MouseAction, act.mouseButton + " click", true });
                        mousPrev = active;
                    }
                    srcTrig = 0.0f;
                    break;
                case ButtonActionType::Macro:
                    if (active && !kbPrev) {  // reuse kbPrev as macroPrev for trigger
                        if (macOk) {
                            if (mac.getMode() == MacroRepeatMode::UntilRelease) mac.start();
                            else mac.toggle();
                            if (mac.isActive()) pushEvent({ PadEventType::MacroToggle, act.name, true });
                        }
                        kbPrev = true;
                    } else if (!active && kbPrev) {
                        if (macOk && mac.getMode() == MacroRepeatMode::UntilRelease) mac.stop();
                        kbPrev = false;
                    }
                    if (macOk && mac.isActive()) mac.tick(state);
                    srcTrig = 0.0f;
                    break;
                default: break;
                }
            };

            // Simple trigger actions.
            // Track cross-passthrough: if L2 was routed to R2 (or R2 to L2), skip the
            // destination's own trigger_actions so the analog value reaches ViGEm unmodified.
            bool trigLWasCrossTarget = false;
            bool trigRWasCrossTarget = false;

            if (cfg->triggerLHasAction && cfg->triggerLRanges.empty()) {
                const auto& lAct = cfg->triggerLAction;
                // Marker targets (Macro/KB/Mouse) are not written by the Component System,
                // so read the physical value directly — same pattern as dpadActive().
                bool lNeedsPhys = lAct.type == ButtonActionType::Macro    ||
                                  lAct.type == ButtonActionType::Keyboard  ||
                                  lAct.type == ButtonActionType::MouseClick;
                float physL = lNeedsPhys ? input->getPhysicalState().triggerL : state.triggerL;
                applyTrigAct(physL, lAct,
                             trigLKbPrev, trigLMousPrev, trigLMacro, trigLMacroOk,
                             state.triggerL);
                if (lAct.type == ButtonActionType::TriggerPassthrough &&
                    lAct.target == "r2" && physL > 0.0f)
                    trigRWasCrossTarget = true;
            }
            if (cfg->triggerRHasAction && cfg->triggerRRanges.empty()) {
                const auto& rAct = cfg->triggerRAction;
                bool rNeedsPhys = rAct.type == ButtonActionType::Macro    ||
                                  rAct.type == ButtonActionType::Keyboard  ||
                                  rAct.type == ButtonActionType::MouseClick;
                float physR = rNeedsPhys ? input->getPhysicalState().triggerR : state.triggerR;
                applyTrigAct(physR, rAct,
                             trigRKbPrev, trigRMousPrev, trigRMacro, trigRMacroOk,
                             state.triggerR);
                if (rAct.type == ButtonActionType::TriggerPassthrough &&
                    rAct.target == "l2" && physR > 0.0f)
                    trigLWasCrossTarget = true;
            }

            // Ranged trigger actions (overrides simple when non-empty).
            // Skipped for triggers that received a cross-passthrough value.
            auto applyTrigRanges = [&](float physVal,
                                        const std::vector<TriggerRange>& ranges,
                                        std::vector<uint8_t>& rangePrev,
                                        std::vector<Macro>& rangeMacs,
                                        std::vector<uint8_t>& rangeMacOk,
                                        float& srcTrig) {
                if (ranges.empty()) return;
                srcTrig = 0.0f;  // trigger no longer outputs analog value
                for (size_t i = 0; i < ranges.size(); ++i) {
                    const TriggerRange& r = ranges[i];
                    bool active = (physVal >= r.from && physVal <= r.to);
                    uint8_t& prev = rangePrev[i];
                    if (!r.hasAction) { prev = active; continue; }
                    const ButtonAction& act = r.action;
                    switch (act.type) {
                    case ButtonActionType::VirtualButton:
                        applyVirtualBtnByName(state, act.name, active);
                        break;
                    case ButtonActionType::Keyboard:
                        if (active != prev) {
                            sendKeyCombo(act.keys, active);
                            if (active) {
                                std::string combo;
                                for (const auto& k : act.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                                pushEvent({ PadEventType::KeyboardAction, combo, true });
                            }
                        }
                        break;
                    case ButtonActionType::MouseClick:
                        if (active != prev) {
                            sendMouseButton(act.mouseButton, active);
                            if (active) pushEvent({ PadEventType::MouseAction, act.mouseButton + " click", true });
                        }
                        break;
                    case ButtonActionType::Macro:
                        if (active && !prev && i < rangeMacs.size() && rangeMacOk[i]) {
                            if (rangeMacs[i].getMode() == MacroRepeatMode::UntilRelease) rangeMacs[i].start();
                            else rangeMacs[i].toggle();
                            if (rangeMacs[i].isActive()) pushEvent({ PadEventType::MacroToggle, act.name, true });
                        } else if (!active && prev && i < rangeMacs.size() && rangeMacOk[i]) {
                            if (rangeMacs[i].getMode() == MacroRepeatMode::UntilRelease) rangeMacs[i].stop();
                        }
                        if (i < rangeMacs.size() && rangeMacOk[i] && rangeMacs[i].isActive())
                            rangeMacs[i].tick(state);
                        break;
                    default: break;
                    }
                    prev = active;
                }
            };
            if (!trigLWasCrossTarget) {
                float physL = input->getPhysicalState().triggerL;
                applyTrigRanges(physL, cfg->triggerLRanges,
                                trigLRangePrev, trigLRangeMacros, trigLRangeMacroOk, state.triggerL);
            }
            if (!trigRWasCrossTarget) {
                float physR = input->getPhysicalState().triggerR;
                applyTrigRanges(physR, cfg->triggerRRanges,
                                trigRRangePrev, trigRRangeMacros, trigRRangeMacroOk, state.triggerR);
            }

            trigLPrev = state.triggerL;
            trigRPrev = state.triggerR;

            // --- Mouse movement (continuous, sub-pixel accumulator) ---
            constexpr float kMouseDeadZone = 0.12f;
            float mx = (fabsf(state.mouseX) > kMouseDeadZone) ?  state.mouseX : 0.0f;
            float my = (fabsf(state.mouseY) > kMouseDeadZone) ? -state.mouseY : 0.0f;
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
            if (!editorOpen) output->update(state);
        }

        Sleep(8);
    }

        // ── End of run loop ───────────────────────────────────────────────────
        m_connected = false;

        if (!m_running) break;  // normal stop — exit outer loop

        if (lostDevice) {
            // Device disconnected unexpectedly — rescan to find it again (path may have changed).
            lostDevice = false;
            spdlog::info("[Reconnect] Scanning for device...");
            setStatus("Device disconnected — scanning...");
            // preSelected stays empty → outer loop rescans normally
        } else {
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
