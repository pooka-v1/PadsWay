#include "HIDInputSource.h"
#include "../GamepadState.h"
#include "../Log.h"
#include "StickSlotsHelper.h"
#include "TouchGestures.h"
#include <hidsdi.h>
#include <algorithm>
#include <cmath>
#include <vector>

#define PREPARSED  (static_cast<PHIDP_PREPARSED_DATA>(m_hid.preparsed()))

// HID Generic Desktop axis usage IDs
static constexpr USHORT kUsageX   = 0x30;
static constexpr USHORT kUsageY   = 0x31;
static constexpr USHORT kUsageZ   = 0x32;
static constexpr USHORT kUsageRx  = 0x33;
static constexpr USHORT kUsageRy  = 0x34;
static constexpr USHORT kUsageRz  = 0x35;
static constexpr USHORT kUsageHat = 0x39;

// Movimiento (Gestos) — starting thresholds, unmeasured (see ARCHITECTURE.md "Harness de
// umbrales de Gestos"), tuned by feel against real hardware rather than derived from harness
// data yet. Same physical-unit space as logTouchSession's dx/dy (normalized delta * maxX/maxY).
static constexpr float     kGestureMinDist        = 100.0f;
static constexpr ULONGLONG kTwoFingerWindowMs      = 150;

// ---------------------------------------------------------------------------

HIDInputSource::AxisUsage HIDInputSource::usageFromAxisName(const std::string& name) {
    if (name == "hid_x")     return { HID_USAGE_PAGE_GENERIC,    kUsageX  };
    if (name == "hid_y")     return { HID_USAGE_PAGE_GENERIC,    kUsageY  };
    if (name == "hid_z")     return { HID_USAGE_PAGE_GENERIC,    kUsageZ  };
    if (name == "hid_rx")    return { HID_USAGE_PAGE_GENERIC,    kUsageRx };
    if (name == "hid_ry")    return { HID_USAGE_PAGE_GENERIC,    kUsageRy };
    if (name == "hid_rz")    return { HID_USAGE_PAGE_GENERIC,    kUsageRz };
    if (name == "hid_brake") return { HID_USAGE_PAGE_SIMULATION, 0xC4     }; // Brake
    if (name == "hid_accel") return { HID_USAGE_PAGE_SIMULATION, 0xC5     }; // Accelerator
    return { 0, 0 };
}

// ---------------------------------------------------------------------------

HIDInputSource::HIDInputSource(const std::string& devicePath, const ControllerConfig& config)
    : m_hid(devicePath, config.source_name), m_config(config), m_name(config.source_name)
{
}

HIDInputSource::~HIDInputSource() {
}

// ---------------------------------------------------------------------------

bool HIDInputSource::isConnected() const {
    return m_hid.isConnected();
}

bool HIDInputSource::read(GamepadState& state) {
    auto result = m_hid.read(20);
    if (result == HIDDevice::ReadResult::Disconnected) return false;
    if (result == HIDDevice::ReadResult::Timeout) {
        state.touchDeltaX = 0.0f;
        state.touchDeltaY = 0.0f;
        if (++m_readCount % 240 == 0)
            spdlog::trace("[HID][{}] lx={:.2f} ly={:.2f} rx={:.2f} ry={:.2f} tL={:.2f} tR={:.2f} btns={:08X} (no report)",
                   m_name,
                   state.leftX, state.leftY, state.rightX, state.rightY,
                   state.triggerL, state.triggerR, m_lastButtonMask);
        return true;
    }

    PCHAR buf      = reinterpret_cast<PCHAR>(const_cast<BYTE*>(m_hid.reportBuf().data()));
    ULONG bufLen   = m_hid.reportLen();
    ULONG bytesRead = m_hid.lastBytesRead();

    // Generic decode (buttons/axes/hat/raw bytes), independent of m_config — see getLastRawSnapshot().
    decodeRawHIDReport(m_hid, m_lastRawSnapshot);

    // Diagnostic: log raw bytes every ~250ms (30 reads * 8ms) — full report, no offset cap
    if (++m_readCount % 30 == 0) {
        ULONG dumpLen = bufLen;
        std::string raw;
        raw.reserve(dumpLen * 3);
        for (ULONG i = 0; i < dumpLen; ++i) {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%02X ", (unsigned char)buf[i]);
            raw += tmp;
        }
        spdlog::trace("[HID][raw] {}", raw);
    }

    bool hasAxisDpad = false;
    for (const auto& [src, m] : m_config.axes)
        if (m.target == "dpad_x" || m.target == "dpad_y") { hasAxisDpad = true; break; }

    if (m_hasPhysicalController) {
        // ── Component-system path ─────────────────────────────────────────────
        m_physicalState = {};
        buildPhysicalButtons(buf, bufLen);
        buildPhysicalAxes(buf, bufLen);
        // Gyro/accel must land in m_physicalState (the "physical" input to process() below),
        // not just in the output `state` — otherwise PhysicalGyro/PhysicalAccel always see
        // gyroActive/accelActive false and every gyro_actions/accel_actions mapping is a no-op,
        // even though the widget still shows live motion (it reads the applyIMU(state) call
        // further down, which stays as-is for the legacy path and as an output passthrough here).
        applyIMU(buf, bytesRead, m_physicalState);

        // Hat switch → physical state; process() handles virtual output via PhysicalDpadDir.
        if (!hasAxisDpad && m_config.dpad == "hid_hat") {
            ULONG hatValue = 0xFFFFFFFF;
            NTSTATUS hatStatus = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, kUsageHat,
                                                    &hatValue, PREPARSED, buf, bufLen);
            if (hatStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
                char savedId = buf[0];
                buf[0] = static_cast<char>(m_hid.buttonReportId());
                HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, kUsageHat,
                                   &hatValue, PREPARSED, buf, bufLen);
                buf[0] = savedId;
            }
            bool hatUp = false, hatDown = false, hatLeft = false, hatRight = false;
            auto hatCapIt = m_hid.valueCaps().find(kUsageHat);
            DWORD normHat = 0xFFFFFFFF;
            if (hatCapIt != m_hid.valueCaps().end()) {
                ULONG hatMin = static_cast<ULONG>(hatCapIt->second.logMin);
                ULONG hatMax = static_cast<ULONG>(hatCapIt->second.logMax);
                if (hatValue >= hatMin && hatValue <= hatMax) {
                    normHat = hatValue - hatMin;
                    parseHIDDpad(normHat, hatUp, hatDown, hatLeft, hatRight);
                }
            } else {
                normHat = hatValue;
                parseHIDDpad(hatValue, hatUp, hatDown, hatLeft, hatRight);
            }
            m_lastRawHat.store(normHat);
            m_physicalState.dpadUp    = hatUp;
            m_physicalState.dpadDown  = hatDown;
            m_physicalState.dpadLeft  = hatLeft;
            m_physicalState.dpadRight = hatRight;
        }

        state = {};
        // Must run BEFORE process(): PhysicalTouchpad::process() reads touch1Active/X/Y (and, for
        // Analog mode, drives a stick accumulator from them) off m_physicalState, the same way
        // applyIMU() above feeds gyro/accel into it. Calling this after process() (as used to be
        // the case, back when only Mouse mode existed and read state.touchDelta* directly outside
        // the Component System) left m_physicalState.touch1Active permanently false — wiped by the
        // `m_physicalState = {}` at the top of this branch and never refreshed in time — so Analog
        // mode's condition on physical.touch1Active could never fire.
        applyTouchpad(buf, bytesRead, state);
        m_physicalController.process(m_physicalState, state);
        applyAxesResidual(buf, bufLen, state);

        spdlog::trace("[HID][{}] lx={:.2f} ly={:.2f} rx={:.2f} ry={:.2f} tL={:.2f} tR={:.2f} btns={:08X}",
               m_name, state.leftX, state.leftY, state.rightX, state.rightY,
               state.triggerL, state.triggerR, m_lastButtonMask);
    } else {
        // ── Legacy path (no PhysicalController loaded) ────────────────────────
        applyButtons(buf, bufLen, state);
        applyAxes   (buf, bufLen, state);

        if (!hasAxisDpad && m_config.dpad == "hid_hat") {
            ULONG hatValue = 0xFFFFFFFF;
            NTSTATUS hatStatus = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, kUsageHat,
                                                    &hatValue, PREPARSED, buf, bufLen);
            if (hatStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
                char savedId = buf[0];
                buf[0] = static_cast<char>(m_hid.buttonReportId());
                HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, kUsageHat,
                                   &hatValue, PREPARSED, buf, bufLen);
                buf[0] = savedId;
            }
            bool hatUp = false, hatDown = false, hatLeft = false, hatRight = false;
            auto hatCapIt = m_hid.valueCaps().find(kUsageHat);
            DWORD normHat = 0xFFFFFFFF;
            if (hatCapIt != m_hid.valueCaps().end()) {
                ULONG hatMin = static_cast<ULONG>(hatCapIt->second.logMin);
                ULONG hatMax = static_cast<ULONG>(hatCapIt->second.logMax);
                if (hatValue >= hatMin && hatValue <= hatMax) {
                    normHat = hatValue - hatMin;
                    parseHIDDpad(normHat, hatUp, hatDown, hatLeft, hatRight);
                }
            } else {
                normHat = hatValue;
                parseHIDDpad(hatValue, hatUp, hatDown, hatLeft, hatRight);
            }
            m_lastRawHat.store(normHat);
            m_physicalState.dpadUp    = hatUp;
            m_physicalState.dpadDown  = hatDown;
            m_physicalState.dpadLeft  = hatLeft;
            m_physicalState.dpadRight = hatRight;
            state.dpadUp    |= hatUp;
            state.dpadDown  |= hatDown;
            state.dpadLeft  |= hatLeft;
            state.dpadRight |= hatRight;
        }

        if (!m_config.dpadRemap.empty()) {
            bool wasUp = state.dpadUp, wasDown = state.dpadDown,
                 wasLeft = state.dpadLeft, wasRight = state.dpadRight;
            auto applyRemap = [&](bool active, const std::string& dir, bool& srcFlag) {
                if (!active) return;
                auto it = m_config.dpadRemap.find(dir);
                if (it == m_config.dpadRemap.end()) return;
                srcFlag = false;
                const std::string& v = it->second;
                if      (v == "a")          state.btnA     = true;
                else if (v == "b")          state.btnB     = true;
                else if (v == "x")          state.btnX     = true;
                else if (v == "y")          state.btnY     = true;
                else if (v == "l1")         state.btnLB    = true;
                else if (v == "r1")         state.btnRB    = true;
                else if (v == "select")     state.btnBack  = true;
                else if (v == "start")      state.btnStart = true;
                else if (v == "home")       state.btnHome  = true;
                else if (v == "l3")         state.btnL3    = true;
                else if (v == "r3")         state.btnR3    = true;
                else if (v == "dpad_up")    state.dpadUp    = true;
                else if (v == "dpad_down")  state.dpadDown  = true;
                else if (v == "dpad_left")  state.dpadLeft  = true;
                else if (v == "dpad_right") state.dpadRight = true;
            };
            applyRemap(wasUp,    "up",    state.dpadUp);
            applyRemap(wasDown,  "down",  state.dpadDown);
            applyRemap(wasLeft,  "left",  state.dpadLeft);
            applyRemap(wasRight, "right", state.dpadRight);
        }

        for (const auto& [bit, action] : m_config.buttons) {
            if (action.type != ButtonActionType::VirtualButton) continue;
            if (!action.physical.empty()) {
                bool srcIsSlot = false;
                for (const auto& [slot, srcs] : m_config.stickSlots)
                    for (const auto& src : srcs)
                        if (src == action.physical) { srcIsSlot = true; break; }
                if (srcIsSlot) continue;
            }
            bool pressed = (m_lastButtonMask & (1u << (bit - 1))) != 0;
            if (!pressed) continue;
            if      (action.name == "dpad_up")    state.dpadUp    = true;
            else if (action.name == "dpad_down")  state.dpadDown  = true;
            else if (action.name == "dpad_left")  state.dpadLeft  = true;
            else if (action.name == "dpad_right") state.dpadRight = true;
        }

        applyStickSlots(m_config, m_physicalState, state);
        applyTouchpad(buf, bytesRead, state);

        spdlog::trace("[HID][{}] lx={:.2f} ly={:.2f} rx={:.2f} ry={:.2f} tL={:.2f} tR={:.2f} btns={:08X}",
               m_name, state.leftX, state.leftY, state.rightX, state.rightY,
               state.triggerL, state.triggerR, m_lastButtonMask);
    }

    applyIMU     (buf, bytesRead, state);
    applyImuActions();

    return true;
}

