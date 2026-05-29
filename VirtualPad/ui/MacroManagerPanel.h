#pragma once
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include "MacroCreatorModal.h"

// Self-contained macro library manager.
// AppWindow calls init(device) once, activate() when the "Macros" button is
// pressed, render() each frame while isActive(), and checks pollMacrosSaved()
// to signal the engine after a change.
class MacroManagerPanel {
public:
    void init(ID3D11Device* device);   // load icon textures — call once after D3D init
    void activate();
    bool isActive() const { return m_active; }
    void render();

    bool pollMacrosSaved() { bool r = m_macrosSaved; m_macrosSaved = false; return r; }

private:
    bool m_active      = false;
    bool m_macrosSaved = false;

    std::vector<std::pair<std::string, std::string>> m_macros;  // sorted by name
    int  m_selectedIdx = -1;

    // Commit error (name conflict) shown near the action buttons
    std::string m_commitError;

    // Toast (success feedback)
    std::string m_toastMsg;
    ULONGLONG   m_toastTime = 0;

    MacroCreatorModal m_creator;

    void load();
    void save();
    void selectMacro(int idx);
    void commitFromModal();
    void showToast(const char* key);
    void renderList();
    void renderActions();
};
