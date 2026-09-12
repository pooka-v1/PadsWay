#include "MappingEditor.h"
#include "../config/Strings.h"
#include "MappingHelpers.h"
#include "ActionPanel.h"
#include "MappingSourceSelector.h"
#include "../imgui/imgui.h"
#include "../nlohmann/json.hpp"
using json = nlohmann::json;
#include "../config/ConfigLoader.h"
#include "../input/ComponentTypes.h"  // applyDeadzoneMaxSigned — see H9 gyro/accel hold-to-arm
#include "../Paths.h"
#include "../Log.h"

#include <fstream>
#include <algorithm>

// ---------------------------------------------------------------------------
void MappingEditor::init(ID3D11Device* device, PadEngine* engine,
                         const std::vector<PadLayout>& layouts,
                         const std::vector<std::string>& acceptedXbox,
                         float stickSelectThreshold, int stickHoldMs,
                         float gyroSelectThreshold, float accelSelectThreshold) {
    m_device               = device;
    m_engine               = engine;
    m_layouts              = layouts;
    m_acceptedXbox         = acceptedXbox;
    m_stickSelectThreshold = stickSelectThreshold;
    m_stickHoldMs          = stickHoldMs;
    m_gyroSelectThreshold  = gyroSelectThreshold;
    m_accelSelectThreshold = accelSelectThreshold;
    m_macroModal.init(device);
}

void MappingEditor::setConfigs(const std::vector<ControllerConfig>& configs) {
    m_configs = configs;
}

bool MappingEditor::pollConfigsSaved() {
    bool r = m_configsSaved;
    m_configsSaved = false;
    return r;
}

void MappingEditor::unload() {
    m_arrowTex.release();
    for (auto& t : m_gestureIconTex) t.release();
}

// ---------------------------------------------------------------------------
void MappingEditor::activateProfile(const std::vector<std::string>& profilePaths,
                                    const std::vector<std::string>& profileNames,
                                    int preselectedIdx) {
    m_mode         = Mode::kProfile;
    m_active       = true;
    m_profilePaths = profilePaths;
    m_profileNames = profileNames;
    m_profIdx      = preselectedIdx;
    m_profToast    = false;
    memset(m_profNameBuf, 0, sizeof(m_profNameBuf));

    DeviceCandidate dev = m_engine->getActiveDevice();
    m_model.vid = dev.vid;
    m_model.pid = dev.pid;
    m_sel.clear();
    reload();
}

void MappingEditor::reload() {
    if (m_mode == Mode::kProfile) {
        if (m_profIdx >= 0 && m_profIdx < (int)m_profilePaths.size()) {
            DeviceCandidate dev = m_engine->getActiveDevice();
            const ControllerConfig* base =
                findConfig(m_configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);
            if (base) {
                GameProfile profile = loadGameProfile(m_profilePaths[m_profIdx]);
                m_model.loadProfile(*base, profile);
                strncpy_s(m_profNameBuf, profile.profile_name.c_str(), sizeof(m_profNameBuf) - 1);
            }
        } else {
            // New profile: load base config as starting point
            DeviceCandidate dev = m_engine->getActiveDevice();
            const ControllerConfig* base =
                findConfig(m_configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);
            if (base) m_model.reloadFromConfig(*base);
            memset(m_profNameBuf, 0, sizeof(m_profNameBuf));
        }
        m_sel.triggerSrc.clear();
        m_sel.h9HoldTriggerSrc.clear();
        m_sel.h9HoldTriggerTimer = 0.0f;
        return;
    }

    m_model.reload(m_configs);
    m_sel.triggerSrc.clear();
    m_sel.h9HoldTriggerSrc.clear();
    m_sel.h9HoldTriggerTimer = 0.0f;
}

void MappingEditor::updateProfileList(const std::vector<std::string>& paths,
                                      const std::vector<std::string>& names) {
    std::string currentPath = (m_profIdx >= 0 && m_profIdx < (int)m_profilePaths.size())
        ? m_profilePaths[m_profIdx] : std::string();
    m_profilePaths = paths;
    m_profileNames = names;
    if (currentPath.empty()) return;
    auto it = std::find(m_profilePaths.begin(), m_profilePaths.end(), currentPath);
    m_profIdx = (it != m_profilePaths.end()) ? (int)(it - m_profilePaths.begin()) : -1;
}

