#pragma once

#include <windows.h>
#include <ViGEm/Client.h>
#include "../GamepadState.h"

// Wraps the ViGEmBus driver to expose a virtual Xbox 360 controller to Windows.
//
// On construction it connects to the ViGEmBus driver and plugs in a virtual pad.
// Call update() each frame to push the current GamepadState to the virtual device.
// The destructor cleanly unplugs and releases all resources.
class ViGEmOutputAdapter {
public:
    // vid/pid: custom identity for the virtual pad (used to identify it in WinMM scans).
    explicit ViGEmOutputAdapter(USHORT vid, USHORT pid);
    ~ViGEmOutputAdapter();

    // Returns true if the virtual pad was created and plugged in successfully.
    bool isReady() const;

    // Translates a GamepadState into an XUSB_REPORT and sends it to the virtual pad.
    void update(const GamepadState& state);

private:
    PVIGEM_CLIENT m_client = nullptr;
    PVIGEM_TARGET m_pad    = nullptr;
    bool          m_ready  = false;

    // Converts float [-1.0, 1.0] to SHORT [-32767, 32767] (Xbox stick range).
    static SHORT toShort(float value);

    // Converts float [0.0, 1.0] to BYTE [0, 255] (Xbox trigger range).
    static BYTE  toByte(float value);
};
