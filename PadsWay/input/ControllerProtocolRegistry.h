#pragma once
#include "ControllerProtocol.h"
#include <memory>
#include <string>
#include <string_view>

class HIDDevice;

// Protocol names accepted in controllers.json ("protocol" field). One per controller family, never
// per model (ARCHITECTURE.md, "Protocolos de mando" → principios). Add the name here and its
// factory in ControllerProtocolRegistry.cpp together.
inline constexpr std::string_view kGenericHidProtocol = "generic_hid";

inline bool isKnownControllerProtocol(std::string_view name) {
    return name == kGenericHidProtocol;
}

// Builds the protocol named in a controller's config for this device. An unknown name logs a
// warning and falls back to generic_hid — a typo in controllers.json must never leave a pad unread.
std::unique_ptr<ControllerProtocol> createControllerProtocol(const std::string& name, const HIDDevice& device);
