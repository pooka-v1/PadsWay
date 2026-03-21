#pragma once
#include <windows.h>

// Lulu Overdrive macro: spins the right analog stick in continuous circles.
// No thread needed — call tick() once per main loop frame.
// Auto-stops after AUTO_OFF_MS milliseconds.
class LuluMacro {
public:
    void toggle();
    bool isActive() const { return m_active; }

    // Advances the angle and writes the new stick values.
    // Returns true if the macro is active and values were written.
    // Auto-stops when the time limit is reached (isActive() becomes false).
    bool tick(float& rightX, float& rightY);

private:
    bool       m_active    = false;
    float      m_angle     = 0.0f;      // current angle in radians
    ULONGLONG  m_startTime = 0;         // GetTickCount64() at activation

    // ~4 full rotations per second at the 8ms main-loop poll rate.
    static constexpr float     ANGLE_STEP  = 0.201f;
    static constexpr float     TWO_PI      = 6.2831853f;
    static constexpr ULONGLONG AUTO_OFF_MS = 10000;
};
