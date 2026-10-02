#include "SonyProtocol.h"
#include "SonyCrc.h"
#include <algorithm>

// No logging here on purpose: the caller (enableFullModeWithRetries, the readers) logs the outcome,
// and the unit tests build this file without spdlog.

namespace {

constexpr float kAxisLogMax = 255.0f;
constexpr ULONG kHatLogMax  = 7;

} // namespace

// ---------------------------------------------------------------------------

bool SonyProtocol::enableFullMode(HidChannel& channel, HidTransport transport)
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
            && !channel.reportBuf().empty() && channel.reportBuf()[0] == m_btFullReportId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

bool SonyProtocol::decode(const BYTE* report, ULONG len, RawHIDState& out)
{
    if (!report || len == 0) return false;

    if (report[0] != m_btFullReportId)
        return m_fallback ? m_fallback->decode(report, len, out) : false;

    // A truncated or corrupted full report is dropped whole — never decode half a report.
    if (len < kBtFullReportLen) return false;
    if (!sony_crc::check(sony_crc::kSeedInput, report, kBtFullReportLen)) return false;

    // Rebuild the USB report first, then read everything from it: one layout to get right.
    out.raw.assign(kUsbReportLen, 0);
    out.raw[0] = kUsbReportId;
    std::copy(report + 1 + m_btHeaderLen, report + kUsbReportLen + m_btHeaderLen, out.raw.begin() + 1);

    decodeUsbReport(out.raw, out);
    out.valid = true;
    return true;
}

// ---------------------------------------------------------------------------

float SonyProtocol::normalizeAxisByte(BYTE raw)
{
    return std::clamp(static_cast<float>(raw) / kAxisLogMax * 2.0f - 1.0f, -1.0f, 1.0f);
}

void SonyProtocol::setStandardAxes(RawHIDState& out, BYTE leftX, BYTE leftY, BYTE rightX, BYTE rightY,
                                   BYTE l2, BYTE r2)
{
    out.axisX  = normalizeAxisByte(leftX);
    out.axisY  = normalizeAxisByte(leftY);
    out.axisZ  = normalizeAxisByte(rightX);
    out.axisRz = normalizeAxisByte(rightY);
    out.axisRx = normalizeAxisByte(l2);
    out.axisRy = normalizeAxisByte(r2);
    out.axisMask = 0;
    for (RawAxis a : { RawAxis::X, RawAxis::Y, RawAxis::Z, RawAxis::Rx, RawAxis::Ry, RawAxis::Rz })
        out.axisMask |= static_cast<uint16_t>(1u << static_cast<int>(a));
}

ULONG SonyProtocol::decodeHat(BYTE nibble)
{
    const ULONG hat = nibble & 0x0F;
    return (hat <= kHatLogMax) ? hat : 0xFFFFFFFF;
}
