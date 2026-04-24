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

                // ── Axis-action mode: stick direction seleccionada → asignar VirtualButton/Dpad ──
                if (!m_sel.stickDir.empty()) {
                    auto [xId9, yId9] = stickIdsFromStateX(selComp2.stateX);
                    std::string axisKey9;
                    if      (m_sel.stickDir == "up")    axisKey9 = yId9 + "_pos";
                    else if (m_sel.stickDir == "down")  axisKey9 = yId9 + "_neg";
                    else if (m_sel.stickDir == "right") axisKey9 = xId9 + "_pos";
                    else if (m_sel.stickDir == "left")  axisKey9 = xId9 + "_neg";
                    bool isDpad9 = (virtShort.rfind("dpad_", 0) == 0);
                    if ((valid || isDpad9) && !axisKey9.empty()) {
                        HalfAxisAction ha9;
                        if (isDpad9) {
                            ha9.type = HalfAxisActionType::Dpad;
                            ha9.target = virtShort.substr(5); // "up"/"down"/"left"/"right"
                        } else {
                            ha9.type = HalfAxisActionType::VirtualButton;
                            ha9.target = virtShort;
                        }
                        auto it9 = m_model.axisActionEdits.find(axisKey9);
                        bool already9 = (it9 != m_model.axisActionEdits.end() &&
                                         it9->second.type == ha9.type &&
                                         it9->second.target == ha9.target);
                        if (already9) {
                            m_model.axisActionEdits.erase(axisKey9);
                            m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                        } else {
                            m_model.axisActionEdits[axisKey9] = ha9;
                            if (!isDpad9) {
                                int fc = findCompByState(virt.getLayout(), shortToState(virtShort));
                                m_sel.flashComp = fc; m_sel.flashTimer = 0.5f;
                                m_sel.flashVirtShort = shortToState(virtShort);
                            } else {
                                m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                            }
                        }
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.stickAsButton = false; m_sel.actionType = H5ActionType::Xbox;
                    } else {
                        m_sel.h9ErrorTimer = 2.0f;
                    }
                    break;
                }

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

            // Analog stick tilt → assign source to a stick slot destination.
            if (m_sel.physComp >= 0 && !physShort.empty()) {
                const auto& virtComps2 = virt.getLayout().components;
                for (int i = 0; i < (int)virtComps2.size(); ++i) {
                    if (virtComps2[i].type != "stick") continue;
                    float sx = 0.0f, sy = 0.0f;
                    readStickXY(physNow, virtComps2[i].stateX, sx, sy);
                    std::string slotDir;
                    if      (sy >=  m_stickSelectThreshold) slotDir = "up";
                    else if (sy <= -m_stickSelectThreshold) slotDir = "down";
                    else if (sx >=  m_stickSelectThreshold) slotDir = "right";
                    else if (sx <= -m_stickSelectThreshold) slotDir = "left";
                    if (slotDir.empty()) continue;

                    auto [vxId, vyId] = stickIdsFromStateX(virtComps2[i].stateX);
                    std::string slotKey;
                    if      (slotDir == "up")    slotKey = vyId + "_pos";
                    else if (slotDir == "down")  slotKey = vyId + "_neg";
                    else if (slotDir == "right") slotKey = vxId + "_pos";
                    else if (slotDir == "left")  slotKey = vxId + "_neg";

                    auto it = m_model.buttonEdits.find(physShort);
                    if (it != m_model.buttonEdits.end() && it->second == slotKey) {
                        m_model.buttonEdits.erase(physShort);
                    } else {
                        m_model.h5ActionEdits.erase(physShort);
                        m_model.buttonEdits[physShort] = slotKey;
                    }

                    m_sel.physComp = -1; m_sel.stickAsButton = false;
                    m_sel.dpadDir.clear(); m_sel.actionType = H5ActionType::Xbox;
                    break;
                }
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
            // Virtual stick tilt → assign trigger source to a stick slot.
            if (!m_sel.triggerSrc.empty()) {
                const auto& virtComps2 = virt.getLayout().components;
                for (int i = 0; i < (int)virtComps2.size(); ++i) {
                    if (virtComps2[i].type != "stick") continue;
                    float sx = 0.0f, sy = 0.0f;
                    readStickXY(physNow, virtComps2[i].stateX, sx, sy);
                    std::string slotDir;
                    if      (sy >=  m_stickSelectThreshold) slotDir = "up";
                    else if (sy <= -m_stickSelectThreshold) slotDir = "down";
                    else if (sx >=  m_stickSelectThreshold) slotDir = "right";
                    else if (sx <= -m_stickSelectThreshold) slotDir = "left";
                    if (slotDir.empty()) continue;

                    auto [vxId, vyId] = stickIdsFromStateX(virtComps2[i].stateX);
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
                    m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox;
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
            else if (!m_sel.stickDir.empty()) {
                if (m_sel.actionType == H5ActionType::Xbox)
                    msg = "Haz clic en el bot\xC3\xB3n, stick o cruceta virtual para asignar";
                else if (m_sel.actionType == H5ActionType::Keyboard)
                    msg = m_sel.captureKeys.empty()
                        ? "Pulsa las teclas del combo  (L1+R1 o A+B para cancelar)"
                        : "Pulsa m\xC3\xA1s teclas o haz clic en Asignar";
                else
                    msg = "Elige la acci\xC3\xB3n del semieje en el panel";
            } else
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
    // ── H6 T4: axis_actions panel (stick direction selected) ─────────────────
    if (m_sel.physComp >= 0) {
        const auto& physComps4 = phys.getLayout().components;
        if (m_sel.physComp < (int)physComps4.size() &&
            physComps4[m_sel.physComp].type == "stick" &&
            !m_sel.stickAsButton && !m_sel.stickDir.empty()) {

            const auto& selComp4 = physComps4[m_sel.physComp];
            auto [xId4, yId4] = stickIdsFromStateX(selComp4.stateX);
            std::string axisKey;
            if      (m_sel.stickDir == "up")    axisKey = yId4 + "_pos";
            else if (m_sel.stickDir == "down")  axisKey = yId4 + "_neg";
            else if (m_sel.stickDir == "right") axisKey = xId4 + "_pos";
            else if (m_sel.stickDir == "left")  axisKey = xId4 + "_neg";

            if (!axisKey.empty()) {
                float availW4 = m_virtOrigin.x + virt.getLayout().W - m_physOrigin.x;
                ImGui::Spacing();

                // Direction label
                {
                    float hdrW = ImGui::CalcTextSize(axisKey.c_str()).x;
                    float offX = (availW4 - hdrW) * 0.5f;
                    if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                    ImGui::TextColored({ 1.0f, 0.86f, 0.0f, 1.0f }, "%s", axisKey.c_str());
                }

                // Tab buttons
                constexpr float kBtnW = 80.0f;
                float totalW = kBtnW * 5 + ImGui::GetStyle().ItemSpacing.x * 4;
                float offX = (availW4 - totalW) * 0.5f;
                if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                auto typeBtn4 = [&](const char* label, H5ActionType type) {
                    bool s = (m_sel.actionType == type);
                    if (s) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    if (ImGui::Button(label, { kBtnW, 0.0f })) { m_sel.actionType = type; m_sel.captureKeys.clear(); }
                    if (s) ImGui::PopStyleColor();
                };
                typeBtn4("Mando##axt0",        H5ActionType::Xbox);      ImGui::SameLine();
                typeBtn4("Macro##axt1",         H5ActionType::Macro);     ImGui::SameLine();
                typeBtn4("Teclado##axt2",       H5ActionType::Keyboard);  ImGui::SameLine();
                typeBtn4("Rat\xC3\xB3n##axt3",  H5ActionType::Mouse);     ImGui::SameLine();
                typeBtn4("Mov.Rat\xC3\xB3n##axt4", H5ActionType::MouseMove);

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
                    if (ActionPanel::renderMacroCombo("mac_ax", m_sel.macroSel, m_macroNames, availW4)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Macro; ha.target = m_sel.macroSel;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = H5ActionType::Xbox; m_sel.macroSel.clear();
                    }
                } else if (m_sel.actionType == H5ActionType::Keyboard) {
                    bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
                    if (cancel) { m_sel.actionType = H5ActionType::Xbox; m_sel.captureKeys.clear(); }
                    else if (ActionPanel::renderKeyboardCapture("kb_ax", m_sel.captureKeys, availW4)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Keyboard;
                        for (const auto& p : m_sel.captureKeys) ha.keys.push_back(p.first);
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = H5ActionType::Xbox; m_sel.captureKeys.clear();
                    }
                } else if (m_sel.actionType == H5ActionType::Mouse) {
                    std::string mbResult;
                    if (ActionPanel::renderMouseButtons("mb_ax", mbResult, availW4)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::MouseClick; ha.mouseButton = mbResult;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = H5ActionType::Xbox;
                    }
                } else if (m_sel.actionType == H5ActionType::MouseMove) {
                    float panelW = 280.0f;
                    float offX2 = (availW4 - panelW) * 0.5f;
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);
                    ImGui::TextDisabled("Asigna ambas direcciones del eje");
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);
                    ImGui::SetNextItemWidth(100.0f);
                    ImGui::SliderFloat("Vel.##axspd", &m_sel.axisMouseSpeed, 1.0f, 50.0f, "%.0f");
                    ImGui::SameLine();
                    const char* mouseAxes[] = { "X", "Y" };
                    int axIdx = (m_sel.axisMouseAxis == "mouse_y") ? 1 : 0;
                    ImGui::SetNextItemWidth(60.0f);
                    if (ImGui::Combo("Eje##axax", &axIdx, mouseAxes, 2))
                        m_sel.axisMouseAxis = (axIdx == 1) ? "mouse_y" : "mouse_x";
                    ImGui::SameLine();
                    if (ImGui::Button("Asignar##axmov")) {
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
                        m_sel.actionType = H5ActionType::Xbox;
                    }
                }
                // Mando mode: user clicks virtual pad → onVirtHitAxisAction

                // Clear button if already assigned
                if (m_model.axisActionEdits.count(axisKey)) {
                    ImGui::Spacing();
                    float clearW = 100.0f;
                    float offX3 = (availW4 - clearW) * 0.5f;
                    if (offX3 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX3);
                    if (ImGui::Button("Limpiar##axclr", { clearW, 0.0f })) {
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
    } // H6 T4
    } // physComp >= 0 (H5/H6 panel)

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
                m_trigRangeModal.open(m_sel.triggerSrc, curRanges);
                m_sel.actionType = H5ActionType::Xbox;
                m_sel.captureKeys.clear(); m_sel.macroSel.clear();
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
    if (m_trigRangeModal.render()) {
        const std::string& trig = m_trigRangeModal.forTrigger();
        if (trig == "l2")
            m_model.trigLRangeEdits = m_trigRangeModal.result();
        else
            m_model.trigRRangeEdits = m_trigRangeModal.result();
        m_model.trigActionEdits.erase(trig);
        m_sel.triggerSrc.clear();
        m_sel.actionType = H5ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
    }

    // ── Gestión de clicks ─────────────────────────────────────────────────────
    if (mouseClicked)
        handleClick(phys, virt, mouse);

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
                             (!m_sel.triggerSrc.empty() && m_sel.actionType == H5ActionType::Xbox);
            if (hasSource) { onVirtArrowHit(phys, virt, virtArrowComp, virtArrowDir); return; }
        }
    }

    if (m_sel.physComp >= 0) {
        const std::string& selType = phys.getLayout().components[m_sel.physComp].type;
        if (selType == "stick" && !m_sel.stickAsButton) {
            if (!m_sel.stickDir.empty() && m_sel.actionType == H5ActionType::Xbox)
                onVirtHitAxisAction(phys, virt, mouse);
            else if (m_sel.stickDir.empty())
                onVirtHitPhysStick(phys, virt, mouse);
        } else if (m_sel.actionType == H5ActionType::Xbox) {
            onVirtHitPhysButton(phys, virt, mouse);
        }
        return;
    }

    if (!m_sel.triggerSrc.empty() && m_sel.actionType == H5ActionType::Xbox)
        onVirtHitTriggerSrc(virt, mouse);
}

