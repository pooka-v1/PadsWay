#pragma once

#include <windows.h>
#include <ViGEm/Client.h>
#include "../GamepadState.h"
#include "IOutputSink.h"

// Shared base for the ViGEm-backed output adapters (Xbox 360 and DualShock 4).
//
// It owns everything the two concrete adapters have in common: the connection to
// the ViGEmBus driver and the virtual target's lifecycle (plug in via plugIn() in
// the concrete constructor, unplug + release in this destructor). The part that
// differs — turning a GamepadState into the device-specific HID report — stays
// pure virtual in update(), so each adapter only writes its own translation.
//
// This is implementation sharing INSIDE the adapter layer; the engine still only
// ever sees the IOutputSink port, never this base. The class is abstract (update()
// is never defined here), so it cannot be instantiated on its own.
class ViGEmOutputBase : public IOutputSink {
public:
    // Connects to ViGEmBus. On failure m_client stays null and isReady() is false;
    // the concrete adapter must check before trying to plug a target in.
    ViGEmOutputBase();
    ~ViGEmOutputBase() override;

    bool isReady() const override { return m_ready; }
    // update() remains pure virtual (inherited from IOutputSink).

protected:
    // Stamps the custom VID/PID on an already-allocated target (x360 or ds4) and
    // plugs it in. Takes ownership: on success it is stored in m_pad and freed by
    // the destructor; on failure it is freed here and false is returned.
    bool plugIn(PVIGEM_TARGET target, USHORT vid, USHORT pid);

    // float [0.0, 1.0] -> BYTE [0, 255]. Trigger range; identical on both devices.
    static BYTE toByte(float value);

    PVIGEM_CLIENT m_client = nullptr;
    PVIGEM_TARGET m_pad    = nullptr;
    bool          m_ready  = false;
};
