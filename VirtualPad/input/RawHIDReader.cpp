#include "RawHIDReader.h"
#include <hidsdi.h>
#include <algorithm>

#pragma comment(lib, "hid.lib")

#define PREPARSED  (static_cast<PHIDP_PREPARSED_DATA>(m_preparsed))

static constexpr USHORT kUsageX   = 0x30;
static constexpr USHORT kUsageY   = 0x31;
static constexpr USHORT kUsageZ   = 0x32;
static constexpr USHORT kUsageRx  = 0x33;
static constexpr USHORT kUsageRy  = 0x34;
static constexpr USHORT kUsageRz  = 0x35;
static constexpr USHORT kUsageHat = 0x39;

// ---------------------------------------------------------------------------

RawHIDReader::RawHIDReader(const std::string& devicePath) {
    m_device = CreateFileA(devicePath.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (m_device == INVALID_HANDLE_VALUE) return;

    m_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_event) {
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return;
    }

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(m_device, &preparsed)) {
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

    m_reportLen = caps.InputReportByteLength;
    m_buf.resize(m_reportLen, 0);

    // Cache logical ranges and actual usage pages for axis normalisation.
    // Use emplace for both range and individual caps so the first declaration wins
    // (avoids a bad individual cap overwriting a correct range cap for the same usage).
    USHORT numVal = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> vcaps(numVal);
    HidP_GetValueCaps(HidP_Input, vcaps.data(), &numVal, PREPARSED);
    for (USHORT i = 0; i < numVal; ++i) {
        ValueRange vr = { vcaps[i].LogicalMin, vcaps[i].LogicalMax, vcaps[i].BitSize };
        if (vcaps[i].IsRange) {
            for (USHORT u = vcaps[i].Range.UsageMin; u <= vcaps[i].Range.UsageMax; ++u) {
                m_valueCaps.emplace(u, vr);
                m_usagePage.emplace(u, vcaps[i].UsagePage);
            }
        } else {
            m_valueCaps.emplace(vcaps[i].NotRange.Usage, vr);
            m_usagePage.emplace(vcaps[i].NotRange.Usage, vcaps[i].UsagePage);
        }
    }

    // Cache button report ID (needed for the INCOMPATIBLE_REPORT_ID workaround)
    USHORT numBtn = caps.NumberInputButtonCaps;
    if (numBtn > 0) {
        std::vector<HIDP_BUTTON_CAPS> bcaps(numBtn);
        if (HidP_GetButtonCaps(HidP_Input, bcaps.data(), &numBtn, PREPARSED) == HIDP_STATUS_SUCCESS && numBtn > 0)
            m_btnReportId = bcaps[0].ReportID;
    }
}

RawHIDReader::~RawHIDReader() {
    if (m_preparsed)                        HidD_FreePreparsedData(PREPARSED);
    if (m_event)                          { CloseHandle(m_event);  m_event  = nullptr; }
    if (m_device != INVALID_HANDLE_VALUE) { CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE; }
}

// ---------------------------------------------------------------------------

