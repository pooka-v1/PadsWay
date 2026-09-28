#include "ControllerProtocolRegistry.h"
#include "GenericHidProtocol.h"
#include "../Log.h"

std::unique_ptr<ControllerProtocol> createControllerProtocol(const std::string& name, const HIDDevice& device)
{
    if (!name.empty() && !isKnownControllerProtocol(name))
        spdlog::warn("[Protocol] Unknown protocol '{}' in controllers.json — using {}",
                     name, kGenericHidProtocol);
    return std::make_unique<GenericHidProtocol>(device);
}
