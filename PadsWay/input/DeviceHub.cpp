#include "DeviceHub.h"

// ---------------------------------------------------------------------------

DeviceHub::~DeviceHub() {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    for (auto& [path, c] : m_connections) {
        if (c->watching) {
            c->watchStop = true;
            if (c->watchThread.joinable()) c->watchThread.join();
        }
    }
    m_connections.clear();
}

// ---------------------------------------------------------------------------

void DeviceHub::openDriven(const std::string& devicePath, const ControllerConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    closeLocked(devicePath); // fresh handle per connect cycle, same as the old local unique_ptr

    auto conn = std::make_unique<Connection>();
    conn->input    = std::make_unique<HIDInputSource>(devicePath, cfg);
    conn->watching = false;
    m_connections[devicePath] = std::move(conn);
}

bool DeviceHub::read(const std::string& devicePath, GamepadState& outState) {
    Connection* c = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_mapMutex);
        auto it = m_connections.find(devicePath);
        if (it == m_connections.end() || !it->second->input) return false;
        c = it->second.get();
    }
    // Only PadEngine's own thread drives a connection opened via openDriven(), so calling the
    // blocking read() here without holding m_mapMutex is safe — see DeviceHub.h class comment.
    bool ok = c->input->read(outState);
    refreshSnapshot(*c, outState);
    return ok;
}

HIDInputSource* DeviceHub::get(const std::string& devicePath) const {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto it = m_connections.find(devicePath);
    return (it != m_connections.end()) ? it->second->input.get() : nullptr;
}

// ---------------------------------------------------------------------------

void DeviceHub::watch(const std::string& devicePath, const std::string& name) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    if (m_connections.count(devicePath)) return; // already open (driven or watched) — no-op

    ControllerConfig rawCfg;   // no mapping — HIDInputSource's generic snapshot doesn't need one
    rawCfg.source_name = name;

    auto conn = std::make_unique<Connection>();
    conn->input    = std::make_unique<HIDInputSource>(devicePath, rawCfg);
    conn->watching = true;
    Connection* raw = conn.get();
    m_connections[devicePath] = std::move(conn);

    raw->watchStop     = false;
    raw->watchThread = std::thread([raw] { watchLoop(raw); });
}

void DeviceHub::unwatch(const std::string& devicePath) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto it = m_connections.find(devicePath);
    if (it == m_connections.end() || !it->second->watching) return;
    closeLocked(devicePath);
}

void DeviceHub::watchLoop(Connection* c) {
    GamepadState scratch;
    while (!c->watchStop.load()) {
        if (!c->input->read(scratch)) {
            std::lock_guard<std::mutex> lk(c->m_snapshotMutex);
            c->snapshot.connected = false;
            break; // disconnected — caller notices via isOpen()/snapshot() and calls unwatch()
        }
        refreshSnapshot(*c, scratch);
    }
}

// ---------------------------------------------------------------------------

void DeviceHub::close(const std::string& devicePath) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    closeLocked(devicePath);
}

void DeviceHub::closeLocked(const std::string& devicePath) {
    auto it = m_connections.find(devicePath);
    if (it == m_connections.end()) return;
    Connection& c = *it->second;
    if (c.watching) {
        c.watchStop = true;
        if (c.watchThread.joinable()) c.watchThread.join();
    }
    m_connections.erase(it);
}

// ---------------------------------------------------------------------------

DeviceSnapshot DeviceHub::snapshot(const std::string& devicePath) const {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto it = m_connections.find(devicePath);
    if (it == m_connections.end()) return {};
    Connection& c = *it->second;
    std::lock_guard<std::mutex> lk2(c.m_snapshotMutex);
    return c.snapshot;
}

bool DeviceHub::isOpen(const std::string& devicePath) const {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto it = m_connections.find(devicePath);
    return it != m_connections.end() && it->second->input && it->second->input->isConnected();
}

// ---------------------------------------------------------------------------

void DeviceHub::refreshSnapshot(Connection& c, const GamepadState& decoded) {
    std::lock_guard<std::mutex> lk(c.m_snapshotMutex);
    c.snapshot.connected = c.input->isConnected();
    c.snapshot.raw       = c.input->getLastRawSnapshot();
    c.snapshot.state     = decoded;
}
