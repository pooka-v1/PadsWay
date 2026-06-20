#define NOMINMAX
#include "MacroCreatorModal.h"
#include "../config/Strings.h"
#include "../macros/Macro.h"
#include "../macros/MacroParser.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <stdexcept>

// ---------------------------------------------------------------------------
// init / loadIcons
// ---------------------------------------------------------------------------

static const char* kAllImages[] = {
    "PressA","PressB","PressX","PressY",
    "PressL1","PressR1","PressL2","PressR2",
    "PressL3","PressR3","PressStart","PressSelect","PressHome",
    "CrossMoveUp","CrossMoveUpRight","CrossMoveRight","CrossMoveDownRight",
    "CrossMoveDown","CrossMoveDownLeft","CrossMoveLeft","CrossMoveUpLeft",
    "CrossSpinLeft","CrossSpinRight",
    "AnalogicMoveUp","AnalogicMoveUpRight","AnalogicMoveRight","AnalogicMoveDownRight",
    "AnalogicMoveDown","AnalogicMoveDownLeft","AnalogicMoveLeft","AnalogicMoveUpLeft",
    "AnalogicSpinLeft","AnalogicSpinRight",
    "CrossQuarterCircleForwardRight","CrossQuarterCircleForwardLeft",
    "CrossHalfCircleForwardRight","CrossHalfCircleForwardLeft",
    "AnalogicQuarterCircleForwardRight","AnalogicQuarterCircleForwardLeft",
    "AnalogicHalfCircleForwardRight","AnalogicHalfCircleForwardLeft",
    "Wait",
};

void MacroCreatorModal::init(ID3D11Device* device) {
    if (m_initialized) return;
    loadIcons(device);
    m_initialized = true;
}

