#include "MappingEditor.h"
#include "../config/Strings.h"
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
                    m_sel.actionType = ActionType::Xbox;
                } else {
                    bool hasAssignment = m_model.h5ActionEdits.count(physShort) > 0 ||
                        (m_model.buttonEdits.count(physShort) && !m_model.buttonEdits.at(physShort).empty());
                    if (hasAssignment && !physShort.empty()) {
                        m_model.buttonEdits[physShort] = "";
                        m_model.h5ActionEdits.erase(physShort);
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
                    auto [xId9, yId9] = stickIdsFromStateX(selComp2.stateX);
                    std::string axisKey9;
                    if      (m_sel.stickDir == "up")    axisKey9 = yId9 + "_pos";
                    else if (m_sel.stickDir == "down")  axisKey9 = yId9 + "_neg";
                    else if (m_sel.stickDir == "right") axisKey9 = xId9 + "_pos";
                    else if (m_sel.stickDir == "left")  axisKey9 = xId9 + "_neg";
                    if (axisKey9.empty()) return;
                    HalfAxisAction ha9;
                    ha9.type = HalfAxisActionType::Trigger;
                    ha9.target = trigTarget;
                    auto it9 = m_model.axisActionEdits.find(axisKey9);
                    bool already9 = (it9 != m_model.axisActionEdits.end() &&
                                     it9->second.type == ha9.type &&
                                     it9->second.target == ha9.target);
                    if (already9) {
                        m_model.axisActionEdits.erase(axisKey9);
                        m_sel.flashComp = -1; m_sel.flashTimer = 0.0f; m_sel.flashVirtShort.clear();
                    } else {
                        m_model.axisActionEdits[axisKey9] = ha9;
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
                const auto& virtComps2 = virt.getLayout().components;
                for (int i = 0; i < (int)virtComps2.size(); ++i) {
                    if (virtComps2[i].type != "stick") continue;
                    // Rising-edge: require stick to have been below threshold last frame.
                    // Prevents immediate fire when physComp is set while a stick is held.
                    float prevSx = 0.0f, prevSy = 0.0f;
                    readStickXY(m_sel.h9PrevPhysState, virtComps2[i].stateX, prevSx, prevSy);
                    if (prevSx >=  m_stickSelectThreshold || prevSx <= -m_stickSelectThreshold ||
                        prevSy >=  m_stickSelectThreshold || prevSy <= -m_stickSelectThreshold) continue;
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

                    if (!m_sel.stickDir.empty()) {
                        // Axis-action source: assign StickSlot target.
                        auto [xId9, yId9] = stickIdsFromStateX(selComp2.stateX);
                        std::string axisKey9;
                        if      (m_sel.stickDir == "up")    axisKey9 = yId9 + "_pos";
                        else if (m_sel.stickDir == "down")  axisKey9 = yId9 + "_neg";
                        else if (m_sel.stickDir == "right") axisKey9 = xId9 + "_pos";
                        else if (m_sel.stickDir == "left")  axisKey9 = xId9 + "_neg";
                        if (!axisKey9.empty()) {
                            HalfAxisAction ha;
                            ha.type = HalfAxisActionType::StickSlot; ha.target = slotKey;
                            auto it9 = m_model.axisActionEdits.find(axisKey9);
                            bool already9 = (it9 != m_model.axisActionEdits.end() &&
                                             it9->second.type == ha.type &&
                                             it9->second.target == ha.target);
                            if (already9) {
                                m_model.axisActionEdits.erase(axisKey9);
                                m_sel.flashSlotKey.clear(); m_sel.flashTimer = 0.0f; m_sel.flashComp = -1;
                            } else {
                                m_model.axisActionEdits[axisKey9] = ha;
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
                            m_model.h5ActionEdits.erase(physShort);
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
                    applyVirtShort(virtShort);
                }
            } else if (selComp.type == "stick" && m_sel.stickAsButton) {
                activateState(physDisplay, selComp.stateClick);
                std::string physShort = stateToShort(selComp.stateClick);
                if (!activateTriggerIfAssigned(physShort)) {
                    auto it = m_model.buttonEdits.find(physShort);
                    std::string virtShort = (it != m_model.buttonEdits.end()) ? it->second : physShort;
                    applyVirtShort(virtShort);
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
                    applyVirtShort(virtShort);
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

        auto typeBtn = [&](const char* label, ActionType type) {
            bool sel = (m_sel.actionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_sel.actionType = type;
                m_sel.captureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        char lbT1[64], lbT2[64], lbT3[64];
        snprintf(lbT1, sizeof(lbT1), "%s##t1", tr("action.type_macro"));
        snprintf(lbT2, sizeof(lbT2), "%s##t2", tr("action.type_keyboard"));
        snprintf(lbT3, sizeof(lbT3), "%s##t3", tr("action.type_mouse"));
        typeBtn("Xbox##t0", ActionType::Xbox);    ImGui::SameLine();
        typeBtn(lbT1,       ActionType::Macro);   ImGui::SameLine();
        typeBtn(lbT2,       ActionType::Keyboard); ImGui::SameLine();
        typeBtn(lbT3,       ActionType::Mouse);

        ImGui::Spacing();

        if (m_sel.actionType == ActionType::Macro) {
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
                m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear();
            }

        } else if (m_sel.actionType == ActionType::Keyboard) {
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) {
                m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            } else if (ActionPanel::renderKeyboardCapture("kb_h5", m_sel.captureKeys, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::Keyboard; act.physical = physShortSel;
                    for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                    m_model.h5ActionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_h5", mbResult, availW)) {
                if (!physShortSel.empty()) {
                    ButtonAction act;
                    act.type = ButtonActionType::MouseClick; act.physical = physShortSel; act.mouseButton = mbResult;
                    m_model.h5ActionEdits[physShortSel] = act;
                    m_model.buttonEdits.erase(physShortSel);
                }
                m_sel.physComp = -1; m_sel.stickAsButton = false; m_sel.dpadDir.clear();
                m_sel.actionType = ActionType::Xbox;
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
                float totalW = kBtnW * 6 + ImGui::GetStyle().ItemSpacing.x * 5;
                float offX = (availW4 - totalW) * 0.5f;
                if (offX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
                auto typeBtn4 = [&](const char* label, ActionType type) {
                    bool s = (m_sel.actionType == type);
                    if (s) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    if (ImGui::Button(label, { kBtnW, 0.0f })) { m_sel.actionType = type; m_sel.captureKeys.clear(); }
                    if (s) ImGui::PopStyleColor();
                };
                char lbA0[64], lbA1[64], lbA2[64], lbA3[64], lbA4[64];
                snprintf(lbA0, sizeof(lbA0), "%s##axt0", tr("action.type_gamepad"));
                snprintf(lbA1, sizeof(lbA1), "%s##axt1", tr("action.type_macro"));
                snprintf(lbA2, sizeof(lbA2), "%s##axt2", tr("action.type_keyboard"));
                snprintf(lbA3, sizeof(lbA3), "%s##axt3", tr("action.type_mouse"));
                snprintf(lbA4, sizeof(lbA4), "%s##axt4", tr("action.type_mousemove"));
                typeBtn4(lbA0, ActionType::Xbox);       ImGui::SameLine();
                typeBtn4(lbA1, ActionType::Macro);      ImGui::SameLine();
                typeBtn4(lbA2, ActionType::Keyboard);   ImGui::SameLine();
                typeBtn4(lbA3, ActionType::Mouse);      ImGui::SameLine();
                typeBtn4(lbA4, ActionType::MouseMove);  ImGui::SameLine();
                {
                    auto it4 = m_model.axisActionEdits.find(axisKey);
                    bool hasRanges4 = (it4 != m_model.axisActionEdits.end() &&
                                       it4->second.type == HalfAxisActionType::Ranges &&
                                       !it4->second.ranges.empty());
                    if (hasRanges4) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    if (ImGui::Button(trid("btn.ranges", "axt5").c_str(), { kBtnW, 0.0f })) {
                        std::vector<RangeEdit> cur;
                        if (hasRanges4)
                            for (const auto& tr : it4->second.ranges) {
                                RangeEdit re; re.from = tr.from; re.to = tr.to;
                                re.action = tr.action; re.hasAction = tr.hasAction;
                                cur.push_back(re);
                            }
                        m_trigRangeModal.open(axisKey, cur);
                        m_sel.actionType = ActionType::Xbox;
                        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
                    }
                    if (hasRanges4) ImGui::PopStyleColor();
                }

                ImGui::Spacing();

                if (m_sel.actionType == ActionType::Macro) {
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
                        m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear();
                    }
                } else if (m_sel.actionType == ActionType::Keyboard) {
                    bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
                    if (cancel) { m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear(); }
                    else if (ActionPanel::renderKeyboardCapture("kb_ax", m_sel.captureKeys, availW4)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::Keyboard;
                        for (const auto& p : m_sel.captureKeys) ha.keys.push_back(p.first);
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
                    }
                } else if (m_sel.actionType == ActionType::Mouse) {
                    std::string mbResult;
                    if (ActionPanel::renderMouseButtons("mb_ax", mbResult, availW4)) {
                        HalfAxisAction ha;
                        ha.type = HalfAxisActionType::MouseClick; ha.mouseButton = mbResult;
                        m_model.axisActionEdits[axisKey] = ha;
                        m_sel.physComp = -1; m_sel.stickDir.clear();
                        m_sel.actionType = ActionType::Xbox;
                    }
                } else if (m_sel.actionType == ActionType::MouseMove) {
                    float panelW = 280.0f;
                    float offX2 = (availW4 - panelW) * 0.5f;
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);
                    ImGui::TextDisabled("%s", tr("mapper.axis_hint"));
                    if (offX2 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX2);
                    ImGui::SetNextItemWidth(100.0f);
                    ImGui::SliderFloat(trid("mapper.mouse_speed", "axspd").c_str(), &m_sel.axisMouseSpeed, 1.0f, 50.0f, "%.0f");
                    ImGui::SameLine();
                    const char* mouseAxes[] = { "X", "Y" };
                    int axIdx = (m_sel.axisMouseAxis == "mouse_y") ? 1 : 0;
                    ImGui::SetNextItemWidth(60.0f);
                    if (ImGui::Combo(trid("mapper.mouse_axis", "axax").c_str(), &axIdx, mouseAxes, 2))
                        m_sel.axisMouseAxis = (axIdx == 1) ? "mouse_y" : "mouse_x";
                    ImGui::SameLine();
                    if (ImGui::Button(trid("btn.assign", "axmov").c_str())) {
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
                }
                // Mando mode: user clicks virtual pad → onVirtHitAxisAction

                // Clear button if already assigned
                if (m_model.axisActionEdits.count(axisKey)) {
                    ImGui::Spacing();
                    float clearW = 100.0f;
                    float offX3 = (availW4 - clearW) * 0.5f;
                    if (offX3 > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX3);
                    if (ImGui::Button(trid("btn.clear", "axclr").c_str(), { clearW, 0.0f })) {
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
        auto typeBtn7 = [&](const char* label, ActionType type) {
            bool sel = (m_sel.actionType == type);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, { btnW, 0.0f })) {
                m_sel.actionType = type; m_sel.captureKeys.clear();
            }
            if (sel) ImGui::PopStyleColor();
        };
        char lbH1[64], lbH2[64], lbH3[64];
        snprintf(lbH1, sizeof(lbH1), "%s##h7t1", tr("action.type_macro"));
        snprintf(lbH2, sizeof(lbH2), "%s##h7t2", tr("action.type_keyboard"));
        snprintf(lbH3, sizeof(lbH3), "%s##h7t3", tr("action.type_mouse"));
        typeBtn7("Xbox/Anal.##h7t0", ActionType::Xbox);    ImGui::SameLine();
        typeBtn7(lbH1,               ActionType::Macro);   ImGui::SameLine();
        typeBtn7(lbH2,               ActionType::Keyboard); ImGui::SameLine();
        typeBtn7(lbH3,               ActionType::Mouse);   ImGui::SameLine();
        {
            const std::vector<RangeEdit>& curRanges = (m_sel.triggerSrc == "l2") ? m_model.trigLRangeEdits : m_model.trigRRangeEdits;
            bool hasRanges = !curRanges.empty();
            if (hasRanges) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(trid("btn.ranges", "h7t4").c_str(), { btnW, 0.0f })) {
                m_trigRangeModal.open(m_sel.triggerSrc, curRanges);
                m_sel.actionType = ActionType::Xbox;
                m_sel.captureKeys.clear(); m_sel.macroSel.clear();
            }
            if (hasRanges) ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        if (m_sel.actionType == ActionType::Macro) {
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
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.macroSel.clear();
            }

        } else if (m_sel.actionType == ActionType::Keyboard) {
            bool cancel = (physNow.btnLB && physNow.btnRB) || (physNow.btnA && physNow.btnB);
            if (cancel) { m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear(); }
            else if (ActionPanel::renderKeyboardCapture("kb_h7", m_sel.captureKeys, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::Keyboard; act.physical = m_sel.triggerSrc;
                for (const auto& p : m_sel.captureKeys) act.keys.push_back(p.first);
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox; m_sel.captureKeys.clear();
            }

        } else if (m_sel.actionType == ActionType::Mouse) {
            std::string mbResult;
            if (ActionPanel::renderMouseButtons("mb_h7", mbResult, availW)) {
                ButtonAction act;
                act.type = ButtonActionType::MouseClick; act.physical = m_sel.triggerSrc; act.mouseButton = mbResult;
                m_model.trigActionEdits[m_sel.triggerSrc] = act;
                m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
            }
        }
    } // H7 block

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
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
    }

    // ── Gestión de clicks ─────────────────────────────────────────────────────
    if (mouseClicked)
        handleClick(phys, virt, mouse);

    // ── Guardar / Cancelar ────────────────────────────────────────────────────
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
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
    }
}

// ---------------------------------------------------------------------------
void MappingEditor::onPhysButtonHit(PadView& phys, int physHit) {
    const std::string& hitState = phys.getLayout().components[physHit].state;
    if (hitState == "triggerL" || hitState == "triggerR") {
        std::string trigSrc = (hitState == "triggerL") ? "l2" : "r2";
        if (m_sel.triggerSrc == trigSrc) {
            m_sel.triggerSrc.clear(); m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear();
        } else {
            m_sel.triggerSrc = trigSrc; m_sel.physComp = -1;
            m_sel.actionType = ActionType::Xbox;
            m_sel.captureKeys.clear(); m_sel.macroSel.clear();
        }
    } else if (physHit == m_sel.physComp) {
        m_sel.physComp = -1; m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
    } else {
        m_sel.physComp = physHit; m_sel.triggerSrc.clear();
        m_sel.actionType = ActionType::Xbox;
        m_sel.captureKeys.clear(); m_sel.macroSel.clear();
    }
}

// ---------------------------------------------------------------------------
void MappingEditor::onPhysStickHit(int physHit) {
    if (physHit == m_sel.physComp && m_sel.stickAsButton) {
        m_sel.physComp = -1; m_sel.stickAsButton = false;
    } else {
        m_sel.physComp = physHit; m_sel.stickAsButton = true; m_sel.stickDir.clear();
        m_sel.actionType = ActionType::Xbox;
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
        m_sel.actionType = ActionType::Xbox;
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
            m_model.h5ActionEdits.erase(source);
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
