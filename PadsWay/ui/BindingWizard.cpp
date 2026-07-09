#define NOMINMAX
#include "BindingWizard.h"
#include "../config/Strings.h"
#include "../config/ConfigLoader.h"
#include "../input/ControllerConfig.h"
#include "../Log.h"
#include "../imgui/imgui.h"
#include "../nlohmann/json.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>
using json = nlohmann::json;

// ── HID axis names by RawHIDState field index ────────────────────────────────
static const char* kHIDAxisNames[] = {
    "hid_x", "hid_y", "hid_z", "hid_rx", "hid_ry", "hid_rz",
    "hid_brake", "hid_accel"
};
static float kHIDAxisValues(const RawHIDState& s, int i) {
    switch (i) {
    case 0: return s.axisX;
    case 1: return s.axisY;
    case 2: return s.axisZ;
    case 3: return s.axisRx;
    case 4: return s.axisRy;
    case 5: return s.axisRz;
    case 6: return s.axisBrake;
    case 7: return s.axisAccel;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Init / unload
// ---------------------------------------------------------------------------

void BindingWizard::init(ID3D11Device* device,
                         const std::string& controllersPath,
                         const std::string& stateMapPath) {
    m_device          = device;
    m_controllersPath = controllersPath;
    m_stateMapPath    = stateMapPath;
    m_canvasView.load(device);
    loadStateMap();
    loadArrows();
}

void BindingWizard::unload() {
    closeReader();
    m_canvasView.unload();
    m_arrowLeft.release();
    m_arrowRight.release();
    m_arrowUp.release();
    m_arrowDown.release();
}

void BindingWizard::loadArrows() {
    PadView::loadPng(m_device, "images/decorations/ArrowLeft.png",  m_arrowLeft);
    PadView::loadPng(m_device, "images/decorations/ArrowRight.png", m_arrowRight);
    PadView::loadPng(m_device, "images/decorations/ArrowUp.png",    m_arrowUp);
    PadView::loadPng(m_device, "images/decorations/ArrowDown.png",  m_arrowDown);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void BindingWizard::start(const PadLayout& layout) {
    m_layout      = layout;
    m_state       = State::SelectController;
    m_selectedCtrl = -1;
    m_currentStep  = 0;
    m_noStateCount = 0;
    m_boundButtons.clear();
    m_boundAxes.clear();
    m_overlayLabels.clear();
    m_hasDpad  = false;
    m_dpadType.clear();
    m_saveWithConnection = false;
    memset(m_nameBuf, 0, sizeof(m_nameBuf));

    m_gyroPhase    = GyroPhase::Baseline;
    m_hasGyroStep  = false;
    m_gyroSamples.clear();
    m_gyroPhaseFrames = 0;
    m_gyroResult   = ImuCalibrationResult{};

    m_canvasView.forceSetLayout(m_layout);
    scanControllers();
}

// ---------------------------------------------------------------------------
// Main render dispatcher
// ---------------------------------------------------------------------------

void BindingWizard::render() {
    switch (m_state) {
    case State::SelectController: renderSelectController(); break;
    case State::NameController:   renderNameController();   break;
    case State::WarnNoState:      renderWarnNoState();      break;
    case State::Binding:          renderBinding();          break;
    case State::Review:           renderReview();           break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Step 1 — Select controller
// ---------------------------------------------------------------------------

void BindingWizard::renderSelectController() {
    ImGui::SeparatorText(tr("wizard.step_select_title"));
    ImGui::Spacing();

    if (ImGui::Button(tr("btn.refresh"))) scanControllers();
    ImGui::Spacing();

    if (m_controllers.empty()) {
        ImGui::TextDisabled("%s", tr("wizard.no_ctrl"));
    } else {
        ImGui::BeginChild("##ctrlList", { 0.0f, 180.0f }, true);
        for (int i = 0; i < (int)m_controllers.size(); ++i) {
            const auto& c = m_controllers[i];
            char transport[16];
            if      (c.connectionType == "bt")  snprintf(transport, sizeof(transport), "HID/BT");
            else if (c.connectionType == "usb") snprintf(transport, sizeof(transport), "HID/USB");
            else                                snprintf(transport, sizeof(transport), "HID");
            char label[256];
            snprintf(label, sizeof(label), "%s  [%04X:%04X]  (%s)##ctrl%d",
                     c.name.c_str(), c.vid, c.pid, transport, i);
            if (ImGui::Selectable(label, m_selectedCtrl == i))
                m_selectedCtrl = i;
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    bool canContinue = (m_selectedCtrl >= 0 && m_selectedCtrl < (int)m_controllers.size());
    if (!canContinue) ImGui::BeginDisabled();
    if (ImGui::Button(trid("btn.continue", "selCtrl").c_str(), { 140.0f, 0.0f })) {
        const auto& c = m_controllers[m_selectedCtrl];
        strncpy_s(m_nameBuf, c.name.c_str(), sizeof(m_nameBuf) - 1);
        m_state = State::NameController;
    }
    if (!canContinue) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(trid("btn.cancel", "selCtrl").c_str(), { 100.0f, 0.0f })) cancel();
}

// ---------------------------------------------------------------------------
// Step 2 — Name + mode
// ---------------------------------------------------------------------------

void BindingWizard::renderNameController() {
    ImGui::SeparatorText(tr("wizard.step_name_title"));
    ImGui::Spacing();

    const auto& c = m_controllers[m_selectedCtrl];

    ImGui::Text("VID:%04X  PID:%04X", c.vid, c.pid);
    if (!c.productName.empty())
        ImGui::Text(tr("wizard.hid_name"), c.productName.c_str());
    {
        const char* conn = c.connectionType == "bt"  ? tr("wizard.conn_bt") :
                           c.connectionType == "usb" ? tr("wizard.conn_usb") : tr("wizard.conn_unknown");
        ImGui::Text(tr("wizard.connection"), conn);
        ImGui::Checkbox(tr("wizard.specific_mapping"), &m_saveWithConnection);
        if (m_saveWithConnection)
            ImGui::TextDisabled(tr("wizard.mapping_specific"), conn);
        else
            ImGui::TextDisabled("%s", tr("wizard.mapping_generic"));
    }
    ImGui::Spacing();
    ImGui::Text("%s", tr("wizard.display_name"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##cname", m_nameBuf, sizeof(m_nameBuf));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(trid("btn.back", "name").c_str(), { 100.0f, 0.0f })) m_state = State::SelectController;
    ImGui::SameLine();
    bool canContinue = (m_nameBuf[0] != '\0');
    if (!canContinue) ImGui::BeginDisabled();
    if (ImGui::Button(trid("btn.continue", "name").c_str(), { 140.0f, 0.0f })) {
        buildSteps();
        if (m_noStateCount > 0)
            m_state = State::WarnNoState;
        else {
            m_state = State::Binding;
            openReader();
            beginStep();
        }
    }
    if (!canContinue) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(trid("btn.cancel", "name").c_str(), { 100.0f, 0.0f })) cancel();
}

// ---------------------------------------------------------------------------
// Step 3 — Warn no-state components
// ---------------------------------------------------------------------------

void BindingWizard::renderWarnNoState() {
    ImGui::SeparatorText(tr("wizard.step_warn_title"));
    ImGui::Spacing();
    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f },
        tr("wizard.warn_count"),
        m_noStateCount);
    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr("wizard.warn_hint"));
    ImGui::Spacing();

    if (ImGui::Button(trid("btn.continue", "warn").c_str(), { 140.0f, 0.0f })) {
        m_state = State::Binding;
        openReader();
        beginStep();
    }
    ImGui::SameLine();
    if (ImGui::Button(trid("btn.cancel", "warn").c_str(), { 100.0f, 0.0f })) cancel();
}

// ---------------------------------------------------------------------------
// Step 4 — Binding loop
// ---------------------------------------------------------------------------

void BindingWizard::renderBinding() {
    // ── Layout: canvas sized to pad width, controls panel right next to it ──
    float rightW  = 350.0f;
    float avail   = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float canvasW = m_layout.W;
    if (canvasW + spacing + rightW > avail)
        canvasW = avail - spacing - rightW;

    // Canvas
    ImGui::BeginChild("##wizCanvas", { canvasW, 0.0f }, false);
    int highlightComp = (m_currentStep < (int)m_steps.size())
                      ? m_steps[m_currentStep].compIndex : -1;
    renderCanvas(highlightComp);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel
    ImGui::BeginChild("##wizRight", { rightW, 0.0f }, true);

    // Progress
    ImGui::SeparatorText(tr("wizard.step_bind_title"));
    ImGui::Text(tr("wizard.step"), m_currentStep + 1, (int)m_steps.size());
    ImGui::Spacing();

    if (m_currentStep < (int)m_steps.size()) {
        const BindStep& step = m_steps[m_currentStep];
        const std::string& t = step.mapping.type;

        // Component label
        if (step.compIndex >= 0 && step.compIndex < (int)m_layout.components.size())
            ImGui::Text(tr("wizard.component"), m_layout.components[step.compIndex].id.c_str());

        ImGui::Spacing();

        // Prompt — yellow, larger font
        {
            const char* promptText = "";
            char promptBuf[128];
            if (t == "button" || t == "physical_only") {
                const char* id = (step.compIndex >= 0)
                    ? m_layout.components[step.compIndex].id.c_str()
                    : step.state.c_str();
                snprintf(promptBuf, sizeof(promptBuf), tr("wizard.press_button"), id);
                promptText = promptBuf;
            } else if (t == "axis" || t == "trigger") {
                // analog_dpad steps use axis detection but show dpad-specific prompts
                if (step.compIndex >= 0 &&
                    step.compIndex < (int)m_layout.components.size() &&
                    m_layout.components[step.compIndex].type == "analog_dpad") {
                    bool isY = (step.mapping.axis_target.find("_y") != std::string::npos);
                    promptText = isY ? tr("wizard.press_dpad_down") : tr("wizard.press_dpad_right");
                } else {
                    promptText = step.mapping.prompt.c_str();
                }
            } else if (t == "dpad") {
                promptText = tr("wizard.press_dpad");
            } else if (t == "gyro") {
                switch (m_gyroPhase) {
                case GyroPhase::Baseline: promptText = tr("wizard.gyro_baseline"); break;
                case GyroPhase::Roll:     promptText = tr("wizard.gyro_roll");     break;
                case GyroPhase::Pitch:    promptText = tr("wizard.gyro_pitch");    break;
                case GyroPhase::Yaw:      promptText = tr("wizard.gyro_yaw");      break;
                }
            }

            if (t == "gyro") {
                ImGui::Text(tr("wizard.gyro_phase"), static_cast<int>(m_gyroPhase) + 1);
                ImGui::Spacing();
            }

            ImGui::SetWindowFontScale(1.35f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s", promptText);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Capture input ────────────────────────────────────────────────────
        if (t == "button" || t == "physical_only") {
            int idx = 0;
            if (captureButton(idx)) commitButton(idx);
        } else if (t == "axis" || t == "trigger") {
            std::string src; bool inv = false;
            if (captureAxis(src, inv, step.mapping.invert_if_positive))
                commitAxis(src, inv);
        } else if (t == "dpad") {
            std::string dt;
            if (captureDpad(dt)) commitDpad(dt);
        } else if (t == "gyro") {
            if (!m_gyroPhaseStarted) {
                ImGui::TextWrapped("%s", tr("wizard.gyro_start"));
                int dummyIdx = 0;
                if (captureButton(dummyIdx)) m_gyroPhaseStarted = true;
            } else if (m_gyroPhase == GyroPhase::Baseline) {
                // Baseline has no gesture to repeat — it just needs the controller quiet for a
                // fixed time, so it keeps the original time-based gate.
                sampleGyroFrame();
                bool canAdvance = m_gyroPhaseFrames >= kGyroMinCaptureFrames;
                int  pct = std::min(100, (m_gyroPhaseFrames * 100) / kGyroMinCaptureFrames);
                ImGui::Text(tr("wizard.gyro_capturing"), pct);
                if (canAdvance) ImGui::TextDisabled("%s", tr("wizard.gyro_ready_hint"));
                ImGui::Spacing();
                if (renderGyroAdvanceControl(canAdvance)) commitGyroPhase();
            } else {
                // Roll/Pitch/Yaw: instead of a fixed capture time, keep going until one offset's
                // amplitude clearly separates from the runner-up — see gyroConfidence().
                sampleGyroFrame();
                float confidence = gyroConfidence();
                ImGui::Text(tr("wizard.gyro_confidence"), static_cast<int>(confidence * 100.0f));
                ImGui::Spacing();
                if (confidence >= kGyroConfidenceThreshold) {
                    // Move on the instant we're confident — no reason to wait for a click once
                    // the leading candidate has clearly separated from the rest.
                    commitGyroPhase();
                } else if (renderGyroAdvanceControl(m_gyroPhaseFrames >= kGyroMinPhaseFrames)) {
                    // Manual escape hatch: once there's at least some data, let the user force
                    // past a phase whose confidence is stalling instead of climbing.
                    commitGyroPhase();
                }
            }
        }

        // ── Manual controls ──────────────────────────────────────────────────
        bool atVeryStart = (m_currentStep == 0 && !(t == "gyro" && m_gyroPhase != GyroPhase::Baseline));
        if (atVeryStart) ImGui::BeginDisabled();
        if (ImGui::Button(trid("btn.back", "bind").c_str(), { 90.0f, 0.0f })) {
            if (t == "gyro" && m_gyroPhase != GyroPhase::Baseline) {
                m_gyroPhase = static_cast<GyroPhase>(static_cast<int>(m_gyroPhase) - 1);
                resetGyroPhaseSamples(m_gyroPhase);
                m_gyroPhaseStarted = false;
                snapshotBaseline(); // resync m_prevButtonMask so a repeated start-button press is detected as new
            } else {
                goBack();
            }
        }
        if (atVeryStart) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(trid("btn.skip", "bind").c_str(), { 80.0f, 0.0f })) skipStep();
        ImGui::SameLine();
        if (ImGui::Button(trid("btn.cancel", "bind").c_str(), { 90.0f, 0.0f })) cancel();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Step 5 — Review
// ---------------------------------------------------------------------------

void BindingWizard::renderReview() {
    float rightW  = 350.0f;
    float avail   = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float canvasW = m_layout.W;
    if (canvasW + spacing + rightW > avail)
        canvasW = avail - spacing - rightW;

    ImGui::BeginChild("##revCanvas", { canvasW, 0.0f }, false);
    renderCanvas(-1); // no highlight, all overlays visible
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##revRight", { rightW, 0.0f }, true);
    ImGui::SeparatorText(tr("wizard.step_review_title"));

    ImGui::Text(tr("wizard.review_buttons"), (int)m_boundButtons.size());
    ImGui::Text(tr("wizard.review_axes"), (int)m_boundAxes.size());
    if (m_hasDpad) ImGui::Text(tr("wizard.review_dpad"), m_dpadType.c_str());
    else           ImGui::TextDisabled("%s", tr("wizard.review_dpad_none"));

    if (m_hasGyroStep) {
        if (m_gyroResult.ok) {
            const char* axes = m_gyroResult.accelXOffset >= 0 ? tr("wizard.gyro_result_full")
                                                                : tr("wizard.gyro_result_gyro_only");
            ImGui::Text(tr("wizard.review_gyro_ok"), axes);
        } else {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, "%s", tr("wizard.review_gyro_failed"));
        }
    }

    ImGui::Spacing();
    ImGui::BeginChild("##revList", { 0.0f, 180.0f }, true);
    for (const auto& b : m_boundButtons) {
        if (b.physicalOnly)
            ImGui::Text(tr("wizard.review_visual"), b.physIndex, b.physical.c_str());
        else
            ImGui::Text(tr("wizard.review_btn"), b.physIndex, b.physical.c_str());
    }
    for (const auto& a : m_boundAxes) {
        ImGui::Text(tr("wizard.review_axis"), a.source.c_str(), a.target.c_str(), a.invert ? tr("wizard.review_inverted") : "");
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(trid("btn.save", "rev").c_str(), { 120.0f, 0.0f })) saveResult();
    ImGui::SameLine();
    if (ImGui::Button(trid("btn.repeat", "rev").c_str(), { 100.0f, 0.0f })) {
        closeReader();
        m_boundButtons.clear();
        m_boundAxes.clear();
        m_overlayLabels.clear();
        m_hasDpad    = false;
        m_dpadType.clear();
        m_currentStep  = 0;
        m_stepCooldown = 0;
        buildSteps();
        openReader();
        beginStep();
        m_state = State::Binding;
    }
    ImGui::SameLine();
    if (ImGui::Button(trid("btn.cancel", "rev").c_str(), { 100.0f, 0.0f })) cancel();

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Canvas render with overlays
// ---------------------------------------------------------------------------

void BindingWizard::renderCanvas(int highlightComp) {
    bool inBinding = (m_state == State::Binding);

    // In binding mode: show current component as pressed (active color), no yellow box.
    // In review mode: all inactive, no highlight.
    GamepadState displayState = inBinding ? buildFakeState() : GamepadState{};
    m_canvasOrigin = ImGui::GetCursorScreenPos();
    m_canvasView.render(displayState, inBinding ? -1 : highlightComp);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw number/axis label pills (always)
    for (const auto& [compIdx, label] : m_overlayLabels) {
        if (compIdx < 0 || compIdx >= (int)m_layout.components.size()) continue;
        const auto& comp = m_layout.components[compIdx];
        ImVec2 pos = { m_canvasOrigin.x + comp.cx, m_canvasOrigin.y + comp.cy };

        ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        float pad = 3.0f;
        ImVec2 tl = { pos.x - textSize.x * 0.5f - pad, pos.y - textSize.y * 0.5f - pad };
        ImVec2 br = { pos.x + textSize.x * 0.5f + pad, pos.y + textSize.y * 0.5f + pad };
        dl->AddRectFilled(tl, br, IM_COL32(20, 20, 20, 200), 4.0f);
        dl->AddText({ tl.x + pad, tl.y + pad }, IM_COL32(255, 220, 60, 255), label.c_str());
    }

    // Draw directional arrows for the current axis step
    if (inBinding && m_currentStep >= 0 && m_currentStep < (int)m_steps.size()) {
        const BindStep& step = m_steps[m_currentStep];
        const std::string& type = step.mapping.type;
        if ((type == "axis" || type == "trigger") && step.compIndex >= 0 &&
            step.compIndex < (int)m_layout.components.size()) {

            const PadComponent& comp = m_layout.components[step.compIndex];
            ImVec2 center = { m_canvasOrigin.x + comp.cx, m_canvasOrigin.y + comp.cy };

            // Component radius: use size for sticks, otherwise half of w/h
            float radius = comp.size > 0.0f ? comp.size * 0.5f
                         : (comp.w > comp.h ? comp.w : comp.h) * 0.5f;
            if (radius < 8.0f) radius = 8.0f;

            const std::string& target = step.mapping.axis_target;
            bool isTrigger    = (target.find("trigger") != std::string::npos);
            bool isHorizontal = (target.find("_x") != std::string::npos ||
                                 target == "mouse_x");

            constexpr float kArrowSize = 28.0f;
            constexpr float kGap       = 10.0f;
            float offset = radius + kGap;

            auto drawArrow = [&](const PadTexture& tex, ImVec2 topLeft) {
                if (!tex.valid()) return;
                ImVec2 br = { topLeft.x + kArrowSize, topLeft.y + kArrowSize };
                dl->AddImage((ImTextureID)(intptr_t)tex.srv, topLeft, br,
                             {0,0}, {1,1}, IM_COL32(255,255,255,220));
            };

            if (isTrigger) {
                // No arrow for triggers
            } else if (isHorizontal) {
                drawArrow(m_arrowRight, { center.x + offset,
                                          center.y - kArrowSize * 0.5f });
            } else {
                drawArrow(m_arrowDown, { center.x - kArrowSize * 0.5f,
                                         center.y + offset });
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Wizard logic
// ---------------------------------------------------------------------------

void BindingWizard::scanControllers() {
    m_controllers.clear();

    // Load existing configs so we can pre-fill source_name as the display name
    std::vector<ControllerConfig> existingConfigs;
    try {
        std::ifstream f(m_controllersPath);
        if (f.is_open()) {
            json root = json::parse(f);
            if (root.contains("controllers")) {
                for (const auto& c : root["controllers"]) {
                    ControllerConfig cfg;
                    cfg.vid          = static_cast<uint16_t>(std::stoul(c.at("vid").get<std::string>(), nullptr, 16));
                    cfg.pid          = static_cast<uint16_t>(std::stoul(c.at("pid").get<std::string>(), nullptr, 16));
                    cfg.source_name  = c.value("source_name", "");
                    cfg.mode         = c.value("mode", "");
                    cfg.connection   = c.value("connection", "");
                    existingConfigs.push_back(std::move(cfg));
                }
            }
        }
    } catch (...) {}

    // HID scan — all physical controllers use HID
    for (const auto& h : HIDScanner::scan()) {
        if (h.vid == 0x5650 && h.pid == 0x0001) continue;  // skip ViGEm
        const ControllerConfig* existing = findConfig(existingConfigs, h.vid, h.pid,
                                                       h.connectionType);
        DetectedController c;
        c.vid            = h.vid;
        c.pid            = h.pid;
        c.productName    = h.productName;
        c.connectionType = h.connectionType;
        c.path           = h.path;
        // Hardware name takes priority — the config source_name may belong to a different
        // model that shares VID/PID (e.g. Pro 2 config showing for a Zero 2 device).
        c.name = !h.productName.empty() ? h.productName
               : (existing && !existing->source_name.empty()) ? existing->source_name
               : "HID device";
        m_controllers.push_back(std::move(c));
    }
}

void BindingWizard::loadStateMap() {
    m_stateMap.clear();
    std::ifstream f(m_stateMapPath);
    if (!f.is_open()) return;
    try {
        json j = json::parse(f);
        for (auto& [key, val] : j["state_map"].items()) {
            StateMapEntry e;
            e.type    = val.value("type",    "");
            e.physical= val.value("physical","");
            e.axis_target     = val.value("axis_target",     "");
            e.prompt          = val.value("prompt",          "");
            e.invert_if_positive = val.value("invert_if_positive", false);
            e.direction       = val.value("direction",       "");
            m_stateMap[key]   = e;
        }
    } catch (...) {}
}

void BindingWizard::buildSteps() {
    m_steps.clear();
    m_noStateCount = 0;
    m_hasGyroStep  = false;
    bool dpadAdded = false;

    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const auto& comp = m_layout.components[i];
        if (comp.type == "template" || comp.type == "decoration") continue;

        // Gyro/IMU: one step that internally runs a 4-phase calibration sub-machine
        // (Baseline/Roll/Pitch/Yaw). No state_map entry — it doesn't bind to a virtual
        // action, it calibrates raw sensor byte offsets for ImuConfig.
        if (comp.type == "gyro") {
            BindStep s;
            s.compIndex   = i;
            s.state       = "gyro";
            s.mapping.type = "gyro";
            m_steps.push_back(s);
            m_hasGyroStep = true;
            continue;
        }

        // Stick: add click button (L3/R3); axes are added separately below.
        if (comp.type == "stick") {
            if (!comp.stateClick.empty()) {
                auto it = m_stateMap.find(comp.stateClick);
                if (it != m_stateMap.end()) {
                    BindStep s;
                    s.compIndex = i;
                    s.state     = comp.stateClick;
                    s.mapping   = it->second;
                    m_steps.push_back(s);
                } else {
                    ++m_noStateCount;
                }
            }
            continue;
        }

        // Touchpad: one step for the click button (surface data is fixed, nothing to calibrate).
        if (comp.type == "touchpad") {
            const std::string clickState = comp.state.empty() ? "btnTouch" : comp.state;
            auto it = m_stateMap.find(clickState);
            if (it != m_stateMap.end()) {
                BindStep s;
                s.compIndex = i;
                s.state     = clickState;
                s.mapping   = it->second;
                m_steps.push_back(s);
            } else {
                ++m_noStateCount;
            }
            continue;
        }

        // Analog dpad: reads two float axes (Y first = press DOWN, X second = press RIGHT).
        if (comp.type == "analog_dpad") {
            if (!comp.stateY.empty()) {
                auto it = m_stateMap.find(comp.stateY);
                if (it != m_stateMap.end()) {
                    BindStep s; s.compIndex = i; s.state = comp.stateY; s.mapping = it->second;
                    m_steps.push_back(s);
                } else { ++m_noStateCount; }
            }
            if (!comp.stateX.empty()) {
                auto it = m_stateMap.find(comp.stateX);
                if (it != m_stateMap.end()) {
                    BindStep s; s.compIndex = i; s.state = comp.stateX; s.mapping = it->second;
                    m_steps.push_back(s);
                } else { ++m_noStateCount; }
            }
            continue;
        }

        // Dpad: compound component (stateUp/Down/Left/Right, no state field) → one step.
        if (comp.type == "dpad") {
            if (!dpadAdded) {
                const std::string dirState =
                    !comp.stateUp.empty()    ? comp.stateUp    :
                    !comp.stateDown.empty()  ? comp.stateDown  :
                    !comp.stateLeft.empty()  ? comp.stateLeft  :
                                               comp.stateRight;
                if (!dirState.empty()) {
                    auto it = m_stateMap.find(dirState);
                    if (it != m_stateMap.end()) {
                        BindStep s;
                        s.compIndex = i;
                        s.state     = dirState;
                        s.mapping   = it->second;
                        m_steps.push_back(s);
                        dpadAdded = true;
                    } else { ++m_noStateCount; }
                }
            }
            continue;
        }

        // Regular buttons / triggers
        if (comp.state.empty()) continue;

        auto it = m_stateMap.find(comp.state);
        if (it == m_stateMap.end()) { ++m_noStateCount; continue; }

        const StateMapEntry& mapping = it->second;

        // Legacy: individual dpad button components with state="dpadUp" etc.
        if (mapping.type == "dpad") {
            if (!dpadAdded) {
                BindStep s;
                s.compIndex = i;
                s.state     = comp.state;
                s.mapping   = mapping;
                m_steps.push_back(s);
                dpadAdded = true;
            }
            continue;
        }

        BindStep s;
        s.compIndex = i;
        s.state     = comp.state;
        s.mapping   = mapping;
        m_steps.push_back(s);
    }

    // Add axis steps for any stick components
    // We look for unique axis states (leftX, leftY, rightX, rightY)
    std::vector<std::string> axisStates = { "leftX", "leftY", "rightX", "rightY" };
    for (const auto& axState : axisStates) {
        // Check if any stick component binds this axis
        bool found = false;
        int  stickCompIdx = -1;
        for (int i = 0; i < (int)m_layout.components.size(); ++i) {
            const auto& comp = m_layout.components[i];
            if (comp.type != "stick") continue;
            // A stick component covers both X and Y of its stick
            // state_x/state_y are in the PadLayout component
            if ((axState == "leftX"  && comp.stateX == "leftX")  ||
                (axState == "leftY"  && comp.stateX == "leftX")   || // same stick
                (axState == "rightX" && comp.stateX == "rightX") ||
                (axState == "rightY" && comp.stateX == "rightX")) {
                found = true; stickCompIdx = i; break;
            }
        }
        if (!found) continue;

        auto it = m_stateMap.find(axState);
        if (it == m_stateMap.end()) { ++m_noStateCount; continue; }

        BindStep s;
        s.compIndex = stickCompIdx;
        s.state     = axState;
        s.mapping   = it->second;
        m_steps.push_back(s);
    }
}

void BindingWizard::beginStep() {
    if (m_currentStep >= (int)m_steps.size()) {
        closeReader();
        m_state = State::Review;
        return;
    }
    if (m_steps[m_currentStep].mapping.type == "gyro") {
        m_gyroPhase = GyroPhase::Baseline;
        m_gyroSamples.clear();
        m_gyroPhaseFrames = 0;
        m_gyroPhaseStarted = false;
        m_gyroResult = ImuCalibrationResult{};
        m_gyroAliveOffsets.clear();
        if (m_hidReader && m_hidReader->isOpen())
            m_hidReader->read(m_gyroAxisBaseline);
    }
    snapshotBaseline();
}

void BindingWizard::commitButton(int physIndex) {
    const BindStep& step = m_steps[m_currentStep];
    ButtonResult r;
    r.compIndex    = step.compIndex;
    r.physIndex    = physIndex;
    r.physical     = step.mapping.physical;
    r.physicalOnly = (step.mapping.type == "physical_only");
    m_boundButtons.push_back(r);

    if (step.compIndex >= 0)
        m_overlayLabels[step.compIndex] = std::to_string(physIndex);

    m_stepCooldown = kAxisCooldown;
    ++m_currentStep;
    beginStep();
}

void BindingWizard::commitAxis(const std::string& source, bool invert) {
    const BindStep& step = m_steps[m_currentStep];
    AxisResult r;
    r.source = source;
    r.target = step.mapping.axis_target;
    // Triggers always go from rest (min) to pressed (max) — invert is never correct.
    // applyAxes already remaps [-1,1] to [0,1] for trigger targets.
    if (r.target == "trigger_l" || r.target == "trigger_r")
        r.invert = false;
    else
        r.invert = invert;
    if (step.compIndex >= 0 &&
        step.compIndex < (int)m_layout.components.size() &&
        m_layout.components[step.compIndex].type == "analog_dpad")
        r.isAnalogDpad = true;
    m_boundAxes.push_back(r);

    if (step.compIndex >= 0)
        m_overlayLabels[step.compIndex] = source;

    m_stepCooldown = kAxisCooldown;
    ++m_currentStep;
    beginStep();
}

void BindingWizard::commitDpad(const std::string& dpadType) {
    m_hasDpad  = true;
    m_dpadType = dpadType;

    const BindStep& step = m_steps[m_currentStep];
    if (step.compIndex >= 0)
        m_overlayLabels[step.compIndex] = "dpad";

    m_stepCooldown = kAxisCooldown;
    ++m_currentStep;
    beginStep();
}

void BindingWizard::commitGyroPhase() {
    if (m_gyroPhase == GyroPhase::Yaw) {
        m_gyroResult = classifyGyro();
        const BindStep& step = m_steps[m_currentStep];
        if (step.compIndex >= 0)
            m_overlayLabels[step.compIndex] = m_gyroResult.ok ? "IMU" : "IMU?";
        m_stepCooldown = kAxisCooldown;
        ++m_currentStep;
        beginStep();
    } else {
        // Baseline just committed: freeze which offsets are real sensor channels before Roll
        // starts — gyroConfidence() and classifyGyro() both reuse this same set.
        if (m_gyroPhase == GyroPhase::Baseline) {
            m_gyroAliveOffsets = computeAliveOffsets();
            for (int o = 0; o < (int)m_gyroSamples.size(); ++o) {
                const GyroOffsetStats& base = m_gyroSamples[o][static_cast<int>(GyroPhase::Baseline)];
                float amp  = (base.count > 0) ? (base.maxV - base.minV) : 0.0f;
                float mean = (base.count > 0) ? (base.sum / base.count) : 0.0f;
                bool  alive = o < (int)m_gyroAliveOffsets.size() && m_gyroAliveOffsets[o];
                spdlog::trace("[GyroCal] offset={} alive={} baselineMean={:.1f} baselineAmp={:.1f}",
                              o, alive, mean, amp);
            }
        }
        m_gyroPhase = static_cast<GyroPhase>(static_cast<int>(m_gyroPhase) + 1);
        resetGyroPhaseSamples(m_gyroPhase);
        m_gyroPhaseStarted = false;
        snapshotBaseline(); // resync m_prevButtonMask so a repeated start-button press is detected as new
    }
}

void BindingWizard::skipStep() {
    ++m_currentStep;
    beginStep();
}

void BindingWizard::goBack() {
    if (m_currentStep == 0) return;
    --m_currentStep;

    // Remove overlay for this step if it was committed
    const BindStep& step = m_steps[m_currentStep];
    m_overlayLabels.erase(step.compIndex);

    // Remove from results
    const std::string& t = step.mapping.type;
    if (t == "button" || t == "physical_only") {
        auto it = std::find_if(m_boundButtons.begin(), m_boundButtons.end(),
            [&](const ButtonResult& b) { return b.compIndex == step.compIndex; });
        if (it != m_boundButtons.end()) m_boundButtons.erase(it);
    } else if (t == "axis" || t == "trigger") {
        if (!m_boundAxes.empty()) m_boundAxes.pop_back();
    } else if (t == "dpad") {
        m_hasDpad = false; m_dpadType.clear();
    } else if (t == "gyro") {
        // Re-entering a completed gyro step: start the 4-phase capture over from scratch.
        m_gyroPhase = GyroPhase::Baseline;
        m_gyroSamples.clear();
        m_gyroPhaseFrames = 0;
        m_gyroPhaseStarted = false;
        m_gyroResult = ImuCalibrationResult{};
        m_gyroAliveOffsets.clear();
    }

    snapshotBaseline();
}

void BindingWizard::cancel() {
    closeReader();
    m_state = State::Idle;
    m_boundButtons.clear();
    m_boundAxes.clear();
    m_overlayLabels.clear();
    m_hasDpad = false;
}

// ---------------------------------------------------------------------------
// Input capture
// ---------------------------------------------------------------------------

// Shared "Continuar" control for the gyro sub-phases: draws the button (disabled until
// canAdvance) and also accepts any controller button press as a confirm, so the user never
// has to let go of the controller to reach for the mouse. Returns true once confirmed.
bool BindingWizard::renderGyroAdvanceControl(bool canAdvance) {
    if (!canAdvance) ImGui::BeginDisabled();
    bool clicked = ImGui::Button(trid("wizard.gyro_continue", "bind").c_str(), { 180.0f, 0.0f });
    if (!canAdvance) ImGui::EndDisabled();

    // Always poll captureButton (not just once canAdvance) so a button already held during the
    // gesture doesn't read as a fresh press the instant the threshold flips.
    int dummyIdx = 0;
    bool pressedAnyButton = captureButton(dummyIdx);
    return clicked || (canAdvance && pressedAnyButton);
}

bool BindingWizard::captureButton(int& outIndex) {
    if (!m_hidReader || !m_hidReader->isOpen()) return false;
    RawHIDState s{};
    m_hidReader->read(s);
    DWORD mask = s.buttonMask;

    DWORD newBits = mask & ~m_prevButtonMask;
    m_prevButtonMask = mask;

    if (newBits == 0) return false;
    // Find lowest set bit → button index (1-based)
    for (int i = 0; i < 32; ++i) {
        if (newBits & (1u << i)) { outIndex = i + 1; return true; }
    }
    return false;
}

bool BindingWizard::captureAxis(std::string& outSource, bool& outInvert, bool invertIfPositive) {
    if (m_stepCooldown > 0) {
        --m_stepCooldown;
        if (m_stepCooldown == 0) snapshotBaseline(); // re-snapshot once movement settles
        return false;
    }
    // Skip axes already committed in previous steps to avoid bleed-over.
    // Exception: allow reuse when the current step is the paired trigger (trigger_combined).
    const BindStep& curStep = m_steps[m_currentStep];
    const bool curIsTrigger = (curStep.mapping.type == "trigger");
    const std::string& curTarget = curStep.mapping.axis_target;
    auto alreadyBound = [&](const std::string& name) -> bool {
        for (const auto& a : m_boundAxes) {
            if (a.source != name) continue;
            if (curIsTrigger &&
                (a.target == "trigger_l" || a.target == "trigger_r") &&
                (curTarget  == "trigger_l" || curTarget  == "trigger_r") &&
                a.target != curTarget)
                continue; // same axis, opposite trigger → trigger_combined, not a conflict
            return true;
        }
        return false;
    };

    if (m_hidReader && m_hidReader->isOpen()) {
        m_hidReader->read(m_axisLastRead); // on timeout keeps previous state (event-driven devices)
        const RawHIDState& cur = m_axisLastRead;
        float bestDelta = 0.0f;
        int   bestAxis  = -1;
        for (int i = 0; i < 8; ++i) {
            if (alreadyBound(kHIDAxisNames[i])) continue;
            float delta = std::abs(kHIDAxisValues(cur, i) - kHIDAxisValues(m_axisBaseline, i));
            if (delta > bestDelta) { bestDelta = delta; bestAxis = i; }
        }
        float deltaSigned = (bestAxis >= 0)
            ? kHIDAxisValues(cur, bestAxis) - kHIDAxisValues(m_axisBaseline, bestAxis)
            : 0.0f;

        if (bestDelta < kAxisNoiseFloor || bestAxis < 0) {
            // Below noise floor: drift or noise — reset confirmation state.
            m_axisConfirmCount = 0;
            m_axisConfirmBest  = -1;
            m_axisConfirmSum   = 0.0f;
            return false;
        }

        // Require the same axis to dominate for kAxisConfirm consecutive frames.
        // Prevents committing on fast swipes caught during the release phase or cross-axis drift.
        if (bestAxis != m_axisConfirmBest) {
            m_axisConfirmBest  = bestAxis;
            m_axisConfirmCount = 0;
            m_axisConfirmSum   = 0.0f;
        }
        m_axisConfirmSum += deltaSigned;
        ++m_axisConfirmCount;
        if (m_axisConfirmCount < kAxisConfirm || bestDelta < kAxisThreshold) return false;

        // Commit: derive direction from average signed delta over the confirmation window.
        float avgDelta = m_axisConfirmSum / m_axisConfirmCount;
        m_axisConfirmCount = 0;
        m_axisConfirmBest  = -1;
        m_axisConfirmSum   = 0.0f;

        outSource = kHIDAxisNames[bestAxis];
        outInvert = invertIfPositive ? (avgDelta > 0.0f) : (avgDelta < 0.0f);
        return true;
    }
    return false;
}

bool BindingWizard::captureDpad(std::string& outDpadType) {
    if (m_stepCooldown > 0) {
        --m_stepCooldown;
        if (m_stepCooldown == 0) snapshotBaseline();
        return false;
    }
    if (m_hidReader && m_hidReader->isOpen()) {
        RawHIDState s{};
        m_hidReader->read(s);
        if (s.hat != 0xFFFFFFFF) { outDpadType = "hid_hat"; return true; }
    }
    return false;
}

void BindingWizard::sampleGyroFrame() {
    if (!m_hidReader || !m_hidReader->isOpen()) return;
    RawHIDState s{};
    m_hidReader->read(s);
    if (!s.valid || s.raw.size() < 2) return;

    // Reject frames where a declared HID axis (stick/trigger) drifted from its Baseline-start
    // value — likely an accidental touch during the gesture, not the gyro/accel itself.
    for (int i = 0; i < 8; ++i) {
        if (std::abs(kHIDAxisValues(s, i) - kHIDAxisValues(m_gyroAxisBaseline, i)) > kGyroAxisContamination)
            return;
    }

    int n = static_cast<int>(s.raw.size()) - 1;
    if (static_cast<int>(m_gyroSamples.size()) != n) m_gyroSamples.assign(n, {});

    int phaseIdx = static_cast<int>(m_gyroPhase);
    std::vector<float> v(n);
    for (int o = 0; o < n; ++o) {
        int16_t raw = static_cast<int16_t>(
            static_cast<uint8_t>(s.raw[o]) | (static_cast<uint16_t>(s.raw[o + 1]) << 8));
        v[o] = static_cast<float>(raw);
        GyroOffsetStats& st = m_gyroSamples[o][phaseIdx];
        st.minV = (st.count == 0) ? v[o] : std::min(st.minV, v[o]);
        st.maxV = (st.count == 0) ? v[o] : std::max(st.maxV, v[o]);
        st.sum += v[o];
        ++st.count;
    }
    ++m_gyroPhaseFrames;

    // Throttled trace of the current best-vs-runner-up amplitude gap, so we can see confidence
    // building without flooding the log — one line every ~0.5s. See gyroConfidence() for what
    // actually drives the live progress the user sees.
    if (m_gyroPhase != GyroPhase::Baseline && (m_gyroPhaseFrames % 30 == 0)) {
        spdlog::trace("[GyroCal] phase={} confidence={:.0f}%", phaseIdx, gyroConfidence() * 100.0f);
    }
}

void BindingWizard::resetGyroPhaseSamples(GyroPhase phase) {
    int idx = static_cast<int>(phase);
    for (auto& offsetStats : m_gyroSamples) offsetStats[idx] = GyroOffsetStats{};
    m_gyroPhaseFrames = 0;
}

// An offset is a real sensor channel only if it stayed quiet (low peak-to-peak) during
// Baseline (mando quieto) — discards CRC/packet-counter bytes, which drift or jump even at
// rest. It also must show SOME jitter (amp > 0): a real sensor always has a little electrical
// noise, but a constant header/padding byte reads EXACTLY the same value every single frame
// (amp == 0 exactly) — which otherwise passes the "quiet" half of this test *more* cleanly than
// genuine sensor noise does. Without this, findRun() below picks up the first run of constant
// header bytes (e.g. offsets 0,2,4,6,8,10, all amp==0) instead of the real accel+gyro block,
// and classification fails silently (see BITACORA — first case caught 2026/07/10).
// Computed once when Baseline commits (see commitGyroPhase()) and reused unchanged through
// Roll/Pitch/Yaw, both for live confidence and for classifyGyro() at the end.
std::vector<bool> BindingWizard::computeAliveOffsets() const {
    const int n = static_cast<int>(m_gyroSamples.size());
    std::vector<bool> alive(n, false);
    for (int o = 0; o < n; ++o) {
        const GyroOffsetStats& base = m_gyroSamples[o][static_cast<int>(GyroPhase::Baseline)];
        if (base.count == 0) continue;
        float amp = base.maxV - base.minV;
        alive[o] = amp > 0.0f && amp < kGyroBaselineNoiseFloor;
    }
    return alive;
}

// Confidence (0..1) that a real gesture-sized signal has been captured this phase. Watches the
// SAME running (max - min) amplitude per offset that classifyGyro() uses at the end — no
// separate repetition tracking, so there's nothing to segment and nothing that gets less stable
// as more (noisy) motion comes in: the leading amplitude only ever grows.
//
// This does NOT compare the leader against the runner-up — two accelerometer axes legitimately
// react similarly to the same tilt (both measure gravity's changing projection), so demanding
// the winner beat every other offset by a wide margin is an unrealistic bar that real hardware
// won't clear. Telling gyro from accel (and which axis is which) is classifyGyro()'s job, via
// the cross-phase comparison at the end — this number only answers "is there a clean, sizeable
// signal yet," not "whose."
float BindingWizard::gyroConfidence() const {
    if (m_gyroPhaseFrames < kGyroMinPhaseFrames) return 0.0f;

    int phaseIdx = static_cast<int>(m_gyroPhase);
    float bestAmp = 0.0f;
    for (int o = 0; o < (int)m_gyroSamples.size(); ++o) {
        if (o >= (int)m_gyroAliveOffsets.size() || !m_gyroAliveOffsets[o]) continue;
        const GyroOffsetStats& st = m_gyroSamples[o][phaseIdx];
        if (st.count == 0) continue;
        bestAmp = std::max(bestAmp, st.maxV - st.minV);
    }

    float confidence = (bestAmp - kGyroMinSignalAmp) / (kGyroTargetSignalAmp - kGyroMinSignalAmp);
    return std::clamp(confidence, 0.0f, 0.99f);
}

BindingWizard::ImuCalibrationResult BindingWizard::classifyGyro() const {
    ImuCalibrationResult result;
    const int n = static_cast<int>(m_gyroSamples.size());
    if (n == 0) return result;

    auto amp  = [](const GyroOffsetStats& s) { return (s.count > 0) ? (s.maxV - s.minV) : 0.0f; };
    auto mean = [](const GyroOffsetStats& s) { return (s.count > 0) ? (s.sum / s.count) : 0.0f; };

    // Step 1: which offsets are real sensor channels (see computeAliveOffsets()).
    std::vector<bool> alive = computeAliveOffsets();

    // Step 2: find the first run of offsets spaced 2 bytes apart, all alive. Real devices seen
    // so far always expose a contiguous 6-offset block (gyro+accel); fall back to 3 (gyro only,
    // no accelerometer — e.g. the DS4 in this project's current config).
    auto findRun = [&](int runLen) -> int {
        for (int s = 0; s + (runLen - 1) * 2 < n; ++s) {
            bool all = true;
            for (int k = 0; k < runLen; ++k)
                if (!alive[s + k * 2]) { all = false; break; }
            if (all) return s;
        }
        return -1;
    };

    bool hasAccel   = true;
    int  blockStart = findRun(6);
    if (blockStart < 0) { hasAccel = false; blockStart = findRun(3); }
    if (blockStart < 0) {
        spdlog::trace("[GyroCal] classify FAILED: no 6-run or 3-run of alive offsets found");
        return result; // nothing usable found
    }

    std::vector<int> blockOffsets;
    int runLen = hasAccel ? 6 : 3;
    for (int k = 0; k < runLen; ++k) blockOffsets.push_back(blockStart + k * 2);

    // Step 3: pull out the "normal" accel axis (by far the largest steady baseline magnitude —
    // gravity). The other 5 candidates (or all 3, if !hasAccel) can't be split into accel/gyro
    // by baseline magnitude alone: with the mando held flat for Baseline, 2 of the 3 OTHER
    // accel axes read close to zero too, same as the 3 real gyro axes (confirmed on real
    // hardware, BITACORA 2026-07-10) — that split has to come from Step 4's cross-phase
    // reactivity instead, not a fixed threshold on a single static reading.
    int normalOffset = -1;
    if (hasAccel) {
        for (int o : blockOffsets) {
            float m = std::abs(mean(m_gyroSamples[o][static_cast<int>(GyroPhase::Baseline)]));
            if (normalOffset < 0 ||
                m > std::abs(mean(m_gyroSamples[normalOffset][static_cast<int>(GyroPhase::Baseline)])))
                normalOffset = o;
        }
    }
    std::vector<int> remaining;
    for (int o : blockOffsets) if (o != normalOffset) remaining.push_back(o);

    // Step 4: gyro axis — for each phase, whichever remaining candidate reacts most strongly
    // to THAT phase is its axis. Gyro reacts far more to the rotation it directly measures
    // than an accel offset's incidental reaction to the resulting orientation change, so this
    // holds even though 2 accel candidates are still mixed into the pool. Assigned greedily
    // (single strongest offset/phase pair first, across the whole pool) so a clash — one
    // offset being the top reactor for two different phases — resolves in favor of its best
    // match, leaving its second-best phase to the next-strongest candidate.
    struct PhaseReaction { int offset; float roll, pitch, yaw; };
    std::vector<PhaseReaction> reactions;
    for (int o : remaining) {
        reactions.push_back({
            o,
            amp(m_gyroSamples[o][static_cast<int>(GyroPhase::Roll)]),
            amp(m_gyroSamples[o][static_cast<int>(GyroPhase::Pitch)]),
            amp(m_gyroSamples[o][static_cast<int>(GyroPhase::Yaw)])
        });
    }
    int rollOffset = -1, pitchOffset = -1, yawOffset = -1;
    std::vector<bool> claimed(reactions.size(), false);
    for (int pass = 0; pass < 3; ++pass) {
        int bestIdx = -1; float bestAmp = -1.0f; GyroPhase bestPhase = GyroPhase::Roll;
        for (int i = 0; i < (int)reactions.size(); ++i) {
            if (claimed[i]) continue;
            if (rollOffset  < 0 && reactions[i].roll  > bestAmp) { bestAmp = reactions[i].roll;  bestIdx = i; bestPhase = GyroPhase::Roll;  }
            if (pitchOffset < 0 && reactions[i].pitch > bestAmp) { bestAmp = reactions[i].pitch; bestIdx = i; bestPhase = GyroPhase::Pitch; }
            if (yawOffset   < 0 && reactions[i].yaw   > bestAmp) { bestAmp = reactions[i].yaw;   bestIdx = i; bestPhase = GyroPhase::Yaw;   }
        }
        if (bestIdx < 0) break;
        claimed[bestIdx] = true;
        switch (bestPhase) {
        case GyroPhase::Roll:  rollOffset  = reactions[bestIdx].offset; break;
        case GyroPhase::Pitch: pitchOffset = reactions[bestIdx].offset; break;
        case GyroPhase::Yaw:   yawOffset   = reactions[bestIdx].offset; break;
        default: break;
        }
    }
    if (rollOffset < 0 || pitchOffset < 0 || yawOffset < 0) {
        spdlog::trace("[GyroCal] classify FAILED: roll={} pitch={} yaw={} (ambiguous reactivity match)",
                      rollOffset, pitchOffset, yawOffset);
        return result; // ambiguous, don't guess
    }

    // Axis naming convention already used by the DS4/Pro3 entries in controllers.json:
    // gyroX = pitch, gyroY = yaw, gyroZ = roll.
    result.gyroXOffset = pitchOffset;
    result.gyroYOffset = yawOffset;
    result.gyroZOffset = rollOffset;

    if (hasAccel) {
        // Step 5: whatever's left of the 5 candidates after Step 4 claimed the 3 gyro axes
        // are the other 2 accel axes (lateral/frontal) — the one that reacts most to roll is
        // lateral, the other (reacting more to pitch) is frontal.
        std::vector<int> accelOffsets;
        for (int i = 0; i < (int)reactions.size(); ++i)
            if (!claimed[i]) accelOffsets.push_back(reactions[i].offset);
        if (accelOffsets.size() != 2) {
            spdlog::trace("[GyroCal] classify FAILED: expected 2 leftover accel offsets, got {}", accelOffsets.size());
            return result; // ambiguous, don't guess
        }

        int lateralOffset = accelOffsets[0];
        int frontalOffset = accelOffsets[1];
        float rLateral = amp(m_gyroSamples[lateralOffset][static_cast<int>(GyroPhase::Roll)]);
        float rFrontal = amp(m_gyroSamples[frontalOffset][static_cast<int>(GyroPhase::Roll)]);
        if (rFrontal > rLateral) std::swap(lateralOffset, frontalOffset);

        result.accelZOffset = normalOffset;
        result.accelXOffset = lateralOffset;
        result.accelYOffset = frontalOffset;
        spdlog::trace("[GyroCal] classify OK: gyroX(pitch)={} gyroY(yaw)={} gyroZ(roll)={} accelX(lateral)={} accelY(frontal)={} accelZ(normal)={}",
                      pitchOffset, yawOffset, rollOffset, lateralOffset, frontalOffset, normalOffset);
    } else {
        spdlog::trace("[GyroCal] classify OK (no accel): gyroX(pitch)={} gyroY(yaw)={} gyroZ(roll)={}",
                      pitchOffset, yawOffset, rollOffset);
    }

    result.ok = true;
    return result;
}

void BindingWizard::openReader() {
    closeReader();
    const auto& c = m_controllers[m_selectedCtrl];
    m_hidReader = std::make_unique<RawHIDReader>(c.path);
    m_prevButtonMask = 0;
}

void BindingWizard::closeReader() {
    m_hidReader.reset();
}

GamepadState BindingWizard::buildFakeState() const {
    GamepadState s{};
    if (m_currentStep < 0 || m_currentStep >= (int)m_steps.size()) return s;
    const BindStep& step = m_steps[m_currentStep];
    if (step.compIndex < 0 || step.compIndex >= (int)m_layout.components.size()) return s;
    const PadComponent& c = m_layout.components[step.compIndex];

    // For button/dpad steps: set the matching bool in GamepadState so the
    // component renders in its active (pressed) color.
    auto activate = [&](const std::string& name) {
        if      (name == "btnA")      s.btnA      = true;
        else if (name == "btnB")      s.btnB      = true;
        else if (name == "btnX")      s.btnX      = true;
        else if (name == "btnY")      s.btnY      = true;
        else if (name == "btnLB")     s.btnLB     = true;
        else if (name == "btnRB")     s.btnRB     = true;
        else if (name == "btnL3")     s.btnL3     = true;
        else if (name == "btnR3")     s.btnR3     = true;
        else if (name == "btnBack")   s.btnBack   = true;
        else if (name == "btnStart")  s.btnStart  = true;
        else if (name == "btnHome")   s.btnHome   = true;
        else if (name == "btnL4")     s.btnL4     = true;
        else if (name == "btnR4")     s.btnR4     = true;
        else if (name == "btnLP")     s.btnLP     = true;
        else if (name == "btnRP")     s.btnRP     = true;
        else if (name == "btnTouch")  s.btnTouch  = true;
        else if (name == "dpadUp")    s.dpadUp    = true;
        else if (name == "dpadDown")  s.dpadDown  = true;
        else if (name == "dpadLeft")  s.dpadLeft  = true;
        else if (name == "dpadRight") s.dpadRight = true;
        else if (name == "triggerL")  s.triggerL  = 1.0f;
        else if (name == "triggerR")  s.triggerR  = 1.0f;
    };

    const std::string& t = step.mapping.type;
    if (t == "button" || t == "physical_only" || t == "stick") {
        // Use step.state (= comp.state for buttons, comp.stateClick for L3/R3 stick steps)
        activate(step.state);
    } else if (t == "trigger") {
        activate(step.mapping.axis_target == "trigger_l" ? "triggerL" : "triggerR");
    } else if (t == "dpad") {
        // Prompt says "any direction" — light up all arms
        s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = true;
    } else if (t == "axis" && step.compIndex >= 0 &&
               step.compIndex < (int)m_layout.components.size() &&
               m_layout.components[step.compIndex].type == "analog_dpad") {
        // Light the arm we're asking the user to press.
        // Y convention: negative = down (joystick convention, invert_if_positive:true).
        // X convention: positive = right.
        bool isY = (step.mapping.axis_target.find("_y") != std::string::npos);
        if (isY) {
            if      (step.state == "leftY")  s.leftY  = -1.0f;
            else if (step.state == "rightY") s.rightY = -1.0f;
        } else {
            if      (step.state == "leftX")  s.leftX  = 1.0f;
            else if (step.state == "rightX") s.rightX = 1.0f;
        }
    }
    // axis steps: no fake state — arrows convey the direction instead

    return s;
}

void BindingWizard::snapshotBaseline() {
    m_prevButtonMask   = 0;
    m_axisConfirmCount = 0;
    m_axisConfirmBest  = -1;
    m_axisConfirmSum   = 0.0f;
    if (m_hidReader && m_hidReader->isOpen()) {
        RawHIDState s{};
        m_hidReader->read(s);
        m_axisBaseline  = s;
        m_axisLastRead  = s; // sync persistent read state with new baseline
        m_prevButtonMask = s.buttonMask;
    }
}

// ---------------------------------------------------------------------------
// Save result to controllers.json
// ---------------------------------------------------------------------------

void BindingWizard::saveResult() {
    const auto& ctrl = m_controllers[m_selectedCtrl];

    const std::string mode = "hid";

    // Build the new entry as JSON
    json entry;
    char vidStr[8], pidStr[8];
    snprintf(vidStr, sizeof(vidStr), "%04X", ctrl.vid);
    snprintf(pidStr, sizeof(pidStr), "%04X", ctrl.pid);
    entry["vid"]         = vidStr;
    entry["pid"]         = pidStr;
    entry["source_name"] = m_nameBuf;
    entry["mode"]        = mode;
    entry["layout_id"]   = m_layout.id;
    if (m_saveWithConnection && !ctrl.connectionType.empty())
        entry["connection"] = ctrl.connectionType;
    if (!ctrl.productName.empty())
        entry["product_name"] = ctrl.productName;

    // Buttons
    json buttons = json::object();
    for (const auto& b : m_boundButtons) {
        std::string key = std::to_string(b.physIndex);
        if (b.physicalOnly)
            buttons[key] = { { "physical", b.physical } };
        else
            buttons[key] = { { "physical", b.physical }, { "virtual", b.physical } };
    }
    entry["buttons"] = buttons;

    // Axes — detect trigger_combined: same source bound as trigger_l + trigger_r pair
    json axes = json::object();
    for (const auto& a : m_boundAxes) {
        if (a.target == "trigger_l" || a.target == "trigger_r") {
            auto pairedIt = std::find_if(m_boundAxes.begin(), m_boundAxes.end(),
                [&](const AxisResult& b) {
                    return b.source == a.source &&
                           (b.target == "trigger_l" || b.target == "trigger_r") &&
                           b.target != a.target;
                });
            if (pairedIt != m_boundAxes.end()) {
                if (a.target == "trigger_l")  // write combined once, from the trigger_l entry
                    axes[a.source] = { { "target", "trigger_combined" }, { "invert", false } };
                continue; // trigger_r entry is skipped (already merged above)
            }
        }
        axes[a.source] = { { "target", a.target }, { "invert", a.invert } };
    }
    entry["axes"] = axes;

    // Analog dpad: generate axis_actions from the two captured axes.
    // Convention: positive Y = down, positive X = right.
    // Invert flag (already absorbed into axes) flips both halves.
    {
        json axActions = json::object();
        for (const auto& a : m_boundAxes) {
            if (!a.isAnalogDpad) continue;
            bool isY = (a.target.find("_y") != std::string::npos);
            // Base assumption: raw positive = down/right. The invert flag (set by the wizard
            // when the device sends positive for down) swaps pos/neg so the final axis_actions
            // always read: left_y_pos→dpad_up, left_y_neg→dpad_down (joystick convention).
            std::string posDir = isY ? "dpad_down" : "dpad_right";
            std::string negDir = isY ? "dpad_up"   : "dpad_left";
            if (a.invert) std::swap(posDir, negDir);
            axActions[a.target + "_pos"] = { {"virtual", posDir} };
            axActions[a.target + "_neg"] = { {"virtual", negDir} };
        }
        if (!axActions.empty()) entry["axis_actions"] = axActions;
    }

    // Dpad
    if (m_hasDpad) entry["dpad"] = m_dpadType;

    // IMU — only written when the layout has a gyro component AND classification succeeded.
    // On failure the key is simply omitted, so the merge below preserves whatever "imu" block
    // (if any) was already in controllers.json for this controller.
    if (m_hasGyroStep && m_gyroResult.ok) {
        json imu;
        imu["enabled"]        = true;
        imu["gyro_scale"]     = 0.0001;
        imu["gyro_x_offset"]  = m_gyroResult.gyroXOffset;
        imu["gyro_y_offset"]  = m_gyroResult.gyroYOffset;
        imu["gyro_z_offset"]  = m_gyroResult.gyroZOffset;
        if (m_gyroResult.accelXOffset >= 0) {
            imu["accel_scale"]    = 0.0001;
            imu["accel_x_offset"] = m_gyroResult.accelXOffset;
            imu["accel_y_offset"] = m_gyroResult.accelYOffset;
            imu["accel_z_offset"] = m_gyroResult.accelZOffset;
        }
        entry["imu"] = imu;
    }

    // Load existing controllers.json, replace or append
    json root;
    {
        std::ifstream f(m_controllersPath);
        if (f.is_open()) {
            try { root = json::parse(f); } catch (...) {}
        }
    }
    if (!root.contains("controllers") || !root["controllers"].is_array())
        root["controllers"] = json::array();

    // Replace existing entry matching VID+PID+connection+source_name, or append.
    // Rules:
    //  - connection: if both sides declare it and differ → skip (different transport)
    //  - source_name: if both sides have it and differ → skip (different device identity)
    //  - Old entries without connection/source_name: matched by VID+PID only (backwards compat)
    std::string newConn = ctrl.connectionType;
    std::string newName = m_nameBuf;
    bool replaced = false;
    for (auto& e : root["controllers"]) {
        if (e.value("vid","") != std::string(vidStr) || e.value("pid","") != std::string(pidStr))
            continue;
        // Entries without "connection" are generic fallbacks → match any transport
        std::string eConn = e.value("connection","");
        bool connMatch = newConn.empty() || eConn.empty() || eConn == newConn;
        std::string eName = e.value("source_name","");
        bool nameMatch = eName.empty() || newName.empty() || eName == newName;
        if (connMatch && nameMatch) {
            // Preserve fields the wizard doesn't manage (e.g. "touchpad", "_hid_prototype")
            // by starting from the old entry and overwriting only wizard-managed keys.
            for (auto& [k, v] : entry.items())
                e[k] = v;
            replaced = true;
            break;
        }
    }
    if (!replaced) root["controllers"].push_back(entry);

    // Write back
    std::ofstream f(m_controllersPath);
    if (f.is_open()) {
        f << root.dump(2);
        m_state     = State::Idle;
        m_savedFlag = true;
    }
}