void MacroCreatorModal::loadIcons(ID3D11Device* device) {
    for (const char* name : kAllImages) {
        std::string path = std::string("images/input_tokens/") + name + ".png";
        PadTexture tex;
        if (PadView::loadPng(device, path.c_str(), tex))
            m_icons[name] = std::move(tex);
    }
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------
void MacroCreatorModal::open(Mode mode, const std::string& name, const std::string& execution) {
    m_mode        = mode;
    m_waitMsBuf  = 200;
    m_validOk    = false;
    m_validMsg    = {};
    m_selAnchor   = -1;
    m_selEnd      = -1;
    m_repeatSpec  = {};
    m_repeatWarn  = {};

    strncpy_s(m_nameBuffer, name.c_str(),      sizeof(m_nameBuffer) - 1);
    strncpy_s(m_dslBuffer,  execution.c_str(), sizeof(m_dslBuffer)  - 1);

    m_steps   = tryParseSimpleDsl(execution);
    m_dslOnly = !execution.empty() && m_steps.empty();

    validate();
    m_open = true;
}

// ---------------------------------------------------------------------------
// DSL sync & validation
// ---------------------------------------------------------------------------
void MacroCreatorModal::syncDslFromSteps() {
    std::string dsl;
    for (size_t i = 0; i < m_steps.size(); ++i) {
        if (i > 0) dsl += ", ";
        dsl += m_steps[i].toDsl();
    }
    strncpy_s(m_dslBuffer, dsl.c_str(), sizeof(m_dslBuffer) - 1);
    validate();
}

void MacroCreatorModal::validate() {
    if (m_dslBuffer[0] == '\0') {
        m_validOk  = false;
        m_validMsg = tr("macros.err_empty");
        return;
    }
    try {
        Macro tmp;
        MacroParser::parse(std::string(m_dslBuffer), tmp);
        m_validOk  = true;
        m_validMsg = tr("macros.valid");
    } catch (const std::exception& e) {
        m_validOk  = false;
        m_validMsg = std::string(tr("macros.invalid")) + " " + e.what();
    }
}

// ---------------------------------------------------------------------------
// onTokenClick — creates a new Press step or toggles token in the active step
// ---------------------------------------------------------------------------
void MacroCreatorModal::onTokenClick(const std::string& token) {
    if (m_dslOnly) {
        // Complex DSL: append a one-token Press step directly
        MacroStepItem item;
        item.kind   = MacroStepItem::Kind::Press;
        item.tokens = { token };
        std::string cur(m_dslBuffer);
        if (!cur.empty()) cur += ", ";
        cur += item.toDsl();
        strncpy_s(m_dslBuffer, cur.c_str(), sizeof(m_dslBuffer) - 1);
        validate();
        return;
    }

    bool hasActivePress = m_selEnd >= 0
                         && m_selEnd < (int)m_steps.size()
                         && m_steps[m_selEnd].kind == MacroStepItem::Kind::Press;

    if (!hasActivePress) {
        // Create a new Press step at the end — no focus by default
        MacroStepItem item;
        item.kind   = MacroStepItem::Kind::Press;
        item.tokens = { token };
        m_steps.push_back(std::move(item));
    } else {
        // Toggle token in the active Press step
        auto& tokens = m_steps[m_selEnd].tokens;
        auto it = std::find(tokens.begin(), tokens.end(), token);
        if (it != tokens.end()) {
            tokens.erase(it);
            if (tokens.empty()) {
                // Removing the last token deletes the step
                m_steps.erase(m_steps.begin() + m_selEnd);
                m_selAnchor = m_selEnd = -1;
            }
        } else {
            tokens.push_back(token);
        }
    }
    syncDslFromSteps();
}

bool MacroCreatorModal::isTokenInActiveStep(const std::string& token) const {
    if (m_selEnd < 0 || m_selEnd >= (int)m_steps.size()) return false;
    const auto& step = m_steps[m_selEnd];
    if (step.kind != MacroStepItem::Kind::Press) return false;
    return std::find(step.tokens.begin(), step.tokens.end(), token) != step.tokens.end();
}

// ---------------------------------------------------------------------------
// addSpinSteps — inserts 8 directional Press steps in circular order
// ---------------------------------------------------------------------------
void MacroCreatorModal::addSpinSteps(bool clockwise, bool analog, bool rightStick) {
    constexpr int kSpinMs   = 30;
    constexpr int kRightIdx = 2;  // CR / AnalogicMoveRight in both arrays
    constexpr int kLeftIdx  = 6;  // CL / AnalogicMoveLeft  in both arrays

    int total    = analog ? kAnalogCount : kDpadCount;
    int startIdx = clockwise ? kRightIdx : kLeftIdx;

    std::vector<MacroStepItem> spin;
    spin.reserve(total);
    for (int i = 0; i < total; ++i) {
        int idx = clockwise
            ? (startIdx + i) % total
            : (startIdx - i + total) % total;
        MacroStepItem step;
        step.kind   = MacroStepItem::Kind::Press;
        step.holdMs = kSpinMs;
        step.tokens = { analog ? analogToken(rightStick, kAnalog[idx].x, kAnalog[idx].y)
                               : std::string(kDpad[idx].token) };
        spin.push_back(std::move(step));
    }

    if (m_dslOnly) {
        std::string cur(m_dslBuffer);
        for (auto& s : spin) { if (!cur.empty()) cur += ", "; cur += s.toDsl(); }
        strncpy_s(m_dslBuffer, cur.c_str(), sizeof(m_dslBuffer) - 1);
        validate();
    } else {
        for (auto& s : spin) m_steps.push_back(std::move(s));
        m_selAnchor = m_selEnd = -1;
        syncDslFromSteps();
    }
}

// ---------------------------------------------------------------------------
// addMotionSteps — inserts a fixed directional sequence (QCF, HCF, etc.)
// ---------------------------------------------------------------------------
void MacroCreatorModal::addMotionSteps(const int* indices, int count,
                                        bool analog, bool rightStick) {
    constexpr int kMotionMs = 40;

    std::vector<MacroStepItem> steps;
    steps.reserve(count);
    for (int i = 0; i < count; ++i) {
        MacroStepItem step;
        step.kind   = MacroStepItem::Kind::Press;
        step.holdMs = kMotionMs;
        step.tokens = { analog
            ? analogToken(rightStick, kAnalog[indices[i]].x, kAnalog[indices[i]].y)
            : std::string(kDpad[indices[i]].token) };
        steps.push_back(std::move(step));
    }

    if (m_dslOnly) {
        std::string cur(m_dslBuffer);
        for (auto& s : steps) { if (!cur.empty()) cur += ", "; cur += s.toDsl(); }
        strncpy_s(m_dslBuffer, cur.c_str(), sizeof(m_dslBuffer) - 1);
        validate();
    } else {
        for (auto& s : steps) m_steps.push_back(std::move(s));
        m_selAnchor = m_selEnd = -1;
        syncDslFromSteps();
    }
}

// ---------------------------------------------------------------------------
// renderMotionButton
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderMotionButton(const std::string& imgName,
                                            const char*        tooltip,
                                            const int*         indices,
                                            int                count,
                                            bool               analog,
                                            bool               rightStick) {
    constexpr float kImgSz = 24.0f;
    ImGui::PushID((imgName + (rightStick ? "_r" : "_l")).c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.25f, 1.0f));

    auto srv = iconSrv(imgName);
    bool clicked;
    if (srv) {
        clicked = ImGui::ImageButton("##icon", srv, ImVec2(kImgSz, kImgSz));
    } else {
        const ImVec2& pad = ImGui::GetStyle().FramePadding;
        clicked = ImGui::Button(tooltip, ImVec2(kImgSz + 2.0f * pad.x, kImgSz + 2.0f * pad.y));
    }

    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();

    if (clicked) addMotionSteps(indices, count, analog, rightStick);
}

