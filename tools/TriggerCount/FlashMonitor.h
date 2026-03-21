#pragma once

#include <atomic>
#include <thread>

// FFX Thunder Plains — passive flash detector.
// Monitors a screen row using the same two-phase logic as LightningBot,
// but does NOT press any button. It simply counts and logs detected flashes
// so that TriggerCount can correlate them with manual A presses.
//
// Usage:
//   FlashMonitor mon;
//   mon.toggle();              // start/stop monitoring
//   int n = mon.flashCount();  // total flashes detected so far
class FlashMonitor {
public:
    FlashMonitor();
    ~FlashMonitor();

    void toggle();
    bool isActive()    const { return m_active.load(); }
    int  flashCount()  const { return m_flashCount.load(); }

private:
    void threadFunc();
    int  sampleBrightness(bool* outIsFlash = nullptr) const;

    std::thread       m_thread;
    std::atomic<bool> m_running   { false };
    std::atomic<bool> m_active    { false };
    std::atomic<int>  m_flashCount{ 0 };
};
