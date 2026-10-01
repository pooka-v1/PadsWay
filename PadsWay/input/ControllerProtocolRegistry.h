#pragma once
#include "ControllerProtocol.h"
#include <memory>
#include <string>
#include <string_view>

class HIDDevice;

// Protocol names accepted in controllers.json ("protocol" field). One per controller family, never
// per model (ARCHITECTURE.md, "Protocolos de mando" → principios). Add the name here, its hardware
// IDs in protocolForHardware() and its factory in ControllerProtocolRegistry.cpp together.
inline constexpr std::string_view kGenericHidProtocol = "generic_hid";
inline constexpr std::string_view kSonyDs4Protocol    = "sony_ds4";

inline bool isKnownControllerProtocol(std::string_view name) {
    return name == kGenericHidProtocol || name == kSonyDs4Protocol;
}

// The family a pad belongs to by its own hardware identity — a fact of the hardware, not a
// preference: the wizard and the Scanner open pads that have no config yet and still need it.
inline std::string_view protocolForHardware(USHORT vid, USHORT pid) {
    constexpr USHORT kSony = 0x054C;
    if (vid == kSony && (pid == 0x05C4      // DS4 v1
                      || pid == 0x09CC      // DS4 v2
                      || pid == 0x0BA0))    // DS4 USB wireless adapter (USB layout, nothing to do)
        return kSonyDs4Protocol;
    return kGenericHidProtocol;
}

// Protocol a pad gets: the config's "protocol" when it names a known one (an override, e.g.
// "generic_hid" to read a DS4 the old way), else its hardware family. Empty or unknown = by hardware.
inline std::string_view resolveControllerProtocol(std::string_view configured, USHORT vid, USHORT pid) {
    if (isKnownControllerProtocol(configured)) return configured;
    return protocolForHardware(vid, pid);
}

// Builds the protocol for this device (resolveControllerProtocol with the device's own VID/PID).
// An unknown configured name logs a warning — a typo in controllers.json must never leave a pad
// unread, so it falls back to the hardware's family.
std::unique_ptr<ControllerProtocol> createControllerProtocol(const std::string& configured,
                                                             const HIDDevice& device);