// ---------------------------------------------------------------------------

void HIDInputSource::applyButtons(PCHAR buf, ULONG bufLen, GamepadState& state) {
    USAGE usages[128];
    ULONG usageCount = 128;

    NTSTATUS btnStatus = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                                        usages, &usageCount, PREPARSED, buf, bufLen);

    // If the device sends a different Report ID than the one in the descriptor,
    // temporarily swap it so HidP can parse the same data layout.
    if (btnStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(m_hid.buttonReportId());
        usageCount = 128;
        btnStatus = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                                   usages, &usageCount, PREPARSED, buf, bufLen);
        buf[0] = savedId;
    }

    if (btnStatus != HIDP_STATUS_SUCCESS) {
        if (++m_btnErrCount <= 3)
            spdlog::warn("[HID] HidP_GetUsages failed: 0x{:08X} (count={})",
                         static_cast<unsigned>(btnStatus), usageCount);
        return;
    }

    m_lastButtonMask = 0;
    for (ULONG i = 0; i < usageCount; ++i)
        if (usages[i] >= 1 && usages[i] <= 32)
            m_lastButtonMask |= (1u << (usages[i] - 1));

    // OR semantics: only set true, never overwrite with false.
    // GamepadState starts zeroed each frame, so un-pressed buttons are already false.
    // This allows multiple physical buttons to map to the same virtual target.
    auto setBtn = [&](const std::string& name, bool v) {
        if (!v) return;
        if      (name == "a")         state.btnA     = true;
        else if (name == "b")         state.btnB     = true;
        else if (name == "x")         state.btnX     = true;
        else if (name == "y")         state.btnY     = true;
        else if (name == "l1")        state.btnLB    = true;
        else if (name == "r1")        state.btnRB    = true;
        else if (name == "select")    state.btnBack  = true;
        else if (name == "start")     state.btnStart = true;
        else if (name == "home")      state.btnHome  = true;
        else if (name == "l3")        state.btnL3    = true;
        else if (name == "r3")        state.btnR3    = true;
        else if (name == "l4")        state.btnL4    = true;
        else if (name == "r4")        state.btnR4    = true;
        else if (name == "lp")        state.btnLP    = true;
        else if (name == "rp")        state.btnRP    = true;
        else if (name == "touch_btn") state.btnTouch = true;
    };

    // Physical display state: build separately using action.physical names.
    // Must run BEFORE virtual loop so display and ViGEm output stay independent.
    // Do NOT inherit from state here — state has last frame's remapped axes, not physical ones.
    // Axis physical values are written by applyAxes() using stickId (physical position).
    GamepadState physDisplay = {};
    auto setPhys = [&](const std::string& name, bool v) {
        if      (name == "a")         physDisplay.btnA     = v;
        else if (name == "b")         physDisplay.btnB     = v;
        else if (name == "x")         physDisplay.btnX     = v;
        else if (name == "y")         physDisplay.btnY     = v;
        else if (name == "l1")        physDisplay.btnLB    = v;
        else if (name == "r1")        physDisplay.btnRB    = v;
        else if (name == "select")    physDisplay.btnBack  = v;
        else if (name == "start")     physDisplay.btnStart = v;
        else if (name == "home")      physDisplay.btnHome  = v;
        else if (name == "l3")        physDisplay.btnL3    = v;
        else if (name == "r3")        physDisplay.btnR3    = v;
        else if (name == "l4")        physDisplay.btnL4    = v;
        else if (name == "r4")        physDisplay.btnR4    = v;
        else if (name == "lp")        physDisplay.btnLP    = v;
        else if (name == "rp")        physDisplay.btnRP    = v;
        else if (name == "touch_btn") physDisplay.btnTouch = v;
    };
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.physical.empty()) continue;
        bool pressed = (m_lastButtonMask & (1u << (bit - 1))) != 0;
        setPhys(action.physical, pressed);
        // Track physical L2/R2 trigger buttons (not remapped buttons acting as triggers)
        if (pressed && action.type == ButtonActionType::Trigger) {
            if (action.physical == "l2") physDisplay.triggerL = 1.0f;
            else if (action.physical == "r2") physDisplay.triggerR = 1.0f;
        }
    }
    m_physicalState = physDisplay;
    // Restore axis values overwritten by the button-only physDisplay assignment.
    // applyAxes() already wrote stickId axes to m_physicalState, but physDisplay zeroed them.
    buildPhysicalAxes(buf, bufLen);

    // Reset virtual button states before remapping so OR logic works correctly
    // regardless of unordered_map iteration order.
    state.btnA = state.btnB = state.btnX    = state.btnY   = false;
    state.btnLB = state.btnRB = false;
    state.btnBack = state.btnStart = state.btnHome = false;
    state.btnL3 = state.btnR3 = false;
    state.btnL4 = state.btnR4 = false;
    state.btnLP = state.btnRP = state.btnTouch = false;
    // Triggers also reset each frame so button-mapped triggers clear when released
    state.triggerL = state.triggerR = 0.0f;
    // Dpad bits reset so axis_actions Dpad assignments clear when stick returns to neutral
    state.dpadUp = state.dpadDown = state.dpadLeft = state.dpadRight = false;
    // Mouse delta reset each frame so axis_actions mouse_move stops when stick returns to neutral
    state.mouseX = state.mouseY = 0.0f;

    // Buttons whose physical identity is a stick slot source lose their virtual
    // action entirely (one input → one output). Checked against action.physical
    // so unrelated buttons remapped to the same virtual target are not suppressed.
    auto isSlotSrc = [&](const std::string& phys) -> bool {
        if (phys.empty() || m_config.stickSlots.empty()) return false;
        for (const auto& [slot, srcs] : m_config.stickSlots)
            for (const auto& src : srcs)
                if (src == phys) return true;
        return false;
    };

    // Virtual remapping: use action.name so ViGEm receives the mapped output.
    for (const auto& [bit, action] : m_config.buttons) {
        if (isSlotSrc(action.physical)) continue;
        bool pressed = (m_lastButtonMask & (1u << (bit - 1))) != 0;
        switch (action.type) {
        case ButtonActionType::VirtualButton:
            setBtn(action.name, pressed);
            break;
        case ButtonActionType::Trigger:
            if (pressed) {
                if      (action.target == "l2") state.triggerL = 1.0f;
                else if (action.target == "r2") state.triggerR = 1.0f;
            }
            break;
        default: break;
        }
    }

    if (state.triggerL > 0.0f && state.triggerR > 0.0f) {
        state.triggerL = 0.0f;
        state.triggerR = 0.0f;
    }
}