void MappingEditor::save() {
    if (m_mode == Mode::kProfile) {
        m_profSaveError.clear();
        spdlog::trace("[Profile] save() called, profIdx={}, profilePaths.size()={}",
                     m_profIdx, m_profilePaths.size());
        if (m_profIdx >= 0 && m_profIdx < (int)m_profilePaths.size()) {
            DeviceCandidate dev = m_engine->getActiveDevice();
            const ControllerConfig* base =
                findConfig(m_configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);
            spdlog::trace("[Profile] active device VID={:04X} PID={:04X}, base config {}",
                         dev.vid, dev.pid, base ? "FOUND" : "NOT FOUND");
            if (base) {
                try {
                    bool ok = m_model.saveProfile(m_profilePaths[m_profIdx],
                                                  m_profNameBuf[0] ? m_profNameBuf : m_profileNames[m_profIdx].c_str(),
                                                  *base);
                    spdlog::trace("[Profile] saveProfile('{}') returned {}",
                                 m_profilePaths[m_profIdx], ok);
                    if (ok) {
                        m_engine->requestProfileReload();
                        m_profToast     = true;
                        m_profToastTime = GetTickCount64();
                    } else {
                        m_profSaveError = tr("profiles.save_error");
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("[Profile] saveProfile() threw: {}", e.what());
                    m_profSaveError = tr("profiles.save_error");
                } catch (...) {
                    spdlog::warn("[Profile] saveProfile() threw an unknown exception");
                    m_profSaveError = tr("profiles.save_error");
                }
            } else {
                spdlog::warn("[Profile] save() aborted: no base ControllerConfig found for the active device");
                m_profSaveError = tr("profiles.save_error");
            }
        }
        return;
    }
    try { m_model.save(Paths::userData("data/controllers.json")); } catch (...) {}
    m_configs = loadControllerConfigs(Paths::userData("data/controllers.json"));
    m_engine->reloadConfigs();
    m_configsSaved = true;
}

// ---------------------------------------------------------------------------
// Converts a slot key ("left_y_pos", "right_x_neg", …) to the virtual stick
// component index and arrow direction string needed by renderStickArrows.
static std::pair<int, std::string> slotKeyToArrow(const PadLayout& vLayout, const std::string& sk) {
    bool isLeft  = sk.rfind("left_",  0) == 0;
    bool isRight = sk.rfind("right_", 0) == 0;
    if (!isLeft && !isRight) return {-1, ""};
    size_t off = isLeft ? 5 : 6;
    if (sk.size() < off + 3) return {-1, ""};
    char axis  = sk[off];
    std::string sign = sk.substr(off + 2);
    std::string dir;
    if      (axis == 'x' && sign == "pos") dir = "right";
    else if (axis == 'x' && sign == "neg") dir = "left";
    else if (axis == 'y' && sign == "pos") dir = "up";
    else if (axis == 'y' && sign == "neg") dir = "down";
    else return {-1, ""};
    for (int i = 0; i < (int)vLayout.components.size(); ++i) {
        if (vLayout.components[i].type != "stick") continue;
        const std::string& sx = vLayout.components[i].stateX;
        if ((isLeft  && sx.rfind("left",  0) == 0) ||
            (isRight && sx.rfind("right", 0) == 0))
            return {i, dir};
    }
    return {-1, dir};
}

// ---------------------------------------------------------------------------
// gyroKeyFromDir/accelKeyFromDir/imuDefaultUsesAccel moved to free functions in
// MappingSelection.h (Tarea 3b) — resolveImuTargetMap() (also moved there) needs them, and it is
// called both from this file and from MappingSourceSelector's H9 Paso 2.

// Sustain time required at/above the arm threshold for the gyro/accel progressive-sweep arming
// (see the H9 block in render() and MappingSelection.h's h9ImuSweep). Shared with the
// progress-bar display code so both sides agree on the same duration.
constexpr float kImuConfirmSec = 0.18f;

// The other half of the same logical axis (for MouseMove's bidirectional auto-assign and Clear).
static std::string oppositeGyroDir(const std::string& dir) {
    if (dir == "up")    return "down";
    if (dir == "down")  return "up";
    if (dir == "left")  return "right";
    if (dir == "right") return "left";
    if (dir == "cw")    return "ccw";
    if (dir == "ccw")   return "cw";
    return "";
}

// Reverse of gyroKeyFromDir/accelKeyFromDir — used by the Rangos/macro-inline modals, which only
// carry the already-resolved sensor-specific key (they were opened with it, not the direction).
static std::string dirFromGyroKey(const std::string& k) {
    if (k == "x_pos") return "up";
    if (k == "x_neg") return "down";
    if (k == "z_pos") return "right";
    if (k == "z_neg") return "left";
    if (k == "y_pos") return "cw";
    if (k == "y_neg") return "ccw";
    return "";
}
static std::string dirFromAccelKey(const std::string& k) {
    if (k == "y_pos") return "up";
    if (k == "y_neg") return "down";
    if (k == "x_pos") return "right";
    if (k == "x_neg") return "left";
    return "";
}

// TouchZoneTemplate::id (data/touch_zone_templates.json, e.g. "cross-plus-5-center") -> localized
// display name (strings_*.json). Falls back to the raw id for any template not yet localized
// (new catalog entry added without a matching string key) instead of showing nothing.
static std::string touchZoneTemplateDisplayName(const std::string& templateId) {
    if (templateId == "single-1")            return tr("action.touch_zone_tmpl_single_1");
    if (templateId == "split-lr-2")          return tr("action.touch_zone_tmpl_split_lr_2");
    if (templateId == "cross-x-4")           return tr("action.touch_zone_tmpl_cross_x_4");
    if (templateId == "cross-plus-4")        return tr("action.touch_zone_tmpl_cross_plus_4");
    if (templateId == "cross-x-5-center")    return tr("action.touch_zone_tmpl_cross_x_5_center");
    if (templateId == "cross-plus-5-center") return tr("action.touch_zone_tmpl_cross_plus_5_center");
    if (templateId == "grid-6")              return tr("action.touch_zone_tmpl_grid_6");
    if (templateId == "compass-8-center")    return tr("action.touch_zone_tmpl_compass_8_center");
    if (templateId == "compass-8")           return tr("action.touch_zone_tmpl_compass_8");
    return templateId;
}

// Movimiento (Gestos) catalog — 14 fixed entries, closed set (ARCHITECTURE.md "Movimiento
// (catalogo de Gestos, cerrado 2026/08/15)"): 8 one-finger linear + 2 two-finger parallel +
// 2 two-finger twist + 2 two-finger pinch. Unlike Zonas' templates this isn't data-driven —
// the catalog doesn't change per instance/hardware, so it lives as a static table here instead
// of a JSON file. Row order below matches the 2-row grid layout (8 + 6) picked for the panel.
struct GestureIconDef { const char* id; const char* file; };
static const GestureIconDef kGestureIcons[14] = {
    // Row 1 - linear, 1 finger, clockwise from Up
    { "up",         "images/decorations/MoveUp.png" },
    { "up_right",   "images/decorations/MoveRightUp.png" },
    { "right",      "images/decorations/MoveRight.png" },
    { "down_right", "images/decorations/MoveRightDown.png" },
    { "down",       "images/decorations/MoveDown.png" },
    { "down_left",  "images/decorations/MoveLeftDown.png" },
    { "left",       "images/decorations/MoveLeft.png" },
    { "up_left",    "images/decorations/MoveLeftUp.png" },
    // Row 2 - 2 fingers: parallel, twist, pinch
    { "parallel_up",   "images/decorations/MoveDobleUp.png" },
    { "parallel_down", "images/decorations/MoveDobleDown.png" },
    { "twist_up_down", "images/decorations/MoveUpDown.png" },
    { "twist_down_up", "images/decorations/MoveDownUp.png" },
    { "pinch_close",   "images/decorations/MoveHorizontalClose.png" },
    { "pinch_open",    "images/decorations/MoveHorizontalAway.png" },
};

static std::string touchGestureDisplayName(const std::string& gestureId) {
    if (gestureId == "up")           return tr("action.touch_gesture_up");
    if (gestureId == "up_right")     return tr("action.touch_gesture_up_right");
    if (gestureId == "right")        return tr("action.touch_gesture_right");
    if (gestureId == "down_right")   return tr("action.touch_gesture_down_right");
    if (gestureId == "down")         return tr("action.touch_gesture_down");
    if (gestureId == "down_left")    return tr("action.touch_gesture_down_left");
    if (gestureId == "left")         return tr("action.touch_gesture_left");
    if (gestureId == "up_left")      return tr("action.touch_gesture_up_left");
    if (gestureId == "parallel_up")   return tr("action.touch_gesture_parallel_up");
    if (gestureId == "parallel_down") return tr("action.touch_gesture_parallel_down");
    if (gestureId == "twist_up_down") return tr("action.touch_gesture_twist_up_down");
    if (gestureId == "twist_down_up") return tr("action.touch_gesture_twist_down_up");
    if (gestureId == "pinch_close")   return tr("action.touch_gesture_pinch_close");
    if (gestureId == "pinch_open")    return tr("action.touch_gesture_pinch_open");
    return gestureId;
}

// stateToShort()'s short code ("l1", "dpad_up", ...) -> a compact display label for the "<label>
// -> ..." action-panel header (mirrors the hardcoded "L2"/"R2" the trigger panel already used).
static std::string physButtonDisplayLabel(const std::string& shortCode) {
    static const std::pair<const char*, const char*> kLabels[] = {
        { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" },
        { "l1", "L1" }, { "r1", "R1" }, { "l3", "L3" }, { "r3", "R3" },
        { "l4", "L4" }, { "r4", "R4" }, { "lp", "LP" }, { "rp", "RP" },
        { "select", "Select" }, { "start", "Start" }, { "home", "Home" },
        { "touch_btn", "Touch" },
        { "dpad_up", "D-Pad \xe2\x86\x91" }, { "dpad_down", "D-Pad \xe2\x86\x93" },
        { "dpad_left", "D-Pad \xe2\x86\x90" }, { "dpad_right", "D-Pad \xe2\x86\x92" },
    };
    for (auto& [k, v] : kLabels) if (k == shortCode) return v;
    return shortCode;
}

// Action-type sets for ActionPanel::renderActionTypeTabs (see ActionPanel.h) — every one of the 6
// action panels below passes one of these two, in the same fixed display order they always used.
// kAxisActionTypes (Analogico/Gyro only) adds Raton-movimiento, absent everywhere else.
static const std::vector<ActionType> kStdActionTypes  = {
    ActionType::Xbox, ActionType::Macro, ActionType::Keyboard, ActionType::Mouse, ActionType::Bot };
static const std::vector<ActionType> kAxisActionTypes = {
    ActionType::Xbox, ActionType::Macro, ActionType::Keyboard, ActionType::Mouse,
    ActionType::MouseMove, ActionType::Bot };

// ---------------------------------------------------------------------------
// render — full mapping editor UI (called each frame when active)
// ---------------------------------------------------------------------------
void MappingEditor::render(PadView& phys, PadView& virt) {
    // ── Pre-populate edits cuando cambia el mando activo (normal mode only) ──
    if (m_mode == Mode::kNormal) {
        DeviceCandidate dev = m_engine->getActiveDevice();
        if (dev.vid != m_model.vid || dev.pid != m_model.pid) {
            m_model.vid    = dev.vid;
            m_model.pid    = dev.pid;
            m_sel.physComp = -1;
            reload();
        }
    }

    // ── Profile mode header ───────────────────────────────────────────────────
    if (m_mode == Mode::kProfile) {
        if (ImGui::Button(tr("btn.back"))) {
            m_mode   = Mode::kNormal;
            m_active = false;
            return;
        }
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::Text("%s", tr("profiles.title"));

        // Toast
        if (m_profToast) {
            if (GetTickCount64() - m_profToastTime < 2500) {
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "%s", tr("profiles.toast_saved"));
            } else {
                m_profToast = false;
            }
        }
        if (!m_profSaveError.empty()) {
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", m_profSaveError.c_str());
        }

        // Profile selector — same row as Atras/title (unified single-row header, 2026/09/03)
        ImGui::SameLine(0.0f, 16.0f);
        std::vector<const char*> items;
        items.push_back(tr("profiles.new"));
        for (const auto& n : m_profileNames) items.push_back(n.c_str());

        int comboIdx = m_profIdx + 1;  // 0 = new, 1+ = existing
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::Combo("##profsel", &comboIdx, items.data(), (int)items.size())) {
            m_profIdx = comboIdx - 1;
            m_sel.physComp = -1;
            reload();
        }
        ImGui::SameLine(0.0f, 8.0f);

        // Name field (always editable)
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText(tr("profiles.name_label"), m_profNameBuf, sizeof(m_profNameBuf));
        ImGui::SameLine(0.0f, 8.0f);

        if (m_profIdx < 0) {
            // New profile: create button (needs a name and a connected device)
            DeviceCandidate dev = m_engine->getActiveDevice();
            bool canCreate = m_profNameBuf[0] != '\0' && (dev.vid != 0 || dev.pid != 0);
            ImGui::BeginDisabled(!canCreate);
            if (ImGui::Button(tr("profiles.btn_create"))) {
                // Build a path from the name
                std::string safeName(m_profNameBuf);
                for (auto& c : safeName) if (c == ' ' || c == '/' || c == '\\') c = '_';
                std::string newPath = Paths::userData("data/profiles/") + safeName + ".json";
                spdlog::trace("[Profile] 'Crear' clicked, name='{}' path='{}' VID={:04X} PID={:04X}",
                            m_profNameBuf, newPath, dev.vid, dev.pid);
                m_profilePaths.push_back(newPath);
                m_profileNames.push_back(m_profNameBuf);
                m_profIdx = (int)m_profilePaths.size() - 1;
                m_profileListChanged = true;
                save();
            }
            ImGui::EndDisabled();
        } else {
            // Existing profile: save + delete
            if (ImGui::Button(tr("btn.save")))
                save();
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,        { 0.6f, 0.15f, 0.15f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.75f, 0.2f, 0.2f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.5f, 0.1f, 0.1f, 1.0f });
            if (ImGui::Button(tr("btn.delete")))
                ImGui::OpenPopup("##prof_del_confirm");
            ImGui::PopStyleColor(3);

            if (ImGui::BeginPopupModal("##prof_del_confirm", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("%s", tr("profiles.confirm_delete"));
                ImGui::Spacing();
                if (ImGui::Button(tr("btn.delete"), { 100.0f, 0.0f })) {
                    DeleteFileA(m_profilePaths[m_profIdx].c_str());
                    m_profilePaths.erase(m_profilePaths.begin() + m_profIdx);
                    m_profileNames.erase(m_profileNames.begin() + m_profIdx);
                    m_profileListChanged = true;
                    m_profIdx = -1;
                    memset(m_profNameBuf, 0, sizeof(m_profNameBuf));
                    m_sel.physComp = -1;
                    reload();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button(tr("btn.cancel"), { 100.0f, 0.0f }))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        // Bot selector — single bot per profile. Right-aligned combo, set apart
        // from save/delete so it doesn't read as part of that button group.
        {
            std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
            std::vector<const char*> botItems;
            botItems.push_back(tr("profiles.bots_none"));
            for (const auto& b : availableBots) botItems.push_back(b.c_str());

            int botIdx = 0;  // 0 = none
            if (!m_model.contextBotsEdits.empty()) {
                auto it = std::find(availableBots.begin(), availableBots.end(),
                                    m_model.contextBotsEdits.front());
                if (it != availableBots.end())
                    botIdx = 1 + (int)std::distance(availableBots.begin(), it);
            }

            const char* label      = tr("profiles.bot_label");
            const float labelWidth = ImGui::CalcTextSize(label).x;
            const float comboWidth = 180.0f;
            const float groupWidth = labelWidth + ImGui::GetStyle().ItemSpacing.x + comboWidth;

            ImGui::SameLine();
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > groupWidth)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - groupWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::Combo("##profile_bot", &botIdx, botItems.data(), (int)botItems.size())) {
                m_model.contextBotsEdits.clear();
                if (botIdx > 0) m_model.contextBotsEdits.push_back(availableBots[botIdx - 1]);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("profiles.bots_hint"));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    } else if (m_mode == Mode::kNormal) {
        // Atras / Guardar — mirrors the Profile mode header row (2026/09/03) so the pad
        // images start at the same height across Pads/Mapeador/Perfiles.
        if (ImGui::Button(trid("btn.back", "mapCancel").c_str())) {
            m_sel.physComp = -1; m_sel.stickDir.clear(); m_sel.stickAsButton = false;
            m_sel.dpadDir.clear(); m_sel.triggerSrc.clear();
            m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
            reload();
            m_active = false;
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(trid("btn.save", "mapSave").c_str(), { 120.0f, 0.0f })) {
            save();
            m_active = false;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::Spacing();
    ImVec2 mouse        = ImGui::GetIO().MousePos;
    bool   mouseClicked = ImGui::IsMouseClicked(0);
    float  dt           = ImGui::GetIO().DeltaTime;

    // ── H9: lógica de mapping desde el mando ─────────────────────────────────
    GamepadState physNow = m_engine->getLastState();
    // physNow.touch1X/Y are RAW (see PhysicalTouchpad::process()'s comment) — shape them through
    // the active device's own touch calibration so region hit-testing in the editor agrees with
    // what actually fires in game (same reasoning as the accel/gyro shaping further down).
    float touch1X = physNow.touch1X, touch1Y = physNow.touch1Y;
    {
        DeviceCandidate dev = m_engine->getActiveDevice();
        const ControllerConfig* activeTouchCfg =
            findConfig(m_configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);
        if (activeTouchCfg) {
            touch1X = applyTouchAxisCalib(physNow.touch1X, activeTouchCfg->touchpad.xMax);
            touch1Y = applyTouchAxisCalib(physNow.touch1Y, activeTouchCfg->touchpad.yMax);
        }
    }
    // H9 Paso 1 (selección de fuente) — extraído a MappingSourceSelector (Tarea 3, parte 1). Ver
    // MappingSourceSelector.h para el detalle de qué cubre y por qué Paso 2 (justo debajo) se
    // queda aquí. paso1Gate se calcula ANTES de llamar a update() y se usa, sin recalcular, para
    // decidir si Paso 2 corre este mismo frame — preserva la exclusión mutua del if/else-if
    // original: si Paso 1 corría este frame, Paso 2 nunca corría en el mismo frame, aunque Paso 1
    // dejara m_sel.physComp listo para que Paso 2 lo viera.
    bool paso1Gate = (m_sel.physComp < 0 && m_sel.triggerSrc.empty());
    MappingSourceSelector::update(phys, m_model, m_sel, physNow, touch1X, touch1Y,
                                  *m_engine, m_configs,
                                  m_stickSelectThreshold, m_stickHoldMs,
                                  m_gyroSelectThreshold, m_accelSelectThreshold, dt);

    // H9 Paso 2 (asignación) — extraído a MappingSourceSelector (Tarea 3b). Llamada solo cuando
    // Paso 1 NO corrió este frame (paso1Gate en false), igual que el if/else-if original.
    if (!paso1Gate) {
        MappingSourceSelector::assign(phys, virt, m_model, m_sel, physNow,
                                       m_acceptedXbox, m_stickSelectThreshold);
    }
    // Unconditional every frame, regardless of whether Paso 1 or Paso 2 ran above — Paso 2's
    // rising-edge detection next frame (isStateActive(m_sel.h9PrevPhysState, ...) vs physNow)
    // depends on this never being skipped.
    m_sel.h9PrevPhysState = physNow;

    // ── Construir estados de display ──────────────────────────────────────────
    m_sel.flashTimer -= dt;
    if (m_sel.flashTimer <= 0.0f) {
        m_sel.flashComp = -1; m_sel.flashVirtShort.clear(); m_sel.flashSlotKey.clear();
        m_sel.flashPhysArrowComp = -1; m_sel.flashPhysArrowDir.clear();
    }

    GamepadState physDisplay{};
    GamepadState virtDisplay{};
    // Gyro/accel widget reads its ball position straight off physDisplay (via resolveFloat in
    // PadView.cpp) — without this it never receives the live physNow reading and stays frozen at
    // center regardless of real controller motion, even while H9's hold-to-arm logic below (which
    // reads physNow directly) works fine.
    physDisplay.gyroActive  = physNow.gyroActive;
    physDisplay.accelActive = physNow.accelActive;
    physDisplay.gyroX  = physNow.gyroX;  physDisplay.gyroY  = physNow.gyroY;  physDisplay.gyroZ  = physNow.gyroZ;
    physDisplay.accelX = physNow.accelX; physDisplay.accelY = physNow.accelY; physDisplay.accelZ = physNow.accelZ;
    if (m_sel.physComp < 0 && m_sel.h9HoldComp >= 0) {
        const auto& physComps = phys.getLayout().components;
        if (m_sel.h9HoldComp < (int)physComps.size()) {
            const PadComponent& heldComp = physComps[m_sel.h9HoldComp];
            if (heldComp.type == "button")
                activateState(physDisplay, heldComp.state);
            else if (heldComp.type == "stick" && !m_sel.h9HoldStickDir.empty())
                ;
            else if (heldComp.type == "stick")
                activateState(physDisplay, heldComp.stateClick);
            else if (heldComp.type == "dpad" && !m_sel.h9HoldDpadDir.empty()) {
                std::string dpadState = dpadDirToState(heldComp, m_sel.h9HoldDpadDir);
                activateState(physDisplay, dpadState);
            }
        }
    }
    // Apply a virtual short OR a stick slot key ("left_x_neg" etc.) to virtDisplay.
    auto applyVirtShort = [&](const std::string& vs) {
        if      (vs == "left_x_pos")  virtDisplay.leftX  =  1.0f;
        else if (vs == "left_x_neg")  virtDisplay.leftX  = -1.0f;
        else if (vs == "left_y_pos")  virtDisplay.leftY  =  1.0f;
        else if (vs == "left_y_neg")  virtDisplay.leftY  = -1.0f;
        else if (vs == "right_x_pos") virtDisplay.rightX =  1.0f;
        else if (vs == "right_x_neg") virtDisplay.rightX = -1.0f;
        else if (vs == "right_y_pos") virtDisplay.rightY =  1.0f;
        else if (vs == "right_y_neg") virtDisplay.rightY = -1.0f;
        else activateState(virtDisplay, shortToState(vs));
    };

    if (m_sel.physComp >= 0) {
        const auto& physComps = phys.getLayout().components;
        if (m_sel.physComp < (int)physComps.size()) {
            const PadComponent& selComp = physComps[m_sel.physComp];
            auto activateTriggerIfAssigned = [&](const std::string& physShort) -> bool {
                auto trigEdit = m_model.actionEdits.find(physShort);
                if (trigEdit != m_model.actionEdits.end() && trigEdit->second.type == ButtonActionType::Trigger) {
                    activateState(virtDisplay, trigEdit->second.target == "l2" ? "triggerL" : "triggerR");
                    return true;
                }
                return false;
            };
            if (selComp.type == "button") {
                const std::string& physState = selComp.state;
                activateState(physDisplay, physState);
                std::string physShort = stateToShort(physState);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_model.buttonEdits.find(physShort);
                    if (it != m_model.buttonEdits.end())
                        applyVirtShort(it->second);
                }
            } else if (selComp.type == "stick" && m_sel.stickAsButton) {
                activateState(physDisplay, selComp.stateClick);
                std::string physShort = stateToShort(selComp.stateClick);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_model.buttonEdits.find(physShort);
                    if (it != m_model.buttonEdits.end())
                        applyVirtShort(it->second);
                }
            } else if (selComp.type == "stick") {
                // Show current axis_action assignment in virtual display
                if (!m_sel.stickDir.empty()) {
                    auto [xId, yId] = stickIdsFromStateX(selComp.stateX);
                    std::string axisKey;
                    if      (m_sel.stickDir == "up")    axisKey = yId + "_pos";
                    else if (m_sel.stickDir == "down")  axisKey = yId + "_neg";
                    else if (m_sel.stickDir == "right") axisKey = xId + "_pos";
                    else if (m_sel.stickDir == "left")  axisKey = xId + "_neg";
                    auto it = m_model.axisActionEdits.find(axisKey);
                    if (it != m_model.axisActionEdits.end()) {
                        const HalfAxisAction& ha = it->second;
                        if (ha.type == HalfAxisActionType::VirtualButton)
                            activateState(virtDisplay, shortToState(ha.target));
                        else if (ha.type == HalfAxisActionType::Dpad) {
                            if      (ha.target == "up")    virtDisplay.dpadUp    = true;
                            else if (ha.target == "down")  virtDisplay.dpadDown  = true;
                            else if (ha.target == "left")  virtDisplay.dpadLeft  = true;
                            else if (ha.target == "right") virtDisplay.dpadRight = true;
                        } else if (ha.type == HalfAxisActionType::Trigger) {
                            if (ha.target == "l2" || ha.target == "trigger_l") virtDisplay.triggerL = 1.0f;
                            else                                                virtDisplay.triggerR = 1.0f;
                        } else if (ha.type == HalfAxisActionType::StickSlot) {
                            if      (ha.target == "left_x_pos")  virtDisplay.leftX  =  1.0f;
                            else if (ha.target == "left_x_neg")  virtDisplay.leftX  = -1.0f;
                            else if (ha.target == "left_y_pos")  virtDisplay.leftY  =  1.0f;
                            else if (ha.target == "left_y_neg")  virtDisplay.leftY  = -1.0f;
                            else if (ha.target == "right_x_pos") virtDisplay.rightX =  1.0f;
                            else if (ha.target == "right_x_neg") virtDisplay.rightX = -1.0f;
                            else if (ha.target == "right_y_pos") virtDisplay.rightY =  1.0f;
                            else if (ha.target == "right_y_neg") virtDisplay.rightY = -1.0f;
                        }
                    }
                }
            } else if (selComp.type == "dpad" && !m_sel.dpadDir.empty()) {
                std::string dpadState = dpadDirToState(selComp, m_sel.dpadDir);
                activateState(physDisplay, dpadState);
                std::string physShort = stateToShort(dpadState);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_model.buttonEdits.find(physShort);
                    if (it != m_model.buttonEdits.end())
                        applyVirtShort(it->second);
                }
            }
        }
    }
    if (!m_sel.triggerSrc.empty()) {
        if (m_sel.triggerSrc == "l2") physDisplay.triggerL = 1.0f;
        else                          physDisplay.triggerR = 1.0f;
        auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
        if (it != m_model.trigActionEdits.end()) {
            const ButtonAction& act = it->second;
            if (act.type == ButtonActionType::TriggerPassthrough)
                activateState(virtDisplay, act.target == "l2" ? "triggerL" : "triggerR");
            else if (act.type == ButtonActionType::VirtualButton)
                applyVirtShort(act.name);
        }
    }
    if (m_sel.flashComp >= 0 && !m_sel.flashVirtShort.empty())
        activateState(virtDisplay, shortToState(m_sel.flashVirtShort));
    if (m_sel.flashTimer > 0.0f && !m_sel.flashSlotKey.empty()) {
        const std::string& sk = m_sel.flashSlotKey;
        if      (sk == "left_x_pos")  virtDisplay.leftX  =  1.0f;
        else if (sk == "left_x_neg")  virtDisplay.leftX  = -1.0f;
        else if (sk == "left_y_pos")  virtDisplay.leftY  =  1.0f;
        else if (sk == "left_y_neg")  virtDisplay.leftY  = -1.0f;
        else if (sk == "right_x_pos") virtDisplay.rightX =  1.0f;
        else if (sk == "right_x_neg") virtDisplay.rightX = -1.0f;
        else if (sk == "right_y_pos") virtDisplay.rightY =  1.0f;
        else if (sk == "right_y_neg") virtDisplay.rightY = -1.0f;
    }

    // ── Pad físico ────────────────────────────────────────────────────────────
    ImGui::BeginGroup();
    m_physOrigin = ImGui::GetCursorScreenPos();
    phys.render(physDisplay);
    {
        int   physArrowComp = m_sel.physComp;
        std::string physArrowDir = m_sel.stickDir;
        if (physArrowComp < 0 && m_sel.flashTimer > 0.0f && m_sel.flashPhysArrowComp >= 0) {
            physArrowComp = m_sel.flashPhysArrowComp;
            physArrowDir  = m_sel.flashPhysArrowDir;
        }
        phys.renderStickArrows(m_physOrigin, physArrowComp, physArrowDir);
        phys.renderGyroArrows(m_physOrigin, physArrowComp, physArrowDir);
        phys.renderTouchpadHints(m_physOrigin, m_sel.physComp, m_sel.touchSurfaceSelected);
        // Only while the touchpad is actively selected as Superficie (the user clicked into the
        // touch event, not just glancing at the pad) — otherwise the overlay drew over the touch
        // icons on every frame the editor was open, regardless of what was selected.
        bool touchpadSelected = m_sel.physComp >= 0 &&
            m_sel.physComp < (int)phys.getLayout().components.size() &&
            phys.getLayout().components[m_sel.physComp].type == "touchpad" &&
            m_sel.touchSurfaceSelected;
        if (touchpadSelected && m_model.touchSurfaceMode == TouchpadSurfaceMode::Zones &&
            !m_model.touchZones.empty()) {
            std::string hoveredRegionId;
            phys.hitTestZoneRegion(ImGui::GetMousePos(), m_physOrigin, m_model.touchZones, hoveredRegionId);
            phys.renderTouchZoneOverlay(m_physOrigin, m_model.touchZones,
                                         m_sel.touchZoneRegionSelected, hoveredRegionId);
        }
    }
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextDisabled("%s", tr("mapper.physical"));
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();
    {
        if (!m_arrowTex.valid())
            PadView::loadPng(m_device, "images/decorations/ArrowRight.png", m_arrowTex);
        const auto& L = phys.getLayout();
        constexpr float kArrowSize = 40.0f;
        float push = (L.FrontH + L.TopH) * 0.5f - kArrowSize * 0.5f;
        if (push > 0.0f) ImGui::Dummy({ 0.0f, push });
        if (m_arrowTex.valid())
            ImGui::Image((ImTextureID)m_arrowTex.srv, { kArrowSize, kArrowSize });
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);

    ImGui::BeginGroup();
    m_virtOrigin = ImGui::GetCursorScreenPos();
    virt.render(virtDisplay);
    {
        // Determine which virtual stick arrow to highlight:
        // steady-state (current assignment) or flash (just assigned).
        int         virtArrowComp = -1;
        std::string virtArrowDir;

        // Steady-state: button / dpad / stickAsButton source with a slot assignment.
        if (m_sel.physComp >= 0) {
            const auto& pComps = phys.getLayout().components;
            if (m_sel.physComp < (int)pComps.size()) {
                const PadComponent& sc = pComps[m_sel.physComp];
                std::string physShort;
                if (sc.type == "button")
                    physShort = stateToShort(sc.state);
                else if (sc.type == "stick" && m_sel.stickAsButton)
                    physShort = stateToShort(sc.stateClick);
                else if (sc.type == "dpad" && !m_sel.dpadDir.empty())
                    physShort = stateToShort(dpadDirToState(sc, m_sel.dpadDir));
                else if (sc.type == "stick" && !m_sel.stickDir.empty()) {
                    // Axis-action source: show StickSlot target if assigned.
                    auto [xId, yId] = stickIdsFromStateX(sc.stateX);
                    std::string ak;
                    if      (m_sel.stickDir == "up")    ak = yId + "_pos";
                    else if (m_sel.stickDir == "down")  ak = yId + "_neg";
                    else if (m_sel.stickDir == "right") ak = xId + "_pos";
                    else if (m_sel.stickDir == "left")  ak = xId + "_neg";
                    auto it = m_model.axisActionEdits.find(ak);
                    if (it != m_model.axisActionEdits.end() &&
                        it->second.type == HalfAxisActionType::StickSlot)
                        std::tie(virtArrowComp, virtArrowDir) =
                            slotKeyToArrow(virt.getLayout(), it->second.target);
                } else if (sc.type == "gyro" && !m_sel.stickDir.empty()) {
                    // Gyro/accel source: show StickSlot target if assigned, whichever sensor holds it.
                    auto git = m_model.gyroActionEdits.find(gyroKeyFromDir(m_sel.stickDir));
                    std::string accK = accelKeyFromDir(m_sel.stickDir);
                    auto ait = accK.empty() ? m_model.accelActionEdits.end()
                                            : m_model.accelActionEdits.find(accK);
                    if (git != m_model.gyroActionEdits.end() &&
                        git->second.type == HalfAxisActionType::StickSlot)
                        std::tie(virtArrowComp, virtArrowDir) =
                            slotKeyToArrow(virt.getLayout(), git->second.target);
                    else if (ait != m_model.accelActionEdits.end() &&
                             ait->second.type == HalfAxisActionType::StickSlot)
                        std::tie(virtArrowComp, virtArrowDir) =
                            slotKeyToArrow(virt.getLayout(), ait->second.target);
                }
                if (!physShort.empty()) {
                    auto it = m_model.buttonEdits.find(physShort);
                    if (it != m_model.buttonEdits.end() && !it->second.empty())
                        std::tie(virtArrowComp, virtArrowDir) =
                            slotKeyToArrow(virt.getLayout(), it->second);
                }
            }
        } else if (!m_sel.triggerSrc.empty()) {
            auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
            if (it != m_model.trigActionEdits.end() &&
                it->second.type == ButtonActionType::VirtualButton)
                std::tie(virtArrowComp, virtArrowDir) =
                    slotKeyToArrow(virt.getLayout(), it->second.name);
        }

        // Flash overrides steady-state for 0.5 s after assignment.
        if (m_sel.flashTimer > 0.0f && !m_sel.flashSlotKey.empty())
            std::tie(virtArrowComp, virtArrowDir) =
                slotKeyToArrow(virt.getLayout(), m_sel.flashSlotKey);

        virt.renderStickArrows(m_virtOrigin, virtArrowComp, virtArrowDir);
    }
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextDisabled("%s", tr("mapper.virtual"));
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();

    // ── Marcos de foco y texto instruccional ──────────────────────────────────
    {
        constexpr ImU32 kFrameColor = IM_COL32(255, 220, 0, 200);
        constexpr float kThickness  = 2.5f;
        constexpr float kPad        = 4.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const auto& physL = phys.getLayout();
        const auto& virtL = virt.getLayout();
        float physH = physL.FrontH + physL.TopH;
        float virtH = virtL.FrontH + virtL.TopH;

        if (m_sel.physComp < 0 && m_sel.triggerSrc.empty()) {
            ImVec2 rMin = { m_physOrigin.x - kPad, m_physOrigin.y - kPad };
            ImVec2 rMax = { m_physOrigin.x + physL.W + kPad, m_physOrigin.y + physH + kPad };
            dl->AddRect(rMin, rMax, kFrameColor, 4.0f, 0, kThickness);
        } else {
            ImVec2 rMin = { m_virtOrigin.x - kPad, m_virtOrigin.y - kPad };
            ImVec2 rMax = { m_virtOrigin.x + virtL.W + kPad, m_virtOrigin.y + virtH + kPad };
            dl->AddRect(rMin, rMax, kFrameColor, 4.0f, 0, kThickness);
        }
    }

    // Texto instruccional — subido ~15px (2026/09/04, afinado tras probar 10 y 20) para ganar
    // hueco para las 2 filas de botones del panel de accion (fila de tipo + contenido a la
    // derecha).
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 18.0f);
    ImGui::Spacing();
    {
        const char* msg;
        ImVec4      col = { 1.0f, 0.86f, 0.0f, 1.0f };
        if (m_sel.h9ErrorTimer > 0.0f) {
            msg = tr("mapper.hint_no_xbox");
            col = { 1.0f, 0.3f, 0.3f, 1.0f };
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && !m_sel.h9HoldTriggerSrc.empty()) {
            msg = tr("mapper.hint_hold_trigger");
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && !m_sel.h9HoldGyroDir.empty()) {
            msg = tr("mapper.hint_hold_gyro");
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && m_sel.h9HoldComp >= 0) {
            msg = m_sel.h9HoldStickDir.empty()
                ? tr("mapper.hint_hold_button")
                : tr("mapper.hint_hold_stick");
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty()) {
            msg = tr("mapper.hint_pick_source");
        } else if (!m_sel.triggerSrc.empty()) {
            // Trigger only has Xbox/Macro/Keyboard/Mouse/Bot — all 5 merge into the "<label> ->
            // ..." line the trigger panel draws, so this is always nullptr now (2026/09/04,
            // hint_trig_action is unreachable — kept in strings_*.json in case a future type needs
            // it back).
            msg = nullptr;
        } else if (m_sel.physComp >= 0 &&
                   phys.getLayout().components[m_sel.physComp].type == "stick" &&
                   !m_sel.stickAsButton) {
            if (!m_sel.stickDir.empty()) {
                // All 6 ActionType tabs the axis/gyro panels expose (Xbox/Macro/Keyboard/Mouse/
                // MouseMove/Bot) merge into the "<label> -> ..." line they draw — hint_half_axis
                // is unreachable now (2026/09/04, kept in strings_*.json in case it's needed again).
                msg = nullptr;
            } else
                msg = tr("mapper.hint_click_stick");
        } else {
            // Button/d-pad (stick-as-button/L3-R3 included, 2026/09/04 — it goes through the H5
            // panel same as any other button, see the guard below), touch (Zonas/Gestos) and
            // gyro/accel — Keyboard capture included (2026/09/04): merged into that panel's own
            // "<label> -> ..." line instead (2026/09/03) — saves a line vs. a separate generic hint.
            msg = nullptr;
        }

        float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;
        if (msg) {
            ImGui::SetWindowFontScale(1.35f);
            float textW   = ImGui::CalcTextSize(msg).x;
            float offsetX = (availW - textW) * 0.5f;
            if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            ImGui::TextColored(col, "%s", msg);
            ImGui::SetWindowFontScale(1.0f);
        }

        if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && m_sel.h9HoldComp >= 0 && m_sel.h9HoldTimer > 0.0f) {
            constexpr float kBarW = 160.0f;
            float barOffX = (availW - kBarW) * 0.5f;
            if (barOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barOffX);
            float holdSec = !m_sel.h9HoldStickDir.empty() ? (m_stickHoldMs / 1000.0f) : 1.0f;
            ImGui::ProgressBar(m_sel.h9HoldTimer / holdSec, { kBarW, 6.0f }, "");
        }
        if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() &&
            !m_sel.h9HoldGyroDir.empty() && m_sel.h9HoldGyroTimer > 0.0f) {
            constexpr float kBarW = 160.0f;
            float barOffX = (availW - kBarW) * 0.5f;
            if (barOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barOffX);
            ImGui::ProgressBar(m_sel.h9HoldGyroTimer / kImuConfirmSec, { kBarW, 6.0f }, "");
        }
        if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() &&
            !m_sel.h9HoldTriggerSrc.empty() && m_sel.h9HoldTriggerTimer > 0.0f) {
            constexpr float kBarW = 160.0f;
            float barOffX = (availW - kBarW) * 0.5f;
            if (barOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barOffX);
            ImGui::ProgressBar(m_sel.h9HoldTriggerTimer / 2.0f, { kBarW, 6.0f }, "");
        }
    }

    // ── Action panel for the selected physical component ────────────────────────
    if (m_sel.physComp >= 0) {
        const auto& physComps = phys.getLayout().components;
        const std::string& selType = physComps[m_sel.physComp].type;
        ImGui::Spacing();
        float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;

    if (selType == "touchpad" && m_sel.touchSurfaceSelected) {
        // Superficie: surfaceMode selector. Mouse (pre-existing touchDelta->mouse routing) and
        // Analog (recentered touch position -> a chosen virtual stick, see below) have real
        // behavior; Gesture has the 14-icon grid but no action-assignment wiring yet; see
        // ARCHITECTURE.md "Touchpad" for the full design.
        //
        // Left half: mode buttons (Mov.Raton/Analogico/Gestos/Zonas/Limpiar), one row, left-
        // aligned. Right half (same row, via the Indent trick used everywhere else in this file):
        // content specific to whichever mode is active (2026/09/04).
        float colGap  = 16.0f;
        float halfW   = (availW - colGap) * 0.5f;
        float indentW = halfW + colGap;

        auto modeBtn = [&](const char* label, TouchpadSurfaceMode mode, bool enabled, float w) {
            bool sel = (m_model.touchSurfaceMode == mode);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (!enabled) ImGui::BeginDisabled();
            if (ImGui::Button(label, { w, 0.0f })) {
                if (m_model.touchSurfaceMode != mode) {
                    // Switching to a different mode: clear any leftover sub-selection from a
                    // previous visit this same Mapeador session, so re-entering a mode never
                    // starts pre-armed with a stale region/gesture and its action panel already
                    // showing — e.g. picking a Zonas region, switching to Gestos, then back to
                    // Zonas used to leave that old region selected. Found 2026/08/27 while
                    // designing the physical mode-selector idea (see SESSION_CONTEXT.md): a
                    // pre-armed Zonas selection would also make its physical-press destination
                    // (H9 Paso 2) compete with a future mode-cycle trigger — but it was already
                    // a real UX papercut on its own, independent of that.
                    m_sel.touchZoneRegionSelected.clear();
                    m_sel.touchGestureSelected.clear();
                    m_sel.actionType = ActionType::Xbox;
                    m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
                }
                m_model.touchSurfaceMode = mode;
                // Analog is meaningless with no stick target — default to Left the moment the
                // mode is picked instead of showing an empty/"none" resting state (there's no
                // reason to pick Analog and not want a stick driven by it).
                if (mode == TouchpadSurfaceMode::Analog && m_model.touchAnalogStickTarget.empty())
                    m_model.touchAnalogStickTarget = "left";
            }
            if (!enabled) ImGui::EndDisabled();
            if (sel) ImGui::PopStyleColor();
        };

        constexpr int kNBtn = ActionPanel::kActionTypeBtnRefCount;
        float modeBtnW = (halfW - ImGui::GetStyle().ItemSpacing.x * (kNBtn - 1)) / kNBtn;
        modeBtn(tr("action.type_mousemove"),     TouchpadSurfaceMode::Mouse,   true, modeBtnW);
        ImGui::SameLine();
        modeBtn(tr("action.touch_mode_analog"),  TouchpadSurfaceMode::Analog,  true, modeBtnW);
        ImGui::SameLine();
        modeBtn(tr("action.touch_mode_gesture"), TouchpadSurfaceMode::Gesture, true, modeBtnW);
        ImGui::SameLine();
        modeBtn(tr("action.touch_mode_zones"),   TouchpadSurfaceMode::Zones,   true, modeBtnW);
        ImGui::SameLine();
        // "Limpiar" — 5th stop, back to Unassigned (the device default, see BITACORA.md
        // 2026/09/02): a real state, not a one-shot reset — reuses modeBtn as-is so it highlights
        // like the other 4 while active and gets the same leftover-selection cleanup on switch.
        modeBtn(tr("btn.clear"), TouchpadSurfaceMode::Unassigned, true, modeBtnW);

        // Confirms and exits the touch picker — used by Mov.Raton/Analogico below, which (unlike
        // Zonas/Gestos, that only close once a whole region/gesture has a committed action) have
        // nothing left to configure once the mode/target is picked, so there's no deeper
        // self-committing widget to close through (mirrors the Zonas/Gestos "Explicit close
        // button" further down, same reset).
        auto closeTouchPicker = [&]() {
            m_sel.physComp = -1;
            m_sel.touchSurfaceSelected = false;
            m_sel.touchZoneRegionSelected.clear();
            m_sel.touchGestureSelected.clear();
        };

        ImGui::SameLine();
        ImGui::Indent(indentW);

        if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Mouse) {
            // Nothing to configure for a straight mouse-move surface (2026/09/04).
            if (ImGui::Button(trid("btn.assign", "touchMouseAssign").c_str(), { 110.0f, 0.0f }))
                closeTouchPicker();
        } else if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Analog) {
            // Analog: no per-direction action assignment (that's Zonas/Movimiento's job, whole
            // discrete triggers) — the surface is a continuous stick, the only choice is which
            // virtual stick it drives.
            auto targetBtn = [&](const char* label, const char* target) {
                bool sel = (m_model.touchAnalogStickTarget == target);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button(label, { 100.0f, 0.0f })) m_model.touchAnalogStickTarget = target;
                if (sel) ImGui::PopStyleColor();
            };
            targetBtn(tr("action.touch_analog_left"),  "left");
            ImGui::SameLine();
            targetBtn(tr("action.touch_analog_right"), "right");
            ImGui::SameLine();
            targetBtn(tr("action.touch_analog_both"),  "both");
            ImGui::SameLine();
            if (ImGui::Button(trid("btn.assign", "touchAnalogAssign").c_str(), { 100.0f, 0.0f }))
                closeTouchPicker();
        } else if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Gesture) {
            // 14-icon grid, lazy-loaded once, same precedent as m_arrowTex/m_zoneTemplatesLoaded
            // below. Clicking a gesture opens the same 5-button action panel Zonas uses (see
            // below) for all 14 discrete gestures, twist included (a twist is itself a one-shot
            // release classification, not a continuous signal — see TouchGestures.h's
            // classifyTwoFingerGesture()). See ARCHITECTURE.md "Movimiento" for the full design.
            if (!m_gestureIconsLoaded) {
                m_gestureIconTex.resize(std::size(kGestureIcons));
                for (size_t i = 0; i < std::size(kGestureIcons); ++i)
                    PadView::loadPng(m_device, kGestureIcons[i].file, m_gestureIconTex[i]);
                m_gestureIconsLoaded = true;
            }
            // Single row of all 14 (2026/09/04, was 2 rows of 8+6 stacked below the mode
            // buttons), sized to match the mode buttons' own footprint (2026/09/04, cont.: was
            // GetFrameHeight() alone — ImageButton adds its own FramePadding around the image size
            // given, on top of an already frame-height-sized image, so the rendered button came out
            // visibly bigger than a mode button; subtracting that padding back out makes the two
            // rows match, and capping by modeBtnW keeps an icon from ever going wider than one mode
            // button either) — shrink further only if the right half is still too narrow for all 14.
            float gFramePad = ImGui::GetStyle().FramePadding.y;
            float kGestureIconSzTarget = ImGui::GetFrameHeight() - 2.0f * gFramePad;
            float kGestureIconSzDefault = (kGestureIconSzTarget < modeBtnW) ? kGestureIconSzTarget : modeBtnW;
            const int kGestureCount = (int)std::size(kGestureIcons);
            float gSp = ImGui::GetStyle().ItemSpacing.x;
            float gNeededW = kGestureIconSzDefault * kGestureCount + gSp * (kGestureCount - 1);
            float gestureIconSz = (gNeededW <= halfW)
                ? kGestureIconSzDefault
                : (halfW - gSp * (kGestureCount - 1)) / kGestureCount;
            for (int i = 0; i < kGestureCount; ++i) {
                const GestureIconDef& g = kGestureIcons[i];
                bool sel = (m_sel.touchGestureSelected == g.id);
                ImGui::PushID(i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                const PadTexture& tex = m_gestureIconTex[i];
                bool clicked;
                if (tex.valid())
                    clicked = ImGui::ImageButton("##gicon", (ImTextureID)(uintptr_t)tex.srv,
                                                  { gestureIconSz, gestureIconSz });
                else
                    clicked = ImGui::Button("?", { gestureIconSz, gestureIconSz });
                if (clicked) m_sel.touchGestureSelected = g.id;
                if (sel) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", touchGestureDisplayName(g.id).c_str());
                ImGui::PopID();
                if (i + 1 < kGestureCount) ImGui::SameLine();
            }
        } else if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Zones) {
            // Template picker — always visible, not just while touchZones is still empty, so the
            // instance can switch templates later too, not only seed one the first time. Loaded
            // lazily, same precedent as m_macroNamesLoaded below.
            if (!m_zoneTemplatesLoaded) {
                m_zoneTemplates = loadTouchZoneTemplates(Paths::userData("data/touch_zone_templates.json"));
                m_zoneTemplatesLoaded = true;
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", tr("action.touch_zones_pick_template"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            std::string currentTmplLabel = m_model.touchZoneTemplateId.empty()
                ? tr("action.touch_zones_pick_template")
                : touchZoneTemplateDisplayName(m_model.touchZoneTemplateId);
            if (ImGui::BeginCombo("##zoneTemplateSel", currentTmplLabel.c_str())) {
                for (const auto& tmpl : m_zoneTemplates) {
                    bool sel = (tmpl.id == m_model.touchZoneTemplateId);
                    if (ImGui::Selectable(touchZoneTemplateDisplayName(tmpl.id).c_str(), sel)) {
                        m_model.touchZoneTemplateId = tmpl.id;
                        m_model.touchZones = tmpl.regions;
                        // Region ids from the previous template may not exist in the new one (or
                        // may mean something different even if the id string matches) — wipe
                        // per-region actions rather than leave orphaned/mismatched entries.
                        m_model.touchZoneActionEdits.clear();
                        m_sel.touchZoneRegionSelected.clear();
                    }
                }
                ImGui::EndCombo();
            }
            if (!m_model.touchZones.empty()) {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", tr("action.touch_zones_pick_region"));
            }
        } else {
            // Limpiar/Unassigned: a real state, not a one-shot reset (see BITACORA.md
            // 2026/09/02) — nothing to configure until another mode is picked, just a close
            // button next to the hint like Mov.Raton/Analogico have above (2026/09/0X: had gone
            // missing in the redesign pass, found by the user testing).
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", tr("action.touch_mode_unassigned_hint"));
            ImGui::SameLine();
            if (ImGui::Button(trid("btn.assign", "touchUnassignedAssign").c_str(), { 110.0f, 0.0f }))
                closeTouchPicker();
        }
        ImGui::Unindent(indentW);

        // Zonas — region action sub-panel, below the row above (unchanged from before, still
        // spans the full width, only reached once a template AND a region are picked).
        if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Zones &&
            !m_model.touchZones.empty() && !m_sel.touchZoneRegionSelected.empty()) {
            ImGui::Spacing();
            const std::string& regionSel = m_sel.touchZoneRegionSelected;

            // Label + merged hint (2026/09/03) — Zonas had no identifying label before.
            {
                std::string lbl = regionSel + " \xe2\x86\x92";
                if (m_sel.actionType == ActionType::Xbox)
                    lbl += std::string(" ") + tr("mapper.hint_choose_action");
                else if (m_sel.actionType == ActionType::Macro)
                    lbl += std::string(" ") + tr("mapper.hint_choose_macro");
                else if (m_sel.actionType == ActionType::Mouse)
                    lbl += std::string(" ") + tr("mapper.hint_choose_mouse");
                else if (m_sel.actionType == ActionType::Bot)
                    lbl += std::string(" ") + tr("mapper.hint_choose_bot");
                else if (m_sel.actionType == ActionType::Keyboard)
                    lbl += std::string(" ") +
                           tr(m_sel.captureKeys.empty() ? "mapper.hint_press_combo" : "mapper.hint_press_more") +
                           " " + tr("mapper.hint_cancel_combo");
                float hdrW = ImGui::CalcTextSize(lbl.c_str()).x;
                float offXl = (availW - hdrW) * 0.5f;
                if (offXl > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offXl);
                ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl.c_str());
            }

            // Left half: type buttons, one row, left-aligned. Right half (same row, via the
            // Indent trick — see the H5 panel above): content for the selected type (2026/09/03).
            ActionPanel::renderActionTypeTabs("typeBtnZone", m_sel.actionType, m_sel.captureKeys,
                                              kStdActionTypes, halfW);

            ImGui::SameLine();
            ImGui::Indent(indentW);

            if (m_sel.actionType == ActionType::Xbox) {
                // No widget here, same as every other component's Gamepad/Xbox tab (button,
                // axis, gyro, trigger) — it's a silent "waiting for a virtual pad click" state.
                // See onVirtHitPhysButton's new touchpad branch for the region-specific
                // handling: button/dpad-direction/trigger target, keyed by
                // touchZoneActionEdits[regionSel] instead of buttonEdits.
                ImGui::NewLine();
            } else if (m_sel.actionType == ActionType::Macro) {
                if (!m_macroNamesLoaded) {
                    m_macroNames.clear(); m_macroLibrary.clear();
                    try {
                        std::ifstream f(Paths::userData("data/macros.json"));
                        if (f.is_open()) {
                            json j = json::parse(f);
                            for (auto& [k, v] : j.items()) {
                                m_macroNames.push_back(k);
                                m_macroLibrary.emplace_back(k, v.get<std::string>());
                            }
                        }
                    } catch (...) {}
                    m_macroNamesLoaded = true;
                }
                if (m_sel.macroSel.empty()) {
                    auto it = m_model.touchZoneActionEdits.find(regionSel);
                    if (it != m_model.touchZoneActionEdits.end() && it->second.type == ButtonActionType::Macro)
                        m_sel.macroSel = it->second.name;
                }
                if (ActionPanel::renderMacroCombo("macZone", m_sel.macroSel, m_macroNames, halfW)) {
                    ButtonAction act;
                    act.type = ButtonActionType::Macro; act.physical = regionSel; act.name = m_sel.macroSel;
                    m_model.touchZoneActionEdits[regionSel] = act;
                    m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
                }
            } else if (m_sel.actionType == ActionType::Keyboard) {
                bool cancel = ActionPanel::isCancelSelectionCombo(physNow);
                if (cancel) {
                    m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                    ImGui::NewLine();
                } else if (ActionPanel::renderKeyboardCapture("kbZone", m_sel.captureKeys, halfW, true)) {
                    ButtonAction act;
                    act.type = ButtonActionType::Keyboard; act.physical = regionSel;
                    for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                    m_model.touchZoneActionEdits[regionSel] = act;
                    m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                }
            } else if (m_sel.actionType == ActionType::Mouse) {
                std::string mbResult;
                if (ActionPanel::renderMouseButtons("mbZone", mbResult, halfW)) {
                    ButtonAction act;
                    act.type = ButtonActionType::MouseClick; act.physical = regionSel; act.mouseButton = mbResult;
                    m_model.touchZoneActionEdits[regionSel] = act;
                    m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
                    m_sel.actionType = ActionType::Xbox;
                }
            } else if (m_sel.actionType == ActionType::Bot) {
                std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
                if (m_sel.botSel.empty()) {
                    auto it = m_model.touchZoneActionEdits.find(regionSel);
                    if (it != m_model.touchZoneActionEdits.end() && it->second.type == ButtonActionType::Bot)
                        m_sel.botSel = it->second.name;
                }
                if (ActionPanel::renderBotCombo("botZone", m_sel.botSel, availableBots, halfW)) {
                    ButtonAction act;
                    act.type = ButtonActionType::Bot; act.physical = regionSel; act.name = m_sel.botSel;
                    m_model.touchZoneActionEdits[regionSel] = act;
                    m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
                }
            }
            ImGui::Unindent(indentW);
        } else if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Gesture &&
                   !m_sel.touchGestureSelected.empty()) {
            // Gestos — gesture action sub-panel, below the row above (unchanged from before,
            // still spans the full width, only reached once a specific gesture icon is picked).
            ImGui::Spacing();
            const std::string& gestureSel = m_sel.touchGestureSelected;

            // Label + merged hint (2026/09/03), centered like the other panels.
            {
                std::string lbl = touchGestureDisplayName(gestureSel) + " \xe2\x86\x92";
                if (m_sel.actionType == ActionType::Xbox)
                    lbl += std::string(" ") + tr("mapper.hint_choose_action");
                else if (m_sel.actionType == ActionType::Macro)
                    lbl += std::string(" ") + tr("mapper.hint_choose_macro");
                else if (m_sel.actionType == ActionType::Mouse)
                    lbl += std::string(" ") + tr("mapper.hint_choose_mouse");
                else if (m_sel.actionType == ActionType::Bot)
                    lbl += std::string(" ") + tr("mapper.hint_choose_bot");
                else if (m_sel.actionType == ActionType::Keyboard)
                    lbl += std::string(" ") +
                           tr(m_sel.captureKeys.empty() ? "mapper.hint_press_combo" : "mapper.hint_press_more") +
                           " " + tr("mapper.hint_cancel_combo");
                float hdrW = ImGui::CalcTextSize(lbl.c_str()).x;
                float offXl = (availW - hdrW) * 0.5f;
                if (offXl > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offXl);
                ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl.c_str());
            }

            // Left half: type buttons, one row, left-aligned. Right half (same row, via the
            // Indent trick — see the H5 panel above): content for the selected type (2026/09/03).
            ActionPanel::renderActionTypeTabs("typeBtnGesture", m_sel.actionType, m_sel.captureKeys,
                                              kStdActionTypes, halfW);

            ImGui::SameLine();
            ImGui::Indent(indentW);

            if (m_sel.actionType == ActionType::Xbox) {
                // No widget here, same as every other component's Gamepad/Xbox tab — a silent
                // "waiting for a virtual pad click" state. See onVirtHitTouchGesture for the
                // button/dpad-direction/trigger target resolution, keyed by
                // touchGestureActionEdits[gestureSel] instead of buttonEdits.
                ImGui::NewLine();
            } else if (m_sel.actionType == ActionType::Macro) {
                if (!m_macroNamesLoaded) {
                    m_macroNames.clear(); m_macroLibrary.clear();
                    try {
                        std::ifstream f(Paths::userData("data/macros.json"));
                        if (f.is_open()) {
                            json j = json::parse(f);
                            for (auto& [k, v] : j.items()) {
                                m_macroNames.push_back(k);
                                m_macroLibrary.emplace_back(k, v.get<std::string>());
                            }
                        }
                    } catch (...) {}
                    m_macroNamesLoaded = true;
                }
                if (m_sel.macroSel.empty()) {
                    auto it = m_model.touchGestureActionEdits.find(gestureSel);
                    if (it != m_model.touchGestureActionEdits.end() && it->second.type == ButtonActionType::Macro)
                        m_sel.macroSel = it->second.name;
                }
                if (ActionPanel::renderMacroCombo("macGest", m_sel.macroSel, m_macroNames, halfW)) {
                    ButtonAction act;
                    act.type = ButtonActionType::Macro; act.physical = gestureSel; act.name = m_sel.macroSel;
                    m_model.touchGestureActionEdits[gestureSel] = act;
                    m_sel.physComp = -1; m_sel.touchGestureSelected.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
                }
            } else if (m_sel.actionType == ActionType::Keyboard) {
                bool cancel = ActionPanel::isCancelSelectionCombo(physNow);
                if (cancel) {
                    m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                    ImGui::NewLine();
                } else if (ActionPanel::renderKeyboardCapture("kbGest", m_sel.captureKeys, halfW, true)) {
                    ButtonAction act;
                    act.type = ButtonActionType::Keyboard; act.physical = gestureSel;
                    for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                    m_model.touchGestureActionEdits[gestureSel] = act;
                    m_sel.physComp = -1; m_sel.touchGestureSelected.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                }
            } else if (m_sel.actionType == ActionType::Mouse) {
                std::string mbResult;
                if (ActionPanel::renderMouseButtons("mbGest", mbResult, halfW)) {
                    ButtonAction act;
                    act.type = ButtonActionType::MouseClick; act.physical = gestureSel; act.mouseButton = mbResult;
                    m_model.touchGestureActionEdits[gestureSel] = act;
                    m_sel.physComp = -1; m_sel.touchGestureSelected.clear();
                    m_sel.actionType = ActionType::Xbox;
                }
            } else if (m_sel.actionType == ActionType::Bot) {
                std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
                if (m_sel.botSel.empty()) {
                    auto it = m_model.touchGestureActionEdits.find(gestureSel);
                    if (it != m_model.touchGestureActionEdits.end() && it->second.type == ButtonActionType::Bot)
                        m_sel.botSel = it->second.name;
                }
                if (ActionPanel::renderBotCombo("botGest", m_sel.botSel, availableBots, halfW)) {
                    ButtonAction act;
                    act.type = ButtonActionType::Bot; act.physical = gestureSel; act.name = m_sel.botSel;
                    m_model.touchGestureActionEdits[gestureSel] = act;
                    m_sel.physComp = -1; m_sel.touchGestureSelected.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
                }
            }
            ImGui::Unindent(indentW);
        } else if (m_sel.touchGestureSelected.empty() &&
                   m_model.touchSurfaceMode == TouchpadSurfaceMode::Gesture) {
            ImGui::TextDisabled("%s", tr("action.touch_gestures_hint"));
        }

        // Explicit close button — only for the Zonas/Gestos region-or-gesture action sub-panel
        // (2026/09/04: gated to that, was firing for every mode since m_sel.actionType defaults
        // to Xbox — showed a stray extra "Asignar" under Mov.Raton/Analogico/Limpiar too, which
        // already got their own close button, or none at all, up in the mode row), and only for
        // action types with no self-committing widget of their own: Mando waits silently for a
        // virtual-pad click, Ratón has no combo/capture step to commit through. Macro/Teclado/Bot
        // already assign and close themselves via their own inner "Asignar"
        // (ActionPanel::renderMacroCombo/renderKeyboardCapture/renderBotCombo) — a second button
        // with the same label there would assign nothing, just close, so it's hidden to avoid two
        // "Asignar" on screen at once.
        bool inTouchSubPanel =
            (m_model.touchSurfaceMode == TouchpadSurfaceMode::Zones && !m_sel.touchZoneRegionSelected.empty()) ||
            (m_model.touchSurfaceMode == TouchpadSurfaceMode::Gesture && !m_sel.touchGestureSelected.empty());
        if (inTouchSubPanel &&
            (m_sel.actionType == ActionType::Xbox || m_sel.actionType == ActionType::Mouse)) {
            ImGui::Spacing();
            float assignW  = 110.0f;
            float assignOffX = (availW - assignW) * 0.5f;
            if (assignOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + assignOffX);
            const char* closeLabel = (m_sel.actionType == ActionType::Mouse) ? tr("btn.back") : tr("btn.assign");
            if (ImGui::Button(closeLabel, { assignW, 0.0f })) {
                m_sel.physComp = -1;
                m_sel.touchSurfaceSelected = false;
                m_sel.touchZoneRegionSelected.clear();
                m_sel.touchGestureSelected.clear();
            }
        }
    } else if ((selType != "stick" && selType != "gyro") || m_sel.stickAsButton) {
        // ── H5: botón seleccionado ─────────────────────────────────────────
        const auto& selPhysComp = physComps[m_sel.physComp];
        const std::string physShortSel = (selType == "stick" && m_sel.stickAsButton)
            ? stateToShort(selPhysComp.stateClick)
            : (selType == "dpad")
                ? stateToShort(dpadDirToState(selPhysComp, m_sel.dpadDir))
                : stateToShort(selPhysComp.state);

        // Label + merged hint (2026/09/03: replaces the old separate "Elige boton..." line —
        // same "<label> -> ..." pattern the trigger/axis/gyro panels already used).
        {
            std::string lbl = physButtonDisplayLabel(physShortSel) + " \xe2\x86\x92";
            if (m_sel.actionType == ActionType::Xbox)
                lbl += std::string(" ") + tr("mapper.hint_choose_action");
            else if (m_sel.actionType == ActionType::Macro)
                lbl += std::string(" ") + tr("mapper.hint_choose_macro");
            else if (m_sel.actionType == ActionType::Mouse)
                lbl += std::string(" ") + tr("mapper.hint_choose_mouse");
            else if (m_sel.actionType == ActionType::Bot)
                lbl += std::string(" ") + tr("mapper.hint_choose_bot");
            else if (m_sel.actionType == ActionType::Keyboard)
                lbl += std::string(" ") +
                       tr(m_sel.captureKeys.empty() ? "mapper.hint_press_combo" : "mapper.hint_press_more") +
                       " " + tr("mapper.hint_cancel_combo");
            float hdrW = ImGui::CalcTextSize(lbl.c_str()).x;
            float offXl = (availW - hdrW) * 0.5f;
            if (offXl > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offXl);
            ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl.c_str());
        }

        // Left half: type buttons, one row, left-aligned. Right half (same row, via the
        // Indent trick below): the content for whichever type is selected — combo/capture/
        // buttons — instead of a separate row underneath (2026/09/03).
        float colGap  = 16.0f;
        float halfW   = (availW - colGap) * 0.5f;
        float indentW = halfW + colGap;

        ActionPanel::renderActionTypeTabs("typeBtn", m_sel.actionType, m_sel.captureKeys,
                                          kStdActionTypes, halfW);

        // Jump to the right half, same row (Indent() repositions the cursor immediately and
        // also becomes the left margin every subsequent line inside this block wraps to, so
        // multi-row content like the keyboard capture stays confined to the right half).
        ImGui::SameLine();
        ImGui::Indent(indentW);

        if (m_sel.actionType == ActionType::Macro) {
            if (!m_macroNamesLoaded) {
                m_macroNames.clear(); m_macroLibrary.clear();
                try {
                    std::ifstream f(Paths::userData("data/macros.json"));
                    if (f.is_open()) {
                        json j = json::parse(f);
                        for (auto& [k,v] : j.items()) {
                            m_macroNames.push_back(k);
                            m_macroLibrary.emplace_back(k, v.get<std::string>());
                        }
                    }
                } catch (...) {}
                m_macroNamesLoaded = true;
            }
            if (m_sel.macroSel.empty() && !physShortSel.empty()) {
                auto it = m_model.actionEdits.find(physShortSel);
                if (it != m_model.actionEdits.end() && it->second.type == ButtonActionType::Macro)
                    m_sel.macroSel = it->second.name;
            }
            bool editInlineMacro = false;
            if (ActionPanel::renderMacroCombo("macButton", m_sel.macroSel, m_macroNames, halfW,
                                              tr("btn.edit_macro"), &editInlineMacro)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Macro; act.physical = physShortSel; act.name = m_sel.macroSel;
                    m_model.actionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
            }
            if (editInlineMacro && !physShortSel.empty()) {
                m_macroModalPending.ctx = MacroModalPending::Ctx::Button;
                m_macroModalPending.key = physShortSel;
                m_macroModal.setMacroLibrary(m_macroLibrary);
                std::string currentDsl;
                auto actIt = m_model.actionEdits.find(physShortSel);
                if (actIt != m_model.actionEdits.end() && actIt->second.type == ButtonActionType::Macro)
                    currentDsl = actIt->second.execution;
                m_macroModal.open(MacroCreatorModal::Mode::kInline, "", currentDsl);
            }

        } else if (m_sel.actionType == ActionType::Keyboard) {
            bool cancel = ActionPanel::isCancelSelectionCombo(physNow);
            if (cancel) {
                m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                ImGui::NewLine();
            } else if (ActionPanel::renderKeyboardCapture("kbButton", m_sel.captureKeys, halfW, true)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Keyboard; act.physical = physShortSel;
                    for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                    m_model.actionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mbButton", mbResult, halfW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::MouseClick; act.physical = physShortSel; act.mouseButton = mbResult;
                    m_model.actionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox;
            }

        } else if (m_sel.actionType == ActionType::Bot) {
            std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
            if (m_sel.botSel.empty() && !physShortSel.empty()) {
                auto it = m_model.actionEdits.find(physShortSel);
                if (it != m_model.actionEdits.end() && it->second.type == ButtonActionType::Bot)
                    m_sel.botSel = it->second.name;
            }
            if (ActionPanel::renderBotCombo("botButton", m_sel.botSel, availableBots, halfW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Bot; act.physical = physShortSel; act.name = m_sel.botSel;
                    m_model.actionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
            }
        } else {
            // Xbox: silent "waiting for a virtual pad click" state, same as every other panel —
            // just close the row (nothing to draw on the right).
            ImGui::NewLine();
        }

        ImGui::Unindent(indentW);
    } // button action panel
    // ── Stick axis action panel ───────────────────────────────────────────────
    if (m_sel.physComp >= 0) {
        const auto& stickComps = phys.getLayout().components;
        if (m_sel.physComp < (int)stickComps.size() &&
            stickComps[m_sel.physComp].type == "stick" &&
            !m_sel.stickAsButton && !m_sel.stickDir.empty()) {

            const auto& stickComp = stickComps[m_sel.physComp];
            auto [stickXId, stickYId] = stickIdsFromStateX(stickComp.stateX);
            std::string axisKey;
            if      (m_sel.stickDir == "up")    axisKey = stickYId + "_pos";
            else if (m_sel.stickDir == "down")  axisKey = stickYId + "_neg";
            else if (m_sel.stickDir == "right") axisKey = stickXId + "_pos";
            else if (m_sel.stickDir == "left")  axisKey = stickXId + "_neg";

            if (!axisKey.empty()) {
                float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;

                // Label + merged hint (2026/09/03) — replaces the old separate "Elige el semieje
                // en el panel" line.
                {
                    std::string lbl = axisKey + " \xe2\x86\x92";
                    if (m_sel.actionType == ActionType::Xbox)
                        lbl += std::string(" ") + tr("mapper.hint_choose_action");
                    else if (m_sel.actionType == ActionType::Macro)
                        lbl += std::string(" ") + tr("mapper.hint_choose_macro");
                    else if (m_sel.actionType == ActionType::Mouse)
                        lbl += std::string(" ") + tr("mapper.hint_choose_mouse");
                    else if (m_sel.actionType == ActionType::MouseMove)
                        lbl += std::string(" ") + tr("mapper.hint_choose_mousemove");
                    else if (m_sel.actionType == ActionType::Bot)
                        lbl += std::string(" ") + tr("mapper.hint_choose_bot");
                    else if (m_sel.actionType == ActionType::Keyboard)
                        lbl += std::string(" ") +
                               tr(m_sel.captureKeys.empty() ? "mapper.hint_press_combo" : "mapper.hint_press_more") +
                               " " + tr("mapper.hint_cancel_combo");
                    float hdrW = ImGui::CalcTextSize(lbl.c_str()).x;
                    float offX = (availW - hdrW) * 0.5f;
                    if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                    ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl.c_str());
                }

                // Left half: type buttons (incl. Raton-movimiento/Rangos, the axis panel's 2
                // "extra" buttons beyond the standard 5), one row, left-aligned. Right half (same
                // row, via the Indent trick below): the content for the selected type (2026/09/03).
                float colGap  = 16.0f;
                float halfW   = (availW - colGap) * 0.5f;
                float indentW = halfW + colGap;
                auto axisEdit = m_model.axisActionEdits.find(axisKey);
                bool hasRanges = (axisEdit != m_model.axisActionEdits.end() &&
                                  axisEdit->second.type == HalfAxisActionType::Ranges &&
                                  !axisEdit->second.ranges.empty());
                auto openAxisRanges = [&]() {
                    std::vector<RangeEdit> cur;
                    if (hasRanges)
                        for (const auto& tr : axisEdit->second.ranges) {
                            RangeEdit re; re.from = tr.from; re.to = tr.to;
                            re.action = tr.action; re.hasAction = tr.hasAction;
                            cur.push_back(re);
                        }
                    m_trigRangeModal.open(axisKey, cur, m_engine->getLoadedBotNames());
                    m_sel.actionType = ActionType::Xbox;
                    m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
                };

                // 1 row, all 7 buttons — kActionTypeBtnRefCount is 7 precisely so this row (the
                // widest of the 6 panels) fits without wrapping (2026/09/04).
                ActionPanel::ActionTypeExtra rangesExtra;
                rangesExtra.label   = trid("btn.ranges", "axisRanges");
                rangesExtra.active  = hasRanges;
                rangesExtra.onClick = openAxisRanges;
                ActionPanel::renderActionTypeTabs("typeBtnAxis", m_sel.actionType, m_sel.captureKeys,
                                                  kAxisActionTypes, halfW, &rangesExtra);

                // Jump to the right half, same row (see the H5 panel above for how Indent()
                // keeps multi-row content like keyboard capture confined to the right half).
                ImGui::SameLine();
                ImGui::Indent(indentW);

                if (m_sel.actionType == ActionType::Macro) {
                    if (!m_macroNamesLoaded) {
                        m_macroNames.clear(); m_macroLibrary.clear();
                        try {
                            std::ifstream f(Paths::userData("data/macros.json"));
                            if (f.is_open()) {
                                json j = json::parse(f);
                                for (auto& [k,v] : j.items()) {
                                    m_macroNames.push_back(k);
                                    m_macroLibrary.emplace_back(k, v.get<std::string>());
                                }
                            }
                        } catch (...) {}
                        m_macroNamesLoaded = true;
                    }
                    bool editInlineMacro = false;
                    if (ActionPanel::renderMacroCombo("macAxis", m_sel.macroSel, m_macroNames, halfW,
                                                      tr("btn.edit_macro"), &editInlineMacro)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Macro; ha.target = m_sel.macroSel;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
                    }
                    if (editInlineMacro && !axisKey.empty()) {
                        m_macroModalPending.ctx = MacroModalPending::Ctx::Axis;
                        m_macroModalPending.key = axisKey;
                        m_macroModal.setMacroLibrary(m_macroLibrary);
                        std::string currentDsl;
                        auto actIt = m_model.axisActionEdits.find(axisKey);
                        if (actIt != m_model.axisActionEdits.end() && actIt->second.type == HalfAxisActionType::Macro)
                            currentDsl = actIt->second.execution;
                        m_macroModal.open(MacroCreatorModal::Mode::kInline, "", currentDsl);
                    }
                } else if (m_sel.actionType == ActionType::Keyboard) {
                    bool cancel = ActionPanel::isCancelSelectionCombo(physNow);
                    if (cancel) {
                        m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                        ImGui::NewLine();
                    } else if (ActionPanel::renderKeyboardCapture("kbAxis", m_sel.captureKeys, halfW, true)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Keyboard;
                        for (const auto& p : m_sel.captureKeys) ha.keys.push_back(p.first);
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                    }
                } else if (m_sel.actionType == ActionType::Mouse) {
                    std::string mbResult;
                    if (ActionPanel::renderMouseButtons("mbAxis", mbResult, halfW)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::MouseClick; ha.mouseButton = mbResult;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox;
                    }
                } else if (m_sel.actionType == ActionType::MouseMove) {
                    // Single row (2026/09/04, was hint text on its own line above the controls):
                    // hint + speed slider + axis combo + Asignar, all together. Width estimate
                    // includes the sliders'/combo's own trailing labels ("Vel."/"Eje") since ImGui
                    // draws those inline right after the widget — an underestimate just means the
                    // offX guard below skips centering and the row starts at the left margin
                    // instead, never overflows.
                    constexpr float kSliderW = 100.0f, kComboW = 60.0f, kAssignW = 80.0f;
                    float sp      = ImGui::GetStyle().ItemSpacing.x;
                    float innerSp = ImGui::GetStyle().ItemInnerSpacing.x;
                    float rowW = ImGui::CalcTextSize(tr("mapper.axis_hint")).x + sp
                               + kSliderW + innerSp + ImGui::CalcTextSize(tr("mapper.mouse_speed")).x + sp
                               + kComboW  + innerSp + ImGui::CalcTextSize(tr("mapper.mouse_axis")).x  + sp
                               + kAssignW;
                    float offX2 = (halfW - rowW) * 0.5f;
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", tr("mapper.axis_hint"));
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(kSliderW);
                    ImGui::SliderFloat(trid("mapper.mouse_speed", "mouseSpeed").c_str(), &m_sel.axisMouseSpeed, 1.0f, 50.0f, "%.0f");
                    ImGui::SameLine();
                    const char* mouseAxes[] = { "X", "Y" };
                    int axIdx = (m_sel.axisMouseAxis == "mouse_y") ? 1 : 0;
                    ImGui::SetNextItemWidth(kComboW);
                    if (ImGui::Combo(trid("mapper.mouse_axis", "mouseAxis").c_str(), &axIdx, mouseAxes, 2))
                        m_sel.axisMouseAxis = (axIdx == 1) ? "mouse_y" : "mouse_x";
                    ImGui::SameLine();
                    if (ImGui::Button(trid("btn.assign", "mouseAssign").c_str(), { kAssignW, 0.0f })) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::MouseMove;
                        ha.target = m_sel.axisMouseAxis; ha.speed = m_sel.axisMouseSpeed;
                        m_model.axisActionEdits[axisKey] = ha;
                        // Auto-assign opposite half so the full axis controls mouse bidirectionally.
                        // halfV already carries the correct sign at runtime (_pos>0, _neg<0).
                        auto oppositeKey = [](const std::string& k) {
                            size_t p = k.rfind("_pos");
                            if (p != std::string::npos) { auto r = k; r.replace(p, 4, "_neg"); return r; }
                            size_t n = k.rfind("_neg");
                            if (n != std::string::npos) { auto r = k; r.replace(n, 4, "_pos"); return r; }
                            return k;
                        };
                        m_model.axisActionEdits[oppositeKey(axisKey)] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox;
                    }
                } else if (m_sel.actionType == ActionType::Bot) {
                    std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
                    if (m_sel.botSel.empty()) {
                        auto it = m_model.axisActionEdits.find(axisKey);
                        if (it != m_model.axisActionEdits.end() && it->second.type == HalfAxisActionType::Bot)
                            m_sel.botSel = it->second.target;
                    }
                    if (ActionPanel::renderBotCombo("botAxis", m_sel.botSel, availableBots, halfW)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Bot; ha.target = m_sel.botSel;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
                    }
                } else {
                    // Mando mode: user clicks virtual pad → onVirtHitAxisAction — silent, just
                    // close the row (nothing to draw on the right).
                    ImGui::NewLine();
                }
                ImGui::Unindent(indentW);

                // Clear button if already assigned
                if (m_model.axisActionEdits.count(axisKey)) {
                    ImGui::Spacing();
                    float clearW = 100.0f;
                    float offX3 = (availW - clearW) * 0.5f;
                    if (offX3 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX3);
                    if (ImGui::Button(trid("btn.clear", "axisClear").c_str(), { clearW, 0.0f })) {
                        auto it = m_model.axisActionEdits.find(axisKey);
                        bool isMouseMove = (it != m_model.axisActionEdits.end() &&
                                            it->second.type == HalfAxisActionType::MouseMove);
                        m_model.axisActionEdits.erase(axisKey);
                        if (isMouseMove) {
                            auto oppositeKey = [](const std::string& k) {
                                size_t p = k.rfind("_pos");
                                if (p != std::string::npos) { auto r = k; r.replace(p, 4, "_neg"); return r; }
                                size_t n = k.rfind("_neg");
                                if (n != std::string::npos) { auto r = k; r.replace(n, 4, "_pos"); return r; }
                                return k;
                            };
                            m_model.axisActionEdits.erase(oppositeKey(axisKey));
                        }
                    }
                }
            }
        }
    } // stick axis action panel

    // ── Gyro/Accel axis action panel ──────────────────────────────────────────
    if (m_sel.physComp >= 0) {
        const auto& gyroComps = phys.getLayout().components;
        if (m_sel.physComp < (int)gyroComps.size() &&
            gyroComps[m_sel.physComp].type == "gyro" && !m_sel.stickDir.empty()) {

            const std::string dir = m_sel.stickDir;
            bool dirIsYaw = (dir == "cw" || dir == "ccw");

            static const std::unordered_map<std::string, const char*> kDirLabelKeys = {
                {"up",    "mapper.gyro_dir_pitch_pos"}, {"down", "mapper.gyro_dir_pitch_neg"},
                {"right", "mapper.gyro_dir_roll_pos"},  {"left", "mapper.gyro_dir_roll_neg"},
                {"cw",    "mapper.gyro_dir_yaw_pos"},   {"ccw",  "mapper.gyro_dir_yaw_neg"},
            };
            auto dirLabelIt = kDirLabelKeys.find(dir);
            const char* dirLabel = dirLabelIt != kDirLabelKeys.end() ? tr(dirLabelIt->second) : dir.c_str();

            float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;

            // Label + merged hint (2026/09/03) — replaces the old separate "Elige el semieje en
            // el panel" line.
            {
                std::string lbl = std::string(dirLabel) + " \xe2\x86\x92";
                if (m_sel.actionType == ActionType::Xbox)
                    lbl += std::string(" ") + tr("mapper.hint_choose_action");
                else if (m_sel.actionType == ActionType::Macro)
                    lbl += std::string(" ") + tr("mapper.hint_choose_macro");
                else if (m_sel.actionType == ActionType::Mouse)
                    lbl += std::string(" ") + tr("mapper.hint_choose_mouse");
                else if (m_sel.actionType == ActionType::MouseMove)
                    lbl += std::string(" ") + tr("mapper.hint_choose_mousemove");
                else if (m_sel.actionType == ActionType::Bot)
                    lbl += std::string(" ") + tr("mapper.hint_choose_bot");
                else if (m_sel.actionType == ActionType::Keyboard)
                    lbl += std::string(" ") +
                           tr(m_sel.captureKeys.empty() ? "mapper.hint_press_combo" : "mapper.hint_press_more") +
                           " " + tr("mapper.hint_cancel_combo");
                float hdrW = ImGui::CalcTextSize(lbl.c_str()).x;
                float offX = (availW - hdrW) * 0.5f;
                if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl.c_str());
            }

            // Left/right split shared by the source toggle below and the type-button row further
            // down, so both line up on the left half.
            float colGap  = 16.0f;
            float halfW   = (availW - colGap) * 0.5f;
            float indentW = halfW + colGap;

            // Source toggle: Gyro / Accel. Whichever is the type's own default shows marked
            // until the user picks one explicitly. Hidden for yaw (cw/ccw) - accel cannot sense
            // rotation around the vertical axis while flat, always gyro. Centered within the left
            // half (2026/09/04, was centered on the full width) so it sits above the type-button
            // row instead of floating separately over the whole panel.
            if (!dirIsYaw) {
                // Representative HalfAxisActionType per tab, used only to preview the default
                // before a concrete target is picked. The "Mando" tab covers 4 different target
                // kinds (button/dpad/trigger/stick) decided only at click time — VirtualButton
                // (gyro-default) previews it since button is the most common case there.
                HalfAxisActionType previewType = HalfAxisActionType::VirtualButton;
                switch (m_sel.actionType) {
                    case ActionType::Macro:     previewType = HalfAxisActionType::Macro;      break;
                    case ActionType::Keyboard:  previewType = HalfAxisActionType::Keyboard;   break;
                    case ActionType::Mouse:     previewType = HalfAxisActionType::MouseClick; break;
                    case ActionType::MouseMove: previewType = HalfAxisActionType::MouseMove;  break;
                    case ActionType::Bot:       previewType = HalfAxisActionType::Bot;        break;
                    default: break;
                }
                bool displayAccel = m_sel.imuSourceOverridden ? m_sel.imuUseAccel
                                                               : imuDefaultUsesAccel(previewType);

                constexpr float kSrcBtnW = 70.0f;
                float totalSrcW = kSrcBtnW * 2 + ImGui::GetStyle().ItemSpacing.x;
                float offXs = (halfW - totalSrcW) * 0.5f;
                if (offXs > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offXs);
                auto srcBtn = [&](const char* label, bool active) {
                    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    bool clicked = ImGui::Button(label, { kSrcBtnW, 0.0f });
                    if (active) ImGui::PopStyleColor();
                    return clicked;
                };
                char lblSrcGyro[64], lblSrcAccel[64];
                snprintf(lblSrcGyro,  sizeof(lblSrcGyro),  "%s##imuSrcGyro",  tr("mapper.gyro_source_gyro"));
                snprintf(lblSrcAccel, sizeof(lblSrcAccel), "%s##imuSrcAccel", tr("mapper.gyro_source_accel"));
                if (srcBtn(lblSrcGyro, !displayAccel)) {
                    m_sel.imuSourceOverridden = true; m_sel.imuUseAccel = false;
                }
                ImGui::SameLine();
                if (srcBtn(lblSrcAccel, displayAccel)) {
                    m_sel.imuSourceOverridden = true; m_sel.imuUseAccel = true;
                }
            }

            // Left half: type buttons (incl. Raton-movimiento/Rangos, the gyro panel's 2 "extra"
            // buttons beyond the standard 5), one row, left-aligned. Right half (same row, via
            // the Indent trick — see the H5 panel above): content for the selected type (2026/09/03).
            std::string rangesKey;
            auto& rangesMap = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::Ranges, rangesKey);
            auto gyAxisEdit = rangesMap.find(rangesKey);
            bool hasRanges = (gyAxisEdit != rangesMap.end() &&
                              gyAxisEdit->second.type == HalfAxisActionType::Ranges &&
                              !gyAxisEdit->second.ranges.empty());
            auto openGyroRanges = [&]() {
                std::vector<RangeEdit> cur;
                if (hasRanges)
                    for (const auto& tr : gyAxisEdit->second.ranges) {
                        RangeEdit re; re.from = tr.from; re.to = tr.to;
                        re.action = tr.action; re.hasAction = tr.hasAction;
                        cur.push_back(re);
                    }
                bool usedAccel = (&rangesMap == &m_model.accelActionEdits);
                m_trigRangeModal.open((usedAccel ? "accel_" : "gyro_") + rangesKey, cur,
                                      m_engine->getLoadedBotNames());
                m_sel.actionType = ActionType::Xbox;
                m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
            };

            // 1 row, all 7 buttons — kActionTypeBtnRefCount is 7 precisely so this row (the
            // widest of the 6 panels) fits without wrapping (2026/09/04).
            ActionPanel::ActionTypeExtra rangesExtra;
            rangesExtra.label   = trid("btn.ranges", "gyroRanges");
            rangesExtra.active  = hasRanges;
            rangesExtra.onClick = openGyroRanges;
            ActionPanel::renderActionTypeTabs("typeBtnGyro", m_sel.actionType, m_sel.captureKeys,
                                              kAxisActionTypes, halfW, &rangesExtra);

            ImGui::SameLine();
            ImGui::Indent(indentW);

            if (m_sel.actionType == ActionType::Macro) {
                if (!m_macroNamesLoaded) {
                    m_macroNames.clear(); m_macroLibrary.clear();
                    try {
                        std::ifstream f(Paths::userData("data/macros.json"));
                        if (f.is_open()) {
                            json j = json::parse(f);
                            for (auto& [k,v] : j.items()) {
                                m_macroNames.push_back(k);
                                m_macroLibrary.emplace_back(k, v.get<std::string>());
                            }
                        }
                    } catch (...) {}
                    m_macroNamesLoaded = true;
                }
                bool editInlineMacro = false;
                if (ActionPanel::renderMacroCombo("macGyro", m_sel.macroSel, m_macroNames, halfW,
                                                  tr("btn.edit_macro"), &editInlineMacro)) {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::Macro; ha.target = m_sel.macroSel;
                    // Below the 0.5 (50% of the calibrated comfortable-max) struct default — a
                    // relaxed gesture should fire a digital action without reaching that ceiling,
                    // which analog output still uses in full. No UI control on purpose (2026/08/10
                    // discussion): revisit this number by hand if it still feels excessive.
                    ha.threshold = 0.4f;
                    assignImuAction(m_sel, m_model, dir, ha);
                    m_sel.physComp = -1; m_sel.stickDir.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
                }
                if (editInlineMacro) {
                    std::string key;
                    auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::Macro, key);
                    bool usedAccel = (&map == &m_model.accelActionEdits);
                    m_macroModalPending.ctx = MacroModalPending::Ctx::Gyro;
                    m_macroModalPending.key = (usedAccel ? "accel_" : "gyro_") + key;
                    m_macroModal.setMacroLibrary(m_macroLibrary);
                    std::string currentDsl;
                    auto actIt = map.find(key);
                    if (actIt != map.end() && actIt->second.type == HalfAxisActionType::Macro)
                        currentDsl = actIt->second.execution;
                    m_macroModal.open(MacroCreatorModal::Mode::kInline, "", currentDsl);
                }
            } else if (m_sel.actionType == ActionType::Keyboard) {
                bool cancel = ActionPanel::isCancelSelectionCombo(physNow);
                if (cancel) {
                    m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                    ImGui::NewLine();
                } else if (ActionPanel::renderKeyboardCapture("kbGyro", m_sel.captureKeys, halfW, true)) {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::Keyboard;
                    for (const auto& p : m_sel.captureKeys) ha.keys.push_back(p.first);
                    ha.threshold = 0.4f;  // see Macro assign above
                    assignImuAction(m_sel, m_model, dir, ha);
                    m_sel.physComp = -1; m_sel.stickDir.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                }
            } else if (m_sel.actionType == ActionType::Mouse) {
                std::string mbResult;
                if (ActionPanel::renderMouseButtons("mbGyro", mbResult, halfW)) {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::MouseClick; ha.mouseButton = mbResult;
                    ha.threshold = 0.4f;  // see Macro assign above
                    assignImuAction(m_sel, m_model, dir, ha);
                    m_sel.physComp = -1; m_sel.stickDir.clear();
                    m_sel.actionType = ActionType::Xbox;
                }
            } else if (m_sel.actionType == ActionType::MouseMove) {
                // Single row (2026/09/04, was hint text on its own line above the controls): hint
                // + speed slider + axis combo + Invertir + Asignar, all together. Same approach as
                // the Analogico panel above (see its comment) — width estimate includes the
                // widgets' own trailing labels, an underestimate just falls back to left-aligned.
                constexpr float kSliderW = 100.0f, kComboW = 60.0f, kAssignW = 80.0f;
                float sp      = ImGui::GetStyle().ItemSpacing.x;
                float innerSp = ImGui::GetStyle().ItemInnerSpacing.x;
                float rowW = ImGui::CalcTextSize(tr("mapper.axis_hint")).x + sp
                           + kSliderW + innerSp + ImGui::CalcTextSize(tr("mapper.mouse_speed")).x + sp
                           + kComboW  + innerSp + ImGui::CalcTextSize(tr("mapper.mouse_axis")).x  + sp
                           + ImGui::GetFrameHeight() + innerSp + ImGui::CalcTextSize(tr("mapper.mouse_invert")).x + sp
                           + kAssignW;
                float offX2 = (halfW - rowW) * 0.5f;
                if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);

                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", tr("mapper.axis_hint"));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(kSliderW);
                ImGui::SliderFloat(trid("mapper.mouse_speed", "gyroMouseSpeed").c_str(), &m_sel.axisMouseSpeed, 1.0f, 50.0f, "%.0f");
                ImGui::SameLine();
                const char* mouseAxes[] = { "X", "Y" };
                int axIdx = (m_sel.axisMouseAxis == "mouse_y") ? 1 : 0;
                ImGui::SetNextItemWidth(kComboW);
                if (ImGui::Combo(trid("mapper.mouse_axis", "gyroMouseAxis").c_str(), &axIdx, mouseAxes, 2)) {
                    m_sel.axisMouseAxis = (axIdx == 1) ? "mouse_y" : "mouse_x";
                    // Default suggestion: Y is the known pitch case that needs the "pointer"
                    // convention (aim down = cursor down); X has no known bug, starts unchecked.
                    m_sel.axisMouseInvert = (axIdx == 1);
                }
                ImGui::SameLine();
                ImGui::Checkbox(trid("mapper.mouse_invert", "gyroMouseInvert").c_str(), &m_sel.axisMouseInvert);
                ImGui::SameLine();
                if (ImGui::Button(trid("btn.assign", "gyroMouseAssign").c_str(), { kAssignW, 0.0f })) {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::MouseMove;
                    ha.target = m_sel.axisMouseAxis; ha.speed = m_sel.axisMouseSpeed; ha.invert = m_sel.axisMouseInvert;
                    assignImuAction(m_sel, m_model, dir, ha);
                    // Auto-assign the opposite logical direction too, so the whole axis controls
                    // the mouse bidirectionally (same sensor, resolved independently but
                    // consistently since the override/default only depends on type+yaw-ness).
                    std::string oppDir = oppositeGyroDir(dir);
                    if (!oppDir.empty()) assignImuAction(m_sel, m_model, oppDir, ha);
                    m_sel.physComp = -1; m_sel.stickDir.clear();
                    m_sel.actionType = ActionType::Xbox;
                }
            } else if (m_sel.actionType == ActionType::Bot) {
                std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
                if (m_sel.botSel.empty()) {
                    std::string key;
                    auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::Bot, key);
                    auto it = map.find(key);
                    if (it != map.end() && it->second.type == HalfAxisActionType::Bot)
                        m_sel.botSel = it->second.target;
                }
                if (ActionPanel::renderBotCombo("botGyro", m_sel.botSel, availableBots, halfW)) {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::Bot; ha.target = m_sel.botSel;
                    ha.threshold = 0.4f;  // see Macro assign above
                    assignImuAction(m_sel, m_model, dir, ha);
                    m_sel.physComp = -1; m_sel.stickDir.clear();
                    m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
                }
            } else {
                // Mando mode: user clicks virtual pad → onVirtHitGyroAction — silent, just close
                // the row (nothing to draw on the right).
                ImGui::NewLine();
            }
            ImGui::Unindent(indentW);

            // Clear button if already assigned (checks whichever sensor currently holds it)
            {
                bool inGyro  = m_model.gyroActionEdits.count(gyroKeyFromDir(dir))  > 0;
                bool inAccel = !accelKeyFromDir(dir).empty() &&
                               m_model.accelActionEdits.count(accelKeyFromDir(dir)) > 0;
                if (inGyro || inAccel) {
                    ImGui::Spacing();
                    float clearW = 100.0f;
                    float offX3 = (availW - clearW) * 0.5f;
                    if (offX3 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX3);
                    if (ImGui::Button(trid("btn.clear", "gyroClear").c_str(), { clearW, 0.0f })) {
                        std::string gk = gyroKeyFromDir(dir), ak = accelKeyFromDir(dir);
                        bool wasMouseMove =
                            (m_model.gyroActionEdits.count(gk) &&
                             m_model.gyroActionEdits[gk].type == HalfAxisActionType::MouseMove) ||
                            (!ak.empty() && m_model.accelActionEdits.count(ak) &&
                             m_model.accelActionEdits[ak].type == HalfAxisActionType::MouseMove);
                        if (!gk.empty()) m_model.gyroActionEdits.erase(gk);
                        if (!ak.empty()) m_model.accelActionEdits.erase(ak);
                        if (wasMouseMove) {
                            std::string oppDir = oppositeGyroDir(dir);
                            std::string ogk = gyroKeyFromDir(oppDir), oak = accelKeyFromDir(oppDir);
                            if (!ogk.empty()) m_model.gyroActionEdits.erase(ogk);
                            if (!oak.empty()) m_model.accelActionEdits.erase(oak);
                        }
                    }
                }
            }
        }
    } // gyro/accel axis action panel
    } // action panels

    // ── Trigger action panel ─────────────────────────────────────────────────
    if (!m_sel.triggerSrc.empty()) {
        ImGui::Spacing();
        float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;

        {
            std::string lbl = (m_sel.triggerSrc == "l2") ? "L2 \xe2\x86\x92" : "R2 \xe2\x86\x92";
            if (m_sel.actionType == ActionType::Xbox)
                lbl += std::string(" ") + tr("mapper.hint_choose_action");
            else if (m_sel.actionType == ActionType::Macro)
                lbl += std::string(" ") + tr("mapper.hint_choose_macro");
            else if (m_sel.actionType == ActionType::Mouse)
                lbl += std::string(" ") + tr("mapper.hint_choose_mouse");
            else if (m_sel.actionType == ActionType::Bot)
                lbl += std::string(" ") + tr("mapper.hint_choose_bot");
            else if (m_sel.actionType == ActionType::Keyboard)
                lbl += std::string(" ") +
                       tr(m_sel.captureKeys.empty() ? "mapper.hint_press_combo" : "mapper.hint_press_more") +
                       " " + tr("mapper.hint_cancel_combo");
            float hdrW = ImGui::CalcTextSize(lbl.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - hdrW) * 0.5f);
            ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl.c_str());
        }

        // Left half: type buttons (incl. Rangos, the trigger panel's 1 "extra" button beyond the
        // standard 5), one row, left-aligned. Right half (same row, via the Indent trick — see
        // the H5 panel above): content for the selected type (2026/09/03).
        float colGap  = 16.0f;
        float halfW   = (availW - colGap) * 0.5f;
        float indentW = halfW + colGap;
        const std::vector<RangeEdit>& curRanges = (m_sel.triggerSrc == "l2") ? m_model.trigLRangeEdits : m_model.trigRRangeEdits;
        bool hasRanges = !curRanges.empty();

        ActionPanel::ActionTypeExtra rangesExtra;
        rangesExtra.label   = trid("btn.ranges", "trigRanges");
        rangesExtra.active  = hasRanges;
        rangesExtra.onClick = [&]() {
            m_trigRangeModal.open(m_sel.triggerSrc, curRanges, m_engine->getLoadedBotNames());
            m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
        };
        ActionPanel::renderActionTypeTabs("typeBtnTrigger", m_sel.actionType, m_sel.captureKeys,
                                          kStdActionTypes, halfW, &rangesExtra);

        ImGui::SameLine();
        ImGui::Indent(indentW);

        if (m_sel.actionType == ActionType::Macro) {
            if (!m_macroNamesLoaded) {
                m_macroNames.clear(); m_macroLibrary.clear();
                try {
                    std::ifstream f(Paths::userData("data/macros.json"));
                    if (f.is_open()) {
                        json j = json::parse(f);
                        for (auto& [k,v] : j.items()) {
                            m_macroNames.push_back(k);
                            m_macroLibrary.emplace_back(k, v.get<std::string>());
                        }
                    }
                } catch (...) {}
                m_macroNamesLoaded = true;
            }
            bool editInlineMacro = false;
            if (ActionPanel::renderMacroCombo("macTrigger", m_sel.macroSel, m_macroNames, halfW,
                                              tr("btn.edit_macro"), &editInlineMacro)) {
                ButtonAction act;
                act.type = ButtonActionType::Macro; act.physical = m_sel.triggerSrc; act.name = m_sel.macroSel;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
            }
            if (editInlineMacro && !m_sel.triggerSrc.empty()) {
                m_macroModalPending.ctx = MacroModalPending::Ctx::Trigger;
                m_macroModalPending.key = m_sel.triggerSrc;
                m_macroModal.setMacroLibrary(m_macroLibrary);
                std::string currentDsl;
                auto actIt = m_model.trigActionEdits.find(m_sel.triggerSrc);
                if (actIt != m_model.trigActionEdits.end() && actIt->second.type == ButtonActionType::Macro)
                    currentDsl = actIt->second.execution;
                m_macroModal.open(MacroCreatorModal::Mode::kInline, "", currentDsl);
            }

        } else if (m_sel.actionType == ActionType::Keyboard) {
            bool cancel = ActionPanel::isCancelSelectionCombo(physNow);
            if (cancel) {
                m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                ImGui::NewLine();
            } else if (ActionPanel::renderKeyboardCapture("kbTrigger", m_sel.captureKeys, halfW, true)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard; act.physical = m_sel.triggerSrc;
                for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mbTrigger", mbResult, halfW)) {
                ButtonAction act;
                act.type = ButtonActionType::MouseClick; act.physical = m_sel.triggerSrc; act.mouseButton = mbResult;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
            }

        } else if (m_sel.actionType == ActionType::Bot) {
            std::vector<std::string> availableBots = m_engine->getLoadedBotNames();
            if (m_sel.botSel.empty() && !m_sel.triggerSrc.empty()) {
                auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
                if (it != m_model.trigActionEdits.end() && it->second.type == ButtonActionType::Bot)
                    m_sel.botSel = it->second.name;
            }
            if (ActionPanel::renderBotCombo("botTrigger", m_sel.botSel, availableBots, halfW)) {
                ButtonAction act;
                act.type = ButtonActionType::Bot; act.physical = m_sel.triggerSrc; act.name = m_sel.botSel;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
            }
        } else {
            // Mando/Analogico: user clicks virtual pad / stick arrow → onVirtHitPhysButton — silent,
            // just close the row (nothing to draw on the right).
            ImGui::NewLine();
        }
        ImGui::Unindent(indentW);
    } // trigger action panel

    // ── Modal Rangos ──────────────────────────────────────────────────────────
    if (m_trigRangeModal.render()) {
        const std::string& key = m_trigRangeModal.forKey();
        if (key == "l2" || key == "r2") {
            if (key == "l2") m_model.trigLRangeEdits = m_trigRangeModal.result();
            else             m_model.trigRRangeEdits = m_trigRangeModal.result();
            m_model.trigActionEdits.erase(key);
            m_sel.triggerSrc.clear();
        } else if (key.rfind("gyro_", 0) == 0 || key.rfind("accel_", 0) == 0) {
            // Gyro/accel direction ranges — key is "gyro_x_pos"/"accel_y_neg"/…
            bool isAccel = key.rfind("accel_", 0) == 0;
            std::string sensorKey = key.substr(isAccel ? 6 : 5);
            std::string dir = isAccel ? dirFromAccelKey(sensorKey) : dirFromGyroKey(sensorKey);
            auto& map = isAccel ? m_model.accelActionEdits : m_model.gyroActionEdits;
            const auto& edits = m_trigRangeModal.result();
            if (edits.empty()) {
                map.erase(sensorKey);
            } else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Ranges;
                for (const auto& re : edits) {
                    TriggerRange tr; tr.from = re.from; tr.to = re.to;
                    tr.action = re.action; tr.hasAction = re.hasAction;
                    ha.ranges.push_back(tr);
                }
                if (!dir.empty()) clearImuOtherMap(m_model, dir, isAccel);
                map[sensorKey] = ha;
            }
            m_sel.physComp = -1; m_sel.stickDir.clear();
        } else {
            // Axis direction ranges
            const auto& edits = m_trigRangeModal.result();
            if (edits.empty()) {
                m_model.axisActionEdits.erase(key);
            } else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Ranges;
                for (const auto& re : edits) {
                    TriggerRange tr; tr.from = re.from; tr.to = re.to;
                    tr.action = re.action; tr.hasAction = re.hasAction;
                    ha.ranges.push_back(tr);
                }
                m_model.axisActionEdits[key] = ha;
            }
            m_sel.physComp = -1; m_sel.stickDir.clear();
        }
        m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    }

    // ── Modal macro inline ────────────────────────────────────────────────────
    if (m_macroModal.render()) {
        const std::string ex = m_macroModal.getExecution();
        if (!ex.empty() && m_macroModalPending.ctx != MacroModalPending::Ctx::None) {
            const std::string& key = m_macroModalPending.key;
            if (m_macroModalPending.ctx == MacroModalPending::Ctx::Button) {
                ButtonAction act;
                act.type = ButtonActionType::Macro;
                act.physical = key; act.name = ""; act.execution = ex;
                m_model.actionEdits[key] = act;
                m_model.buttonEdits.erase(key);
            } else if (m_macroModalPending.ctx == MacroModalPending::Ctx::Axis) {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Macro; ha.target = ""; ha.execution = ex;
                m_model.axisActionEdits[key] = ha;
            } else if (m_macroModalPending.ctx == MacroModalPending::Ctx::Trigger) {
                ButtonAction act;
                act.type = ButtonActionType::Macro;
                act.physical = key; act.name = ""; act.execution = ex;
                m_model.trigActionEdits[key] = act;
            } else if (m_macroModalPending.ctx == MacroModalPending::Ctx::Gyro) {
                // key is "gyro_x_pos"/"accel_y_neg"/… (set when the inline modal was opened).
                bool isAccel = key.rfind("accel_", 0) == 0;
                std::string sensorKey = key.substr(isAccel ? 6 : 5);
                std::string dir = isAccel ? dirFromAccelKey(sensorKey) : dirFromGyroKey(sensorKey);
                auto& map = isAccel ? m_model.accelActionEdits : m_model.gyroActionEdits;
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Macro; ha.target = ""; ha.execution = ex;
                if (!dir.empty()) clearImuOtherMap(m_model, dir, isAccel);
                map[sensorKey] = ha;
            }
        }
        m_macroModalPending.ctx = MacroModalPending::Ctx::None;
        m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
        m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear(); m_sel.botSel.clear();
        m_sel.stickDir.clear(); m_sel.triggerSrc.clear();
    }

    // ── Gestión de clicks ─────────────────────────────────────────────────────
    if (mouseClicked)
        handleClick(phys, virt, mouse);
}