bool RawHIDReader::read(RawHIDState& out) {
    if (m_device == INVALID_HANDLE_VALUE) return false;

    ResetEvent(m_event);
    OVERLAPPED ov = {}; ov.hEvent = m_event;

    DWORD bytesRead = 0;
    BOOL  ok = ReadFile(m_device, m_buf.data(), m_reportLen, &bytesRead, &ov);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
            return false;
        }
        DWORD wait = WaitForSingleObject(m_event, 20);
        if (wait != WAIT_OBJECT_0) {
            CancelIo(m_device);
            WaitForSingleObject(m_event, INFINITE);
            return true; // timeout — keep previous state
        }
        if (!GetOverlappedResult(m_device, &ov, &bytesRead, FALSE)) {
            CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
            return false;
        }
    }

    PCHAR  buf    = reinterpret_cast<PCHAR>(m_buf.data());
    ULONG  bufLen = m_reportLen;

    // ── Buttons ──────────────────────────────────────────────────────────────
    USAGE usages[128];
    ULONG usageCount = 128;
    NTSTATUS btnSt = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                                    usages, &usageCount, PREPARSED, buf, bufLen);
    if (btnSt == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_btnReportId != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(m_btnReportId);
        usageCount = 128;
        btnSt = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                               usages, &usageCount, PREPARSED, buf, bufLen);
        buf[0] = savedId;
    }
    if (btnSt == HIDP_STATUS_SUCCESS) {
        out.buttonMask = 0;
        for (ULONG i = 0; i < usageCount; ++i)
            if (usages[i] >= 1 && usages[i] <= 32)
                out.buttonMask |= (1u << (usages[i] - 1));
    }

    // ── Axes ─────────────────────────────────────────────────────────────────
    auto readAxis = [&](USHORT usage, float& dest) {
        // Use the page recorded from the descriptor; fall back to Generic Desktop.
        auto pit = m_usagePage.find(usage);
        USHORT page = (pit != m_usagePage.end()) ? pit->second : HID_USAGE_PAGE_GENERIC;
        ULONG raw = 0;
        NTSTATUS st = HidP_GetUsageValue(HidP_Input, page, 0,
                                         usage, &raw, PREPARSED, buf, bufLen);
        if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_btnReportId != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_btnReportId);
            st = HidP_GetUsageValue(HidP_Input, page, 0,
                                    usage, &raw, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        if (st == HIDP_STATUS_SUCCESS) dest = normalize(usage, raw);
    };

    readAxis(kUsageX,  out.axisX);
    readAxis(kUsageY,  out.axisY);
    readAxis(kUsageZ,  out.axisZ);
    readAxis(kUsageRx, out.axisRx);
    readAxis(kUsageRy, out.axisRy);
    readAxis(kUsageRz, out.axisRz);

    // Simulation page axes (e.g. 8BitDo Pro 3 triggers in D-mode)
    auto readSimAxis = [&](USHORT usage, float& dest) {
        ULONG raw = 0;
        NTSTATUS st = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_SIMULATION, 0,
                                         usage, &raw, PREPARSED, buf, bufLen);
        if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_btnReportId != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(m_btnReportId);
            st = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_SIMULATION, 0,
                                    usage, &raw, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        if (st == HIDP_STATUS_SUCCESS) dest = normalize(usage, raw);
    };
    readSimAxis(0xC4, out.axisBrake);
    readSimAxis(0xC5, out.axisAccel);

    // ── Hat ──────────────────────────────────────────────────────────────────
    ULONG hat = 0xFFFFFFFF;
    NTSTATUS hatSt = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0,
                                        kUsageHat, &hat, PREPARSED, buf, bufLen);
    if (hatSt == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_btnReportId != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(m_btnReportId);
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0,
                           kUsageHat, &hat, PREPARSED, buf, bufLen);
        buf[0] = savedId;
    }
    auto hatIt = m_valueCaps.find(kUsageHat);
    if (hatIt != m_valueCaps.end()) {
        ULONG hatMin = static_cast<ULONG>(hatIt->second.logMin);
        ULONG hatMax = static_cast<ULONG>(hatIt->second.logMax);
        out.hat = (hat >= hatMin && hat <= hatMax) ? hat - hatMin : 0xFFFFFFFF;
    } else {
        out.hat = hat;
    }

    out.valid = true;
    return true;
}

// ---------------------------------------------------------------------------

float RawHIDReader::normalize(USHORT usage, ULONG raw) const {
    auto it = m_valueCaps.find(usage);
    if (it == m_valueCaps.end()) return 0.0f;

    LONG logMin = it->second.logMin;
    LONG logMax = it->second.logMax;

    if (logMax < logMin) {
        USHORT bits = it->second.bitSize;
        ULONG uMax = (bits > 0 && bits < 32) ? (1UL << bits) - 1 : 0xFFFFFFFFUL;
        if (uMax == 0) return 0.0f;
        return std::clamp(static_cast<float>(raw) / static_cast<float>(uMax) * 2.0f - 1.0f, -1.0f, 1.0f);
    }
    LONG range = logMax - logMin;
    if (range <= 0) return 0.0f;
    return std::clamp((static_cast<float>(static_cast<LONG>(raw) - logMin) / range) * 2.0f - 1.0f, -1.0f, 1.0f);
}

#undef PREPARSED