void HIDInputSource::applyAxes(PCHAR buf, ULONG bufLen, GamepadState& state) {
    // OR-semantics button setter (same as setBtn in applyButtons; needed for btn_dir target)
    auto setBtn = [&](const std::string& name, bool v) {
        if (!v) return;
        if      (name == "a")         state.btnA     = true;
        else if (name == "b")         state.btnB     = true;
        else if (name == "x")         state.btnX     = true;
        else if (name == "y")         state.btnY     = true;
        else if (name == "l1")        state.btnLB    = true;
        else if (name == "r1")        state.btnRB    = true;
        else if (name == "select")    state.btnBack  = true;
        else if (name == "start")     state.btnStart = true;
        else if (name == "home")      state.btnHome  = true;
        else if (name == "l3")        state.btnL3    = true;
        else if (name == "r3")        state.btnR3    = true;
    };

    for (const auto& [source, mapping] : m_config.axes) {
        AxisUsage au = usageFromAxisName(source);
        if (au.usage == 0) continue;

        // Use the page from the device descriptor when available — handles devices
        // that put axes on a non-standard page (e.g. triggers on page 0x01 instead of 0x02).
        auto pit = m_hid.usagePage().find(au.usage);
        USHORT page = (pit != m_hid.usagePage().end()) ? pit->second : au.page;

        ULONG rawValue = 0;
        NTSTATUS axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                               au.usage, &rawValue, PREPARSED, buf, bufLen);
        if (axStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_hid.buttonReportId());
            axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                          au.usage, &rawValue, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }

        if (axStatus != HIDP_STATUS_SUCCESS)
            continue;

        float v = m_hid.normalizeAxis(au.usage, rawValue);
        if (mapping.invert) v = -v;

        // Physical display: write to stickId position (physical axis), not target (virtual axis).
        // This ensures the physical pad shows the stick that is actually moving on the hardware,
        // regardless of where it is routed by the user's mapping.
        if (!mapping.stickId.empty()) {
            if      (mapping.stickId == "left_x")  m_physicalState.leftX  = v;
            else if (mapping.stickId == "left_y")  m_physicalState.leftY  = v;
            else if (mapping.stickId == "right_x") m_physicalState.rightX = v;
            else if (mapping.stickId == "right_y") m_physicalState.rightY = v;
        }

        if      (mapping.target == "left_x")          state.leftX   = v;
        else if (mapping.target == "left_y")          state.leftY   = v;
        else if (mapping.target == "right_x")         state.rightX  = v;
        else if (mapping.target == "right_y")         state.rightY  = v;
        else if (mapping.target == "trigger_l")       { float tv = (v + 1.0f) * 0.5f; m_physicalState.triggerL = tv; if (tv > state.triggerL) state.triggerL = tv; }
        else if (mapping.target == "trigger_r")       { float tv = (v + 1.0f) * 0.5f; m_physicalState.triggerR = tv; if (tv > state.triggerR) state.triggerR = tv; }
        else if (mapping.target == "trigger_combined") {
            float tl = (v > 0.0f) ?  v : 0.0f;
            float tr = (v < 0.0f) ? -v : 0.0f;
            m_physicalState.triggerL = tl; m_physicalState.triggerR = tr;
            if (tl > state.triggerL) state.triggerL = tl;
            if (tr > state.triggerR) state.triggerR = tr;
        }
        else if (mapping.target == "mouse_x") state.mouseX = v;
        else if (mapping.target == "mouse_y") state.mouseY = v;
        else if (mapping.target == "dpad_x") {
            state.dpadLeft  = v < -mapping.threshold;
            state.dpadRight = v >  mapping.threshold;
        }
        else if (mapping.target == "dpad_y") {
            state.dpadUp   = v < -mapping.threshold;
            state.dpadDown = v >  mapping.threshold;
        }
        else if (mapping.target == "btn_dir") {
            if (!mapping.btnNeg.empty()) setBtn(mapping.btnNeg, v < -mapping.threshold);
            if (!mapping.btnPos.empty()) setBtn(mapping.btnPos, v >  mapping.threshold);
        }
    }

    // ── axis_actions: per-direction half-axis processing ─────────────────────
    // !! LEGACY PATH — DEPRECATED !!
    // Used only when m_hasPhysicalController == false (controllers not yet migrated to P4/P5).
    // The Component System path (checkHalf, below) supersedes this entirely.
    // Remove this block when P6 is implemented and all controllers are migrated.
    // For each axis that has entries in axis_actions, process positive and negative halves.
    // This runs after whole-axis processing so both can coexist during transition.
    m_activeAxisActions.clear();
    m_activeAxisRangeActions.clear();
    for (const auto& [source, mapping] : m_config.axes) {
        if (m_config.axis_actions.empty()) break;  // fast path: nothing to do

        AxisUsage au = usageFromAxisName(source);
        if (au.usage == 0) continue;
        auto pit = m_hid.usagePage().find(au.usage);
        USHORT page = (pit != m_hid.usagePage().end()) ? pit->second : au.page;
        ULONG rawValue = 0;
        if (HidP_GetUsageValue(HidP_Input, page, 0, au.usage, &rawValue, PREPARSED, buf, bufLen)
            != HIDP_STATUS_SUCCESS) continue;

        float v = m_hid.normalizeAxis(au.usage, rawValue);
        if (mapping.invert) v = -v;

        auto processHalf = [&](const std::string& key, float halfV) {
            auto ait = m_config.axis_actions.find(key);
            if (ait == m_config.axis_actions.end()) return;
            const HalfAxisAction& ha = ait->second;
            float absV = std::abs(halfV);

            switch (ha.type) {
            case HalfAxisActionType::Analog: {
                float outV = absV * ha.scale;
                if (ha.outDir == "neg") outV = -outV;
                if      (ha.target == "left_x")  state.leftX  = outV;
                else if (ha.target == "left_y")  state.leftY  = outV;
                else if (ha.target == "right_x") state.rightX = outV;
                else if (ha.target == "right_y") state.rightY = outV;
                break;
            }
            case HalfAxisActionType::VirtualButton:
                if (absV > ha.threshold) setBtn(ha.target, true);
                break;
            case HalfAxisActionType::Dpad:
                if (absV > ha.threshold) {
                    if      (ha.target == "up")    state.dpadUp    = true;
                    else if (ha.target == "down")  state.dpadDown  = true;
                    else if (ha.target == "left")  state.dpadLeft  = true;
                    else if (ha.target == "right") state.dpadRight = true;
                }
                break;
            case HalfAxisActionType::Trigger:
                if (absV > ha.threshold) {
                    if      (ha.target == "l2" || ha.target == "trigger_l") state.triggerL = absV;
                    else if (ha.target == "r2" || ha.target == "trigger_r") state.triggerR = absV;
                }
                break;
            case HalfAxisActionType::StickSlot:
                if (absV > ha.threshold) {
                    if      (ha.target == "left_x_pos")  state.leftX  =  1.0f;
                    else if (ha.target == "left_x_neg")  state.leftX  = -1.0f;
                    else if (ha.target == "left_y_pos")  state.leftY  =  1.0f;
                    else if (ha.target == "left_y_neg")  state.leftY  = -1.0f;
                    else if (ha.target == "right_x_pos") state.rightX =  1.0f;
                    else if (ha.target == "right_x_neg") state.rightX = -1.0f;
                    else if (ha.target == "right_y_pos") state.rightY =  1.0f;
                    else if (ha.target == "right_y_neg") state.rightY = -1.0f;
                }
                break;
            case HalfAxisActionType::MouseMove:
                if      (ha.target == "mouse_x") state.mouseX += halfV * ha.speed;
                else if (ha.target == "mouse_y") state.mouseY += halfV * ha.speed;
                break;
            case HalfAxisActionType::Ranges:
                for (const auto& r : ha.ranges) {
                    if (absV < r.from || absV > r.to || !r.hasAction) continue;
                    switch (r.action.type) {
                    case ButtonActionType::VirtualButton: setBtn(r.action.name, true); break;
                    case ButtonActionType::Keyboard:
                    case ButtonActionType::MouseClick:
                    case ButtonActionType::Macro:
                    case ButtonActionType::Bot:
                        m_activeAxisRangeActions[key] = r.action;
                        break;
                    default: break;
                    }
                    break;
                }
                break;
            case HalfAxisActionType::Macro:
            case HalfAxisActionType::Keyboard:
            case HalfAxisActionType::MouseClick:
            case HalfAxisActionType::Bot:
                if (absV > ha.threshold)
                    m_activeAxisActions.push_back(key);
                break;
            }
            // Suppress raw axis contribution for this half when redirected to a non-analog target.
            // Mirrors btn_dir: when a half-axis is repurposed it stops feeding the virtual stick.
            if (ha.type != HalfAxisActionType::Analog && ha.type != HalfAxisActionType::Ranges) {
                if      (mapping.target == "left_x")  state.leftX  = (halfV < 0.0f) ? (state.leftX  > 0.0f ? state.leftX  : 0.0f) : (state.leftX  < 0.0f ? state.leftX  : 0.0f);
                else if (mapping.target == "left_y")  state.leftY  = (halfV < 0.0f) ? (state.leftY  > 0.0f ? state.leftY  : 0.0f) : (state.leftY  < 0.0f ? state.leftY  : 0.0f);
                else if (mapping.target == "right_x") state.rightX = (halfV < 0.0f) ? (state.rightX > 0.0f ? state.rightX : 0.0f) : (state.rightX < 0.0f ? state.rightX : 0.0f);
                else if (mapping.target == "right_y") state.rightY = (halfV < 0.0f) ? (state.rightY > 0.0f ? state.rightY : 0.0f) : (state.rightY < 0.0f ? state.rightY : 0.0f);
            }
        };

        // Key uses virtual axis name (mapping.target) so JSON entries match: "left_x_pos" etc.
        if (v >= 0.0f) processHalf(mapping.target + "_pos", v);
        else           processHalf(mapping.target + "_neg", v);
    }
}

