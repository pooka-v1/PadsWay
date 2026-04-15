#include "MappingEditor.h"
#include "MappingHelpers.h"
#include "ActionPanel.h"
#include "../imgui/imgui.h"
#include "../nlohmann/json.hpp"
using json = nlohmann::json;
#include "../config/ConfigLoader.h"

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
void MappingEditor::reload() {
    m_model.reload(m_configs);
    m_sel.triggerSrc.clear();
    m_sel.h9HoldTriggerSrc.clear();
    m_sel.h9HoldTriggerTimer = 0.0f;
}

void MappingEditor::save() {
    try { m_model.save("data/controllers.json"); } catch (...) {}
    m_configs = loadControllerConfigs("data/controllers.json");
    m_engine->reloadConfigs();
    m_configsSaved = true;
}

// ---------------------------------------------------------------------------
// render — full mapping editor UI (called each frame when active)
// ---------------------------------------------------------------------------
void MappingEditor::render(PadView& phys, PadView& virt) {
    // ── Pre-populate edits cuando cambia el mando activo ─────────────────────
    DeviceCandidate dev = m_engine->getActiveDevice();
    if (dev.vid != m_model.vid || dev.pid != m_model.pid) {
        m_model.vid   = dev.vid;
        m_model.pid   = dev.pid;
        m_sel.physComp = -1;
        reload();
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
                                m_sel.actionType    = H5ActionType::Xbox;
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
                                    m_sel.actionType         = H5ActionType::Xbox;
                                    m_sel.captureKeys.clear();
                                    m_sel.macroSel.clear();
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
        } else if (m_sel.physComp >= 0 && m_sel.actionType == H5ActionType::Xbox) {
            // Paso 2 (solo modo Xbox): detectar rising edge → asignar botón virtual
            const PadComponent& selComp2 = physComps[m_sel.physComp];
            std::string selState;
            if (m_sel.stickAsButton)
                selState = selComp2.stateClick;
            else if (selComp2.type == "dpad" && !m_sel.dpadDir.empty())
                selState = dpadDirToState(selComp2, m_sel.dpadDir);
            else
                selState = selComp2.state;
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

                if (valid) {
                    if (!physShort.empty()) {
                        m_model.h5ActionEdits.erase(physShort);
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
                    m_sel.actionType = H5ActionType::Xbox;
                } else {
                    bool hasAssignment = m_model.h5ActionEdits.count(physShort) > 0 ||
                        (m_model.buttonEdits.count(physShort) && !m_model.buttonEdits.at(physShort).empty());
                    if (hasAssignment && !physShort.empty()) {
                        m_model.buttonEdits[physShort] = "";
                        m_model.h5ActionEdits.erase(physShort);
                        m_sel.physComp    = -1;
                        m_sel.stickAsButton = false;
                        m_sel.dpadDir.clear();
                        m_sel.actionType = H5ActionType::Xbox;
                    } else {
                        m_sel.h9ErrorTimer = 2.0f;
                    }
                }
                break;
            }

            // Physical L2/R2 → asignar componente seleccionado como gatillo virtual
            {
                constexpr float kTrigThresh = 0.5f;
                auto doTrigAssign = [&](const std::string& trigTarget, const std::string& trigState) {
                    if (physShort.empty()) return;
                    auto h5it = m_model.h5ActionEdits.find(physShort);
                    bool already = (h5it != m_model.h5ActionEdits.end() &&
                                    h5it->second.type == ButtonActionType::Trigger &&
                                    h5it->second.target == trigTarget);
                    if (already) {
                        m_model.h5ActionEdits.erase(physShort);
                        m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                    } else {
                        ButtonAction act;
                        act.type = ButtonActionType::Trigger; act.physical = physShort; act.target = trigTarget;
                        m_model.h5ActionEdits[physShort] = act;
                        m_model.buttonEdits.erase(physShort);
                        m_sel.flashComp      = findCompByState(virt.getLayout(), trigState);
                        m_sel.flashTimer     = 0.5f;
                        m_sel.flashVirtShort = trigState;
                    }
                    m_sel.physComp = -1; m_sel.stickAsButton = false;
                    m_sel.dpadDir.clear(); m_sel.actionType = H5ActionType::Xbox;
                };
                if (physNow.triggerL > kTrigThresh && m_sel.h9PrevPhysState.triggerL <= kTrigThresh)
                    doTrigAssign("l2", "triggerL");
                else if (physNow.triggerR > kTrigThresh && m_sel.h9PrevPhysState.triggerR <= kTrigThresh)
                    doTrigAssign("r2", "triggerR");
            }
        } else if (!m_sel.triggerSrc.empty() && m_sel.actionType == H5ActionType::Xbox) {
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
                m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox;
                break;
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
                    m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox;
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
    if (m_sel.flashTimer <= 0.0f) { m_sel.flashComp = -1; m_sel.flashVirtShort.clear(); }

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
    if (m_sel.physComp >= 0) {
        const auto& physComps = phys.getLayout().components;
        if (m_sel.physComp < (int)physComps.size()) {
            const PadComponent& selComp = physComps[m_sel.physComp];
            auto activateTriggerIfAssigned = [&](const std::string& physShort) -> bool {
                auto h5trig = m_model.h5ActionEdits.find(physShort);
                if (h5trig != m_model.h5ActionEdits.end() && h5trig->second.type == ButtonActionType::Trigger) {
                    activateState(virtDisplay, h5trig->second.target == "l2" ? "triggerL" : "triggerR");
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
                    std::string virtShort = (it != m_model.buttonEdits.end()) ? it->second : physShort;
                    activateState(virtDisplay, shortToState(virtShort));
                }
            } else if (selComp.type == "stick" && m_sel.stickAsButton) {
                activateState(physDisplay, selComp.stateClick);
                std::string physShort = stateToShort(selComp.stateClick);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_model.buttonEdits.find(physShort);
                    std::string virtShort = (it != m_model.buttonEdits.end()) ? it->second : physShort;
                    activateState(virtDisplay, shortToState(virtShort));
                }
            } else if (selComp.type == "stick") {
                (void)selComp;
            } else if (selComp.type == "dpad" && !m_sel.dpadDir.empty()) {
                std::string dpadState = dpadDirToState(selComp, m_sel.dpadDir);
                activateState(physDisplay, dpadState);
                std::string physShort = stateToShort(dpadState);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_model.buttonEdits.find(physShort);
                    std::string virtShort = (it != m_model.buttonEdits.end()) ? it->second : physShort;
                    activateState(virtDisplay, shortToState(virtShort));
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
                activateState(virtDisplay, shortToState(act.name));
        }
    }
    if (m_sel.flashComp >= 0 && !m_sel.flashVirtShort.empty())
        activateState(virtDisplay, shortToState(m_sel.flashVirtShort));

    // ── Pad físico ────────────────────────────────────────────────────────────
    ImGui::BeginGroup();
    m_physOrigin = ImGui::GetCursorScreenPos();
    phys.render(physDisplay);
    phys.renderStickArrows(m_physOrigin, m_sel.physComp, m_sel.stickDir);
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextDisabled("F\xC3\xADsico");
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
    virt.renderStickArrows(m_virtOrigin, -1, "");
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextDisabled("Virtual (Xbox One)");
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
            msg = "Ese bot\xC3\xB3n no tiene equivalente Xbox \xe2\x80\x94 elige otro";
            col = { 1.0f, 0.3f, 0.3f, 1.0f };
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && !m_sel.h9HoldTriggerSrc.empty()) {
            msg = "Mant\xC3\xA9n el gatillo al tope para seleccionarlo como fuente";
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty() && m_sel.h9HoldComp >= 0) {
            msg = m_sel.h9HoldStickDir.empty()
                ? "Mant\xC3\xA9n pulsado para seleccionar"
                : "Mant\xC3\xA9n el stick al tope para seleccionar direcci\xC3\xB3n";
        } else if (m_sel.physComp < 0 && m_sel.triggerSrc.empty()) {
            msg = "Elige el bot\xC3\xB3n, cruceta, stick o gatillo que quieres reasignar";
        } else if (!m_sel.triggerSrc.empty()) {
            if (m_sel.actionType == H5ActionType::Keyboard)
                msg = m_sel.captureKeys.empty()
                    ? "Pulsa las teclas del combo  (L1+R1 o A+B para cancelar)"
                    : "Pulsa m\xC3\xA1s teclas o haz clic en Asignar";
            else if (m_sel.actionType == H5ActionType::Xbox)
                msg = "Haz clic en el bot\xC3\xB3n o gatillo virtual  \xe2\x80\x94  o pulsa el bot\xC3\xB3n f\xC3\xADsico";
            else
                msg = "Elige la acci\xC3\xB3n del gatillo en el panel";
        } else if (m_sel.physComp >= 0 &&
                   phys.getLayout().components[m_sel.physComp].type == "stick" &&
                   (!m_sel.stickAsButton || m_sel.actionType == H5ActionType::Xbox)) {
            if (m_sel.stickAsButton)
                msg = "Elige en el virtual o pulsa el bot\xC3\xB3n f\xC3\xADsico que quieras asignarle";
            else
                msg = "Haz clic en el stick o cruceta virtual al que quieres asignar";
        } else if (m_sel.actionType == H5ActionType::Keyboard) {
            msg = m_sel.captureKeys.empty()
                ? "Pulsa las teclas del combo  (L1+R1 o A+B para cancelar)"
                : "Pulsa m\xC3\xA1s teclas o haz clic en Asignar";
        } else {
            msg = "Elige bot\xC3\xB3n, stick o cruceta en el virtual  \xe2\x80\x94  o pulsa el bot\xC3\xB3n f\xC3\xADsico";
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

    // ── H5/H6: UI específica según el tipo de componente seleccionado ──────────
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
        float totalW = btnW * 4 + ImGui::GetStyle().ItemSpacing.x * 3;
        float offX   = (availW - totalW) * 0.5f;
        if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);

        auto typeBtn = [&](const char* label, H5ActionType type) {
            bool sel = (m_sel.actionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_sel.actionType = type;
                m_sel.captureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        typeBtn("Xbox##t0",    H5ActionType::Xbox);    ImGui::SameLine();
        typeBtn("Macro##t1",   H5ActionType::Macro);   ImGui::SameLine();
        typeBtn("Teclado##t2", H5ActionType::Keyboard); ImGui::SameLine();
        typeBtn("Rat\xC3\xB3n##t3",  H5ActionType::Mouse);

        ImGui::Spacing();

        if (m_sel.actionType == H5ActionType::Macro) {
            if (!m_macroNamesLoaded) {
                m_macroNames.clear();
                try {
                    std::ifstream f("data/macros.json");
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k,v] : j.items()) m_macroNames.push_back(k); }
                } catch (...) {}
                m_macroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_h5", m_sel.macroSel, m_macroNames, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Macro; act.physical = physShortSel; act.name = m_sel.macroSel;
                    m_model.h5ActionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = H5ActionType::Xbox; m_sel.macroSel.clear();
            }

        } else if (m_sel.actionType == H5ActionType::Keyboard) {
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) {
                m_sel.actionType = H5ActionType::Xbox; m_sel.captureKeys.clear();
            } else if (ActionPanel::renderKeyboardCapture("kb_h5", m_sel.captureKeys, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Keyboard; act.physical = physShortSel;
                    for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                    m_model.h5ActionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = H5ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == H5ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_h5", mbResult, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::MouseClick; act.physical = physShortSel; act.mouseButton = mbResult;
                    m_model.h5ActionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = H5ActionType::Xbox;
            }
        }
    } // H5 block
    } // if (m_sel.physComp >= 0)

    // ── H7: UI para gatillo como fuente ──────────────────────────────────────
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
        float totalW = btnW * 5 + ImGui::GetStyle().ItemSpacing.x * 4;
        float offX   = (availW - totalW) * 0.5f;
        if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
        auto typeBtn7 = [&](const char* label, H5ActionType type) {
            bool sel = (m_sel.actionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_sel.actionType = type; m_sel.captureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        typeBtn7("Xbox/Anal.##h7t0", H5ActionType::Xbox);    ImGui::SameLine();
        typeBtn7("Macro##h7t1",      H5ActionType::Macro);   ImGui::SameLine();
        typeBtn7("Teclado##h7t2",    H5ActionType::Keyboard); ImGui::SameLine();
        typeBtn7("Rat\xC3\xB3n##h7t3", H5ActionType::Mouse); ImGui::SameLine();
        {
            const std::vector<RangeEdit>& curRanges = (m_sel.triggerSrc == "l2") ? m_model.trigLRangeEdits : m_model.trigRRangeEdits;
            bool hasRanges = !curRanges.empty();
            if (hasRanges) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("Rangos##h7t4", { btnW, 0.0f })) {
                m_rangosForTrigger = m_sel.triggerSrc;
                m_rangosWork       = curRanges;
                m_rangosSelSect    = -1;
                m_rangosActType    = H5ActionType::Xbox;
                m_rangosCaptureKeys.clear(); m_rangosMacroSel.clear(); m_rangosXboxSel = -1;
                m_sel.actionType = H5ActionType::Xbox;
                m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                if (m_rangosWork.empty()) {
                    RangeEdit re; re.from = 0.1f; re.to = 1.0f;
                    m_rangosWork.push_back(re);
                }
                m_rangosModalOpen = true;
            }
            if (hasRanges) ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        if (m_sel.actionType == H5ActionType::Macro) {
            if (!m_macroNamesLoaded) {
                m_macroNames.clear();
                try {
                    std::ifstream f("data/macros.json");
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k,v] : j.items()) m_macroNames.push_back(k); }
                } catch (...) {}
                m_macroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_h7", m_sel.macroSel, m_macroNames, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::Macro; act.physical = m_sel.triggerSrc; act.name = m_sel.macroSel;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox; m_sel.macroSel.clear();
            }

        } else if (m_sel.actionType == H5ActionType::Keyboard) {
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) { m_sel.actionType = H5ActionType::Xbox; m_sel.captureKeys.clear(); }
            else if (ActionPanel::renderKeyboardCapture("kb_h7", m_sel.captureKeys, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard; act.physical = m_sel.triggerSrc;
                for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == H5ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_h7", mbResult, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::MouseClick; act.physical = m_sel.triggerSrc; act.mouseButton = mbResult;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox;
            }
        }
    } // H7 block

    // ── Modal Rangos ──────────────────────────────────────────────────────────
    renderRangosModal();

    // ── Gestión de clicks ─────────────────────────────────────────────────────
    if (mouseClicked && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        std::string arrowDir;
        int arrowComp = phys.hitTestStickArrow(mouse, m_physOrigin, arrowDir);
        if (arrowComp >= 0) {
            if (m_sel.physComp == arrowComp && m_sel.stickDir == arrowDir && !m_sel.stickAsButton) {
                m_sel.physComp = -1; m_sel.stickDir.clear(); m_sel.stickAsButton = false;
            } else {
                m_sel.physComp = arrowComp; m_sel.stickDir = arrowDir; m_sel.stickAsButton = false;
                m_sel.captureKeys.clear(); m_sel.macroSel.clear();
            }
        } else {

        int physHit = phys.hitTest(mouse, m_physOrigin);
        if (physHit >= 0) {
            const std::string& hitType = phys.getLayout().components[physHit].type;
            if (hitType == "button") {
                const std::string& hitState = phys.getLayout().components[physHit].state;
                if (hitState == "triggerL" || hitState == "triggerR") {
                    std::string trigSrc = (hitState == "triggerL") ? "l2" : "r2";
                    if (m_sel.triggerSrc == trigSrc) {
                        m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox;
                        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                    } else {
                        m_sel.triggerSrc = trigSrc; m_sel.physComp = -1;
                        m_sel.actionType = H5ActionType::Xbox;
                        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                    }
                } else if (physHit == m_sel.physComp) {
                    m_sel.physComp = -1; m_sel.actionType = H5ActionType::Xbox;
                    m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                } else {
                    m_sel.physComp = physHit; m_sel.triggerSrc.clear();
                    m_sel.actionType = H5ActionType::Xbox;
                    m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                }
            } else if (hitType == "stick") {
                if (physHit == m_sel.physComp && m_sel.stickAsButton) {
                    m_sel.physComp = -1; m_sel.stickAsButton = false;
                } else {
                    m_sel.physComp = physHit; m_sel.stickAsButton = true; m_sel.stickDir.clear();
                    m_sel.actionType = H5ActionType::Xbox;
                    m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                }
            } else if (hitType == "dpad") {
                const PadComponent& dc = phys.getLayout().components[physHit];
                std::string dir = dpadDirFromMouse(mouse, m_physOrigin.x + dc.cx, m_physOrigin.y + dc.cy);
                if (physHit == m_sel.physComp && m_sel.dpadDir == dir) {
                    m_sel.physComp = -1; m_sel.dpadDir.clear();
                } else {
                    m_sel.physComp = physHit; m_sel.triggerSrc.clear(); m_sel.dpadDir = dir;
                    m_sel.actionType = H5ActionType::Xbox;
                    m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                }
            }
        } else if (m_sel.physComp >= 0) {
            const std::string& selType2 = phys.getLayout().components[m_sel.physComp].type;
            if ((selType2 == "button" || (selType2 == "stick" && m_sel.stickAsButton)
                 || selType2 == "dpad")
                && m_sel.actionType == H5ActionType::Xbox) {
                int virtHit = virt.hitTest(mouse, m_virtOrigin);
                if (virtHit >= 0) {
                    const auto& virtComp  = virt.getLayout().components[virtHit];
                    const auto& physComps = phys.getLayout().components;
                    const auto& selPC = physComps[m_sel.physComp];
                    std::string physShort;
                    if (selType2 == "stick")
                        physShort = stateToShort(selPC.stateClick);
                    else if (selType2 == "dpad")
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
                            auto h5it = m_model.h5ActionEdits.find(physShort);
                            bool already = (h5it != m_model.h5ActionEdits.end() &&
                                            h5it->second.type == ButtonActionType::Trigger &&
                                            h5it->second.target == trigTarget);
                            if (already) {
                                m_model.h5ActionEdits.erase(physShort);
                                m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                            } else {
                                ButtonAction act;
                                act.type = ButtonActionType::Trigger; act.physical = physShort; act.target = trigTarget;
                                m_model.h5ActionEdits[physShort] = act;
                                m_model.buttonEdits.erase(physShort);
                                m_sel.flashComp = virtHit; m_sel.flashTimer = 0.5f; m_sel.flashVirtShort = virtShort;
                            }
                        } else {
                            m_model.h5ActionEdits.erase(physShort);
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
            } else if (selType2 == "stick") {
                int virtHit = virt.hitTest(mouse, m_virtOrigin);
                if (virtHit >= 0) {
                    const auto& virtComps = virt.getLayout().components;
                    const std::string& virtType = virtComps[virtHit].type;
                    const auto& physComps = phys.getLayout().components;
                    auto [xId, yId] = stickIdsFromStateX(physComps[m_sel.physComp].stateX);

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
                                        m_model.h6AxisEdits[sid] = edit;
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
                            m_model.h6AxisEdits[id] = edit;
                        };
                        buildDpadEdit(xId, "dpad_x");
                        if (!yId.empty()) buildDpadEdit(yId, "dpad_y");
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                    }
                }
            }
        } else if (!m_sel.triggerSrc.empty() && m_sel.actionType == H5ActionType::Xbox) {
            int virtHit = virt.hitTest(mouse, m_virtOrigin);
            if (virtHit >= 0) {
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
                if (assigned) { m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox; }
            }
        }
        } // end else (no arrow hit)
    }

    // ── Guardar / Cancelar ────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Guardar##mapSave", { 120.0f, 0.0f })) {
        save();
        m_active = false;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.35f, 0.35f, 0.35f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.45f, 0.45f, 0.45f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.25f, 0.25f, 0.25f, 1.0f });
    if (ImGui::Button("Cancelar##mapCancel", { 100.0f, 0.0f })) {
        m_sel.physComp = -1; m_sel.stickDir.clear(); m_sel.stickAsButton = false;
        m_sel.dpadDir.clear(); m_sel.triggerSrc.clear();
        m_sel.actionType = H5ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
        reload();
        m_active = false;
    }
    ImGui::PopStyleColor(3);
}

