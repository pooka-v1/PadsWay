#pragma once
#include <string>
#include <vector>
#include <utility>
#include <d3d11.h>
#include "../PadEngine.h"
#include "../config/ConfigLoader.h"
#include "PadView.h"
#include "MappingModel.h"
#include "MappingSelection.h"

// ---------------------------------------------------------------------------
// MappingEditor — self-contained mapping editor widget.
//
// Owns: MappingModel (pending edits), MappingSelection (UI state),
//       rangos modal state, macro name cache, arrow texture.
//
// AppWindow calls:
//   init()        once after D3D11 device is ready
//   setConfigs()  after loading/reloading controllers.json
//   render()      each frame when isActive()
//   pollConfigsSaved() each frame to detect when save completed
//   unload()      in cleanup()
// ---------------------------------------------------------------------------
class MappingEditor {
public:
    // Called once after D3D11 + ImGui are ready.
    void init(ID3D11Device* device, PadEngine* engine,
              const std::vector<PadLayout>& layouts,
              const std::vector<std::string>& acceptedXbox,
              float stickSelectThreshold, int stickHoldMs);

    // Update the controller config snapshot (call after any save/reload).
    void setConfigs(const std::vector<ControllerConfig>& configs);

    // Render the full mapping editor subtab (H5-H9 logic + pads + action panels).
    // Call only when isActive().
    void render(PadView& phys, PadView& virt);

    // Enter mapping mode.
    void activate() { m_active = true; }

    bool isActive() const { return m_active; }

    // Returns true once per save cycle so AppWindow can reload its own config copy.
    bool pollConfigsSaved();

    // Release D3D11 texture.
    void unload();

private:
    bool m_active       = false;
    bool m_configsSaved = false;

    ID3D11Device*               m_device     = nullptr;
    PadEngine*                  m_engine     = nullptr;
    std::vector<ControllerConfig> m_configs;
    std::vector<PadLayout>      m_layouts;
    std::vector<std::string>    m_acceptedXbox;
    float                       m_stickSelectThreshold = 0.85f;
    int                         m_stickHoldMs          = 2000;

    MappingModel     m_model;
    MappingSelection m_sel;

    // Macro name cache (loaded lazily from data/macros.json)
    std::vector<std::string> m_macroNames;
    bool                     m_macroNamesLoaded = false;

    // Arrow texture (lazy-loaded on first render)
    PadTexture m_arrowTex;

    // Canvas origins (set during render, used for hit testing)
    ImVec2 m_physOrigin = {};
    ImVec2 m_virtOrigin = {};

    // Rangos modal state (to be moved to RangosModal in S5)
    bool         m_rangosModalOpen  = false;
    std::string  m_rangosForTrigger;
    std::vector<RangeEdit> m_rangosWork;
    int          m_rangosSelSect    = -1;
    H5ActionType m_rangosActType    = H5ActionType::Xbox;
    std::vector<std::pair<std::string,std::string>> m_rangosCaptureKeys;
    std::string  m_rangosMacroSel;
    int          m_rangosXboxSel    = -1;

    void reload();
    void save();
    void renderRangosModal();
};
