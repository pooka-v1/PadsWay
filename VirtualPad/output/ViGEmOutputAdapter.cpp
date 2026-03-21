#include "ViGEmOutputAdapter.h"
#include <iostream>
#include <algorithm>

ViGEmOutputAdapter::ViGEmOutputAdapter() {
    // 1. Allocate a client handle (represents our connection to the driver)
    m_client = vigem_alloc();
    if (!m_client) {
        std::cerr << "[ViGEm] Failed to allocate client handle.\n";
        return;
    }

    // 2. Connect to the ViGEmBus driver (must be installed on the system)
    VIGEM_ERROR err = vigem_connect(m_client);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "[ViGEm] Could not connect to driver (error 0x"
                  << std::hex << err << std::dec << ").\n"
                  << "        Is ViGEmBus installed and running?\n";
        vigem_free(m_client);
        m_client = nullptr;
        return;
    }

    // 3. Allocate a virtual Xbox 360 target device
    m_pad = vigem_target_x360_alloc();

    // 4. Plug it in — Windows will now see it as a connected Xbox 360 controller
    err = vigem_target_add(m_client, m_pad);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "[ViGEm] Failed to plug in virtual pad (error 0x"
                  << std::hex << err << std::dec << ").\n";
        vigem_target_free(m_pad);
        m_pad = nullptr;
        vigem_disconnect(m_client);
        vigem_free(m_client);
        m_client = nullptr;
        return;
    }

    m_ready = true;
    std::cout << "[ViGEm] Virtual Xbox 360 controller plugged in.\n";
}

ViGEmOutputAdapter::~ViGEmOutputAdapter() {
    if (m_pad) {
        vigem_target_remove(m_client, m_pad);
        vigem_target_free(m_pad);
    }
    if (m_client) {
        vigem_disconnect(m_client);
        vigem_free(m_client);
    }
}

bool ViGEmOutputAdapter::isReady() const {
    return m_ready;
}

void ViGEmOutputAdapter::update(const GamepadState& state) {
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

SHORT ViGEmOutputAdapter::toShort(float value) {
    float clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<SHORT>(clamped * 32767.0f);
}

BYTE ViGEmOutputAdapter::toByte(float value) {
    float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<BYTE>(clamped * 255.0f);
}
