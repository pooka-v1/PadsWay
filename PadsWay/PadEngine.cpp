#include "PadEngine.h"
#include "Log.h"
#include "Paths.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <windows.h>
#include <timeapi.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

#include "input/HIDScanner.h"
#include "input/HIDInputSource.h"
#include "input/DeviceHub.h"
#include "output/IOutputSink.h"
#include "output/ViGEmX360OutputAdapter.h"
#include "output/ViGEmDs4OutputAdapter.h"
#include "config/ConfigLoader.h"
#include "bots/BotLoader.h"
#include "macros/Macro.h"
#include "macros/MacroParser.h"

#pragma comment(lib, "ViGEmClient.lib")
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "winmm.lib")   // timeBeginPeriod/timeEndPeriod (timer resolution, not input)


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
//
// Fills both wVk and wScan (+ KEYEVENTF_SCANCODE). Message-based consumers (WM_KEYDOWN, e.g. menu
// navigation) only ever needed wVk and keep working; games that read hardware scan codes via
// DirectInput/Raw Input (e.g. No Man's Sky movement) were getting a wScan of 0 and never saw the
// keypress at all — see [BUG-KEYBOARD-WASD-SCANCODE], SESSION_CONTEXT.md.
static void sendKeyCombo(const std::vector<std::string>& keys, bool press) {
    if (keys.empty()) return;
    std::vector<INPUT> inputs;
    inputs.reserve(keys.size());
    auto addKey = [&](const std::string& k, bool up) {
        WORD vk = keyNameToVK(k);
        if (vk == 0) return;
        UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
        INPUT inp = {};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wVk     = vk;
        inp.ki.wScan   = static_cast<WORD>(scan & 0xFF);
        inp.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
        if ((scan >> 8) == 0xE0 || (scan >> 8) == 0xE1)
            inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
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

static void applyBotOutput(const BotOutput& out, GamepadState& state) {
    if (out.buttons & BOT_BTN_A)          state.btnA     = true;
    if (out.buttons & BOT_BTN_B)          state.btnB     = true;
    if (out.buttons & BOT_BTN_X)          state.btnX     = true;
    if (out.buttons & BOT_BTN_Y)          state.btnY     = true;
    if (out.buttons & BOT_BTN_LB)         state.btnLB    = true;
    if (out.buttons & BOT_BTN_RB)         state.btnRB    = true;
    if (out.buttons & BOT_BTN_BACK)       state.btnBack  = true;
    if (out.buttons & BOT_BTN_START)      state.btnStart = true;
    if (out.buttons & BOT_BTN_HOME)       state.btnHome  = true;
    if (out.buttons & BOT_BTN_L3)         state.btnL3    = true;
    if (out.buttons & BOT_BTN_R3)         state.btnR3    = true;
    if (out.buttons & BOT_BTN_DPAD_UP)    state.dpadUp    = true;
    if (out.buttons & BOT_BTN_DPAD_DOWN)  state.dpadDown  = true;
    if (out.buttons & BOT_BTN_DPAD_LEFT)  state.dpadLeft  = true;
    if (out.buttons & BOT_BTN_DPAD_RIGHT) state.dpadRight = true;
    if (out.lt > 0.0f)  state.triggerL = out.lt;
    if (out.rt > 0.0f)  state.triggerR = out.rt;
    if (out.lx != 0.0f) state.leftX    = out.lx;
    if (out.ly != 0.0f) state.leftY    = out.ly;
    if (out.rx != 0.0f) state.rightX   = out.rx;
    if (out.ry != 0.0f) state.rightY   = out.ry;
}

// ---------------------------------------------------------------------------
// PadEngine
// ---------------------------------------------------------------------------

PadEngine::PadEngine(DeviceHub& deviceHub) : m_deviceHub(deviceHub) {}
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

DWORD PadEngine::getLastRawButtonMask() const {
    return m_lastRawButtonMask.load();
}

DWORD PadEngine::getLastRawHat() const {
    return m_lastRawHat.load();
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

std::vector<std::string> PadEngine::getLoadedBotNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_loadedBotNames;
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
        auto configs = loadControllerConfigs(Paths::userData("data/controllers.json"));
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
            const ControllerConfig* cfg = findConfig(configs, h.vid, h.pid, h.connectionType, "", h.productName);
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
// Background thread: mirrors the original PadsWay.cpp main() logic.
// ---------------------------------------------------------------------------

void PadEngine::threadFunc() {
    // Raise the Windows timer resolution to 1 ms for the lifetime of this thread.
    // Without it, Sleep(8) in the forwarding loop rounds up to the OS default
    // granularity (~15.6 ms) → the loop runs at ~64 Hz instead of ~125 Hz, the
    // OS HID input buffer fills with stale reports and input lags by tenths of a
    // second. (Previously this was set only as a side effect of LightningBot's
    // thread, so removing the bot exposed the latent lag.) RAII guard so the
    // matching timeEndPeriod runs on every exit path — early return or exception.
    struct TimerResolutionGuard {
        TimerResolutionGuard()  { timeBeginPeriod(1); }
        ~TimerResolutionGuard() { timeEndPeriod(1);   }
    } timerResolutionGuard;

    m_phase.store(EnginePhase::Scanning);
    setStatus("Scanning for devices...");
    spdlog::info("=== PadsWay — device init ===");

    m_hidHide.addSelfToWhitelist();

    // --- One-time init: configs (shared with monitor thread) ---
    std::vector<ControllerConfig>    configs;
    std::vector<PhysicalController>  physCtrls;
    try {
        configs   = loadControllerConfigs(Paths::userData("data/controllers.json"));
        physCtrls = loadPhysicalControllers(Paths::userData("data/controllers.json"));
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
        macroLibrary = loadMacroLibrary(Paths::userData("data/macros.json"));
        if (!macroLibrary.empty())
            printf("Macro library loaded: %zu macros.\n", macroLibrary.size());
    } catch (const std::exception& ex) {
        spdlog::warn("Could not load macro library: {}", ex.what());
    }

    // --- One-time init: ViGEm (persists through device switches) ---
    VirtualPadConfig vpCfg;
    try {
        vpCfg = loadVirtualPadConfig(Paths::userData("data/virtualpad.json"));
    } catch (const std::exception& ex) {
        spdlog::warn("Could not load virtualpad.json: {} — using defaults.", ex.what());
    }
    m_outputType.store(vpCfg.outputType);

    setStatus("Connecting to ViGEm...");
    // Output port: the engine only knows IOutputSink; the concrete adapter is chosen
    // here from the configured type. Each type advertises ITS OWN VID/PID pair, and we
    // publish the active one into m_virtualVid/Pid so the scanner ignores our own
    // virtual pad. Xbox = 5650:0001; DS4 = 054C:05C4 (DualShock 4 v1, a real DS4 id that
    // ViGEm emulates natively), which doesn't clash with a physical DS4 v2 (054C:09CC).
    //
    // Persists output_type to disk, but ONLY when the engine actually applies a type (a
    // successful hot-swap, or the startup Xbox fallback). The UI no longer persists on
    // confirm, so a switch that fails to plug in can't leave a stale output_type behind.
    auto persistOutputType = [](VirtualOutputType t) {
        try { saveVirtualPadOutputType(Paths::userData("data/virtualpad.json"), t); }
        catch (...) {}
    };

    std::unique_ptr<IOutputSink> output;
    auto makeOutput = [&](VirtualOutputType type) -> std::unique_ptr<IOutputSink> {
        if (type == VirtualOutputType::DualShock) {
            spdlog::info("[PadEngine] Virtual output: DualShock 4 (DirectInput), id {:04X}:{:04X}.",
                         vpCfg.directVid, vpCfg.directPid);
            m_virtualVid.store(vpCfg.directVid);
            m_virtualPid.store(vpCfg.directPid);
            return std::make_unique<ViGEmDs4OutputAdapter>(vpCfg.directVid, vpCfg.directPid);
        }
        spdlog::info("[PadEngine] Virtual output: Xbox 360 (XInput), id {:04X}:{:04X}.",
                     vpCfg.xboxVid, vpCfg.xboxPid);
        m_virtualVid.store(vpCfg.xboxVid);
        m_virtualPid.store(vpCfg.xboxPid);
        return std::make_unique<ViGEmX360OutputAdapter>(vpCfg.xboxVid, vpCfg.xboxPid);
    };
    output = makeOutput(vpCfg.outputType);
    if (!output->isReady() && vpCfg.outputType != VirtualOutputType::Xbox) {
        // The configured type failed to plug in (transient ViGEm error, or a VID/PID the
        // driver won't enumerate). Fall back to Xbox so the engine stays alive with a
        // working virtual pad instead of dying — the user can retry the switch later.
        spdlog::warn("[PadEngine] Configured output failed to plug in; falling back to Xbox.");
        setStatus("Output failed — fell back to Xbox");
        m_outputType.store(VirtualOutputType::Xbox);
        output = makeOutput(VirtualOutputType::Xbox);
        persistOutputType(VirtualOutputType::Xbox);   // correct the file so it won't retry DS4 next boot
    }
    if (!output->isReady()) {
        spdlog::error("Aborting: could not create virtual pad.");
        setStatus("ViGEm error — is the driver installed?");
        m_running = false;
        return;
    }

    // Output-type hot-swap: recreate the ViGEm target in place. Runs on the engine
    // thread that OWNS `output`, so there is no orphan target and no data race. Any
    // game using the old virtual pad will see it disconnect and a new one appear —
    // that is why the UI warns the user to close games before switching.
    auto applyPendingOutputSwitch = [&]() {
        if (!m_outputSwitchPending.exchange(false)) return;
        VirtualOutputType newType = m_pendingOutputType.load();
        spdlog::info("[PadEngine] Hot-swapping virtual output type...");
        const VirtualOutputType prevType = m_outputType.load();
        output.reset();                 // RAII destructor calls vigem_target_remove on the old target
        output = makeOutput(newType);   // rebuilds the adapter and republishes the active VID/PID
        if (output->isReady()) {
            m_outputType.store(newType);
            persistOutputType(newType);   // only persist a switch that actually plugged in
        } else {
            // New target rejected (transient ViGEm error or a VID/PID it won't enumerate).
            // Roll back to the previous WORKING type so the user is never left without a
            // virtual pad nor stuck on "switching...".
            spdlog::error("[PadEngine] Output switch failed; rolling back to previous type.");
            setStatus("Output switch failed — kept previous type");
            output.reset();
            output = makeOutput(prevType);
        }
    };

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
                applyPendingOutputSwitch();   // honour an output switch even with no device connected
                auto hidEntries = HIDScanner::scan();

                std::vector<DeviceCandidate> allCandidates;
                for (auto& h : hidEntries) {
                    const uint16_t vVid = m_virtualVid.load(), vPid = m_virtualPid.load();
                    if (vVid && h.vid == vVid && h.pid == vPid) continue;
                    const ControllerConfig* c = findConfig(configs, h.vid, h.pid, h.connectionType, "", h.productName);
                    if (!c || c->mode != "hid") {
                        spdlog::debug("[Scan] No config: VID={:04X} PID={:04X} conn='{}' name='{}'",
                                      h.vid, h.pid, h.connectionType, h.productName);
                        continue;
                    }
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

                    while (m_running && m_selectedIndex.load() < 0) {
                        applyPendingOutputSwitch();
                        Sleep(50);
                    }

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
                                                     selected.connectionType, "", selected.name);
        if (!cfgBase) {
            spdlog::error("No config for VID={:04X} PID={:04X} ({}) — add to controllers.json.",
                selected.vid, selected.pid, selected.name);
            setStatus("No config for this device — rescanning");
            preSelected = {};
            m_phase.store(EnginePhase::Scanning);
            Sleep(2000);
            continue;  // back to scan
        }
        spdlog::info("Config loaded: {} (HID: {})", cfgBase->source_name, selected.name);
        setDevice(selected.name);
        { std::lock_guard<std::mutex> lock(m_mutex); m_activeLayoutId = cfgBase->layout_id; }

        ControllerConfig effectiveCfg = *cfgBase;
        std::vector<std::string> activeProfileContextBots;
        {
            std::string profilePath = getProfilePath();
            if (!profilePath.empty()) {
                try {
                    GameProfile profile = loadGameProfile(profilePath);
                    effectiveCfg = applyProfile(*cfgBase, profile);
                    activeProfileContextBots = profile.context_bots;
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

        // The Hub owns the actual HID connection (indexed by path) — the engine just drives its
        // reads. Lets the Scanner inspect this same device without opening a second, conflicting
        // handle to it (see ARCHITECTURE.md, "DeviceHub").
        m_deviceHub.openDriven(selected.hidPath, *cfg);
        HIDInputSource* input = m_deviceHub.get(selected.hidPath);

        // Inject PhysicalController for component-system processing (P4).
        // Rebuild button layer from effectiveCfg so profile overrides are reflected.
        {
            auto it = std::find_if(physCtrls.begin(), physCtrls.end(),
                [&](const PhysicalController& pc) {
                    return pc.vid == selected.vid && pc.pid == selected.pid;
                });
            if (it != physCtrls.end()) {
                PhysicalController pc = *it;
                rebuildPhysicalControllerFromConfig(pc, effectiveCfg);
                input->setPhysicalController(pc);
                spdlog::info("PhysicalController injected for {:04X}:{:04X}", selected.vid, selected.pid);
            }
        }

        if (!input->isConnected()) {
            spdlog::error("Failed to open input device — rescanning.");
            setStatus("Failed to open input device — rescanning");
            m_deviceHub.close(selected.hidPath);
            preSelected = {};
            m_phase.store(EnginePhase::Scanning);
            Sleep(1000);
            continue;
        }

        // Release the previous device from HidHide, then hide the new one
        m_hidHide.unhideDevice();
        m_hidHide.hideDevice(selected.vid, selected.pid);

        // ── Macros (re-initialised per device / profile) ─────────────────────
        std::unordered_map<int, std::string> botBits;     // physical bit → bot name
        std::unordered_map<int, bool>        botBtnPrev;  // physical bit → prev pressed state
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
        std::unordered_map<std::string, std::string> axisBotNames;  // key → bot name
        std::unordered_map<std::string, bool>        axisBotPrev;   // key → prev active state
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
        std::unordered_map<std::string, std::string> dpadBotNames;  // dir → bot name
        std::unordered_map<std::string, bool>        dpadBotPrev;   // dir → prev active state

        // Touchpad Zonas actions (keyed by TouchZoneRegion::id, dynamic per template — not a
        // fixed set like dpad's 4 directions). Same shape as the dpad maps above, driven by
        // cfg->touchZoneActions instead of cfg->dpadActions.
        std::unordered_map<std::string, Macro>       touchZoneMacros;
        std::unordered_map<std::string, bool>        touchZoneMacroPrev;
        std::unordered_map<std::string, std::string> touchZoneMacroNames;
        std::unordered_map<std::string, bool>        touchZoneKbPrev;
        std::unordered_map<std::string, bool>        touchZoneMousePrev;
        std::unordered_map<std::string, std::string> touchZoneBotNames;  // region id → bot name
        std::unordered_map<std::string, bool>        touchZoneBotPrev;   // region id → prev active state

        // Touchpad Movimiento (Gestos) actions (keyed by gesture id, the 12 discrete entries of
        // kGestureIcons — see MappingEditor.cpp). Same shape as the Zonas maps above, driven by
        // cfg->touchGestureActions instead of cfg->touchZoneActions — but "active" for a gesture
        // means state.touchGestureFired == gestureId THIS frame only (a 1-frame pulse from
        // HIDInputSource's classifier, see TouchGestures.h), not "held" like a zone region. The
        // shared edge-triggered lambdas below (dispatchKeyboard/etc.) already turn a single true
        // frame into a press+release pulse on their own, so no new dispatch mechanism is needed.
        std::unordered_map<std::string, Macro>       touchGestureMacros;
        std::unordered_map<std::string, bool>        touchGestureMacroPrev;
        std::unordered_map<std::string, std::string> touchGestureMacroNames;
        std::unordered_map<std::string, bool>        touchGestureKbPrev;
        std::unordered_map<std::string, bool>        touchGestureMousePrev;
        std::unordered_map<std::string, std::string> touchGestureBotNames;  // gesture id → bot name
        std::unordered_map<std::string, bool>        touchGestureBotPrev;   // gesture id → prev active state

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
        bool  trigLBotPrev  = false;      // previous active state for bot-toggle trigger L
        bool  trigRBotPrev  = false;
        // Ranged trigger state (indexed by range position in triggerLRanges/triggerRRanges)
        // uint8_t instead of bool to avoid std::vector<bool> proxy reference issues
        std::vector<uint8_t> trigLRangePrev;
        std::vector<uint8_t> trigRRangePrev;
        std::vector<Macro>   trigLRangeMacros;
        std::vector<Macro>   trigRRangeMacros;
        std::vector<uint8_t> trigLRangeMacroOk;
        std::vector<uint8_t> trigRRangeMacroOk;

        // Gyro/accel-as-source Macro/Keyboard/Mouse/Bot state — same shape as the axis-direction
        // maps above (keyed by "x_pos".."z_neg"), but driven by cfg->gyro_actions/accel_actions
        // and HIDInputSource::getActiveGyroActions()/getActiveAccelActions() instead of the
        // stick/trigger axis_actions. See ImuActionState comment below.
        struct ImuActionState {
            std::unordered_map<std::string, Macro>       macros;
            std::unordered_map<std::string, bool>        macroPrev;
            std::unordered_map<std::string, std::string> macroNames;
            std::unordered_map<std::string, bool>        kbPrev;
            std::unordered_map<std::string, bool>        mousePrev;
            std::unordered_map<std::string, std::string> botNames;
            std::unordered_map<std::string, bool>        botPrev;
            std::unordered_map<std::string, std::optional<ButtonAction>> rangePrev;
            std::unordered_map<std::string, Macro>        rangeMacros;
            std::unordered_map<std::string, bool>         rangeMacroOk;
        };
        ImuActionState gyroActionState;
        ImuActionState accelActionState;

        // (Re)builds an ImuActionState from a gyro_actions/accel_actions map. Mirrors the
        // axis_actions init blocks above (kb/mouse/bot/ranges-macro-parse + simple macros).
        auto initImuActionState = [&](ImuActionState& st,
                                       const std::unordered_map<std::string, HalfAxisAction>& actions) {
            st.macros.clear();    st.macroPrev.clear(); st.macroNames.clear();
            st.kbPrev.clear();    st.mousePrev.clear();
            st.botNames.clear();  st.botPrev.clear();
            st.rangePrev.clear(); st.rangeMacros.clear(); st.rangeMacroOk.clear();
            for (const auto& [key, action] : actions) {
                if (action.type == HalfAxisActionType::Keyboard)   st.kbPrev[key]    = false;
                if (action.type == HalfAxisActionType::MouseClick) st.mousePrev[key] = false;
                if (action.type == HalfAxisActionType::Bot) {
                    st.botNames[key] = action.target;
                    st.botPrev[key]  = false;
                    spdlog::info("Bot '{}' assigned to IMU axis direction {}.", action.target, key);
                }
                if (action.type == HalfAxisActionType::Macro) {
                    std::string execution = action.execution;
                    if (execution.empty()) {
                        auto it = macroLibrary.find(action.target);
                        if (it == macroLibrary.end()) {
                            spdlog::warn("Macro '{}' (IMU axis {}) not found in library.", action.target, key);
                            continue;
                        }
                        execution = it->second;
                    }
                    try {
                        Macro m;
                        MacroParser::parse(execution, m);
                        st.macros[key]     = std::move(m);
                        st.macroPrev[key]  = false;
                        st.macroNames[key] = action.target;
                        spdlog::info("Macro '{}' assigned to IMU axis direction {}.", action.target, key);
                    } catch (const std::exception& ex) {
                        spdlog::error("Error parsing macro '{}': {}", action.target, ex.what());
                    }
                }
                if (action.type == HalfAxisActionType::Ranges) {
                    st.rangePrev[key] = std::nullopt;
                    for (const auto& r : action.ranges) {
                        if (!r.hasAction || r.action.type != ButtonActionType::Macro) continue;
                        std::string mkey = key + "|" + r.action.name;
                        auto it = macroLibrary.find(r.action.name);
                        if (it == macroLibrary.end()) {
                            spdlog::warn("Macro '{}' (IMU range {}) not found.", r.action.name, key);
                            st.rangeMacroOk[mkey] = false;
                            continue;
                        }
                        try {
                            Macro m;
                            MacroParser::parse(it->second, m);
                            st.rangeMacros[mkey]  = std::move(m);
                            st.rangeMacroOk[mkey] = true;
                        } catch (...) {
                            spdlog::warn("Failed to parse macro '{}' (IMU range {}).", r.action.name, key);
                            st.rangeMacroOk[mkey] = false;
                        }
                    }
                }
            }
        };

        // ── Shared edge-triggered dispatch mechanics ──────────────────────────────
        // Every action-holder (button/dpad/axis/gyro/accel/touch zone/trigger) fires its
        // Macro/Keyboard/MouseClick/Bot through one of these four, instead of each re-implementing
        // the same press-edge/release-edge bookkeeping. All are gated by `editorOpen` (passed
        // explicitly, not captured — same reason `state`/`botLoader` are parameters below: they're
        // declared further down this function, after these lambdas): while the Mapeador is open,
        // re-selecting an already-mapped source (e.g. holding H9 on a touch zone that already fires
        // a macro) must not leak real keystrokes/clicks/macro starts to the OS mid-edit. Bailing out
        // *before* touching `prev` (rather than updating it and only gating the dispatch) means a
        // source held across the open→close transition still fires correctly once the editor
        // closes, instead of the transition being silently missed.
        //
        // Keyboard/Mouse return the edge that just fired (1 = fresh press, -1 = fresh release, 0 =
        // none/suppressed) for the rare caller that logs something source-specific beyond the
        // shared pushEvent (buttons' debug traces). Macro returns whether it just started/toggled
        // on an unsuppressed press edge, for callers with extra per-source bookkeeping (buttons'
        // macro rotation-count reset) or a per-source log tag to react to.
        auto dispatchKeyboard = [&](bool editorOpen, bool active, bool& prev,
                                    const std::vector<std::string>& keys) -> int {
            if (editorOpen) return 0;
            int edge = 0;
            if (active && !prev) {
                sendKeyCombo(keys, true);
                std::string combo;
                for (const auto& k : keys) { if (!combo.empty()) combo += '+'; combo += k; }
                pushEvent({ PadEventType::KeyboardAction, combo, true });
                edge = 1;
            } else if (!active && prev) {
                sendKeyCombo(keys, false);
                edge = -1;
            }
            prev = active;
            return edge;
        };

        auto dispatchMouse = [&](bool editorOpen, bool active, bool& prev,
                                  const std::string& btn) -> int {
            if (editorOpen || active == prev) return 0;
            sendMouseButton(btn, active);
            if (active) pushEvent({ PadEventType::MouseAction, btn + " click", true });
            prev = active;
            return active ? 1 : -1;
        };

        auto dispatchBot = [&](bool editorOpen, bool active, bool& prev,
                                const std::string& botName, BotLoader& botLoader) {
            if (editorOpen) return;
            if (active && !prev) {
                if (auto* b = botLoader.find(botName)) {
                    b->toggle();
                    spdlog::info("[BOT] '{}' {}", botName, b->isActive() ? "ON" : "OFF");
                    pushEvent({ PadEventType::BotToggle, botName, b->isActive() });
                } else {
                    spdlog::warn("[BOT] '{}' not loaded.", botName);
                }
            }
            prev = active;
        };

        // Start/stop mechanic only — does NOT tick the macro. Most callers tick right after this
        // returns (a plain `macro.tick(state);` inside the same loop); buttons keep their own
        // separate tick pass (rotation-lap auto-off counting alongside the bot tick loop), so
        // ticking here would double-tick for them.
        auto dispatchMacro = [&](bool editorOpen, Macro& macro, bool active, bool& prev) -> bool {
            if (editorOpen) return false;
            bool freshPress = false;
            if (active && !prev) {
                if (macro.getMode() == MacroRepeatMode::UntilRelease) macro.start();
                else macro.toggle();
                freshPress = true;
            } else if (!active && prev) {
                if (macro.getMode() == MacroRepeatMode::UntilRelease) macro.stop();
            }
            prev = active;
            return freshPress;
        };

        // Shared "ranged" dispatch: exactly one action out of a keyed set of exclusive ranges can
        // be active at a time (e.g. a stick axis or gyro/accel direction split into magnitude
        // bands). Releases whichever action was previously active when the current one changes
        // (including "went back to nothing"), then activates the new one. Shared by gyro/accel
        // (IMU) and stick-axis ranges, which both use this unordered_map<string,
        // optional<ButtonAction>> shape — trigger ranges use a plain vector<uint8_t> instead
        // (ranges there are positions in a list, not named/keyed), so that block keeps its own
        // shape rather than being forced into this one. Bails out before touching `prev` while
        // editorOpen, same reasoning as the dispatch lambdas above, so the transition is detected
        // correctly once the editor closes instead of being missed.
        auto dispatchRangeAction = [&](bool editorOpen, const std::string& key,
                                        std::optional<ButtonAction>& prev,
                                        const std::unordered_map<std::string, ButtonAction>& activeRangeActions,
                                        std::unordered_map<std::string, Macro>& rangeMacros,
                                        std::unordered_map<std::string, bool>& rangeMacroOk,
                                        GamepadState& state, BotLoader& botLoader) {
            if (editorOpen) return;
            auto it = activeRangeActions.find(key);
            bool isActive = (it != activeRangeActions.end());
            bool changed  = isActive
                ? (!prev.has_value() ||
                   prev->type        != it->second.type        ||
                   prev->name        != it->second.name        ||
                   prev->mouseButton != it->second.mouseButton ||
                   prev->keys        != it->second.keys)
                : prev.has_value();
            if (!changed) return;

            if (prev.has_value()) {
                if (prev->type == ButtonActionType::Keyboard)
                    sendKeyCombo(prev->keys, false);
                else if (prev->type == ButtonActionType::MouseClick)
                    sendMouseButton(prev->mouseButton, false);
                else if (prev->type == ButtonActionType::Macro) {
                    auto mit = rangeMacros.find(key + "|" + prev->name);
                    if (mit != rangeMacros.end() && rangeMacroOk[key + "|" + prev->name])
                        if (mit->second.getMode() == MacroRepeatMode::UntilRelease)
                            mit->second.stop();
                }
            }
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
                    auto mit = rangeMacros.find(mkey);
                    if (mit != rangeMacros.end() && rangeMacroOk[mkey]) {
                        if (mit->second.getMode() == MacroRepeatMode::UntilRelease)
                            mit->second.start();
                        else
                            mit->second.toggle();
                        pushEvent({ PadEventType::MacroToggle, cur.name, mit->second.isActive() });
                    }
                } else if (cur.type == ButtonActionType::Bot) {
                    if (auto* b = botLoader.find(cur.name)) {
                        b->toggle();
                        spdlog::info("[BOT] '{}' {}", cur.name, b->isActive() ? "ON" : "OFF");
                        pushEvent({ PadEventType::BotToggle, cur.name, b->isActive() });
                    } else {
                        spdlog::warn("[BOT] '{}' not loaded.", cur.name);
                    }
                }
                prev = cur;
            } else {
                prev = std::nullopt;
            }
        };

        // Ticks one ImuActionState for the current frame: edge-detects activeKeys/activeRangeActions
        // (as reported by HIDInputSource::getActive{Gyro,Accel}[Range]Actions()) and fires
        // Macro/Keyboard/MouseClick/Bot exactly like the axis-direction block below does for
        // axis_actions. `actions` is the source map (cfg->gyro_actions/accel_actions), needed to
        // look up .keys/.mouseButton for the simple (non-Ranges) Keyboard/MouseClick case.
        // `state`/`botLoader` are taken by reference (not captured) because both are declared
        // further down this function, after this lambda — see their declarations below.
        auto tickImuActionState = [&](bool editorOpen, ImuActionState& st,
                                       const std::unordered_map<std::string, HalfAxisAction>& actions,
                                       const std::vector<std::string>& activeKeys,
                                       const std::unordered_map<std::string, ButtonAction>& activeRangeActions,
                                       GamepadState& state, BotLoader& botLoader) {
            std::unordered_set<std::string> activeSet(activeKeys.begin(), activeKeys.end());

            for (auto& [key, macro] : st.macros) {
                bool active = activeSet.count(key) > 0;
                bool& prev  = st.macroPrev[key];
                if (dispatchMacro(editorOpen, macro, active, prev)) {
                    if (macro.isActive())
                        spdlog::info("[MACRO][IMU] '{}' ON", st.macroNames[key]);
                    pushEvent({ PadEventType::MacroToggle, st.macroNames[key], macro.isActive() });
                }
                macro.tick(state);
            }

            for (auto& [key, prev] : st.kbPrev) {
                bool active = activeSet.count(key) > 0;
                dispatchKeyboard(editorOpen, active, prev, actions.at(key).keys);
            }

            for (auto& [key, prev] : st.mousePrev) {
                bool active = activeSet.count(key) > 0;
                dispatchMouse(editorOpen, active, prev, actions.at(key).mouseButton);
            }

            for (auto& [key, prev] : st.botPrev) {
                bool active = activeSet.count(key) > 0;
                dispatchBot(editorOpen, active, prev, st.botNames[key], botLoader);
            }

            for (auto& [key, prev] : st.rangePrev) {
                dispatchRangeAction(editorOpen, key, prev, activeRangeActions,
                                    st.rangeMacros, st.rangeMacroOk, state, botLoader);
            }
            for (auto& [mkey, macro] : st.rangeMacros)
                macro.tick(state);
        };

        auto initMacros = [&]() {
            macros.clear();      macroPrevBtn.clear(); macroNames.clear();
            macroRotCount.clear(); macroLastRX.clear(); macroLastRY.clear();
            kbPrevBtn.clear();   mousePrevBtn.clear();
            axisMacros.clear();  axisMacroPrev.clear(); axisMacroNames.clear();
            axisKbPrev.clear();  axisMousePrev.clear();
            axisBotNames.clear(); axisBotPrev.clear();
            dpadMacros.clear();  dpadMacroPrev.clear(); dpadMacroNames.clear();
            dpadKbPrev.clear();  dpadMousePrev.clear();
            dpadBotNames.clear(); dpadBotPrev.clear();
            touchZoneMacros.clear();  touchZoneMacroPrev.clear(); touchZoneMacroNames.clear();
            touchZoneKbPrev.clear();  touchZoneMousePrev.clear();
            touchZoneBotNames.clear(); touchZoneBotPrev.clear();
            touchGestureMacros.clear();  touchGestureMacroPrev.clear(); touchGestureMacroNames.clear();
            touchGestureKbPrev.clear();  touchGestureMousePrev.clear();
            touchGestureBotNames.clear(); touchGestureBotPrev.clear();
            for (const auto& [bit, action] : cfg->buttons) {
                if (action.type == ButtonActionType::Keyboard)   kbPrevBtn[bit]    = false;
                if (action.type == ButtonActionType::MouseClick) mousePrevBtn[bit] = false;
            }
            for (const auto& [dir, action] : cfg->dpadActions) {
                if (action.type == ButtonActionType::Keyboard)   dpadKbPrev[dir]    = false;
                if (action.type == ButtonActionType::MouseClick) dpadMousePrev[dir] = false;
            }
            for (const auto& [regionId, action] : cfg->touchZoneActions) {
                if (action.type == ButtonActionType::Keyboard)   touchZoneKbPrev[regionId]    = false;
                if (action.type == ButtonActionType::MouseClick) touchZoneMousePrev[regionId] = false;
            }
            for (const auto& [gestureId, action] : cfg->touchGestureActions) {
                if (action.type == ButtonActionType::Keyboard)   touchGestureKbPrev[gestureId]    = false;
                if (action.type == ButtonActionType::MouseClick) touchGestureMousePrev[gestureId] = false;
            }
            axisRangePrev.clear();
            axisRangeMacros.clear();
            axisRangeMacroOk.clear();
            for (const auto& [key, action] : cfg->axis_actions) {
                if (action.type == HalfAxisActionType::Keyboard)   axisKbPrev[key]    = false;
                if (action.type == HalfAxisActionType::MouseClick) axisMousePrev[key] = false;
                if (action.type == HalfAxisActionType::Bot) {
                    axisBotNames[key] = action.target;
                    axisBotPrev[key]  = false;
                    spdlog::info("Bot '{}' assigned to axis direction {}.", action.target, key);
                }
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
            botBits.clear();
            botBtnPrev.clear();
            for (const auto& [bit, action] : cfg->buttons) {
                if (action.type != ButtonActionType::Bot) continue;
                botBits[bit]    = action.name;
                botBtnPrev[bit] = false;
                spdlog::info("Bot '{}' assigned to button {}.", action.name, bit);
            }
            for (const auto& [dir, action] : cfg->dpadActions) {
                if (action.type != ButtonActionType::Bot) continue;
                dpadBotNames[dir] = action.name;
                dpadBotPrev[dir]  = false;
                spdlog::info("Bot '{}' assigned to dpad {}.", action.name, dir);
            }
            for (const auto& [regionId, action] : cfg->touchZoneActions) {
                if (action.type != ButtonActionType::Bot) continue;
                touchZoneBotNames[regionId] = action.name;
                touchZoneBotPrev[regionId]  = false;
                spdlog::info("Bot '{}' assigned to touch zone '{}'.", action.name, regionId);
            }
            for (const auto& [gestureId, action] : cfg->touchGestureActions) {
                if (action.type != ButtonActionType::Bot) continue;
                touchGestureBotNames[gestureId] = action.name;
                touchGestureBotPrev[gestureId]  = false;
                spdlog::info("Bot '{}' assigned to gesture '{}'.", action.name, gestureId);
            }
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
            // Touch zone macros
            for (const auto& [regionId, action] : cfg->touchZoneActions) {
                if (action.type != ButtonActionType::Macro) continue;
                std::string execution = action.execution;
                if (execution.empty()) {
                    auto it = macroLibrary.find(action.name);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' (touch zone '{}') not found in library.", action.name, regionId);
                        continue;
                    }
                    execution = it->second;
                }
                try {
                    Macro m;
                    MacroParser::parse(execution, m);
                    touchZoneMacros[regionId]     = std::move(m);
                    touchZoneMacroPrev[regionId]  = false;
                    touchZoneMacroNames[regionId] = action.name;
                    spdlog::info("Macro '{}' assigned to touch zone '{}'.", action.name, regionId);
                } catch (const std::exception& ex) {
                    spdlog::error("Error parsing macro '{}': {}", action.name, ex.what());
                }
            }
            // Touch gesture macros
            for (const auto& [gestureId, action] : cfg->touchGestureActions) {
                if (action.type != ButtonActionType::Macro) continue;
                std::string execution = action.execution;
                if (execution.empty()) {
                    auto it = macroLibrary.find(action.name);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' (gesture '{}') not found in library.", action.name, gestureId);
                        continue;
                    }
                    execution = it->second;
                }
                try {
                    Macro m;
                    MacroParser::parse(execution, m);
                    touchGestureMacros[gestureId]     = std::move(m);
                    touchGestureMacroPrev[gestureId]  = false;
                    touchGestureMacroNames[gestureId] = action.name;
                    spdlog::info("Macro '{}' assigned to gesture '{}'.", action.name, gestureId);
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
            trigLBotPrev = trigRBotPrev = false;
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

            initImuActionState(gyroActionState, cfg->gyro_actions);
            initImuActionState(accelActionState, cfg->accel_actions);
        };
        initMacros();

        BotLoader botLoader;
        botLoader.scan("data/bots");
        {
            std::vector<std::string> names;
            for (const auto& b : botLoader.bots()) names.push_back(b->name);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_loadedBotNames = std::move(names);
        }
        for (const auto& bname : cfgBase->context_bots) {
            if (auto* b = botLoader.find(bname)) {
                b->start();
                spdlog::info("[BOT] Context bot '{}' started (device).", bname);
                pushEvent({ PadEventType::BotToggle, bname, true });
            } else {
                spdlog::warn("[BOT] Context bot '{}' not loaded.", bname);
            }
        }
        for (const auto& bname : activeProfileContextBots) {
            if (auto* b = botLoader.find(bname)) {
                b->start();
                spdlog::info("[BOT] Context bot '{}' started (profile init).", bname);
                pushEvent({ PadEventType::BotToggle, bname, true });
            }
        }
        std::string currentProfilePath = getProfilePath();

        // ── Main run loop ─────────────────────────────────────────────────────
        setStatus("Running");
        m_connected = true;
        m_phase.store(EnginePhase::Running);
        spdlog::info("Forwarding input. Close the window to exit.");

    GamepadState state;
    bool         mouseWasMoving    = false;
    float        mouseAccumX       = 0.0f;
    float        mouseAccumY       = 0.0f;
    bool         lostDevice        = false;  // set when device disconnects unexpectedly
    int          reconnectTries    = 0;

    while (m_running && !m_switchPending.load()) {
        applyPendingOutputSwitch();   // recreate the virtual target if the UI asked to
        // Profile hot-swap: detect change or explicit reload request.
        std::string newProfile = getProfilePath();
        if (newProfile != currentProfilePath || m_profileDirty.exchange(false)) {
            currentProfilePath = newProfile;
            effectiveCfg = *cfgBase;
            activeProfileContextBots.clear();
            if (!currentProfilePath.empty()) {
                try {
                    GameProfile profile = loadGameProfile(currentProfilePath);
                    effectiveCfg = applyProfile(*cfgBase, profile);
                    activeProfileContextBots = profile.context_bots;
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
                    rebuildPhysicalControllerFromConfig(pc, effectiveCfg);
                    input->setPhysicalController(pc);
                }
            }
            botLoader.stopAll();
            for (const auto& bname : cfgBase->context_bots) {
                if (auto* b = botLoader.find(bname)) {
                    b->start();
                    pushEvent({ PadEventType::BotToggle, bname, true });
                }
            }
            for (const auto& bname : activeProfileContextBots) {
                if (auto* b = botLoader.find(bname)) {
                    b->start();
                    pushEvent({ PadEventType::BotToggle, bname, true });
                }
            }
            mouseAccumX = 0.0f;
            mouseAccumY = 0.0f;
            initMacros();
        }

        // Macro library hot-reload: pick up macros.json changes saved by the macro manager.
        if (m_macroLibDirty.exchange(false)) {
            try {
                macroLibrary = loadMacroLibrary(Paths::userData("data/macros.json"));
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
            cfgBase = findConfig(configs, selected.vid, selected.pid, selected.connectionType, "", selected.name);
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
                    physCtrls = loadPhysicalControllers(Paths::userData("data/controllers.json"));
                    auto it = std::find_if(physCtrls.begin(), physCtrls.end(),
                        [&](const PhysicalController& pc) {
                            return pc.vid == selected.vid && pc.pid == selected.pid;
                        });
                    if (it != physCtrls.end()) {
                        PhysicalController pc = *it;
                        rebuildPhysicalControllerFromConfig(pc, effectiveCfg);
                        input->setPhysicalController(pc);
                    }
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

        if (m_deviceHub.read(selected.hidPath, state)) {
            DWORD btns = input->getLastButtonMask();
            m_lastRawButtonMask.store(btns);
            m_lastRawHat.store(input->getLastRawHat());
            { std::lock_guard<std::mutex> lock(m_mutex); m_lastState = input->getPhysicalState(); }
            const bool editorOpen = m_editorOpen.load();
            // Bot and macro toggle detection uses the button mask from the read just performed

            for (auto& [bit, botName] : botBits) {
                bool  pressed = (btns & (1u << (bit - 1))) != 0;
                bool& prev    = botBtnPrev[bit];
                dispatchBot(editorOpen, pressed, prev, botName, botLoader);
            }

            for (auto& [bit, macro] : macros) {
                bool pressed = (btns & (1u << (bit - 1))) != 0;
                bool& prev   = macroPrevBtn[bit];
                if (dispatchMacro(editorOpen, macro, pressed, prev)) {
                    if (macro.isActive()) {
                        macroRotCount[bit] = 0;
                        macroLastRX[bit]   = 0.0f;
                        macroLastRY[bit]   = 0.0f;
                    }
                    spdlog::info("[MACRO][{}] '{}' {}", GetTickCount64(), macroNames[bit],
                           macro.isActive() ? "ON" : "OFF");
                    pushEvent({ PadEventType::MacroToggle, macroNames[bit], macro.isActive() });
                }
            }

            for (auto& botInst : botLoader.bots()) {
                if (!botInst->isActive()) continue;
                BotOutput out{};
                out.version = BOT_API_VERSION;
                if (botInst->tick(&out))
                    applyBotOutput(out, state);
            }

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
                int edge = dispatchKeyboard(editorOpen, pressed, prev, cfg->buttons.at(bit).keys);
                if (edge == 1)  spdlog::debug("[KB] button {} down", bit);
                if (edge == -1) spdlog::debug("[KB] button {} up", bit);
            }

            // --- Mouse click actions (edge-triggered) ---
            for (auto& [bit, prev] : mousePrevBtn) {
                bool pressed = (btns & (1u << (bit - 1))) != 0;
                int edge = dispatchMouse(editorOpen, pressed, prev, cfg->buttons.at(bit).mouseButton);
                if (edge != 0) spdlog::debug("[MOUSE] button {} {}", bit, edge > 0 ? "down" : "up");
            }

            // --- Axis-direction Macro / Keyboard / Mouse (edge-triggered) ---
            {
                auto activeAA = input->getActiveAxisActions();
                std::unordered_set<std::string> activeAASet(activeAA.begin(), activeAA.end());

                for (auto& [key, macro] : axisMacros) {
                    bool active = activeAASet.count(key) > 0;
                    bool& prev  = axisMacroPrev[key];
                    if (dispatchMacro(editorOpen, macro, active, prev)) {
                        if (macro.isActive())
                            spdlog::info("[MACRO][AXIS] '{}' ON", axisMacroNames[key]);
                        pushEvent({ PadEventType::MacroToggle, axisMacroNames[key], macro.isActive() });
                    }
                    macro.tick(state);
                }

                for (auto& [key, prev] : axisKbPrev) {
                    bool active = activeAASet.count(key) > 0;
                    dispatchKeyboard(editorOpen, active, prev, cfg->axis_actions.at(key).keys);
                }

                for (auto& [key, prev] : axisMousePrev) {
                    bool active = activeAASet.count(key) > 0;
                    dispatchMouse(editorOpen, active, prev, cfg->axis_actions.at(key).mouseButton);
                }

                for (auto& [key, prev] : axisBotPrev) {
                    bool active = activeAASet.count(key) > 0;
                    dispatchBot(editorOpen, active, prev, axisBotNames[key], botLoader);
                }

                // Axis Ranges: Keyboard / MouseClick / Macro edge-triggered per range action
                {
                    const auto& rangeActions = input->getActiveAxisRangeActions();
                    for (auto& [key, prev] : axisRangePrev) {
                        dispatchRangeAction(editorOpen, key, prev, rangeActions,
                                            axisRangeMacros, axisRangeMacroOk, state, botLoader);
                    }
                    // Tick active axis range macros every frame
                    for (auto& [mkey, macro] : axisRangeMacros)
                        macro.tick(state);
                }
            }

            // --- Gyro/Accel-as-source Macro / Keyboard / Mouse / Bot (edge-triggered) ---
            // Same shape as the axis-direction block above, but for gyro/accel used as the
            // *source* of a Keyboard/Macro/MouseClick/Bot assignment. PhysicalGyro/PhysicalAccel
            // (Component System) only resolve VirtualButton/Dpad/Trigger/StickSlot/MouseMove
            // targets into GamepadState directly; these marker targets need picking up here.
            tickImuActionState(editorOpen, gyroActionState, cfg->gyro_actions,
                                input->getActiveGyroActions(), input->getActiveGyroRangeActions(),
                                state, botLoader);
            tickImuActionState(editorOpen, accelActionState, cfg->accel_actions,
                                input->getActiveAccelActions(), input->getActiveAccelRangeActions(),
                                state, botLoader);

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
            // Consumes the raw dpad flag for `dir` once a remap on it fires, so it doesn't also
            // pass through as a plain dpad press to the virtual output.
            auto consumeDpadDir = [&](const std::string& dir) {
                if (dir == "up")    state.dpadUp    = false;
                if (dir == "down")  state.dpadDown  = false;
                if (dir == "left")  state.dpadLeft  = false;
                if (dir == "right") state.dpadRight = false;
            };
            for (auto& [dir, macro] : dpadMacros) {
                bool active = dpadActive(dir);
                bool& prev  = dpadMacroPrev[dir];
                if (dispatchMacro(editorOpen, macro, active, prev)) {
                    if (macro.isActive())
                        spdlog::info("[MACRO][DPAD] '{}' ON", dpadMacroNames[dir]);
                    pushEvent({ PadEventType::MacroToggle, dpadMacroNames[dir], macro.isActive() });
                }
                macro.tick(state);
                if (active) consumeDpadDir(dir);
            }
            for (auto& [dir, prev] : dpadKbPrev) {
                bool active = dpadActive(dir);
                dispatchKeyboard(editorOpen, active, prev, cfg->dpadActions.at(dir).keys);
                if (active) consumeDpadDir(dir);
            }
            for (auto& [dir, prev] : dpadMousePrev) {
                bool active = dpadActive(dir);
                dispatchMouse(editorOpen, active, prev, cfg->dpadActions.at(dir).mouseButton);
                if (active) consumeDpadDir(dir);
            }
            for (auto& [dir, prev] : dpadBotPrev) {
                bool active = dpadActive(dir);
                dispatchBot(editorOpen, active, prev, dpadBotNames[dir], botLoader);
                if (active) consumeDpadDir(dir);
            }
            // Dpad direction → virtual trigger (L2/R2)
            for (const auto& [dir, action] : cfg->dpadActions) {
                if (action.type != ButtonActionType::Trigger) continue;
                bool active = dpadActive(dir);
                if (active) {
                    if      (action.target == "l2") state.triggerL = 1.0f;
                    else if (action.target == "r2") state.triggerR = 1.0f;
                    consumeDpadDir(dir);
                }
            }

            // --- Touchpad Zonas actions (Macro / Keyboard / Mouse / Bot / VirtualButton / Trigger,
            // edge-triggered where it applies) ---
            // Reads `state`, not physical, unlike dpadActive above: activeTouchZone1/2 is itself
            // the Component System's output (PhysicalTouchpad::process() resolves the hit-test
            // straight into GamepadState), not something a downstream remap could have touched.
            // Either finger can drive its own region independently (e.g. two-finger chords on
            // split-lr-2), so a region counts as active if ANY finger currently sits in it.
            auto touchZoneActive = [&](const std::string& regionId) -> bool {
                return state.activeTouchZone1 == regionId || state.activeTouchZone2 == regionId;
            };
            for (auto& [regionId, macro] : touchZoneMacros) {
                bool active = touchZoneActive(regionId);
                bool& prev  = touchZoneMacroPrev[regionId];
                if (dispatchMacro(editorOpen, macro, active, prev)) {
                    if (macro.isActive())
                        spdlog::info("[MACRO][TOUCHZONE] '{}' ON", touchZoneMacroNames[regionId]);
                    pushEvent({ PadEventType::MacroToggle, touchZoneMacroNames[regionId], macro.isActive() });
                }
                macro.tick(state);
            }
            for (auto& [regionId, prev] : touchZoneKbPrev) {
                bool active = touchZoneActive(regionId);
                dispatchKeyboard(editorOpen, active, prev, cfg->touchZoneActions.at(regionId).keys);
            }
            for (auto& [regionId, prev] : touchZoneMousePrev) {
                bool active = touchZoneActive(regionId);
                dispatchMouse(editorOpen, active, prev, cfg->touchZoneActions.at(regionId).mouseButton);
            }
            for (auto& [regionId, prev] : touchZoneBotPrev) {
                bool active = touchZoneActive(regionId);
                dispatchBot(editorOpen, active, prev, touchZoneBotNames[regionId], botLoader);
            }
            // VirtualButton/Trigger: level-based, no prev-state (applyVirtualBtnByName only ever
            // sets true — same OR-latch every other button source relies on; a plain trigger
            // level matches the dpad-as-trigger block above).
            for (const auto& [regionId, action] : cfg->touchZoneActions) {
                if (action.type != ButtonActionType::VirtualButton) continue;
                if (touchZoneActive(regionId)) applyVirtualBtnByName(state, action.name, true);
            }
            for (const auto& [regionId, action] : cfg->touchZoneActions) {
                if (action.type != ButtonActionType::Trigger) continue;
                if (!touchZoneActive(regionId)) continue;
                if      (action.target == "l2") state.triggerL = 1.0f;
                else if (action.target == "r2") state.triggerR = 1.0f;
            }

            // --- Touchpad Movimiento (Gestos) actions — same vocabulary/dispatch shape as Zonas
            // above, but "active" means state.touchGestureFired == gestureId THIS frame only (a
            // 1-frame pulse — see TouchGestures.h/HIDInputSource::classifyTouchRelease), not "a
            // finger is currently sitting here". The shared edge-triggered lambdas below turn that
            // single true frame into a press+release pulse on their own, exactly like a very quick
            // button tap — no new dispatch mechanism needed. Only the 12 discrete gestures ever
            // populate touchGestureActions/touchGestureFired; the 2 twist gestures are a separate,
            // not-yet-implemented continuous mechanism (see ARCHITECTURE.md "Movimiento").
            auto touchGestureActive = [&](const std::string& gestureId) -> bool {
                return state.touchGestureFired == gestureId;
            };
            for (auto& [gestureId, macro] : touchGestureMacros) {
                bool active = touchGestureActive(gestureId);
                bool& prev  = touchGestureMacroPrev[gestureId];
                if (dispatchMacro(editorOpen, macro, active, prev)) {
                    if (macro.isActive())
                        spdlog::info("[MACRO][GESTURE] '{}' ON", touchGestureMacroNames[gestureId]);
                    pushEvent({ PadEventType::MacroToggle, touchGestureMacroNames[gestureId], macro.isActive() });
                }
                macro.tick(state);
            }
            for (auto& [gestureId, prev] : touchGestureKbPrev) {
                bool active = touchGestureActive(gestureId);
                dispatchKeyboard(editorOpen, active, prev, cfg->touchGestureActions.at(gestureId).keys);
            }
            for (auto& [gestureId, prev] : touchGestureMousePrev) {
                bool active = touchGestureActive(gestureId);
                dispatchMouse(editorOpen, active, prev, cfg->touchGestureActions.at(gestureId).mouseButton);
            }
            for (auto& [gestureId, prev] : touchGestureBotPrev) {
                bool active = touchGestureActive(gestureId);
                dispatchBot(editorOpen, active, prev, touchGestureBotNames[gestureId], botLoader);
            }
            for (const auto& [gestureId, action] : cfg->touchGestureActions) {
                if (action.type != ButtonActionType::VirtualButton) continue;
                if (touchGestureActive(gestureId)) applyVirtualBtnByName(state, action.name, true);
            }
            for (const auto& [gestureId, action] : cfg->touchGestureActions) {
                if (action.type != ButtonActionType::Trigger) continue;
                if (!touchGestureActive(gestureId)) continue;
                if      (action.target == "l2") state.triggerL = 1.0f;
                else if (action.target == "r2") state.triggerR = 1.0f;
            }

            // --- Trigger-as-source actions ---
            constexpr float kTrigActThresh = 0.1f;  // activation threshold for digital targets
            // Helper: apply a single ButtonAction driven by a float trigger value.
            // physVal is the raw physical trigger value [0..1].
            // prevActive is the per-action edge-detect flag (modified in place).
            // After the call, the source trigger value in state has been routed/cleared.
            auto applyTrigAct = [&](float physVal, const ButtonAction& act,
                                     bool& kbPrev, bool& mousPrev, Macro& mac, bool macOk,
                                     bool& botPrev, float& srcTrig) {
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
                    dispatchKeyboard(editorOpen, active, kbPrev, act.keys);
                    srcTrig = 0.0f;
                    break;
                case ButtonActionType::MouseClick:
                    dispatchMouse(editorOpen, active, mousPrev, act.mouseButton);
                    srcTrig = 0.0f;
                    break;
                case ButtonActionType::Macro:
                    // Reuses kbPrev as macroPrev for trigger sources (mutually exclusive with the
                    // Keyboard case above by construction — act.type is one or the other).
                    if (macOk) {
                        if (dispatchMacro(editorOpen, mac, active, kbPrev) && mac.isActive())
                            pushEvent({ PadEventType::MacroToggle, act.name, true });
                        if (mac.isActive()) mac.tick(state);
                    } else {
                        kbPrev = active;
                    }
                    srcTrig = 0.0f;
                    break;
                case ButtonActionType::Bot:
                    dispatchBot(editorOpen, active, botPrev, act.name, botLoader);
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
                // Marker targets (Macro/KB/Mouse/Bot) are not written by the Component System,
                // so read the physical value directly — same pattern as dpadActive().
                bool lNeedsPhys = lAct.type == ButtonActionType::Macro    ||
                                  lAct.type == ButtonActionType::Keyboard  ||
                                  lAct.type == ButtonActionType::MouseClick ||
                                  lAct.type == ButtonActionType::Bot;
                float physL = lNeedsPhys ? input->getPhysicalState().triggerL : state.triggerL;
                applyTrigAct(physL, lAct,
                             trigLKbPrev, trigLMousPrev, trigLMacro, trigLMacroOk,
                             trigLBotPrev, state.triggerL);
                if (lAct.type == ButtonActionType::TriggerPassthrough &&
                    lAct.target == "r2" && physL > 0.0f)
                    trigRWasCrossTarget = true;
            }
            if (cfg->triggerRHasAction && cfg->triggerRRanges.empty()) {
                const auto& rAct = cfg->triggerRAction;
                bool rNeedsPhys = rAct.type == ButtonActionType::Macro    ||
                                  rAct.type == ButtonActionType::Keyboard  ||
                                  rAct.type == ButtonActionType::MouseClick ||
                                  rAct.type == ButtonActionType::Bot;
                float physR = rNeedsPhys ? input->getPhysicalState().triggerR : state.triggerR;
                applyTrigAct(physR, rAct,
                             trigRKbPrev, trigRMousPrev, trigRMacro, trigRMacroOk,
                             trigRBotPrev, state.triggerR);
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
                    if (editorOpen) {
                        // Suppressed while the mapping editor is open — bail before touching
                        // `prev` (not after dispatching) so a range entered/left mid-edit is still
                        // caught correctly once the editor closes, same reasoning as
                        // dispatchKeyboard/Mouse/Bot/Macro above. An already-running macro still
                        // ticks though (so it finishes/keeps playing out instead of freezing
                        // mid-sequence), matching every other block — its state mutations just
                        // don't reach output while editorOpen (see output->update() below).
                        if (act.type == ButtonActionType::Macro &&
                            i < rangeMacs.size() && rangeMacOk[i] && rangeMacs[i].isActive())
                            rangeMacs[i].tick(state);
                        continue;
                    }
                    switch (act.type) {
                    case ButtonActionType::VirtualButton:
                        applyVirtualBtnByName(state, act.name, active);
                        break;
                    case ButtonActionType::Keyboard:
                        if (active != (prev != 0)) {
                            sendKeyCombo(act.keys, active);
                            if (active) {
                                std::string combo;
                                for (const auto& k : act.keys) { if (!combo.empty()) combo += '+'; combo += k; }
                                pushEvent({ PadEventType::KeyboardAction, combo, true });
                            }
                        }
                        break;
                    case ButtonActionType::MouseClick:
                        if (active != (prev != 0)) {
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
                    case ButtonActionType::Bot:
                        if (active && !prev) {
                            if (auto* b = botLoader.find(act.name)) {
                                b->toggle();
                                spdlog::info("[BOT] '{}' {}", act.name, b->isActive() ? "ON" : "OFF");
                                pushEvent({ PadEventType::BotToggle, act.name, b->isActive() });
                            } else {
                                spdlog::warn("[BOT] '{}' not loaded.", act.name);
                            }
                        }
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
            if (cfg->touchpad.surfaceMode == TouchpadSurfaceMode::Mouse &&
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
            spdlog::trace("[Virtual] L({:+.3f},{:+.3f}) R({:+.3f},{:+.3f}) LT:{:.3f} RT:{:.3f}",
                          state.leftX, state.leftY, state.rightX, state.rightY,
                          state.triggerL, state.triggerR);
            if (!editorOpen) output->update(state);
        }

        Sleep(8);
    }

        // ── End of run loop ───────────────────────────────────────────────────
        m_connected = false;
        m_deviceHub.close(selected.hidPath);  // done with this device — reconnect/switch opens fresh

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
