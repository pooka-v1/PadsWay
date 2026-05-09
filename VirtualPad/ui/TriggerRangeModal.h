#pragma once
#include <string>
#include <vector>
#include <utility>
#include "MappingModel.h"      // RangeEdit, ButtonAction, ButtonActionType
#include "MappingSelection.h"  // ActionType

// ---------------------------------------------------------------------------
// TriggerRangeModal — self-contained trigger-range editor modal.
//
// Usage:
//   open(trigger, currentRanges)   — call when "Rangos" button is pressed
//   render()                       — call every frame; returns true on Aceptar
//   result() / forKey()            — valid only when render() returns true
// ---------------------------------------------------------------------------
class TriggerRangeModal {
public:
    // Initialise and open the modal.
    void open(const std::string& trigger, const std::vector<RangeEdit>& current);

    // Render the popup. Returns true once when the user accepts.
    // Caller must then read result() and forKey() and apply them.
    bool render();

    const std::vector<RangeEdit>& result() const { return m_work; }
    const std::string&            forKey() const { return m_forKey; }
    bool                          isOpen() const { return m_open; }

private:
    bool         m_open   = false;
    std::string  m_forKey;
    std::vector<RangeEdit> m_work;
    int          m_selSect   = -1;
    ActionType m_actType   = ActionType::Xbox;
    std::vector<std::pair<std::string, std::string>> m_captureKeys;
    std::string  m_macroSel;
    int          m_xboxSel   = -1;

    // Macro name cache — lazy-loaded on first use, persists across openings.
    std::vector<std::string> m_macroNames;
    bool                     m_macroNamesLoaded = false;
};
