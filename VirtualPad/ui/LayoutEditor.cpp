#include "LayoutEditor.h"
#include "../config/Strings.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <windows.h>

#include "../imgui/imgui.h"
#include "../config/ConfigLoader.h"

// ---------------------------------------------------------------------------
// State-name table
// ---------------------------------------------------------------------------

static const char* kStateGroups[][16] = {
    { "── Cara ──",     "btnA","btnB","btnX","btnY", nullptr },
    { "── Hombros ──",  "btnLB","btnRB","triggerL","triggerR", nullptr },
    { "── Extras ──",   "btnL3","btnR3","btnL4","btnR4","btnLP","btnRP",
                        "btnBack","btnStart","btnHome", nullptr },
    { "── D-pad ──",    "dpadUp","dpadDown","dpadLeft","dpadRight", nullptr },
    { "── Ejes ──",     "leftX","leftY","rightX","rightY", nullptr },
    { "── Giroscopio ──", "gyroX","gyroY","gyroZ", nullptr },
};
static const int kGroupCount = (int)(sizeof(kStateGroups) / sizeof(kStateGroups[0]));

static const char* kStateGroupKeys[] = {
    "layout.state_group_face",
    "layout.state_group_shoulder",
    "layout.state_group_extras",
    "layout.state_group_dpad",
    "layout.state_group_axes",
    "layout.state_group_gyro",
};

