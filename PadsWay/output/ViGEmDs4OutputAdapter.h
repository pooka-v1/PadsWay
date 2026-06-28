#pragma once

#include "ViGEmOutputBase.h"

// Concrete IOutputSink adapter for the DualShock 4 / DirectInput output type.
//
// Plugs in a virtual DS4 on construction (via the base) and, on each update(),
// translates the engine's always-Xbox-shaped GamepadState into a DS4_REPORT.
// This is the Adapter that lets old games / emulators (which read DirectInput)
// and >4-pad setups see our virtual pad. Buttons are remapped by physical
// position: A->Cross, B->Circle, X->Square, Y->Triangle.
class ViGEmDs4OutputAdapter : public ViGEmOutputBase {
public:
    // vid/pid: custom identity for the virtual pad.
    explicit ViGEmDs4OutputAdapter(USHORT vid, USHORT pid);

    void update(const GamepadState& state) override;
};
