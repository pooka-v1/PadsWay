#pragma once
#include <vector>
#include <string>
#include <utility>
#include <functional>
#include "../imgui/imgui.h"
#include "../GamepadState.h"
#include "MappingSelection.h"   // ActionType

// ---------------------------------------------------------------------------
// ActionPanel — reusable ImGui sub-panels for action assignment.
//
// Each function wraps a repeated UI pattern (key capture, macro combo,
// mouse buttons) that appears in multiple places in the mapping editor.
// Callers push a unique contextId so all internal widget IDs are scoped
// and cannot collide even when called from the same ImGui window.
// ---------------------------------------------------------------------------

namespace ActionPanel {

// Reference button count for every action-type row across the app (Boton/D-pad, Gatillo,
// Analogico, Gyro/Accel, Zonas, Gestos, and the Rangos modal) — 2026/09/04. All of them divide
// their available width by this same count so a button is the same pixel width everywhere,
// regardless of how many buttons that particular panel actually shows. 7 because Analogico/Gyro's
// own row (the widest, Mando/Macro/Teclado/Raton/Raton-movimiento/Bot/Rangos) needs to fit without
// wrapping — the widest row picks the reference, not the other way round.
constexpr int kActionTypeBtnRefCount = 7;

// Key translation: ImGuiKey → {json_name, display_name}.
// Returns {"",""} for keys that are not mappable to a PadsWay action.
std::pair<const char*, const char*> imguiKeyToKeyName(ImGuiKey k);

// Accumulates key presses into `keys` and renders:
//   [Ctrl + Z]  [Asignar]  [Limpiar]
// showWhenEmpty: if true, always renders with placeholder text and a
//   disabled Asignar button; if false, renders nothing when keys is empty.
// Returns true when the user clicks Asignar (caller must clear keys).
bool renderKeyboardCapture(
    const char* contextId,
    std::vector<std::pair<std::string, std::string>>& keys,
    float availW,
    bool showWhenEmpty = false);

// Renders a macro combo + Asignar button:
//   [-- elige macro --  ▼]  [Asignar]  [extraLabel?]
// Returns true when Asignar is clicked with a non-empty selection.
// extraLabel/extraClicked: optional extra button rendered on the same row.
bool renderMacroCombo(
    const char* contextId,
    std::string& sel,
    const std::vector<std::string>& names,
    float availW,
    const char* extraLabel   = nullptr,
    bool*       extraClicked = nullptr);

// Renders a bot combo + Asignar button:
//   [-- elige bot --  ▼]  [Asignar]  [extraLabel?]
// Returns true when Asignar is clicked with a non-empty selection.
// extraLabel/extraClicked: optional extra button rendered on the same row.
bool renderBotCombo(
    const char* contextId,
    std::string& sel,
    const std::vector<std::string>& names,
    float availW,
    const char* extraLabel   = nullptr,
    bool*       extraClicked = nullptr);

// Renders 5 mouse buttons centered:
//   [Izq] [Der] [Centro] [Atrás] [Adelante]
// Returns true and sets `result` to "left"/"right"/"middle"/"x1"/"x2" on click.
bool renderMouseButtons(
    const char* contextId,
    std::string& result,
    float availW);

// Optional trailing button in renderActionTypeTabs' row, for the one case that isn't a plain
// ActionType selector: the "Rangos" button (trigger/analogico/gyro panels). Highlighted via
// `active` (not by comparing to the selected ActionType) and its click runs `onClick` directly
// instead of writing to `sel` — opening the ranges modal is its own flow, not another action type.
struct ActionTypeExtra {
    std::string           label;
    bool                  active = false;
    std::function<void()> onClick;
};

// Renders the "Mando/Macro/Teclado/Raton[/Raton-movimiento]/Bot[/Rangos]" tab row shared by the 6
// action panels (Boton/D-pad, Gatillo, Analogico, Gyro/Accel, Zonas, Gestos) — 2026/09/07,
// extracted from 6 near-identical copies in MappingEditor.cpp (see SESSION_CONTEXT.md, "Refactor
// de codigo — auditoria 2026/09/07", tarea 1). `types` lists which ActionType values this
// particular panel offers, in display order — every caller passes either the 5 standard ones
// (Xbox/Macro/Keyboard/Mouse/Bot) or that same set plus MouseMove (Analogico/Gyro only). Every
// button is the same pixel width regardless of how many render in a given panel —
// kActionTypeBtnRefCount is the fixed reference, so a button looks identical across every panel.
// Clicking a standard button sets `sel` and clears `captureKeys` (any pending keyboard capture
// belongs to the type being left). `extra`, if given, renders one more button after them.
void renderActionTypeTabs(
    const char* contextId,
    ActionType& sel,
    std::vector<std::pair<std::string, std::string>>& captureKeys,
    const std::vector<ActionType>& types,
    float rowWidth,
    const ActionTypeExtra* extra = nullptr);

// True while the "cancel current selection" chord is held (L1+R1 or A+B on the physical pad) —
// checked while a Teclado capture panel is open so the user can back out without pressing Esc.
// Extracted 2026/09/07, was copied literally at 6 call sites in MappingEditor.cpp (same task as
// renderActionTypeTabs above).
bool isCancelSelectionCombo(const GamepadState& physNow);

} // namespace ActionPanel