static bool isKnownState(const std::string& s) {
    if (s.empty()) return true;
    for (int g = 0; g < kGroupCount; ++g)
        for (int i = 1; kStateGroups[g][i]; ++i)
            if (s == kStateGroups[g][i]) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void LayoutEditor::init(ID3D11Device* device,
                        std::vector<PadLayout>* layouts,
                        const std::string& layoutsPath) {
    m_device      = device;
    m_layouts     = layouts;
    m_layoutsPath = layoutsPath;
    m_canvasView.load(device);
    m_wizard.init(device, "data/controllers.json", "data/state_map.json");
}

void LayoutEditor::unload() {
    m_wizard.unload();
    m_canvasView.unload();
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void LayoutEditor::render() {
    if (m_wizard.isActive()) {
        m_wizard.render();
        renderToast();
        return;
    }

    constexpr float kLeftW  = 210.0f;
    constexpr float kRightW = 260.0f;

    renderLeftPanel(kLeftW);
    ImGui::SameLine();

    // Middle canvas — takes remaining width
    float avail  = ImGui::GetContentRegionAvail().x;
    float canvasW = avail - kRightW - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("##LECanvas", { canvasW, 0.0f }, false);
    renderCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    renderRightPanel(kRightW);

    // Save-as popup (modal)
    renderSavePopup();

    // Floating toast
    renderToast();
}

// ---------------------------------------------------------------------------
// Left panel
// ---------------------------------------------------------------------------

void LayoutEditor::renderLeftPanel(float w) {
    ImGui::BeginChild("##LELeft", { w, 0.0f }, true);

    // ── Layout list (scrollable) ─────────────────────────────────────────────
    ImGui::SeparatorText(tr("layout.title"));

    float lineH = ImGui::GetFrameHeightWithSpacing();
    int   nLayouts = m_layouts ? (int)m_layouts->size() : 0;
    float layoutListH = (nLayouts > 0)
        ? std::clamp((float)nLayouts * lineH, lineH, lineH * 4.0f)
        : lineH;

    bool wantConfirmPopup = false;
    ImGui::BeginChild("##LayoutList", { -1.0f, layoutListH }, true);
    if (m_layouts) {
        for (int i = 0; i < nLayouts; ++i) {
            const auto& L = (*m_layouts)[i];
            bool sel = (m_selectedLayout == i);
            if (ImGui::Selectable(L.id.c_str(), sel)) {
                if (i != m_selectedLayout) {
                    if (m_dirty) {
                        m_pendingSwitchIdx = i;
                        wantConfirmPopup   = true;
                    } else {
                        startEditing(i);
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    // OpenPopup must be called from the same window context as BeginPopupModal
    if (wantConfirmPopup) ImGui::OpenPopup("##confirm_switch");

    // Confirm switch popup
    if (ImGui::BeginPopupModal("##confirm_switch", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", tr("layout.confirm_switch"));
        ImGui::Text("%s", tr("layout.unsaved"));
        ImGui::Spacing();
        if (ImGui::Button(tr("btn.no"), { 120.0f, 0.0f })) {
            m_pendingSwitchIdx = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("layout.confirm_yes"), { 120.0f, 0.0f })) {
            startEditing(m_pendingSwitchIdx);
            m_pendingSwitchIdx = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    float hw2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(trid("btn.new", "newlayout").c_str(), { hw2, 0.0f }))
        startNew();
    ImGui::SameLine();
    bool canCopy = (m_selectedLayout >= 0 && m_layouts &&
                    m_selectedLayout < (int)m_layouts->size());
    if (!canCopy) ImGui::BeginDisabled();
    if (ImGui::Button(trid("btn.copy", "copylayout").c_str(), { hw2, 0.0f }))
        startCopy(m_selectedLayout);
    if (!canCopy) ImGui::EndDisabled();

    // Delete layout popup
    if (ImGui::BeginPopupModal("##confirm_delete", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(tr("layout.confirm_delete"), m_editLayout.id.c_str());
        ImGui::Text("%s", tr("layout.delete_warning"));
        ImGui::Spacing();
        if (ImGui::Button(tr("btn.cancel"), { 120.0f, 0.0f }))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(tr("btn.delete"), { 120.0f, 0.0f })) {
            if (m_layouts && m_selectedLayout >= 0 &&
                m_selectedLayout < (int)m_layouts->size()) {
                m_layouts->erase(m_layouts->begin() + m_selectedLayout);
                try {
                    savePadLayouts(m_layoutsPath, *m_layouts);
                    showToast(tr("layout.toast_deleted"));
                } catch (const std::exception& e) {
                    showToast(std::string(tr("layout.toast_err_delete")) + e.what(), true);
                }
                m_selectedLayout = -1;
                m_isEditing      = false;
                m_isNew          = false;
                m_selectedComp   = -1;
                m_dirty          = false;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ── Element list (only while editing) ────────────────────────────────────
    if (!m_isEditing) {
        ImGui::EndChild();
        return;
    }

    ImGui::SeparatorText(tr("layout.elements"));

    // Reserve space for the fixed buttons at the bottom:
    // 2 half-width rows + 1 full-width row + separator + Guardar + Descartar cambios + Borrar layout + separator + Emparejar
    float bottomH = lineH * 8.0f + ImGui::GetStyle().ItemSpacing.y * 4.0f
                  + ImGui::GetStyle().SeparatorTextBorderSize * 2.0f + 6.0f;
    float elemListH = ImGui::GetContentRegionAvail().y - bottomH;
    if (elemListH < lineH * 2.0f) elemListH = lineH * 2.0f;

    ImGui::BeginChild("##ElemList", { -1.0f, elemListH }, false);
    for (int i = 0; i < (int)m_editLayout.components.size(); ++i) {
        const auto& c = m_editLayout.components[i];
        char label[128];
        snprintf(label, sizeof(label), "[%s] %s##ec%d",
                 c.type.c_str(), c.id.c_str(), i);
        bool sel = (m_selectedComp == i);
        if (ImGui::Selectable(label, sel))
            m_selectedComp = i;
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Add-component buttons — always visible
    float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(tr("layout.add_button"), { hw, 0.0f })) addComponent("button");
    ImGui::SameLine();
    if (ImGui::Button(tr("layout.add_dpad"), { hw, 0.0f })) addComponent("dpad");
    if (ImGui::Button(tr("layout.add_stick"), { hw, 0.0f })) addComponent("stick");
    ImGui::SameLine();
    if (ImGui::Button(tr("layout.add_deco"), { hw, 0.0f })) addComponent("decoration");
    if (ImGui::Button(tr("layout.add_analog_dpad"), { hw, 0.0f })) addComponent("analog_dpad");
    ImGui::SameLine();
    if (ImGui::Button(tr("layout.add_gyro"), { hw, 0.0f })) addComponent("gyro");

    ImGui::Separator();

    if (ImGui::Button(trid("btn.save", "left").c_str(), { -1.0f, 0.0f }))          trySave();
    if (ImGui::Button(trid("layout.discard", "left").c_str(), { -1.0f, 0.0f })) discardChanges();
    if (ImGui::Button(trid("layout.delete_layout", "left").c_str(), { -1.0f, 0.0f }))
        ImGui::OpenPopup("##confirm_delete");

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button(trid("layout.pair", "left").c_str(), { -1.0f, 0.0f }))
        m_wizard.start(m_editLayout);

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

void LayoutEditor::renderCanvas() {
    if (!m_isEditing) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("layout.hint"));
        return;
    }

    if (!m_imageFilesLoaded) loadImageList();

    const PadLayout& L = m_editLayout;
    ImDrawList* dl     = ImGui::GetWindowDrawList();
    m_canvasOrigin     = ImGui::GetCursorScreenPos();

    // Zone backgrounds (drawn before components so they appear behind)
    ImVec2 frontTL = m_canvasOrigin;
    ImVec2 frontBR = { m_canvasOrigin.x + L.W, m_canvasOrigin.y + L.FrontH };
    ImVec2 topTL   = { m_canvasOrigin.x,        frontBR.y };
    ImVec2 topBR   = { m_canvasOrigin.x + L.W, m_canvasOrigin.y + L.FrontH + L.TopH };

    dl->AddRectFilled(frontTL, frontBR, IM_COL32(38, 38, 55, 255));
    dl->AddRect      (frontTL, frontBR, IM_COL32(70, 70, 120, 180), 0.0f, 0, 1.0f);
    dl->AddText      ({ frontTL.x + 4.0f, frontTL.y + 3.0f },
                      IM_COL32(90, 90, 140, 200), tr("layout.zone_front"));

    dl->AddRectFilled(topTL, topBR, IM_COL32(33, 42, 38, 255));
    dl->AddRect      (topTL, topBR, IM_COL32(60, 100, 70, 180), 0.0f, 0, 1.0f);
    dl->AddText      ({ topTL.x + 4.0f, topTL.y + 3.0f },
                      IM_COL32(80, 120, 90, 200), tr("layout.zone_top"));

    // Render components
    m_canvasView.render(m_emptyState, m_selectedComp);
    // render() advanced the cursor with Dummy({W, FrontH+TopH})

    // ── Mouse interaction ────────────────────────────────────────────────────
    ImVec2 mouse      = ImGui::GetIO().MousePos;
    bool   inCanvas   = mouse.x >= m_canvasOrigin.x &&
                        mouse.x <= m_canvasOrigin.x + L.W &&
                        mouse.y >= m_canvasOrigin.y &&
                        mouse.y <= m_canvasOrigin.y + L.FrontH + L.TopH;
    bool   winHovered = ImGui::IsWindowHovered();

    if (inCanvas && winHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int hit = m_canvasView.hitTest(mouse, m_canvasOrigin);
        m_selectedComp = hit;
        // Templates are selectable but not draggable
        if (hit >= 0 && m_editLayout.components[hit].type != "template") {
            m_dragging    = true;
            m_dragStart   = mouse;
            m_dragOrigCx  = m_editLayout.components[hit].cx;
            m_dragOrigCy  = m_editLayout.components[hit].cy;
        }
    }

    if (m_dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_selectedComp >= 0) {
            float dx = mouse.x - m_dragStart.x;
            float dy = mouse.y - m_dragStart.y;
            m_editLayout.components[m_selectedComp].cx = m_dragOrigCx + dx;
            m_editLayout.components[m_selectedComp].cy = m_dragOrigCy + dy;
            m_canvasView.updateLayout(m_editLayout);
            m_dirty = true;
        } else {
            m_dragging = false;
        }
    }

    // Arrow-key nudge: 1px per step, only for non-template selected elements
    if (m_selectedComp >= 0 &&
        m_selectedComp < (int)m_editLayout.components.size() &&
        m_editLayout.components[m_selectedComp].type != "template" &&
        ImGui::IsWindowFocused()) {
        auto& comp = m_editLayout.components[m_selectedComp];
        bool moved = false;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  { comp.cx -= 1.0f; moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { comp.cx += 1.0f; moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    { comp.cy -= 1.0f; moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  { comp.cy += 1.0f; moved = true; }
        if (moved) { m_canvasView.updateLayout(m_editLayout); m_dirty = true; }
    }
}

// ---------------------------------------------------------------------------
// Right panel — component properties
// ---------------------------------------------------------------------------

void LayoutEditor::renderRightPanel(float w) {
    ImGui::BeginChild("##LERight", { w, 0.0f }, true);

    if (!m_isEditing) {
        ImGui::TextDisabled("%s", tr("layout.no_active"));
        ImGui::EndChild();
        return;
    }

    if (m_selectedComp < 0 || m_selectedComp >= (int)m_editLayout.components.size()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("layout.click_hint"));
        ImGui::EndChild();
        return;
    }

    PadComponent& c = m_editLayout.components[m_selectedComp];

    ImGui::Spacing();
    ImGui::Text(tr("layout.element_n"), m_selectedComp);
    ImGui::Separator();
    ImGui::LabelText(tr("layout.label_type"), "%s", c.type.c_str());
    ImGui::LabelText(tr("layout.label_view"), "%s", c.view.c_str());

    // Templates: only image + tint color, no position/size controls
    if (c.type == "template") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.images"));
        bool imageChanged = comboImage("image##tpl", c.image, "templates");
        if (imageChanged) reloadCanvasTextures();

        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.tint"));  ImGui::SameLine();
        float col[4] = { c.colorR, c.colorG, c.colorB, c.colorA };
        if (ImGui::ColorEdit4("##tce", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float)) {
            c.colorR = col[0]; c.colorG = col[1];
            c.colorB = col[2]; c.colorA = col[3];
        }
        m_canvasView.updateLayout(m_editLayout);
        ImGui::EndChild();
        return;
    }

    // ID — combo with type-appropriate suggestions + free text
    {
        static const char* kBtnIds[]  = { "btnA","btnB","btnX","btnY",
                                          "btnLB","btnRB","triggerL","triggerR",
                                          "btnL3","btnR3","btnL4","btnR4",
                                          "btnLP","btnRP","btnBack","btnStart","btnHome",
                                          nullptr };
        static const char* kStickIds[]= { "leftStick","rightStick", nullptr };
        static const char* kDpadIds[] = { "dpad", nullptr };
        static const char* kDecoIds[] = { "logo","usbPort","decoration","label", nullptr };

        const char** sugg = (c.type == "stick")      ? kStickIds :
                            (c.type == "dpad")        ? kDpadIds  :
                            (c.type == "decoration")  ? kDecoIds  : kBtnIds;

        // Unique widget ID per component index — prevents state sharing
        char comboId[32];
        snprintf(comboId, sizeof(comboId), "ID##id_%d", m_selectedComp);
        char inputId[32];
        snprintf(inputId, sizeof(inputId), "##idinput_%d", m_selectedComp);

        const char* preview = c.id.empty() ? tr("action.no_id") : c.id.c_str();
        if (ImGui::BeginCombo(comboId, preview)) {
            for (int i = 0; sugg[i]; ++i) {
                bool sel = (c.id == sugg[i]);
                if (ImGui::Selectable(sugg[i], sel)) c.id = sugg[i];
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", tr("layout.custom"));
            char idBuf[64] = {};
            strncpy_s(idBuf, c.id.c_str(), sizeof(idBuf) - 1);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(inputId, idBuf, sizeof(idBuf)))
                c.id = idBuf;
            ImGui::EndCombo();
        }
    }

    // View
    int viewIdx = (c.view == "front") ? 0 : 1;
    const char* viewItems[] = { tr("layout.view_front"), tr("layout.view_top") };
    if (ImGui::Combo(tr("layout.label_view"), &viewIdx, viewItems, 2))
        c.view = (viewIdx == 0) ? "front" : "top";

    // Position
    ImGui::Spacing();
    ImGui::Text("%s", tr("layout.pos_size"));
    ImGui::DragFloat("cx", &c.cx, 0.5f, 0.0f, 0.0f, "%.1f");
    ImGui::DragFloat("cy", &c.cy, 0.5f, 0.0f, 0.0f, "%.1f");

    if (c.type == "stick" || c.type == "gyro") {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("size",       &c.size,      0.5f, 1.0f, 500.0f, "size: %.1f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("max_offset", &c.maxOffset, 0.5f, 0.0f, 100.0f, "max_offset: %.1f");
    } else if (c.type == "dpad" || c.type == "analog_dpad") {
        if (c.size <= 0.0f) c.size = 1.0f;  // initialize for editing (0 = natural = 1.0)
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("scale", &c.size, 0.01f, 0.1f, 5.0f, "scale: %.2f");
    } else {
        bool prevLock = m_lockAspect;
        float origW = c.w, origH = c.h;
        ImGui::Checkbox(tr("layout.keep_ratio"), &m_lockAspect);
        bool wChanged = ImGui::DragFloat("w", &c.w, 0.5f, 1.0f, 2000.0f, "%.1f");
        bool hChanged = ImGui::DragFloat("h", &c.h, 0.5f, 1.0f, 2000.0f, "%.1f");
        bool lockJustEnabled = m_lockAspect && !prevLock;
        if (lockJustEnabled)
            m_lockedRatio = (origH > 0.01f) ? origW / origH : 1.0f;
        if (m_lockAspect && !lockJustEnabled && m_lockedRatio > 0.001f) {
            if      (wChanged) c.h = c.w / m_lockedRatio;
            else if (hChanged) c.w = c.h * m_lockedRatio;
        }
    }

    // Images (gyro has none)
    if (c.type != "gyro") {
    ImGui::Spacing();
    ImGui::Text("%s", tr("layout.images"));

    bool imageChanged = false;
    if (c.type == "dpad" || c.type == "analog_dpad") {
        imageChanged |= comboImage("image_up",    c.imageUp,    "cross");
        imageChanged |= comboImage("image_down",  c.imageDown,  "cross");
        imageChanged |= comboImage("image_left",  c.imageLeft,  "cross");
        imageChanged |= comboImage("image_right", c.imageRight, "cross");
    } else {
        const char* imgFolder = (c.type == "stick") ? "analogics" : "buttons";
        if (comboImage("image", c.image, imgFolder)) {
            // Auto-fill w/h from texture natural size for button/decoration
            if (c.type == "button" || c.type == "decoration") {
                reloadCanvasTextures();
                int tw = 0, th = 0;
                if (m_canvasView.getTextureSize(c.image, tw, th)) {
                    c.w = (float)tw;
                    c.h = (float)th;
                }
            } else {
                imageChanged = true;
            }
        }
        if (c.type == "button") {
            imageChanged |= comboImage("overlay", c.overlay, "decorations");
            bool prevLockOv = m_lockOverlayAspect;
            ImGui::Checkbox(trid("layout.keep_ratio", "ov").c_str(), &m_lockOverlayAspect);
            bool oxChanged = ImGui::DragFloat("ov_x", &c.overlayScaleX, 0.005f, 0.0f, 4.0f, "%.3f");
            bool oyChanged = ImGui::DragFloat("ov_y", &c.overlayScaleY, 0.005f, 0.0f, 4.0f, "%.3f");
            bool lockOvJustEnabled = m_lockOverlayAspect && !prevLockOv;
            if (lockOvJustEnabled)
                m_lockedOverlayRatio = (c.overlayScaleY > 0.001f) ? c.overlayScaleX / c.overlayScaleY : 1.0f;
            if (m_lockOverlayAspect && !lockOvJustEnabled && m_lockedOverlayRatio > 0.001f) {
                if      (oxChanged) c.overlayScaleY = c.overlayScaleX / m_lockedOverlayRatio;
                else if (oyChanged) c.overlayScaleX = c.overlayScaleY * m_lockedOverlayRatio;
            }
        }
        if (c.type == "decoration") {
            imageChanged |= comboImage("overlay", c.overlay, "decorations");
        }
    }
    if (imageChanged) reloadCanvasTextures();
    } // end if (c.type != "gyro") for images block

    // State bindings
    if (c.type == "button") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.state"));
        stateCombo("state##s", c.state);
    } else if (c.type == "stick") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.states"));
        stateCombo("state_x##sx",     c.stateX);
        stateCombo("state_y##sy",     c.stateY);
        stateCombo("state_click##sc", c.stateClick);
    } else if (c.type == "analog_dpad") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.states"));
        stateCombo("state_x##ax", c.stateX);
        stateCombo("state_y##ay", c.stateY);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("threshold##athr", &c.threshold, 0.01f, 0.1f, 0.9f, "thr: %.2f");
    } else if (c.type == "dpad") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.states"));
        stateCombo("state_up##su",    c.stateUp);
        stateCombo("state_down##sd",  c.stateDown);
        stateCombo("state_left##sl",  c.stateLeft);
        stateCombo("state_right##sr", c.stateRight);
    } else if (c.type == "gyro") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.gyro_axes"));
        ImGui::TextDisabled("%s", tr("layout.gyro_h"));
        stateCombo("state_x##gx", c.stateX);
        ImGui::TextDisabled("%s", tr("layout.gyro_v"));
        stateCombo("state_y##gy", c.stateY);
    }

    // Colors (gyro renders with fixed colors, nothing to edit)
    if (c.type == "gyro") {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.55f, 0.08f, 0.08f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.75f, 0.15f, 0.15f, 1.0f });
        if (ImGui::Button(tr("layout.delete_elem"), { -1.0f, 0.0f })) {
            m_editLayout.components.erase(
                m_editLayout.components.begin() + m_selectedComp);
            m_selectedComp = -1;
            m_dirty = true;
            reloadCanvasTextures();
        }
        ImGui::PopStyleColor(2);
        m_canvasView.updateLayout(m_editLayout);
        if (ImGui::IsAnyItemActive()) m_dirty = true;
        ImGui::EndChild();
        return;
    }

    // Colors -- compact row: one square per color with short label
    ImGui::Spacing();
    ImGui::Text("%s", tr("layout.colors"));
    ImGui::Separator();

    constexpr auto kNoIn = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float;

    {   // base color — always shown
        float v[4] = { c.colorR, c.colorG, c.colorB, c.colorA };
        ImGui::Text("%s", tr("layout.color_base"));  ImGui::SameLine();
        if (ImGui::ColorEdit4("##ce", v, kNoIn))
            { c.colorR=v[0]; c.colorG=v[1]; c.colorB=v[2]; c.colorA=v[3]; }
    }

    if (c.type == "button" || c.type == "stick" || c.type == "dpad" || c.type == "analog_dpad") {
        ImGui::SameLine(0, 12);
        float v[4] = { c.activeColorR, c.activeColorG, c.activeColorB, c.activeColorA };
        ImGui::Text("%s", tr("layout.color_active"));  ImGui::SameLine();
        if (ImGui::ColorEdit4("##ace", v, kNoIn))
            { c.activeColorR=v[0]; c.activeColorG=v[1]; c.activeColorB=v[2]; c.activeColorA=v[3]; }
    }

    if (c.type == "button") {
        ImGui::Spacing();
        ImGui::Text("%s", tr("layout.color_overlay"));  ImGui::SameLine();
        {
            float v[4] = { c.ovColorR, c.ovColorG, c.ovColorB, c.ovColorA };
            if (ImGui::ColorEdit4("##oc", v, kNoIn))
                { c.ovColorR=v[0]; c.ovColorG=v[1]; c.ovColorB=v[2]; c.ovColorA=v[3]; }
        }
        ImGui::SameLine(0, 12);
        ImGui::Text("%s", tr("layout.color_active"));  ImGui::SameLine();
        {
            float v[4] = { c.activeOvColorR, c.activeOvColorG,
                           c.activeOvColorB, c.activeOvColorA };
            if (ImGui::ColorEdit4("##aoc", v, kNoIn))
                { c.activeOvColorR=v[0]; c.activeOvColorG=v[1];
                  c.activeOvColorB=v[2]; c.activeOvColorA=v[3]; }
        }
    }

    // Delete
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.55f, 0.08f, 0.08f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.75f, 0.15f, 0.15f, 1.0f });
    if (ImGui::Button(tr("layout.delete_elem"), { -1.0f, 0.0f })) {
        m_editLayout.components.erase(
            m_editLayout.components.begin() + m_selectedComp);
        m_selectedComp = -1;
        m_dirty = true;
        reloadCanvasTextures();
    }
    ImGui::PopStyleColor(2);

    // Sync all property changes (colors, states, positions) to the canvas view
    m_canvasView.updateLayout(m_editLayout);
    if (ImGui::IsAnyItemActive()) m_dirty = true;

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Save popup
// ---------------------------------------------------------------------------

void LayoutEditor::renderSavePopup() {
    if (m_showIdPopup) {
        ImGui::OpenPopup(trid("layout.new_id_title", "idpop").c_str());
        m_showIdPopup = false;
    }

    if (ImGui::BeginPopupModal(trid("layout.new_id_title", "idpop").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", tr("layout.new_id_prompt"));
        ImGui::Spacing();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("##nid", m_newIdBuf, sizeof(m_newIdBuf));
        ImGui::Spacing();

        // Validate: non-empty and not already used
        std::string newId = m_newIdBuf;
        bool conflict = false;
        if (m_layouts)
            for (const auto& L : *m_layouts)
                if (L.id == newId) { conflict = true; break; }

        if (conflict)
            ImGui::TextColored({ 1.0f,0.4f,0.4f,1.0f }, "%s", tr("layout.id_exists"));

        bool canSave = !newId.empty() && !conflict;

        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button(trid("btn.save", "popup").c_str(), { 120.0f, 0.0f })) {
            ensureBackup();
            m_editLayout.id = newId;
            if (m_layouts) {
                m_layouts->push_back(m_editLayout);
                m_selectedLayout = (int)m_layouts->size() - 1;
            }
            try {
                savePadLayouts(m_layoutsPath, *m_layouts);
                m_layoutSaved = true;
                showToast(std::string(tr("layout.toast_saved_new")) + newId);
            } catch (const std::exception& e) {
                showToast(std::string(tr("layout.toast_err_save")) + e.what(), true);
            }
            m_isNew  = false;
            m_dirty  = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canSave) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(trid("btn.cancel", "popup").c_str(), { 120.0f, 0.0f }))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Editor actions
// ---------------------------------------------------------------------------

void LayoutEditor::startEditing(int index) {
    if (!m_layouts || index < 0 || index >= (int)m_layouts->size()) return;
    m_editLayout     = (*m_layouts)[index];
    m_selectedLayout = index;
    m_selectedComp   = -1;
    m_dragging       = false;
    m_isEditing      = true;
    m_isNew          = false;
    m_dirty          = false;
    reloadCanvasTextures();
}

void LayoutEditor::startNew() {
    m_dirty             = false;
    m_editLayout        = {};
    m_editLayout.id     = "__new__";
    m_editLayout.W      = 480.0f;
    m_editLayout.FrontH = 200.0f;
    m_editLayout.TopH   = 320.0f;

    // Auto-generate the two mandatory template backgrounds
    PadComponent tFront;
    tFront.id   = "body_front";
    tFront.type = "template";
    tFront.view = "front";
    tFront.cx   = m_editLayout.W * 0.5f;
    tFront.cy   = m_editLayout.FrontH * 0.5f;
    tFront.w    = m_editLayout.W;
    tFront.h    = m_editLayout.FrontH;
    m_editLayout.components.push_back(tFront);

    PadComponent tTop;
    tTop.id   = "body_top";
    tTop.type = "template";
    tTop.view = "top";
    tTop.cx   = m_editLayout.W * 0.5f;
    tTop.cy   = m_editLayout.FrontH + m_editLayout.TopH * 0.5f;
    tTop.w    = m_editLayout.W;
    tTop.h    = m_editLayout.TopH;
    m_editLayout.components.push_back(tTop);

    m_selectedComp = -1;
    m_dragging     = false;
    m_isEditing    = true;
    m_isNew        = true;
    m_newIdBuf[0]  = '\0';
    reloadCanvasTextures();
}

void LayoutEditor::startCopy(int index) {
    if (!m_layouts || index < 0 || index >= (int)m_layouts->size()) return;
    m_editLayout     = (*m_layouts)[index];
    m_editLayout.id  = "__new__";
    m_selectedComp   = -1;
    m_dragging       = false;
    m_isEditing      = true;
    m_isNew          = true;
    m_dirty          = false;
    m_newIdBuf[0]    = '\0';
    m_showIdPopup    = true;
    reloadCanvasTextures();
}

void LayoutEditor::addComponent(const char* type) {
    PadComponent c;
    c.type = type;
    c.view = "top";
    c.id   = std::string(type) + "_new";
    c.cx   = m_editLayout.W * 0.5f;
    c.cy   = m_editLayout.FrontH + m_editLayout.TopH * 0.5f;

    if (strcmp(type, "template") == 0) {
        c.w  = m_editLayout.W;
        c.h  = m_editLayout.TopH;
    } else if (strcmp(type, "stick") == 0 || strcmp(type, "gyro") == 0) {
        c.size      = 60.0f;
        c.maxOffset = 20.0f;
        if (strcmp(type, "gyro") == 0) {
            c.stateX = "gyroZ";
            c.stateY = "gyroX";
        }
    } else if (strcmp(type, "dpad") == 0 || strcmp(type, "analog_dpad") == 0) {
        c.size = 1.0f;  // scale factor: 1.0 = natural texture size
    } else {
        c.w = 50.0f;
        c.h = 50.0f;
    }

    m_editLayout.components.push_back(c);
    m_selectedComp = (int)m_editLayout.components.size() - 1;
    m_dirty = true;
    // No texture reload needed: new component has no images yet
    m_canvasView.updateLayout(m_editLayout);
}

void LayoutEditor::trySave() {
    if (m_isNew) {
        // Ask for an ID via modal popup
        m_showIdPopup = true;
        return;
    }

    ensureBackup();

    // Overwrite the layout in the live list
    if (m_layouts && m_selectedLayout >= 0 &&
        m_selectedLayout < (int)m_layouts->size())
        (*m_layouts)[m_selectedLayout] = m_editLayout;

    try {
        savePadLayouts(m_layoutsPath, *m_layouts);
        showToast(tr("layout.toast_saved"));
        m_dirty       = false;
        m_layoutSaved = true;
    } catch (const std::exception& e) {
        showToast(std::string(tr("layout.toast_err_save")) + e.what(), true);
    }
}

void LayoutEditor::discardChanges() {
    m_isEditing    = false;
    m_isNew        = false;
    m_selectedComp = -1;
    m_dragging     = false;
    m_dirty        = false;
}

void LayoutEditor::ensureBackup() {
    if (m_backupDone) return;
    std::string bakPath = m_layoutsPath + ".bak";
    // Never overwrite an existing backup — the user manages it manually
    std::ifstream existingBak(bakPath);
    if (!existingBak.is_open()) {
        std::ifstream src(m_layoutsPath, std::ios::binary);
        if (src.is_open()) {
            std::ofstream dst(bakPath, std::ios::binary);
            dst << src.rdbuf();
        }
    }
    m_backupDone = true;
}


void LayoutEditor::reloadCanvasTextures() {
    m_canvasView.forceSetLayout(m_editLayout);
}

void LayoutEditor::showToast(std::string msg, bool isError) {
    m_statusMsg       = std::move(msg);
    m_statusIsError   = isError;
    m_toastExpireTime = ImGui::GetTime() + 3.0;
}

void LayoutEditor::renderToast() {
    if (m_statusMsg.empty()) return;
    if (ImGui::GetTime() >= m_toastExpireTime) {
        m_statusMsg.clear();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const float pad = 20.0f;
    ImVec2 pos = { io.DisplaySize.x * 0.5f, io.DisplaySize.y - pad };
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, { 0.5f, 1.0f });
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration    |
                             ImGuiWindowFlags_NoNav            |
                             ImGuiWindowFlags_NoMove           |
                             ImGuiWindowFlags_NoInputs         |
                             ImGuiWindowFlags_NoSavedSettings  |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin("##toast", nullptr, flags);
    ImVec4 col = m_statusIsError
               ? ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }
               : ImVec4{ 0.4f, 1.0f, 0.4f, 1.0f };
    ImGui::TextColored(col, "%s", m_statusMsg.c_str());
    ImGui::End();
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

void LayoutEditor::loadImageList() {
    m_imageFolders.clear();
    m_imageFolders.resize(kImageFolderCount);

    for (int i = 0; i < kImageFolderCount; ++i) {
        m_imageFolders[i].name = kImageFolders[i];
        std::string pattern = std::string("images\\") + kImageFolders[i] + "\\*.png";
        WIN32_FIND_DATAA fd{};
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { m_imageFolders[i].files.push_back(fd.cFileName); }
            while (FindNextFileA(h, &fd));
            FindClose(h);
            std::sort(m_imageFolders[i].files.begin(), m_imageFolders[i].files.end());
        }
    }

    m_imageFilesLoaded = true;
}

bool LayoutEditor::comboImage(const char* label, std::string& value,
                              const char* preferredFolder) {
    bool changed = false;
    const char* preview = value.empty() ? tr("layout.no_image") : value.c_str();

    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(tr("layout.no_image"), value.empty())) { value = ""; changed = true; }

        // Helper: render one folder section
        auto renderFolder = [&](const ImageFolder& folder) {
            if (folder.files.empty()) return;
            ImGui::Separator();
            ImGui::TextDisabled("  %s/", folder.name.c_str());
            for (const auto& fname : folder.files) {
                std::string fullPath = folder.name + "/" + fname;
                bool sel = (fullPath == value);
                std::string selId = "  " + fname + "##img_" + fullPath;
                if (ImGui::Selectable(selId.c_str(), sel)) { value = fullPath; changed = true; }
                if (sel) ImGui::SetItemDefaultFocus();
            }
        };

        // Preferred folder first
        if (preferredFolder && preferredFolder[0]) {
            for (const auto& folder : m_imageFolders)
                if (folder.name == preferredFolder) { renderFolder(folder); break; }
        }

        // Rest of the folders
        for (const auto& folder : m_imageFolders)
            if (!preferredFolder || folder.name != preferredFolder)
                renderFolder(folder);

        ImGui::EndCombo();
    }
    return changed;
}

bool LayoutEditor::stateCombo(const char* label, std::string& value) {
    bool changed = false;
    const char* preview = value.empty() ? tr("layout.no_state") : value.c_str();

    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(tr("layout.no_state"), value.empty())) { value = ""; changed = true; }
        ImGui::Separator();

        for (int g = 0; g < kGroupCount; ++g) {
            ImGui::TextDisabled("%s", tr(kStateGroupKeys[g]));
            for (int i = 1; kStateGroups[g][i]; ++i) {
                const char* s   = kStateGroups[g][i];
                bool         sel = (value == s);
                if (ImGui::Selectable(s, sel)) { value = s; changed = true; }
                if (sel) ImGui::SetItemDefaultFocus();
            }
        }

        // Allow free-form entry for values not in the table
        ImGui::Separator();
        ImGui::TextDisabled("%s", tr("layout.custom"));
        char customBuf[64] = {};
        strncpy_s(customBuf, value.c_str(), sizeof(customBuf) - 1);
        std::string inputId = std::string("##pcv_") + label;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText(inputId.c_str(), customBuf, sizeof(customBuf))) {
            value   = customBuf;
            changed = true;
        }

        ImGui::EndCombo();
    }
    return changed;
}
