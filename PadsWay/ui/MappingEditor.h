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
              float stickSelectThreshold, int stickHoldMs,
              float gyroSelectThreshold, float accelSelectThreshold);

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

    // Update the profile list shown in the profile selector (call after re-scan). Re-anchors
    // m_profIdx to wherever the currently-open profile ended up in the new list — the rescan
    // order (filesystem enumeration) doesn't match insertion order, so the old index can now
    // point at an unrelated profile (or nothing) once the vectors are replaced.
    void updateProfileList(const std::vector<std::string>& paths,
                           const std::vector<std::string>& names);

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
    // Set when saveProfile() fails (e.g. data/profiles/ missing) — save() must check its
    // return value instead of assuming success, since it never throws on this failure.
    std::string m_profSaveError;

    ID3D11Device*               m_device     = nullptr;
    PadEngine*                  m_engine     = nullptr;
    std::vector<ControllerConfig> m_configs;
    std::vector<PadLayout>      m_layouts;
    std::vector<std::string>    m_acceptedXbox;
    float                       m_stickSelectThreshold = 0.85f;
    int                         m_stickHoldMs          = 2000;
    float                       m_gyroSelectThreshold  = 0.24f;
    float                       m_accelSelectThreshold = 0.5f;

    MappingModel     m_model;
    MappingSelection m_sel;

    // Macro name cache (loaded lazily from data/macros.json)
    std::vector<std::string>                           m_macroNames;
    std::vector<std::pair<std::string,std::string>>    m_macroLibrary;
    bool                                               m_macroNamesLoaded = false;

    // Inline macro modal
    MacroCreatorModal m_macroModal;
    struct MacroModalPending {
        enum class Ctx { None, Button, Axis, Trigger, Gyro } ctx = Ctx::None;
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
    void onGyroArrowHit(int arrowComp, const std::string& dir);
    void onPhysButtonHit(PadView& phys, int physHit);
    void onPhysStickHit(int physHit);
    void onPhysDpadHit(PadView& phys, int physHit, ImVec2 mouse);
    void onVirtHitPhysButton(PadView& phys, PadView& virt, ImVec2 mouse);
    void onVirtHitPhysStick(PadView& phys, PadView& virt, ImVec2 mouse);
    void onVirtHitTriggerSrc(PadView& virt, ImVec2 mouse);
    void onVirtArrowHit(PadView& phys, PadView& virt, int virtComp, const std::string& dir);
    void onVirtHitAxisAction(PadView& phys, PadView& virt, ImVec2 mouse);
    void onVirtHitGyroAction(PadView& phys, PadView& virt, ImVec2 mouse);

    // PURE lookup, no side effects — safe to call every frame for display purposes as well as
    // right before a write. Resolves which map (m_model.gyroActionEdits or accelActionEdits) a
    // gyro-widget logical direction ("up"/"down"/"left"/"right"/"cw"/"ccw") should read/write for
    // the given HalfAxisActionType, and writes the native key for that sensor to outKey (gyro and
    // accel use different keys for the same direction — see PhysicalAccel's comment in
    // ComponentTypes.h). cw/ccw always resolve to gyro (accel can't sense yaw). Otherwise:
    // m_sel.imuSourceOverridden wins if set, else the type's own default (Dpad/StickSlot/Trigger
    // -> accel, everything else -> gyro).
    std::unordered_map<std::string, HalfAxisAction>& resolveImuTargetMap(
        const std::string& dir, HalfAxisActionType targetType, std::string& outKey);

    // Erases the OTHER sensor's entry for this direction — call right before actually committing
    // a new assignment (not on every frame) so a direction only ever has one active source.
    void clearImuOtherMap(const std::string& dir, bool chosenIsAccel);

    // Resolve + clear-other + write in one step, for the common "assign this action outright"
    // call sites (macro/keyboard/mouse-click/bot/mouse-move). Toggle-off-if-already-same sites
    // (onVirtHitGyroAction, Ranges, macro-inline modal) do their own read-then-write instead.
    void assignImuAction(const std::string& dir, const HalfAxisAction& ha);
};
