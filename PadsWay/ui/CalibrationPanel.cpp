#define NOMINMAX
#include "CalibrationPanel.h"
#include "../config/Strings.h"
#include "../Paths.h"
#include "../imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
// Solid arc between two angles (radians, 0 = straight up, positive = clockwise) at a fixed
// radius around center — used for the gyro compass's yaw sector, the one axis that has no
// absolute orientation to show as a ball position (see PhysicalAccel's comment in
// ComponentTypes.h: pure rotation about the vertical axis doesn't tilt the case).
void drawArc(ImDrawList* dl, ImVec2 center, float radius, float angleFrom, float angleTo,
            ImU32 col, float thickness) {
    constexpr int kSegments = 24;
    ImVec2 prev = { center.x + radius * sinf(angleFrom), center.y - radius * cosf(angleFrom) };
    for (int i = 1; i <= kSegments; ++i) {
        float a   = angleFrom + (angleTo - angleFrom) * (float)i / (float)kSegments;
        ImVec2 cur = { center.x + radius * sinf(a), center.y - radius * cosf(a) };
        dl->AddLine(prev, cur, col, thickness);
        prev = cur;
    }
}

// Shared setup for a compass widget (renderGyroCompass/renderAccelCompass): places the
// InvisibleButton, then reads back its active/activated state and the mouse offset from center
// along both axes. Both render functions call this before building their own candidate list
// (gyro's adds the yaw arc, so it can't be folded into this shared struct).
struct CompassSetup {
    ImVec2 center;
    bool   active;
    bool   activated;
    ImVec2 mouse;
    float  mVOff, mHOff;
    bool   onVAxis, onHAxis;
};

CompassSetup beginCompassWidget(const char* buttonId, float radius, float pad, float axisTol) {
    CompassSetup s{};
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    s.center = { canvasPos.x + radius + pad, canvasPos.y + radius + pad };
    float diameter = (radius + pad) * 2.0f;

    ImGui::InvisibleButton(buttonId, { diameter, diameter });
    s.active    = ImGui::IsItemActive();
    s.activated = ImGui::IsItemActivated();

    s.mouse   = ImGui::GetIO().MousePos;
    s.mVOff   = std::fabs(s.mouse.y - s.center.y);
    s.mHOff   = std::fabs(s.mouse.x - s.center.x);
    s.onVAxis = s.mHOff <= axisTol;
    s.onHAxis = s.mVOff <= axisTol;
    return s;
}

// Applies one drag update to a linear axis (deadzone if isInner, else max) — the toValue+clamp
// math shared by the vertical and horizontal axes of both compass widgets.
void applyAxisDrag(bool isInner, float offsetPx, float radius, float outerCeiling,
                   float& deadzone, float& max) {
    float v = std::round(std::clamp((offsetPx / radius) * outerCeiling, 0.0f, outerCeiling) * 100.0f) / 100.0f;
    if (isInner)
        deadzone = std::clamp(v, 0.0f, max - 0.02f > 0.0f ? max - 0.02f : 0.0f);
    else
        max = std::clamp(v, deadzone + 0.02f, outerCeiling);
}

// The two concentric rings ("our 1.0", ceiling edge) and the crosshair guide lines behind every
// compass widget.
void drawCompassRingAndGuides(ImDrawList* dl, ImVec2 center, float radius, float outerCeiling) {
    dl->AddCircle(center, radius / outerCeiling, IM_COL32(90, 100, 120, 140), 48, 1.0f);  // "nuestro 1"
    dl->AddCircle(center, radius, IM_COL32(70, 75, 85, 90), 48, 1.0f);                     // ceiling edge

    dl->AddLine({ center.x, center.y - radius }, { center.x, center.y + radius },
               IM_COL32(90, 100, 120, 90), 1.0f);
    dl->AddLine({ center.x - radius, center.y }, { center.x + radius, center.y },
               IM_COL32(90, 100, 120, 90), 1.0f);
}

// Draws the paired inner/outer tick marks (deadzone/max) for one linear axis of a compass —
// vertical (ticks drawn as horizontal marks above/below center, e.g. pitch/Y) or horizontal
// (ticks drawn as vertical marks left/right of center, e.g. roll/X). Shared by both axes of both
// renderGyroCompass and renderAccelCompass.
void drawAxisTicks(ImDrawList* dl, ImVec2 center, bool vertical, float deadzone, float max,
                   float radius, float outerCeiling, bool innerActive, bool outerActive) {
    auto toPx = [&](float v) { return radius * (v / outerCeiling); };
    ImU32 inCol  = innerActive ? IM_COL32(210, 210, 220, 255) : IM_COL32(160, 160, 170, 220);
    ImU32 outCol = outerActive ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    auto tick = [&](float off, ImU32 col, float w) {
        if (vertical) {
            dl->AddLine({ center.x - 9.0f, center.y + off }, { center.x + 9.0f, center.y + off }, col, w);
            dl->AddCircleFilled({ center.x, center.y + off }, 3.5f, col);  // dot marker — easier to spot
        } else {
            dl->AddLine({ center.x + off, center.y - 9.0f }, { center.x + off, center.y + 9.0f }, col, w);
            dl->AddCircleFilled({ center.x + off, center.y }, 3.5f, col);
        }
    };
    tick(-toPx(deadzone), inCol,  innerActive ? 3.0f : 2.0f);
    tick( toPx(deadzone), inCol,  innerActive ? 3.0f : 2.0f);
    tick(-toPx(max),      outCol, outerActive ? 3.0f : 2.0f);
    tick( toPx(max),      outCol, outerActive ? 3.0f : 2.0f);
}

// The live-reading ball (paired axes: pitch/roll or accelY/accelX), colored by whether either
// axis is currently outside its max or inside its deadzone.
void drawCompassBall(ImDrawList* dl, ImVec2 center, float rawV, float rawH,
                     float vDeadzone, float vMax, float hDeadzone, float hMax,
                     float radius, float outerCeiling) {
    auto toPx = [&](float v) { return radius * (v / outerCeiling); };
    bool vOut = std::fabs(rawV) > vMax, hOut = std::fabs(rawH) > hMax;
    bool vIn  = std::fabs(rawV) < vDeadzone, hIn = std::fabs(rawH) < hDeadzone;
    ImU32 ballCol = (vOut || hOut) ? IM_COL32(255, 140, 60, 230)
                   : (vIn && hIn)  ? IM_COL32(140, 140, 150, 220)
                                   : IM_COL32(90, 230, 120, 230);
    ImVec2 ball = { center.x + toPx(rawH), center.y - toPx(rawV) };
    dl->AddCircleFilled({ ball.x + 1.5f, ball.y + 2.5f }, 7.0f, IM_COL32(0, 0, 0, 80));  // shadow
    dl->AddCircleFilled(ball, 7.0f, ballCol);
    dl->AddCircle(ball, 7.0f, IM_COL32(20, 20, 25, 200), 16, 1.5f);
}

