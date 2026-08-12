#include "RawHIDReader.h"
#include <hidsdi.h>

#define PREPARSED  (static_cast<PHIDP_PREPARSED_DATA>(hid.preparsed()))

static constexpr USHORT kUsageX   = 0x30;
static constexpr USHORT kUsageY   = 0x31;
static constexpr USHORT kUsageZ   = 0x32;
static constexpr USHORT kUsageRx  = 0x33;
static constexpr USHORT kUsageRy  = 0x34;
static constexpr USHORT kUsageRz  = 0x35;
static constexpr USHORT kUsageHat = 0x39;

// ---------------------------------------------------------------------------

RawHIDReader::RawHIDReader(const std::string& devicePath, const std::string& name)
    : m_hid(devicePath, name)
{
}

// ---------------------------------------------------------------------------

bool RawHIDReader::read(RawHIDState& out, int timeoutMs)
{
    auto result = m_hid.read(timeoutMs);
    if (result == HIDDevice::ReadResult::Disconnected) return false;
    if (result == HIDDevice::ReadResult::Timeout)      return true;

    decodeRawHIDReport(m_hid, out);
    return true;
}

// ---------------------------------------------------------------------------

void decodeRawHIDReport(const HIDDevice& hid, RawHIDState& out)
{
    PCHAR buf    = reinterpret_cast<PCHAR>(const_cast<BYTE*>(hid.reportBuf().data()));
    ULONG bufLen = hid.reportLen();
    BYTE  btnId  = hid.buttonReportId();

    // ── Buttons ──────────────────────────────────────────────────────────────
    USAGE usages[128];
    ULONG usageCount = 128;
    NTSTATUS btnSt = HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0,
                                    usages, &usageCount, PREPARSED, buf, bufLen);
    if (btnSt == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && btnId != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(btnId);
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
        auto pit   = hid.usagePage().find(usage);
        USHORT page = (pit != hid.usagePage().end()) ? pit->second : HID_USAGE_PAGE_GENERIC;
        ULONG raw = 0;
        NTSTATUS st = HidP_GetUsageValue(HidP_Input, page, 0,
                                         usage, &raw, PREPARSED, buf, bufLen);
        if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && btnId != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(btnId);
            st = HidP_GetUsageValue(HidP_Input, page, 0,
                                    usage, &raw, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        if (st == HIDP_STATUS_SUCCESS) dest = hid.normalizeAxis(usage, raw);
    };

    readAxis(kUsageX,  out.axisX);
    readAxis(kUsageY,  out.axisY);
    readAxis(kUsageZ,  out.axisZ);
    readAxis(kUsageRx, out.axisRx);
    readAxis(kUsageRy, out.axisRy);
    readAxis(kUsageRz, out.axisRz);

    // Simulation page (e.g. 8BitDo Pro 3 triggers in D-mode)
    auto readSimAxis = [&](USHORT usage, float& dest) {
        ULONG raw = 0;
        NTSTATUS st = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_SIMULATION, 0,
                                         usage, &raw, PREPARSED, buf, bufLen);
        if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && btnId != 0xFF) {
            char savedId = buf[0];
            buf[0] = static_cast<char>(btnId);
            st = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_SIMULATION, 0,
                                    usage, &raw, PREPARSED, buf, bufLen);
            buf[0] = savedId;
        }
        if (st == HIDP_STATUS_SUCCESS) dest = hid.normalizeAxis(usage, raw);
    };
    readSimAxis(0xC4, out.axisBrake);
    readSimAxis(0xC5, out.axisAccel);

    // ── Hat ──────────────────────────────────────────────────────────────────
    ULONG hat = 0xFFFFFFFF;
    NTSTATUS hatSt = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0,
                                        kUsageHat, &hat, PREPARSED, buf, bufLen);
    if (hatSt == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && btnId != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(btnId);
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0,
                           kUsageHat, &hat, PREPARSED, buf, bufLen);
        buf[0] = savedId;
    }
    auto hatIt = hid.valueCaps().find(kUsageHat);
    if (hatIt != hid.valueCaps().end()) {
        ULONG hatMin = static_cast<ULONG>(hatIt->second.logMin);
        ULONG hatMax = static_cast<ULONG>(hatIt->second.logMax);
        out.hat = (hat >= hatMin && hat <= hatMax) ? hat - hatMin : 0xFFFFFFFF;
    } else {
        out.hat = hat;
    }

    // ── Full raw report bytes (for the IMU calibration wizard and the Scanner tab) ──
    {
        const auto& rb = hid.reportBuf();
        ULONG n = hid.lastBytesRead();
        if (n > rb.size()) n = static_cast<ULONG>(rb.size());
        out.raw.assign(rb.begin(), rb.begin() + n);
    }

    out.valid = true;
}

#undef PREPARSED
