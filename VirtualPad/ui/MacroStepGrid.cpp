#define NOMINMAX
#include "MacroStepGrid.h"
#include "../config/Strings.h"
#include <cstdio>
#include <algorithm>

MacroStepGrid::MacroStepGrid(const std::unordered_map<std::string, PadTexture>& icons)
    : m_icons(icons) {}

ImTextureID MacroStepGrid::iconSrv(const std::string& name) const {
    auto it = m_icons.find(name);
    if (it != m_icons.end() && it->second.valid())
        return (ImTextureID)(uintptr_t)it->second.srv;
    return (ImTextureID)0;
}

// ---------------------------------------------------------------------------
// stepWidth — rough pixel estimate used for line-wrap lookahead
// ---------------------------------------------------------------------------
float MacroStepGrid::stepWidth(const MacroStepItem& step) {
    constexpr float kIconSz = 24.0f;
    constexpr float kGap    = 3.0f;
    constexpr float kSepW   = 18.0f;
    constexpr float kBadge  = 46.0f;  // "×UP" / "×5000" max width

    float w = 0.0f;
    switch (step.kind) {
    case MacroStepItem::Kind::Wait:
        w = kIconSz + 44.0f;
        break;
    case MacroStepItem::Kind::Press:
        w = (float)step.tokens.size() * (kIconSz + kGap)
            + (step.holdMs > 0 ? 32.0f : 0.0f);
        break;
    case MacroStepItem::Kind::Group: {
        w = 28.0f;  // "(" + ")" + padding
        for (const auto& c : step.children) w += stepWidth(c) + kSepW;
        if (!step.children.empty()) w -= kSepW;
        break;
    }
    case MacroStepItem::Kind::MacroRef:
        w = 90.0f;  // fixed estimate for named chip
        break;
    }
    if (step.repeat.isActive()) w += kBadge;
    return w;
}

// ---------------------------------------------------------------------------
// renderStepContent — renders one step; returns true if clicked
// ---------------------------------------------------------------------------
bool MacroStepGrid::renderStepContent(const MacroStepItem& step, const ImVec4& btnColor) {
    constexpr float kIconSz = 24.0f;
    constexpr float kGap    = 3.0f;
    bool clicked = false;

    ImGui::BeginGroup();

    switch (step.kind) {

    case MacroStepItem::Kind::Wait: {
        auto srv = iconSrv("Wait");
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        if (srv) clicked = ImGui::ImageButton("##w", srv, ImVec2(kIconSz, kIconSz));
        else     clicked = ImGui::Button("W##w", ImVec2(0, kIconSz + 4));
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, kGap);
        char buf[16]; std::snprintf(buf, sizeof(buf), "%dms", step.waitMs);
        ImGui::TextDisabled("%s", buf);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", step.toDsl().c_str());
        break;
    }

    case MacroStepItem::Kind::Press: {
        for (int t = 0; t < (int)step.tokens.size(); ++t) {
            if (t > 0) {
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextDisabled("+");
                ImGui::SameLine(0.0f, 2.0f);
            }
            const auto& tok = step.tokens[t];
            auto img = tokenToImageName(tok);
            auto srv = img.empty() ? (ImTextureID)0 : iconSrv(img);
            ImGui::PushID(t);
            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            if (srv) { if (ImGui::ImageButton("##t", srv, ImVec2(kIconSz, kIconSz))) clicked = true; }
            else     { if (ImGui::Button(tok.c_str(), ImVec2(0, kIconSz + 4)))       clicked = true; }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", step.toDsl().c_str());
            ImGui::PopID();
        }
        if (step.holdMs > 0) {
            ImGui::SameLine(0.0f, kGap);
            char buf[12]; std::snprintf(buf, sizeof(buf), "=%d", step.holdMs);
            ImGui::TextDisabled("%s", buf);
        }
        break;
    }

    case MacroStepItem::Kind::Group: {
        // Compact chip — button border is enough to signal grouping
        std::string label;
        for (size_t c = 0; c < step.children.size(); ++c) {
            if (c > 0) label += " \xe2\x86\x92 ";  // →
            label += step.children[c].toDsl();
        }
        if (label.size() > 30) label = label.substr(0, 27) + "...";
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        clicked = ImGui::Button(label.c_str(), ImVec2(0, kIconSz + 4));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", step.toDsl().c_str());
        break;
    }

    case MacroStepItem::Kind::MacroRef: {
        // Named chip — ◈ prefix signals it's a macro reference
        std::string label = "\xe2\x80\xa2 " + step.macroName;  // • (U+2022, in loaded font range)
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        clicked = ImGui::Button(label.c_str(), ImVec2(0, kIconSz + 4));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Macro: %s\n%s", step.macroName.c_str(), step.expandedDsl.c_str());
        break;
    }
    }

    // Repeat badge — only shown when active; no space reservation (step width changes)
    if (step.repeat.isActive()) {
        ImGui::SameLine(0.0f, 4.0f);
        std::string sfx = step.repeat.toSuffix();       // "*UP", "*5000", etc.
        std::string badge = "\xc3\x97" + sfx.substr(1); // "×UP", "×5000"
        ImGui::TextDisabled("%s", badge.c_str());
    }

    ImGui::EndGroup();
    return clicked;
}