// Sets up the fixed-width invert-checkbox column to the LEFT of the axis readout lines that
// follow (not after them with SameLine — SameLine's position depends on the text's own variable
// width, so the checkbox visibly shifted every time a number changed digits, same root cause as
// the compass-column jitter fixed earlier) and draws the "Invertir" header above it. Returns the
// text column's fixed x position, to pass to drawInvertRow.
float beginInvertColumn() {
    constexpr float kInvertBlockRise = 10.0f;  // 3 tightly-packed rows otherwise sit lower than
                                               // the stick widget's shorter block below its ring
    float textColX = ImGui::GetCursorScreenPos().x;
    ImGui::SetCursorScreenPos({ textColX, ImGui::GetCursorScreenPos().y - kInvertBlockRise });
    ImGui::TextDisabled("%s", tr("calibration.invert"));
    return textColX;
}

// One row: an invert checkbox in the fixed column at textColX, then the printf-formatted axis
// readout text to its right.
template <typename... Args>
void drawInvertRow(float textColX, const char* checkboxId, bool& invert, const char* fmt, Args... args) {
    constexpr float kCheckboxColWidth = 30.0f;
    ImVec2 rowPos = ImGui::GetCursorScreenPos();
    ImGui::Checkbox(checkboxId, &invert);
    ImGui::SetCursorScreenPos({ textColX + kCheckboxColWidth, rowPos.y });
    ImGui::Text(fmt, args...);
}
}  // namespace

CalibrationPanel::CompassHandle CalibrationPanel::pickCompassHandle(const CompassCandidate* candidates,
                                                                     size_t count, float hitTol) {
    CompassHandle best = CompassHandle::None;
    float bestD = hitTol;
    for (size_t i = 0; i < count; ++i)
        if (candidates[i].valid && candidates[i].d <= bestD) { bestD = candidates[i].d; best = candidates[i].h; }
    return best;
}

void CalibrationPanel::init(PadEngine* engine) {
    m_engine = engine;
}

void CalibrationPanel::setConfigs(const std::vector<ControllerConfig>& configs) {
    m_configs = configs;
}

void CalibrationPanel::activate() {
    m_active = true;
    reload();
}

CalibrationPanel::AxisInvertRef CalibrationPanel::findAxisInvert(const ControllerConfig& cfg,
                                                                  const char* target) {
    AxisInvertRef ref;
    for (const auto& [hidKey, mapping] : cfg.axes) {
        if (mapping.target == target) {
            ref.hidKey         = hidKey;
            ref.invert         = mapping.invert;
            ref.originalInvert = mapping.invert;
            break;
        }
    }
    return ref;
}

void CalibrationPanel::reload() {
    DeviceCandidate dev = m_engine->getActiveDevice();
    const ControllerConfig* cfg =
        findConfig(m_configs, dev.vid, dev.pid, dev.connectionType, "", dev.name);

    m_hasActiveConfig = (cfg != nullptr);
    if (cfg) {
        m_activeConfig     = *cfg;
        m_activeDeviceName = dev.name;
        m_editLeftStick    = cfg->leftStickCalib;
        m_editRightStick   = cfg->rightStickCalib;
        m_editTriggerL     = cfg->triggerLCalib;
        m_editTriggerR     = cfg->triggerRCalib;
        m_editImu          = cfg->imu;
        m_editTouch        = cfg->touchpad;
        m_leftXInvertRef   = findAxisInvert(*cfg, "left_x");
        m_leftYInvertRef   = findAxisInvert(*cfg, "left_y");
        m_rightXInvertRef  = findAxisInvert(*cfg, "right_x");
        m_rightYInvertRef  = findAxisInvert(*cfg, "right_y");
    }
    m_saveError.clear();
}

void CalibrationPanel::save() {
    m_saveError.clear();
    try {
        std::vector<std::pair<std::string, bool>> axisInverts;
        for (const AxisInvertRef* ref : { &m_leftXInvertRef, &m_leftYInvertRef,
                                          &m_rightXInvertRef, &m_rightYInvertRef })
            if (!ref->hidKey.empty()) axisInverts.emplace_back(ref->hidKey, ref->invert);

        saveCalibration(Paths::userData("data/controllers.json"), m_activeConfig.source_name,
                        m_editLeftStick, m_editRightStick, m_editTriggerL, m_editTriggerR,
                        m_editImu, m_editTouch, axisInverts);
        m_activeConfig.leftStickCalib  = m_editLeftStick;
        m_activeConfig.rightStickCalib = m_editRightStick;
        m_activeConfig.triggerLCalib   = m_editTriggerL;
        m_activeConfig.triggerRCalib   = m_editTriggerR;
        m_activeConfig.imu             = m_editImu;
        m_activeConfig.touchpad        = m_editTouch;
        for (AxisInvertRef* ref : { &m_leftXInvertRef, &m_leftYInvertRef,
                                    &m_rightXInvertRef, &m_rightYInvertRef }) {
            if (ref->hidKey.empty()) continue;
            m_activeConfig.axes[ref->hidKey].invert = ref->invert;
            ref->originalInvert = ref->invert;
        }
        m_calibrationSaved = true;
        m_toastMsg  = tr("calibration.toast_saved");
        m_toastTime = GetTickCount64();
    } catch (const std::exception&) {
        m_saveError = tr("calibration.save_error");
    }
}

