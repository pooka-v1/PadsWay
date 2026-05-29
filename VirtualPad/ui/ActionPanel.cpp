#include "ActionPanel.h"
#include "../config/Strings.h"
#include "../imgui/imgui.h"
#include <string>

namespace ActionPanel {

// ---------------------------------------------------------------------------
std::pair<const char*, const char*> imguiKeyToKeyName(ImGuiKey k) {
    switch (k) {
    case ImGuiKey_F1:  return {"f1","F1"};   case ImGuiKey_F2:  return {"f2","F2"};
    case ImGuiKey_F3:  return {"f3","F3"};   case ImGuiKey_F4:  return {"f4","F4"};
    case ImGuiKey_F5:  return {"f5","F5"};   case ImGuiKey_F6:  return {"f6","F6"};
    case ImGuiKey_F7:  return {"f7","F7"};   case ImGuiKey_F8:  return {"f8","F8"};
    case ImGuiKey_F9:  return {"f9","F9"};   case ImGuiKey_F10: return {"f10","F10"};
    case ImGuiKey_F11: return {"f11","F11"}; case ImGuiKey_F12: return {"f12","F12"};
    case ImGuiKey_Space:     return {"space",    "Space"};
    case ImGuiKey_Enter:     return {"enter",    "Enter"};
    case ImGuiKey_Escape:    return {"esc",      "Esc"};
    case ImGuiKey_Tab:       return {"tab",      "Tab"};
    case ImGuiKey_Backspace: return {"backspace","Backspace"};
    case ImGuiKey_Delete:    return {"delete",   "Delete"};
    case ImGuiKey_Insert:    return {"insert",   "Insert"};
    case ImGuiKey_Home:      return {"home_key", "Home"};
    case ImGuiKey_End:       return {"end",      "End"};
    case ImGuiKey_PageUp:    return {"pageup",   "PageUp"};
    case ImGuiKey_PageDown:  return {"pagedown", "PageDown"};
    case ImGuiKey_UpArrow:   return {"up",   "\xe2\x86\x91"};
    case ImGuiKey_DownArrow: return {"down", "\xe2\x86\x93"};
    case ImGuiKey_LeftArrow: return {"left", "\xe2\x86\x90"};
    case ImGuiKey_RightArrow:return {"right","\xe2\x86\x92"};
    case ImGuiKey_LeftCtrl:  case ImGuiKey_RightCtrl:  return {"ctrl", "Ctrl"};
    case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return {"shift","Shift"};
    case ImGuiKey_LeftAlt:   case ImGuiKey_RightAlt:   return {"alt",  "Alt"};
    case ImGuiKey_LeftSuper: case ImGuiKey_RightSuper: return {"win",  "Win"};
    case ImGuiKey_A: return {"a","A"}; case ImGuiKey_B: return {"b","B"};
    case ImGuiKey_C: return {"c","C"}; case ImGuiKey_D: return {"d","D"};
    case ImGuiKey_E: return {"e","E"}; case ImGuiKey_F: return {"f","F"};
    case ImGuiKey_G: return {"g","G"}; case ImGuiKey_H: return {"h","H"};
    case ImGuiKey_I: return {"i","I"}; case ImGuiKey_J: return {"j","J"};
    case ImGuiKey_K: return {"k","K"}; case ImGuiKey_L: return {"l","L"};
    case ImGuiKey_M: return {"m","M"}; case ImGuiKey_N: return {"n","N"};
    case ImGuiKey_O: return {"o","O"}; case ImGuiKey_P: return {"p","P"};
    case ImGuiKey_Q: return {"q","Q"}; case ImGuiKey_R: return {"r","R"};
    case ImGuiKey_S: return {"s","S"}; case ImGuiKey_T: return {"t","T"};
    case ImGuiKey_U: return {"u","U"}; case ImGuiKey_V: return {"v","V"};
    case ImGuiKey_W: return {"w","W"}; case ImGuiKey_X: return {"x","X"};
    case ImGuiKey_Y: return {"y","Y"}; case ImGuiKey_Z: return {"z","Z"};
    case ImGuiKey_0: return {"0","0"}; case ImGuiKey_1: return {"1","1"};
    case ImGuiKey_2: return {"2","2"}; case ImGuiKey_3: return {"3","3"};
    case ImGuiKey_4: return {"4","4"}; case ImGuiKey_5: return {"5","5"};
    case ImGuiKey_6: return {"6","6"}; case ImGuiKey_7: return {"7","7"};
    case ImGuiKey_8: return {"8","8"}; case ImGuiKey_9: return {"9","9"};
    default: return {"", ""};
    }
}

// ---------------------------------------------------------------------------
bool renderKeyboardCapture(const char* contextId,
                           std::vector<std::pair<std::string, std::string>>& keys,
                           float availW, bool showWhenEmpty) {
    // Accumulate key presses (no repeats, no duplicates)
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        if (!ImGui::IsKeyPressed((ImGuiKey)k, false)) continue;
        auto [name, disp] = imguiKeyToKeyName((ImGuiKey)k);
        if (name[0] == '\0') continue;
        bool dup = false;
        for (const auto& p : keys) if (p.first == name) { dup = true; break; }
        if (!dup) keys.push_back({name, disp});
    }