// ---------------------------------------------------------------------------
// Component-system path
// ---------------------------------------------------------------------------

void HIDInputSource::buildPhysicalButtons(PCHAR buf, ULONG bufLen) {
    USAGE usages[128];
    ULONG usageCount = 128;

    NTSTATUS btnStatus = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                                        usages, &usageCount, PREPARSED, buf, bufLen);
    if (btnStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(m_hid.buttonReportId());
        usageCount = 128;
        btnStatus = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                                   usages, &usageCount, PREPARSED, buf, bufLen);
        buf[0] = savedId;
    }
    if (btnStatus != HIDP_STATUS_SUCCESS) {
        if (++m_btnErrCount <= 3)
            spdlog::warn("[HID] HidP_GetUsages failed: 0x{:08X} (count={})",
                         static_cast<unsigned>(btnStatus), usageCount);
        return;
    }

    m_lastButtonMask = 0;
    for (ULONG i = 0; i < usageCount; ++i)
        if (usages[i] >= 1 && usages[i] <= 32)
            m_lastButtonMask |= (1u << (usages[i] - 1));

    auto setPhys = [&](const std::string& name, bool v) {
        if      (name == "a")         m_physicalState.btnA     = v;
        else if (name == "b")         m_physicalState.btnB     = v;
        else if (name == "x")         m_physicalState.btnX     = v;
        else if (name == "y")         m_physicalState.btnY     = v;
        else if (name == "l1")        m_physicalState.btnLB    = v;
        else if (name == "r1")        m_physicalState.btnRB    = v;
        else if (name == "select")    m_physicalState.btnBack  = v;
        else if (name == "start")     m_physicalState.btnStart = v;
        else if (name == "home")      m_physicalState.btnHome  = v;
        else if (name == "l3")        m_physicalState.btnL3    = v;
        else if (name == "r3")        m_physicalState.btnR3    = v;
        else if (name == "l4")        m_physicalState.btnL4    = v;
        else if (name == "r4")        m_physicalState.btnR4    = v;
        else if (name == "lp")        m_physicalState.btnLP    = v;
        else if (name == "rp")        m_physicalState.btnRP    = v;
        else if (name == "touch_btn") m_physicalState.btnTouch = v;
    };
    for (const auto& [bit, action] : m_config.buttons) {
        if (action.physical.empty()) continue;
        bool pressed = (m_lastButtonMask & (1u << (bit - 1))) != 0;
        setPhys(action.physical, pressed);
        if (pressed && action.type == ButtonActionType::Trigger) {
            if      (action.physical == "l2") m_physicalState.triggerL = 1.0f;
            else if (action.physical == "r2") m_physicalState.triggerR = 1.0f;
        }
    }
}

