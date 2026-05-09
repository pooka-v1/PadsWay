#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <string>
#include <vector>
#include <windows.h>
#include "GamepadState.h"
#include "input/ControllerConfig.h"
#include "output/HidHideClient.h"

// ---------------------------------------------------------------------------
// Engine events — domain facts emitted by the engine, interpreted by the UI.
// ---------------------------------------------------------------------------

enum class PadEventType { BotToggle, MacroToggle, KeyboardAction, MouseAction };

struct PadEvent {
    PadEventType type;
    std::string  name;    // bot or macro name
    bool         active;  // true = ON / started, false = OFF / stopped
};

// ---------------------------------------------------------------------------
// Unified description of a physical HID input device.
// Built during the scan phase; used to create an HIDInputSource.
struct DeviceCandidate {
    std::string hidPath;
    WORD        vid            = 0;
    WORD        pid            = 0;
    std::string name;
    std::string connectionType;  // "usb" / "bt" / ""
};

enum class EnginePhase {
    Idle,
    Scanning,           // looking for devices
    WaitingSelection,   // multiple devices found, waiting for UI to pick one
    Configuring,        // loading config + ViGEm init
    Running,            // main input loop active
    Stopped,
    Error,
};

// Runs the gamepad read → macro → ViGEm pipeline on a background thread.
// The main (UI) thread reads engine status via the thread-safe accessors below.
class PadEngine {
public:
    PadEngine();
    ~PadEngine();

    void start();   // spawn background thread
    void stop();    // signal thread to stop and wait for it to finish

    // Thread-safe status accessors (called from the UI thread)
    bool        isRunning()   const { return m_running.load(); }
    bool        isConnected() const { return m_connected.load(); }
    std::string getDevice()   const;
    std::string getStatus()   const;

    // Phase / device selection
    EnginePhase getPhase()       const { return m_phase.load(); }
    uint16_t    getVirtualVid()  const { return m_virtualVid.load(); }
    uint16_t    getVirtualPid()  const { return m_virtualPid.load(); }
    std::vector<DeviceCandidate> getCandidates()       const;  // for WaitingSelection (startup)
    std::vector<DeviceCandidate> getAvailableDevices() const;  // live list from monitor thread
    void        selectDevice(int index);    // call from UI during WaitingSelection
    void        requestSwitch(int index);   // switch to available device[index] while Running
    DeviceCandidate getActiveDevice() const;
    GamepadState          getLastState()        const;
    GamepadState          getLastVirtualState() const;  // state actually sent to ViGEm (post-bot/macro)
    std::vector<PadEvent> pollEvents();                 // drain the event queue (UI calls each frame)

    // Game profile — set from UI thread; applied at next Configuring phase
    void        setProfilePath(const std::string& path);
    std::string getProfilePath()       const;
    std::string getActiveProfileName() const;
    std::string getActiveLayoutId()    const;

    // Mouse speed (pixels/tick at full stick deflection) — set from UI slider
    void  setMouseSpeed(float s);
    float getMouseSpeed() const;

    // Reload controllers.json from disk (call after wizard saves a new entry).
    void reloadConfigs();

private:
    void threadFunc();
    void monitorFunc();  // background thread: keeps m_availableDevices updated

    std::thread        m_thread;
    std::thread        m_monitorThread;
    std::atomic<bool>  m_running       { false };
    std::atomic<bool>  m_connected     { false };
    std::atomic<bool>  m_configsDirty  { false };  // set by reloadConfigs(); picked up by run loop

    mutable std::mutex m_mutex;
    std::string        m_device;   // name of the active input device
    std::string        m_status;   // one-line human-readable status

    std::atomic<EnginePhase> m_phase         { EnginePhase::Idle };
    std::atomic<int>         m_selectedIndex { -1 };      // index into m_candidates (WaitingSelection)
    std::atomic<uint16_t>    m_virtualVid    { 0 };
    std::atomic<uint16_t>    m_virtualPid    { 0 };
    std::vector<DeviceCandidate>  m_candidates;       // startup multi-device selection; protected by m_mutex
    std::vector<DeviceCandidate>  m_availableDevices; // live list from monitor; protected by m_mutex
    std::vector<ControllerConfig> m_configs;          // shared with monitor thread; protected by m_mutex
    DeviceCandidate               m_activeDevice;     // currently active physical device; protected by m_mutex
    GamepadState                  m_lastState;         // protected by m_mutex
    GamepadState                  m_lastVirtualState;  // post-bot/macro, sent to ViGEm; protected by m_mutex
    std::deque<PadEvent>          m_eventQueue;        // max 16 entries; protected by m_mutex
    std::string                   m_profilePath;      // protected by m_mutex
    std::string                   m_activeProfileName; // protected by m_mutex
    std::string                   m_activeLayoutId;    // protected by m_mutex
    float                         m_mouseSpeed = 15.0f; // protected by m_mutex
    HidHideClient                 m_hidHide;

    // Switch-in-hot: set by requestSwitch(), consumed by threadFunc()
    std::atomic<bool>  m_switchPending  { false };
    DeviceCandidate    m_switchTarget;  // protected by m_mutex

    void setDevice(const std::string& s);
    void setStatus(const std::string& s);
    void pushEvent(PadEvent e);              // called from threadFunc (acquires m_mutex)
};