// ---------------------------------------------------------------------------
// Click handling — chained dispatch
// ---------------------------------------------------------------------------
void MappingEditor::handleClick(PadView& phys, PadView& virt, ImVec2 mouse) {
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;

    // Zonas/Gestos: while a region or gesture is selected and waiting for an action, OTHER
    // physical components (button/stick/dpad/gyro) must stop reacting — an accidental click there
    // would otherwise silently abandon the in-progress assignment. The touchpad itself and the
    // virtual pad both still need to react normally below: the touchpad to switch/deselect
    // regions/gestures, the virtual pad because that's how the Gamepad/Xbox tab actually assigns
    // anything (click a button/dpad-direction/trigger there -> onVirtHitTouchZone/
    // onVirtHitTouchGesture, reached via the normal dispatch further down). See ARCHITECTURE.md
    // "Touchpad" -> "Zonas"/"Movimiento". Guarded on physComp still pointing at a real component
    // as a second line of defense — activate()/activateProfile() now call m_sel.clear() as the
    // primary fix for stale selection surviving a reset.
    if ((!m_sel.touchZoneRegionSelected.empty() || !m_sel.touchGestureSelected.empty()) &&
        m_sel.physComp >= 0 && m_sel.physComp < (int)phys.getLayout().components.size()) {
        int otherPhysHit = phys.hitTest(mouse, m_physOrigin);
        if (otherPhysHit >= 0 && phys.getLayout().components[otherPhysHit].type != "touchpad")
            return;
        // Touchpad click, virtual pad click, or empty space — fall through to the normal
        // dispatch below (the arrow hit-tests ahead of it target stick/gyro geometry the
        // touchpad/virtual pad don't have, so they're harmless no-ops here).
    }

    std::string arrowDir;
    int arrowComp = phys.hitTestStickArrow(mouse, m_physOrigin, arrowDir);
    if (arrowComp >= 0) { onArrowHit(arrowComp, arrowDir); return; }

    std::string gyroArrowDir;
    int gyroArrowComp = phys.hitTestGyroArrow(mouse, m_physOrigin, gyroArrowDir);
    if (gyroArrowComp >= 0) { onGyroArrowHit(gyroArrowComp, gyroArrowDir); return; }

    int physHit = phys.hitTest(mouse, m_physOrigin);
    if (physHit >= 0) {
        const std::string& hitType = phys.getLayout().components[physHit].type;
        if      (hitType == "button") onPhysButtonHit(phys, physHit);
        else if (hitType == "stick")  onPhysStickHit(physHit);
        else if (hitType == "dpad")   onPhysDpadHit(phys, physHit, mouse);
        else if (hitType == "touchpad") onPhysTouchpadHit(phys, physHit, mouse);
        return;
    }

    // Virtual stick arrows: assign selected source to a stick slot.
    {
        std::string virtArrowDir;
        int virtArrowComp = virt.hitTestStickArrow(mouse, m_virtOrigin, virtArrowDir);
        if (virtArrowComp >= 0) {
            bool selIsAxisSource = m_sel.physComp >= 0 &&
                                    (phys.getLayout().components[m_sel.physComp].type == "stick" ||
                                     phys.getLayout().components[m_sel.physComp].type == "gyro") &&
                                    !m_sel.stickAsButton;
            // Superficie (touch channel) never goes through this per-half-axis arrow-click path,
            // even in Analog mode: the surface is assigned to a whole stick (both axes at once)
            // via its own target-stick selector in the action panel, not by clicking one arrow
            // at a time like a digital source would — see ARCHITECTURE.md, Touchpad "Analógico".
            bool selIsTouchSurface = m_sel.physComp >= 0 && m_sel.touchSurfaceSelected &&
                                      phys.getLayout().components[m_sel.physComp].type == "touchpad";
            bool hasSource = (m_sel.physComp >= 0 && !selIsAxisSource && !selIsTouchSurface) ||
                             (!m_sel.triggerSrc.empty() && m_sel.actionType == ActionType::Xbox);
            if (hasSource) { onVirtArrowHit(phys, virt, virtArrowComp, virtArrowDir); return; }
        }
    }

    if (m_sel.physComp >= 0) {
        const std::string& selType = phys.getLayout().components[m_sel.physComp].type;
        if (selType == "gyro") {
            if (!m_sel.stickDir.empty() && m_sel.actionType == ActionType::Xbox)
                onVirtHitGyroAction(phys, virt, mouse);
        } else if (selType == "stick" && !m_sel.stickAsButton) {
            if (!m_sel.stickDir.empty() && m_sel.actionType == ActionType::Xbox)
                onVirtHitAxisAction(phys, virt, mouse);
            else if (m_sel.stickDir.empty())
                onVirtHitPhysStick(phys, virt, mouse);
        } else if (selType == "touchpad" && m_sel.touchSurfaceSelected) {
            // Superficie: Mouse has no target to assign, Analog is assigned via its own
            // target-stick buttons in the action panel (whole surface -> whole stick, not
            // per-arrow). Zones and Gesture are the two cases with a real click-to-assign target
            // — a region/gesture on the Gamepad/Xbox tab, waiting for a virtual pad click (button/
            // dpad-direction/trigger), same as any other component's Xbox tab.
            if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Zones &&
                !m_sel.touchZoneRegionSelected.empty() && m_sel.actionType == ActionType::Xbox) {
                onVirtHitTouchZone(virt, mouse);
            } else if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Gesture &&
                       !m_sel.touchGestureSelected.empty() && m_sel.actionType == ActionType::Xbox) {
                onVirtHitTouchGesture(virt, mouse);
            }
        } else if (m_sel.actionType == ActionType::Xbox) {
            onVirtHitPhysButton(phys, virt, mouse);
        }
        return;
    }

    if (!m_sel.triggerSrc.empty() && m_sel.actionType == ActionType::Xbox)
        onVirtHitTriggerSrc(virt, mouse);
}

