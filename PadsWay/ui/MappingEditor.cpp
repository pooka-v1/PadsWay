#include "MappingEditor.h"
#include "../config/Strings.h"
#include "MappingHelpers.h"
#include "ActionPanel.h"
#include "../imgui/imgui.h"
#include "../nlohmann/json.hpp"
using json = nlohmann::json;
#include "../config/ConfigLoader.h"
#include "../Paths.h"

#include <fstream>
#include <algorithm>

// ---------------------------------------------------------------------------
void MappingEditor::init(ID3D11Device* device, PadEngine* engine,
                         const std::vector<PadLayout>& layouts,
                         const std::vector<std::string>& acceptedXbox,
                         float stickSelectThreshold, int stickHoldMs) {
    m_device               = device;
    m_engine               = engine;
    m_layouts              = layouts;
    m_acceptedXbox         = acceptedXbox;
    m_stickSelectThreshold = stickSelectThreshold;
    m_stickHoldMs          = stickHoldMs;
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
    m_sel.physComp = -1;
    reload();
}

void MappingEditor::reload() {
    if (m_mode == Mode::kProfile) {
        if (m_profIdx >= 0 && m_profIdx < (int)m_profilePaths.size()) {
            DeviceCandidate dev = m_engine->getActiveDevice();
            const ControllerConfig* base = findConfig(m_configs, dev.vid, dev.pid);
            if (base) {
                GameProfile profile = loadGameProfile(m_profilePaths[m_profIdx]);
                m_model.loadProfile(*base, profile);
                strncpy_s(m_profNameBuf, profile.profile_name.c_str(), sizeof(m_profNameBuf) - 1);
            }
        } else {
            // New profile: load base config as starting point
            DeviceCandidate dev = m_engine->getActiveDevice();
            const ControllerConfig* base = findConfig(m_configs, dev.vid, dev.pid);
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

void MappingEditor::save() {
    if (m_mode == Mode::kProfile) {
        if (m_profIdx >= 0 && m_profIdx < (int)m_profilePaths.size()) {
            DeviceCandidate dev = m_engine->getActiveDevice();
            const ControllerConfig* base = findConfig(m_configs, dev.vid, dev.pid);
            if (base) {
                try {
                    m_model.saveProfile(m_profilePaths[m_profIdx],
                                        m_profNameBuf[0] ? m_profNameBuf : m_profileNames[m_profIdx].c_str(),
                                        *base);
                    m_engine->requestProfileReload();
                    m_profToast     = true;
                    m_profToastTime = GetTickCount64();
                } catch (...) {}
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

        ImGui::Separator();
        ImGui::Spacing();

        // Profile selector
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
    }

    ImGui::Spacing();
    ImVec2 mouse        = ImGui::GetIO().MousePos;
    bool   mouseClicked = ImGui::IsMouseClicked(0);
    float  dt           = ImGui::GetIO().DeltaTime;

    // ── H9: lógica de mapping desde el mando ─────────────────────────────────
    GamepadState physNow = m_engine->getLastState();
    {
        const auto& physComps = phys.getLayout().components;

        if (m_sel.h9ErrorTimer > 0.0f)
            m_sel.h9ErrorTimer -= dt;

        if (m_sel.physComp < 0 && m_sel.triggerSrc.empty()) {
            // ── Paso 1a: stick al tope → seleccionar eje ──────────────────────
            int         activeStickComp = -1;
            std::string activeStickDir;
            for (int i = 0; i < (int)physComps.size(); ++i) {
                const PadComponent& c = physComps[i];
                if (c.type != "stick") continue;
                float x = 0.0f, y = 0.0f;
                readStickXY(physNow, c.stateX, x, y);
                std::string dir;
                if      (y >=  m_stickSelectThreshold) dir = "up";
                else if (y <= -m_stickSelectThreshold) dir = "down";
                else if (x <= -m_stickSelectThreshold) dir = "left";
                else if (x >=  m_stickSelectThreshold) dir = "right";
                if (!dir.empty()) { activeStickComp = i; activeStickDir = dir; break; }
            }

            if (activeStickComp >= 0) {
                if (m_sel.h9HoldComp != activeStickComp || m_sel.h9HoldStickDir != activeStickDir) {
                    m_sel.h9HoldComp      = activeStickComp;
                    m_sel.h9HoldStickDir  = activeStickDir;
                    m_sel.h9HoldTimer     = 0.0f;
                } else {
                    m_sel.h9HoldTimer += dt;
                    if (m_sel.h9HoldTimer >= m_stickHoldMs / 1000.0f) {
                        m_sel.physComp      = activeStickComp;
                        m_sel.stickDir      = activeStickDir;
                        m_sel.stickAsButton = false;
                        m_sel.h9HoldComp    = -1;
                        m_sel.h9HoldStickDir.clear();
                        m_sel.h9HoldTimer   = 0.0f;
                    }
                }
            } else {
                // ── Paso 1b: botón mantenido 1s → seleccionarlo ──
                if (!m_sel.h9HoldStickDir.empty()) {
                    m_sel.h9HoldComp = -1;
                    m_sel.h9HoldStickDir.clear();
                    m_sel.h9HoldDpadDir.clear();
                    m_sel.h9HoldTimer = 0.0f;
                } else {
                    int  activeComp       = -1;
                    bool activeIsStickBtn = false;
                    std::string activeDpadDir;
                    for (int i = 0; i < (int)physComps.size(); ++i) {
                        const PadComponent& c = physComps[i];
                        if (c.type == "button" && isStateActive(physNow, c.state)) {
                            activeComp = i; activeIsStickBtn = false; break;
                        }
                        if (c.type == "stick" && !c.stateClick.empty() &&
                            isStateActive(physNow, c.stateClick)) {
                            activeComp = i; activeIsStickBtn = true; break;
                        }
                        if (c.type == "dpad") {
                            for (const char* d : {"up","down","left","right"}) {
                                std::string st = dpadDirToState(c, d);
                                if (!st.empty() && isStateActive(physNow, st)) {
                                    activeComp = i; activeDpadDir = d; break;
                                }
                            }
                            if (activeComp >= 0) break;
                        }
                    }
                    if (activeComp >= 0) {
                        if (m_sel.h9HoldComp != activeComp) {
                            m_sel.h9HoldComp    = activeComp;
                            m_sel.h9HoldDpadDir = activeDpadDir;
                            m_sel.h9HoldTimer   = 0.0f;
                        } else {
                            m_sel.h9HoldDpadDir = activeDpadDir;
                            m_sel.h9HoldTimer += dt;
                            if (m_sel.h9HoldTimer >= 1.0f) {
                                m_sel.physComp      = activeComp;
                                m_sel.stickAsButton = activeIsStickBtn;
                                m_sel.dpadDir       = activeDpadDir;
                                m_sel.actionType    = ActionType::Xbox;
                                m_sel.h9HoldComp    = -1;
                                m_sel.h9HoldDpadDir.clear();
                                m_sel.h9HoldTimer   = 0.0f;
                            }
                        }
                    } else {
                        m_sel.h9HoldComp    = -1;
                        m_sel.h9HoldDpadDir.clear();
                        m_sel.h9HoldTimer   = 0.0f;
                        // ── Paso 1c: gatillo al tope 2s → seleccionar como fuente ──
                        constexpr float kTrigSelThresh = 0.75f;
                        if (physNow.triggerL > kTrigSelThresh || physNow.triggerR > kTrigSelThresh) {
                            std::string tSrc = (physNow.triggerL >= physNow.triggerR) ? "l2" : "r2";
                            if (m_sel.h9HoldTriggerSrc != tSrc) {
                                m_sel.h9HoldTriggerSrc   = tSrc;
                                m_sel.h9HoldTriggerTimer = 0.0f;
                            } else {
                                m_sel.h9HoldTriggerTimer += dt;
                                if (m_sel.h9HoldTriggerTimer >= 2.0f) {
                                    m_sel.triggerSrc         = tSrc;
                                    m_sel.actionType         = ActionType::Xbox;
                                    m_sel.captureKeys.clear();
                                    m_sel.macroSel.clear();
                                    m_sel.botSel.clear();
                                    m_sel.h9HoldTriggerSrc.clear();
                                    m_sel.h9HoldTriggerTimer = 0.0f;
                                }
                            }
                        } else {
                            m_sel.h9HoldTriggerSrc.clear();
                            m_sel.h9HoldTriggerTimer = 0.0f;
                        }
                    }
                }
            }
        } else if (m_sel.physComp >= 0 && m_sel.actionType == ActionType::Xbox) {
            // Xbox mode: detect rising edge on physical input → assign virtual button
            const PadComponent& selPhysComp = physComps[m_sel.physComp];
            std::string selState;
            if (m_sel.stickAsButton)
                selState = selPhysComp.stateClick;
            else if (selPhysComp.type == "dpad" && !m_sel.dpadDir.empty())
                selState = dpadDirToState(selPhysComp, m_sel.dpadDir);
            else
                selState = selPhysComp.state;
            std::string physShort = stateToShort(selState);

            std::vector<std::string> candidateStates;
            for (int i = 0; i < (int)physComps.size(); ++i) {
                const PadComponent& c = physComps[i];
                if (c.type == "button" && !c.state.empty())
                    candidateStates.push_back(c.state);
                else if (c.type == "stick" && !c.stateClick.empty())
                    candidateStates.push_back(c.stateClick);
                else if (c.type == "dpad") {
                    for (const char* d : {"up","down","left","right"}) {
                        std::string st = dpadDirToState(c, d);
                        if (!st.empty()) candidateStates.push_back(st);
                    }
                }
            }
            for (const auto& compState : candidateStates) {
                bool wasActive = isStateActive(m_sel.h9PrevPhysState, compState);
                bool isActive  = isStateActive(physNow, compState);
                if (!isActive || wasActive) continue;

                std::string virtShort = stateToShort(compState);
                bool valid = false;
                for (const auto& s : m_acceptedXbox) if (virtShort == s) { valid = true; break; }

                // ── Axis-action mode: stick direction seleccionada → asignar VirtualButton/Dpad ──
                if (!m_sel.stickDir.empty()) {
                    auto [xId, yId] = stickIdsFromStateX(selPhysComp.stateX);
                    std::string axisKey;
                    if      (m_sel.stickDir == "up")    axisKey = yId + "_pos";
                    else if (m_sel.stickDir == "down")  axisKey = yId + "_neg";
                    else if (m_sel.stickDir == "right") axisKey = xId + "_pos";
                    else if (m_sel.stickDir == "left")  axisKey = xId + "_neg";
                    bool isDpad9 = (virtShort.rfind("dpad_", 0) == 0);
                    if ((valid || isDpad9) && !axisKey.empty()) {
                        HalfAxisAction axisAction;
                        if (isDpad9) {
                            axisAction.type = HalfAxisActionType::Dpad;
                            axisAction.target = virtShort.substr(5); // "up"/"down"/"left"/"right"
                        } else {
                            axisAction.type = HalfAxisActionType::VirtualButton;
                            axisAction.target = virtShort;
                        }
                        auto axisEditIt = m_model.axisActionEdits.find(axisKey);
                        bool alreadyAssigned = (axisEditIt != m_model.axisActionEdits.end() &&
                                         axisEditIt->second.type == axisAction.type &&
                                         axisEditIt->second.target == axisAction.target);
                        if (alreadyAssigned) {
                            m_model.axisActionEdits.erase(axisKey);
                            m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                        } else {
                            m_model.axisActionEdits[axisKey] = axisAction;
                            if (!isDpad9) {
                                int fc = findCompByState(virt.getLayout(), shortToState(virtShort));
                                m_sel.flashComp = fc; m_sel.flashTimer = 0.5f;
                                m_sel.flashVirtShort = shortToState(virtShort);
                            } else {
                                m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                            }
                        }
                        m_sel.flashPhysArrowComp = m_sel.physComp;
                        m_sel.flashPhysArrowDir  = m_sel.stickDir;
                        if (m_sel.flashTimer < 1.0f) m_sel.flashTimer = 1.0f;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.stickAsButton = false; m_sel.actionType = ActionType::Xbox;
                    } else {
                        m_sel.h9ErrorTimer = 2.0f;
                    }
                    break;
                }

                if (valid) {
                    if (!physShort.empty()) {
                        m_model.actionEdits.erase(physShort);
                        auto it = m_model.buttonEdits.find(physShort);
                        bool alreadyAssigned = (it != m_model.buttonEdits.end() && it->second == virtShort);
                        m_model.buttonEdits[physShort] = alreadyAssigned ? "" : virtShort;
                        int flashComp = findCompByState(virt.getLayout(), shortToState(virtShort));
                        m_sel.flashComp      = alreadyAssigned ? -1 : flashComp;
                        m_sel.flashTimer     = alreadyAssigned ? 0.0f : 0.5f;
                        m_sel.flashVirtShort = alreadyAssigned ? "" : virtShort;
                    }
                    m_sel.physComp    = -1;
                    m_sel.stickAsButton = false;
                    m_sel.dpadDir.clear();
                    m_sel.actionType = ActionType::Xbox;
                } else {
                    bool hasAssignment = m_model.actionEdits.count(physShort) > 0 ||
                        (m_model.buttonEdits.count(physShort) && !m_model.buttonEdits.at(physShort).empty());
                    if (hasAssignment && !physShort.empty()) {
                        m_model.buttonEdits[physShort] = "";
                        m_model.actionEdits.erase(physShort);
                        m_sel.physComp    = -1;
                        m_sel.stickAsButton = false;
                        m_sel.dpadDir.clear();
                        m_sel.actionType = ActionType::Xbox;
                    } else {
                        m_sel.h9ErrorTimer = 2.0f;
                    }
                }
                break;
            }

            // Physical L2/R2 → asignar componente seleccionado como gatillo virtual
            {
                constexpr float kTrigThresh = 0.5f;
                auto doAxisTrigAssign = [&](const std::string& trigTarget, const std::string& trigState) {
                    auto [xId, yId] = stickIdsFromStateX(selPhysComp.stateX);
                    std::string axisKey;
                    if      (m_sel.stickDir == "up")    axisKey = yId + "_pos";
                    else if (m_sel.stickDir == "down")  axisKey = yId + "_neg";
                    else if (m_sel.stickDir == "right") axisKey = xId + "_pos";
                    else if (m_sel.stickDir == "left")  axisKey = xId + "_neg";
                    if (axisKey.empty()) return;
                    HalfAxisAction axisAction;
                    axisAction.type = HalfAxisActionType::Trigger;
                    axisAction.target = trigTarget;
                    auto axisEditIt = m_model.axisActionEdits.find(axisKey);
                    bool alreadyAssigned = (axisEditIt != m_model.axisActionEdits.end() &&
                                     axisEditIt->second.type == axisAction.type &&
                                     axisEditIt->second.target == axisAction.target);
                    if (alreadyAssigned) {
                        m_model.axisActionEdits.erase(axisKey);
                        m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                    } else {
                        m_model.axisActionEdits[axisKey] = axisAction;
                        m_sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                        m_sel.flashTimer     = 1.0f;
                        m_sel.flashVirtShort = trigState;
                        m_sel.flashPhysArrowComp = m_sel.physComp;
                        m_sel.flashPhysArrowDir  = m_sel.stickDir;
                    }
                    m_sel.physComp = -1; m_sel.stickDir.clear();
                    m_sel.stickAsButton = false; m_sel.actionType = ActionType::Xbox;
                };
                auto doTrigAssign = [&](const std::string& trigTarget, const std::string& trigState) {
                    if (physShort.empty()) return;
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
                        m_sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                        m_sel.flashTimer     = 0.5f;
                        m_sel.flashVirtShort = trigState;
                    }
                    m_sel.physComp = -1; m_sel.stickAsButton = false;
                    m_sel.dpadDir.clear(); m_sel.actionType = ActionType::Xbox;
                };
                if (physNow.triggerL > kTrigThresh && m_sel.h9PrevPhysState.triggerL <= kTrigThresh) {
                    if (!m_sel.stickDir.empty()) doAxisTrigAssign("l2", "triggerL");
                    else doTrigAssign("l2", "triggerL");
                } else if (physNow.triggerR > kTrigThresh && m_sel.h9PrevPhysState.triggerR <= kTrigThresh) {
                    if (!m_sel.stickDir.empty()) doAxisTrigAssign("r2", "triggerR");
                    else doTrigAssign("r2", "triggerR");
                }
            }

            // Analog stick tilt → assign source to a stick slot destination.
            // physShort is empty for stick sources (state field unused), so also check stickDir.
            if (m_sel.physComp >= 0 && (!physShort.empty() || !m_sel.stickDir.empty())) {
                const auto& virtComps = virt.getLayout().components;
                for (int i = 0; i < (int)virtComps.size(); ++i) {
                    if (virtComps[i].type != "stick") continue;
                    // Rising-edge: require stick to have been below threshold last frame.
                    // Prevents immediate fire when physComp is set while a stick is held.
                    float prevSx = 0.0f, prevSy = 0.0f;
                    readStickXY(m_sel.h9PrevPhysState, virtComps[i].stateX, prevSx, prevSy);
                    if (prevSx >=  m_stickSelectThreshold || prevSx <= -m_stickSelectThreshold ||
                        prevSy >=  m_stickSelectThreshold || prevSy <= -m_stickSelectThreshold) continue;
                    float sx = 0.0f, sy = 0.0f;
                    readStickXY(physNow, virtComps[i].stateX, sx, sy);
                    std::string slotDir;
                    if      (sy >=  m_stickSelectThreshold) slotDir = "up";
                    else if (sy <= -m_stickSelectThreshold) slotDir = "down";
                    else if (sx >=  m_stickSelectThreshold) slotDir = "right";
                    else if (sx <= -m_stickSelectThreshold) slotDir = "left";
                    if (slotDir.empty()) continue;

                    auto [vxId, vyId] = stickIdsFromStateX(virtComps[i].stateX);
                    std::string slotKey;
                    if      (slotDir == "up")    slotKey = vyId + "_pos";
                    else if (slotDir == "down")  slotKey = vyId + "_neg";
                    else if (slotDir == "right") slotKey = vxId + "_pos";
                    else if (slotDir == "left")  slotKey = vxId + "_neg";

                    if (!m_sel.stickDir.empty()) {
                        // Axis-action source: assign StickSlot target.
                        auto [xId, yId] = stickIdsFromStateX(selPhysComp.stateX);
                        std::string axisKey;
                        if      (m_sel.stickDir == "up")    axisKey = yId + "_pos";
                        else if (m_sel.stickDir == "down")  axisKey = yId + "_neg";
                        else if (m_sel.stickDir == "right") axisKey = xId + "_pos";
                        else if (m_sel.stickDir == "left")  axisKey = xId + "_neg";
                        if (!axisKey.empty()) {
                            HalfAxisAction ha;
                            ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                            auto axisEditIt = m_model.axisActionEdits.find(axisKey);
                            bool alreadyAssigned = (axisEditIt != m_model.axisActionEdits.end() &&
                                             axisEditIt->second.type == ha.type &&
                                             axisEditIt->second.target == ha.target);
                            if (alreadyAssigned) {
                                m_model.axisActionEdits.erase(axisKey);
                                m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f; m_sel.flashComp = -1;
                            } else {
                                m_model.axisActionEdits[axisKey] = ha;
                                m_sel.flashSlotKey = slotKey; m_sel.flashTimer = 1.0f; m_sel.flashComp = -1;
                                m_sel.flashPhysArrowComp = m_sel.physComp;
                                m_sel.flashPhysArrowDir  = m_sel.stickDir;
                            }
                            m_sel.physComp = -1; m_sel.stickDir.clear();
                            m_sel.stickAsButton = false; m_sel.actionType = ActionType::Xbox;
                        }
                    } else {
                        auto it = m_model.buttonEdits.find(physShort);
                        if (it != m_model.buttonEdits.end() && it->second == slotKey) {
                            m_model.buttonEdits.erase(physShort);
                        } else {
                            m_model.actionEdits.erase(physShort);
                            m_model.buttonEdits[physShort] = slotKey;
                        }
                        m_sel.physComp = -1; m_sel.stickAsButton = false;
                        m_sel.dpadDir.clear(); m_sel.actionType = ActionType::Xbox;
                    }
                    break;
                }
            }
        } else if (!m_sel.triggerSrc.empty() && m_sel.actionType == ActionType::Xbox) {
            // Paso 2 — gatillo como fuente: asignar target por botón/gatillo físico
            std::vector<std::string> candStates;
            for (int i = 0; i < (int)physComps.size(); ++i) {
                const PadComponent& c = physComps[i];
                if (c.type == "button" && !c.state.empty())
                    candStates.push_back(c.state);
                else if (c.type == "stick" && !c.stateClick.empty())
                    candStates.push_back(c.stateClick);
                else if (c.type == "dpad") {
                    for (const char* d : {"up","down","left","right"}) {
                        std::string st = dpadDirToState(c, d);
                        if (!st.empty()) candStates.push_back(st);
                    }
                }
            }
            for (const auto& cState : candStates) {
                if (!isStateActive(physNow, cState) || isStateActive(m_sel.h9PrevPhysState, cState)) continue;
                std::string vShort = stateToShort(cState);
                bool valid = false;
                for (const auto& s : m_acceptedXbox) if (vShort == s) { valid = true; break; }
                if (!valid) { m_sel.h9ErrorTimer = 2.0f; break; }
                auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
                bool already = (it != m_model.trigActionEdits.end() &&
                                it->second.type == ButtonActionType::VirtualButton &&
                                it->second.name == vShort);
                if (already) {
                    m_model.trigActionEdits.erase(m_sel.triggerSrc);
                    m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                } else {
                    ButtonAction act;
                    act.type = ButtonActionType::VirtualButton; act.physical = m_sel.triggerSrc; act.name = vShort;
                    m_model.trigActionEdits[m_sel.triggerSrc] = act;
                    m_sel.flashComp = findCompByState(virt.getLayout(), shortToState(vShort));
                    m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = shortToState(vShort);
                }
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
                break;
            }
            // Virtual stick tilt → assign trigger source to a stick slot.
            if (!m_sel.triggerSrc.empty()) {
                const auto& virtComps = virt.getLayout().components;
                for (int i = 0; i < (int)virtComps.size(); ++i) {
                    if (virtComps[i].type != "stick") continue;
                    float sx = 0.0f, sy = 0.0f;
                    readStickXY(physNow, virtComps[i].stateX, sx, sy);
                    std::string slotDir;
                    if      (sy >=  m_stickSelectThreshold) slotDir = "up";
                    else if (sy <= -m_stickSelectThreshold) slotDir = "down";
                    else if (sx >=  m_stickSelectThreshold) slotDir = "right";
                    else if (sx <= -m_stickSelectThreshold) slotDir = "left";
                    if (slotDir.empty()) continue;

                    auto [vxId, vyId] = stickIdsFromStateX(virtComps[i].stateX);
                    std::string slotKey;
                    if      (slotDir == "up")    slotKey = vyId + "_pos";
                    else if (slotDir == "down")  slotKey = vyId + "_neg";
                    else if (slotDir == "right") slotKey = vxId + "_pos";
                    else if (slotDir == "left")  slotKey = vxId + "_neg";

                    auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
                    bool already = (it != m_model.trigActionEdits.end() &&
                                    it->second.type == ButtonActionType::VirtualButton &&
                                    it->second.name == slotKey);
                    if (already) {
                        m_model.trigActionEdits.erase(m_sel.triggerSrc);
                    } else {
                        auto& ranges = (m_sel.triggerSrc == "l2") ? m_model.trigLRangeEdits
                                                                   : m_model.trigRRangeEdits;
                        ranges.clear();
                        ButtonAction act;
                        act.type = ButtonActionType::VirtualButton;
                        act.name = slotKey;
                        m_model.trigActionEdits[m_sel.triggerSrc] = act;
                    }
                    m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
                    break;
                }
            }

            // Trigger press: rising edge → TriggerPassthrough target
            {
                constexpr float kTrigThresh2 = 0.5f;
                auto doTrigTgtAssign = [&](const std::string& trigTarget, const std::string& trigState) {
                    auto it = m_model.trigActionEdits.find(m_sel.triggerSrc);
                    bool already = (it != m_model.trigActionEdits.end() &&
                                    it->second.type == ButtonActionType::TriggerPassthrough &&
                                    it->second.target == trigTarget);
                    if (already) {
                        m_model.trigActionEdits.erase(m_sel.triggerSrc);
                        m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                    } else {
                        ButtonAction act;
                        act.type = ButtonActionType::TriggerPassthrough; act.physical = m_sel.triggerSrc;
                        act.target = trigTarget;
                        m_model.trigActionEdits[m_sel.triggerSrc] = act;
                        m_sel.flashComp = findCompByState(virt.getLayout(), trigState);
                        m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = trigState;
                    }
                    m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
                };
                if (!m_sel.triggerSrc.empty()) {
                    if (physNow.triggerL > kTrigThresh2 && m_sel.h9PrevPhysState.triggerL <= kTrigThresh2)
                        doTrigTgtAssign("l2", "triggerL");
                    else if (physNow.triggerR > kTrigThresh2 && m_sel.h9PrevPhysState.triggerR <= kTrigThresh2)
                        doTrigTgtAssign("r2", "triggerR");
                }
            }
        }

        m_sel.h9PrevPhysState = physNow;
    }

    // ── Construir estados de display ──────────────────────────────────────────
    m_sel.flashTimer -= dt;
    if (m_sel.flashTimer <= 0.0f) {
        m_sel.flashComp = -1; m_sel.flashVirtShort.clear(); m_sel.flashSlotKey.clear();
        m_sel.flashPhysArrowComp = -1; m_sel.flashPhysArrowDir.clear();
    }

    GamepadState physDisplay{};
    GamepadState virtDisplay{};
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

    // Texto instruccional
    ImGui::Spacing();
    {
        const char* msg;
        ImVec4      col = { 1.0f, 0.86f, 0.0f, 1.0f };
        if (m_sel.h9ErrorTimer > 0.0f) {
            msg = tr("mapper.hint_no_xbox");
            col = { 1.0f, 0.3f, 0.3f, 1.0f };
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && !m_sel.h9HoldTriggerSrc.empty()) {
            msg = tr("mapper.hint_hold_trigger");
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && m_sel.h9HoldComp >= 0) {
            msg = m_sel.h9HoldStickDir.empty()
                ? tr("mapper.hint_hold_button")
                : tr("mapper.hint_hold_stick");
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty()) {
            msg = tr("mapper.hint_pick_source");
        } else if (!m_sel.triggerSrc.empty()) {
            if (m_sel.actionType == ActionType::Keyboard)
                msg = m_sel.captureKeys.empty()
                    ? tr("mapper.hint_press_combo")
                    : tr("mapper.hint_press_more");
            else if (m_sel.actionType == ActionType::Xbox)
                msg = tr("mapper.hint_click_virt");
            else
                msg = tr("mapper.hint_trig_action");
        } else if (m_sel.physComp >= 0 &&
                   phys.getLayout().components[m_sel.physComp].type == "stick" &&
                   (!m_sel.stickAsButton || m_sel.actionType == ActionType::Xbox)) {
            if (m_sel.stickAsButton)
                msg = tr("mapper.hint_stick_btn");
            else if (!m_sel.stickDir.empty()) {
                if (m_sel.actionType == ActionType::Xbox)
                    msg = tr("mapper.hint_click_any");
                else if (m_sel.actionType == ActionType::Keyboard)
                    msg = m_sel.captureKeys.empty()
                        ? tr("mapper.hint_press_combo")
                        : tr("mapper.hint_press_more");
                else
                    msg = tr("mapper.hint_half_axis");
            } else
                msg = tr("mapper.hint_click_stick");
        } else if (m_sel.actionType == ActionType::Keyboard) {
            msg = m_sel.captureKeys.empty()
                ? tr("mapper.hint_press_combo")
                : tr("mapper.hint_press_more");
        } else {
            msg = tr("mapper.hint_pick_target");
        }

        float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;
        ImGui::SetWindowFontScale(1.35f);
        float textW   = ImGui::CalcTextSize(msg).x;
        float offsetX = (availW - textW) * 0.5f;
        if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::TextColored(col, "%s", msg);
        ImGui::SetWindowFontScale(1.0f);

        if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && m_sel.h9HoldComp >= 0 && m_sel.h9HoldTimer > 0.0f) {
            constexpr float kBarW = 160.0f;
            float barOffX = (availW - kBarW) * 0.5f;
            if (barOffX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barOffX);
            float holdSec = m_sel.h9HoldStickDir.empty() ? 1.0f : (m_stickHoldMs / 1000.0f);
            ImGui::ProgressBar(m_sel.h9HoldTimer / holdSec, { kBarW, 6.0f }, "");
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

    if (selType != "stick" || m_sel.stickAsButton) {
        // ── H5: botón seleccionado ─────────────────────────────────────────
        const auto& selPhysComp = physComps[m_sel.physComp];
        const std::string physShortSel = (selType == "stick" && m_sel.stickAsButton)
            ? stateToShort(selPhysComp.stateClick)
            : (selType == "dpad")
                ? stateToShort(dpadDirToState(selPhysComp, m_sel.dpadDir))
                : stateToShort(selPhysComp.state);

        float btnW   = 90.0f;
        float totalW = btnW * 5 + ImGui::GetStyle().ItemSpacing.x * 4;
        float offX   = (availW - totalW) * 0.5f;
        if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);

        auto typeBtn = [&](const char* label, ActionType type) {
            bool sel = (m_sel.actionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_sel.actionType = type;
                m_sel.captureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        char lblMacro[64], lblKeyboard[64], lblMouse[64], lblBot[64];
        snprintf(lblMacro,    sizeof(lblMacro),    "%s##btnMacro",  tr("action.type_macro"));
        snprintf(lblKeyboard, sizeof(lblKeyboard), "%s##btnKb",     tr("action.type_keyboard"));
        snprintf(lblMouse,    sizeof(lblMouse),    "%s##btnMouse",  tr("action.type_mouse"));
        snprintf(lblBot,      sizeof(lblBot),      "%s##btnBot",    tr("action.type_bot"));
        typeBtn("Xbox##btnXbox", ActionType::Xbox);     ImGui::SameLine();
        typeBtn(lblMacro,        ActionType::Macro);    ImGui::SameLine();
        typeBtn(lblKeyboard,     ActionType::Keyboard); ImGui::SameLine();
        typeBtn(lblMouse,        ActionType::Mouse);    ImGui::SameLine();
        typeBtn(lblBot,          ActionType::Bot);

        ImGui::Spacing();

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
            if (ActionPanel::renderMacroCombo("macButton", m_sel.macroSel, m_macroNames, availW,
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
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) {
                m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            } else if (ActionPanel::renderKeyboardCapture("kbButton", m_sel.captureKeys, availW)) {
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
            if (ActionPanel::renderMouseButtons("mbButton", mbResult, availW)) {
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
            if (ActionPanel::renderBotCombo("botButton", m_sel.botSel, availableBots, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Bot; act.physical = physShortSel; act.name = m_sel.botSel;
                    m_model.actionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
            }
        }
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
                ImGui::Spacing();

                // Direction label
                {
                    float hdrW = ImGui::CalcTextSize(axisKey.c_str()).x;
                    float offX = (availW - hdrW) * 0.5f;
                    if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                    ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", axisKey.c_str());
                }

                // Tab buttons
                constexpr float kBtnW = 80.0f;
                float totalW = kBtnW * 7 + ImGui::GetStyle().ItemSpacing.x * 6;
                float offX = (availW - totalW) * 0.5f;
                if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                auto renderTypeTab = [&](const char* label, ActionType type) {
                    bool s = (m_sel.actionType == type);
                    if (s) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    if (ImGui::Button(label, { kBtnW, 0.0f })) { m_sel.actionType = type; m_sel.captureKeys.clear(); }
                    if (s) ImGui::PopStyleColor();
                };
                char lblGamepad[64], lblMacro[64], lblKeyboard[64], lblMouse[64], lblMouseMove[64], lblBot[64];
                snprintf(lblGamepad,   sizeof(lblGamepad),   "%s##axGamepad", tr("action.type_gamepad"));
                snprintf(lblMacro,     sizeof(lblMacro),     "%s##axMacro",   tr("action.type_macro"));
                snprintf(lblKeyboard,  sizeof(lblKeyboard),  "%s##axKb",      tr("action.type_keyboard"));
                snprintf(lblMouse,     sizeof(lblMouse),     "%s##axMouse",   tr("action.type_mouse"));
                snprintf(lblMouseMove, sizeof(lblMouseMove), "%s##axMMove",   tr("action.type_mousemove"));
                snprintf(lblBot,       sizeof(lblBot),       "%s##axBot",     tr("action.type_bot"));
                renderTypeTab(lblGamepad,   ActionType::Xbox);      ImGui::SameLine();
                renderTypeTab(lblMacro,     ActionType::Macro);     ImGui::SameLine();
                renderTypeTab(lblKeyboard,  ActionType::Keyboard);  ImGui::SameLine();
                renderTypeTab(lblMouse,     ActionType::Mouse);     ImGui::SameLine();
                renderTypeTab(lblMouseMove, ActionType::MouseMove); ImGui::SameLine();
                renderTypeTab(lblBot,       ActionType::Bot);       ImGui::SameLine();
                {
                    auto axisEdit = m_model.axisActionEdits.find(axisKey);
                    bool hasRanges = (axisEdit != m_model.axisActionEdits.end() &&
                                      axisEdit->second.type == HalfAxisActionType::Ranges &&
                                      !axisEdit->second.ranges.empty());
                    if (hasRanges) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    if (ImGui::Button(trid("btn.ranges", "axisRanges").c_str(), { kBtnW, 0.0f })) {
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
                    }
                    if (hasRanges) ImGui::PopStyleColor();
                }

                ImGui::Spacing();

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
                    if (ActionPanel::renderMacroCombo("macAxis", m_sel.macroSel, m_macroNames, availW,
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
                    bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
                    if (cancel) { m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear(); }
                    else if (ActionPanel::renderKeyboardCapture("kbAxis", m_sel.captureKeys, availW)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Keyboard;
                        for (const auto& p : m_sel.captureKeys) ha.keys.push_back(p.first);
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                    }
                } else if (m_sel.actionType == ActionType::Mouse) {
                    std::string mbResult;
                    if (ActionPanel::renderMouseButtons("mbAxis", mbResult, availW)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::MouseClick; ha.mouseButton = mbResult;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox;
                    }
                } else if (m_sel.actionType == ActionType::MouseMove) {
                    float panelW = 280.0f;
                    float offX2 = (availW - panelW) * 0.5f;
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);
                    ImGui::TextDisabled("%s", tr("mapper.axis_hint"));
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);
                    ImGui::SetNextItemWidth(100.0f);
                    ImGui::SliderFloat(trid("mapper.mouse_speed", "mouseSpeed").c_str(), &m_sel.axisMouseSpeed, 1.0f, 50.0f, "%.0f");
                    ImGui::SameLine();
                    const char* mouseAxes[] = { "X", "Y" };
                    int axIdx = (m_sel.axisMouseAxis == "mouse_y") ? 1 : 0;
                    ImGui::SetNextItemWidth(60.0f);
                    if (ImGui::Combo(trid("mapper.mouse_axis", "mouseAxis").c_str(), &axIdx, mouseAxes, 2))
                        m_sel.axisMouseAxis = (axIdx == 1) ? "mouse_y" : "mouse_x";
                    ImGui::SameLine();
                    if (ImGui::Button(trid("btn.assign", "mouseAssign").c_str())) {
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
                    if (ActionPanel::renderBotCombo("botAxis", m_sel.botSel, availableBots, availW)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Bot; ha.target = m_sel.botSel;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
                    }
                }
                // Mando mode: user clicks virtual pad → onVirtHitAxisAction

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
    } // action panels

    // ── Trigger action panel ─────────────────────────────────────────────────
    if (!m_sel.triggerSrc.empty()) {
        ImGui::Spacing();
        float availW = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;

        {
            const char* lbl = (m_sel.triggerSrc == "l2") ? "L2 \xe2\x86\x92" : "R2 \xe2\x86\x92";
            float hdrW = ImGui::CalcTextSize(lbl).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - hdrW) * 0.5f);
            ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", lbl);
        }

        float btnW   = 90.0f;
        float totalW = btnW * 6 + ImGui::GetStyle().ItemSpacing.x * 5;
        float offX   = (availW - totalW) * 0.5f;
        if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
        auto renderTypeTab = [&](const char* label, ActionType type) {
            bool sel = (m_sel.actionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_sel.actionType = type; m_sel.captureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        char lblMacro[64], lblKeyboard[64], lblMouse[64], lblBot[64];
        snprintf(lblMacro,    sizeof(lblMacro),    "%s##trigMacro",  tr("action.type_macro"));
        snprintf(lblKeyboard, sizeof(lblKeyboard), "%s##trigKb",     tr("action.type_keyboard"));
        snprintf(lblMouse,    sizeof(lblMouse),    "%s##trigMouse",  tr("action.type_mouse"));
        snprintf(lblBot,      sizeof(lblBot),      "%s##trigBot",    tr("action.type_bot"));
        renderTypeTab("Xbox/Anal.##trigXbox", ActionType::Xbox);     ImGui::SameLine();
        renderTypeTab(lblMacro,               ActionType::Macro);    ImGui::SameLine();
        renderTypeTab(lblKeyboard,            ActionType::Keyboard); ImGui::SameLine();
        renderTypeTab(lblMouse,               ActionType::Mouse);    ImGui::SameLine();
        renderTypeTab(lblBot,                 ActionType::Bot);      ImGui::SameLine();
        {
            const std::vector<RangeEdit>& curRanges = (m_sel.triggerSrc == "l2") ? m_model.trigLRangeEdits : m_model.trigRRangeEdits;
            bool hasRanges = !curRanges.empty();
            if (hasRanges) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(trid("btn.ranges", "trigRanges").c_str(), { btnW, 0.0f })) {
                m_trigRangeModal.open(m_sel.triggerSrc, curRanges, m_engine->getLoadedBotNames());
                m_sel.actionType = ActionType::Xbox;
                m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
            }
            if (hasRanges) ImGui::PopStyleColor();
        }

        ImGui::Spacing();

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
            if (ActionPanel::renderMacroCombo("macTrigger", m_sel.macroSel, m_macroNames, availW,
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
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) { m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear(); }
            else if (ActionPanel::renderKeyboardCapture("kbTrigger", m_sel.captureKeys, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard; act.physical = m_sel.triggerSrc;
                for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mbTrigger", mbResult, availW)) {
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
            if (ActionPanel::renderBotCombo("botTrigger", m_sel.botSel, availableBots, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::Bot; act.physical = m_sel.triggerSrc; act.name = m_sel.botSel;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.botSel.clear();
            }
        }
    } // trigger action panel

    // ── Modal Rangos ──────────────────────────────────────────────────────────
    if (m_trigRangeModal.render()) {
        const std::string& key = m_trigRangeModal.forKey();
        if (key == "l2" || key == "r2") {
            if (key == "l2") m_model.trigLRangeEdits = m_trigRangeModal.result();
            else             m_model.trigRRangeEdits = m_trigRangeModal.result();
            m_model.trigActionEdits.erase(key);
            m_sel.triggerSrc.clear();
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

    // ── Guardar / Cancelar (normal mode only — profile mode uses the header) ──
    if (m_mode == Mode::kNormal) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button(trid("btn.save", "mapSave").c_str(), { 120.0f, 0.0f })) {
            save();
            m_active = false;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.35f, 0.35f, 0.35f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.45f, 0.45f, 0.45f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.25f, 0.25f, 0.25f, 1.0f });
        if (ImGui::Button(trid("btn.cancel", "mapCancel").c_str(), { 100.0f, 0.0f })) {
            m_sel.physComp = -1; m_sel.stickDir.clear(); m_sel.stickAsButton = false;
            m_sel.dpadDir.clear(); m_sel.triggerSrc.clear();
            m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear(); m_sel.botSel.clear();
            reload();
            m_active = false;
        }
        ImGui::PopStyleColor(3);
    }
}

// ---------------------------------------------------------------------------
// Click handling — chained dispatch
// ---------------------------------------------------------------------------
void MappingEditor::handleClick(PadView& phys, PadView& virt, ImVec2 mouse) {
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;

    std::string arrowDir;
    int arrowComp = phys.hitTestStickArrow(mouse, m_physOrigin, arrowDir);
    if (arrowComp >= 0) { onArrowHit(arrowComp, arrowDir); return; }

    int physHit = phys.hitTest(mouse, m_physOrigin);
    if (physHit >= 0) {
        const std::string& hitType = phys.getLayout().components[physHit].type;
        if      (hitType == "button") onPhysButtonHit(phys, physHit);
        else if (hitType == "stick")  onPhysStickHit(physHit);
        else if (hitType == "dpad")   onPhysDpadHit(phys, physHit, mouse);
        return;
    }

    // Virtual stick arrows: assign selected source to a stick slot.
    {
        std::string virtArrowDir;
        int virtArrowComp = virt.hitTestStickArrow(mouse, m_virtOrigin, virtArrowDir);
        if (virtArrowComp >= 0) {
            bool hasSource = (m_sel.physComp >= 0 &&
                              (phys.getLayout().components[m_sel.physComp].type != "stick" ||
                               m_sel.stickAsButton)) ||
                             (!m_sel.triggerSrc.empty() && m_sel.actionType == ActionType::Xbox);
            if (hasSource) { onVirtArrowHit(phys, virt, virtArrowComp, virtArrowDir); return; }
        }
    }

    if (m_sel.physComp >= 0) {
        const std::string& selType = phys.getLayout().components[m_sel.physComp].type;
        if (selType == "stick" && !m_sel.stickAsButton) {
            if (!m_sel.stickDir.empty() && m_sel.actionType == ActionType::Xbox)
                onVirtHitAxisAction(phys, virt, mouse);
            else if (m_sel.stickDir.empty())
                onVirtHitPhysStick(phys, virt, mouse);
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
