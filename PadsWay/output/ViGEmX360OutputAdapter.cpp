#include "ViGEmX360OutputAdapter.h"
#include "../Log.h"
#include <algorithm>

ViGEmX360OutputAdapter::ViGEmX360OutputAdapter(USHORT vid, USHORT pid) {
    if (!m_client) return;   // base failed to connect to ViGEmBus
    if (plugIn(vigem_target_x360_alloc(), vid, pid))
        spdlog::info("[ViGEm] Virtual Xbox 360 controller plugged in.");
}

void ViGEmX360OutputAdapter::update(const GamepadState& state) {
    if (!m_ready) return;

    XUSB_REPORT report = {};    // Zero-initialize all fields
    XUSB_REPORT_INIT(&report);

    // --- Build button bitmask ---
    // Each Xbox button is a bit in a 16-bit word. We OR in the bits that are active.
    auto setBtn = [&](WORD mask, bool pressed) {
        if (pressed) report.wButtons |= mask;
    };

    setBtn(XUSB_GAMEPAD_A,              state.btnA);
    setBtn(XUSB_GAMEPAD_B,              state.btnB);
    setBtn(XUSB_GAMEPAD_X,              state.btnX);
    setBtn(XUSB_GAMEPAD_Y,              state.btnY);
    setBtn(XUSB_GAMEPAD_LEFT_SHOULDER,  state.btnLB);
    setBtn(XUSB_GAMEPAD_RIGHT_SHOULDER, state.btnRB);
    setBtn(XUSB_GAMEPAD_START,          state.btnStart);
    setBtn(XUSB_GAMEPAD_BACK,           state.btnBack);
    setBtn(XUSB_GAMEPAD_GUIDE,          state.btnHome);
    setBtn(XUSB_GAMEPAD_LEFT_THUMB,     state.btnL3);
    setBtn(XUSB_GAMEPAD_RIGHT_THUMB,    state.btnR3);
    setBtn(XUSB_GAMEPAD_DPAD_UP,        state.dpadUp);
    setBtn(XUSB_GAMEPAD_DPAD_DOWN,      state.dpadDown);
    setBtn(XUSB_GAMEPAD_DPAD_LEFT,      state.dpadLeft);
    setBtn(XUSB_GAMEPAD_DPAD_RIGHT,     state.dpadRight);

    // --- Triggers: float [0.0, 1.0] → BYTE [0, 255] ---
    report.bLeftTrigger  = toByte(state.triggerL);
    report.bRightTrigger = toByte(state.triggerR);

    // --- Sticks: float [-1.0, 1.0] → SHORT [-32767, 32767] ---
    report.sThumbLX = toShort(state.leftX);
    report.sThumbLY = toShort(state.leftY);
    report.sThumbRX = toShort(state.rightX);
    report.sThumbRY = toShort(state.rightY);

    vigem_target_x360_update(m_client, m_pad, report);
}

SHORT ViGEmX360OutputAdapter::toShort(float value) {
    float clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<SHORT>(clamped * 32767.0f);
}
