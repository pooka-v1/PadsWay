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

// Physical stick half-axis source ("left_x_pos": the physical left stick pushed right) -> any
// target, as one HalfAxisAction keyed by that half in axisActionEdits. MappingEditor::
// onVirtHitAxisAction (virtual button/trigger/dpad/stick arrow) and the axis row of the action
// panel (macro/keyboard/mouse/bot) both end in `axisActionEdits[axisKey] = ha`.
void assignHalfAxis(MappingModel& model, const std::string& axisKey, const HalfAxisAction& action);
// The HalfAxisAction each of those stores:
// virtual button ("b") -> VirtualButton, dpad direction ("dpad_up") -> Dpad with the bare
// direction ("up"), stick half-axis slot ("right_x_neg") -> StickSlot.
HalfAxisAction halfAxisToVirtual(const std::string& virtShort);
HalfAxisAction halfAxisToTrigger(const std::string& trigger);   // "l2"/"r2"
// Keyboard / MouseClick / inline Macro / Bot, from the same ButtonAction the button path uses.
HalfAxisAction halfAxisFromAction(const ButtonAction& action);

// Stick half-axis -> mouse movement on `mouseAxis` ("mouse_x"/"mouse_y"). The Mapeador's
// "Asignar" in the MouseMove row stores the same action on BOTH halves of the axis, so the whole
// axis drives the cursor both ways; this does the same.
void assignMouseMoveAxis(MappingModel& model, const std::string& axisKey, const std::string& mouseAxis,
                         float speed);

// Physical trigger source ("l2"/"r2") -> any target, as one ButtonAction in trigActionEdits:
//   other trigger ("r2")                  -> TriggerPassthrough (onVirtHitTriggerSrc)
//   button / dpad dir / stick half-axis   -> VirtualButton named after it (onVirtHitTriggerSrc,
//                                            onVirtArrowHit — the latter also drops that trigger's
//                                            range edits, mirrored here)
//   keyboard / mouse / inline macro / bot -> that ButtonAction (action panel, trigger row)
void assignTriggerSource(MappingModel& model, const std::string& trigger, ButtonAction action);
ButtonAction triggerToTrigger(const std::string& targetTrigger);
ButtonAction triggerToVirtual(const std::string& virtShort);

// Inline macro, as the Mapeador's macro creator stores it: no name, the DSL right in the entry.
ButtonAction inlineMacroAction(const std::string& execution);
ButtonAction botAction(const std::string& botName);
// `keys` in the Mapeador's capture names ("shift", "k", "f5"...), in the order they were pressed.
ButtonAction keyboardAction(const std::vector<std::string>& keys);
// `button`: "left" "right" "middle" "x1" "x2", as the Mapeador's mouse buttons store it.
ButtonAction mouseClickAction(const std::string& button);

// True if the engine loaded `botName` from the sandbox data/bots/.
bool isBotLoaded(const std::string& botName);

}
