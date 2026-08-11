#pragma once
#include "HIDInputSource.h"
#include "RawHIDReader.h"
#include "ControllerConfig.h"
#include "../GamepadState.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

// Thread-safe snapshot of one open HID connection, shared by every consumer of that path
// regardless of who is actually driving the reads for it (see DeviceHub below).
struct DeviceSnapshot {
    bool         connected = false;
    RawHIDState  raw;      // generic decode (buttons/axes/hat/raw bytes) — config-independent
    GamepadState state;    // decoded output mapped via the connection's ControllerConfig
};

// Owns every HID connection PadsWay currently has open, indexed by device path. A step below
// PadEngine and AppWindow — neither owns it, both consume it as siblings, constructed in
// PadsWay.cpp (main) and passed by reference to both. See ARCHITECTURE.md, "DeviceHub".
//
// Wraps HIDInputSource per connection; does not reimplement HID reading/decoding. A connection's
// bytes get read in one of two ways, both updating the same shared DeviceSnapshot:
//   - openDriven()+read(): the caller drives reads on its own cadence (PadEngine, for the engine's
//     active device) and calls read() once per tick from its own thread.
//   - watch(): DeviceHub runs its own background thread for a path nobody is driving via read()
//     above (the Scanner, watching a device that isn't the engine's active one).
// Either way, any consumer can poll snapshot(path) without needing its own OS handle — this is
// what lets the Scanner inspect the engine's active device instead of racing it for a second,
// conflicting open() (most HID backends only deliver reports to the first opener).
class DeviceHub {
public:
    DeviceHub()  = default;
    ~DeviceHub();

    DeviceHub(const DeviceHub&)            = delete;
    DeviceHub& operator=(const DeviceHub&) = delete;

    // Opens devicePath if not already open (closing/replacing any prior connection for it first —
    // same "fresh handle per connect cycle" behaviour PadEngine relied on before this existed).
    // Never starts a background thread; the caller is expected to drive read() itself.
    void openDriven(const std::string& devicePath, const ControllerConfig& cfg);

    // Synchronous read on a connection opened via openDriven(), called once per engine tick.
    // Forwards to the underlying HIDInputSource::read() and refreshes the shared snapshot.
    // Returns false on disconnect (matches HIDInputSource::read()).
    bool read(const std::string& devicePath, GamepadState& outState);

    // Pass-throughs to the underlying HIDInputSource for the connection's driving consumer
    // (PadEngine). Returns nullptr if devicePath isn't open.
    HIDInputSource* get(const std::string& devicePath) const;

    // Opens devicePath (if needed, with an empty/default config — no mapping, raw decode only)
    // and starts a background reader thread for it. No-op if the path is already open (whether
    // driven or already watched) — the Scanner must not call this for the engine's active device,
    // it should read that device's snapshot() instead.
    void watch(const std::string& devicePath, const std::string& name);
    void unwatch(const std::string& devicePath);

    // Closes and forgets a connection outright (stops its watch thread first, if any).
    void close(const std::string& devicePath);

    DeviceSnapshot snapshot(const std::string& devicePath) const;
    bool           isOpen(const std::string& devicePath) const;

private:
    struct Connection {
        std::unique_ptr<HIDInputSource> input;
        bool                            watching = false; // watch thread owns reads for this connection

        mutable std::mutex m_snapshotMutex; // protects snapshot below
        DeviceSnapshot      snapshot;

        std::thread       watchThread;
        std::atomic<bool> watchStop { false };
    };

    static void refreshSnapshot(Connection& c, const GamepadState& decoded);
    static void watchLoop(Connection* c);
    void        closeLocked(const std::string& devicePath); // assumes m_mapMutex already held

    mutable std::mutex m_mapMutex; // protects m_connections itself
    std::unordered_map<std::string, std::unique_ptr<Connection>> m_connections;
};