// ---------------------------------------------------------------------------
void MappingEditor::onArrowHit(int arrowComp, const std::string& dir) {
    if (m_sel.physComp == arrowComp && m_sel.stickDir == dir && !m_sel.stickAsButton) {
        m_sel.physComp = -1; m_sel.stickDir.clear(); m_sel.stickAsButton = false;
    } else {
        m_sel.physComp = arrowComp; m_sel.stickDir = dir; m_sel.stickAsButton = false;
        m_sel.actionType = ActionType::Xbox;
        m_sel.triggerSrc.clear(); m_sel.dpadDir.clear();
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    }
}

// ---------------------------------------------------------------------------
void MappingEditor::onGyroArrowHit(int arrowComp, const std::string& dir) {
    if (m_sel.physComp == arrowComp && m_sel.stickDir == dir) {
        m_sel.physComp = -1; m_sel.stickDir.clear();
    } else {
        m_sel.physComp = arrowComp; m_sel.stickDir = dir; m_sel.stickAsButton = false;
        m_sel.actionType = ActionType::Xbox;
        m_sel.triggerSrc.clear(); m_sel.dpadDir.clear();
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
        m_sel.imuUseAccel = false; m_sel.imuSourceOverridden = false;
    }
}

// resolveImuTargetMap/clearImuOtherMap/assignImuAction moved to free functions in
// MappingSelection.h (Tarea 3b) — see the call sites below, now passing m_sel/m_model explicitly.

