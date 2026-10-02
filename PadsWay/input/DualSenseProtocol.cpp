#include "DualSenseProtocol.h"

namespace {

// USB report byte positions (REFERENCE.md, "Sony DualSense (PS5)"), as HidP reads them. Unlike the
// DS4, the triggers come right after the sticks and the buttons after a sequence counter.
constexpr ULONG kLeftX   = 1;
constexpr ULONG kLeftY   = 2;
constexpr ULONG kRightX  = 3;
constexpr ULONG kRightY  = 4;
constexpr ULONG kL2      = 5;
constexpr ULONG kR2      = 6;
// Byte 7 is a sequence counter, not input.
constexpr ULONG kHatBtn  = 8;   // low nibble = hat, high nibble = buttons 1-4
constexpr ULONG kBtnMid  = 9;   // buttons 5-12
constexpr ULONG kBtnHigh = 10;  // bits 0-2 = buttons 13-15 (PS, touch click, mute); bits 3-7 unused

} // namespace

// ---------------------------------------------------------------------------

void DualSenseProtocol::decodeUsbReport(const std::vector<uint8_t>& usb, RawHIDState& out) const
{
    out.buttonMask = static_cast<DWORD>(usb[kHatBtn] >> 4)
                   | static_cast<DWORD>(usb[kBtnMid]) << 4
                   | static_cast<DWORD>(usb[kBtnHigh] & 0x07) << 12;
    out.buttonsValid = true;

    setStandardAxes(out, usb[kLeftX], usb[kLeftY], usb[kRightX], usb[kRightY], usb[kL2], usb[kR2]);
    out.hat = decodeHat(usb[kHatBtn]);
}
