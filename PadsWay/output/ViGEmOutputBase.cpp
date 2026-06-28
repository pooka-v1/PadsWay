#include "ViGEmOutputBase.h"
#include "../Log.h"
#include <algorithm>

ViGEmOutputBase::ViGEmOutputBase() {
    // 1. Allocate a client handle (represents our connection to the driver).
    m_client = vigem_alloc();
    if (!m_client) {
        spdlog::error("[ViGEm] Failed to allocate client handle.");
        return;
    }

    // 2. Connect to the ViGEmBus driver (must be installed on the system).
    VIGEM_ERROR err = vigem_connect(m_client);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::error("[ViGEm] Could not connect to driver (error 0x{:08X}). Is ViGEmBus installed?",
                      static_cast<unsigned>(err));
        vigem_free(m_client);
        m_client = nullptr;
    }
}

ViGEmOutputBase::~ViGEmOutputBase() {
    if (m_pad) {
        vigem_target_remove(m_client, m_pad);
        vigem_target_free(m_pad);
    }
    if (m_client) {
        vigem_disconnect(m_client);
        vigem_free(m_client);
    }
}

bool ViGEmOutputBase::plugIn(PVIGEM_TARGET target, USHORT vid, USHORT pid) {
    if (!target) {
        spdlog::error("[ViGEm] Failed to allocate virtual target.");
        return false;
    }

    // Custom identity for the virtual pad (used to identify it in device scans).
    vigem_target_set_vid(target, vid);
    vigem_target_set_pid(target, pid);

    VIGEM_ERROR err = vigem_target_add(m_client, target);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::error("[ViGEm] Failed to plug in virtual pad (error 0x{:08X}).",
                      static_cast<unsigned>(err));
        vigem_target_free(target);
        return false;
    }

    m_pad   = target;   // now owned by us; freed in the destructor
    m_ready = true;
    return true;
}

BYTE ViGEmOutputBase::toByte(float value) {
    float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<BYTE>(clamped * 255.0f);
}
