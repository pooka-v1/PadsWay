#pragma once
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <d3d11.h>
#include "../imgui/imgui.h"
#include "PadView.h"
#include "MacroStep.h"
#include "MacroStepGrid.h"

// ---------------------------------------------------------------------------
// MacroCreatorModal — shared macro editor modal used from three contexts:
//   kLibrary : name field shown, confirms by writing to macros.json (caller's job)
//   kInline  : no name field, returns execution string to caller
//
// Interaction model:
//   - Click an icon     -> creates a new Press step (or adds token to the active step)
//   - Click a step      -> makes it the active step (click again to deselect)
//   - Active Press step -> icons reflect its tokens; Hold ms is editable below
//   - Active Wait step  -> Wait ms is editable below
//   - Delete step       -> button shown below the sequence when a step is active
//
// Usage:
//   init(device)           — call once with D3D11 device (loads icon textures)
//   open(mode, name, dsl)  — call when the modal should open
//   render()               — call every frame; returns true once on confirm
//   getExecution()         — valid after render() returns true
//   getName()              — valid after render() returns true (kLibrary only)
// ---------------------------------------------------------------------------
class MacroCreatorModal {
public:
    enum class Mode { kLibrary, kInline };

    void init(ID3D11Device* device);
    void open(Mode mode, const std::string& name, const std::string& execution);
    void setMacroLibrary(const std::vector<std::pair<std::string,std::string>>& lib) { m_macroLibrary = lib; }

    // Call every frame. Returns true once when user confirms.
    bool render();

    bool        isOpen()       const { return m_open; }
    std::string getExecution() const { return std::string(m_dslBuffer); }
    std::string getName()      const { return std::string(m_nameBuffer); }

    // Renders a read-only step preview for a given DSL string (no modal opened).
    void renderPreview(const std::string& dsl);

private:
    bool m_initialized = false;
    bool m_open        = false;
    Mode m_mode        = Mode::kLibrary;

    char m_nameBuffer[128] = {};
    char m_dslBuffer[4096] = {};

    // Visual step list.
    std::vector<MacroStepItem> m_steps;
    bool m_dslOnly    = false;

    // Range selection: selAnchor = first clicked, selEnd = last clicked.
    // Active step for editing = selEnd.
    int  m_selAnchor  = -1;
    int  m_selEnd     = -1;

    // Step controls state
    int        m_waitMsBuf  = 200;   // ms for new Wait steps
    RepeatSpec m_repeatSpec;         // repeat controls state
    std::string m_repeatWarn;        // warning shown below repeat controls

    // DSL validation
    std::string m_validMsg;
    bool        m_validOk = false;

    // Macro library snapshot (set by caller before open())
    std::vector<std::pair<std::string,std::string>> m_macroLibrary;

    // Icon textures: image name -> PadTexture
    std::unordered_map<std::string, PadTexture> m_icons;
    MacroStepGrid m_stepGrid{ m_icons };

    // Internal helpers
    void loadIcons(ID3D11Device* device);
    void syncDslFromSteps();
    void addWaitStep();
    void validate();

    // Called when any token icon is clicked in the picker.
    // Creates a new Press step or toggles the token in the active Press step.
    void onTokenClick(const std::string& token);

    // Inserts 8 circular-direction Press steps (spin preset).
    void addSpinSteps(bool clockwise, bool analog, bool rightStick);

    // Inserts a fixed directional sequence (motion preset: QCF, HCF, etc.).
    // indices: array of kDpad/kAnalog indices in order; count: array length.
    void addMotionSteps(const int* indices, int count, bool analog, bool rightStick);

    // Returns true if the active step is a Press step that contains this token.
    bool isTokenInActiveStep(const std::string& token) const;

    void renderTokenPicker();
    void renderInsertMacro();
    void renderActiveStepControls();
    void renderDslField();
    void renderReference();
    void createRepeat();   // applies m_repeatSpec to current selection
    void insertMacroRef(const std::string& name, const std::string& dsl);

    // Renders one icon as a toggle button reflecting the active-step token state.
    bool renderIconToggle(const std::string& imgName,
                          const std::string& token,
                          const char*        tooltip);

    // Renders a spin preset button (no toggle state). Calls addSpinSteps on click.
    void renderSpinButton(const std::string& imgName,
                          const char*        tooltip,
                          bool               clockwise,
                          bool               analog,
                          bool               rightStick);

    // Renders a motion preset button. Calls addMotionSteps on click.
    void renderMotionButton(const std::string& imgName,
                            const char*        tooltip,
                            const int*         indices,
                            int                count,
                            bool               analog,
                            bool               rightStick);

    ImTextureID iconSrv(const std::string& name) const;
};