// ---------------------------------------------------------------------------
void MappingEditor::onPhysButtonHit(PadView& phys, int physHit) {
    const std::string& hitState = phys.getLayout().components[physHit].state;
    if (hitState == "triggerL" || hitState == "triggerR") {
        std::string trigSrc = (hitState == "triggerL") ? "l2" : "r2";
        if (m_sel.triggerSrc == trigSrc) {
            m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
        } else {
            m_sel.triggerSrc = trigSrc; m_sel.physComp = -1;
            m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
        }
    } else if (physHit == m_sel.physComp) {
        m_sel.physComp = -1; m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    } else {
        m_sel.physComp = physHit; m_sel.triggerSrc.clear();
        m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    }
}

// ---------------------------------------------------------------------------
void MappingEditor::onPhysStickHit(int physHit) {
    if (physHit == m_sel.physComp && m_sel.stickAsButton) {
        m_sel.physComp = -1; m_sel.stickAsButton = false;
    } else {
        m_sel.physComp = physHit; m_sel.stickAsButton = true; m_sel.stickDir.clear();
        m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    }
}

// ---------------------------------------------------------------------------
void MappingEditor::onPhysDpadHit(PadView& phys, int physHit, ImVec2 mouse) {
    const PadComponent& dc = phys.getLayout().components[physHit];
    std::string dir = dpadDirFromMouse(mouse, m_physOrigin.x + dc.cx, m_physOrigin.y + dc.cy);
    if (physHit == m_sel.physComp && m_sel.dpadDir == dir) {
        m_sel.physComp = -1; m_sel.dpadDir.clear();
    } else {
        m_sel.physComp = physHit; m_sel.triggerSrc.clear(); m_sel.dpadDir = dir;
        m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    }
}

