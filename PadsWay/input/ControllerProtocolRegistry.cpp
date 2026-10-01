#include "ControllerProtocolRegistry.h"
#include "GenericHidProtocol.h"
#include "Ds4Protocol.h"
#include "../Log.h"

std::unique_ptr<ControllerProtocol> createControllerProtocol(const std::string& configured,
                                                             const HIDDevice& device)
{
    const std::string_view name = resolveControllerProtocol(configured, device.vendorId(),
                                                            device.productId());
    if (!configured.empty() && !isKnownControllerProtocol(configured))
        spdlog::warn("[Protocol] Unknown protocol '{}' in controllers.json — using {}", configured, name);

    if (name == kSonyDs4Protocol) {
        spdlog::info("[Protocol] {:04X}:{:04X} -> {}", device.vendorId(), device.productId(), name);
        return std::make_unique<Ds4Protocol>(std::make_unique<GenericHidProtocol>(device));
    }
    return std::make_unique<GenericHidProtocol>(device);
}
