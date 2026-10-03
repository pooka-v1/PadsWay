#include "ControllerProtocolRegistry.h"
#include "GenericHidProtocol.h"
#include "Ds4Protocol.h"
#include "DualSenseProtocol.h"
#include "../Log.h"

std::unique_ptr<ControllerProtocol> createControllerProtocol(const std::string& configured,
                                                             const HIDDevice& device)
{
    const std::string_view name = resolveControllerProtocol(configured, device.vendorId(),
                                                            device.productId());
    if (!configured.empty() && !isKnownControllerProtocol(configured))
        spdlog::warn("[Protocol] Unknown protocol '{}' in controllers.json — using {}", configured, name);

    if (name == kDualShock4Protocol) {
        spdlog::info("[Protocol] {:04X}:{:04X} -> {}", device.vendorId(), device.productId(), name);
        return std::make_unique<Ds4Protocol>(std::make_unique<GenericHidProtocol>(device));
    }
    if (name == kDualSenseProtocol) {
        spdlog::info("[Protocol] {:04X}:{:04X} -> {}", device.vendorId(), device.productId(), name);
        return std::make_unique<DualSenseProtocol>(std::make_unique<GenericHidProtocol>(device));
    }
    return std::make_unique<GenericHidProtocol>(device);
}

bool enableFullModeWithRetries(ControllerProtocol& protocol, HIDDevice& device, const std::string& name)
{
    for (int attempt = 1; attempt <= kFullModeAttempts; ++attempt) {
        if (protocol.enableFullMode(device, device.transport())) {
            if (attempt > 1)
                spdlog::info("[Protocol][{}] Full mode enabled on attempt {}", name, attempt);
            return true;
        }
        if (!device.isConnected()) return false;   // the next read() reports the disconnect
    }
    spdlog::warn("[Protocol][{}] Full mode not confirmed after {} attempts — staying in basic mode",
                 name, kFullModeAttempts);
    return false;
}
