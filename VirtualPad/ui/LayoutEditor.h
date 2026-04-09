#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include "PadView.h"
#include "PadLayout.h"
#include "BindingWizard.h"
#include "../GamepadState.h"
#include "../imgui/imgui.h"

// Visual editor for pad_layouts.json.
// Renders a three-pane UI: layout list (left), canvas (center), properties (right).
// The caller is responsible for calling render() every frame while the Layout tab is active.
class LayoutEditor {
public:
    // Call once after D3D device creation.  layouts is the AppWindow's live list — the
    // editor reads and writes it directly.  layoutsPath is the on-disk path for saving.
    void init(ID3D11Device* device,
              std::vector<PadLayout>* layouts,
              const std::string& layoutsPath = "data/pad_layouts.json");

    // Call every frame inside the Layout tab.
    void render();

    // Returns true (once) after the binding wizard has saved a new controller entry.
    bool pollControllersSaved() { return m_wizard.pollSaved(); }

    // Returns true (once) after the layout editor has saved changes to pad_layouts.json.
    bool pollLayoutSaved() { bool v = m_layoutSaved; m_layoutSaved = false; return v; }

    // Release D3D resources held by the internal canvas view.
    void unload();

private:
    // ── Sub-panels ──────────────────────────────────────────────────────────
    void renderLeftPanel(float w);
    void renderCanvas();
    void renderRightPanel(float w);
    void renderSavePopup();

    // ── Editor actions ───────────────────────────────────────────────────────
    void startEditing(int layoutIndex);
    void startNew();
    void startCopy(int layoutIndex);
    void addComponent(const char* type);
    void trySave();
    void discardChanges();
    void ensureBackup();
    void reloadCanvasTextures();
    void renderToast();
    void showToast(std::string msg, bool isError = false);

    // ── UI helpers ───────────────────────────────────────────────────────────
    void        loadImageList();
    // preferredFolder: shown first in the combo (e.g. "buttons"). Pass "" to show all equally.
    bool        comboImage(const char* label, std::string& value,
                           const char* preferredFolder = "");
    bool        stateCombo(const char* label, std::string& value);

    // ── State ────────────────────────────────────────────────────────────────
    ID3D11Device*           m_device      = nullptr;
    std::vector<PadLayout>* m_layouts     = nullptr;
    std::string             m_layoutsPath;

    int       m_selectedLayout = -1;   // index into *m_layouts (-1 = none selected)
    int       m_selectedComp   = -1;   // index into m_editLayout.components
    bool      m_isEditing      = false;
    bool      m_isNew          = false;
    PadLayout m_editLayout;            // working copy while editing
    PadView   m_canvasView;            // D3D view used for editor canvas
    GamepadState m_emptyState{};       // all-zero — components drawn in inactive state

    // Drag (move component by mouse)
    bool    m_dragging      = false;
    ImVec2  m_dragStart     = {};
    float   m_dragOrigCx    = 0.0f;
    float   m_dragOrigCy    = 0.0f;
    ImVec2  m_canvasOrigin  = {};      // screen-space origin at last render; used for hitTest

    // Persistence
    bool        m_backupDone  = false;
    bool        m_lockAspect         = false;
    float       m_lockedRatio        = 1.0f;   // w/h captured when lock is enabled
    bool        m_lockOverlayAspect  = false;
    float       m_lockedOverlayRatio = 1.0f;   // ov_x/ov_y captured when lock is enabled
    bool        m_dirty              = false;   // unsaved changes in m_editLayout
    bool        m_layoutSaved        = false;   // set to true after a successful save; polled by AppWindow
    int         m_pendingSwitchIdx   = -1;      // layout index awaiting confirm-switch popup
    char        m_newIdBuf[64]{};
    bool        m_showIdPopup = false;
    std::string m_statusMsg;
    bool        m_statusIsError  = false;
    double      m_toastExpireTime = 0.0;
    BindingWizard m_wizard;

    // Image files grouped by fixed subfolders
    static constexpr const char* kImageFolders[] = {
        "templates", "cross", "buttons", "analogics", "decorations"
    };
    static constexpr int kImageFolderCount = 5;

    struct ImageFolder { std::string name; std::vector<std::string> files; };
    std::vector<ImageFolder> m_imageFolders;   // one entry per kImageFolders slot
    bool                     m_imageFilesLoaded = false;
};