// ---------------------------------------------------------------------------
// addWaitStep
// ---------------------------------------------------------------------------
void MacroCreatorModal::addWaitStep() {
    MacroStepItem item;
    item.kind   = MacroStepItem::Kind::Wait;
    item.waitMs = m_waitMsBuf;

    if (m_dslOnly) {
        std::string cur(m_dslBuffer);
        if (!cur.empty()) cur += ", ";
        cur += item.toDsl();
        strncpy_s(m_dslBuffer, cur.c_str(), sizeof(m_dslBuffer) - 1);
        validate();
    } else {
        m_steps.push_back(std::move(item));
        m_selAnchor = m_selEnd = (int)m_steps.size() - 1;
        syncDslFromSteps();
    }
}

// ---------------------------------------------------------------------------
// Icon helpers
// ---------------------------------------------------------------------------
ImTextureID MacroCreatorModal::iconSrv(const std::string& name) const {
    auto it = m_icons.find(name);
    if (it != m_icons.end() && it->second.valid())
        return (ImTextureID)(uintptr_t)it->second.srv;
    return (ImTextureID)0;
}

bool MacroCreatorModal::renderIconToggle(const std::string& imgName,
                                          const std::string& token,
                                          const char*        tooltip) {
    constexpr float kImgSz = 24.0f;

    bool selected = isTokenInActiveStep(token);
    ImVec4 bgColor = selected
        ? ImVec4(0.25f, 0.55f, 1.0f, 1.0f)
        : ImVec4(0.18f, 0.18f, 0.18f, 1.0f);

    bool clicked = false;
    ImGui::PushID((imgName + "_" + token).c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, bgColor);

    auto srv = iconSrv(imgName);
    if (srv) {
        clicked = ImGui::ImageButton("##icon", srv, ImVec2(kImgSz, kImgSz));
    } else {
        // Match ImageButton total size so the row height never shifts
        const ImVec2& pad = ImGui::GetStyle().FramePadding;
        ImVec2 sz(kImgSz + 2.0f * pad.x, kImgSz + 2.0f * pad.y);
        clicked = ImGui::Button(token.c_str(), sz);
    }

    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && tooltip)
        ImGui::SetTooltip("%s", tooltip);

    ImGui::PopID();

    if (clicked)
        onTokenClick(token);

    return clicked;
}