void HIDInputSource::buildPhysicalAxes(PCHAR buf, ULONG bufLen) {
    for (const auto& [source, mapping] : m_config.axes) {
        AxisUsage au = usageFromAxisName(source);
        if (au.usage == 0) continue;

        auto pit = m_hid.usagePage().find(au.usage);
        USHORT page = (pit != m_hid.usagePage().end()) ? pit->second : au.page;

        ULONG rawValue = 0;
        NTSTATUS axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                               au.usage, &rawValue, PREPARSED, buf, bufLen);
        if (axStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_hid.buttonReportId());
            axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                          au.usage, &rawValue, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        if (axStatus != HIDP_STATUS_SUCCESS) continue;

        float v = m_hid.normalizeAxis(au.usage, rawValue);
        if (mapping.invert) v = -v;

        // Physical position (stickId): write signed value so process() can decompose pos/neg halves.
        if (!mapping.stickId.empty()) {
            if      (mapping.stickId == "left_x")  m_physicalState.leftX  = v;
            else if (mapping.stickId == "left_y")  m_physicalState.leftY  = v;
            else if (mapping.stickId == "right_x") m_physicalState.rightX = v;
            else if (mapping.stickId == "right_y") m_physicalState.rightY = v;
        } else {
            // No stickId override: physical position = virtual target (common case).
            if      (mapping.target == "left_x")  m_physicalState.leftX  = v;
            else if (mapping.target == "left_y")  m_physicalState.leftY  = v;
            else if (mapping.target == "right_x") m_physicalState.rightX = v;
            else if (mapping.target == "right_y") m_physicalState.rightY = v;
        }

        // Triggers: convert signed [-1,1] → [0,1] for m_physicalState.
        // trigger_combined is split and handled in applyAxesResidual.
        if      (mapping.target == "trigger_l") m_physicalState.triggerL = (v + 1.0f) * 0.5f;
        else if (mapping.target == "trigger_r") m_physicalState.triggerR = (v + 1.0f) * 0.5f;
    }
}

void HIDInputSource::applyAxesResidual(PCHAR buf, ULONG bufLen, GamepadState& state) {
    auto setBtn = [&](const std::string& name, bool v) {
        if (!v) return;
        if      (name == "a")      state.btnA     = true;
        else if (name == "b")      state.btnB     = true;
        else if (name == "x")      state.btnX     = true;
        else if (name == "y")      state.btnY     = true;
        else if (name == "l1")     state.btnLB    = true;
        else if (name == "r1")     state.btnRB    = true;
        else if (name == "select") state.btnBack  = true;
        else if (name == "start")  state.btnStart = true;
        else if (name == "home")   state.btnHome  = true;
        else if (name == "l3")     state.btnL3    = true;
        else if (name == "r3")     state.btnR3    = true;
    };

    m_activeAxisActions.clear();
    m_activeAxisRangeActions.clear();
    for (const auto& [source, mapping] : m_config.axes) {
        AxisUsage au = usageFromAxisName(source);
        if (au.usage == 0) continue;

        auto pit = m_hid.usagePage().find(au.usage);
        USHORT page = (pit != m_hid.usagePage().end()) ? pit->second : au.page;

        ULONG rawValue = 0;
        NTSTATUS axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                               au.usage, &rawValue, PREPARSED, buf, bufLen);
        if (axStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_hid.buttonReportId() != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_hid.buttonReportId());
            axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                          au.usage, &rawValue, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        if (axStatus != HIDP_STATUS_SUCCESS) continue;

        float v = m_hid.normalizeAxis(au.usage, rawValue);
        if (mapping.invert) v = -v;

        // Targets not handled by PhysicalController::process() — write directly to state.
        if (mapping.target == "mouse_x") state.mouseX = v;
        else if (mapping.target == "mouse_y") state.mouseY = v;
        else if (mapping.target == "dpad_x") {
            state.dpadLeft  = v < -mapping.threshold;
            state.dpadRight = v >  mapping.threshold;
        }
        else if (mapping.target == "dpad_y") {
            state.dpadUp   = v < -mapping.threshold;
            state.dpadDown = v >  mapping.threshold;
        }
        else if (mapping.target == "trigger_combined") {
            float tl = (v > 0.0f) ?  v : 0.0f;
            float tr = (v < 0.0f) ? -v : 0.0f;
            m_physicalState.triggerL = tl; m_physicalState.triggerR = tr;
            if (tl > state.triggerL) state.triggerL = tl;
            if (tr > state.triggerR) state.triggerR = tr;
        }
        else if (mapping.target == "btn_dir") {
            if (!mapping.btnNeg.empty()) setBtn(mapping.btnNeg, v < -mapping.threshold);
            if (!mapping.btnPos.empty()) setBtn(mapping.btnPos, v >  mapping.threshold);
        }

        // axis_actions: only Macro/Keyboard/MouseClick need m_activeAxisActions.
        // All other types (VirtualButton, Dpad, Trigger, StickSlot, Analog, MouseMove) are
        // handled by PhysicalAnalogDir inside m_physicalController.process().
        if (!m_config.axis_actions.empty()) {
            auto checkHalf = [&](const std::string& key, float halfV) {
                auto ait = m_config.axis_actions.find(key);
                if (ait == m_config.axis_actions.end()) return;
                const HalfAxisAction& ha = ait->second;
                float absV = std::abs(halfV);
                switch (ha.type) {
                case HalfAxisActionType::Macro:
                case HalfAxisActionType::Keyboard:
                case HalfAxisActionType::MouseClick:
                case HalfAxisActionType::Bot:
                    if (absV > ha.threshold)
                        m_activeAxisActions.push_back(key);
                    break;
                case HalfAxisActionType::Ranges:
                    for (const auto& r : ha.ranges) {
                        if (absV < r.from || absV > r.to || !r.hasAction) continue;
                        if (r.action.type == ButtonActionType::Keyboard   ||
                            r.action.type == ButtonActionType::MouseClick  ||
                            r.action.type == ButtonActionType::Macro       ||
                            r.action.type == ButtonActionType::Bot)
                            m_activeAxisRangeActions[key] = r.action;
                        break;
                    }
                    break;
                default: break;
                }
            };
            if (v >= 0.0f) checkHalf(mapping.target + "_pos", v);
            else           checkHalf(mapping.target + "_neg", v);
        }
    }
}

// ---------------------------------------------------------------------------
// Gesture-threshold harness — see ARCHITECTURE.md "Touchpad", "Harness de umbrales de Gestos".
// Captures raw session data only (no gesture classification here, by design — that's what these
// numbers are meant to let someone design later, not something the harness should presuppose).
void HIDInputSource::logTouchSession(int finger, float x0, float y0, float x1, float y1,
                                      ULONGLONG startMs) const {
    float dx   = (x1 - x0) * static_cast<float>(m_config.touchpad.maxX);
    float dy   = (y1 - y0) * static_cast<float>(m_config.touchpad.maxY);
    float dist = std::sqrt(dx * dx + dy * dy);
    float angleDeg = std::atan2(dy, dx) * 180.0f / 3.14159265f;
    ULONGLONG durationMs = GetTickCount64() - startMs;
    spdlog::debug("[TOUCH][sess] finger={} x0={:.3f} y0={:.3f} x1={:.3f} y1={:.3f} "
                  "dx={:.1f} dy={:.1f} dist={:.1f} angleDeg={:.1f} durationMs={}",
                  finger, x0, y0, x1, y1, dx, dy, dist, angleDeg, durationMs);
}

