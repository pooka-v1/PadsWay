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
    m_gyroArrowN.release();
    m_gyroArrowS.release();
    m_gyroArrowE.release();
    m_gyroArrowW.release();
    m_gyroArrowCW.release();
    m_gyroArrowCCW.release();
}

void BindingWizard::loadArrows() {
    PadView::loadPng(m_device, "images/decorations/ArrowLeft.png",  m_arrowLeft);
    PadView::loadPng(m_device, "images/decorations/ArrowRight.png", m_arrowRight);
    PadView::loadPng(m_device, "images/decorations/ArrowUp.png",    m_arrowUp);
    PadView::loadPng(m_device, "images/decorations/ArrowDown.png",  m_arrowDown);
    PadView::loadPng(m_device, "images/gyroscope/ArrowNort.png",             m_gyroArrowN);
    PadView::loadPng(m_device, "images/gyroscope/ArrowSouth.png",            m_gyroArrowS);
    PadView::loadPng(m_device, "images/gyroscope/ArrowEst.png",              m_gyroArrowE);
    PadView::loadPng(m_device, "images/gyroscope/ArrowWest.png",             m_gyroArrowW);
    PadView::loadPng(m_device, "images/gyroscope/ArrowClockwise.png",        m_gyroArrowCW);
    PadView::loadPng(m_device, "images/gyroscope/ArrowCounterclockwise.png", m_gyroArrowCCW);
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
                if (m_gyroPoolFailed) {
                    promptText = tr("wizard.gyro_pool_failed");
                } else if (m_gyroPhase == GyroPhase::Baseline) {
                    promptText = tr("wizard.gyro_baseline");
                } else if (m_gyroPhase == GyroPhase::Flip) {
                    promptText = tr("wizard.gyro_flip");
                } else if (m_gyroRoundStage == GyroRoundStage::MoveToA ||
                           m_gyroRoundStage == GyroRoundStage::MoveToB) {
                    bool toA = (m_gyroRoundStage == GyroRoundStage::MoveToA);
                    switch (m_gyroPhase) {
                    case GyroPhase::Roll:  promptText = toA ? tr("wizard.gyro_roll_a")  : tr("wizard.gyro_roll_b");  break;
                    case GyroPhase::Pitch: promptText = toA ? tr("wizard.gyro_pitch_a") : tr("wizard.gyro_pitch_b"); break;
                    case GyroPhase::Yaw:   promptText = toA ? tr("wizard.gyro_yaw_a")   : tr("wizard.gyro_yaw_b");   break;
                    default: break;
                    }
                } else {
                    promptText = tr("wizard.gyro_hold"); // HoldA / HoldB — same instruction either side
                }
            } else if (t == "touch_surface") {
                if (m_touchPoolFailed) {
                    // m_touchPhase is still whichever phase failed (Lift or Confirm — see
                    // commitTouchPhase(), it returns before advancing the phase on failure), so
                    // the message can say exactly which one, instead of one ambiguous text for
                    // both (2026/08/30: this ambiguity made a real Confirm failure hard to tell
                    // apart from a Lift failure while diagnosing it with the user).
                    promptText = (m_touchPhase == TouchPhase::Lift) ? tr("wizard.touch_surface_lift_failed")
                                                                      : tr("wizard.touch_surface_confirm_failed");
                } else switch (m_touchPhase) {
                    case TouchPhase::Lift:    promptText = tr("wizard.touch_surface_lift");     break;
                    case TouchPhase::Confirm: promptText = tr("wizard.touch_surface_confirm");  break;
                    case TouchPhase::RangeX:  promptText = tr("wizard.touch_surface_range_x");  break;
                    case TouchPhase::RangeY:  promptText = tr("wizard.touch_surface_range_y");  break;
                }
            }

            if (t == "gyro" && m_gyroPhase != GyroPhase::Baseline && m_gyroPhase != GyroPhase::Flip) {
                int axisIdx = static_cast<int>(m_gyroPhase) - static_cast<int>(GyroPhase::Roll);
                ImGui::Text(tr("wizard.gyro_phase"), axisIdx + 1);
                ImGui::Text(tr("wizard.gyro_round"), m_gyroRound);
                ImGui::Spacing();
            }

            // Amber for the pool-failed message (matches Review's review_gyro_failed), yellow for
            // every normal instruction prompt.
            bool isFailure = (t == "gyro" && m_gyroPoolFailed) || (t == "touch_surface" && m_touchPoolFailed);
            ImGui::SetWindowFontScale(1.35f);
            ImGui::PushStyleColor(ImGuiCol_Text, isFailure ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f)
                                                             : ImVec4(1.0f, 0.95f, 0.2f, 1.0f));
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
            if (m_gyroPoolFailed) {
                // Held here instead of auto-advancing (see commitGyroPhase()) so the amber
                // message above is actually seen before the gyro component gets skipped.
                if (ImGui::Button(trid("wizard.gyro_continue", "bind").c_str(), { 180.0f, 0.0f })) {
                    m_gyroPoolFailed = false;
                    finishGyroStep();
                }
            } else if (!m_gyroPhaseStarted) {
                ImGui::TextWrapped("%s", tr("wizard.gyro_start"));
                int dummyIdx = 0;
                // Always call captureButton() so m_prevButtonMask stays in sync with the real
                // controller state — but only honor the press once m_gyroAwaitingRelease has
                // cleared. Without this, a button still physically held from confirming the
                // PREVIOUS step (or a stray bump while the hand repositions) can start the phase
                // instantly, before the user ever sees this prompt — confirmed for real, 2026/07:
                // finishing the last regular button step (Home) sometimes jumped straight into
                // Baseline capture with no visible pause.
                bool pressed = captureButton(dummyIdx);
                if (m_gyroAwaitingRelease) {
                    if (m_prevButtonMask == 0) m_gyroAwaitingRelease = false;
                } else if (pressed) {
                    m_gyroPhaseStarted = true;
                    // Re-snapshot the contamination reference right as the user commits to
                    // starting, not back in beginStep() (which fires the instant the step
                    // becomes active — often still mid-release from confirming the PREVIOUS
                    // step, e.g. a trigger binding). A stale one-shot snapshot here means every
                    // later frame compares against a wrong resting value forever: confirmed for
                    // real on the DS4, 2026/07/11 — R2 (hid_ry) read a constant -2.0 delta
                    // (a full bipolar swing) against every subsequent frame, so Baseline never
                    // saw a single "clean" frame and sat at 0% the whole capture.
                    if (m_hidReader && m_hidReader->isOpen())
                        m_hidReader->read(m_gyroAxisBaseline);
                }
            } else if (m_gyroPhase == GyroPhase::Baseline || m_gyroPhase == GyroPhase::Flip) {
                // Baseline and Flip have no back-and-forth gesture to repeat — they just need the
                // controller quiet (in a different orientation each) for a fixed time, so both
                // keep the same time-based gate instead of the Roll/Pitch/Yaw round machine.
                sampleGyroFrame();
                int  minFrames  = (m_gyroPhase == GyroPhase::Baseline) ? kGyroMinCaptureFrames : kGyroFlipMinCaptureFrames;
                bool canAdvance = m_gyroPhaseFrames >= minFrames;
                int  pct        = std::min(100, (m_gyroPhaseFrames * 100) / minFrames);
                ImGui::Text(tr("wizard.gyro_capturing"), pct);
                if (canAdvance) ImGui::TextDisabled("%s", tr("wizard.gyro_ready_hint"));
                ImGui::Spacing();
                if (renderPhaseAdvanceControl(canAdvance)) commitGyroPhase();
            } else {
                // Roll/Pitch/Yaw: fully automatic move/hold detection (see updateGyroRound()) —
                // no manual "Continuar", the round machine advances itself. The only escape
                // hatches are Atras (redo this axis) and Saltar (skip the whole component);
                // finishGyroRound() has its own kGyroMaxRounds cap if an axis never converges.
                std::vector<float> raw;
                if (sampleGyroFrame(&raw)) updateGyroRound(raw);
                float confidence = axisConfidence();
                ImGui::Text(tr("wizard.gyro_confidence"), static_cast<int>(confidence * 100.0f));
            }
        } else if (t == "touch_surface") {
            if (m_touchPoolFailed) {
                // Held here instead of auto-advancing (see commitTouchPhase()) so the amber
                // message above is actually seen before the touch_surface step gets skipped.
                if (ImGui::Button(trid("wizard.touch_surface_continue", "bind").c_str(), { 180.0f, 0.0f })) {
                    m_touchPoolFailed = false;
                    finishTouchSurfaceStep();
                }
            } else if (!m_touchPhaseStarted &&
                       (m_touchPhase == TouchPhase::Lift || m_touchPhase == TouchPhase::Confirm)) {
                // Same "press any button to start" gate as the gyro phases (see the comment on
                // that block above) — lets the user get their hand in position (or off the pad,
                // for Lift) before the capture window starts counting.
                ImGui::TextWrapped("%s", tr("wizard.touch_surface_start"));
                int dummyIdx = 0;
                bool pressed = captureButton(dummyIdx);
                if (m_touchAwaitingRelease) {
                    if (m_prevButtonMask == 0) m_touchAwaitingRelease = false;
                } else if (pressed) {
                    m_touchPhaseStarted = true;
                }
            } else if (m_touchPhase == TouchPhase::Lift) {
                sampleTouchFrame();
                bool canAdvance = m_touchPhaseFrames >= kTouchLiftMinFrames;
                int  pct = std::min(100, (m_touchPhaseFrames * 100) / kTouchLiftMinFrames);
                ImGui::Text(tr("wizard.touch_surface_capturing"), pct);
                if (canAdvance) ImGui::TextDisabled("%s", tr("wizard.gyro_ready_hint"));
                ImGui::Spacing();
                if (renderPhaseAdvanceControl(canAdvance)) commitTouchPhase();
            } else if (m_touchPhase == TouchPhase::Confirm) {
                // No fraction/percentage here — the live number IS the feedback: it's the best
                // tap count seen so far across all surviving Lift candidates (not necessarily the
                // eventual winner while still low), so the user can tell at a glance whether their
                // taps are actually registering before pressing Continue.
                sampleTouchFrame();
                int bestEdgesSoFar = 0;
                for (int o = 0; o < static_cast<int>(m_touchLiftAlive.size()); ++o) {
                    if (m_touchLiftAlive[o] && m_touchConfirmEdges[o] > bestEdgesSoFar)
                        bestEdgesSoFar = m_touchConfirmEdges[o];
                }
                bool canAdvance = m_touchPhaseFrames >= kTouchConfirmMinFrames;
                ImGui::Text(tr("wizard.touch_surface_confirm_taps"), bestEdgesSoFar);
                if (canAdvance) ImGui::TextDisabled("%s", tr("wizard.gyro_ready_hint"));
                ImGui::Spacing();
                if (renderPhaseAdvanceControl(canAdvance)) commitTouchPhase();
            } else {
                // RangeX/RangeY: no percentage, no rush — slide into the edge as many times as
                // needed and watch the live max below; once it stops growing, confirm with any
                // controller button (except the touchpad's own click, excluded via
                // m_touchClickPhysIndex — see renderPhaseAdvanceControl()'s comment) or Continue.
                sampleTouchFrame();
                bool canAdvance = m_touchRangeTouchedFrames >= kTouchRangeMinFrames;
                int  liveMax = (m_touchPhase == TouchPhase::RangeX) ? m_touchRangeMaxX : m_touchRangeMaxY;
                ImGui::Text(tr("wizard.touch_surface_range_live"), liveMax);
                ImGui::Spacing();
                if (renderPhaseAdvanceControl(canAdvance, /*acceptAnyButton=*/true, m_touchClickPhysIndex))
                    commitTouchPhase();
            }
        }

        // ── Manual controls ──────────────────────────────────────────────────
        bool midGyroPhases  = (t == "gyro" && m_gyroPhase != GyroPhase::Baseline);
        bool midTouchPhases = (t == "touch_surface" && m_touchPhase != TouchPhase::Lift);
        bool atVeryStart = (m_currentStep == 0 && !midGyroPhases && !midTouchPhases);
        if (atVeryStart) ImGui::BeginDisabled();
        if (ImGui::Button(trid("btn.back", "bind").c_str(), { 90.0f, 0.0f })) {
            if (midGyroPhases) {
                m_gyroPhase = static_cast<GyroPhase>(static_cast<int>(m_gyroPhase) - 1);
                resetGyroRoundState(/*clearVotes=*/true);
                if (m_gyroPhase == GyroPhase::Baseline) {
                    m_gyroCandidates.clear();
                    m_gyroHasAccel     = false;
                    m_gyroNormalOffset = -1;
                } else if (m_gyroPhase == GyroPhase::Flip) {
                    // Stepping back into Flip from Roll: undo confirmNormalOffsetFromFlip()'s
                    // pick, fall back to computeGyroCandidatePool()'s baseline-magnitude guess so
                    // Flip can be redone cleanly.
                    computeGyroCandidatePool();
                } else {
                    int axisIdx = static_cast<int>(m_gyroPhase) - static_cast<int>(GyroPhase::Roll);
                    m_gyroAxisGyroOffset[axisIdx]  = -1;
                    m_gyroAxisAccelOffset[axisIdx] = -1;
                }
                m_gyroPhaseStarted = false;
                m_gyroAwaitingRelease = true;
                snapshotBaseline(); // resync m_prevButtonMask so a repeated start-button press is detected as new
            } else if (midTouchPhases) {
                m_touchPhase = static_cast<TouchPhase>(static_cast<int>(m_touchPhase) - 1);
                m_touchPoolFailed  = false;
                m_touchPhaseFrames = 0;
                if (m_touchPhase == TouchPhase::RangeX) {
                    m_touchRangeMaxX = 0;
                    m_touchRangeTouchedFrames = 0;
                } else if (m_touchPhase == TouchPhase::Confirm) {
                    m_touchDataOffset = -1;
                    std::fill(m_touchConfirmEdges.begin(), m_touchConfirmEdges.end(), 0);
                    std::fill(m_touchConfirmPrevTouching.begin(), m_touchConfirmPrevTouching.end(), false);
                } else { // back to Lift
                    m_touchLiftAlive.clear(); // forces sampleTouchFrame() to reinit all-true
                    m_touchLiftMin.clear();
                    m_touchLiftMax.clear();
                }
                m_touchPhaseStarted    = false;
                m_touchAwaitingRelease = true;
                snapshotBaseline();
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

            std::vector<const char*> inverted;
            if (m_gyroResult.gyroXInvert)  inverted.push_back("pitch");
            if (m_gyroResult.gyroYInvert)  inverted.push_back("yaw");
            if (m_gyroResult.gyroZInvert)  inverted.push_back("roll");
            if (m_gyroResult.accelXInvert) inverted.push_back("lateral");
            if (m_gyroResult.accelYInvert) inverted.push_back("frontal");
            if (m_gyroResult.accelZInvert) inverted.push_back("normal");
            if (!inverted.empty()) {
                std::string list;
                for (size_t i = 0; i < inverted.size(); ++i) {
                    if (i > 0) list += ", ";
                    list += inverted[i];
                }
                ImGui::Text(tr("wizard.review_gyro_inverted"), list.c_str());
            }
        } else {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, "%s", tr("wizard.review_gyro_failed"));
        }
    }

    if (m_hasTouchSurfaceStep) {
        if (m_touchSurfaceResult.ok) {
            ImGui::Text(tr("wizard.review_touch_ok"), m_touchSurfaceResult.dataOffset,
                        m_touchSurfaceResult.maxX, m_touchSurfaceResult.maxY);
        } else {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, "%s", tr("wizard.review_touch_failed"));
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
        } else if (type == "gyro" &&
                   (m_gyroRoundStage == GyroRoundStage::MoveToA || m_gyroRoundStage == GyroRoundStage::MoveToB) &&
                   step.compIndex >= 0 && step.compIndex < (int)m_layout.components.size()) {
            // Light the gyro widget's own reference arrow for the gesture being requested
            // (see gyro_roll_a/gyro_pitch_a/gyro_yaw_a prompts) — drawn at native size centered
            // on the component, same as every other gyro-widget layer (PadView.cpp), as an
            // overlay on top of the widget's always-dim arrows (PadView's fake wizard state has
            // no gyro reading).
            const PadComponent& comp = m_layout.components[step.compIndex];
            ImVec2 gCenter = { m_canvasOrigin.x + comp.cx, m_canvasOrigin.y + comp.cy };

            auto drawGyroArrow = [&](const PadTexture& tex) {
                if (!tex.valid()) return;
                ImVec2 tl = { gCenter.x - tex.w * 0.5f, gCenter.y - tex.h * 0.5f };
                ImVec2 br = { tl.x + tex.w, tl.y + tex.h };
                dl->AddImage((ImTextureID)(intptr_t)tex.srv, tl, br,
                             {0,0}, {1,1}, IM_COL32(255, 235, 120, 255));
            };

            bool toA = (m_gyroRoundStage == GyroRoundStage::MoveToA);
            switch (m_gyroPhase) {
            case GyroPhase::Roll:  drawGyroArrow(toA ? m_gyroArrowE : m_gyroArrowW);  break;
            case GyroPhase::Pitch: drawGyroArrow(toA ? m_gyroArrowN : m_gyroArrowS);  break;
            case GyroPhase::Yaw:   drawGyroArrow(toA ? m_gyroArrowCW : m_gyroArrowCCW); break;
            default: break;
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
    m_hasTouchSurfaceStep = false;
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

        // Touchpad: one step for the click button, followed by a second step that runs the
        // touch_surface discovery sub-machine (dataOffset/maxX/maxY) — see ARCHITECTURE.md,
        // "Wizard — descubrimiento crudo". No state_map entry for the second step: it doesn't
        // bind to a virtual action, it discovers raw report byte offsets, same relationship the
        // "gyro" step above has to ImuConfig.
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

            BindStep surf;
            surf.compIndex    = i;
            surf.state        = "touch_surface";
            surf.mapping.type = "touch_surface";
            m_steps.push_back(surf);
            m_hasTouchSurfaceStep = true;
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
        m_gyroAwaitingRelease = true;
        m_gyroResult = ImuCalibrationResult{};
        m_gyroCandidates.clear();
        m_gyroHasAccel     = false;
        m_gyroNormalOffset = -1;
        m_gyroPoolFailed   = false;
        for (int i = 0; i < 3; ++i) { m_gyroAxisGyroOffset[i] = -1; m_gyroAxisAccelOffset[i] = -1; }
        resetGyroRoundState(/*clearVotes=*/true);
        if (m_hidReader && m_hidReader->isOpen())
            m_hidReader->read(m_gyroAxisBaseline);
    } else if (m_steps[m_currentStep].mapping.type == "touch_surface") {
        resetTouchSurfaceState();
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

// Only reachable from Baseline and Flip — Roll/Pitch/Yaw auto-advance via finishGyroRound()
// instead of a manual "Continuar" click, see renderBinding()'s gyro block.
void BindingWizard::commitGyroPhase() {
    if (m_gyroPhase == GyroPhase::Baseline) {
        computeGyroCandidatePool();
        if (m_gyroCandidates.empty()) {
            // No usable run of alive offsets — same failure classifyGyro() used to report at the
            // very end, caught early here instead so Flip/Roll/Pitch/Yaw aren't captured for
            // nothing. Don't auto-advance: hold the step on an explicit message (see
            // renderBinding()'s gyro block, m_gyroPoolFailed) so the user sees why the gyro step
            // is about to be skipped instead of the wizard silently jumping to the next
            // component (2026/08/10 field report).
            m_gyroResult = ImuCalibrationResult{};
            m_gyroPoolFailed = true;
            return;
        }
        // A gyro-only device (no accelerometer block found) has nothing for the flip gesture to
        // confirm — skip straight to Roll.
        m_gyroPhase = m_gyroHasAccel ? GyroPhase::Flip : GyroPhase::Roll;
    } else if (m_gyroPhase == GyroPhase::Flip) {
        confirmNormalOffsetFromFlip();
        m_gyroPhase = GyroPhase::Roll;
    }
    resetGyroRoundState(/*clearVotes=*/true);
    m_gyroPhaseStarted = false;
    m_gyroAwaitingRelease = true;
    snapshotBaseline(); // resync m_prevButtonMask so a repeated start-button press is detected as new
}

// Shared tail of the gyro BindStep, reached either after all 3 axes converge or after a failure
// (no usable candidate pool, or kGyroMaxRounds reached without a winner) — same bookkeeping the
// other commit* methods do.
void BindingWizard::finishGyroStep() {
    const BindStep& step = m_steps[m_currentStep];
    if (step.compIndex >= 0)
        m_overlayLabels[step.compIndex] = m_gyroResult.ok ? "IMU" : "IMU?";
    m_stepCooldown = kAxisCooldown;
    ++m_currentStep;
    beginStep();
}

// ---------------------------------------------------------------------------
// Touch surface discovery (touch_surface step)
// ---------------------------------------------------------------------------

void BindingWizard::resetTouchSurfaceState() {
    m_touchPhase           = TouchPhase::Lift;
    m_touchPhaseStarted    = false;
    m_touchAwaitingRelease = true;
    m_touchPhaseFrames     = 0;
    m_touchPoolFailed      = false;
    m_touchLiftAlive.clear();       // sampleTouchFrame() lazily resizes+inits all-true
    m_touchLiftMin.clear();
    m_touchLiftMax.clear();
    m_touchConfirmEdges.clear();
    m_touchConfirmPrevTouching.clear();
    m_touchDataOffset       = -1;
    m_touchRangeMaxX        = 0;
    m_touchRangeMaxY        = 0;
    m_touchRangeTouchedFrames = 0;
    m_touchSurfaceResult    = TouchSurfaceResult{};

    // Resolve the touchpad's own click physIndex from the preceding click BindStep (same
    // compIndex, already committed by the time this — the touch_surface step — starts), so
    // RangeX/RangeY can exclude it from "any button confirms" (see m_touchClickPhysIndex's
    // comment in the header for why). -1 (no exclusion) if that step was skipped.
    m_touchClickPhysIndex = -1;
    if (m_currentStep < (int)m_steps.size()) {
        int compIdx = m_steps[m_currentStep].compIndex;
        for (const auto& b : m_boundButtons) {
            if (b.compIndex == compIdx) { m_touchClickPhysIndex = b.physIndex; break; }
        }
    }
}

bool BindingWizard::sampleTouchFrame() {
    if (!m_hidReader || !m_hidReader->isOpen()) return false;
    RawHIDState s{};
    m_hidReader->read(s);
    if (!s.valid || s.raw.size() < 4) return false;

    const int n = static_cast<int>(s.raw.size());

    if (m_touchPhase == TouchPhase::Lift) {
        bool firstFrame = (static_cast<int>(m_touchLiftAlive.size()) != n);
        if (firstFrame) {
            m_touchLiftAlive.assign(n, true);
            m_touchLiftMin.assign(n, 0);
            m_touchLiftMax.assign(n, 0);
        }
        for (int o = 0; o < n; ++o) {
            uint8_t v = static_cast<uint8_t>(s.raw[o]);
            if (firstFrame) {
                m_touchLiftMin[o] = v;
                m_touchLiftMax[o] = v;
            } else {
                if (v < m_touchLiftMin[o]) m_touchLiftMin[o] = v;
                if (v > m_touchLiftMax[o]) m_touchLiftMax[o] = v;
            }
            if ((v & 0x80) == 0) m_touchLiftAlive[o] = false;
        }
    } else if (m_touchPhase == TouchPhase::Confirm) {
        if (static_cast<int>(m_touchConfirmEdges.size()) != n) {
            m_touchConfirmEdges.assign(n, 0);
            // Baseline "not touching" — matches how the phase actually starts (right after Lift,
            // finger off the pad), so the very first tap counts as an edge too.
            m_touchConfirmPrevTouching.assign(n, false);
        }
        for (int o = 0; o < n && o < static_cast<int>(m_touchLiftAlive.size()); ++o) {
            if (!m_touchLiftAlive[o]) continue; // not a Lift candidate, skip scoring it
            bool touching = (s.raw[o] & 0x80) == 0;
            if (touching && !m_touchConfirmPrevTouching[o]) ++m_touchConfirmEdges[o]; // clean tap detected
            m_touchConfirmPrevTouching[o] = touching;
        }
    } else { // RangeX / RangeY — decode against the confirmed dataOffset
        int o = m_touchDataOffset;
        if (o >= 0 && o + 3 < n && (s.raw[o] & 0x80) == 0) {
            ++m_touchRangeTouchedFrames;
            if (m_touchPhase == TouchPhase::RangeX) {
                int x = s.raw[o + 1] | ((s.raw[o + 2] & 0x0F) << 8);
                m_touchRangeMaxX = std::max(m_touchRangeMaxX, x);
            } else {
                int y = ((s.raw[o + 2] & 0xF0) >> 4) | (s.raw[o + 3] << 4);
                m_touchRangeMaxY = std::max(m_touchRangeMaxY, y);
            }
        }
    }

    ++m_touchPhaseFrames;
    return true;
}

// "Continuar" for the touch_surface step: evaluates the phase that just finished capturing.
// Lift/Confirm can fail outright (see m_touchPoolFailed); RangeX/RangeY always succeed since
// m_touchDataOffset is already confirmed by the time they run.
void BindingWizard::commitTouchPhase() {
    if (m_touchPhase == TouchPhase::Lift) {
        // A candidate must ALSO have read a bit-for-bit constant value the whole time — see
        // m_touchLiftMin/m_touchLiftMax's comment — this is what actually rules out gyro/accel
        // bytes, not the bit7 check alone (a near-still sensor byte can hold bit7 stable while
        // still jittering a few counts below it).
        bool anyCandidate = false;
        for (int o = 0; o < static_cast<int>(m_touchLiftAlive.size()); ++o) {
            if (!m_touchLiftAlive[o]) continue;
            int peakToPeak = static_cast<int>(m_touchLiftMax[o]) - static_cast<int>(m_touchLiftMin[o]);
            if (peakToPeak > kTouchLiftNoiseFloor) { m_touchLiftAlive[o] = false; continue; }
            anyCandidate = true;
        }
        if (!anyCandidate) {
            m_touchPoolFailed = true;
            return;
        }
        m_touchPhase = TouchPhase::Confirm;
        m_touchConfirmEdges.assign(m_touchLiftAlive.size(), 0);
        m_touchConfirmPrevTouching.assign(m_touchLiftAlive.size(), false);
    } else if (m_touchPhase == TouchPhase::Confirm) {
        // Winner = the lowest-offset candidate whose edge count actually lands within
        // +-kTouchConfirmTapTolerance of the requested kTouchConfirmTargetTaps — not "whichever
        // has the most" (see m_touchConfirmEdges' comment for why that picked the wrong byte for
        // real, 2026/08/30). The loop already runs offsets low-to-high, so the first match found
        // IS the lowest-offset one — no separate tie-break needed.
        int winner = -1;
        for (int o = 0; o < static_cast<int>(m_touchLiftAlive.size()); ++o) {
            if (!m_touchLiftAlive[o]) continue;
            int diff = m_touchConfirmEdges[o] - kTouchConfirmTargetTaps;
            if (diff < 0) diff = -diff;
            if (diff <= kTouchConfirmTapTolerance) { winner = o; break; }
        }
        if (winner < 0) {
            m_touchPoolFailed = true;
            return;
        }
        m_touchDataOffset = winner;
        m_touchPhase      = TouchPhase::RangeX;
    } else if (m_touchPhase == TouchPhase::RangeX) {
        m_touchPhase = TouchPhase::RangeY;
        m_touchRangeMaxY = 0;
        m_touchRangeTouchedFrames = 0;
    } else { // RangeY
        m_touchSurfaceResult = { true, m_touchDataOffset, m_touchRangeMaxX, m_touchRangeMaxY };
        finishTouchSurfaceStep();
        return;
    }
    m_touchPhaseFrames        = 0;
    m_touchRangeTouchedFrames = 0;
    m_touchPhaseStarted       = false;
    m_touchAwaitingRelease    = true;
    snapshotBaseline(); // resync m_prevButtonMask so a repeated start-button press is detected as new
}

// Shared tail of the touch_surface BindStep, reached either after RangeY succeeds or after a
// failure (m_touchPoolFailed's own "Continuar" — see renderBinding()) — same bookkeeping
// finishGyroStep() does.
void BindingWizard::finishTouchSurfaceStep() {
    const BindStep& step = m_steps[m_currentStep];
    if (step.compIndex >= 0)
        m_overlayLabels[step.compIndex] = m_touchSurfaceResult.ok ? "Touch OK" : "Touch?";
    m_stepCooldown = kAxisCooldown;
    ++m_currentStep;
    beginStep();
}

// See BindingWizard.h for what clearVotes gates. Round-scoped fields (rest streak/HoldA
// reading/this-round move amplitude) are always reset; axis-scoped fields (vote tallies, round
// counter, this axis's slice of m_gyroSamples) only when starting a genuinely fresh axis.
void BindingWizard::resetGyroRoundState(bool clearVotes) {
    m_gyroRoundStage = GyroRoundStage::MoveToA;
    m_gyroRestA.clear();
    m_gyroRestStreak.clear();
    m_gyroRestStreakFrames = 0;
    m_gyroLegMoveAmp.clear();
    m_gyroRoundMoveAmp.clear();
    m_gyroMoveASignMean.clear();
    if (clearVotes) {
        m_gyroRound = 1;
        m_gyroGyroVotes.clear();
        m_gyroAccelVotes.clear();
        m_gyroBaselineMisses = 0;
        m_gyroBaselineContaminated = 0;
        m_gyroPhaseFrames = 0;
        int idx = static_cast<int>(m_gyroPhase);
        for (auto& offsetStats : m_gyroSamples) offsetStats[idx] = GyroOffsetStats{};
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
        // Re-entering a completed gyro step: start the whole capture over from scratch.
        m_gyroPhase = GyroPhase::Baseline;
        m_gyroSamples.clear();
        m_gyroPhaseFrames = 0;
        m_gyroPhaseStarted = false;
        m_gyroAwaitingRelease = true;
        m_gyroResult = ImuCalibrationResult{};
        m_gyroCandidates.clear();
        m_gyroHasAccel     = false;
        m_gyroNormalOffset = -1;
        m_gyroPoolFailed   = false;
        for (int i = 0; i < 3; ++i) { m_gyroAxisGyroOffset[i] = -1; m_gyroAxisAccelOffset[i] = -1; }
        resetGyroRoundState(/*clearVotes=*/true);
    } else if (t == "touch_surface") {
        // Re-entering a completed/failed touch_surface step: start the whole discovery over.
        resetTouchSurfaceState();
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

// Shared "Continuar" control for the gyro and touch_surface sub-phases: draws the button
// (disabled until canAdvance) and, when acceptAnyButton is true, also accepts any controller
// button press (other than excludePhysIndex) as a confirm, so the user never has to let go of the
// controller to reach for the mouse. Returns true once confirmed.
//
// excludePhysIndex matters for touch_surface's RangeX/RangeY phases: unlike the gyro gesture
// (which never touches a button by itself), sliding a finger toward the touchpad's edge can
// trigger its own physical click — without excluding it, that accidental click would end the
// phase the instant it fires, well before the finger actually reaches the true edge (confirmed
// for real 2026/08/30: max_x/max_y came in short — 1893/900 instead of 1919/942). Passing the
// touchpad's own physIndex here still lets any OTHER button confirm normally.
bool BindingWizard::renderPhaseAdvanceControl(bool canAdvance, bool acceptAnyButton, int excludePhysIndex) {
    if (!canAdvance) ImGui::BeginDisabled();
    bool clicked = ImGui::Button(trid("wizard.gyro_continue", "bind").c_str(), { 180.0f, 0.0f });
    if (!canAdvance) ImGui::EndDisabled();

    // Always poll captureButton (not just once canAdvance) so a button already held during the
    // gesture doesn't read as a fresh press the instant the threshold flips. Polled even when
    // acceptAnyButton is false, to keep m_prevButtonMask in sync either way.
    int  idx = 0;
    bool pressedAnyButton = captureButton(idx);
    if (pressedAnyButton && excludePhysIndex >= 0 && idx == excludePhysIndex) pressedAnyButton = false;
    return clicked || (canAdvance && acceptAnyButton && pressedAnyButton);
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

bool BindingWizard::sampleGyroFrame(std::vector<float>* outRaw) {
    if (!m_hidReader || !m_hidReader->isOpen()) return false;
    RawHIDState s{};
    bool readOk = m_hidReader->read(s);
    if (!s.valid || s.raw.size() < 2) {
        ++m_gyroBaselineMisses;
        if (m_gyroPhase == GyroPhase::Baseline && (m_gyroBaselineMisses % 60 == 1)) {
            spdlog::trace("[GyroCal] baseline stall: misses={} readOk={} valid={} rawSize={}",
                          m_gyroBaselineMisses, readOk, s.valid, s.raw.size());
        }
        return false;
    }

    // Reject frames where a declared HID axis (stick/trigger) drifted from its Baseline-start
    // value — likely an accidental touch during the gesture, not the gyro/accel itself.
    for (int i = 0; i < 8; ++i) {
        if (std::abs(kHIDAxisValues(s, i) - kHIDAxisValues(m_gyroAxisBaseline, i)) > kGyroAxisContamination) {
            ++m_gyroBaselineContaminated;
            if (m_gyroPhase == GyroPhase::Baseline && (m_gyroBaselineContaminated % 60 == 1)) {
                spdlog::trace("[GyroCal] baseline stall: contaminated={} axis={} delta={:.3f}",
                              m_gyroBaselineContaminated, i,
                              kHIDAxisValues(s, i) - kHIDAxisValues(m_gyroAxisBaseline, i));
            }
            return false;
        }
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

    if (outRaw) *outRaw = std::move(v);
    return true;
}

// Runs the old Step 1-3 once, right when Baseline commits: find the longest run of alive
// offsets, trim to 6/3, pick the gravity ("normal") axis. See BindingWizard.h for what each
// output field feeds. Leaves m_gyroCandidates empty if no usable run was found (bestLen < 3).
void BindingWizard::computeGyroCandidatePool() {
    m_gyroCandidates.clear();
    m_gyroHasAccel     = false;
    m_gyroNormalOffset = -1;

    const int n = static_cast<int>(m_gyroSamples.size());
    if (n == 0) return;

    std::vector<bool> alive = computeAliveOffsets();

    // Longest run of alive offsets spaced 2 bytes apart. See the historical comment that used to
    // live here (now in classifyGyro()'s Step 5/6 block) for why the LONGEST run is taken instead
    // of the first one.
    int bestStart = -1, bestLen = 0;
    for (int s = 0; s < n; ++s) {
        if (!alive[s]) continue;
        if (s >= 2 && alive[s - 2]) continue; // not the start of a run, already counted
        int len = 0;
        while (s + len * 2 < n && alive[s + len * 2]) ++len;
        if (len > bestLen) { bestLen = len; bestStart = s; }
    }
    if (bestLen < 3) {
        spdlog::trace("[GyroCal] pool FAILED: no run of >=3 alive offsets found (longest={})", bestLen);
        return;
    }

    auto baselineAmp = [&](int o) -> float {
        if (o < 0 || o >= n) return -1.0f;
        const GyroOffsetStats& b = m_gyroSamples[o][static_cast<int>(GyroPhase::Baseline)];
        return (b.count > 0) ? (b.maxV - b.minV) : -1.0f;
    };

    // A hardware IMU block has a FIXED length (3 gyro-only or 6 gyro+accel) — it doesn't
    // organically shrink between captures of the SAME device. A borderline offset's baseline
    // peak-to-peak can land just above kGyroBaselineNoiseFloor on an unlucky capture (confirmed
    // for real on the DS4: offset 21 measured 288.0 one capture, 957.0 another, straddling the
    // 800 cutoff both times) and get marked "not alive", silently costing the whole calibration
    // its accelerometer (or, for a gyro-only device, an entire axis). Bridge a SINGLE immediate
    // neighbor when the run is exactly one short of 3 or 6, but only up to
    // kGyroBorderlineNoiseFloor — a genuinely dead byte (CRC/counter, changing by design every
    // frame) reads far higher than that, so this doesn't risk pulling in real noise, just
    // rescuing a real sensor channel that had a noisy moment.
    if (bestLen == 2 || bestLen == 5) {
        int before = bestStart - 2;
        int after  = bestStart + bestLen * 2;
        float ampAfter  = baselineAmp(after);
        float ampBefore = baselineAmp(before);
        if (ampAfter >= 0.0f && ampAfter < kGyroBorderlineNoiseFloor) {
            spdlog::trace("[GyroCal] pool: bridged borderline offset={} (amp={:.1f}) to complete the block", after, ampAfter);
            ++bestLen;
        } else if (ampBefore >= 0.0f && ampBefore < kGyroBorderlineNoiseFloor) {
            spdlog::trace("[GyroCal] pool: bridged borderline offset={} (amp={:.1f}) to complete the block", before, ampBefore);
            bestStart = before;
            ++bestLen;
        }
    }

    // A run longer than 6 has a spurious extra offset stuck to one end (e.g. a slowly
    // incrementing counter byte) — trim from whichever end has the smallest Baseline amplitude
    // deviation... actually: trim from whichever end reacts LESS overall. Reactivity isn't known
    // yet at this point (Roll/Pitch/Yaw haven't been captured), so trim using Baseline amplitude
    // alone: the spurious byte is flatter (closer to constant) than a real sensor's noise floor.
    while (bestLen > 6) {
        int headOffset = bestStart;
        int tailOffset = bestStart + (bestLen - 1) * 2;
        if (baselineAmp(headOffset) < baselineAmp(tailOffset)) bestStart += 2;
        --bestLen;
    }

    m_gyroHasAccel = bestLen >= 6;
    int runLen = m_gyroHasAccel ? 6 : bestLen;
    std::vector<int> blockOffsets;
    for (int k = 0; k < runLen; ++k) blockOffsets.push_back(bestStart + k * 2);

    // Pull out the "normal" accel axis: by far the largest steady baseline magnitude (gravity).
    if (m_gyroHasAccel) {
        auto baselineMean = [&](int o) {
            const GyroOffsetStats& b = m_gyroSamples[o][static_cast<int>(GyroPhase::Baseline)];
            return (b.count > 0) ? std::abs(b.sum / b.count) : 0.0f;
        };
        for (int o : blockOffsets) {
            if (m_gyroNormalOffset < 0 || baselineMean(o) > baselineMean(m_gyroNormalOffset))
                m_gyroNormalOffset = o;
        }
    }

    for (int o : blockOffsets) if (o != m_gyroNormalOffset) m_gyroCandidates.push_back(o);

    spdlog::trace("[GyroCal] pool OK: {} candidates, hasAccel={}, normalOffset={}",
                  m_gyroCandidates.size(), m_gyroHasAccel, m_gyroNormalOffset);
}

// See BindingWizard.h for the reasoning. Called once, right when Flip commits.
void BindingWizard::confirmNormalOffsetFromFlip() {
    if (m_gyroNormalOffset < 0) return; // no accel guess to confirm (hasAccel gated the Flip phase)

    std::vector<int> blockOffsets = m_gyroCandidates;
    blockOffsets.push_back(m_gyroNormalOffset);

    auto phaseMean = [&](int o, GyroPhase p) -> float {
        const GyroOffsetStats& s = m_gyroSamples[o][static_cast<int>(p)];
        return (s.count > 0) ? (s.sum / s.count) : 0.0f;
    };

    // A genuine 180-degree flip inverts the SIGN of gravity's projection while keeping roughly
    // the same magnitude (+g -> -g). A candidate that merely drifted during an imperfect flip
    // (a lateral/frontal accel axis nudged off-level, say) tends to move in only one direction
    // without truly crossing zero with comparable strength on both sides — plain abs(delta)
    // can't tell those apart and picked the wrong offset on a rough flip (2026/08/10 field
    // report). Require an actual sign flip, scored by the WEAKER of the two magnitudes so a
    // one-sided jump can't win just because its big side is huge.
    int   bestOffset = m_gyroNormalOffset;
    float bestScore  = -1.0f;
    for (int o : blockOffsets) {
        float baseMean = phaseMean(o, GyroPhase::Baseline);
        float flipMean = phaseMean(o, GyroPhase::Flip);
        bool  oppositeSign = (baseMean > 0.0f && flipMean < 0.0f) || (baseMean < 0.0f && flipMean > 0.0f);
        if (!oppositeSign) continue;
        float score = std::min(std::abs(baseMean), std::abs(flipMean));
        if (score > bestScore) { bestScore = score; bestOffset = o; }
    }

    // No candidate flipped sign with enough magnitude on both sides to trust (e.g. the user
    // didn't actually flip the controller, or the flip wasn't clean) — keep
    // computeGyroCandidatePool()'s magnitude-only guess rather than risk a worse pick from noise.
    if (bestScore < kGyroMinSignalAmp) {
        spdlog::trace("[GyroCal] flip: no clean sign flip (best score={:.1f} at offset={}), keeping baseline guess normalOffset={}",
                      bestScore, bestOffset, m_gyroNormalOffset);
        return;
    }

    if (bestOffset != m_gyroNormalOffset) {
        spdlog::trace("[GyroCal] flip: reclassified normalOffset {} -> {} (score={:.1f})",
                      m_gyroNormalOffset, bestOffset, bestScore);
    }

    m_gyroNormalOffset = bestOffset;
    m_gyroCandidates.clear();
    for (int o : blockOffsets) if (o != m_gyroNormalOffset) m_gyroCandidates.push_back(o);
}

// An offset is a real sensor channel only if it stayed quiet (low peak-to-peak) during
// Baseline (mando quieto) — discards CRC/packet-counter bytes, which drift or jump even at
// rest. It also must show SOME jitter (amp > 0): a real sensor always has a little electrical
// noise, but a constant header/padding byte reads EXACTLY the same value every single frame
// (amp == 0 exactly) — which otherwise passes the "quiet" half of this test *more* cleanly than
// genuine sensor noise does. Without this, findRun() below picks up the first run of constant
// header bytes (e.g. offsets 0,2,4,6,8,10, all amp==0) instead of the real accel+gyro block,
// and classification fails silently (see BITACORA — first case caught 2026/07/10).
// Called once from computeGyroCandidatePool() when Baseline commits.
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

// Advances the Move/Hold sub-state machine for the current axis by one already-decoded frame.
// `raw` is indexed by raw report offset (same indexing as m_gyroSamples' outer index) — only the
// entries listed in m_gyroCandidates are read.
void BindingWizard::updateGyroRound(const std::vector<float>& raw) {
    const int nc = static_cast<int>(m_gyroCandidates.size());
    if (nc == 0) return;

    bool holding = (m_gyroRoundStage == GyroRoundStage::HoldA || m_gyroRoundStage == GyroRoundStage::HoldB);

    if (!holding) {
        // MoveToA / MoveToB: track this leg's amplitude (gate) and the whole round's amplitude
        // (score, see finishGyroRound()) in parallel.
        if (static_cast<int>(m_gyroLegMoveAmp.size())   != nc) m_gyroLegMoveAmp.assign(nc, {});
        if (static_cast<int>(m_gyroRoundMoveAmp.size()) != nc) m_gyroRoundMoveAmp.assign(nc, {});
        float bestLegAmp = 0.0f;
        for (int c = 0; c < nc; ++c) {
            float val = raw[m_gyroCandidates[c]];
            for (GyroOffsetStats* st : { &m_gyroLegMoveAmp[c], &m_gyroRoundMoveAmp[c] }) {
                st->minV = (st->count == 0) ? val : std::min(st->minV, val);
                st->maxV = (st->count == 0) ? val : std::max(st->maxV, val);
                st->sum += val;
                ++st->count;
            }
            bestLegAmp = std::max(bestLegAmp, m_gyroLegMoveAmp[c].maxV - m_gyroLegMoveAmp[c].minV);
        }
        // Frame floor before an amplitude spike is allowed to end the leg — a gyro's angular
        // rate peaks right as the motion STARTS and can already clear kGyroMinSignalAmp before
        // the controller is anywhere near the intended extreme, while the accelerometer only
        // reflects the new orientation once the motion is over. Without this floor, HoldA/HoldB
        // can end up capturing two positions both still close to center (chased by the gyro's
        // early spike, not the accelerometer's settled value), so holdDelta stays near-zero even
        // with a full, deliberate tilt — same "one lucky early frame" risk the old Fast-phase
        // gate (kGyroFastMinPhaseFrames) already guarded against, lost when that design was
        // replaced. Reuses m_gyroLegMoveAmp[0].count — every candidate accumulates together each
        // frame in this loop, so any one of them tracks frames elapsed in this leg.
        if (m_gyroLegMoveAmp[0].count >= kGyroMoveMinFrames && bestLegAmp > kGyroMinSignalAmp) {
            m_gyroRoundStage = (m_gyroRoundStage == GyroRoundStage::MoveToA)
                ? GyroRoundStage::HoldA : GyroRoundStage::HoldB;
            m_gyroRestStreak.assign(nc, {});
            m_gyroRestStreakFrames = 0;
        }
        return;
    }

    // HoldA / HoldB: rest-streak detector. If adding this frame would push any candidate's
    // peak-to-peak past kGyroBaselineNoiseFloor, the streak is broken — restart it from this
    // single frame instead of extending it.
    if (static_cast<int>(m_gyroRestStreak.size()) != nc) m_gyroRestStreak.assign(nc, {});
    bool broke = false;
    for (int c = 0; c < nc; ++c) {
        const GyroOffsetStats& st = m_gyroRestStreak[c];
        if (st.count == 0) continue;
        float val = raw[m_gyroCandidates[c]];
        if (std::max(st.maxV, val) - std::min(st.minV, val) > kGyroBaselineNoiseFloor) { broke = true; break; }
    }
    if (broke) {
        for (int c = 0; c < nc; ++c) {
            float val = raw[m_gyroCandidates[c]];
            m_gyroRestStreak[c] = GyroOffsetStats{ val, val, val, 1 };
        }
        m_gyroRestStreakFrames = 1;
        return;
    }
    for (int c = 0; c < nc; ++c) {
        float val = raw[m_gyroCandidates[c]];
        GyroOffsetStats& st = m_gyroRestStreak[c];
        st.minV = (st.count == 0) ? val : std::min(st.minV, val);
        st.maxV = (st.count == 0) ? val : std::max(st.maxV, val);
        st.sum += val;
        ++st.count;
    }
    ++m_gyroRestStreakFrames;
    if (m_gyroRestStreakFrames < kGyroRestMinFrames) return;

    // Settled — the streak's mean is the stable reading for this hold.
    std::vector<float> stable(nc);
    for (int c = 0; c < nc; ++c) {
        const GyroOffsetStats& st = m_gyroRestStreak[c];
        stable[c] = (st.count > 0) ? (st.sum / st.count) : 0.0f;
    }

    if (m_gyroRoundStage == GyroRoundStage::HoldA) {
        m_gyroRestA = stable;
        // Snapshot the MoveToA leg's mean raw value per candidate before m_gyroLegMoveAmp resets
        // for the B leg — its sign is what finishGyroRound() uses to type gyro axes (see
        // ImuConfig::gyroXInvert et al.), since a gyro's rest reading (~0 either way) can't tell
        // direction, only the reading WHILE moving toward "direction A" can.
        m_gyroMoveASignMean.assign(nc, 0.0f);
        for (int c = 0; c < nc; ++c) {
            const GyroOffsetStats& leg = m_gyroLegMoveAmp[c];
            m_gyroMoveASignMean[c] = (leg.count > 0) ? (leg.sum / leg.count) : 0.0f;
        }
        m_gyroRoundStage = GyroRoundStage::MoveToB;
        m_gyroLegMoveAmp.assign(nc, {}); // fresh gate tracking for the B leg; m_gyroRoundMoveAmp keeps accumulating
    } else {
        finishGyroRound(stable);
    }
}

// top-second vote count ("ventaja"). Returns 0 (no lead) if fewer than 2 candidates have any
// votes yet — a single candidate with votes and everyone else at 0 still counts as a lead of
// its own vote count, which is exactly what we want (see finishGyroRound()'s 2-vote-lead check).
int BindingWizard::voteLead(const std::vector<int>& votes, int* outTopCandidate) {
    int topIdx = -1, topVotes = -1, secondVotes = 0;
    for (int i = 0; i < (int)votes.size(); ++i) {
        if (votes[i] > topVotes) { secondVotes = topVotes < 0 ? 0 : topVotes; topVotes = votes[i]; topIdx = i; }
        else if (votes[i] > secondVotes) { secondVotes = votes[i]; }
    }
    if (outTopCandidate) *outTopCandidate = topIdx;
    return (topIdx < 0) ? 0 : (topVotes - secondVotes);
}

// Closes out one round: hold-delta typing + vote, then either confirms the axis winner, starts
// another round, or fails the whole gyro step (kGyroMaxRounds reached without a winner). See
// REFERENCE.md, "Wizard de calibracion IMU - 4 diseno de classifyGyro()", for the algorithm this
// implements.
void BindingWizard::finishGyroRound(const std::vector<float>& restB) {
    const int nc = static_cast<int>(m_gyroCandidates.size());
    int axis = static_cast<int>(m_gyroPhase) - static_cast<int>(GyroPhase::Roll); // Roll=0,Pitch=1,Yaw=2
    bool accelApplies = m_gyroHasAccel && m_gyroPhase != GyroPhase::Yaw;

    if (static_cast<int>(m_gyroGyroVotes.size())  != nc) m_gyroGyroVotes.assign(nc, 0);
    if (static_cast<int>(m_gyroAccelVotes.size()) != nc) m_gyroAccelVotes.assign(nc, 0);

    // A candidate already confirmed as an EARLIER axis's accel offset can't win this round's
    // gyro vote. Needed because holdDelta (the accel-vs-gyro safety net below) doesn't apply to
    // Yaw at all (yaw rotation doesn't move gravity, so a hold-delta reading during yaw says
    // nothing about whether a candidate is accel or gyro) — without this, a real accelerometer
    // picking up incidental amplitude during ANY hand motion (pure rotation isn't achievable by
    // hand) can outscore the true yaw-gyro channel on raw move-amplitude alone, the same
    // cross-sensor-scale trap that broke the 3 earlier classifyGyro() designs. Confirmed
    // reproducible: offset 17 (Roll's own accel winner) won yaw's gyro vote in 3/3 real runs,
    // every one, regardless of how carefully the gesture was performed.
    auto alreadyClaimedAsAccel = [&](int rawOffset) {
        for (int i = 0; i < 2; ++i) {
            if (i != axis && m_gyroAxisAccelOffset[i] == rawOffset) return true;
        }
        return false;
    };

    int   bestAccelIdx = -1; float bestAccelDelta = 0.0f;
    int   bestGyroIdx  = -1; float bestGyroAmp    = 0.0f;
    for (int c = 0; c < nc; ++c) {
        float holdDelta = std::abs(restB[c] - m_gyroRestA[c]);
        const GyroOffsetStats& mv = (c < (int)m_gyroRoundMoveAmp.size()) ? m_gyroRoundMoveAmp[c] : GyroOffsetStats{};
        float moveAmp = (mv.count > 0) ? (mv.maxV - mv.minV) : 0.0f;
        spdlog::trace("[GyroCal]   candidate offset={} restA={:.1f} restB={:.1f} holdDelta={:.1f} moveAmp={:.1f}",
                      m_gyroCandidates[c], m_gyroRestA[c], restB[c], holdDelta, moveAmp);

        if (accelApplies && holdDelta > kGyroBaselineNoiseFloor) {
            if (holdDelta > bestAccelDelta) { bestAccelDelta = holdDelta; bestAccelIdx = c; }
        } else if (!alreadyClaimedAsAccel(m_gyroCandidates[c]) && moveAmp > kGyroMinSignalAmp) {
            if (moveAmp > bestGyroAmp) { bestGyroAmp = moveAmp; bestGyroIdx = c; }
        }
    }
    if (bestGyroIdx  >= 0) ++m_gyroGyroVotes[bestGyroIdx];
    if (bestAccelIdx >= 0) ++m_gyroAccelVotes[bestAccelIdx];

    int gyroTop = -1, accelTop = -1;
    bool gyroDone  = voteLead(m_gyroGyroVotes, &gyroTop) >= kGyroRoundVoteLead;
    bool accelDone = !accelApplies || voteLead(m_gyroAccelVotes, &accelTop) >= kGyroRoundVoteLead;

    spdlog::trace("[GyroCal] axis={} round={} gyroVote={} accelVote={} gyroDone={} accelDone={}",
                  axis, m_gyroRound, bestGyroIdx, bestAccelIdx, gyroDone, accelDone);

    if (gyroDone && accelDone) {
        m_gyroAxisGyroOffset[axis] = m_gyroCandidates[gyroTop];
        // Sign: rotating toward "direction A" (see gyro_roll_a/gyro_pitch_a/gyro_yaw_a prompts)
        // should read positive after the fixup — invert when the MoveToA mean came out negative.
        m_gyroAxisGyroInvert[axis] = m_gyroMoveASignMean[gyroTop] < 0.0f;
        if (accelApplies) {
            m_gyroAxisAccelOffset[axis] = m_gyroCandidates[accelTop];
            // Sign: tilting toward "direction A" should read HIGHER than "direction B" after the
            // fixup — invert when the hold reading at A came out lower than at B.
            m_gyroAxisAccelInvert[axis] = m_gyroRestA[accelTop] < restB[accelTop];
        }
        // Advance to the next axis, or finish the whole gyro step if this was Yaw.
        if (m_gyroPhase == GyroPhase::Yaw) {
            m_gyroResult = classifyGyro();
            finishGyroStep();
        } else {
            m_gyroPhase = static_cast<GyroPhase>(static_cast<int>(m_gyroPhase) + 1);
            resetGyroRoundState(/*clearVotes=*/true);
            m_gyroPhaseStarted = false;
            m_gyroAwaitingRelease = true;
            snapshotBaseline();
        }
    } else if (m_gyroRound >= kGyroMaxRounds) {
        spdlog::trace("[GyroCal] axis={} classify FAILED: no winner after {} rounds", axis, kGyroMaxRounds);
        m_gyroResult = ImuCalibrationResult{};
        finishGyroStep();
    } else {
        ++m_gyroRound;
        resetGyroRoundState(/*clearVotes=*/false); // votes (m_gyroGyroVotes/m_gyroAccelVotes) intentionally kept
    }
}

// Confidence (0..1) for the axis currently being captured: how close its vote tally is to a
// confirmed winner. 0 until at least 2 rounds have completed (round 1 alone can't show
// agreement or disagreement yet); after that, the lead over the runner-up as a fraction of
// kGyroRoundVoteLead. If this axis also votes accel (Roll/Pitch, see finishGyroRound()), the
// displayed confidence is the MINIMUM of the two tallies — whichever hasn't converged is the
// bottleneck holding the axis back.
float BindingWizard::axisConfidence() const {
    if (m_gyroRound < 2) return 0.0f;
    bool accelApplies = m_gyroHasAccel && m_gyroPhase != GyroPhase::Yaw;
    float gyroConf = std::clamp(static_cast<float>(voteLead(m_gyroGyroVotes)) / kGyroRoundVoteLead, 0.0f, 1.0f);
    if (!accelApplies) return gyroConf;
    float accelConf = std::clamp(static_cast<float>(voteLead(m_gyroAccelVotes)) / kGyroRoundVoteLead, 0.0f, 1.0f);
    return std::min(gyroConf, accelConf);
}

// Step 5/6 of the original design (unchanged): within each already-typed pool, assign axes by
// greedy elimination — strongest (candidate, slot) pair first, claimed candidates removed from
// later passes. Resolves the case where the SAME offset won the round-vote for two different
// axes (real, correlated motion can make e.g. roll's gyro also react somewhat during pitch) by
// letting the axis it scores higher for keep it.
BindingWizard::ImuCalibrationResult BindingWizard::classifyGyro() const {
    ImuCalibrationResult result;
    if (m_gyroCandidates.empty()) return result;

    auto amp = [](const GyroOffsetStats& s) { return (s.count > 0) ? (s.maxV - s.minV) : 0.0f; };

    // Build the deduplicated gyro/accel pools from each axis's round-vote winner.
    std::vector<int> gyroPool, accelPool;
    for (int offset : m_gyroAxisGyroOffset) {
        if (offset >= 0 && std::find(gyroPool.begin(), gyroPool.end(), offset) == gyroPool.end())
            gyroPool.push_back(offset);
    }
    for (int i = 0; i < 2; ++i) { // only Roll(0)/Pitch(1) vote accel — see m_gyroAxisAccelOffset
        int offset = m_gyroAxisAccelOffset[i];
        if (offset >= 0 && std::find(accelPool.begin(), accelPool.end(), offset) == accelPool.end())
            accelPool.push_back(offset);
    }

    // A candidate can win one axis's gyro vote AND a different axis's accel vote (each axis
    // votes independently, unlike the old design's single self-relative typing pass) — real
    // motion isn't perfectly isolated per axis, so a genuine accel offset can pick up enough
    // amplitude during another axis's move to win that axis's gyro vote too. Don't guess which
    // role is right: drop it from BOTH pools, same "ambiguous, don't guess" rule used everywhere
    // else here. The resulting undersized pool fails the size check right below.
    std::vector<int> conflicting;
    for (int o : gyroPool)
        if (std::find(accelPool.begin(), accelPool.end(), o) != accelPool.end())
            conflicting.push_back(o);
    for (int o : conflicting) {
        gyroPool.erase(std::remove(gyroPool.begin(), gyroPool.end(), o), gyroPool.end());
        accelPool.erase(std::remove(accelPool.begin(), accelPool.end(), o), accelPool.end());
        spdlog::trace("[GyroCal] offset={} won both a gyro vote and an accel vote (different axes) - dropped from both pools", o);
    }

    if (gyroPool.size() != 3 || (m_gyroHasAccel && accelPool.size() != 2)) {
        spdlog::trace("[GyroCal] classify FAILED: {} distinct gyro winners / {} distinct accel winners (expected 3 / {})",
                      gyroPool.size(), accelPool.size(), m_gyroHasAccel ? 2 : 0);
        return result;
    }

    struct Slot { int* target; GyroPhase phase; };
    auto assignSlots = [&](std::vector<Slot>& slots, const std::vector<int>& pool) {
        std::vector<bool> claimed(pool.size(), false);
        std::vector<bool> filled(slots.size(), false);
        for (size_t pass = 0; pass < slots.size(); ++pass) {
            int bestCandidate = -1, bestSlot = -1; float bestScore = -1.0f;
            for (size_t sIdx = 0; sIdx < slots.size(); ++sIdx) {
                if (filled[sIdx]) continue;
                int phaseIdx = static_cast<int>(slots[sIdx].phase);
                for (size_t cIdx = 0; cIdx < pool.size(); ++cIdx) {
                    if (claimed[cIdx]) continue;
                    float a = amp(m_gyroSamples[pool[cIdx]][phaseIdx]);
                    if (a > bestScore) { bestScore = a; bestCandidate = (int)cIdx; bestSlot = (int)sIdx; }
                }
            }
            if (bestCandidate < 0) break;
            *slots[bestSlot].target = pool[bestCandidate];
            claimed[bestCandidate] = true;
            filled[bestSlot] = true;
        }
    };

    int rollOffset = -1, pitchOffset = -1, yawOffset = -1;
    std::vector<Slot> gyroSlots = {
        { &rollOffset,  GyroPhase::Roll },
        { &pitchOffset, GyroPhase::Pitch },
        { &yawOffset,   GyroPhase::Yaw },
    };
    assignSlots(gyroSlots, gyroPool);
    if (rollOffset < 0 || pitchOffset < 0 || yawOffset < 0) {
        spdlog::trace("[GyroCal] classify FAILED: roll={} pitch={} yaw={} (ambiguous gyro slot match)",
                      rollOffset, pitchOffset, yawOffset);
        return result;
    }

    int lateralOffset = -1, frontalOffset = -1;
    if (m_gyroHasAccel) {
        std::vector<Slot> accelSlots = {
            { &lateralOffset, GyroPhase::Roll },
            { &frontalOffset, GyroPhase::Pitch },
        };
        assignSlots(accelSlots, accelPool);
        if (lateralOffset < 0 || frontalOffset < 0) {
            spdlog::trace("[GyroCal] classify FAILED: lateral={} frontal={} (ambiguous accel slot match)",
                          lateralOffset, frontalOffset);
            return result;
        }
    }

    // Axis naming convention already used by the DS4/Pro3 entries in controllers.json:
    // gyroX = pitch, gyroY = yaw, gyroZ = roll.
    result.gyroXOffset = pitchOffset;
    result.gyroYOffset = yawOffset;
    result.gyroZOffset = rollOffset;

    // Invert flags travel with their raw offset, not with the axis index — assignSlots() above
    // can hand a candidate to a different axis than the one whose round-vote originally won it
    // (see the comment on assignSlots' caller). Map offset -> invert per sensor so each final
    // slot picks up the sign that was actually measured for that offset.
    std::unordered_map<int, bool> gyroInvertByOffset, accelInvertByOffset;
    for (int i = 0; i < 3; ++i)
        if (m_gyroAxisGyroOffset[i] >= 0) gyroInvertByOffset[m_gyroAxisGyroOffset[i]] = m_gyroAxisGyroInvert[i];
    for (int i = 0; i < 2; ++i)
        if (m_gyroAxisAccelOffset[i] >= 0) accelInvertByOffset[m_gyroAxisAccelOffset[i]] = m_gyroAxisAccelInvert[i];

    result.gyroXInvert = gyroInvertByOffset[pitchOffset];
    result.gyroYInvert = gyroInvertByOffset[yawOffset];
    result.gyroZInvert = gyroInvertByOffset[rollOffset];

    if (m_gyroHasAccel) {
        result.accelZOffset = m_gyroNormalOffset;
        result.accelXOffset = lateralOffset;
        result.accelYOffset = frontalOffset;
        result.accelXInvert = accelInvertByOffset[lateralOffset];
        result.accelYInvert = accelInvertByOffset[frontalOffset];
        // AccelZ (gravity/normal) has no directional gesture of its own — its polarity is read
        // straight from the Baseline posture ("top of the controller facing the ceiling" is
        // documented as the positive reference, see wizard.gyro_baseline).
        const GyroOffsetStats& normalBaseline = m_gyroSamples[m_gyroNormalOffset][static_cast<int>(GyroPhase::Baseline)];
        float normalMean = (normalBaseline.count > 0) ? (normalBaseline.sum / normalBaseline.count) : 0.0f;
        result.accelZInvert = normalMean < 0.0f;
        spdlog::trace("[GyroCal] classify OK: gyroX(pitch)={} gyroY(yaw)={} gyroZ(roll)={} accelX(lateral)={} accelY(frontal)={} accelZ(normal)={}",
                      pitchOffset, yawOffset, rollOffset, lateralOffset, frontalOffset, m_gyroNormalOffset);
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
        imu["gyro_scale"]     = 1.0f / 32768.0f;
        imu["gyro_x_offset"]  = m_gyroResult.gyroXOffset;
        imu["gyro_y_offset"]  = m_gyroResult.gyroYOffset;
        imu["gyro_z_offset"]  = m_gyroResult.gyroZOffset;
        imu["gyro_x_invert"]  = m_gyroResult.gyroXInvert;
        imu["gyro_y_invert"]  = m_gyroResult.gyroYInvert;
        imu["gyro_z_invert"]  = m_gyroResult.gyroZInvert;
        if (m_gyroResult.accelXOffset >= 0) {
            imu["accel_scale"]    = 1.0f / 32768.0f;
            imu["accel_x_offset"] = m_gyroResult.accelXOffset;
            imu["accel_y_offset"] = m_gyroResult.accelYOffset;
            imu["accel_z_offset"] = m_gyroResult.accelZOffset;
            imu["accel_x_invert"] = m_gyroResult.accelXInvert;
            imu["accel_y_invert"] = m_gyroResult.accelYInvert;
            imu["accel_z_invert"] = m_gyroResult.accelZInvert;
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

    // Replace existing entry matching VID+PID (+connection/product_name as tie-breakers), or
    // append. Mirrors the same scoring approach as ConfigLoader::findConfig() (the runtime
    // device→config picker), for the same reason: VID+PID alone isn't always a unique key.
    // 8BitDo Pro 2 (X-mode) and 8BitDo Zero 2 (X-mode) share VID 045E/PID 02E0 — only
    // product_name tells them apart (see controllers.json, both entries) — so product_name
    // has to stay part of this match, same as findConfig() already relies on for runtime
    // device recognition.
    //
    // source_name is deliberately NOT part of it: it's just a display label (device-reported
    // or user-typed), not a device identity. Requiring it to match too caused real duplicate
    // entries — the DS4's hardware-reported name ("Wireless Controller") doesn't match the
    // hand-curated source_name ("Dualshock 4") from before the wizard existed, so every
    // recalibration kept creating a new entry instead of overwriting it
    // (BUG-WIZARD-DUPLICATE-ENTRY, confirmed for real 2026/07/10 and again 2026/07/11). The
    // merge below still updates source_name to whatever the wizard captured, same as any other
    // wizard-managed field.
    //
    // Rule per discriminator (connection, product_name): if BOTH sides declare it and it
    // differs → this entry can't be a match, skip entirely. If only one side declares it (or
    // neither) → not a blocker, but also doesn't count toward the score. Among all surviving
    // candidates, the one with the highest score (most discriminators that actually matched)
    // wins — this is what lets the Pro2/Zero2 collision resolve by elimination: the Zero2
    // entry's product_name check fails outright against a real Pro2 device (so it's skipped),
    // leaving the product_name-less Pro2 entry as the only surviving candidate.
    std::string newConn        = ctrl.connectionType;
    std::string newProductName = ctrl.productName;
    json* bestMatch = nullptr;
    int   bestScore = -1;
    for (auto& e : root["controllers"]) {
        if (e.value("vid","") != std::string(vidStr) || e.value("pid","") != std::string(pidStr))
            continue;
        std::string eConn    = e.value("connection","");
        std::string eProduct = e.value("product_name","");
        if (!eConn.empty()    && !newConn.empty()        && eConn    != newConn)        continue;
        if (!eProduct.empty() && !newProductName.empty() && eProduct != newProductName) continue;
        int score = (!eConn.empty()    && eConn    == newConn        ? 2 : 0)
                  + (!eProduct.empty() && eProduct == newProductName ? 2 : 0);
        if (score > bestScore) { bestScore = score; bestMatch = &e; }
    }
    if (bestMatch) {
        // Preserve fields the wizard doesn't manage (e.g. "touchpad", "_hid_prototype") by
        // starting from the old entry and overwriting only wizard-managed keys.
        for (auto& [k, v] : entry.items())
            (*bestMatch)[k] = v;
    } else {
        root["controllers"].push_back(entry);
    }

    // Touch surface discovery — deliberately NOT folded into `entry` above. Unlike "imu" (which
    // the merge loop just overwrote wholesale), "touchpad" also carries surface_mode/
    // analog_stick_target/zone_template_id/zones set by the Mapeador outside the wizard — a
    // wholesale overwrite here would silently wipe them. Only touch the 3 keys this step actually
    // discovered, on whichever json object this entry ended up being (existing match or the one
    // just appended).
    if (m_hasTouchSurfaceStep && m_touchSurfaceResult.ok) {
        json& target = bestMatch ? *bestMatch : root["controllers"].back();
        auto& tp = target["touchpad"];
        tp["enabled"]     = true;
        tp["data_offset"] = m_touchSurfaceResult.dataOffset;
        tp["max_x"]       = m_touchSurfaceResult.maxX;
        tp["max_y"]       = m_touchSurfaceResult.maxY;
    }

    // Write back
    std::ofstream f(m_controllersPath);
    if (f.is_open()) {
        f << root.dump(2);
        m_state     = State::Idle;
        m_savedFlag = true;
    }
}
