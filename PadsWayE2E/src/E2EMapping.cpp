#include "E2EMapping.h"
#include "E2ESandbox.h"
#include "config/ConfigLoader.h"
#include "Paths.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

namespace E2EMapping {

namespace {

std::string controllersPath() { return Paths::userData("data/controllers.json"); }

void reloadEngineAndSettle() {
    harness().engine().reloadConfigs();
    // The run loop applies the reload on its next ~8 ms tick; wait well past that so the first
    // press after this already goes through the new mapping (edge-triggered actions included).
    std::this_thread::sleep_for(std::chrono::milliseconds(E2EHarness::kSettleMs));
}

} // namespace

MappingModel openMapeador() {
    MappingModel model;
    model.vid = E2ESandbox::kFakePadVid;
    model.pid = E2ESandbox::kFakePadPid;
    model.reload(loadControllerConfigs(controllersPath()));
    return model;
}

void saveNormalMode(MappingModel& model) {
    model.save(controllersPath());
    reloadEngineAndSettle();
}

void restoreBaseline() {
    std::error_code ec;
    std::filesystem::copy_file(E2ESandbox::baselineControllersPath(), controllersPath(),
                               std::filesystem::copy_options::overwrite_existing, ec);
    reloadEngineAndSettle();
}

void assignVirtual(MappingModel& model, const std::string& physShort, const std::string& virtShort) {
    model.actionEdits.erase(physShort);
    model.buttonEdits[physShort] = virtShort;
}

void assignTrigger(MappingModel& model, const std::string& physShort, const std::string& trigger) {
    ButtonAction act;
    act.type     = ButtonActionType::Trigger;
    act.physical = physShort;
    act.target   = trigger;
    model.actionEdits[physShort] = act;
    model.buttonEdits.erase(physShort);
}

void assignAction(MappingModel& model, const std::string& physShort, ButtonAction action) {
    action.physical = physShort;
    model.actionEdits[physShort] = action;
    model.buttonEdits.erase(physShort);
}

ButtonAction macroAction(const std::string& macroName) {
    ButtonAction act;
    act.type = ButtonActionType::Macro;
    act.name = macroName;   // execution left empty: resolved from macros.json by name
    return act;
}

ButtonAction botAction(const std::string& botName) {
    ButtonAction act;
    act.type = ButtonActionType::Bot;
    act.name = botName;
    return act;
}

bool isBotLoaded(const std::string& botName) {
    const auto names = harness().engine().getLoadedBotNames();
    return std::find(names.begin(), names.end(), botName) != names.end();
}

}
