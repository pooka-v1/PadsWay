#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include "../PadEngine.h"
#include "../config/ConfigLoader.h"

// "Calibracion" section — per-axis deadzone/gain for sticks, triggers, gyro and accel, see
// ARCHITECTURE.md "Calibracion de entrada". Sticks, triggers and gyro/accel are all wired up
// (tasks 4-6 of that plan). Follows the same lifecycle as
// MacroManagerPanel: AppWindow calls init() once, activate() when the button is pressed,
// render() each frame while isActive(), and checks pollCalibrationSaved() to reload
// controllers.json after a change.
//
// Calibration always targets the currently active physical device (like MappingEditor's
// normal mode), not a manually picked one — there is nothing to calibrate without a mando
// connected.
class CalibrationPanel {
public:
    void init(PadEngine* engine);

    // Update the controller config snapshot (call after any load/reload of controllers.json).
    void setConfigs(const std::vector<ControllerConfig>& configs);

    void activate();
    bool isActive() const { return m_active; }
    void render();

    bool pollCalibrationSaved() { bool r = m_calibrationSaved; m_calibrationSaved = false; return r; }

private:
    bool m_active            = false;
    bool m_calibrationSaved  = false;

    PadEngine* m_engine = nullptr;
    std::vector<ControllerConfig> m_configs;

    // Snapshot of the active device's config, taken when the panel opens.
    ControllerConfig m_activeConfig;
    bool             m_hasActiveConfig  = false;
    std::string      m_activeDeviceName;

    // Working copy edited by the widgets; only written back to controllers.json on Save.
    // m_editImu carries the whole ImuConfig (offsets/scale/invert included) so it round-trips
    // untouched — only its *Deadzone/*Max fields are ever written by this panel.
    StickCalibration   m_editLeftStick;
    StickCalibration   m_editRightStick;
    TriggerCalibration m_editTriggerL;
    TriggerCalibration m_editTriggerR;
    ImuConfig          m_editImu;
    // Whole struct, same round-trip reasoning as m_editImu — only its xDeadzone/xMax/yDeadzone/
    // yMax fields are ever written by this panel, the rest (surfaceMode, zones, ...) just rides
    // along untouched back to m_activeConfig/saveCalibration() on Save.
    TouchpadConfig     m_editTouch;

    // Which HID axis (if any) feeds a given logical stick axis in this device's config, and its
    // invert flag as edited here. Invert doesn't live on StickCalibration — it's on
    // ControllerConfig::axes, keyed by HID source name (e.g. "hid_x"), not by logical stick
    // axis — so this panel has to find the matching entry by its `target` field first (done in
    // reload()) and remember which HID key to write back to on Save. hidKey empty = no matching
    // axis found on this device (checkbox stays hidden). originalInvert: the value at load time,
    // used to compute a live sign-flip preview before Save actually takes effect (see render()).
    struct AxisInvertRef {
        std::string hidKey;
        bool        invert         = false;
        bool        originalInvert = false;
    };
    AxisInvertRef m_leftXInvertRef, m_leftYInvertRef, m_rightXInvertRef, m_rightYInvertRef;

    // Finds the HID axis entry (if any) whose `target` feeds the given logical stick axis
    // ("left_x"/"left_y"/"right_x"/"right_y"); empty hidKey means none found.
    static AxisInvertRef findAxisInvert(const ControllerConfig& cfg, const char* target);

    std::string m_saveError;
    std::string m_toastMsg;
    ULONGLONG   m_toastTime = 0;

    // Which handle (if any) the mouse grabbed on the last click, per widget — held across
    // frames for the duration of the drag so movement keeps adjusting the same handle even
    // after the cursor drifts away from it. Inner/Outer means deadzone/max on stick rings and
    // trigger bars, deadzone/gain-derived position on the mirrored gyro/accel bars.
    enum class HandleDrag { None, Inner, Outer };
    HandleDrag m_leftStickDrag  = HandleDrag::None;
    HandleDrag m_rightStickDrag = HandleDrag::None;
    HandleDrag m_triggerLDrag   = HandleDrag::None;
    HandleDrag m_triggerRDrag   = HandleDrag::None;
    HandleDrag m_accelZDrag     = HandleDrag::None;

    // Same idea as HandleDrag, but a compass widget has up to 3 draggable axis pairs at once
    // (vertical, horizontal, and — gyro only — the yaw arc), sharing one InvisibleButton, so a
    // single flat enum identifies which specific handle is grabbed for the duration of a drag.
    enum class CompassHandle { None, VInner, VOuter, HInner, HOuter, AInner, AOuter };
    CompassHandle m_gyroCompassDrag  = CompassHandle::None;
    CompassHandle m_accelCompassDrag = CompassHandle::None;
    CompassHandle m_touchCrossDrag   = CompassHandle::None;