// ---------------------------------------------------------------------------
// renderReadOnly — preview without selection or click handling
// ---------------------------------------------------------------------------
void MacroStepGrid::renderReadOnly(const std::vector<MacroStepItem>& steps) {
    if (steps.empty()) return;

    constexpr float kSepW    = 18.0f;
    constexpr ImVec4 kNeutral = { 0.18f, 0.18f, 0.18f, 1.0f };

    float rowLimit = ImGui::GetContentRegionAvail().x - 4.0f;
    float curX     = 0.0f;

    for (int i = 0; i < (int)steps.size(); ++i) {
        ImGui::PushID(i);
        float sw = stepWidth(steps[i]);
        if (i > 0) {
            float needed = kSepW + sw;
            if (curX + needed > rowLimit) { ImGui::NewLine(); curX = 0.0f; }
            else {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("\xe2\x86\x92");
                ImGui::SameLine(0.0f, 4.0f);
                curX += kSepW;
            }
        }
        renderStepContent(steps[i], kNeutral);
        curX += sw;
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
bool MacroStepGrid::render(std::vector<MacroStepItem>& steps, bool& dslOnly,
                            int& selAnchor, int& selEnd) {
    constexpr float kSepW = 18.0f;

    if (dslOnly) {
        ImGui::SeparatorText(tr("macros.section_sequence"));
        float rowH = 24.0f + 2.0f * ImGui::GetStyle().FramePadding.y;
        float w    = ImGui::GetContentRegionAvail().x;
        ImVec2 p   = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, rowH));
        const char* txt = tr("macros.hint_complex");
        ImVec2 ts = ImGui::CalcTextSize(txt);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (rowH - ts.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), txt);
        return false;
    }
    ImGui::SeparatorText(tr("macros.section_sequence"));

    if (steps.empty()) {
        float rowH = 24.0f + 2.0f * ImGui::GetStyle().FramePadding.y;
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, rowH));
        return false;
    }

    // Clamp stale indices after steps are deleted/cleared
    int n = (int)steps.size();
    if (selAnchor >= n) selAnchor = -1;
    if (selEnd    >= n) selEnd    = -1;

    int rangeMin = (selAnchor >= 0 && selEnd >= 0) ? std::min(selAnchor, selEnd) : -1;
    int rangeMax = (selAnchor >= 0 && selEnd >= 0) ? std::max(selAnchor, selEnd) : -1;

    float rowLimit = ImGui::GetContentRegionAvail().x - 4.0f;
    float curX     = 0.0f;
    int   clicked  = -1;

    for (int i = 0; i < n; ++i) {
        ImGui::PushID(i);
        const MacroStepItem& step = steps[i];
        bool inRange = (rangeMin >= 0 && i >= rangeMin && i <= rangeMax);
        bool isEnd   = (i == selEnd);

        float sw = stepWidth(step);

        if (i > 0) {
            float needed = kSepW + sw;
            if (curX + needed > rowLimit) { ImGui::NewLine(); curX = 0.0f; }
            else {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("\xe2\x86\x92");  // →
                ImGui::SameLine(0.0f, 4.0f);
                curX += kSepW;
            }
        }

        // selEnd = bright blue; rest of range = dim blue; unselected = dark
        ImVec4 btnColor = isEnd   ? ImVec4(0.20f, 0.45f, 0.75f, 1.0f)
                        : inRange ? ImVec4(0.15f, 0.30f, 0.55f, 1.0f)
                                  : ImVec4(0.18f, 0.18f, 0.18f, 1.0f);

        if (renderStepContent(step, btnColor)) clicked = i;

        curX += sw;
        ImGui::PopID();
    }

    // Range selection logic
    if (clicked >= 0) {
        if (selAnchor < 0) {
            selAnchor = selEnd = clicked;       // first click: set anchor + end
        } else if (clicked == selAnchor && selAnchor == selEnd) {
            selAnchor = selEnd = -1;            // click sole anchor: clear
        } else {
            selEnd = clicked;                   // extend or shrink range
        }
    }

    return false;
}
