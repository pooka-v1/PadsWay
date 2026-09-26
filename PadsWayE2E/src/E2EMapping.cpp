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

// The run loop applies a reload / profile change on its next ~8 ms tick; wait well past that so the
// first press after this already goes through the new mapping (edge-triggered actions included).
void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(E2EHarness::kSettleMs)); }

void reloadEngineAndSettle() {
    harness().engine().reloadConfigs();
    settle();
}

// The controllers.json entry the Perfiles editor diffs a profile against: MappingEditor looks it up
// for the active device with the same findConfig call. Held in a static so the pointer stays valid.
const ControllerConfig* profileBase() {
    static std::vector<ControllerConfig> configs;
    configs = loadControllerConfigs(controllersPath());
    const DeviceCandidate dev = harness().engine().getActiveDevice();
    return findConfig(configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);
}

} // namespace

std::string testProfilePath() {
    return Paths::userData("data/profiles/") + kTestProfileName + ".json";
}

MappingModel openEditor(SaveMode mode) {
    MappingModel model;
    model.vid = E2ESandbox::kFakePadVid;
    model.pid = E2ESandbox::kFakePadPid;
    if (mode == SaveMode::Normal) {
        model.reload(loadControllerConfigs(controllersPath()));
        return model;
    }
    const ControllerConfig* base = profileBase();
    if (!base) throw std::runtime_error("no controllers.json entry for the active device (profile base)");
    model.loadProfile(*base, loadGameProfile(testProfilePath()));   // missing file = empty profile
    return model;
}

bool saveAssignment(MappingModel& model, SaveMode mode, std::string& error) {
    if (mode == SaveMode::Normal) {
        model.save(controllersPath());
        reloadEngineAndSettle();
        return true;
    }
    const ControllerConfig* base = profileBase();
    if (!base) { error = "no controllers.json entry for the active device (profile base)"; return false; }
    if (!model.saveProfile(testProfilePath(), kTestProfileName, *base)) {
        error = "saveProfile() failed for " + testProfilePath() + " - see the engine log";
        return false;
    }
    harness().engine().setProfilePath(testProfilePath());
    harness().engine().requestProfileReload();
    settle();
    if (harness().engine().getActiveProfileName() != kTestProfileName) {
        error = "the engine didn't apply the test profile - see the engine log";
        return false;
    }
    return true;
}

void undoAssignment(SaveMode mode) {
    std::error_code ec;
    if (mode == SaveMode::Normal) {
        std::filesystem::copy_file(E2ESandbox::baselineControllersPath(), controllersPath(),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        reloadEngineAndSettle();
        return;
    }
    harness().engine().setProfilePath("");
    settle();
    std::filesystem::remove(testProfilePath(), ec);
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

void assignMouseMoveAxis(MappingModel& model, const std::string& axisKey, const std::string& mouseAxis,
                         float speed) {
    HalfAxisAction ha;
    ha.type   = HalfAxisActionType::MouseMove;
    ha.target = mouseAxis;
    ha.speed  = speed;
    model.axisActionEdits[axisKey] = ha;
    const bool isPos = axisKey.ends_with("_pos");
    const std::string opposite = axisKey.substr(0, axisKey.size() - 4) + (isPos ? "_neg" : "_pos");
    model.axisActionEdits[opposite] = ha;   // the engine gives each half its own sign (_pos > 0, _neg < 0)
}

void assignTriggerSource(MappingModel& model, const std::string& trigger, ButtonAction action) {
    action.physical = trigger;
    const bool toStickSlot = action.type == ButtonActionType::VirtualButton &&
                             (action.name.ends_with("_pos") || action.name.ends_with("_neg"));
    if (toStickSlot)
        (trigger == "l2" ? model.trigLRangeEdits : model.trigRRangeEdits).clear();
    model.trigActionEdits[trigger] = action;
}

ButtonAction triggerToTrigger(const std::string& targetTrigger) {
    ButtonAction act;
    act.type   = ButtonActionType::TriggerPassthrough;
    act.target = targetTrigger;
    return act;
}

ButtonAction triggerToVirtual(const std::string& virtShort) {
    ButtonAction act;
    act.type = ButtonActionType::VirtualButton;
    act.name = virtShort;   // "b", "dpad_up" and "right_x_neg" alike
    return act;
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