    // Shared by renderGyroCompass/renderAccelCompass/renderImuAxisWidgetVertical (compass image
    // size) and render()'s manual column layout for that row — see render()'s comment on why the
    // columns use SetCursorPos with this constant instead of SameLine's implicit width.
    static constexpr float kCompassRadius   = 100.0f;
    static constexpr float kCompassPad      = 14.0f;
    static constexpr float kCompassDiameter = (kCompassRadius + kCompassPad) * 2.0f;

    void reload();
    void save();

    // Draws one concentric-circle widget (radial deadzone/max) and lets the user drag the
    // inner or outer ring directly with the mouse to resize it; edits calib in place. rawX/
    // rawY: current physical reading, in [-1, 1], for the live dot (already sign-flipped for
    // preview if xInvert/yInvert differ from their originalInvert — see render()). drag: this
    // widget's own HandleDrag state. xInvert/yInvert: invert checkboxes drawn below the ring,
    // skipped when hidKey is empty (no matching HID axis found for this device).
    void renderStickWidget(const char* label, const char* idSuffix,
                           float rawX, float rawY, StickCalibration& calib, HandleDrag& drag,
                           AxisInvertRef& xInvert, AxisInvertRef& yInvert);

    // Draws one horizontal-bar widget (deadzone/max) for a physical trigger and lets the user
    // drag either handle directly; edits calib in place. rawValue: current physical trigger
    // reading, in [0, 1], for the live fill. mirrored: fills right-to-left instead of
    // left-to-right, so R2 mirrors L2 when the two sit side by side.
    void renderTriggerWidget(const char* label, const char* idSuffix,
                             float rawValue, TriggerCalibration& calib, HandleDrag& drag,
                             bool mirrored = false);

    // Draws one narrow *vertical* mirrored bar (deadzone/max, centered on 0) — used only for
    // accelZ, which has no paired gyro axis and so doesn't fit either compass below. No label or
    // numeric readout of its own — accelZ has no compass, so its text lives as an extra line
    // under the accel compass instead (see render()). Same deadzone/max threshold model as the
    // compasses (max = raw magnitude at which output already saturates; scale runs past 1.0 up
    // to kOuterCeiling). rawValue: current physical reading, in [-1, 1]. See ARCHITECTURE.md
    // "Calibracion de entrada".
    void renderImuAxisWidgetVertical(const char* idSuffix,
                                     float rawValue, float& deadzone, float& max, HandleDrag& drag);

    // Draws the combined gyro widget: a big sphere (matching the stick rings' size) with a
    // live tilt ball for pitch/roll (paired axes, same physical gesture as PadView's gyro
    // widget) plus a deadzone/max cross through the center — vertical tick pair for pitch,
    // horizontal tick pair for roll — and a curved arc along the top for yaw (no absolute
    // orientation to show for yaw, so it gets the CW/CCW arc instead of a ball position, same
    // reasoning as IDEAS.md's yaw clock-bar). All three axes' calib fields are edited in place;
    // drag identifies which of the (up to) 6 handles is currently grabbed. pitchInvert/
    // rollInvert/yawInvert: invert checkboxes drawn below the text lines (these are always valid
    // — an IMU-enabled device always has gyro, unlike accelZ which can be absent).
    void renderGyroCompass(const char* label, const char* idSuffix,
                           float rawPitch, float rawRoll, float rawYaw,
                           float& pitchDeadzone, float& pitchMax,
                           float& rollDeadzone, float& rollMax,
                           float& yawDeadzone, float& yawMax,
                           CompassHandle& drag,
                           bool& pitchInvert, bool& rollInvert, bool& yawInvert);

    // Same idea as renderGyroCompass but for accelY/accelX (paired with pitch/roll respectively,
    // see ComponentTypes.h's PhysicalAccel comment) — no arc, accel has no yaw counterpart.
    void renderAccelCompass(const char* label, const char* idSuffix,
                            float rawY, float rawX,
                            float& yDeadzone, float& yMax,
                            float& xDeadzone, float& xMax,
                            CompassHandle& drag,
                            bool& yInvert, bool& xInvert);

    // Max for the raw touch position, drawn as a crosshair (guide lines + tick marks) instead of
    // renderGyroCompass/renderAccelCompass's ring — a touchpad has no radial shape to preserve
    // (even a round one is read as two independent linear axes internally, see TouchpadConfig's
    // xMax/yMax comment in ControllerConfig.h), so drawing a reference circle around it would
    // suggest a boundary that isn't real. No deadzone handle (unlike gyro/accel) — see that same
    // comment for why a center deadzone is deliberately not a thing here. rawX/rawY: touch1X/Y
    // recentered on the pad's middle and scaled so an edge reads 1.0 (same convention as
    // gyro/accel's rest-centered axes). No invert — touch has no equivalent of the stick/gyro
    // axis-invert flag.
    void renderTouchCross(const char* label, const char* idSuffix,
                          float rawX, float rawY,
                          float& xMax, float& yMax,
                          CompassHandle& drag);
};