// ---------------------------------------------------------------------------
// renderSpinButton
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderSpinButton(const std::string& imgName,
                                          const char*        tooltip,
                                          bool               clockwise,
                                          bool               analog,
                                          bool               rightStick) {
    constexpr float kImgSz = 24.0f;
    ImGui::PushID((imgName + (rightStick ? "_r" : "_l")).c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.25f, 1.0f));

    auto srv = iconSrv(imgName);
    bool clicked;
    if (srv) {
        clicked = ImGui::ImageButton("##icon", srv, ImVec2(kImgSz, kImgSz));
    } else {
        const ImVec2& pad = ImGui::GetStyle().FramePadding;
        clicked = ImGui::Button(tooltip, ImVec2(kImgSz + 2.0f * pad.x, kImgSz + 2.0f * pad.y));
    }

    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();

    if (clicked) addSpinSteps(clockwise, analog, rightStick);
}

// ---------------------------------------------------------------------------
// renderPreview — read-only step preview, called from MacroManagerPanel
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderPreview(const std::string& dsl) {
    auto steps = tryParseSimpleDsl(dsl);
    if (steps.empty()) {
        ImGui::TextDisabled("%s", dsl.c_str());
        return;
    }
    m_stepGrid.renderReadOnly(steps);
}

// ---------------------------------------------------------------------------
// insertMacroRef — creates a MacroRef step (or appends DSL in dslOnly mode)
// ---------------------------------------------------------------------------
void MacroCreatorModal::insertMacroRef(const std::string& name, const std::string& dsl) {
    if (m_dslOnly) {
        std::string cur(m_dslBuffer);
        if (!cur.empty()) cur += ", ";
        cur += dsl;
        strncpy_s(m_dslBuffer, cur.c_str(), sizeof(m_dslBuffer) - 1);
        validate();
        return;
    }
    MacroStepItem item;
    item.kind        = MacroStepItem::Kind::MacroRef;
    item.macroName   = name;
    item.expandedDsl = dsl;
    m_steps.push_back(std::move(item));
    m_selAnchor = m_selEnd = (int)m_steps.size() - 1;
    syncDslFromSteps();
}

