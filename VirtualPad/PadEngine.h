#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>
#include "PadScanner.h"
#include "GamepadState.h"

// Unified description of a physical input device, regardless of API.
// Built during the scan phase; used to create the right IInputSource.
struct DeviceCandidate {
    enum class Source { WinMM, HID };

    Source      source  = Source::WinMM;
    UINT        port    = UINT_MAX;   // WinMM only
    std::string hidPath;              // HID only
    WORD        vid     = 0;
    WORD        pid     = 0;
    std::string name;
    UINT        axes    = 0;
    UINT        buttons = 0;
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

    // Phase / device selection (A3)
    EnginePhase getPhase()       const { return m_phase.load(); }
    uint16_t    getVirtualVid()  const { return m_virtualVid.load(); }
    uint16_t    getVirtualPid()  const { return m_virtualPid.load(); }
    std::vector<DeviceCandidate> getCandidates() const;
    void        selectDevice(int index);   // call from UI during WaitingSelection
    GamepadState getLastState() const;

private:
    void threadFunc();

    std::thread        m_thread;
    std::atomic<bool>  m_running   { false };
    std::atomic<bool>  m_connected { false };

    mutable std::mutex m_mutex;
    std::string        m_device;   // name of the active input device
    std::string        m_status;   // one-line human-readable status

    // A3/A4 additions
    std::atomic<EnginePhase> m_phase         { EnginePhase::Idle };
    std::atomic<int>         m_selectedIndex { -1 };      // index into m_candidates
    std::atomic<uint16_t>    m_virtualVid    { 0 };
    std::atomic<uint16_t>    m_virtualPid    { 0 };
    std::vector<DeviceCandidate> m_candidates;   // protected by m_mutex
    GamepadState                 m_lastState;    // protected by m_mutex

    void setDevice(const std::string& s);
    void setStatus(const std::string& s);
};
