#include "Ds4Protocol.h"

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

} // namespace

// ---------------------------------------------------------------------------

void Ds4Protocol::decodeUsbReport(const std::vector<uint8_t>& usb, RawHIDState& out) const
{
    out.buttonMask = static_cast<DWORD>(usb[kHatBtn] >> 4)
                   | static_cast<DWORD>(usb[kBtnMid]) << 4
                   | static_cast<DWORD>(usb[kBtnHigh] & 0x03) << 12;
    out.buttonsValid = true;

    setStandardAxes(out, usb[kLeftX], usb[kLeftY], usb[kRightX], usb[kRightY], usb[kL2], usb[kR2]);
    out.hat = decodeHat(usb[kHatBtn]);
}
