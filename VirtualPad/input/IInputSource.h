#pragma once

#include <windows.h>
#include "../GamepadState.h"

// Pure abstract interface that every input source must implement.
//
// Whether the input comes from a real physical pad, an AI agent,
// or a network connection — they all produce a GamepadState.
// The rest of the system only talks to this interface, so adding
// new sources never requires touching the output or macro code.
class IInputSource {
public:
    virtual ~IInputSource() = default;

    // Returns true if this source is connected and ready to read.
    virtual bool isConnected() const = 0;

    // Fills 'state' with the latest input values.
    // Returns true on success, false if the read failed (e.g. pad disconnected).
    virtual bool read(GamepadState& state) = 0;

    // Returns the raw button bitmask from the most recent read() call.
    // Bit N-1 is set if physical button N is pressed. Used for bot/macro toggle detection.
    virtual DWORD getLastButtonMask() const = 0;

    // Human-readable name used for logging and diagnostics.
    virtual const char* getName() const = 0;
};
