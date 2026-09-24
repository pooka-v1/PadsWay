#pragma once
#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include "ui/MappingModel.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// E2EMapping — drives the Mapeador's data path without its UI: the same MappingModel edits that
// MappingEditor makes on each click, then the same save + engine reload it does on "Guardar".
// The assign* helpers mirror MappingEditor.cpp line for line (see the comment on each), so a
// change in how the Mapeador stores an assignment should be mirrored here too.
//
// Normal mode (controllers.json) only for now; profiles (saveProfile) come in Fase 2.
// ---------------------------------------------------------------------------
namespace E2EMapping {

// Model loaded from the sandbox controllers.json for the fake pad, as when the Mapeador opens.
MappingModel openMapeador();

// MappingEditor::save() in Normal mode: model.save(controllers.json) + engine.reloadConfigs(),
// then waits for the engine to pick the change up.
void saveNormalMode(MappingModel& model);

// Undo: puts the pristine sandbox controllers.json back and reloads the engine.
void restoreBaseline();

// Undoes whatever the test assigned when it leaves scope, even if a REQUIRE aborted it.
struct ScopedAssignment {
    ScopedAssignment() = default;
    ScopedAssignment(const ScopedAssignment&) = delete;
    ScopedAssignment& operator=(const ScopedAssignment&) = delete;
    ~ScopedAssignment() { restoreBaseline(); }
};

// Physical button/dpad source -> virtual button, virtual dpad direction ("dpad_up") or stick
// half-axis slot ("right_x_neg"). MappingEditor::onVirtHitPhysButton (button/dpad target) and
// onVirtHitStickSlot (slot target): drop any action, store the virtual short name.
void assignVirtual(MappingModel& model, const std::string& physShort, const std::string& virtShort);

// Physical button/dpad source -> virtual trigger ("l2"/"r2"). onVirtHitPhysButton, trigger branch.
void assignTrigger(MappingModel& model, const std::string& physShort, const std::string& trigger);

// Physical button/dpad source -> macro/keyboard/mouse/bot action (action panel on the right).
void assignAction(MappingModel& model, const std::string& physShort, ButtonAction action);

// Inline macro, as the Mapeador's macro creator stores it: no name, the DSL right in the entry.
ButtonAction inlineMacroAction(const std::string& execution);
ButtonAction botAction(const std::string& botName);
// `keys` in the Mapeador's capture names ("shift", "k", "f5"...), in the order they were pressed.
ButtonAction keyboardAction(const std::vector<std::string>& keys);
// `button`: "left" "right" "middle" "x1" "x2", as the Mapeador's mouse buttons store it.
ButtonAction mouseClickAction(const std::string& button);

// True if the engine loaded `botName` from the sandbox data/bots/ (tests SKIP otherwise).
bool isBotLoaded(const std::string& botName);

}