// ---------------------------------------------------------------------------
// Touchpad split hit: left half (finger icon) = Superficie/touch channel, right half
// (button icon) = Botón/btnTouch channel — same left-reads-first, touch-over-click priority
// technique dpad already uses for its 4 directions (see dpadDirFromMouse).
// ---------------------------------------------------------------------------
void MappingEditor::onPhysTouchpadHit(PadView& phys, int physHit, ImVec2 mouse) {
    // Zonas: a template is loaded, so the surface behaves like N buttons (one per region) instead
    // of the Boton/Superficie left-right split below. Same toggle-select/deselect shape as that
    // split, just keyed by region id instead of a bool half.
    if (m_model.touchSurfaceMode == TouchpadSurfaceMode::Zones && !m_model.touchZones.empty()) {
        std::string regionId;
        int zoneHit = phys.hitTestZoneRegion(mouse, m_physOrigin, m_model.touchZones, regionId);
        if (zoneHit < 0) return;  // clicked the touchpad but outside every region
        if (zoneHit == m_sel.physComp && m_sel.touchZoneRegionSelected == regionId) {
            m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
        } else {
            m_sel.physComp = zoneHit; m_sel.triggerSrc.clear(); m_sel.dpadDir.clear();
            // Zones lives inside the Superficie panel (same place surfaceMode itself is chosen) —
            // route there like a normal Superficie selection, not the Boton/click-target side.
            m_sel.touchSurfaceSelected = true;
            m_sel.touchZoneRegionSelected = regionId;
            m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
        }
        return;
    }

    const PadComponent& tc = phys.getLayout().components[physHit];
    bool surfaceHalf = mouse.x < (m_physOrigin.x + tc.cx);
    if (physHit == m_sel.physComp && m_sel.touchSurfaceSelected == surfaceHalf) {
        m_sel.physComp = -1; m_sel.touchSurfaceSelected = false;
    } else {
        m_sel.physComp = physHit; m_sel.triggerSrc.clear(); m_sel.dpadDir.clear();
        m_sel.touchSurfaceSelected = surfaceHalf;
        m_sel.touchZoneRegionSelected.clear();
        m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
    }
}

