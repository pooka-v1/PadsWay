#pragma once
#include <string>
#include <vector>
#include <utility>
#include <windows.h>
#include <d3d11.h>
#include "../PadEngine.h"
#include "../config/ConfigLoader.h"
#include "PadView.h"
#include "MappingModel.h"
#include "MappingSelection.h"
#include "TriggerRangeModal.h"
#include "MacroCreatorModal.h"

// ---------------------------------------------------------------------------
// MappingEditor — self-contained mapping editor widget.
//
// Owns: MappingModel (pending edits), MappingSelection (UI state),
//       range modal state, macro name cache, arrow texture.
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
    enum class Mode { kNormal, kProfile };

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

    // Enter normal mapping mode.
    void activate() { m_mode = Mode::kNormal; m_active = true; }

    // Enter profile editing mode. profilePaths/Names: full list for the selector.
    // preselectedIdx: index to pre-load (-1 = no selection / new profile).
    void activateProfile(const std::vector<std::string>& profilePaths,
                         const std::vector<std::string>& profileNames,
                         int preselectedIdx);

    bool isActive() const { return m_active; }

    // Returns true once per save cycle so AppWindow can reload its own config copy.
    bool pollConfigsSaved();

    // Returns true once when a profile was created or deleted, so AppWindow can
    // re-scan the profile list and update the engine combo.
    bool pollProfileListChanged() { bool r = m_profileListChanged; m_profileListChanged = false; return r; }

    // Update the profile list shown in the profile selector (call after re-scan).
    void updateProfileList(const std::vector<std::string>& paths,
                           const std::vector<std::string>& names) {
        m_profilePaths = paths;
        m_profileNames = names;
    }

    // Release D3D11 texture.
    void unload();

private:
    bool m_active       = false;
    bool m_configsSaved = false;
    Mode m_mode         = Mode::kNormal;

    // Profile mode state
    std::vector<std::string> m_profilePaths;
    std::vector<std::string> m_profileNames;
    int  m_profIdx         = -1;   // index into m_profilePaths; -1 = new profile
    char m_profNameBuf[128] = {};
    bool m_profToast          = false;
    ULONGLONG m_profToastTime = 0;
    bool m_profileListChanged = false;

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
    std::vector<std::string>                           m_macroNames;
    std::vector<std::pair<std::string,std::string>>    m_macroLibrary;
    bool                                               m_macroNamesLoaded = false;

    // Inline macro modal
    MacroCreatorModal m_macroModal;
    struct MacroModalPending {
        enum class Ctx { None, Button, Axis, Trigger } ctx = Ctx::None;
        std::string key;
    } m_macroModalPending;

    // Arrow texture (lazy-loaded on first render)
    PadTexture m_arrowTex;

    // Canvas origins (set during render, used for hit testing)
    ImVec2 m_physOrigin = {};
    ImVec2 m_virtOrigin = {};

    TriggerRangeModal m_trigRangeModal;

    void reload();
    void save();

    // Click handling — chained dispatch
    void handleClick(PadView& phys, PadView& virt, ImVec2 mouse);
    void onArrowHit(int arrowComp, const std::string& dir);
    void onPhysButtonHit(PadView& phys, int physHit);
    void onPhysStickHit(int physHit);
    void onPhysDpadHit(PadView& phys, int physHit, ImVec2 mouse);
    void onVirtHitPhysButton(PadView& phys, PadView& virt, ImVec2 mouse);
    void onVirtHitPhysStick(PadView& phys, PadView& virt, ImVec2 mouse);
    void onVirtHitTriggerSrc(PadView& virt, ImVec2 mouse);
    void onVirtArrowHit(PadView& phys, PadView& virt, int virtComp, const std::string& dir);
    void onVirtHitAxisAction(PadView& phys, PadView& virt, ImVec2 mouse);
};
