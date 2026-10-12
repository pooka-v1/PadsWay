#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// Finds the bytes of a raw report that behave like a counter (a sensor timestamp, a packet
// sequence) while the pad lies still. Fed with every report of a quiet capture: a resting sensor
// only jitters, stepping up about as often as down, while a counter keeps stepping the same way.
//
// Amplitude alone can't tell them apart: a counter's high byte read as the LOW half of an int16
// moves that int16 by 255 at most, which is a believable sensor noise level (DS4 over BT: offset
// 11 = sensor timestamp high byte + temperature byte, 255 peak-to-peak vs 168 on a real accel).
// Bytes that barely move (battery, temperature, a touch counter while untouched) never reach
// kMinMoves, so they are left to the amplitude checks.
//
// Header-only so the wizard and its tests share it without another .cpp in each project.
class CounterByteDetector {
public:
    void reset() {
        m_previous.clear();
        m_stepsUp.clear();
        m_stepsDown.clear();
    }

    void addReport(const std::vector<uint8_t>& report) {
        if (m_previous.size() != report.size()) {   // first report, or a different report length
            m_previous = report;
            m_stepsUp.assign(report.size(), 0);
            m_stepsDown.assign(report.size(), 0);
            return;
        }
        for (size_t i = 0; i < report.size(); ++i) {
            // Modulo 256, so a counter wrapping 255 -> 0 still reads as one step forward.
            const auto step = static_cast<int8_t>(static_cast<uint8_t>(report[i] - m_previous[i]));
            if (step > 0) ++m_stepsUp[i];
            else if (step < 0) ++m_stepsDown[i];
        }
        m_previous = report;
    }

    bool isCounter(int byteIndex) const {
        if (byteIndex < 0 || byteIndex >= static_cast<int>(m_stepsUp.size())) return false;
        const int moves = m_stepsUp[byteIndex] + m_stepsDown[byteIndex];
        if (moves < kMinMoves) return false;
        // Parenthesized: <windows.h>'s max() macro would expand std::max( (no NOMINMAX here).
        const int sameWay = (std::max)(m_stepsUp[byteIndex], m_stepsDown[byteIndex]);
        return sameWay * 100 >= moves * kSameWayPercent;
    }

    // Steps needed before a byte can be judged: a counter steps on nearly every report, so a
    // Baseline capture (270 frames) gives it far more than this.
    static constexpr int kMinMoves = 30;
    // Share of steps in one direction that marks a counter. Resting jitter sits near 50%; a
    // timestamp read every few reports steps forward every time.
    static constexpr int kSameWayPercent = 90;

private:
    std::vector<uint8_t> m_previous;
    std::vector<int>     m_stepsUp;
    std::vector<int>     m_stepsDown;
};