// ---------------------------------------------------------------------------
// Virtual pad click when a button / stick-as-button / dpad is selected
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitPhysButton(PadView& phys, PadView& virt, ImVec2 mouse) {
    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComp  = virt.getLayout().components[virtHit];
    const auto& selPC     = phys.getLayout().components[m_sel.physComp];
    const std::string& selType = selPC.type;

    std::string physShort;
    if (selType == "stick")
        physShort = stateToShort(selPC.stateClick);
    else if (selType == "dpad")
        physShort = stateToShort(dpadDirToState(selPC, m_sel.dpadDir));
    else
        physShort = stateToShort(selPC.state);

    std::string virtShort;
    if (virtComp.type == "button")
        virtShort = stateToShort(virtComp.state);
    else if (virtComp.type == "stick" && !virtComp.stateClick.empty())
        virtShort = stateToShort(virtComp.stateClick);
    else if (virtComp.type == "dpad") {
        std::string vdir = dpadDirFromMouse(mouse,
            m_virtOrigin.x + virtComp.cx, m_virtOrigin.y + virtComp.cy);
        virtShort = stateToShort(dpadDirToState(virtComp, vdir));
    }

    if (!physShort.empty() && !virtShort.empty()) {
        if (virtShort == "triggerL" || virtShort == "triggerR") {
            std::string trigTarget = (virtShort == "triggerL") ? "l2" : "r2";
            auto trigAssignIt = m_model.actionEdits.find(physShort);
            bool already = (trigAssignIt != m_model.actionEdits.end() &&
                            trigAssignIt->second.type == ButtonActionType::Trigger &&
                            trigAssignIt->second.target == trigTarget);
            if (already) {
                m_model.actionEdits.erase(physShort);
                m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
            } else {
                ButtonAction act;
                act.type = ButtonActionType::Trigger; act.physical = physShort; act.target = trigTarget;
                m_model.actionEdits[physShort] = act;
                m_model.buttonEdits.erase(physShort);
                m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = virtShort;
            }
        } else {
            m_model.actionEdits.erase(physShort);
            auto it = m_model.buttonEdits.find(physShort);
            bool alreadyAssigned = (it != m_model.buttonEdits.end() && it->second == virtShort);
            m_model.buttonEdits[physShort] = alreadyAssigned ? "" : virtShort;
            m_sel.flashComp      = alreadyAssigned ? -1 : virtHit;
            m_sel.flashTimer     = alreadyAssigned ? 0.0f : 0.5f;
            m_sel.flashVirtShort = alreadyAssigned ? "" : virtShort;
        }
    }
    m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
}

// ---------------------------------------------------------------------------
// Virtual pad click when a Zonas region is selected (Gamepad/Xbox tab) — same virtShort
// resolution as onVirtHitPhysButton (button / stick-click / dpad-direction / trigger), but the
// "source" is the region id, not a physical component's state string, and the result goes into
// touchZoneActionEdits instead of buttonEdits/actionEdits. Also checks the virtual stick arrows
// first, same convention onVirtHitAxisAction uses for a physical half-axis source: a region can
// target a stick half-axis (StickSlot) too, stored as a VirtualButton action whose name is the
// slot dir string (e.g. "left_x_pos") — the same encoding isStickSlotDir()/applyVirtualBtnByName
// already handle generically, no touch-specific plumbing needed on the config/engine side.
//
// IMPORTANT: this is the MOUSE-click path only. There is a second, independent path for the same
// assignment via H9 "Paso 2" (holding the source ~1s, then completing the target with a physical
// button press instead of the mouse) — search this file for "isTouchZoneSource" inside render(),
// a few hundred lines above this function (there is no separate handler function for it, it's
// inline in the big per-frame H9 block). A touch zone/gesture went unassignable via Paso 2 for a
// full day (2026/08/23-24) because only THIS mouse path got updated when Zonas/Gestos were added —
// Paso 2 didn't know either map existed and silently wrote nothing (mirrors the exact same gap
// gyro/accel had when first added, see BITACORA.md 2026/08/05, bugs 1/4/5). Whenever a NEW source
// type is added to the Mando tab, update BOTH paths, not just this one — they don't share code, so
// nothing forces the second one to be remembered.
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitTouchZone(PadView& virt, ImVec2 mouse) {
    const std::string& regionSel = m_sel.touchZoneRegionSelected;

    std::string virtArrowDir;
    int virtArrowComp = virt.hitTestStickArrow(mouse, m_virtOrigin, virtArrowDir);
    if (virtArrowComp >= 0) {
        const auto& virtComps = virt.getLayout().components;
        auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtArrowComp].stateX);
        if (!vxId.empty()) {
            std::string slotKey;
            if      (virtArrowDir == "up")    slotKey = vyId + "_pos";
            else if (virtArrowDir == "down")  slotKey = vyId + "_neg";
            else if (virtArrowDir == "right") slotKey = vxId + "_pos";
            else if (virtArrowDir == "left")  slotKey = vxId + "_neg";
            if (!slotKey.empty()) {
                auto it = m_model.touchZoneActionEdits.find(regionSel);
                bool already = (it != m_model.touchZoneActionEdits.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == slotKey);
                if (already) {
                    m_model.touchZoneActionEdits.erase(regionSel);
                    m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f;
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = regionSel; act.name = slotKey;
                    m_model.touchZoneActionEdits[regionSel] = act;
                    m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
                }
            }
        }
        m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
        return;
    }

    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComp = virt.getLayout().components[virtHit];

    std::string virtShort;
    if (virtComp.type == "button")
        virtShort = stateToShort(virtComp.state);
    else if (virtComp.type == "stick" && !virtComp.stateClick.empty())
        virtShort = stateToShort(virtComp.stateClick);
    else if (virtComp.type == "dpad") {
        std::string vdir = dpadDirFromMouse(mouse,
            m_virtOrigin.x + virtComp.cx, m_virtOrigin.y + virtComp.cy);
        virtShort = stateToShort(dpadDirToState(virtComp, vdir));
    }
    if (virtShort.empty()) return;

    ButtonAction act;
    act.physical = regionSel;
    if (virtShort == "triggerL" || virtShort == "triggerR") {
        act.type   = ButtonActionType::Trigger;
        act.target = (virtShort == "triggerL") ? "l2" : "r2";
    } else {
        act.type = ButtonActionType::VirtualButton;
        act.name = virtShort;
    }

    auto it = m_model.touchZoneActionEdits.find(regionSel);
    bool alreadyAssigned = (it != m_model.touchZoneActionEdits.end() &&
                             it->second.type == act.type &&
                             ((act.type == ButtonActionType::Trigger && it->second.target == act.target) ||
                              (act.type == ButtonActionType::VirtualButton && it->second.name == act.name)));
    if (alreadyAssigned) {
        m_model.touchZoneActionEdits.erase(regionSel);
        m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
    } else {
        m_model.touchZoneActionEdits[regionSel] = act;
        m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = virtShort;
    }
    m_sel.physComp = -1; m_sel.touchZoneRegionSelected.clear();
}

// ---------------------------------------------------------------------------
// Virtual pad click when a Movimiento (Gestos) gesture is selected (Gamepad/Xbox tab) — same
// virtShort resolution as onVirtHitTouchZone, but the "source" is the gesture id and the result
// goes into touchGestureActionEdits instead of touchZoneActionEdits. Reached for all 14 gestures,
// twist included. Also checks the virtual stick arrows first — see onVirtHitTouchZone's comment,
// same StickSlot-via-VirtualButton convention applies here.
//
// IMPORTANT: same H9 "Paso 2" caveat as onVirtHitTouchZone above — this is the mouse-click path
// only, the physical-press path lives separately inside render()'s H9 block
// (isTouchGestureSource). Update both when touching this.
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitTouchGesture(PadView& virt, ImVec2 mouse) {
    const std::string& gestureSel = m_sel.touchGestureSelected;

    std::string virtArrowDir;
    int virtArrowComp = virt.hitTestStickArrow(mouse, m_virtOrigin, virtArrowDir);
    if (virtArrowComp >= 0) {
        const auto& virtComps = virt.getLayout().components;
        auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtArrowComp].stateX);
        if (!vxId.empty()) {
            std::string slotKey;
            if      (virtArrowDir == "up")    slotKey = vyId + "_pos";
            else if (virtArrowDir == "down")  slotKey = vyId + "_neg";
            else if (virtArrowDir == "right") slotKey = vxId + "_pos";
            else if (virtArrowDir == "left")  slotKey = vxId + "_neg";
            if (!slotKey.empty()) {
                auto it = m_model.touchGestureActionEdits.find(gestureSel);
                bool already = (it != m_model.touchGestureActionEdits.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == slotKey);
                if (already) {
                    m_model.touchGestureActionEdits.erase(gestureSel);
                    m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f;
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = gestureSel; act.name = slotKey;
                    m_model.touchGestureActionEdits[gestureSel] = act;
                    m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
                }
            }
        }
        m_sel.physComp = -1; m_sel.touchGestureSelected.clear();
        return;
    }

    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComp = virt.getLayout().components[virtHit];

    std::string virtShort;
    if (virtComp.type == "button")
        virtShort = stateToShort(virtComp.state);
    else if (virtComp.type == "stick" && !virtComp.stateClick.empty())
        virtShort = stateToShort(virtComp.stateClick);
    else if (virtComp.type == "dpad") {
        std::string vdir = dpadDirFromMouse(mouse,
            m_virtOrigin.x + virtComp.cx, m_virtOrigin.y + virtComp.cy);
        virtShort = stateToShort(dpadDirToState(virtComp, vdir));
    }
    if (virtShort.empty()) return;

    ButtonAction act;
    act.physical = gestureSel;
    if (virtShort == "triggerL" || virtShort == "triggerR") {
        act.type   = ButtonActionType::Trigger;
        act.target = (virtShort == "triggerL") ? "l2" : "r2";
    } else {
        act.type = ButtonActionType::VirtualButton;
        act.name = virtShort;
    }

    auto it = m_model.touchGestureActionEdits.find(gestureSel);
    bool alreadyAssigned = (it != m_model.touchGestureActionEdits.end() &&
                             it->second.type == act.type &&
                             ((act.type == ButtonActionType::Trigger && it->second.target == act.target) ||
                              (act.type == ButtonActionType::VirtualButton && it->second.name == act.name)));
    if (alreadyAssigned) {
        m_model.touchGestureActionEdits.erase(gestureSel);
        m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
    } else {
        m_model.touchGestureActionEdits[gestureSel] = act;
        m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = virtShort;
    }
    m_sel.physComp = -1; m_sel.touchGestureSelected.clear();
}

