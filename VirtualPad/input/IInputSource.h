#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "../GamepadState.h"
#include "ControllerConfig.h"
#include "ComponentTypes.h"

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

    // Replaces the button/axis mapping config without reopening the device.
    // Called when the user switches game profiles at runtime.
    virtual void setConfig(const ControllerConfig& cfg) = 0;

    // Injects the component-system controller model. Default no-op for sources not yet migrated.
    virtual void setPhysicalController(const PhysicalController& ctrl) {}

    // Returns the physical button state (action.physical names) from the last read().
    // Used by the UI to display the physical pad independently of the virtual remapping.
    virtual GamepadState getPhysicalState() const { return GamepadState{}; }

    // Returns the axis_action keys that are currently active (threshold exceeded) after the
    // last read(). Used by PadEngine for Macro/Keyboard/Mouse edge-detection on axis directions.
    virtual std::vector<std::string> getActiveAxisActions() const { return {}; }

    // Returns the active ButtonAction for axis_action keys whose type is Ranges.
    // Maps axis key → the range's action that is currently firing (Keyboard/MouseClick/Macro).
    // Empty for keys not of Ranges type or when no range is active this frame.
    virtual const std::unordered_map<std::string, ButtonAction>& getActiveAxisRangeActions() const {
        static const std::unordered_map<std::string, ButtonAction> empty;
        return empty;
    }
};
