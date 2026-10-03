#pragma once
#include "ControllerProtocol.h"
#include "HIDDevice.h"

// The default protocol: whatever the HID descriptor declares, parsed with HidP — how PadsWay has
// always read every pad. Never writes to the device.
//
// Holds the HIDDevice because HidP needs its descriptor (preparsed data, value caps), and parses
// the device's own report buffer: decode() must be given that same buffer (as HIDInputSource does).
class GenericHidProtocol final : public ControllerProtocol {
public:
    explicit GenericHidProtocol(const HIDDevice& device) : m_device(device) {}

    bool decode(const BYTE* /*report*/, ULONG /*len*/, RawHIDState& out) override {
        decodeRawHIDReport(m_device, out);
        return true;
    }

private:
    const HIDDevice& m_device;
};
