#include "ViGEmOutputAdapter.h"
#include "../Log.h"
#include <algorithm>

ViGEmOutputAdapter::ViGEmOutputAdapter(USHORT vid, USHORT pid) {
    // 1. Allocate a client handle (represents our connection to the driver)
    m_client = vigem_alloc();
    if (!m_client) {
        spdlog::error("[ViGEm] Failed to allocate client handle.");
        return;
    }

    // 2. Connect to the ViGEmBus driver (must be installed on the system)
    VIGEM_ERROR err = vigem_connect(m_client);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::error("[ViGEm] Could not connect to driver (error 0x{:08X}). Is ViGEmBus installed?",
                      static_cast<unsigned>(err));
        vigem_free(m_client);
        m_client = nullptr;
        return;
    }

    // 3. Allocate a virtual Xbox 360 target device and assign custom identity
    m_pad = vigem_target_x360_alloc();
    vigem_target_set_vid(m_pad, vid);
    vigem_target_set_pid(m_pad, pid);

    // 4. Plug it in — Windows will now see it as a connected Xbox 360 controller
    err = vigem_target_add(m_client, m_pad);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::error("[ViGEm] Failed to plug in virtual pad (error 0x{:08X}).",
                      static_cast<unsigned>(err));
        vigem_target_free(m_pad);
        m_pad = nullptr;
        vigem_disconnect(m_client);
        vigem_free(m_client);
        m_client = nullptr;
        return;
    }

    m_ready = true;
    spdlog::info("[ViGEm] Virtual Xbox 360 controller plugged in.");
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