void CalibrationPanel::renderStickWidget(const char* label, const char* idSuffix,
                                         float rawX, float rawY, StickCalibration& calib,
                                         HandleDrag& drag, AxisInvertRef& xInvert,
                                         AxisInvertRef& yInvert) {
    ImGui::PushID(idSuffix);
    ImGui::Text("%s", label);

    constexpr float kRadius   = 100.0f;  // 0..1 spans a full 100px so 0.01 steps stay draggable
    constexpr float kPad      = 10.0f;
    constexpr float kHitTol   = 10.0f;   // px band around a ring that grabs it on click
    // Deadzone can be legitimately 0 (no dead center), which collapses the ring to a single
    // point — invisible and impossible to find with the mouse. Floor it visually/for hit-testing
    // so there's always a small ring marking where the handle is.
    constexpr float kMinInnerR = 4.0f;
    const float     kDiameter = (kRadius + kPad) * 2.0f;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 center    = { canvasPos.x + kRadius + kPad, canvasPos.y + kRadius + kPad };

    ImGui::InvisibleButton("##ring_canvas", { kDiameter, kDiameter });
    bool active = ImGui::IsItemActive();

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float  mDx   = mouse.x - center.x;
    float  mDy   = mouse.y - center.y;
    float  mDist = std::sqrt(mDx * mDx + mDy * mDy);

    if (ImGui::IsItemActivated()) {
        float innerR = std::max(kRadius * calib.deadzone, kMinInnerR);
        float outerR = kRadius * calib.max;
        float dInner = std::fabs(mDist - innerR);
        float dOuter = std::fabs(mDist - outerR);
        if (dInner <= kHitTol && dInner <= dOuter)      drag = HandleDrag::Inner;
        else if (dOuter <= kHitTol)                     drag = HandleDrag::Outer;
        else                                            drag = HandleDrag::None;
    }
    if (!active) drag = HandleDrag::None;

    if (active && drag != HandleDrag::None) {
        float frac = std::clamp(mDist / kRadius, 0.0f, 1.0f);
        // Snap to 2 decimals — the ring is ~140px across, well short of the pixel precision
        // a 3rd decimal would need, and that's the resolution shown in the readout anyway.
        frac = std::round(frac * 100.0f) / 100.0f;
        if (drag == HandleDrag::Inner)
            calib.deadzone = std::clamp(frac, 0.0f, std::max(0.0f, calib.max - 0.02f));
        else
            calib.max = std::clamp(frac, std::min(1.0f, calib.deadzone + 0.02f), 1.0f);
    }

    float mag = std::sqrt(rawX * rawX + rawY * rawY);

    float innerVisualR = std::max(kRadius * calib.deadzone, kMinInnerR);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircle(center, kRadius, IM_COL32(90, 100, 120, 160), 48, 1.5f);                 // reference (1.0)
    dl->AddCircleFilled(center, innerVisualR, IM_COL32(60, 60, 70, 140), 48);               // inner fill

    ImU32 outerCol = (drag == HandleDrag::Outer) ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    dl->AddCircle(center, kRadius * calib.max, outerCol, 48, drag == HandleDrag::Outer ? 3.0f : 2.0f);

    ImU32 innerCol = (drag == HandleDrag::Inner) ? IM_COL32(210, 210, 220, 255) : IM_COL32(130, 130, 140, 200);
    dl->AddCircle(center, innerVisualR, innerCol, 48, drag == HandleDrag::Inner ? 3.0f : 1.5f);

    ImU32 dotColor = mag < calib.deadzone ? IM_COL32(140, 140, 150, 220)
                    : mag > calib.max     ? IM_COL32(255, 140, 60, 230)
                                          : IM_COL32(90, 230, 120, 230);
    ImVec2 dot = { center.x + rawX * kRadius, center.y - rawY * kRadius };
    dl->AddCircleFilled(dot, 5.0f, dotColor);
    dl->AddCircle(dot, 5.0f, IM_COL32(20, 20, 25, 200), 12, 1.5f);

    ImGui::Text("%s %.2f   %s %.2f   %s %.2f", tr("calibration.inner"), calib.deadzone,
                                     tr("calibration.outer"), calib.max,
                                     tr("calibration.current"), mag);

    // Invert checkboxes — hidden if this device's config has no matching HID axis for that
    // logical stick axis (shouldn't normally happen, but a half-configured entry is possible).
    if (!xInvert.hidKey.empty()) {
        ImGui::Checkbox(tr("calibration.invert_x"), &xInvert.invert);
    }
    if (!yInvert.hidKey.empty()) {
        ImGui::Checkbox(tr("calibration.invert_y"), &yInvert.invert);
    }

    ImGui::PopID();
}