// ---------------------------------------------------------------------------
// Virtual pad click when a stick axis is selected (H6 — stick-to-stick / stick-to-dpad)
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitPhysStick(PadView& phys, PadView& virt, ImVec2 mouse) {
    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComps  = virt.getLayout().components;
    const std::string& virtType = virtComps[virtHit].type;
    auto [xId, yId] = stickIdsFromStateX(phys.getLayout().components[m_sel.physComp].stateX);

    if (virtType == "stick" && !xId.empty()) {
        auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtHit].stateX);
        if (!vxId.empty()) {
            for (const auto& cfg : m_configs) {
                if (cfg.vid != m_model.vid || cfg.pid != m_model.pid) continue;
                for (const auto& [src, mapping] : cfg.axes) {
                    std::string sid = mapping.stickId.empty() ? mapping.target : mapping.stickId;
                    if (sid == xId || sid == yId) {
                        AxisMapping edit = mapping;
                        edit.stickId = sid; edit.btnNeg = edit.btnPos = "";
                        edit.target  = (sid == xId) ? vxId : vyId;
                        m_model.axisEdits[sid] = edit;
                    }
                }
                break;
            }
            m_sel.physComp = -1; m_sel.stickDir.clear();
        }
    } else if (virtType == "dpad" && !xId.empty()) {
        auto buildDpadEdit = [&](const std::string& id, const std::string& tgt) {
            AxisMapping edit;
            edit.stickId = id; edit.target = tgt;
            for (const auto& cfg : m_configs) {
                if (cfg.vid != m_model.vid || cfg.pid != m_model.pid) continue;
                for (const auto& [src, mapping] : cfg.axes) {
                    std::string sid = mapping.stickId.empty() ? mapping.target : mapping.stickId;
                    if (sid == id) { edit.invert = mapping.invert; break; }
                }
                break;
            }
            m_model.axisEdits[id] = edit;
        };
        buildDpadEdit(xId, "dpad_x");
        if (!yId.empty()) buildDpadEdit(yId, "dpad_y");
        m_sel.physComp = -1; m_sel.stickDir.clear();
    }
}

// ---------------------------------------------------------------------------
// Virtual pad click when a trigger is selected as source (H7 Xbox mode)
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitTriggerSrc(PadView& virt, ImVec2 mouse) {
    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComp = virt.getLayout().components[virtHit];
    ButtonAction act; act.physical = m_sel.triggerSrc;
    bool assigned = false;

    if (virtComp.type == "button") {
        const std::string& vState = virtComp.state;
        if (vState == "triggerL" || vState == "triggerR") {
            std::string trigTarget = (vState == "triggerL") ? "l2" : "r2";
            auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
            bool already = (it != m_model.trigActionEdits.end() &&
                            it->second.type == ButtonActionType::TriggerPassthrough &&
                            it->second.target == trigTarget);
            if (already) {
                m_model.trigActionEdits.erase(m_sel.triggerSrc);
                m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
            } else {
                act.type = ButtonActionType::TriggerPassthrough; act.target = trigTarget;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = vState;
            }
            assigned = true;
        } else {
            std::string vShort = stateToShort(vState);
            if (!vShort.empty()) {
                auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
                bool already = (it != m_model.trigActionEdits.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == vShort);
                if (already) {
                    m_model.trigActionEdits.erase(m_sel.triggerSrc);
                    m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                } else {
                    act.type = ButtonActionType::VirtualButton; act.name = vShort;
                    m_model.trigActionEdits[m_sel.triggerSrc] = act;
                    m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = vState;
                }
                assigned = true;
            }
        }
    } else if (virtComp.type == "stick" && !virtComp.stateClick.empty()) {
        std::string vShort = stateToShort(virtComp.stateClick);
        auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
        bool already = (it != m_model.trigActionEdits.end() &&
                        it->second.type == ButtonActionType::VirtualButton &&
                        it->second.name == vShort);
        if (already) {
            m_model.trigActionEdits.erase(m_sel.triggerSrc);
            m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
        } else {
            act.type = ButtonActionType::VirtualButton; act.name = vShort;
            m_model.trigActionEdits[m_sel.triggerSrc] = act;
            m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = virtComp.stateClick;
        }
        assigned = true;
    } else if (virtComp.type == "dpad") {
        std::string vdir = dpadDirFromMouse(mouse,
            m_virtOrigin.x + virtComp.cx, m_virtOrigin.y + virtComp.cy);
        if (!vdir.empty()) {
            std::string vShort = "dpad_" + vdir;
            auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
            bool already = (it != m_model.trigActionEdits.end() &&
                            it->second.type == ButtonActionType::VirtualButton &&
                            it->second.name == vShort);
            if (already) {
                m_model.trigActionEdits.erase(m_sel.triggerSrc);
                m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
            } else {
                act.type = ButtonActionType::VirtualButton; act.name = vShort;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = shortToState(vShort);
            }
            assigned = true;
        }
    }

    if (assigned) { m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; }
}

// ---------------------------------------------------------------------------
// Virtual stick arrow click: assign selected source to a stick slot.
// dir = "up"|"down"|"left"|"right" on the virtual stick (virtComp).
// Convention: up=Y+, down=Y-, right=X+, left=X-.
// ---------------------------------------------------------------------------
void MappingEditor::onVirtArrowHit(PadView& phys, PadView& virt, int virtComp, const std::string& dir) {
    const auto& virtComps = virt.getLayout().components;
    if (virtComp < 0 || virtComp >= (int)virtComps.size()) return;
    auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtComp].stateX);
    if (vxId.empty()) return;

    std::string slotKey;
    if      (dir == "up")    slotKey = vyId + "_pos";
    else if (dir == "down")  slotKey = vyId + "_neg";
    else if (dir == "right") slotKey = vxId + "_pos";
    else if (dir == "left")  slotKey = vxId + "_neg";
    if (slotKey.empty()) return;

    std::string source;
    if (m_sel.physComp >= 0) {
        const PadComponent& selComp = phys.getLayout().components[m_sel.physComp];
        if (selComp.type == "dpad" && !m_sel.dpadDir.empty())
            source = "dpad_" + m_sel.dpadDir;
        else if (selComp.type == "stick" && m_sel.stickAsButton)
            source = stateToShort(selComp.stateClick);
        else
            source = stateToShort(selComp.state);
    } else if (!m_sel.triggerSrc.empty()) {
        source = m_sel.triggerSrc;
    }
    if (source.empty()) return;

    // Toggle: click same slot again to remove the assignment.
    if (source == "l2" || source == "r2") {
        // Triggers use trigActionEdits; assigning to a slot clears any range edits.
        auto it = m_model.trigActionEdits.find(source);
        bool alreadySlot = (it != m_model.trigActionEdits.end() &&
                            it->second.type == ButtonActionType::VirtualButton &&
                            it->second.name == slotKey);
        if (alreadySlot) {
            m_model.trigActionEdits.erase(source);
            m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f;
        } else {
            auto& ranges = (source == "l2") ? m_model.trigLRangeEdits : m_model.trigRRangeEdits;
            ranges.clear();
            ButtonAction act;
            act.type = ButtonActionType::VirtualButton;
            act.name = slotKey;
            m_model.trigActionEdits[source] = act;
            m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
        }
    } else {
        // Buttons / dpad: stored as buttonEdits[physShort] = slotDir.
        auto it = m_model.buttonEdits.find(source);
        if (it != m_model.buttonEdits.end() && it->second == slotKey) {
            m_model.buttonEdits.erase(source);
            m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f;
        } else {
            m_model.actionEdits.erase(source);
            m_model.buttonEdits[source] = slotKey;
            m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
        }
    }

    m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
    m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
}

// ---------------------------------------------------------------------------
// Virtual pad click when a stick half-axis (stickDir) is selected (H6 T4)
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitAxisAction(PadView& phys, PadView& virt, ImVec2 mouse) {
    const auto& selComp = phys.getLayout().components[m_sel.physComp];
    auto [xId, yId] = stickIdsFromStateX(selComp.stateX);
    std::string axisKey;
    if      (m_sel.stickDir == "up")    axisKey = yId + "_pos";
    else if (m_sel.stickDir == "down")  axisKey = yId + "_neg";
    else if (m_sel.stickDir == "right") axisKey = xId + "_pos";
    else if (m_sel.stickDir == "left")  axisKey = xId + "_neg";
    if (axisKey.empty()) return;

    // Virtual stick arrow → StickSlot
    std::string virtArrowDir;
    int virtArrowComp = virt.hitTestStickArrow(mouse, m_virtOrigin, virtArrowDir);
    if (virtArrowComp >= 0) {
        const auto& virtComps = virt.getLayout().components;
        auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtArrowComp].stateX);
        if (!vxId.empty()) {
            std::string slotKey;
            if      (virtArrowDir == "up")    slotKey = vyId + "_pos";
            else if (virtArrowDir == "down")  slotKey = vyId + "_neg";
            else if (virtArrowDir == "right") slotKey = vxId + "_pos";
            else if (virtArrowDir == "left")  slotKey = vxId + "_neg";
            if (!slotKey.empty()) {
                auto it = m_model.axisActionEdits.find(axisKey);
                bool already = (it != m_model.axisActionEdits.end() &&
                                it->second.type == HalfAxisActionType::StickSlot &&
                                it->second.target == slotKey);
                if (already) {
                    m_model.axisActionEdits.erase(axisKey);
                    m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f;
                } else {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                    m_model.axisActionEdits[axisKey] = ha;
                    m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
                    m_sel.flashPhysArrowComp = m_sel.physComp;
                    m_sel.flashPhysArrowDir  = m_sel.stickDir;
                }
            }
        }
        m_sel.physComp = -1; m_sel.stickDir.clear();
        return;
    }

    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComp = virt.getLayout().components[virtHit];
    bool assigned = false;

    if (virtComp.type == "button") {
        const std::string& vState = virtComp.state;
        if (vState == "triggerL" || vState == "triggerR") {
            std::string trigTarget = (vState == "triggerL") ? "l2" : "r2";
            auto it = m_model.axisActionEdits.find(axisKey);
            bool already = (it != m_model.axisActionEdits.end() &&
                            it->second.type == HalfAxisActionType::Trigger &&
                            it->second.target == trigTarget);
            if (already) {
                m_model.axisActionEdits.erase(axisKey);
            } else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Trigger; ha.target = trigTarget;
                m_model.axisActionEdits[axisKey] = ha;
            }
            assigned = true;
        } else {
            std::string vShort = stateToShort(vState);
            if (!vShort.empty()) {
                auto it = m_model.axisActionEdits.find(axisKey);
                bool already = (it != m_model.axisActionEdits.end() &&
                                it->second.type == HalfAxisActionType::VirtualButton &&
                                it->second.target == vShort);
                if (already) m_model.axisActionEdits.erase(axisKey);
                else {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::VirtualButton; ha.target = vShort;
                    m_model.axisActionEdits[axisKey] = ha;
                }
                assigned = true;
            }
        }
    } else if (virtComp.type == "stick" && !virtComp.stateClick.empty()) {
        std::string vShort = stateToShort(virtComp.stateClick);
        if (!vShort.empty()) {
            auto it = m_model.axisActionEdits.find(axisKey);
            bool already = (it != m_model.axisActionEdits.end() &&
                            it->second.type == HalfAxisActionType::VirtualButton &&
                            it->second.target == vShort);
            if (already) m_model.axisActionEdits.erase(axisKey);
            else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::VirtualButton; ha.target = vShort;
                m_model.axisActionEdits[axisKey] = ha;
            }
            assigned = true;
        }
    } else if (virtComp.type == "dpad") {
        std::string vdir = dpadDirFromMouse(mouse,
            m_virtOrigin.x + virtComp.cx, m_virtOrigin.y + virtComp.cy);
        if (!vdir.empty()) {
            auto it = m_model.axisActionEdits.find(axisKey);
            bool already = (it != m_model.axisActionEdits.end() &&
                            it->second.type == HalfAxisActionType::Dpad &&
                            it->second.target == vdir);
            if (already) m_model.axisActionEdits.erase(axisKey);
            else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Dpad; ha.target = vdir;
                m_model.axisActionEdits[axisKey] = ha;
            }
            assigned = true;
        }
    }

    if (assigned) {
        m_sel.flashPhysArrowComp = m_sel.physComp;
        m_sel.flashPhysArrowDir  = m_sel.stickDir;
        m_sel.flashTimer = 1.0f;
        m_sel.physComp = -1; m_sel.stickDir.clear();
    }
}

// ---------------------------------------------------------------------------
// Virtual pad click when a gyro logical direction (m_sel.stickDir) is selected.
// Mirrors onVirtHitAxisAction, but the target map (gyro vs accel) is resolved per assignment via
// resolveImuTargetMap() instead of being a single fixed axisKey/axisActionEdits.
// ---------------------------------------------------------------------------
void MappingEditor::onVirtHitGyroAction(PadView& phys, PadView& virt, ImVec2 mouse) {
    const std::string dir = m_sel.stickDir;
    if (dir.empty()) return;

    // Virtual stick arrow → StickSlot (defaults to accel: proportional, held-tilt).
    std::string virtArrowDir;
    int virtArrowComp = virt.hitTestStickArrow(mouse, m_virtOrigin, virtArrowDir);
    if (virtArrowComp >= 0) {
        const auto& virtComps = virt.getLayout().components;
        auto [vxId, vyId] = stickIdsFromStateX(virtComps[virtArrowComp].stateX);
        if (!vxId.empty()) {
            std::string slotKey;
            if      (virtArrowDir == "up")    slotKey = vyId + "_pos";
            else if (virtArrowDir == "down")  slotKey = vyId + "_neg";
            else if (virtArrowDir == "right") slotKey = vxId + "_pos";
            else if (virtArrowDir == "left")  slotKey = vxId + "_neg";
            if (!slotKey.empty()) {
                std::string key;
                auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::StickSlot, key);
                auto it = map.find(key);
                bool already = (it != map.end() && it->second.type == HalfAxisActionType::StickSlot &&
                                it->second.target == slotKey);
                if (already) {
                    map.erase(key);
                    m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f;
                } else {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                    clearImuOtherMap(m_model, dir, &map == &m_model.accelActionEdits);
                    map[key] = ha;
                    m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
                    m_sel.flashPhysArrowComp = m_sel.physComp;
                    m_sel.flashPhysArrowDir  = dir;
                }
            }
        }
        m_sel.physComp = -1; m_sel.stickDir.clear();
        return;
    }

    int virtHit = virt.hitTest(mouse, m_virtOrigin);
    if (virtHit < 0) return;

    const auto& virtComp = virt.getLayout().components[virtHit];
    bool assigned = false;

    if (virtComp.type == "button") {
        const std::string& vState = virtComp.state;
        if (vState == "triggerL" || vState == "triggerR") {
            std::string trigTarget = (vState == "triggerL") ? "l2" : "r2";
            std::string key;
            auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::Trigger, key);
            auto it = map.find(key);
            bool already = (it != map.end() && it->second.type == HalfAxisActionType::Trigger &&
                            it->second.target == trigTarget);
            if (already) map.erase(key);
            else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Trigger; ha.target = trigTarget;
                clearImuOtherMap(m_model, dir, &map == &m_model.accelActionEdits);
                map[key] = ha;
            }
            assigned = true;
        } else {
            std::string vShort = stateToShort(vState);
            if (!vShort.empty()) {
                std::string key;
                auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::VirtualButton, key);
                auto it = map.find(key);
                bool already = (it != map.end() && it->second.type == HalfAxisActionType::VirtualButton &&
                                it->second.target == vShort);
                if (already) map.erase(key);
                else {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::VirtualButton; ha.target = vShort;
                    clearImuOtherMap(m_model, dir, &map == &m_model.accelActionEdits);
                    map[key] = ha;
                }
                assigned = true;
            }
        }
    } else if (virtComp.type == "stick" && !virtComp.stateClick.empty()) {
        std::string vShort = stateToShort(virtComp.stateClick);
        if (!vShort.empty()) {
            std::string key;
            auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::VirtualButton, key);
            auto it = map.find(key);
            bool already = (it != map.end() && it->second.type == HalfAxisActionType::VirtualButton &&
                            it->second.target == vShort);
            if (already) map.erase(key);
            else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::VirtualButton; ha.target = vShort;
                clearImuOtherMap(m_model, dir, &map == &m_model.accelActionEdits);
                map[key] = ha;
            }
            assigned = true;
        }
    } else if (virtComp.type == "dpad") {
        std::string vdir = dpadDirFromMouse(mouse,
            m_virtOrigin.x + virtComp.cx, m_virtOrigin.y + virtComp.cy);
        if (!vdir.empty()) {
            std::string key;
            auto& map = resolveImuTargetMap(m_sel, m_model, dir, HalfAxisActionType::Dpad, key);
            auto it = map.find(key);
            bool already = (it != map.end() && it->second.type == HalfAxisActionType::Dpad &&
                            it->second.target == vdir);
            if (already) map.erase(key);
            else {
                HalfAxisAction ha;
                ha.type = HalfAxisActionType::Dpad; ha.target = vdir;
                clearImuOtherMap(m_model, dir, &map == &m_model.accelActionEdits);
                map[key] = ha;
            }
            assigned = true;
        }
    }

    if (assigned) {
        m_sel.flashPhysArrowComp = m_sel.physComp;
        m_sel.flashPhysArrowDir  = dir;
        m_sel.flashTimer = 1.0f;
        m_sel.physComp = -1; m_sel.stickDir.clear();
    }
}
