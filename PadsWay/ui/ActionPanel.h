#pragma once
#include <vector>
#include <string>
#include <utility>
#include "../imgui/imgui.h"

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

} // namespace ActionPanel
