#include "HIDInputSource.h"
#include "../GamepadState.h"
#include "../Log.h"
#include <hidsdi.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "hid.lib")

// Convenience cast — m_preparsed is stored as void* to keep hidsdi.h out of the header.
#define PREPARSED  (static_cast<PHIDP_PREPARSED_DATA>(m_preparsed))

// HID Generic Desktop axis usage IDs
static constexpr USHORT kUsageX   = 0x30;
static constexpr USHORT kUsageY   = 0x31;
static constexpr USHORT kUsageZ   = 0x32;
static constexpr USHORT kUsageRx  = 0x33;
static constexpr USHORT kUsageRy  = 0x34;
static constexpr USHORT kUsageRz  = 0x35;
static constexpr USHORT kUsageHat = 0x39;

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
    : m_config(config), m_name(config.source_name)
{
    m_device = CreateFileA(devicePath.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

    if (m_device == INVALID_HANDLE_VALUE) {
        spdlog::error("[HID] Failed to open device (error {})", GetLastError());
        return;
    }

    m_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_event) {
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return;
    }

    // Get preparsed data and store as void* (hidsdi.h stays out of the header)
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(m_device, &preparsed)) {
        spdlog::error("[HID] Failed to get preparsed data");
        CloseHandle(m_event);  m_event  = nullptr;
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return;
    }
    m_preparsed = preparsed;

    HIDP_CAPS caps = {};
    if (HidP_GetCaps(PREPARSED, &caps) != HIDP_STATUS_SUCCESS) {
        HidD_FreePreparsedData(PREPARSED); m_preparsed = nullptr;
        CloseHandle(m_event);  m_event  = nullptr;
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return;
    }

    m_inputReportLen = caps.InputReportByteLength;
    m_reportBuf.resize(m_inputReportLen, 0);

    // Cache logical min/max for all value caps so we can normalise axes.
    // Some devices (e.g. 8BitDo Pro 3 D-mode) declare generic axes as a range cap
    // (IsRange=true, UsageMin=0x30..UsageMax=0x35) instead of individual caps.
    // We expand range caps so every usage in the range gets an entry.
    //
    // Collision rule: when the same usage number appears on multiple pages (e.g. DS4 BT
    // exposes usage 0x30 on both page 0x01 and vendor page 0xFF00), prefer the standard
    // page (< 0xFF00) over vendor-specific pages.  Without this, the vendor entry can
    // overwrite the standard one and cause HIDP_STATUS_INCOMPATIBLE_REPORT_ID on reads.
    std::vector<HIDP_VALUE_CAPS> valueCaps(caps.NumberInputValueCaps);
    USHORT numCaps = caps.NumberInputValueCaps;
    HidP_GetValueCaps(HidP_Input, valueCaps.data(), &numCaps, PREPARSED);

    auto insertCap = [&](USHORT u, USHORT page, const ValueRange& vr) {
        auto it = m_usagePage.find(u);
        if (it == m_usagePage.end()) {
            m_valueCaps.emplace(u, vr);
            m_usagePage.emplace(u, page);
        } else {
            // Collision: same usage on multiple pages — prefer standard over vendor-specific.
            bool existingIsStandard = (it->second < 0xFF00);
            bool newIsStandard      = (page       < 0xFF00);
            if (newIsStandard && !existingIsStandard) {
                m_valueCaps[u] = vr;
                m_usagePage[u] = page;
            }
            // else keep existing (existing standard beats new vendor; or keep first of same class)
        }
    };

    for (USHORT ci = 0; ci < numCaps; ++ci) {
        ValueRange vr = { valueCaps[ci].LogicalMin, valueCaps[ci].LogicalMax, valueCaps[ci].BitSize };
        USHORT     page = valueCaps[ci].UsagePage;
        if (valueCaps[ci].IsRange) {
            for (USHORT u = valueCaps[ci].Range.UsageMin; u <= valueCaps[ci].Range.UsageMax; ++u)
                insertCap(u, page, vr);
        } else {
            insertCap(valueCaps[ci].NotRange.Usage, page, vr);
        }
    }

    // Find report ID used for buttons (may differ from the report ID the device sends)
    USHORT numBtnCaps = caps.NumberInputButtonCaps;
    spdlog::info("[HID] Opened: {}  ReportLen={}  ValueCaps={}  BtnCaps={}",
        m_name, m_inputReportLen, numCaps, numBtnCaps);
    if (numBtnCaps > 0) {
        std::vector<HIDP_BUTTON_CAPS> btnCaps(numBtnCaps);
        if (HidP_GetButtonCaps(HidP_Input, btnCaps.data(), &numBtnCaps, PREPARSED) == HIDP_STATUS_SUCCESS && numBtnCaps > 0) {
            m_buttonReportId = btnCaps[0].ReportID;
            spdlog::debug("[HID] Button ReportID in descriptor: {}", m_buttonReportId);
        }
    }
    // Log all value caps for diagnosis (both range and individual)
    for (USHORT ci = 0; ci < numCaps; ++ci) {
        if (valueCaps[ci].IsRange)
            spdlog::debug("[HID] ValCap(range): ReportID={} Page=0x{:02X} Usage=0x{:02X}..0x{:02X} range=[{},{}]",
                valueCaps[ci].ReportID, valueCaps[ci].UsagePage,
                valueCaps[ci].Range.UsageMin, valueCaps[ci].Range.UsageMax,
                valueCaps[ci].LogicalMin, valueCaps[ci].LogicalMax);
        else
            spdlog::debug("[HID] ValCap: ReportID={} Page=0x{:02X} Usage=0x{:02X} range=[{},{}]",
                valueCaps[ci].ReportID, valueCaps[ci].UsagePage,
                valueCaps[ci].NotRange.Usage,
                valueCaps[ci].LogicalMin, valueCaps[ci].LogicalMax);
    }

    m_connected = true;
}

