#include "E2EMapping.h"
#include "E2ESandbox.h"
#include "config/ConfigLoader.h"
#include "Paths.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
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

void assignHalfAxis(MappingModel& model, const std::string& axisKey, const HalfAxisAction& action) {
    model.axisActionEdits[axisKey] = action;
}

HalfAxisAction halfAxisToVirtual(const std::string& virtShort) {
    HalfAxisAction ha;
    if (virtShort.rfind("dpad_", 0) == 0) {
        ha.type   = HalfAxisActionType::Dpad;
        ha.target = virtShort.substr(5);   // "up"/"down"/"left"/"right", as onVirtHitAxisAction stores it
    } else if (virtShort.size() > 4 &&
               (virtShort.ends_with("_pos") || virtShort.ends_with("_neg"))) {
        ha.type   = HalfAxisActionType::StickSlot;
        ha.target = virtShort;
    } else {
        ha.type   = HalfAxisActionType::VirtualButton;
        ha.target = virtShort;
    }
    return ha;
}

HalfAxisAction halfAxisToTrigger(const std::string& trigger) {
    HalfAxisAction ha;
    ha.type   = HalfAxisActionType::Trigger;
    ha.target = trigger;
    return ha;
}

HalfAxisAction halfAxisFromAction(const ButtonAction& action) {
    HalfAxisAction ha;
    switch (action.type) {
    case ButtonActionType::Keyboard:
        ha.type = HalfAxisActionType::Keyboard;
        ha.keys = action.keys;
        break;
    case ButtonActionType::MouseClick:
        ha.type        = HalfAxisActionType::MouseClick;
        ha.mouseButton = action.mouseButton;
        break;
    case ButtonActionType::Macro:   // inline modal, Ctx::Axis: no name, DSL in execution
        ha.type      = HalfAxisActionType::Macro;
        ha.execution = action.execution;
        break;
    case ButtonActionType::Bot:     // bot combo: the bot's name goes in target
        ha.type   = HalfAxisActionType::Bot;
        ha.target = action.name;
        break;
    default:
        throw std::logic_error("halfAxisFromAction: no half-axis equivalent for this ButtonAction type");
    }
    return ha;
}

ButtonAction inlineMacroAction(const std::string& execution) {
    ButtonAction act;
    act.type      = ButtonActionType::Macro;
    act.execution = execution;   // name left empty: MappingEditor's inline macro modal does the same
    return act;
}

ButtonAction botAction(const std::string& botName) {
    ButtonAction act;
    act.type = ButtonActionType::Bot;
    act.name = botName;
    return act;
}

ButtonAction keyboardAction(const std::vector<std::string>& keys) {
    ButtonAction act;
    act.type = ButtonActionType::Keyboard;
    act.keys = keys;
    return act;
}

ButtonAction mouseClickAction(const std::string& button) {
    ButtonAction act;
    act.type        = ButtonActionType::MouseClick;
    act.mouseButton = button;
    return act;
}

bool isBotLoaded(const std::string& botName) {
    const auto names = harness().engine().getLoadedBotNames();
    return std::find(names.begin(), names.end(), botName) != names.end();
}

}