// ---------------------------------------------------------------------------
void MappingEditor::onArrowHit(int arrowComp, const std::string& dir) {
    if (m_sel.physComp == arrowComp && m_sel.stickDir == dir && !m_sel.stickAsButton) {
        m_sel.physComp = -1; m_sel.stickDir.clear(); m_sel.stickAsButton = false;
    } else {
        m_sel.physComp = arrowComp; m_sel.stickDir = dir; m_sel.stickAsButton = false;
        m_sel.actionType = H5ActionType::Xbox;
        m_sel.triggerSrc.clear(); m_sel.dpadDir.clear();
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
    }
}

// ---------------------------------------------------------------------------
void MappingEditor::onPhysButtonHit(PadView& phys, int physHit) {
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
}

// ---------------------------------------------------------------------------
void MappingEditor::onPhysStickHit(int physHit) {
    if (physHit == m_sel.physComp && m_sel.stickAsButton) {
        m_sel.physComp = -1; m_sel.stickAsButton = false;
    } else {
        m_sel.physComp = physHit; m_sel.stickAsButton = true; m_sel.stickDir.clear();
        m_sel.actionType = H5ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
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
        m_sel.actionType = H5ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
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

    if (assigned) { m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox; }
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
        } else {
            auto& ranges = (source == "l2") ? m_model.trigLRangeEdits : m_model.trigRRangeEdits;
            ranges.clear();
            ButtonAction act;
            act.type = ButtonActionType::VirtualButton;
            act.name = slotKey;
            m_model.trigActionEdits[source] = act;
        }
    } else {
        // Buttons / dpad: stored as buttonEdits[physShort] = slotDir.
        auto it = m_model.buttonEdits.find(source);
        if (it != m_model.buttonEdits.end() && it->second == slotKey) {
            m_model.buttonEdits.erase(source);
        } else {
            m_model.h5ActionEdits.erase(source);
            m_model.buttonEdits[source] = slotKey;
        }
    }

    m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
    m_sel.triggerSrc.clear(); m_sel.actionType = H5ActionType::Xbox;
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
                } else {
                    HalfAxisAction ha;
                    ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                    m_model.axisActionEdits[axisKey] = ha;
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

    if (assigned) { m_sel.physComp = -1; m_sel.stickDir.clear(); }
}