HIDInputSource::~HIDInputSource() {
    if (m_preparsed)                        HidD_FreePreparsedData(PREPARSED);
    if (m_event)                          { CloseHandle(m_event);  m_event  = nullptr; }
    if (m_device != INVALID_HANDLE_VALUE) { CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE; }
}

// ---------------------------------------------------------------------------

bool HIDInputSource::isConnected() const {
    return m_connected;
}

bool HIDInputSource::read(GamepadState& state) {
    if (!m_connected || m_device == INVALID_HANDLE_VALUE) return false;

    ResetEvent(m_event);
    OVERLAPPED ov = {};
    ov.hEvent = m_event;

    DWORD bytesRead = 0;
    BOOL  readOk    = ReadFile(m_device, m_reportBuf.data(), m_inputReportLen, &bytesRead, &ov);

    if (!readOk) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            m_connected = false;
            return false;
        }
        DWORD wait = WaitForSingleObject(m_event, 20);
        if (wait != WAIT_OBJECT_0) {
            // Timeout — no new data, keep last state (but clear per-frame deltas)
            CancelIo(m_device);
            WaitForSingleObject(m_event, INFINITE); // drain the cancelled I/O
            state.touchDeltaX = 0.0f;
            state.touchDeltaY = 0.0f;
            if (++m_readCount % 240 == 0) {
                spdlog::debug("[HID][{}] lx={:.2f} ly={:.2f} rx={:.2f} ry={:.2f} tL={:.2f} tR={:.2f} btns={:08X} (no report)",
                       m_name,
                       state.leftX, state.leftY, state.rightX, state.rightY,
                       state.triggerL, state.triggerR, m_lastButtonMask);
            }
            return true;
        }
        if (!GetOverlappedResult(m_device, &ov, &bytesRead, FALSE)) {
            m_connected = false;
            return false;
        }
    }

    PCHAR buf    = reinterpret_cast<PCHAR>(m_reportBuf.data());
    ULONG bufLen = m_inputReportLen;

    applyButtons (buf, bufLen,    state);
    applyAxes    (buf, bufLen,    state);
    applyTouchpad(buf, bytesRead, state);
    applyIMU     (buf, bytesRead, state);

    // Diagnostic: log state + raw bytes every ~2 seconds (240 reads * 8ms = ~2s)
    if (++m_readCount % 240 == 0) {
        spdlog::debug("[HID][{}] lx={:.2f} ly={:.2f} rx={:.2f} ry={:.2f} tL={:.2f} tR={:.2f} btns={:08X}",
               m_name,
               state.leftX, state.leftY, state.rightX, state.rightY,
               state.triggerL, state.triggerR, m_lastButtonMask);
        ULONG dumpLen = (m_inputReportLen < 20) ? m_inputReportLen : 20;
        std::string raw;
        raw.reserve(dumpLen * 3);
        for (ULONG i = 0; i < dumpLen; ++i) {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%02X ", (unsigned char)m_reportBuf[i]);
            raw += tmp;
        }
        spdlog::debug("[HID][raw] {}", raw);
    }

    if (m_config.dpad == "hid_hat") {
        ULONG hatValue = 0xFFFFFFFF; // default = out of range = neutral
        NTSTATUS hatStatus = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, kUsageHat,
                                                &hatValue, PREPARSED, buf, bufLen);
        if (hatStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_buttonReportId != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_buttonReportId);
            HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, kUsageHat,
                               &hatValue, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        // Hat encodings vary:
        //   [0..8]: 0=N, 1=NE ... 7=NW, 8=center  (e.g. Pro 2 D-mode)
        //   [1..8]: 1=N, 2=NE ... 8=NW, center=0  (e.g. Pro 3 X-mode)
        // Values inside [logMin..logMax] are directions; outside = neutral.
        auto hatCapIt = m_valueCaps.find(kUsageHat);
        if (hatCapIt != m_valueCaps.end()) {
            ULONG hatMin = static_cast<ULONG>(hatCapIt->second.logMin);
            ULONG hatMax = static_cast<ULONG>(hatCapIt->second.logMax);
            if (hatValue < hatMin || hatValue > hatMax) {
                state.dpadUp = state.dpadDown = state.dpadLeft = state.dpadRight = false;
            } else {
                parseHIDDpad(hatValue - hatMin, state.dpadUp, state.dpadDown, state.dpadLeft, state.dpadRight);
            }
        } else {
            parseHIDDpad(hatValue, state.dpadUp, state.dpadDown, state.dpadLeft, state.dpadRight);
        }
    }

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
    if (btnStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_buttonReportId != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(m_buttonReportId);
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

    auto setBtn = [&](const std::string& name, bool v) {
        if      (name == "a")      state.btnA     = v;
        else if (name == "b")      state.btnB     = v;
        else if (name == "x")      state.btnX     = v;
        else if (name == "y")      state.btnY     = v;
        else if (name == "l1")     state.btnLB    = v;
        else if (name == "r1")     state.btnRB    = v;
        else if (name == "select") state.btnBack  = v;
        else if (name == "start")  state.btnStart = v;
        else if (name == "home")   state.btnHome  = v;
        else if (name == "l3")     state.btnL3    = v;
        else if (name == "r3")     state.btnR3    = v;
        else if (name == "l4")        state.btnL4    = v;
        else if (name == "r4")        state.btnR4    = v;
        else if (name == "lp")        state.btnLP    = v;
        else if (name == "rp")        state.btnRP    = v;
        else if (name == "touch_btn") state.btnTouch = v;
    };

    // Physical display state: build separately using action.physical names.
    // Must run BEFORE virtual loop so display and ViGEm output stay independent.
    GamepadState physDisplay = state;  // inherit axes already applied
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
    }
    m_physicalState = physDisplay;

    // Virtual remapping: use action.name so ViGEm receives the mapped output.
    for (const auto& [bit, action] : m_config.buttons) {
        bool pressed = (m_lastButtonMask & (1u << (bit - 1))) != 0;
        switch (action.type) {
        case ButtonActionType::VirtualButton:
            setBtn(action.name, pressed);
            break;
        case ButtonActionType::Trigger:
            if      (action.target == "l2") state.triggerL = pressed ? 1.0f : 0.0f;
            else if (action.target == "r2") state.triggerR = pressed ? 1.0f : 0.0f;
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
    // Log first raw report bytes once to diagnose BT vs USB report format
    if (m_readCount == 1) {
        ULONG dumpLen = (bufLen < 24) ? bufLen : 24;
        std::string raw;
        raw.reserve(dumpLen * 3);
        for (ULONG i = 0; i < dumpLen; ++i) {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%02X ", (unsigned char)buf[i]);
            raw += tmp;
        }
        spdlog::info("[HID][{}] First report ({} bytes): {}", m_name, bufLen, raw);
    }

    for (const auto& [source, mapping] : m_config.axes) {
        AxisUsage au = usageFromAxisName(source);
        if (au.usage == 0) continue;

        // Use the page from the device descriptor when available — handles devices
        // that put axes on a non-standard page (e.g. triggers on page 0x01 instead of 0x02).
        auto pit = m_usagePage.find(au.usage);
        USHORT page = (pit != m_usagePage.end()) ? pit->second : au.page;

        ULONG rawValue = 0;
        NTSTATUS axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                               au.usage, &rawValue, PREPARSED, buf, bufLen);
        if (axStatus == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_buttonReportId != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_buttonReportId);
            axStatus = HidP_GetUsageValue(HidP_Input, page, 0,
                                          au.usage, &rawValue, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }

        // Log axis status for first few reads to diagnose BT issues
        if (m_readCount <= 3) {
            float normDbg = (axStatus == HIDP_STATUS_SUCCESS) ? normalizeHIDAxis(au.usage, rawValue) : 0.0f;
            spdlog::info("[HID][{}] axis {} page=0x{:02X} usage=0x{:02X} reportId={} status=0x{:08X} raw={} norm={:.3f}",
                m_name, source, page, au.usage, (unsigned char)buf[0],
                (unsigned)axStatus, rawValue, normDbg);
        }

        if (axStatus != HIDP_STATUS_SUCCESS)
            continue;

        float v = normalizeHIDAxis(au.usage, rawValue);
        if (mapping.invert) v = -v;

        if      (mapping.target == "left_x")          state.leftX   = v;
        else if (mapping.target == "left_y")          state.leftY   = v;
        else if (mapping.target == "right_x")         state.rightX  = v;
        else if (mapping.target == "right_y")         state.rightY  = v;
        else if (mapping.target == "trigger_l")       state.triggerL = (v + 1.0f) * 0.5f;
        else if (mapping.target == "trigger_r")       state.triggerR = (v + 1.0f) * 0.5f;
        else if (mapping.target == "trigger_combined") {
            state.triggerL = (v > 0.0f) ?  v : 0.0f;
            state.triggerR = (v < 0.0f) ? -v : 0.0f;
        }
        else if (mapping.target == "mouse_x") state.mouseX = v;
        else if (mapping.target == "mouse_y") state.mouseY = v;
    }
}