    bool empty = keys.empty();
    if (!showWhenEmpty && empty) return false;

    // Build display string
    std::string dispStr;
    for (const auto& p : keys) { if (!dispStr.empty()) dispStr += " + "; dispStr += p.second; }
    if (dispStr.empty()) dispStr = tr("action.press_keys");

    // Center the row: [text] [Asignar] [Limpiar]
    float bAsigW = 100.0f, bLimpW = 80.0f;
    float sp   = ImGui::GetStyle().ItemSpacing.x;
    float textW = ImGui::CalcTextSize(dispStr.c_str()).x;
    float rowW  = textW + sp + bAsigW + sp + bLimpW;
    float offX  = (availW - rowW) * 0.5f;
    if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);

    ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, "%s", dispStr.c_str());
    ImGui::SameLine();

    ImGui::PushID(contextId);
    if (empty) ImGui::BeginDisabled();
    bool assigned = ImGui::Button(tr("btn.assign"), {bAsigW, 0.0f}) && !empty;
    if (empty) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("btn.clear"), {bLimpW, 0.0f})) keys.clear();
    ImGui::PopID();

    return assigned;
}

// ---------------------------------------------------------------------------
bool renderMacroCombo(const char* contextId, std::string& sel,
                      const std::vector<std::string>& names, float availW,
                      const char* extraLabel, bool* extraClicked) {
    float sp      = ImGui::GetStyle().ItemSpacing.x;
    float comboW  = 220.0f;
    float assignW = 80.0f;
    float extraW  = extraLabel ? (ImGui::CalcTextSize(extraLabel).x + 16.0f) : 0.0f;
    float totalW  = comboW + sp + assignW + (extraLabel ? sp + extraW : 0.0f);
    float off     = (availW - totalW) * 0.5f;
    if (off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    ImGui::PushID(contextId);
    ImGui::SetNextItemWidth(comboW);
    const char* preview = sel.empty() ? tr("action.pick_macro") : sel.c_str();
    if (ImGui::BeginCombo("##combo", preview)) {
        for (const auto& name : names) {
            bool selected = (name == sel);
            if (ImGui::Selectable(name.c_str(), selected)) sel = name;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    bool canA = !sel.empty();
    if (!canA) ImGui::BeginDisabled();
    bool result = ImGui::Button(tr("btn.assign"), {assignW, 0.0f}) && canA;
    if (!canA) ImGui::EndDisabled();
    if (extraLabel) {
        ImGui::SameLine();
        bool ex = ImGui::Button(extraLabel, {extraW, 0.0f});
        if (extraClicked) *extraClicked = ex;
    }
    ImGui::PopID();

    return result;
}

// ---------------------------------------------------------------------------
bool renderMouseButtons(const char* contextId, std::string& result, float availW) {
    static const struct { const char* key; const char* name; } kBtns[] = {
        {"action.mouse_left",    "left"},
        {"action.mouse_right",   "right"},
        {"action.mouse_middle",  "middle"},
        {"action.mouse_back",    "x1"},
        {"action.mouse_forward", "x2"},
    };
    constexpr int kN = 5;
    float btnW  = 80.0f;
    float total = btnW * kN + ImGui::GetStyle().ItemSpacing.x * (kN - 1);
    float off   = (availW - total) * 0.5f;
    if (off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    ImGui::PushID(contextId);
    for (int i = 0; i < kN; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(tr(kBtns[i].key), {btnW, 0.0f})) {
            result = kBtns[i].name;
            ImGui::PopID();
            return true;
        }
    }
    ImGui::PopID();
    return false;
}

} // namespace ActionPanel
