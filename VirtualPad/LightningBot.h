#pragma once

#include <atomic>
#include <thread>

// FFX Thunder Plains lightning dodge bot.
// Monitors a screen region using a two-phase state machine:
//   1. Detects the lavender warning flash (brightness > FLASH_THR)
//   2. Waits for the screen to recover (brightness < RECOVERY_THR) → bolt striking → press A
//
// Usage:
//   LightningBot bot;
//   bot.toggle();               // start/stop monitoring
//   if (bot.consumePressA())    // call each frame; returns true once per dodge
//       state.btnA = true;
class LightningBot {
public:
    LightningBot();
    ~LightningBot();

    void toggle();
    bool isActive()   const { return m_active.load(); }
    int  dodgeCount() const { return m_dodgeCount.load(); }

    // Returns true while the bot wants A held (decrements internal counter each call).
    bool consumePressA();

private:
    void threadFunc();
    int  sampleBrightness(bool* outIsFlash = nullptr) const;

    std::thread       m_thread;
    std::atomic<bool> m_running    { false };
    std::atomic<bool> m_active     { false };
    std::atomic<int>  m_pressFrames{ 0 };     // frames remaining to hold A (0 = released)
    std::atomic<int>  m_dodgeCount { 0 };
};