// ---------------------------------------------------------------------------
// renderInsertMacro — "Macros" section inside the token picker
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderInsertMacro() {
    if (m_macroLibrary.empty()) {
        ImGui::TextDisabled("%s", tr("macros.no_library"));
        return;
    }
    if (ImGui::Button(tr("macros.btn_insert")))
        ImGui::OpenPopup("##ins_macro_popup");

    if (ImGui::BeginPopup("##ins_macro_popup")) {
        for (auto& [name, dsl] : m_macroLibrary) {
            if (ImGui::Selectable(name.c_str())) {
                insertMacroRef(name, dsl);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", dsl.c_str());
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// renderTokenPicker
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderTokenPicker() {
    // kDpad order: 0=CU,1=CUR,2=CR,3=CDR,4=CD,5=CDL,6=CL,7=CUL
    static const int kHCFLeft[]  = { 2, 3, 4, 5, 6 };  // CR..CL
    static const int kHCFRight[] = { 6, 5, 4, 3, 2 };  // CL..CR
    static const int kQCFLeft[]  = { 4, 5, 6 };         // CD, CDL, CL
    static const int kQCFRight[] = { 4, 3, 2 };         // CD, CDR, CR

    ImGui::SeparatorText(tr("macros.section_buttons"));
    for (int i = 0; i < kButtonsCount; ++i) {
        renderIconToggle(kButtons[i].img, kButtons[i].token, kButtons[i].tip);
        if (i < kButtonsCount - 1) ImGui::SameLine(0.0f, 4.0f);
    }

    ImGui::SeparatorText(tr("macros.section_dpad"));
    for (int i = 0; i < kDpadCount; ++i) {
        renderIconToggle(kDpad[i].img, kDpad[i].token, kDpad[i].tip);
        ImGui::SameLine(0.0f, 4.0f);
    }
    renderSpinButton("CrossSpinLeft",  tr("macros.spin_ccw"), false, false, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderSpinButton("CrossSpinRight", tr("macros.spin_cw"),  true,  false, false);
    ImGui::SameLine(0.0f, 8.0f);
    renderMotionButton("CrossHalfCircleForwardLeft",     tr("macros.motion_hcf_left"),  kHCFLeft,  5, false, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("CrossHalfCircleForwardRight",    tr("macros.motion_hcf_right"), kHCFRight, 5, false, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("CrossQuarterCircleForwardLeft",  tr("macros.motion_qcf_left"),  kQCFLeft,  3, false, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("CrossQuarterCircleForwardRight", tr("macros.motion_qcf_right"), kQCFRight, 3, false, false);

    ImGui::SeparatorText(tr("macros.section_analog"));

    // L stick row
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("L");
    for (int i = 0; i < kAnalogCount; ++i) {
        ImGui::SameLine(0.0f, 3.0f);
        renderIconToggle(kAnalog[i].img, analogToken(false, kAnalog[i].x, kAnalog[i].y), kAnalog[i].tip);
    }
    ImGui::SameLine(0.0f, 6.0f);
    renderSpinButton("AnalogicSpinLeft",  tr("macros.spin_ccw"), false, true, false);
    ImGui::SameLine(0.0f, 3.0f);
    renderSpinButton("AnalogicSpinRight", tr("macros.spin_cw"),  true,  true, false);
    ImGui::SameLine(0.0f, 8.0f);
    renderMotionButton("AnalogicHalfCircleForwardLeft",     tr("macros.motion_hcf_left"),  kHCFLeft,  5, true, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("AnalogicHalfCircleForwardRight",    tr("macros.motion_hcf_right"), kHCFRight, 5, true, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("AnalogicQuarterCircleForwardLeft",  tr("macros.motion_qcf_left"),  kQCFLeft,  3, true, false);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("AnalogicQuarterCircleForwardRight", tr("macros.motion_qcf_right"), kQCFRight, 3, true, false);

    // R stick row
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("R");
    for (int i = 0; i < kAnalogCount; ++i) {
        ImGui::SameLine(0.0f, 3.0f);
        renderIconToggle(kAnalog[i].img, analogToken(true, kAnalog[i].x, kAnalog[i].y), kAnalog[i].tip);
    }
    ImGui::SameLine(0.0f, 6.0f);
    renderSpinButton("AnalogicSpinLeft",  tr("macros.spin_ccw"), false, true, true);
    ImGui::SameLine(0.0f, 3.0f);
    renderSpinButton("AnalogicSpinRight", tr("macros.spin_cw"),  true,  true, true);
    ImGui::SameLine(0.0f, 8.0f);
    renderMotionButton("AnalogicHalfCircleForwardLeft",     tr("macros.motion_hcf_left"),  kHCFLeft,  5, true, true);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("AnalogicHalfCircleForwardRight",    tr("macros.motion_hcf_right"), kHCFRight, 5, true, true);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("AnalogicQuarterCircleForwardLeft",  tr("macros.motion_qcf_left"),  kQCFLeft,  3, true, true);
    ImGui::SameLine(0.0f, 4.0f);
    renderMotionButton("AnalogicQuarterCircleForwardRight", tr("macros.motion_qcf_right"), kQCFRight, 3, true, true);

    ImGui::SeparatorText(tr("macros.section_macros"));
    renderInsertMacro();
}

// ---------------------------------------------------------------------------
// createRepeat — applies m_repeatSpec to the current selection
// ---------------------------------------------------------------------------
void MacroCreatorModal::createRepeat() {
    if (m_selEnd < 0) return;
    int lo = std::min(m_selAnchor, m_selEnd);
    int hi = std::max(m_selAnchor, m_selEnd);
    if (lo < 0) lo = hi;

    if (lo == hi) {
        m_steps[lo].repeat = m_repeatSpec;
    } else {
        MacroStepItem grp;
        grp.kind     = MacroStepItem::Kind::Group;
        grp.repeat   = m_repeatSpec;
        grp.children.assign(m_steps.begin() + lo, m_steps.begin() + hi + 1);
        m_steps.erase(m_steps.begin() + lo, m_steps.begin() + hi + 1);
        m_steps.insert(m_steps.begin() + lo, std::move(grp));
    }
    m_selAnchor = m_selEnd = lo;
    syncDslFromSteps();
}

// ---------------------------------------------------------------------------
// renderActiveStepControls — shown below the step sequence
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderActiveStepControls() {
    ImGui::Spacing();

    bool hasActive      = m_selEnd >= 0 && m_selEnd < (int)m_steps.size();
    bool isSingleSel    = hasActive && (m_selAnchor < 0 || m_selAnchor == m_selEnd);
    bool hasActivePress = isSingleSel && m_steps[m_selEnd].kind == MacroStepItem::Kind::Press;
    bool hasActiveWait  = isSingleSel && m_steps[m_selEnd].kind == MacroStepItem::Kind::Wait;
    bool hasSteps       = !m_steps.empty();

    // --- Row 1: Hold ms / Wait ms / Add Wait ---
    if (!hasActivePress) ImGui::BeginDisabled();
    int hold = hasActivePress ? m_steps[m_selEnd].holdMs : 0;
    ImGui::Text("%s", tr("macros.hold_ms")); ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("##hold", &hold) && hasActivePress) {
        if (hold < 0) hold = 0;
        m_steps[m_selEnd].holdMs = hold;
        syncDslFromSteps();
    }
    if (!hasActivePress) ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 12.0f);

    int& waitVal = hasActiveWait ? m_steps[m_selEnd].waitMs : m_waitMsBuf;
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("ms##wms", &waitVal)) {
        if (waitVal < 1) waitVal = 1;
        if (hasActiveWait) syncDslFromSteps();
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(tr("macros.btn_add_wait"))) addWaitStep();

    // --- Row 2: Repeat controls ---
    ImGui::Spacing();
    if (!hasActive) ImGui::BeginDisabled();

    // Mode selector
    const char* kModeLabels[] = {
        tr("macros.rmode_none"),
        tr("macros.rmode_duration"),
        tr("macros.rmode_count"),
        tr("macros.rmode_hold"),
        tr("macros.rmode_toggle")
    };
    int modeIdx = (int)m_repeatSpec.mode;
    ImGui::Text("%s", tr("macros.repeat_label")); ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("##rmode", &modeIdx, kModeLabels, 5))
        m_repeatSpec.mode = (RepeatSpec::Mode)modeIdx;

    // Duration field — enabled only for Duration and CountInDuration
    bool needMs = m_repeatSpec.mode == RepeatSpec::Mode::Duration
               || m_repeatSpec.mode == RepeatSpec::Mode::CountInDuration;
    ImGui::SameLine(0.0f, 12.0f);
    if (!needMs) ImGui::BeginDisabled();
    ImGui::Text("%s", tr("macros.duration_ms")); ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("##rms", &m_repeatSpec.ms) && m_repeatSpec.ms < 0)
        m_repeatSpec.ms = 0;
    if (!needMs) ImGui::EndDisabled();

    // Count field — enabled only for CountInDuration
    bool needN = m_repeatSpec.mode == RepeatSpec::Mode::CountInDuration;
    ImGui::SameLine(0.0f, 12.0f);
    if (!needN) ImGui::BeginDisabled();
    ImGui::Text("%s", tr("macros.n_times")); ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("##rn", &m_repeatSpec.count) && m_repeatSpec.count < 0)
        m_repeatSpec.count = 0;
    if (!needN) ImGui::EndDisabled();

    if (!hasActive) ImGui::EndDisabled();

    // --- Row 3: Warning line (always occupies one line to avoid layout shifts) ---
    ImGui::Spacing();
    if (!m_repeatWarn.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.10f, 1.0f));
        ImGui::TextUnformatted(m_repeatWarn.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
    }

    // --- Row 4: Delete / Clear all / Create repeat ---
    ImGui::Spacing();

    // Delete
    if (!hasActive) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
    const char* delLabel = (!isSingleSel && hasActive) ? tr("macros.btn_delete_range") : tr("macros.btn_delete_step");
    if (ImGui::Button(delLabel) && hasActive) {
        int lo = std::min(m_selAnchor < 0 ? m_selEnd : m_selAnchor, m_selEnd);
        int hi = std::max(m_selAnchor < 0 ? m_selEnd : m_selAnchor, m_selEnd);
        m_steps.erase(m_steps.begin() + lo, m_steps.begin() + hi + 1);
        m_selAnchor = m_selEnd = -1;
        m_dslOnly = false;
        syncDslFromSteps();
    }
    ImGui::PopStyleColor(2);
    if (!hasActive) ImGui::EndDisabled();

    // Clear all
    ImGui::SameLine(0.0f, 8.0f);
    if (!hasSteps) ImGui::BeginDisabled();
    if (ImGui::Button(tr("macros.btn_clear_all")) && hasSteps) {
        m_steps.clear();
        m_selAnchor = m_selEnd = -1;
        m_dslOnly = false;
        syncDslFromSteps();
    }
    if (!hasSteps) ImGui::EndDisabled();

    // Create repeat (right-aligned)
    bool canRepeat = hasActive && m_repeatSpec.mode != RepeatSpec::Mode::None;
    ImGui::SameLine(0.0f, 24.0f);
    if (!canRepeat) ImGui::BeginDisabled();
    if (ImGui::Button(tr("macros.btn_create_repeat")) && canRepeat) {
        m_repeatWarn.clear();
        // Validate required fields
        if (m_repeatSpec.mode == RepeatSpec::Mode::Duration && m_repeatSpec.ms <= 0)
            m_repeatWarn = tr("macros.err_ms_zero");
        else if (m_repeatSpec.mode == RepeatSpec::Mode::CountInDuration
                 && (m_repeatSpec.ms <= 0 || m_repeatSpec.count <= 0))
            m_repeatWarn = (m_repeatSpec.ms <= 0) ? tr("macros.err_ms_zero") : tr("macros.err_n_zero");
        else
            createRepeat();
    }
    if (!canRepeat) ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
// renderReference — collapsible DSL cheat-sheet
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderReference() {
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader(tr("macros.ref_title")))
        return;

    ImGui::Spacing();

    if (ImGui::BeginTabBar("##ref_tabs")) {

        if (ImGui::BeginTabItem(tr("macros.ref_operators"))) {
            ImGui::Spacing();
            static const struct { const char* syntax; const char* effect; } kOps[] = {
                { "A, B, C",        "Sequence: A then B then C (200 ms each)" },
                { "A + Y",          "Combo: A and Y at the same time" },
                { "B=1000",         "Hold B for 1000 ms" },
                { "500",            "Wait (pause) of 500 ms" },
                { "A*5000",         "Repeat A for 5000 ms (200 ms interval)" },
                { "A*1000/10",      "Repeat A 10 times in 1000 ms" },
                { "A*UP",           "Repeat while physical button held" },
                { "A*DO",           "Toggle: start on press, stop on next press" },
                { "(A,B)*5000",     "Loop sequence A,B for 5000 ms" },
                { "(A,B)*1000/5",   "Loop sequence 5 times in 1000 ms" },
                { "(A,B)*UP",       "Loop sequence while button held" },
                { "(A,B)*DO",       "Toggle sequence on/off" },
            };
            constexpr int kOpsRows = (int)(sizeof(kOps) / sizeof(kOps[0]));

            if (ImGui::BeginTable("##ref_ops", 2,
                    ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Syntax", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (int i = 0; i < kOpsRows; i++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(kOps[i].syntax);
                    ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", kOps[i].effect);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(tr("macros.ref_buttons"))) {
            ImGui::Spacing();
            static const struct { const char* token; const char* desc; } kBtns[] = {
                { "A  B  X  Y",         "Face buttons" },
                { "L1  R1",             "LB / RB  (bumpers)" },
                { "L2  R2",             "LT / RT  (triggers)" },
                { "L3  R3",             "LS / RS  (stick clicks)" },
                { "CU  CD  CL  CR",     "D-pad  Up / Down / Left / Right" },
                { "CUR  CUL  CDR  CDL", "D-pad diagonals" },
                { "ST  SE",             "Start / Select (Back)" },
                { "LAX  LAY",           "Left stick  X / Y  [-1.0 .. 1.0]" },
                { "RAX  RAY",           "Right stick X / Y  [-1.0 .. 1.0]" },
            };
            constexpr int kBtnsRows = (int)(sizeof(kBtns) / sizeof(kBtns[0]));

            if (ImGui::BeginTable("##ref_btns", 2,
                    ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Token", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (int i = 0; i < kBtnsRows; i++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(kBtns[i].token);
                    ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", kBtns[i].desc);
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Analog tip: diagonals use +/-0.71 (cos/sin 45deg), not +/-0.5");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// ---------------------------------------------------------------------------
// renderDslField
// ---------------------------------------------------------------------------
void MacroCreatorModal::renderDslField() {
    ImGui::SeparatorText(tr("macros.section_dsl"));

    float avail = ImGui::GetContentRegionAvail().x;
    if (ImGui::InputTextMultiline("##dsl", m_dslBuffer, sizeof(m_dslBuffer),
                                  ImVec2(avail, 60.0f))) {
        validate();
        auto parsed = tryParseSimpleDsl(std::string(m_dslBuffer));
        if (!parsed.empty() || m_dslBuffer[0] == '\0') {
            m_steps     = parsed;
            m_dslOnly   = false;
        } else {
            m_steps.clear();
            m_dslOnly   = true;
        }
        m_selAnchor = m_selEnd = -1;
    }

    ImVec4 col = m_validOk
        ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f)
        : ImVec4(0.9f, 0.3f, 0.2f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", m_validMsg.c_str());
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
bool MacroCreatorModal::render() {
    if (!m_open) return false;

    const char* popupId = "##MacroCreatorModal";
    ImGui::OpenPopup(popupId);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680.0f, 0.0f));

    if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return false;

    const char* title = (m_mode == Mode::kLibrary) ? tr("macros.title_edit") : tr("macros.title_build");
    ImGui::TextDisabled("%s", title);
    ImGui::Separator();

    if (m_mode == Mode::kLibrary) {
        ImGui::Text("%s", tr("macros.name_label")); ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputText("##macroname", m_nameBuffer, sizeof(m_nameBuffer));
        ImGui::Spacing();
    }

    renderTokenPicker();
    m_stepGrid.render(m_steps, m_dslOnly, m_selAnchor, m_selEnd);
    renderActiveStepControls();
    renderDslField();
    renderReference();

    ImGui::Spacing();
    ImGui::Separator();

    bool confirmed = false;
    bool canConfirm = m_validOk &&
                      (m_mode == Mode::kInline || m_nameBuffer[0] != '\0');

    if (!canConfirm) ImGui::BeginDisabled();
    if (ImGui::Button(tr("btn.save"), {120.0f, 0.0f})) {
        confirmed = true;
        m_open    = false;
        ImGui::CloseCurrentPopup();
    }
    if (!canConfirm) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(tr("btn.cancel"), {80.0f, 0.0f})) {
        m_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return confirmed;
}
