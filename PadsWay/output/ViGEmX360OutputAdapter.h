#pragma once

#include "ViGEmOutputBase.h"

// Concrete IOutputSink adapter for the Xbox 360 / XInput output type.
//
// Plugs in a virtual Xbox 360 controller on construction (via the base) and, on
// each update(), translates the GamepadState into an XUSB_REPORT and ships it.
class ViGEmX360OutputAdapter : public ViGEmOutputBase {
public:
    // vid/pid: custom identity for the virtual pad.
    explicit ViGEmX360OutputAdapter(USHORT vid, USHORT pid);

    void update(const GamepadState& state) override;

private:
    // float [-1.0, 1.0] -> SHORT [-32767, 32767] (Xbox stick range).
    static SHORT toShort(float value);
};