// ---------------------------------------------------------------------------
// Movimiento (Gestos) — see TouchGestures.h and ARCHITECTURE.md "Touchpad" -> "Movimiento".
// A non-concurrent session (only this finger ever touched) classifies immediately as one of the
// 8 linear gestures. A concurrent session (both fingers overlapped at some point) can't classify
// alone — it stashes into m_pendingTwoFinger and waits for the OTHER finger's release. If that
// second release arrives within kTwoFingerWindowMs, both deltas go into
// classifyTwoFingerGesture(); if the window expires unused (the other finger never lifted), the
// stash is silently dropped rather than risk misclassifying a single moving finger as a pair.
std::string HIDInputSource::classifyTouchRelease(int finger, float x0, float y0, float x1, float y1,
                                                   bool concurrent) {
    float dx = (x1 - x0) * static_cast<float>(m_config.touchpad.maxX);
    float dy = (y1 - y0) * static_cast<float>(m_config.touchpad.maxY);
    float aspectRatio = static_cast<float>(m_config.touchpad.maxX) / static_cast<float>(m_config.touchpad.maxY);

    if (!concurrent) {
        // A stale pending release from the other finger (its partner never came) doesn't apply to
        // an unrelated, non-concurrent session — drop it so it can't be matched up later by
        // mistake once its deadline is (or isn't) checked elsewhere.
        m_pendingTwoFinger.valid = false;
        return classifyLinearGesture(dx, dy, kGestureMinDist, aspectRatio);
    }

    ULONGLONG now = GetTickCount64();
    if (m_pendingTwoFinger.valid && now <= m_pendingTwoFinger.deadlineMs) {
        std::string gesture = classifyTwoFingerGesture(
            m_pendingTwoFinger.dx, m_pendingTwoFinger.dy, dx, dy,
            m_pendingTwoFinger.x0, x0, kGestureMinDist);
        m_pendingTwoFinger.valid = false;
        return gesture;
    }

    m_pendingTwoFinger.valid      = true;
    m_pendingTwoFinger.dx         = dx;
    m_pendingTwoFinger.dy         = dy;
    m_pendingTwoFinger.x0         = x0;
    m_pendingTwoFinger.deadlineMs = now + kTwoFingerWindowMs;
    return "";
}

// ---------------------------------------------------------------------------