void CalibrationPanel::renderTriggerWidget(const char* label, const char* idSuffix,
                                           float rawValue, TriggerCalibration& calib,
                                           HandleDrag& drag, bool mirrored) {
    ImGui::PushID(idSuffix);
    ImGui::Text("%s", label);

    constexpr float kWidth  = 200.0f;  // 0..1 spans a full 200px so 0.01 steps stay draggable
    constexpr float kHeight = 22.0f;
    constexpr float kPad    = 6.0f;    // vertical room for handles poking above/below the bar
    constexpr float kHitTol = 10.0f;   // px band around a handle that grabs it on click

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 barPos = { origin.x, origin.y + kPad };
    ImVec2 barEnd = { barPos.x + kWidth, barPos.y + kHeight };

    // mirrored: R2 reads right-to-left (0 at the right edge) so it faces L2 like a mirror
    // image when the two bars sit side by side. Only which screen edge is "0" changes — the
    // deadzone/max math (fractions in [0,1]) stays identical.
    auto xAt = [&](float frac) { return mirrored ? (barEnd.x - kWidth * frac) : (barPos.x + kWidth * frac); };

    ImGui::InvisibleButton("##trigger_bar", { kWidth, kHeight + kPad * 2.0f });
    bool active = ImGui::IsItemActive();

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float  mX    = mirrored ? std::clamp(barEnd.x - mouse.x, 0.0f, kWidth)
                            : std::clamp(mouse.x - barPos.x, 0.0f, kWidth);

    if (ImGui::IsItemActivated()) {
        float innerX = kWidth * calib.deadzone;
        float outerX = kWidth * calib.max;
        float dInner = std::fabs(mX - innerX);
        float dOuter = std::fabs(mX - outerX);
        if (dInner <= kHitTol && dInner <= dOuter)      drag = HandleDrag::Inner;
        else if (dOuter <= kHitTol)                     drag = HandleDrag::Outer;
        else                                            drag = HandleDrag::None;
    }
    if (!active) drag = HandleDrag::None;

    if (active && drag != HandleDrag::None) {
        float frac = std::clamp(mX / kWidth, 0.0f, 1.0f);
        frac = std::round(frac * 100.0f) / 100.0f;
        if (drag == HandleDrag::Inner)
            calib.deadzone = std::clamp(frac, 0.0f, std::max(0.0f, calib.max - 0.02f));
        else
            calib.max = std::clamp(frac, std::min(1.0f, calib.deadzone + 0.02f), 1.0f);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(barPos, barEnd, IM_COL32(50, 52, 58, 200), 3.0f);   // track

    ImU32 fillCol = rawValue < calib.deadzone ? IM_COL32(90, 92, 100, 220)
                   : rawValue > calib.max     ? IM_COL32(255, 140, 60, 230)
                                              : IM_COL32(90, 200, 120, 230);
    float fillEdge = xAt(std::clamp(rawValue, 0.0f, 1.0f));
    ImVec2 fillMin = mirrored ? ImVec2{ fillEdge, barPos.y } : barPos;
    ImVec2 fillMax = mirrored ? ImVec2{ barEnd.x, barEnd.y } : ImVec2{ fillEdge, barEnd.y };
    dl->AddRectFilled(fillMin, fillMax, fillCol, 3.0f);

    float innerX = xAt(calib.deadzone);
    ImU32 innerCol = (drag == HandleDrag::Inner) ? IM_COL32(210, 210, 220, 255) : IM_COL32(160, 160, 170, 220);
    dl->AddLine({ innerX, barPos.y - kPad }, { innerX, barEnd.y + kPad }, innerCol,
               drag == HandleDrag::Inner ? 3.0f : 2.0f);

    float outerX = xAt(calib.max);
    ImU32 outerCol = (drag == HandleDrag::Outer) ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    dl->AddLine({ outerX, barPos.y - kPad }, { outerX, barEnd.y + kPad }, outerCol,
               drag == HandleDrag::Outer ? 3.0f : 2.0f);

    dl->AddRect(barPos, barEnd, IM_COL32(90, 100, 120, 160), 3.0f);      // border

    ImGui::Text("%s %.2f   %s %.2f   %s %.2f", tr("calibration.inner"), calib.deadzone,
                                     tr("calibration.outer"), calib.max,
                                     tr("calibration.current"), rawValue);

    ImGui::PopID();
}

void CalibrationPanel::renderImuAxisWidgetVertical(const char* idSuffix,
                                                   float rawValue, float& deadzone, float& max,
                                                   HandleDrag& drag) {
    ImGui::PushID(idSuffix);

    constexpr float kThickness   = 14.0f;  // narrower than the compasses' cross ticks, on request
    constexpr float kPad         = 6.0f;
    constexpr float kHitTol      = 10.0f;
    constexpr float kOuterCeiling = 1.20f;
    // Matches renderGyroCompass/renderAccelCompass's diameter exactly, so this bar's height
    // lines up with the compasses next to it (feedback: keep everything at the same height). No
    // title/readout text here anymore either — this axis has no compass of its own, so its
    // label+numbers live as an extra line under the accel compass instead.
    constexpr float kHalfHeight  = kCompassDiameter / 2.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 barPos = { origin.x + kPad, origin.y };
    ImVec2 center = { barPos.x, barPos.y + kHalfHeight };
    ImVec2 barEnd = { barPos.x + kThickness, barPos.y + kHalfHeight * 2.0f };

    auto toPixels = [&](float v) { return kHalfHeight * (v / kOuterCeiling); };
    auto toValue  = [&](float px) { return std::clamp((px / kHalfHeight) * kOuterCeiling, 0.0f, kOuterCeiling); };

    ImGui::InvisibleButton("##mirror_bar_v", { kThickness + kPad * 2.0f, kHalfHeight * 2.0f });
    bool active = ImGui::IsItemActive();

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float  mOff  = std::clamp(std::fabs(mouse.y - center.y), 0.0f, kHalfHeight);

    if (ImGui::IsItemActivated()) {
        float innerY = toPixels(deadzone);
        float outerY = toPixels(max);
        float dInner = std::fabs(mOff - innerY);
        float dOuter = std::fabs(mOff - outerY);
        if (dInner <= kHitTol && dInner <= dOuter)      drag = HandleDrag::Inner;
        else if (dOuter <= kHitTol)                     drag = HandleDrag::Outer;
        else                                            drag = HandleDrag::None;
    }
    if (!active) drag = HandleDrag::None;

    if (active && drag != HandleDrag::None) {
        float v = toValue(mOff);
        v = std::round(v * 100.0f) / 100.0f;
        if (drag == HandleDrag::Inner)
            deadzone = std::clamp(v, 0.0f, max - 0.02f > 0.0f ? max - 0.02f : 0.0f);
        else
            max = std::clamp(v, deadzone + 0.02f, kOuterCeiling);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(barPos, barEnd, IM_COL32(50, 52, 58, 200), 3.0f);   // track

    float dzHalf = toPixels(deadzone);
    dl->AddRectFilled({ barPos.x, center.y - dzHalf }, { barEnd.x, center.y + dzHalf },
                      IM_COL32(60, 60, 70, 160), 3.0f);                   // deadzone band, centered

    float refY = toPixels(1.0f);
    dl->AddLine({ barPos.x - kPad, center.y - refY }, { barEnd.x + kPad, center.y - refY },
               IM_COL32(120, 125, 135, 170), 1.0f);
    dl->AddLine({ barPos.x - kPad, center.y + refY }, { barEnd.x + kPad, center.y + refY },
               IM_COL32(120, 125, 135, 170), 1.0f);

    float outY = toPixels(max);
    ImU32 outerCol = (drag == HandleDrag::Outer) ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    float outerW   = drag == HandleDrag::Outer ? 3.0f : 2.0f;
    dl->AddLine({ barPos.x - kPad, center.y - outY }, { barEnd.x + kPad, center.y - outY }, outerCol, outerW);
    dl->AddLine({ barPos.x - kPad, center.y + outY }, { barEnd.x + kPad, center.y + outY }, outerCol, outerW);

    ImU32 innerCol = (drag == HandleDrag::Inner) ? IM_COL32(210, 210, 220, 255) : IM_COL32(160, 160, 170, 220);
    float innerW   = drag == HandleDrag::Inner ? 3.0f : 2.0f;
    dl->AddLine({ barPos.x - kPad, center.y - dzHalf }, { barEnd.x + kPad, center.y - dzHalf }, innerCol, innerW);
    dl->AddLine({ barPos.x - kPad, center.y + dzHalf }, { barEnd.x + kPad, center.y + dzHalf }, innerCol, innerW);

    dl->AddLine({ barPos.x, center.y }, { barEnd.x, center.y }, IM_COL32(200, 200, 210, 160), 1.0f); // center tick

    float absV = std::fabs(rawValue);
    ImU32 markCol = absV < deadzone ? IM_COL32(140, 140, 150, 220)
                   : absV > max     ? IM_COL32(255, 140, 60, 230)
                                    : IM_COL32(90, 230, 120, 230);
    // Positive (face up) moves the marker up, matching the gyro/accel compass ball convention.
    ImVec2 markPos = { (barPos.x + barEnd.x) * 0.5f,
                       center.y - (rawValue < 0.0f ? -1.0f : 1.0f) * toPixels(absV) };
    dl->AddCircleFilled(markPos, 5.0f, markCol);
    dl->AddCircle(markPos, 5.0f, IM_COL32(20, 20, 25, 200), 12, 1.5f);

    dl->AddRect(barPos, barEnd, IM_COL32(90, 100, 120, 160), 3.0f);       // border

    ImGui::PopID();
}

void CalibrationPanel::renderGyroCompass(const char* label, const char* idSuffix,
                                         float rawPitch, float rawRoll, float rawYaw,
                                         float& pitchDeadzone, float& pitchMax,
                                         float& rollDeadzone, float& rollMax,
                                         float& yawDeadzone, float& yawMax,
                                         CompassHandle& drag,
                                         bool& pitchInvert, bool& rollInvert, bool& yawInvert) {
    ImGui::PushID(idSuffix);
    ImGui::Text("%s", label);

    constexpr float kRadius       = kCompassRadius;
    constexpr float kPad         = kCompassPad;      // room for the yaw arc + tick marks poking out
    // Grab tolerance and a wider "am I even near this axis" band — split in two because a
    // single Euclidean point-distance test (v1) missed the mirrored handle entirely (clicking
    // the tick BELOW center for pitch, say, was never close to the point stored for "above
    // center") and made curves nearly impossible to grab (feedback: "solo vale pinchar en la
    // puntita"). Testing "distance along the axis" instead of "distance to one fixed point"
    // covers both mirrored sides automatically.
    constexpr float kHitTol      = kCompassHitTol;
    constexpr float kAxisTol     = kCompassAxisTol;
    constexpr float kOuterCeiling = kCompassOuterCeiling;
    constexpr float kYawArcDeg   = 70.0f;    // yaw's total sweep is 2x this, centered on "up"
    const float     kYawArcRad  = kYawArcDeg * 3.14159265f / 180.0f;

    CompassSetup s = beginCompassWidget("##gyro_compass", kRadius, kPad, kAxisTol);
    auto toPx = [&](float v) { return kRadius * (v / kOuterCeiling); };

    float yawInAng  = std::clamp(yawDeadzone / kOuterCeiling, 0.0f, 1.0f) * kYawArcRad;
    float yawOutAng = std::clamp(yawMax      / kOuterCeiling, 0.0f, 1.0f) * kYawArcRad;

    float mDistFromCenter = std::sqrt((s.mouse.x - s.center.x) * (s.mouse.x - s.center.x) +
                                      (s.mouse.y - s.center.y) * (s.mouse.y - s.center.y));
    float mAngle = std::atan2(s.mouse.x - s.center.x, s.center.y - s.mouse.y);
    bool  onArc  = std::fabs(mDistFromCenter - kRadius) <= kAxisTol &&
                   std::fabs(mAngle) <= kYawArcRad + 0.35f;  // ~20deg margin past the ceiling tick

    if (s.activated) {
        CompassCandidate cands[6] = {
            { CompassHandle::VInner, std::fabs(s.mVOff - toPx(pitchDeadzone)), s.onVAxis },
            { CompassHandle::VOuter, std::fabs(s.mVOff - toPx(pitchMax)),      s.onVAxis },
            { CompassHandle::HInner, std::fabs(s.mHOff - toPx(rollDeadzone)), s.onHAxis },
            { CompassHandle::HOuter, std::fabs(s.mHOff - toPx(rollMax)),      s.onHAxis },
            { CompassHandle::AInner, std::fabs(std::fabs(mAngle) - yawInAng)  * kRadius, onArc },
            { CompassHandle::AOuter, std::fabs(std::fabs(mAngle) - yawOutAng) * kRadius, onArc },
        };
        drag = pickCompassHandle(cands, 6, kHitTol);
    }
    if (!s.active) drag = CompassHandle::None;

    if (s.active && drag != CompassHandle::None) {
        if (drag == CompassHandle::VInner || drag == CompassHandle::VOuter)
            applyAxisDrag(drag == CompassHandle::VInner, s.mVOff, kRadius, kOuterCeiling,
                         pitchDeadzone, pitchMax);
        else if (drag == CompassHandle::HInner || drag == CompassHandle::HOuter)
            applyAxisDrag(drag == CompassHandle::HInner, s.mHOff, kRadius, kOuterCeiling,
                         rollDeadzone, rollMax);
        else {
            float ang = std::clamp(mAngle, -kYawArcRad, kYawArcRad);
            float v = std::round((std::fabs(ang) / kYawArcRad * kOuterCeiling) * 100.0f) / 100.0f;
            if (drag == CompassHandle::AInner)
                yawDeadzone = std::clamp(v, 0.0f, yawMax - 0.02f > 0.0f ? yawMax - 0.02f : 0.0f);
            else
                yawMax = std::clamp(v, yawDeadzone + 0.02f, kOuterCeiling);
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawCompassRingAndGuides(dl, s.center, kRadius, kOuterCeiling);

    drawAxisTicks(dl, s.center, /*vertical=*/true, pitchDeadzone, pitchMax, kRadius, kOuterCeiling,
                 drag == CompassHandle::VInner, drag == CompassHandle::VOuter);
    drawAxisTicks(dl, s.center, /*vertical=*/false, rollDeadzone, rollMax, kRadius, kOuterCeiling,
                 drag == CompassHandle::HInner, drag == CompassHandle::HOuter);

    auto arcDot = [&](float ang, ImU32 col) {
        dl->AddCircleFilled({ s.center.x + kRadius * sinf(ang), s.center.y - kRadius * cosf(ang) }, 3.5f, col);
    };
    ImU32 aInCol  = (drag == CompassHandle::AInner) ? IM_COL32(210, 210, 220, 255) : IM_COL32(160, 160, 170, 220);
    ImU32 aOutCol = (drag == CompassHandle::AOuter) ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    drawArc(dl, s.center, kRadius, -yawInAng, yawInAng, aInCol, drag == CompassHandle::AInner ? 3.0f : 2.0f);
    drawArc(dl, s.center, kRadius, -kYawArcRad, -yawOutAng, aOutCol, drag == CompassHandle::AOuter ? 3.0f : 2.0f);
    drawArc(dl, s.center, kRadius, yawOutAng, kYawArcRad, aOutCol, drag == CompassHandle::AOuter ? 3.0f : 2.0f);
    arcDot(yawInAng, aInCol);  arcDot(-yawInAng, aInCol);
    arcDot(yawOutAng, aOutCol); arcDot(-yawOutAng, aOutCol);

    float yawAngNow = std::clamp((rawYaw / kOuterCeiling) * kYawArcRad, -kYawArcRad, kYawArcRad);
    ImVec2 yawDot   = { s.center.x + kRadius * sinf(yawAngNow), s.center.y - kRadius * cosf(yawAngNow) };
    ImU32  yawDotCol = std::fabs(rawYaw) < yawDeadzone ? IM_COL32(140, 140, 150, 220)
                     : std::fabs(rawYaw) > yawMax      ? IM_COL32(255, 140, 60, 230)
                                                        : IM_COL32(90, 230, 120, 230);
    dl->AddCircleFilled(yawDot, 4.5f, yawDotCol);
    dl->AddCircle(yawDot, 4.5f, IM_COL32(20, 20, 25, 200), 12, 1.0f);

    drawCompassBall(dl, s.center, rawPitch, rawRoll, pitchDeadzone, pitchMax, rollDeadzone, rollMax,
                   kRadius, kOuterCeiling);

    // Invert checkboxes sit in their own fixed column, to the LEFT of these text lines — see
    // beginInvertColumn's comment.
    float textColX = beginInvertColumn();

    drawInvertRow(textColX, "##pitchInvert", pitchInvert, "%s: %s %.2f  %s %.2f  %s %.2f",
                 tr("calibration.gyro_x"), tr("calibration.inner"), pitchDeadzone,
                 tr("calibration.outer"), pitchMax, tr("calibration.current"), rawPitch);
    drawInvertRow(textColX, "##rollInvert", rollInvert, "%s: %s %.2f  %s %.2f  %s %.2f",
                 tr("calibration.gyro_z"), tr("calibration.inner"), rollDeadzone,
                 tr("calibration.outer"), rollMax, tr("calibration.current"), rawRoll);
    drawInvertRow(textColX, "##yawInvert", yawInvert, "%s: %s %.2f  %s %.2f  %s %.2f",
                 tr("calibration.gyro_y"), tr("calibration.inner"), yawDeadzone,
                 tr("calibration.outer"), yawMax, tr("calibration.current"), rawYaw);

    ImGui::PopID();
}

void CalibrationPanel::renderAccelCompass(const char* label, const char* idSuffix,
                                          float rawY, float rawX,
                                          float& yDeadzone, float& yMax,
                                          float& xDeadzone, float& xMax,
                                          CompassHandle& drag,
                                          bool& yInvert, bool& xInvert) {
    ImGui::PushID(idSuffix);
    ImGui::Text("%s", label);

    constexpr float kRadius       = kCompassRadius;
    // Same kPad as renderGyroCompass — keeps both compasses' diameters (and hence their text
    // rows below) aligned at the same height, even though accel doesn't need arc clearance.
    constexpr float kPad         = kCompassPad;
    constexpr float kHitTol      = kCompassHitTol;
    constexpr float kAxisTol     = kCompassAxisTol;
    constexpr float kOuterCeiling = kCompassOuterCeiling;

    CompassSetup s = beginCompassWidget("##accel_compass", kRadius, kPad, kAxisTol);
    auto toPx = [&](float v) { return kRadius * (v / kOuterCeiling); };

    if (s.activated) {
        CompassCandidate cands[4] = {
            { CompassHandle::VInner, std::fabs(s.mVOff - toPx(yDeadzone)), s.onVAxis },
            { CompassHandle::VOuter, std::fabs(s.mVOff - toPx(yMax)),      s.onVAxis },
            { CompassHandle::HInner, std::fabs(s.mHOff - toPx(xDeadzone)), s.onHAxis },
            { CompassHandle::HOuter, std::fabs(s.mHOff - toPx(xMax)),      s.onHAxis },
        };
        drag = pickCompassHandle(cands, 4, kHitTol);
    }
    if (!s.active) drag = CompassHandle::None;

    if (s.active && drag != CompassHandle::None) {
        if (drag == CompassHandle::VInner || drag == CompassHandle::VOuter)
            applyAxisDrag(drag == CompassHandle::VInner, s.mVOff, kRadius, kOuterCeiling, yDeadzone, yMax);
        else
            applyAxisDrag(drag == CompassHandle::HInner, s.mHOff, kRadius, kOuterCeiling, xDeadzone, xMax);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawCompassRingAndGuides(dl, s.center, kRadius, kOuterCeiling);

    drawAxisTicks(dl, s.center, /*vertical=*/true, yDeadzone, yMax, kRadius, kOuterCeiling,
                 drag == CompassHandle::VInner, drag == CompassHandle::VOuter);
    drawAxisTicks(dl, s.center, /*vertical=*/false, xDeadzone, xMax, kRadius, kOuterCeiling,
                 drag == CompassHandle::HInner, drag == CompassHandle::HOuter);

    drawCompassBall(dl, s.center, rawY, rawX, yDeadzone, yMax, xDeadzone, xMax, kRadius, kOuterCeiling);

    // Same fixed invert column as renderGyroCompass (to the LEFT of the text) — see its comment.
    float textColX = beginInvertColumn();

    drawInvertRow(textColX, "##accelYInvert", yInvert, "%s: %s %.2f  %s %.2f  %s %.2f",
                 tr("calibration.accel_y"), tr("calibration.inner"), yDeadzone,
                 tr("calibration.outer"), yMax, tr("calibration.current"), rawY);
    drawInvertRow(textColX, "##accelXInvert", xInvert, "%s: %s %.2f  %s %.2f  %s %.2f",
                 tr("calibration.accel_x"), tr("calibration.inner"), xDeadzone,
                 tr("calibration.outer"), xMax, tr("calibration.current"), rawX);

    ImGui::PopID();
}

void CalibrationPanel::renderTouchCross(const char* label, const char* idSuffix,
                                        float rawX, float rawY,
                                        float& xMax, float& yMax,
                                        CompassHandle& drag) {
    ImGui::PushID(idSuffix);
    ImGui::Text("%s", label);

    // Smaller than the gyro/accel compasses (kCompassRadius/kCompassPad) — this one sits in the
    // trigger row rather than its own dedicated row, and has no ring/arc filling out its corners.
    // kPad keeps the same ratio to kRadius the widget originally shipped with (8/55).
    constexpr float kRadius       = 75.0f;
    constexpr float kPad         = kRadius * 8.0f / 55.0f;
    constexpr float kHitTol      = 12.0f;
    constexpr float kAxisTol     = 14.0f;
    // Same headroom trick as the gyro/accel widgets (see ImuConfig's comment): lets max be
    // dragged up to 20% past the nominal edge, absorbing a wizard maxX/maxY that landed a few %
    // short of the true physical corner without needing to re-run it.
    constexpr float kOuterCeiling = 1.20f;
    const float     kDiameter    = (kRadius + kPad) * 2.0f;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 center    = { canvasPos.x + kRadius + kPad, canvasPos.y + kRadius + kPad };

    ImGui::InvisibleButton("##touch_cross", { kDiameter, kDiameter });
    bool active = ImGui::IsItemActive();

    auto toPx    = [&](float v) { return kRadius * (v / kOuterCeiling); };
    auto toValue = [&](float px) { return std::clamp((px / kRadius) * kOuterCeiling, 0.0f, kOuterCeiling); };

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float  mHOff = std::fabs(mouse.x - center.x);
    float  mVOff = std::fabs(mouse.y - center.y);
    bool   onHAxis = mVOff <= kAxisTol;  // near the horizontal guide -> grabbing the X handle
    bool   onVAxis = mHOff <= kAxisTol;  // near the vertical guide -> grabbing the Y handle

    // Only HOuter/VOuter (max) exist here — no HInner/VInner deadzone handles, unlike the
    // gyro/accel compasses. See TouchpadConfig's xMax/yMax comment for why touch calibration
    // deliberately has no center deadzone.
    if (ImGui::IsItemActivated()) {
        struct Cand { CompassHandle h; float d; bool valid; };
        Cand cands[2] = {
            { CompassHandle::HOuter, std::fabs(mHOff - toPx(xMax)), onHAxis },
            { CompassHandle::VOuter, std::fabs(mVOff - toPx(yMax)), onVAxis },
        };
        CompassHandle best = CompassHandle::None;
        float bestD = kHitTol;
        for (const auto& c : cands) if (c.valid && c.d <= bestD) { bestD = c.d; best = c.h; }
        drag = best;
    }
    if (!active) drag = CompassHandle::None;

    if (active && drag != CompassHandle::None) {
        if (drag == CompassHandle::HOuter)
            xMax = std::clamp(std::round(toValue(mHOff) * 100.0f) / 100.0f, 0.02f, kOuterCeiling);
        else if (drag == CompassHandle::VOuter)
            yMax = std::clamp(std::round(toValue(mVOff) * 100.0f) / 100.0f, 0.02f, kOuterCeiling);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // No boundary ring, unlike renderGyroCompass/renderAccelCompass — see this function's
    // declaration comment in CalibrationPanel.h. Just the crosshair guide lines through the
    // pad's center, matching how a finger's travel is actually bounded (two independent linear
    // axes), not a circle.
    dl->AddLine({ center.x, center.y - kRadius }, { center.x, center.y + kRadius },
               IM_COL32(90, 100, 120, 90), 1.0f);
    dl->AddLine({ center.x - kRadius, center.y }, { center.x + kRadius, center.y },
               IM_COL32(90, 100, 120, 90), 1.0f);

    auto hTick = [&](float y, ImU32 col, float w) {
        dl->AddLine({ center.x - 9.0f, y }, { center.x + 9.0f, y }, col, w);
        dl->AddCircleFilled({ center.x, y }, 3.5f, col);
    };
    auto vTick = [&](float x, ImU32 col, float w) {
        dl->AddLine({ x, center.y - 9.0f }, { x, center.y + 9.0f }, col, w);
        dl->AddCircleFilled({ x, center.y }, 3.5f, col);
    };

    ImU32 hOutCol = (drag == CompassHandle::HOuter) ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    vTick(center.x - toPx(xMax), hOutCol, drag == CompassHandle::HOuter ? 3.0f : 2.0f);
    vTick(center.x + toPx(xMax), hOutCol, drag == CompassHandle::HOuter ? 3.0f : 2.0f);

    ImU32 vOutCol = (drag == CompassHandle::VOuter) ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 180, 255, 220);
    hTick(center.y - toPx(yMax), vOutCol, drag == CompassHandle::VOuter ? 3.0f : 2.0f);
    hTick(center.y + toPx(yMax), vOutCol, drag == CompassHandle::VOuter ? 3.0f : 2.0f);

    bool xOut = std::fabs(rawX) > xMax, yOut = std::fabs(rawY) > yMax;
    ImU32 ballCol = (xOut || yOut) ? IM_COL32(255, 140, 60, 230) : IM_COL32(90, 230, 120, 230);
    // No sign flip needed (unlike the gyro/accel "positive = up" tilt convention): touch1X/Y grow
    // left->right and top->bottom, same as screen space, so the ball just follows raw offset.
    ImVec2 ball = { center.x + toPx(rawX), center.y + toPx(rawY) };
    dl->AddCircleFilled({ ball.x + 1.5f, ball.y + 2.5f }, 7.0f, IM_COL32(0, 0, 0, 80));
    dl->AddCircleFilled(ball, 7.0f, ballCol);
    dl->AddCircle(ball, 7.0f, IM_COL32(20, 20, 25, 200), 16, 1.5f);

    // Tighter than the default ItemSpacing between the crosshair and its readout lines (and
    // between the two lines themselves) — this widget sits in the trigger row, so keeping it
    // compact matters more here than it does for the gyro/accel compasses in their own row.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2.0f });
    ImGui::Text("%s: %s %.2f  %s %.2f", tr("calibration.touch_x"),
               tr("calibration.outer"), xMax, tr("calibration.current"), rawX);
    ImGui::Text("%s: %s %.2f  %s %.2f", tr("calibration.touch_y"),
               tr("calibration.outer"), yMax, tr("calibration.current"), rawY);
    ImGui::PopStyleVar();

    ImGui::PopID();
}

void CalibrationPanel::render() {
    if (ImGui::Button(tr("btn.back"))) {
        m_active = false;
        return;
    }

    // Save sits next to Back instead of at the bottom of a long scrolling panel — one line saved,
    // and the button (plus the device title next to it) only draws when there is a device.
    if (m_hasActiveConfig) {
        ImGui::SameLine(0.0f, 20.0f);
        if (ImGui::Button(tr("btn.save"), { 120.0f, 0.0f }))
            save();

        // Merges the old standalone "Calibracion" title and "Mando activo: X" line into this row
        // too, ahead of the toast/error text below — two lines saved off the panel.
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::Text("%s %s", tr("calibration.title_for"), m_activeDeviceName.c_str());
    }

    if (!m_toastMsg.empty()) {
        if (GetTickCount64() - m_toastTime < 2500) {
            ImGui::SameLine(0.0f, 20.0f);
            ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "%s", m_toastMsg.c_str());
        } else {
            m_toastMsg.clear();
        }
    }
    if (!m_saveError.empty()) {
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", m_saveError.c_str());
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (!m_hasActiveConfig) {
        ImGui::Text("%s", tr("calibration.title"));
        ImGui::TextDisabled("%s", tr("calibration.no_device"));
        return;
    }

    GamepadState phys = m_engine->getLastState();

    ImVec2 topRowStart = ImGui::GetCursorScreenPos();

    ImGui::BeginGroup();
    renderTriggerWidget(tr("pad.trigger_l"), "calibTriggerL",
                        phys.triggerL, m_editTriggerL, m_triggerLDrag);
    ImGui::EndGroup();
    float topRowBottom = ImGui::GetItemRectMax().y;

    ImGui::SameLine(0.0f, 40.0f);

    ImGui::BeginGroup();
    renderTriggerWidget(tr("pad.trigger_r"), "calibTriggerR",
                        phys.triggerR, m_editTriggerR, m_triggerRDrag, /*mirrored=*/true);
    ImGui::EndGroup();
    topRowBottom = std::max(topRowBottom, ImGui::GetItemRectMax().y);

    // Same row as the triggers — taller than them (the cross needs room for its guide lines), so
    // the row's bottom (and everything below it) shifts down to fit, tracked via topRowBottom
    // same as the sticks/gyro row further down does with mainRowBottom.
    if (m_activeConfig.touchpad.enabled) {
        ImGui::SameLine(0.0f, 40.0f);
        ImGui::BeginGroup();
        // touch1X/Y are [0,1]; recenter on the pad's own middle and scale so an edge reads 1.0,
        // same rest-centered convention as the gyro/accel axes below. Rests at dead center (0,0)
        // with no finger down instead of following touch1X/Y's idle 0.0f, which would otherwise
        // read as "stuck in the top-left corner".
        float touchOffX = phys.touch1Active ? (phys.touch1X - 0.5f) * 2.0f : 0.0f;
        float touchOffY = phys.touch1Active ? (phys.touch1Y - 0.5f) * 2.0f : 0.0f;
        renderTouchCross(tr("calibration.touch_cross"), "calibTouch",
                         touchOffX, touchOffY,
                         m_editTouch.xMax, m_editTouch.yMax,
                         m_touchCrossDrag);
        ImGui::EndGroup();
        topRowBottom = std::max(topRowBottom, ImGui::GetItemRectMax().y);
    }

    ImGui::SetCursorScreenPos({ topRowStart.x, topRowBottom });

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Invert lives in ControllerConfig::axes, applied by the engine when it computes phys.* —
    // toggling the checkbox here only edits the scratch AxisInvertRef, so without this the ball
    // wouldn't visibly move until Save + a config reload. Flipping the sign here whenever the
    // edited value differs from what's already baked into phys.* gives an immediate preview.
    auto previewSign = [](const AxisInvertRef& ref) {
        return (ref.hidKey.empty() || ref.invert == ref.originalInvert) ? 1.0f : -1.0f;
    };

    // Sticks, gyro and accel share ONE wide row instead of stacking in separate sections —
    // there's plenty of horizontal room, and stacking them forced the panel into vertical
    // scroll. A vertical line marks where the sticks end and the gyro/accel compasses begin
    // (feedback: keep a visual break even on the same row). Fixed screen-space offsets from the
    // row's start (not SameLine) for the same reason as the gyro/accel columns below: SameLine's
    // spacing depends on the PREVIOUS group's actual (variable-width) content, which jitters as
    // numbers change digits.
    constexpr float kStickDiameter = 220.0f;  // renderStickWidget's own (100 radius + 10 pad) * 2
    constexpr float kStickGap      = 40.0f;
    constexpr float kSeparatorGap  = 30.0f;   // clearance on each side of the vertical divider

    ImVec2 mainRowStart = ImGui::GetCursorScreenPos();

    // A stick with no HID axis mapped to either of its logical targets doesn't exist on this
    // device (e.g. the Zero 2's cruceta is an analog_dpad feeding left_x/left_y, with no right
    // stick at all) — skip its widget instead of drawing an interactive ring for calibration
    // data that has nothing physical behind it. Same detection findAxisInvert() already does
    // per-axis for the invert checkboxes, just applied to the whole widget.
    const bool hasLeftStick  = !m_leftXInvertRef.hidKey.empty()  || !m_leftYInvertRef.hidKey.empty();
    const bool hasRightStick = !m_rightXInvertRef.hidKey.empty() || !m_rightYInvertRef.hidKey.empty();

    ImGui::BeginGroup();
    if (hasLeftStick) {
        renderStickWidget(tr("calibration.left_stick"), "calibLeftStick",
                          phys.leftX * previewSign(m_leftXInvertRef), phys.leftY * previewSign(m_leftYInvertRef),
                          m_editLeftStick, m_leftStickDrag, m_leftXInvertRef, m_leftYInvertRef);
    } else {
        ImGui::Text("%s", tr("calibration.left_stick"));
        ImGui::TextDisabled("%s", tr("calibration.no_stick"));
    }
    ImGui::EndGroup();
    float mainRowBottom = ImGui::GetItemRectMax().y;

    ImGui::SetCursorScreenPos({ mainRowStart.x + kStickDiameter + kStickGap, mainRowStart.y });
    ImGui::BeginGroup();
    if (hasRightStick) {
        renderStickWidget(tr("calibration.right_stick"), "calibRightStick",
                          phys.rightX * previewSign(m_rightXInvertRef), phys.rightY * previewSign(m_rightYInvertRef),
                          m_editRightStick, m_rightStickDrag, m_rightXInvertRef, m_rightYInvertRef);
    } else {
        ImGui::Text("%s", tr("calibration.right_stick"));
        ImGui::TextDisabled("%s", tr("calibration.no_stick"));
    }
    ImGui::EndGroup();
    mainRowBottom = std::max(mainRowBottom, ImGui::GetItemRectMax().y);

    float afterSticksX = mainRowStart.x + kStickDiameter + kStickGap + kStickDiameter;
    float imuColX       = afterSticksX + kSeparatorGap * 2.0f;

    if (!m_activeConfig.imu.enabled) {
        ImGui::SetCursorScreenPos({ imuColX, mainRowStart.y });
        ImGui::TextDisabled("%s", tr("calibration.no_imu"));
        mainRowBottom = std::max(mainRowBottom, ImGui::GetItemRectMax().y);
    } else {
        const bool hasAccelX = m_activeConfig.imu.accelXOffset != -1;
        const bool hasAccelY = m_activeConfig.imu.accelYOffset != -1;
        const bool hasAccelZ = m_activeConfig.imu.accelZOffset != -1;
        // Declared at this scope (not inside either `if (hasAccelZ)` block below) because it's
        // read by both: the text/checkbox line under the accel compass and the vertical bar's
        // own live marker further down.
        float effAccelZ = hasAccelZ
            ? phys.accelZ * (m_editImu.accelZInvert == m_activeConfig.imu.accelZInvert ? 1.0f : -1.0f)
            : 0.0f;

        // Paired by physical gesture, not by sensor — gyro (angular velocity) and accel (tilt)
        // read the same real-world motion on two different axis letters, see PhysicalAccel's
        // comment in ComponentTypes.h. Pitch (nod up/down) pairs with accelY; roll (tilt left/
        // right) pairs with accelX. Yaw (turn like a steering wheel) has no accel counterpart —
        // pure rotation about the vertical axis doesn't tilt the case, so it's the compass's arc
        // instead of a ball position, and accel has no arc at all.
        //
        // Same "flip if the edited invert differs from what's already baked into phys.*" trick
        // as the sticks above, so toggling one of these checkboxes previews immediately instead
        // of waiting for Save + reload.
        auto gyroSign = [&](bool edited, bool original) { return edited == original ? 1.0f : -1.0f; };

        ImGui::SetCursorScreenPos({ imuColX, mainRowStart.y });
        ImGui::BeginGroup();
        renderGyroCompass(tr("calibration.gyro_compass"), "calibGyroCompass",
                          phys.gyroX * gyroSign(m_editImu.gyroXInvert, m_activeConfig.imu.gyroXInvert),
                          phys.gyroZ * gyroSign(m_editImu.gyroZInvert, m_activeConfig.imu.gyroZInvert),
                          phys.gyroY * gyroSign(m_editImu.gyroYInvert, m_activeConfig.imu.gyroYInvert),
                          m_editImu.gyroXDeadzone, m_editImu.gyroXMax,
                          m_editImu.gyroZDeadzone, m_editImu.gyroZMax,
                          m_editImu.gyroYDeadzone, m_editImu.gyroYMax,
                          m_gyroCompassDrag,
                          m_editImu.gyroXInvert, m_editImu.gyroZInvert, m_editImu.gyroYInvert);
        ImGui::EndGroup();
        mainRowBottom = std::max(mainRowBottom, ImGui::GetItemRectMax().y);

        constexpr float kGyroAccelGap = 40.0f;
        constexpr float kAccelZGap    = 30.0f;

        if (hasAccelX || hasAccelY) {
            ImGui::SetCursorScreenPos({ imuColX + kCompassDiameter + kGyroAccelGap, mainRowStart.y });
            ImGui::BeginGroup();
            renderAccelCompass(tr("calibration.accel_compass"), "calibAccelCompass",
                              phys.accelY * gyroSign(m_editImu.accelYInvert, m_activeConfig.imu.accelYInvert),
                              phys.accelX * gyroSign(m_editImu.accelXInvert, m_activeConfig.imu.accelXInvert),
                              m_editImu.accelYDeadzone, m_editImu.accelYMax,
                              m_editImu.accelXDeadzone, m_editImu.accelXMax,
                              m_accelCompassDrag,
                              m_editImu.accelYInvert, m_editImu.accelXInvert);
            // accelZ has no compass of its own (no yaw-like counterpart — see the comment
            // above), so its Int/Ext/Actual line — and invert checkbox — stack right under
            // accel_x's, "*" pointing at the resting-orientation footnote below the Save button.
            if (hasAccelZ) {
                // Same fixed invert column as renderAccelCompass (same group, same left margin,
                // so the same offset lines the checkbox up under accel_y/accel_x's column) — to
                // the LEFT of the text, not after it.
                constexpr float kCheckboxColWidth = 30.0f;
                float textColX = ImGui::GetCursorScreenPos().x;
                ImVec2 rowPos = ImGui::GetCursorScreenPos();
                ImGui::Checkbox("##accelZInvert", &m_editImu.accelZInvert);
                ImGui::SetCursorScreenPos({ textColX + kCheckboxColWidth, rowPos.y });
                ImGui::Text("%s*: %s %.2f  %s %.2f  %s %.2f", tr("calibration.accel_z"),
                           tr("calibration.inner"), m_editImu.accelZDeadzone,
                           tr("calibration.outer"), m_editImu.accelZMax,
                           tr("calibration.current"), effAccelZ);
            }
            ImGui::EndGroup();
            mainRowBottom = std::max(mainRowBottom, ImGui::GetItemRectMax().y);
        }

        if (hasAccelZ) {
            ImGui::SetCursorScreenPos({ imuColX + kCompassDiameter + kGyroAccelGap
                                                  + kCompassDiameter + kAccelZGap, mainRowStart.y });
            ImGui::BeginGroup();
            renderImuAxisWidgetVertical("calibAccelZ", effAccelZ,
                                       m_editImu.accelZDeadzone, m_editImu.accelZMax, m_accelZDrag);
            ImGui::EndGroup();
            mainRowBottom = std::max(mainRowBottom, ImGui::GetItemRectMax().y);
        }
    }

    // Drawn last, now that mainRowBottom (the tallest column) is known.
    float sepX = afterSticksX + kSeparatorGap;
    ImGui::GetWindowDrawList()->AddLine({ sepX, mainRowStart.y }, { sepX, mainRowBottom },
                                        IM_COL32(90, 100, 120, 140), 1.5f);

    ImGui::SetCursorScreenPos({ mainRowStart.x, mainRowBottom });

    ImGui::Separator();
    ImGui::Spacing();

    if (m_activeConfig.imu.enabled && m_activeConfig.imu.accelZOffset != -1) {
        ImGui::TextDisabled("* %s", tr("calibration.accel_z_note"));
        ImGui::Spacing();
    }
}