// ---------------------------------------------------------------------------
// renderRangosModal
// ---------------------------------------------------------------------------
void MappingEditor::renderRangosModal() {
    if (!m_rangosModalOpen) return;
    ImGui::OpenPopup("Rangos de gatillo##rangosModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 600.0f, 0.0f });

    if (!ImGui::BeginPopupModal("Rangos de gatillo##rangosModal", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;

    struct XboxChoice { const char* display; const char* name; };
    static const XboxChoice kChoices[] = {
        {"A","a"},{"B","b"},{"X","x"},{"Y","y"},
        {"L1","l1"},{"R1","r1"},{"Select","select"},{"Start","start"},{"Home","home"},
        {"L3","l3"},{"R3","r3"},
        {"Cruceta Arriba","up"},{"Cruceta Abajo","down"},
        {"Cruceta Izq","left"},{"Cruceta Der","right"},
    };
    static const int kNChoices = 15;

    const char* hdr = (m_rangosForTrigger == "l2")
        ? "L2  \xe2\x86\x92  Zonas de recorrido"
        : "R2  \xe2\x86\x92  Zonas de recorrido";
    ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", hdr);
    ImGui::Spacing();

    {
        int n = (int)m_rangosWork.size();
        float barW  = ImGui::GetContentRegionAvail().x - 4.0f;
        float barH  = 28.0f;
        ImVec2 barMin = { ImGui::GetCursorScreenPos().x + 2.0f, ImGui::GetCursorScreenPos().y };
        ImVec2 barMax = { barMin.x + barW, barMin.y + barH };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(barMin, barMax, IM_COL32(40,40,40,255), 4.0f);

        for (int i = 0; i < n; ++i) {
            float t0 = (m_rangosWork[i].from - 0.1f) / 0.9f;
            float t1 = (m_rangosWork[i].to   - 0.1f) / 0.9f;
            t0 = std::clamp(t0, 0.0f, 1.0f);
            t1 = std::clamp(t1, 0.0f, 1.0f);
            ImVec2 r0 = { barMin.x + t0 * barW + 1.0f, barMin.y + 1.0f };
            ImVec2 r1 = { barMin.x + t1 * barW - 1.0f, barMax.y - 1.0f };
            ImU32 col = (i == m_rangosSelSect)
                ? IM_COL32(255,180,0,220)
                : (m_rangosWork[i].hasAction ? IM_COL32(60,160,80,200) : IM_COL32(80,80,120,180));
            dl->AddRectFilled(r0, r1, col, 3.0f);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f\xe2\x80\x93%.2f", m_rangosWork[i].from, m_rangosWork[i].to);
            ImVec2 textSz = ImGui::CalcTextSize(buf);
            float cx = (r0.x + r1.x) * 0.5f - textSz.x * 0.5f;
            float cy = (r0.y + r1.y) * 0.5f - textSz.y * 0.5f;
            if (cx >= r0.x && cx + textSz.x <= r1.x)
                dl->AddText({ cx, cy }, IM_COL32(230,230,230,255), buf);
            if (i > 0)
                dl->AddLine({ barMin.x + t0 * barW, barMin.y }, { barMin.x + t0 * barW, barMax.y },
                             IM_COL32(200,200,200,160), 1.5f);
        }
        dl->AddRect(barMin, barMax, IM_COL32(150,150,150,200), 4.0f);

        ImGui::InvisibleButton("##rangeBar", { barW + 4.0f, barH });
        if (ImGui::IsItemClicked()) {
            float mx = ImGui::GetIO().MousePos.x - barMin.x;
            float normPos = mx / barW;
            float trigPos = normPos * 0.9f + 0.1f;
            for (int i = 0; i < n; ++i) {
                if (trigPos >= m_rangosWork[i].from && trigPos <= m_rangosWork[i].to) {
                    m_rangosSelSect = (m_rangosSelSect == i) ? -1 : i;
                    m_rangosActType = H5ActionType::Xbox;
                    m_rangosCaptureKeys.clear(); m_rangosMacroSel.clear(); m_rangosXboxSel = -1;
                    if (m_rangosSelSect >= 0 && m_rangosWork[i].hasAction) {
                        const auto& act = m_rangosWork[i].action;
                        if (act.type == ButtonActionType::Macro)       m_rangosActType = H5ActionType::Macro;
                        else if (act.type == ButtonActionType::Keyboard)   m_rangosActType = H5ActionType::Keyboard;
                        else if (act.type == ButtonActionType::MouseClick) m_rangosActType = H5ActionType::Mouse;
                        else m_rangosActType = H5ActionType::Xbox;
                    }
                    break;
                }
            }
        }
    }
    ImGui::Spacing();

    {
        int n = (int)m_rangosWork.size();
        bool canAdd = (n < 10);
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button(n == 1 ? "Partici\xC3\xB3n (crear 2 zonas)" : "Partici\xC3\xB3n (a\xC3\xB1\xC3\xA1\xC4\x80\xC3\xBDir zona)", { 0.0f, 0.0f })) {
            int newN = n + 1;
            m_rangosWork.clear();
            for (int i = 0; i < newN; ++i) {
                RangeEdit re;
                re.from = 0.1f + i       * 0.9f / (float)newN;
                re.to   = 0.1f + (i + 1) * 0.9f / (float)newN;
                if (i == newN - 1) re.to = 1.0f;
                m_rangosWork.push_back(re);
            }
            m_rangosSelSect = -1; m_rangosActType = H5ActionType::Xbox;
            m_rangosCaptureKeys.clear(); m_rangosMacroSel.clear();
        }
        if (!canAdd) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canRemove = (n > 1);
        if (!canRemove) ImGui::BeginDisabled();
        if (ImGui::Button("Quitar zona##rangeRm", { 0.0f, 0.0f }) && canRemove && m_rangosSelSect >= 0) {
            m_rangosWork.erase(m_rangosWork.begin() + m_rangosSelSect);
            int newN = (int)m_rangosWork.size();
            for (int i = 0; i < newN; ++i) {
                m_rangosWork[i].from = 0.1f + i       * 0.9f / (float)newN;
                m_rangosWork[i].to   = 0.1f + (i + 1) * 0.9f / (float)newN;
                if (i == newN - 1) m_rangosWork[i].to = 1.0f;
            }
            m_rangosSelSect = -1;
        }
        if (!canRemove) ImGui::EndDisabled();
        ImGui::SameLine();
        if (m_rangosSelSect >= 0 && m_rangosWork[m_rangosSelSect].hasAction) {
            if (ImGui::Button("Borrar acci\xC3\xB3n##rangeClear")) {
                m_rangosWork[m_rangosSelSect].hasAction = false;
                m_rangosWork[m_rangosSelSect].action = ButtonAction{};
                m_rangosActType = H5ActionType::Xbox; m_rangosCaptureKeys.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d/10 zonas)", (int)m_rangosWork.size());
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (m_rangosSelSect < 0) {
        ImGui::TextDisabled("Haz clic en una zona de la barra para asignarle una acci\xC3\xB3n.");
    } else {
        ImGui::Text("Zona %d  (%.2f \xe2\x80\x93 %.2f):",
                    m_rangosSelSect + 1,
                    m_rangosWork[m_rangosSelSect].from,
                    m_rangosWork[m_rangosSelSect].to);
        ImGui::Spacing();

        float bW = 85.0f;
        float sp = ImGui::GetStyle().ItemSpacing.x;
        float totalBtnW = bW * 4 + sp * 3;
        float offBX = (ImGui::GetContentRegionAvail().x - totalBtnW) * 0.5f;
        if (offBX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offBX);
        auto rTypeBtn = [&](const char* lbl, H5ActionType t) {
            bool sel = (m_rangosActType == t);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(lbl, { bW, 0.0f })) { m_rangosActType = t; m_rangosCaptureKeys.clear(); m_rangosMacroSel.clear(); m_rangosXboxSel = -1; }
            if (sel) ImGui::PopStyleColor();
        };
        rTypeBtn("Xbox##rt0",    H5ActionType::Xbox);   ImGui::SameLine();
        rTypeBtn("Macro##rt1",   H5ActionType::Macro);  ImGui::SameLine();
        rTypeBtn("Teclado##rt2", H5ActionType::Keyboard); ImGui::SameLine();
        rTypeBtn("Rat\xC3\xB3n##rt3", H5ActionType::Mouse);

        ImGui::Spacing();

        if (m_rangosActType == H5ActionType::Xbox) {
            float cW = 220.0f;
            float cOff = (ImGui::GetContentRegionAvail().x - cW - sp - 80.0f) * 0.5f;
            if (cOff > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cOff);
            ImGui::SetNextItemWidth(cW);
            const char* preview = (m_rangosXboxSel >= 0 && m_rangosXboxSel < kNChoices)
                ? kChoices[m_rangosXboxSel].display
                : "-- elige bot\xC3\xB3n --";
            if (m_rangosXboxSel < 0 && m_rangosWork[m_rangosSelSect].hasAction) {
                const auto& act = m_rangosWork[m_rangosSelSect].action;
                if (act.type == ButtonActionType::VirtualButton) {
                    for (int ci = 0; ci < kNChoices; ++ci) {
                        if (act.name == kChoices[ci].name) { m_rangosXboxSel = ci; break; }
                    }
                    if (m_rangosXboxSel >= 0) preview = kChoices[m_rangosXboxSel].display;
                }
            }
            if (ImGui::BeginCombo("##rangesXbox", preview)) {
                for (int ci = 0; ci < kNChoices; ++ci) {
                    bool sel = (m_rangosXboxSel == ci);
                    if (ImGui::Selectable(kChoices[ci].display, sel)) m_rangosXboxSel = ci;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            bool canAssign = (m_rangosXboxSel >= 0);
            if (!canAssign) ImGui::BeginDisabled();
            if (ImGui::Button("Asignar##rxbAssign", { 80.0f, 0.0f }) && canAssign) {
                ButtonAction act;
                act.type = ButtonActionType::VirtualButton;
                act.name = kChoices[m_rangosXboxSel].name;
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
            }
            if (!canAssign) ImGui::EndDisabled();
            if (m_rangosWork[m_rangosSelSect].hasAction &&
                m_rangosWork[m_rangosSelSect].action.type == ButtonActionType::VirtualButton) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_rangosWork[m_rangosSelSect].action.name.c_str());
            }

        } else if (m_rangosActType == H5ActionType::Macro) {
            if (!m_macroNamesLoaded) {
                m_macroNames.clear();
                try {
                    std::ifstream f("data/macros.json");
                    if (f.is_open()) { json j = json::parse(f); for (auto& [k,v] : j.items()) m_macroNames.push_back(k); }
                } catch (...) {}
                m_macroNamesLoaded = true;
            }
            if (ActionPanel::renderMacroCombo("mac_rng", m_rangosMacroSel, m_macroNames,
                                              ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type = ButtonActionType::Macro; act.name = m_rangosMacroSel;
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
            }

        } else if (m_rangosActType == H5ActionType::Keyboard) {
            if (ActionPanel::renderKeyboardCapture("kb_rng", m_rangosCaptureKeys,
                                                   ImGui::GetContentRegionAvail().x, true)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard;
                for (const auto& p : m_rangosCaptureKeys) act.keys.push_back(p.first);
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
                m_rangosCaptureKeys.clear();
            }
            if (m_rangosWork[m_rangosSelSect].hasAction &&
                m_rangosWork[m_rangosSelSect].action.type == ButtonActionType::Keyboard &&
                m_rangosCaptureKeys.empty()) {
                std::string ex;
                for (const auto& k : m_rangosWork[m_rangosSelSect].action.keys) { if (!ex.empty()) ex += "+"; ex += k; }
                ImGui::TextDisabled("  actual: %s", ex.c_str());
            }

        } else if (m_rangosActType == H5ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_rng", mbResult,
                                                ImGui::GetContentRegionAvail().x)) {
                ButtonAction act;
                act.type = ButtonActionType::MouseClick; act.mouseButton = mbResult;
                m_rangosWork[m_rangosSelSect].action    = act;
                m_rangosWork[m_rangosSelSect].hasAction = true;
            }
            if (m_rangosWork[m_rangosSelSect].hasAction &&
                m_rangosWork[m_rangosSelSect].action.type == ButtonActionType::MouseClick) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "\xe2\x86\x92 %s",
                    m_rangosWork[m_rangosSelSect].action.mouseButton.c_str());
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW2 = 100.0f;
    float dialogW = ImGui::GetContentRegionAvail().x;
    float btnOff = (dialogW - btnW2 * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (btnOff > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnOff);
    if (ImGui::Button("Aceptar##rangosOk", { btnW2, 0.0f })) {
        if (m_rangosForTrigger == "l2")
            m_model.trigLRangeEdits = m_rangosWork;
        else
            m_model.trigRRangeEdits = m_rangosWork;
        m_model.trigActionEdits.erase(m_rangosForTrigger);
        m_sel.triggerSrc.clear();
        m_sel.actionType = H5ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
        m_rangosModalOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar##rangosCan", { btnW2, 0.0f })) {
        m_rangosModalOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
