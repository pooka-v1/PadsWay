#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <d3d11.h>
#include "../imgui/imgui.h"
#include "MacroStep.h"
#include "PadView.h"

// Renders the visual step sequence (the "Sequence" section of MacroCreatorModal).
// Owned by MacroCreatorModal; receives references to modal-owned state.
//
// Selection model (range):
//   - Click unselected step  -> sets selEnd (anchor stays, or both set if no anchor)
//   - Click anchor alone     -> clears selection
//   - Range = [min, max] of selAnchor/selEnd; all steps in range are highlighted
//   - selEnd is the "active" step for token editing
class MacroStepGrid {
public:
    explicit MacroStepGrid(const std::unordered_map<std::string, PadTexture>& icons);

    // Renders the step list. Handles range selection.
    // selAnchor/selEnd: first/last clicked step (-1 = none). Updated on click.
    // dslOnly passed by reference (read-only here; cleared by the modal on Clear All).
    bool render(std::vector<MacroStepItem>& steps, bool& dslOnly,
                int& selAnchor, int& selEnd);

    // Read-only preview: renders steps without selection or interaction.
    void renderReadOnly(const std::vector<MacroStepItem>& steps);

private:
    const std::unordered_map<std::string, PadTexture>& m_icons;

    ImTextureID iconSrv(const std::string& name) const;

    // Estimated pixel width for line-wrap lookahead.
    static float stepWidth(const MacroStepItem& step);

    // Renders one step's content. Returns true if any widget inside was clicked.
    bool renderStepContent(const MacroStepItem& step, const ImVec4& btnColor);
};