void HIDInputSource::applyTouchpad(PCHAR buf, ULONG bytesRead, GamepadState& state) {
    state.touchDeltaX = 0.0f;
    state.touchDeltaY = 0.0f;
    m_physicalState.touchDeltaX = 0.0f;
    m_physicalState.touchDeltaY = 0.0f;
    state.touchGestureFired.clear();
    m_physicalState.touchGestureFired.clear();

    if (!m_config.touchpad.enabled) return;

    // Guard: touchpad finger data must fit within the bytes actually received.
    // DS4 BT in simplified mode (report ~10 bytes) won't have this data.
    int off = m_config.touchpad.dataOffset;
    if (m_readCount <= 5 || m_readCount % 240 == 0)
        spdlog::debug("[TOUCH][{}] enabled={} offset={} bytesRead={} guard={}",
            m_name, m_config.touchpad.enabled, off, (int)bytesRead, (off + 4 > (int)bytesRead) ? "FAIL" : "OK");
    if (off + 4 > static_cast<int>(bytesRead)) {
        state.touch1Active = false;
        state.touch2Active = false;
        m_lastTouchActive  = false;
        m_lastTouch2Active = false;
        return;
    }

    // DS4 finger-1 encoding (4 bytes at dataOffset):
    //   Byte 0: bit7 = 0 → finger touching; bits 6:0 = touch ID
    //   Byte 1: X[7:0]
    //   Byte 2: X[11:8] (low nibble) | Y[3:0] (high nibble)
    //   Byte 3: Y[11:4]
    BYTE b0 = static_cast<BYTE>(buf[off]);
    BYTE b1 = static_cast<BYTE>(buf[off + 1]);
    BYTE b2 = static_cast<BYTE>(buf[off + 2]);
    BYTE b3 = static_cast<BYTE>(buf[off + 3]);

    // --- Finger 1 ---
    bool active = (b0 & 0x80) == 0;  // bit7 = 0 means touching
    state.touch1Active = active;
    m_physicalState.touch1Active = active;

    if (active) {
        int   rawX  = b1 | ((b2 & 0x0F) << 8);
        int   rawY  = ((b2 & 0xF0) >> 4) | (b3 << 4);
        float normX = static_cast<float>(rawX) / static_cast<float>(m_config.touchpad.maxX);
        float normY = static_cast<float>(rawY) / static_cast<float>(m_config.touchpad.maxY);

        state.touch1X = normX;
        state.touch1Y = normY;
        m_physicalState.touch1X = normX;
        m_physicalState.touch1Y = normY;

        if (m_lastTouchActive) {
            // Delta in raw touchpad units — used for mouse routing
            state.touchDeltaX = (normX - m_lastTouchX) * static_cast<float>(m_config.touchpad.maxX);
            state.touchDeltaY = (normY - m_lastTouchY) * static_cast<float>(m_config.touchpad.maxY);
            m_physicalState.touchDeltaX = state.touchDeltaX;
            m_physicalState.touchDeltaY = state.touchDeltaY;
        } else {
            // First contact this gesture — no delta (avoids jump on finger-down). Also the start
            // of a new touch session for the gesture-threshold harness/Movimiento classifier.
            m_touch1SessStartX  = normX;
            m_touch1SessStartY  = normY;
            m_touch1SessStartMs = GetTickCount64();
            m_touch1SessConcurrent = false;
            m_touch1CommittedGesture.clear();
        }
        // Movimiento (Gestos): finger 2's state as known so far this frame (its own block below
        // hasn't run yet, so this is last frame's value — at most 1 frame of lag on concurrency
        // onset, negligible at HID polling rates).
        if (m_lastTouch2Active) m_touch1SessConcurrent = true;

        // Live commit, while still touching: as soon as this session's displacement crosses
        // kGestureMinDist, lock in the classification (no re-evaluating direction afterward, so
        // the output doesn't flip mid-hold) and report it as ACTIVE every frame from here on —
        // not just once — so the finger staying down genuinely holds the resulting action, same
        // as holding a real button. A session that goes concurrent skips this (defers to the
        // release-correlated 2-finger path in classifyTouchRelease() instead).
        if (m_touch1CommittedGesture.empty() && !m_touch1SessConcurrent) {
            float liveDx = (normX - m_touch1SessStartX) * static_cast<float>(m_config.touchpad.maxX);
            float liveDy = (normY - m_touch1SessStartY) * static_cast<float>(m_config.touchpad.maxY);
            float aspectRatio = static_cast<float>(m_config.touchpad.maxX) / static_cast<float>(m_config.touchpad.maxY);
            m_touch1CommittedGesture = classifyLinearGesture(liveDx, liveDy, kGestureMinDist, aspectRatio);
            // TEMP diagnostic trace for the H9-Paso1-Gestos regression (2026/08/26 session
            // pause) — logs the exact moment the live commit fires, how long into the session
            // it took, and whether it actually classified to something. Remove once fixed.
            if (!m_touch1CommittedGesture.empty()) {
                spdlog::debug("[H9][gesture][commit] finger=1 gesture='{}' sinceStartMs={} "
                               "liveDx={:.1f} liveDy={:.1f} concurrent={}",
                               m_touch1CommittedGesture, GetTickCount64() - m_touch1SessStartMs,
                               liveDx, liveDy, m_touch1SessConcurrent);
            }
        }
        if (!m_touch1CommittedGesture.empty()) {
            state.touchGestureFired = m_touch1CommittedGesture;
            m_physicalState.touchGestureFired = m_touch1CommittedGesture;
        }

        m_lastTouchX = normX;
        m_lastTouchY = normY;
    } else {
        state.touch1X = 0.0f;
        state.touch1Y = 0.0f;
        m_physicalState.touch1X = 0.0f;
        m_physicalState.touch1Y = 0.0f;

        if (m_lastTouchActive) {
            logTouchSession(1, m_touch1SessStartX, m_touch1SessStartY,
                             m_lastTouchX, m_lastTouchY, m_touch1SessStartMs);
            if (m_touch1CommittedGesture.empty()) {
                // Never crossed the threshold live (stayed too short the whole time, or the
                // session went concurrent before committing) — fall back to the release-time
                // classifier, same as before this held-gesture change.
                std::string gesture = classifyTouchRelease(1, m_touch1SessStartX, m_touch1SessStartY,
                                                             m_lastTouchX, m_lastTouchY,
                                                             m_touch1SessConcurrent);
                // TEMP diagnostic trace, see the live-commit one above. Never live-committed —
                // this is the release-time fallback path, logged regardless of outcome (empty
                // included) so a silently-dropped gesture shows up too.
                spdlog::debug("[H9][gesture][release] finger=1 result='{}' concurrent={}",
                               gesture, m_touch1SessConcurrent);
                if (!gesture.empty()) {
                    state.touchGestureFired = gesture;
                    m_physicalState.touchGestureFired = gesture;
                }
            }
            // else: already reported active every frame up to now; leaving touchGestureFired
            // cleared (this function's top) on this release frame is exactly the release edge the
            // shared dispatch lambdas need — nothing else to do here.
            m_touch1CommittedGesture.clear();
        }
    }

    m_lastTouchActive = active;

    // --- Finger 2 ---
    if (off + 8 <= static_cast<int>(bytesRead)) {
        BYTE c0 = static_cast<BYTE>(buf[off + 4]);
        BYTE c1 = static_cast<BYTE>(buf[off + 5]);
        BYTE c2 = static_cast<BYTE>(buf[off + 6]);
        BYTE c3 = static_cast<BYTE>(buf[off + 7]);
        state.touch2Active = (c0 & 0x80) == 0;
        m_physicalState.touch2Active = state.touch2Active;
        if (state.touch2Active) {
            int rawX2 = c1 | ((c2 & 0x0F) << 8);
            int rawY2 = ((c2 & 0xF0) >> 4) | (c3 << 4);
            state.touch2X = static_cast<float>(rawX2) / static_cast<float>(m_config.touchpad.maxX);
            state.touch2Y = static_cast<float>(rawY2) / static_cast<float>(m_config.touchpad.maxY);
            m_physicalState.touch2X = state.touch2X;
            m_physicalState.touch2Y = state.touch2Y;

            if (!m_lastTouch2Active) {
                // Start of a new touch session for finger 2 (gesture-threshold harness/Movimiento).
                m_touch2SessStartX  = state.touch2X;
                m_touch2SessStartY  = state.touch2Y;
                m_touch2SessStartMs = GetTickCount64();
                m_touch2SessConcurrent = false;
                m_touch2CommittedGesture.clear();
            }
            // Movimiento (Gestos): finger 1's state as of this frame (its block already ran above).
            if (state.touch1Active) m_touch2SessConcurrent = true;

            // Live commit — see finger 1's identical block above for the full rationale.
            if (m_touch2CommittedGesture.empty() && !m_touch2SessConcurrent) {
                float liveDx = (state.touch2X - m_touch2SessStartX) * static_cast<float>(m_config.touchpad.maxX);
                float liveDy = (state.touch2Y - m_touch2SessStartY) * static_cast<float>(m_config.touchpad.maxY);
                float aspectRatio = static_cast<float>(m_config.touchpad.maxX) / static_cast<float>(m_config.touchpad.maxY);
                m_touch2CommittedGesture = classifyLinearGesture(liveDx, liveDy, kGestureMinDist, aspectRatio);
            }
            if (!m_touch2CommittedGesture.empty()) {
                state.touchGestureFired = m_touch2CommittedGesture;
                m_physicalState.touchGestureFired = m_touch2CommittedGesture;
            }

            m_lastTouch2X = state.touch2X;
            m_lastTouch2Y = state.touch2Y;
        } else {
            state.touch2X = 0.0f;
            state.touch2Y = 0.0f;
            m_physicalState.touch2X = 0.0f;
            m_physicalState.touch2Y = 0.0f;

            if (m_lastTouch2Active) {
                logTouchSession(2, m_touch2SessStartX, m_touch2SessStartY,
                                 m_lastTouch2X, m_lastTouch2Y, m_touch2SessStartMs);
                if (!m_touch2CommittedGesture.empty()) {
                    m_touch2CommittedGesture.clear();
                } else {
                    std::string gesture = classifyTouchRelease(2, m_touch2SessStartX, m_touch2SessStartY,
                                                                 m_lastTouch2X, m_lastTouch2Y,
                                                                 m_touch2SessConcurrent);
                    if (!gesture.empty()) {
                        state.touchGestureFired = gesture;
                        m_physicalState.touchGestureFired = gesture;
                    }
                }
            }
        }
        m_lastTouch2Active = state.touch2Active;
    } else {
        if (m_lastTouch2Active)
            logTouchSession(2, m_touch2SessStartX, m_touch2SessStartY,
                             m_lastTouch2X, m_lastTouch2Y, m_touch2SessStartMs);
        m_lastTouch2Active = false;
        state.touch2Active = false;
        state.touch2X = state.touch2Y = 0.0f;
        m_physicalState.touch2Active = false;
        m_physicalState.touch2X = m_physicalState.touch2Y = 0.0f;
    }
}

void HIDInputSource::applyIMU(PCHAR buf, ULONG bytesRead, GamepadState& state) {
    state.gyroActive  = false;
    state.accelActive = false;

    if (!m_config.imu.enabled) return;

    auto readI16 = [&](int o) -> int16_t {
        return static_cast<int16_t>(
            static_cast<uint8_t>(buf[o]) |
            (static_cast<uint16_t>(static_cast<uint8_t>(buf[o + 1])) << 8));
    };
    // -1 offset means "axis not present on this device"; each axis has its own
    // offset (no assumed contiguity/order — some controllers interleave gyro/accel
    // differently than the DS4's contiguous X,Y,Z).
    auto fits = [&](int o) { return o >= 0 && o + 2 <= static_cast<int>(bytesRead); };

    const auto& imu = m_config.imu;

    if (fits(imu.gyroXOffset) && fits(imu.gyroYOffset) && fits(imu.gyroZOffset)) {
        state.gyroX = std::clamp(readI16(imu.gyroXOffset) * imu.gyroScale * (imu.gyroXInvert ? -1.0f : 1.0f), -1.0f, 1.0f);
        state.gyroY = std::clamp(readI16(imu.gyroYOffset) * imu.gyroScale * (imu.gyroYInvert ? -1.0f : 1.0f), -1.0f, 1.0f);
        state.gyroZ = std::clamp(readI16(imu.gyroZOffset) * imu.gyroScale * (imu.gyroZInvert ? -1.0f : 1.0f), -1.0f, 1.0f);
        state.gyroActive = true;
        m_physicalState.gyroX      = state.gyroX;
        m_physicalState.gyroY      = state.gyroY;
        m_physicalState.gyroZ      = state.gyroZ;
        m_physicalState.gyroActive = true;
    }

    if (fits(imu.accelXOffset) && fits(imu.accelYOffset) && fits(imu.accelZOffset)) {
        state.accelX = std::clamp(readI16(imu.accelXOffset) * imu.accelScale * (imu.accelXInvert ? -1.0f : 1.0f), -1.0f, 1.0f);
        state.accelY = std::clamp(readI16(imu.accelYOffset) * imu.accelScale * (imu.accelYInvert ? -1.0f : 1.0f), -1.0f, 1.0f);
        state.accelZ = std::clamp(readI16(imu.accelZOffset) * imu.accelScale * (imu.accelZInvert ? -1.0f : 1.0f), -1.0f, 1.0f);
        state.accelActive = true;
        m_physicalState.accelX      = state.accelX;
        m_physicalState.accelY      = state.accelY;
        m_physicalState.accelZ      = state.accelZ;
        m_physicalState.accelActive = true;
    }

    // Diagnostic trace for the parsed IMU values (gyro/accel already scaled+clamped),
    // same 30-read throttle as [HID][raw] — useful again for future controller onboarding.
    if ((state.gyroActive || state.accelActive) && m_readCount % 30 == 0) {
        spdlog::trace("[IMU] gyro({:+.3f},{:+.3f},{:+.3f}) accel({:+.3f},{:+.3f},{:+.3f})",
                      state.gyroX, state.gyroY, state.gyroZ,
                      state.accelX, state.accelY, state.accelZ);
    }
}

