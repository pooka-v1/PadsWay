#include "TriggerRangeModal.h"
#include "../config/Strings.h"
#include "ActionPanel.h"
#include "../imgui/imgui.h"
#include "../nlohmann/json.hpp"
using json = nlohmann::json;
#include "../Paths.h"

#include <fstream>
#include <algorithm>

// ---------------------------------------------------------------------------
void TriggerRangeModal::open(const std::string& trigger, const std::vector<RangeEdit>& current,
                             const std::vector<std::string>& botNames) {
    m_forKey = trigger;
    m_work       = current;
    m_selSect    = -1;
    m_actType    = ActionType::Xbox;
    m_captureKeys.clear();
    m_macroSel.clear();
    m_botSel.clear();
    m_botNames   = botNames;
    m_xboxSel    = -1;
    if (m_work.empty()) {
        RangeEdit re; re.from = 0.1f; re.to = 1.0f;
        m_work.push_back(re);
    }
    m_open = true;
}

// ---------------------------------------------------------------------------
bool TriggerRangeModal::render() {
    if (!m_open) return false;
    ImGui::OpenPopup(trid("ranges.title", "rangesModal").c_str());

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 600.0f, 0.0f });

    if (!ImGui::BeginPopupModal(trid("ranges.title", "rangesModal").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return false;

    struct XboxChoice { const char* key; const char* name; };
    static const XboxChoice kChoices[] = {
        {"pad.btn_a","a"},{"pad.btn_b","b"},{"pad.btn_x","x"},{"pad.btn_y","y"},
        {"pad.btn_l1","l1"},{"pad.btn_r1","r1"},{"pad.btn_select","select"},{"pad.btn_start","start"},{"pad.btn_home","home"},
        {"pad.btn_l3","l3"},{"pad.btn_r3","r3"},
        {"pad.dpad_up","dpad_up"},{"pad.dpad_down","dpad_down"},
        {"pad.dpad_left","dpad_left"},{"pad.dpad_right","dpad_right"},
        {"pad.left_y_pos","left_y_pos"},{"pad.left_y_neg","left_y_neg"},
        {"pad.left_x_pos","left_x_pos"},{"pad.left_x_neg","left_x_neg"},
        {"pad.right_y_pos","right_y_pos"},{"pad.right_y_neg","right_y_neg"},
        {"pad.right_x_pos","right_x_pos"},{"pad.right_x_neg","right_x_neg"},
        {"pad.trigger_l","l2"},{"pad.trigger_r","r2"},
    };
    static const int kNChoices = 25;

    std::string hdrStr = m_forKey + "  \xe2\x86\x92  " + tr("action.ranges_header");
    ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", hdrStr.c_str());
    ImGui::Spacing();

    // ── Range bar ────────────────────────────────────────────────────────────
    {
        int n = (int)m_work.size();
        float barW  = ImGui::GetContentRegionAvail().x - 4.0f;
        float barH  = 28.0f;
        ImVec2 barMin = { ImGui::GetCursorScreenPos().x + 2.0f, ImGui::GetCursorScreenPos().y };
        ImVec2 barMax = { barMin.x + barW, barMin.y + barH };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(barMin, barMax, IM_COL32(40,40,40,255), 4.0f);

        for (int i = 0; i < n; ++i) {
            float t0 = (m_work[i].from - 0.1f) / 0.9f;
            float t1 = (m_work[i].to   - 0.1f) / 0.9f;
            t0 = std::clamp(t0, 0.0f, 1.0f);
            t1 = std::clamp(t1, 0.0f, 1.0f);
            ImVec2 r0 = { barMin.x + t0 * barW + 1.0f, barMin.y + 1.0f };
            ImVec2 r1 = { barMin.x + t1 * barW - 1.0f, barMax.y - 1.0f };
            ImU32 col = (i == m_selSect)
                ? IM_COL32(255,180,0,220)
                : (m_work[i].hasAction ? IM_COL32(60,160,80,200) : IM_COL32(80,80,120,180));
            dl->AddRectFilled(r0, r1, col, 3.0f);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f\xe2\x80\x93%.2f", m_work[i].from, m_work[i].to);
            ImVec2 textSz = ImGui::CalcTextSize(buf);
            float cx = (r0.x + r1.x) * 0.5f - textSz.x * 0.5f;
            float cy = (r0.y + r1.y) * 0.5f - textSz.y * 0.5f;
            if (cx >= r0.x && cx + textSz.x <= r1.x)
                dl->AddText({ cx, cy }, IM_COL32(230,230,230,255), buf);
            if (i > 0)
                dl->AddLine({ barMin.x + t0 * barW, barMin.y }, { barMin.x + t0 * barW, barMax.y },
                             IM_COL32(200,200,200,160), 1.5f);
        }
        dl->AddRect(barMin, barMax, IM_COL32(150,150,150,200), 4.0f);

        ImGui::InvisibleButton("##rangeBar", { barW + 4.0f, barH });
        if (ImGui::IsItemClicked()) {
            float mx = ImGui::GetIO().MousePos.x - barMin.x;
            float normPos = mx / barW;
            float trigPos = normPos * 0.9f + 0.1f;
            for (int i = 0; i < n; ++i) {
                if (trigPos >= m_work[i].from && trigPos <= m_work[i].to) {
                    m_selSect = (m_selSect == i) ? -1 : i;
                    m_actType = ActionType::Xbox;
                    m_captureKeys.clear(); m_macroSel.clear(); m_botSel.clear(); m_xboxSel = -1;
                    if (m_selSect >= 0 && m_work[i].hasAction) {
                        const auto& act = m_work[i].action;
                        if (act.type == ButtonActionType::Macro)           m_actType = ActionType::Macro;
                        else if (act.type == ButtonActionType::Keyboard)   m_actType = ActionType::Keyboard;
                        else if (act.type == ButtonActionType::MouseClick) m_actType = ActionType::Mouse;
                        else if (act.type == ButtonActionType::Bot)        { m_actType = ActionType::Bot; m_botSel = act.name; }
                        else {
                            m_actType = ActionType::Xbox;
                            if (act.type == ButtonActionType::VirtualButton) {
                                for (int ci = 0; ci < kNChoices; ++ci)
                                    if (act.name == kChoices[ci].name) { m_xboxSel = ci; break; }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    ImGui::Spacing();

    // ── Botones de gestión de zonas ───────────────────────────────────────────
    {
        int n = (int)m_work.size();
        bool canAdd = (n < 10);
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button(n == 1 ? tr("ranges.split_create") : tr("ranges.split_add"), { 0.0f, 0.0f })) {
            int newN = n + 1;
            m_work.clear();
            for (int i = 0; i < newN; ++i) {
                RangeEdit re;
                re.from = 0.1f + i       * 0.9f / (float)newN;
                re.to   = 0.1f + (i + 1) * 0.9f / (float)newN;
                if (i == newN - 1) re.to = 1.0f;
                m_work.push_back(re);
            }
            m_selSect = -1; m_actType = ActionType::Xbox;
            m_captureKeys.clear(); m_macroSel.clear(); m_botSel.clear();
        }
        if (!canAdd) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canRemove = (n > 1);
        if (!canRemove) ImGui::BeginDisabled();
        if (ImGui::Button(trid("ranges.remove", "rangeRm").c_str(), { 0.0f, 0.0f }) && canRemove && m_selSect >= 0) {
            m_work.erase(m_work.begin() + m_selSect);
            int newN = (int)m_work.size();
            for (int i = 0; i < newN; ++i) {
                m_work[i].from = 0.1f + i       * 0.9f / (float)newN;
                m_work[i].to   = 0.1f + (i + 1) * 0.9f / (float)newN;
                if (i == newN - 1) m_work[i].to = 1.0f;
            }
            m_selSect = -1;
        }
        if (!canRemove) ImGui::EndDisabled();
        ImGui::SameLine();
        if (m_selSect >= 0 && m_work[m_selSect].hasAction) {
            if (ImGui::Button(trid("ranges.clear_action", "rangeClear").c_str())) {
                m_work[m_selSect].hasAction = false;
                m_work[m_selSect].action = ButtonAction{};
                m_actType = ActionType::Xbox; m_captureKeys.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled(tr("ranges.count"), (int)m_work.size());
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ── Panel de acción de la zona seleccionada ───────────────────────────────
    if (m_selSect < 0) {
        ImGui::TextDisabled("%s", tr("ranges.hint"));
    } else {
        ImGui::Text(tr("ranges.zone"),
                    m_selSect + 1,
                    m_work[m_selSect].from,
                    m_work[m_selSect].to);
        ImGui::Spacing();

        float bW = 85.0f;
        float sp = ImGui::GetStyle().ItemSpacing.x;
        float totalBtnW = bW * 5 + sp * 4;
        float offBX = (ImGui::GetContentRegionAvail().x - totalBtnW) * 0.5f;
        if (offBX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offBX);
        auto rTypeBtn = [&](const char* lbl, ActionType t) {
            bool sel = (m_actType == t);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(lbl, { bW, 0.0f })) { m_actType = t; m_captureKeys.clear(); m_macroSel.clear(); m_botSel.clear(); m_xboxSel = -1; }
            if (sel) ImGui::PopStyleColor();
        };
        char lbl0[64], lbl1[64], lbl2[64], lbl3[64], lbl4[64];
        snprintf(lbl0, sizeof(lbl0), "%s##rt0", tr("action.type_gamepad"));
        snprintf(lbl1, sizeof(lbl1), "%s##rt1", tr("action.type_macro"));
        snprintf(lbl2, sizeof(lbl2), "%s##rt2", tr("action.type_keyboard"));
        snprintf(lbl3, sizeof(lbl3), "%s##rt3", tr("action.type_mouse"));
        snprintf(lbl4, sizeof(lbl4), "%s##rt4", tr("action.type_bot"));
        rTypeBtn(lbl0, ActionType::Xbox);     ImGui::SameLine();
        rTypeBtn(lbl1, ActionType::Macro);    ImGui::SameLine();
        rTypeBtn(lbl2, ActionType::Keyboard); ImGui::SameLine();
        rTypeBtn(lbl3, ActionType::Mouse);    ImGui::SameLine();
        rTypeBtn(lbl4, ActionType::Bot);

        ImGui::Spacing();

        if (m_actType == ActionType::Xbox) {
            float cW = 220.0f;
            float cOff = (ImGui::GetContentRegionAvail().x - cW - sp - 80.0f) * 0.5f;
            if (cOff > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cOff);
            ImGui::SetNextItemWidth(cW);
            const char* preview = (m_xboxSel >= 0 && m_xboxSel < kNChoices)
                ? tr(kChoices[m_xboxSel].key)
                : tr("action.pick_button");
            if (m_xboxSel < 0 && m_work[m_selSect].hasAction) {
                const auto& act = m_work[m_selSect].action;
                if (act.type == ButtonActionType::VirtualButton) {
                    for (int ci = 0; ci < kNChoices; ++ci) {
                        if (act.name == kChoices[ci].name) { m_xboxSel = ci; break; }
                    }
                    if (m_xboxSel >= 0) preview = tr(kChoices[m_xboxSel].key);
                }
            }
            if (ImGui::BeginCombo("##rangesXbox", preview)) {
                for (int ci = 0; ci < kNChoices; ++ci) {
                    bool sel = (m_xboxSel == ci);
                    if (ImGui::Selectable(tr(kChoices[ci].key), sel)) m_xboxSel = ci;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            bool canAssign = (m_xboxSel >= 0);
            if (!canAssign) ImGui::BeginDisabled();
            if (ImGui::Button(trid("btn.assign", "rxbAssign").c_str(), { 80.0f, 0.0f }) && canAssign) {
                ButtonAction act;
                act.type = ButtonActionType::VirtualButton;
                act.name = kChoices[m_xboxSel].name;
                m_work[m_selSect].action    = act;
                m_work[m_selSect].hasAction = true;
            }
            if (!canAssign) ImGui::EndDisabled();
            if (m_work[m_selSect].hasAction &&
                m_work[m_selSect].action.type == ButtonActionType::VirtualButton) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_work[m_selSect].action.name.c_str());
            }

        } else if (m_actType == ActionType::Macro) {
            if (!m_macroNamesLoaded) {
                m_macroNames.clear();
                try {
                    std::ifstream f(Paths::userData("data/macros.json"));
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k,v] : j.items()) m_macroNames.push_back(k); }
                } catch (...) {}
                m_macroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_rng", m_macroSel, m_macroNames,
                                              ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type = ButtonActionType::Macro; act.name = m_macroSel;
                m_work[m_selSect].action    = act;
                m_work[m_selSect].hasAction = true;
            }

        } else if (m_actType == ActionType::Keyboard) {
            if (ActionPanel::renderKeyboardCapture("kb_rng", m_captureKeys,
                                                   ImGui::GetContentRegionAvail().x, true)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard;
                for (const auto& p : m_captureKeys) act.keys.push_back(p.first);
                m_work[m_selSect].action    = act;
                m_work[m_selSect].hasAction = true;
                m_captureKeys.clear();
            }
            if (m_work[m_selSect].hasAction &&
                m_work[m_selSect].action.type == ButtonActionType::Keyboard &&
                m_captureKeys.empty()) {
                std::string ex;
                for (const auto& k : m_work[m_selSect].action.keys) { if (!ex.empty()) ex += "+"; ex += k; }
                ImGui::TextDisabled(tr("ranges.current"), ex.c_str());
            }

        } else if (m_actType == ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_rng", mbResult,
                                                ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type = ButtonActionType::MouseClick; act.mouseButton = mbResult;
                m_work[m_selSect].action    = act;
                m_work[m_selSect].hasAction = true;
            }
            if (m_work[m_selSect].hasAction &&
                m_work[m_selSect].action.type == ButtonActionType::MouseClick) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_work[m_selSect].action.mouseButton.c_str());
            }

        } else if (m_actType == ActionType::Bot) {
            if (m_botSel.empty() && m_work[m_selSect].hasAction &&
                m_work[m_selSect].action.type == ButtonActionType::Bot)
                m_botSel = m_work[m_selSect].action.name;
            if (ActionPanel::renderBotCombo("bot_rng", m_botSel, m_botNames,
                                            ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type = ButtonActionType::Bot; act.name = m_botSel;
                m_work[m_selSect].action    = act;
                m_work[m_selSect].hasAction = true;
            }
            if (m_work[m_selSect].hasAction &&
                m_work[m_selSect].action.type == ButtonActionType::Bot) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_work[m_selSect].action.name.c_str());
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Aceptar / Cancelar ────────────────────────────────────────────────────
    float btnW2  = 100.0f;
    float dialogW = ImGui::GetContentRegionAvail().x;
    float btnOff = (dialogW - btnW2 * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (btnOff > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnOff);

    bool accepted = false;
    if (ImGui::Button(trid("btn.ok", "rangesOk").c_str(), { btnW2, 0.0f })) {
        m_open = false;
        ImGui::CloseCurrentPopup();
        accepted = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(trid("btn.cancel", "rangesCan").c_str(), { btnW2, 0.0f })) {
        m_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return accepted;
}
