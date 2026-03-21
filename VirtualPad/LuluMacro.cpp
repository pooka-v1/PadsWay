#include "LuluMacro.h"
#include <cmath>

void LuluMacro::toggle() {
    m_active = !m_active;
    if (m_active) {
        m_startTime = GetTickCount64();
    } else {
        m_angle = 0.0f;
    }
}

bool LuluMacro::tick(float& rightX, float& rightY) {
    if (!m_active) return false;

    // Auto-off after time limit
    if (GetTickCount64() - m_startTime >= AUTO_OFF_MS) {
        m_active = false;
        m_angle  = 0.0f;
        return false;
    }

    rightX   = std::cos(m_angle);
    rightY   = std::sin(m_angle);
    m_angle += ANGLE_STEP;
    if (m_angle >= TWO_PI) m_angle -= TWO_PI;

    return true;
}
