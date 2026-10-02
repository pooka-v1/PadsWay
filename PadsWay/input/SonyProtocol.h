#pragma once
#include "ControllerProtocol.h"
#include <memory>
#include <vector>

// What the Sony pads that share one Bluetooth scheme have in common — DS4 and DualSense
// (ARCHITECTURE.md, "Protocolos de mando"). Over USB they already send their full report and HidP
// reads it, so this only matters over Bluetooth: there the pad starts with a short 0x01 report
// (sticks, buttons, triggers — no IMU, no touch) and switches to its full report once someone
// reads its calibration feature report 0x05. The full report is the USB report shifted by a small
// BT header, with a CRC32 at the end.
//
// Template Method: this class activates the pad, checks the full report and rebuilds the USB
// report from it; each model (Ds4Protocol, DualSenseProtocol) gives its full report ID, its BT
// header length and how to read its USB report (decodeUsbReport()). A Sony pad with a different
// scheme (e.g. DualShock 3) derives from ControllerProtocol directly instead.
//
// decode() turns the BT full report into exactly what HidP produces from the USB report, so
// controllers.json, the wizard and the IMU/touch offsets need one entry per model for both
// connections. Every other report (USB 0x01, BT short 0x01) goes to the fallback protocol given at
// construction — GenericHidProtocol in the app, a fake in the tests.
class SonyProtocol : public ControllerProtocol {
public:
    static constexpr USHORT kSonyVendorId = 0x054C;

    // BT full report length, CRC32 in its last 4 bytes. Fixed: ReadFile pads the buffer to
    // InputReportByteLength with leftovers of earlier reports, so the OS-returned length is never
    // the report's real length.
    static constexpr ULONG kBtFullReportLen = 78;

    // USB input report 0x01 — the stable layout decode() produces over both connections.
    static constexpr BYTE  kUsbReportId  = 0x01;
    static constexpr ULONG kUsbReportLen = 64;

    // Reading this feature report switches the pad to its full report (its payload is the IMU
    // calibration). Reading needs no write access, so the pad doesn't need a writable handle.
    static constexpr BYTE  kActivationFeatureId = 0x05;

    // Activation is confirmed when a full report arrives: at most this many reads of
    // kConfirmReadTimeoutMs each (~30 ms observed with real hardware).
    static constexpr int   kConfirmMaxReads      = 16;
    static constexpr int   kConfirmReadTimeoutMs = 20;

    // Over BT: requests the full report and waits for the first one. USB needs nothing. Never
    // writes to a pad whose own vendor ID isn't Sony, whatever controllers.json says.
    bool enableFullMode(HidChannel& channel, HidTransport transport) final;

    bool decode(const BYTE* report, ULONG len, RawHIDState& out) final;

protected:
    // btFullReportId: the model's BT full report ID. btHeaderLen: bytes between it and the USB
    // report's byte [1] (BT byte i + btHeaderLen = USB byte i, for i >= 1).
    SonyProtocol(std::unique_ptr<ControllerProtocol> fallback, BYTE btFullReportId, ULONG btHeaderLen)
        : m_fallback(std::move(fallback)), m_btFullReportId(btFullReportId), m_btHeaderLen(btHeaderLen) {}

    // Reads buttons, axes and hat from the rebuilt USB report (kUsbReportLen bytes) into out, the
    // same values HidP gives over USB. usb is out.raw itself: it must not be modified.
    virtual void decodeUsbReport(const std::vector<uint8_t>& usb, RawHIDState& out) const = 0;

    // The descriptor of both pads declares every axis as 0..255 and the hat as 0..7 (8 = neutral).
    // Must match HIDDevice::normalizeAxis() for that range, or the same stick would read
    // differently over BT.
    static float normalizeAxisByte(BYTE raw);
    // Writes the six axes in the slots HidP gives them on USB — X/Y left stick, Z/Rz right stick,
    // Rx/Ry L2/R2 — and marks them present.
    static void  setStandardAxes(RawHIDState& out, BYTE leftX, BYTE leftY, BYTE rightX, BYTE rightY,
                                 BYTE l2, BYTE r2);
    // Hat from its 4-bit field: 0-7 pass through, anything else is neutral (0xFFFFFFFF).
    static ULONG decodeHat(BYTE nibble);

private:
    std::unique_ptr<ControllerProtocol> m_fallback;
    BYTE  m_btFullReportId;
    ULONG m_btHeaderLen;
};
