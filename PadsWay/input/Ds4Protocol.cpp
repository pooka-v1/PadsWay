#include "Ds4Protocol.h"
#include "SonyCrc.h"
#include <algorithm>

// No logging here on purpose: the caller (HIDInputSource, task 4B) logs the outcome, and the unit
// tests build this file without spdlog.

namespace {

// USB report byte positions (REFERENCE.md, "Sony DualShock 4 v2"), as HidP reads them.
constexpr ULONG kLeftX   = 1;
constexpr ULONG kLeftY   = 2;
constexpr ULONG kRightX  = 3;
constexpr ULONG kRightY  = 4;
constexpr ULONG kHatBtn  = 5;   // low nibble = hat, high nibble = buttons 1-4
constexpr ULONG kBtnMid  = 6;   // buttons 5-12
constexpr ULONG kBtnHigh = 7;   // bits 0-1 = buttons 13-14 (PS, touch click); bits 2-7 = counter
constexpr ULONG kL2      = 8;
constexpr ULONG kR2      = 9;

// The DS4 descriptor declares every axis as 0..255 and the hat as 0..7 (8 = neutral). Must match
// HIDDevice::normalizeAxis() for that range, or the same stick would read differently over BT.
constexpr float kAxisLogMax = 255.0f;
constexpr ULONG kHatLogMax  = 7;

float normalizeByte(BYTE raw)
{
    return std::clamp(static_cast<float>(raw) / kAxisLogMax * 2.0f - 1.0f, -1.0f, 1.0f);
}

} // namespace

// ---------------------------------------------------------------------------

bool Ds4Protocol::enableFullMode(HidChannel& channel, HidTransport transport)
{
    if (transport != HidTransport::Bluetooth) return true;   // USB: full report from the start
    if (channel.vendorId() != kSonyVendorId)  return false;  // second barrier, see ARCHITECTURE.md

    std::vector<BYTE> calibration;
    if (!channel.getFeature(kActivationFeatureId, calibration)) return false;

    // Confirmation, not "send and trust": the pad answers by changing report format.
    for (int i = 0; i < kConfirmMaxReads; ++i) {
        const auto result = channel.read(kConfirmReadTimeoutMs);
        if (result == HidChannel::ReadResult::Disconnected) return false;
        if (result == HidChannel::ReadResult::Ok
            && !channel.reportBuf().empty() && channel.reportBuf()[0] == kBtFullReportId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

bool Ds4Protocol::decode(const BYTE* report, ULONG len, RawHIDState& out)
{
    if (!report || len == 0) return false;

    if (report[0] != kBtFullReportId)
        return m_fallback ? m_fallback->decode(report, len, out) : false;

    // A truncated or corrupted 0x11 is dropped whole — never decode half a report.
    if (len < kBtFullReportLen) return false;
    if (!sony_crc::check(sony_crc::kSeedInput, report, kBtFullReportLen)) return false;

    decodeBtFullReport(report, out);
    return true;
}

// ---------------------------------------------------------------------------

void Ds4Protocol::decodeBtFullReport(const BYTE* report, RawHIDState& out)
{
    // Rebuild the USB report first, then read everything from it: one layout to get right.
    out.raw.assign(kUsbReportLen, 0);
    out.raw[0] = kUsbReportId;
    std::copy(report + 1 + kBtHeaderLen, report + kUsbReportLen + kBtHeaderLen, out.raw.begin() + 1);
    const std::vector<uint8_t>& usb = out.raw;

    out.buttonMask = static_cast<DWORD>(usb[kHatBtn] >> 4)
                   | static_cast<DWORD>(usb[kBtnMid]) << 4
                   | static_cast<DWORD>(usb[kBtnHigh] & 0x03) << 12;
    out.buttonsValid = true;

    // Same usage → slot assignment HidP gives on USB: X/Y left stick, Z/Rz right stick, Rx/Ry L2/R2.
    out.axisX  = normalizeByte(usb[kLeftX]);
    out.axisY  = normalizeByte(usb[kLeftY]);
    out.axisZ  = normalizeByte(usb[kRightX]);
    out.axisRz = normalizeByte(usb[kRightY]);
    out.axisRx = normalizeByte(usb[kL2]);
    out.axisRy = normalizeByte(usb[kR2]);
    out.axisMask = 0;
    for (RawAxis a : { RawAxis::X, RawAxis::Y, RawAxis::Z, RawAxis::Rx, RawAxis::Ry, RawAxis::Rz })
        out.axisMask |= static_cast<uint16_t>(1u << static_cast<int>(a));

    const ULONG hat = usb[kHatBtn] & 0x0F;
    out.hat = (hat <= kHatLogMax) ? hat : 0xFFFFFFFF;

    out.valid = true;
}