float HIDInputSource::normalizeHIDAxis(USHORT usage, ULONG rawValue) const {
    auto it = m_valueCaps.find(usage);
    if (it == m_valueCaps.end()) return 0.0f;

    LONG logMin = it->second.logMin;
    LONG logMax = it->second.logMax;

    // Unsigned range: descriptor reports [0, -1] meaning [0, 2^BitSize - 1]
    if (logMax < logMin) {
        USHORT bits = it->second.bitSize;
        ULONG uMax = (bits > 0 && bits < 32) ? (1UL << bits) - 1 : 0xFFFFFFFFUL;
        if (uMax == 0) return 0.0f;
        float norm = static_cast<float>(rawValue) / static_cast<float>(uMax) * 2.0f - 1.0f;
        return std::clamp(norm, -1.0f, 1.0f);
    }

    LONG range = logMax - logMin;
    if (range <= 0) return 0.0f;

    float norm = (static_cast<float>(static_cast<LONG>(rawValue) - logMin) / range) * 2.0f - 1.0f;
    return std::clamp(norm, -1.0f, 1.0f);
}

void HIDInputSource::applyTouchpad(PCHAR buf, ULONG bytesRead, GamepadState& state) {
    state.touchDeltaX = 0.0f;
    state.touchDeltaY = 0.0f;

    if (!m_config.touchpad.enabled) return;

    // Guard: touchpad finger data must fit within the bytes actually received.
    // DS4 BT in simplified mode (report ~10 bytes) won't have this data.
    int off = m_config.touchpad.dataOffset;
    if (off + 4 > static_cast<int>(bytesRead)) {
        state.touch1Active = false;
        state.touch2Active = false;
        m_lastTouchActive  = false;
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

    if (active) {
        int   rawX  = b1 | ((b2 & 0x0F) << 8);
        int   rawY  = ((b2 & 0xF0) >> 4) | (b3 << 4);
        float normX = static_cast<float>(rawX) / static_cast<float>(m_config.touchpad.maxX);
        float normY = static_cast<float>(rawY) / static_cast<float>(m_config.touchpad.maxY);

        state.touch1X = normX;
        state.touch1Y = normY;

        if (m_lastTouchActive) {
            // Delta in raw touchpad units — used for mouse routing
            state.touchDeltaX = (normX - m_lastTouchX) * static_cast<float>(m_config.touchpad.maxX);
            state.touchDeltaY = (normY - m_lastTouchY) * static_cast<float>(m_config.touchpad.maxY);
        }
        // else: first contact this gesture — no delta (avoids jump on finger-down)

        m_lastTouchX = normX;
        m_lastTouchY = normY;
    } else {
        state.touch1X = 0.0f;
        state.touch1Y = 0.0f;
    }

    m_lastTouchActive = active;

    // --- Finger 2 ---
    if (off + 8 <= static_cast<int>(bytesRead)) {
        BYTE c0 = static_cast<BYTE>(buf[off + 4]);
        BYTE c1 = static_cast<BYTE>(buf[off + 5]);
        BYTE c2 = static_cast<BYTE>(buf[off + 6]);
        BYTE c3 = static_cast<BYTE>(buf[off + 7]);
        state.touch2Active = (c0 & 0x80) == 0;
        if (state.touch2Active) {
            int rawX2 = c1 | ((c2 & 0x0F) << 8);
            int rawY2 = ((c2 & 0xF0) >> 4) | (c3 << 4);
            state.touch2X = static_cast<float>(rawX2) / static_cast<float>(m_config.touchpad.maxX);
            state.touch2Y = static_cast<float>(rawY2) / static_cast<float>(m_config.touchpad.maxY);
        } else {
            state.touch2X = 0.0f;
            state.touch2Y = 0.0f;
        }
    } else {
        state.touch2Active = false;
        state.touch2X = state.touch2Y = 0.0f;
    }
}

void HIDInputSource::applyIMU(PCHAR buf, ULONG bytesRead, GamepadState& state) {
    state.gyroActive = false;

    if (!m_config.imu.enabled) return;

    // Need 6 bytes starting at gyroOffset (3 × int16 for X, Y, Z)
    int off = m_config.imu.gyroOffset;
    if (off + 6 > static_cast<int>(bytesRead)) return;

    auto readI16 = [&](int o) -> int16_t {
        return static_cast<int16_t>(
            static_cast<uint8_t>(buf[o]) |
            (static_cast<uint16_t>(static_cast<uint8_t>(buf[o + 1])) << 8));
    };

    int16_t rawX = readI16(off);
    int16_t rawY = readI16(off + 2);
    int16_t rawZ = readI16(off + 4);

    state.gyroX     = std::clamp(rawX * m_config.imu.gyroScale, -1.0f, 1.0f);
    state.gyroY     = std::clamp(rawY * m_config.imu.gyroScale, -1.0f, 1.0f);
    state.gyroZ     = std::clamp(rawZ * m_config.imu.gyroScale, -1.0f, 1.0f);
    state.gyroActive = true;
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