// Populates m_activeGyroActions/m_activeAccelActions (and their Ranges counterparts) from
// cfg.gyro_actions/accel_actions, mirroring applyAxesResidual()'s checkHalf() for stick/trigger
// axes. This is the piece PhysicalGyro/PhysicalAccel::process() deliberately leaves undone —
// they resolve VirtualButton/Dpad/Trigger/StickSlot/MouseMove targets directly into GamepadState,
// but Keyboard/Macro/MouseClick/Bot are marker-only there (see ComponentTypes.cpp,
// applyVirtualTarget()) and need PadEngine to pick them up from here, same as for axis_actions.
// Uses the same per-axis deadzone/max shaping as PhysicalController::process() (imu.*Deadzone/
// *Max) so a Ranges entry mixing e.g. a VirtualButton sub-range and a Macro sub-range on the
// same half-axis activates both at the same physical threshold.
void HIDInputSource::applyImuActions() {
    m_activeGyroActions.clear();
    m_activeGyroRangeActions.clear();
    m_activeAccelActions.clear();
    m_activeAccelRangeActions.clear();
    if (!m_config.imu.enabled) return;

    auto checkHalf = [](const std::unordered_map<std::string, HalfAxisAction>& actions,
                         const std::string& key, float magnitude,
                         std::vector<std::string>& activeActions,
                         std::unordered_map<std::string, ButtonAction>& activeRangeActions) {
        auto ait = actions.find(key);
        if (ait == actions.end()) return;
        const HalfAxisAction& ha = ait->second;
        switch (ha.type) {
        case HalfAxisActionType::Macro:
        case HalfAxisActionType::Keyboard:
        case HalfAxisActionType::MouseClick:
        case HalfAxisActionType::Bot:
            if (magnitude > ha.threshold) activeActions.push_back(key);
            break;
        case HalfAxisActionType::Ranges:
            for (const auto& r : ha.ranges) {
                if (magnitude < r.from || magnitude > r.to || !r.hasAction) continue;
                if (r.action.type == ButtonActionType::Keyboard   ||
                    r.action.type == ButtonActionType::MouseClick ||
                    r.action.type == ButtonActionType::Macro      ||
                    r.action.type == ButtonActionType::Bot)
                    activeRangeActions[key] = r.action;
                break;
            }
            break;
        default: break;
        }
    };

    const auto& imu = m_config.imu;

    // Note: plain (v > 0.0f) ? v : 0.0f instead of std::max() — this file includes hidsdi.h
    // (-> windows.h) without NOMINMAX, so the max()/min() macros from windows.h would otherwise
    // shadow std::max/std::min here. Same workaround PhysicalGyro::process() uses in
    // ComponentTypes.cpp (which doesn't include windows.h, so it can use std::max directly).
    if (m_physicalState.gyroActive && !m_config.gyro_actions.empty()) {
        float gx = applyDeadzoneMaxSigned(m_physicalState.gyroX, imu.gyroXDeadzone, imu.gyroXMax);
        float gy = applyDeadzoneMaxSigned(m_physicalState.gyroY, imu.gyroYDeadzone, imu.gyroYMax);
        float gz = applyDeadzoneMaxSigned(m_physicalState.gyroZ, imu.gyroZDeadzone, imu.gyroZMax);
        checkHalf(m_config.gyro_actions, "x_pos", (gx  > 0.0f) ? gx  : 0.0f, m_activeGyroActions, m_activeGyroRangeActions);
        checkHalf(m_config.gyro_actions, "x_neg", (-gx > 0.0f) ? -gx : 0.0f, m_activeGyroActions, m_activeGyroRangeActions);
        checkHalf(m_config.gyro_actions, "y_pos", (gy  > 0.0f) ? gy  : 0.0f, m_activeGyroActions, m_activeGyroRangeActions);
        checkHalf(m_config.gyro_actions, "y_neg", (-gy > 0.0f) ? -gy : 0.0f, m_activeGyroActions, m_activeGyroRangeActions);
        checkHalf(m_config.gyro_actions, "z_pos", (gz  > 0.0f) ? gz  : 0.0f, m_activeGyroActions, m_activeGyroRangeActions);
        checkHalf(m_config.gyro_actions, "z_neg", (-gz > 0.0f) ? -gz : 0.0f, m_activeGyroActions, m_activeGyroRangeActions);
    }

    if (m_physicalState.accelActive && !m_config.accel_actions.empty()) {
        float ax = applyDeadzoneMaxSigned(m_physicalState.accelX, imu.accelXDeadzone, imu.accelXMax);
        float ay = applyDeadzoneMaxSigned(m_physicalState.accelY, imu.accelYDeadzone, imu.accelYMax);
        float az = applyDeadzoneMaxSigned(m_physicalState.accelZ, imu.accelZDeadzone, imu.accelZMax);
        checkHalf(m_config.accel_actions, "x_pos", (ax  > 0.0f) ? ax  : 0.0f, m_activeAccelActions, m_activeAccelRangeActions);
        checkHalf(m_config.accel_actions, "x_neg", (-ax > 0.0f) ? -ax : 0.0f, m_activeAccelActions, m_activeAccelRangeActions);
        checkHalf(m_config.accel_actions, "y_pos", (ay  > 0.0f) ? ay  : 0.0f, m_activeAccelActions, m_activeAccelRangeActions);
        checkHalf(m_config.accel_actions, "y_neg", (-ay > 0.0f) ? -ay : 0.0f, m_activeAccelActions, m_activeAccelRangeActions);
        checkHalf(m_config.accel_actions, "z_pos", (az  > 0.0f) ? az  : 0.0f, m_activeAccelActions, m_activeAccelRangeActions);
        checkHalf(m_config.accel_actions, "z_neg", (-az > 0.0f) ? -az : 0.0f, m_activeAccelActions, m_activeAccelRangeActions);
    }
}

void HIDInputSource::parseHIDDpad(ULONG hatValue, bool& up, bool& down, bool& left, bool& right) {
    up = down = left = right = false;
    if (hatValue >= 8) return;
    switch (hatValue) {
    case 0: up   = true;                 break;
    case 1: up   = true; right = true;   break;
    case 2:              right = true;   break;
    case 3: down = true; right = true;   break;
    case 4: down = true;                 break;
    case 5: down = true; left  = true;   break;
    case 6:              left  = true;   break;
    case 7: up   = true; left  = true;   break;
    }
}

#undef PREPARSED
