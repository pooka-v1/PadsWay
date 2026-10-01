#pragma once
#include "ControllerProtocol.h"
#include <memory>

// Sony DualShock 4 (ARCHITECTURE.md, "Protocolos de mando", task 4). Over USB the pad already sends
// its full report and HidP reads it, so this protocol only matters over Bluetooth: there the pad
// starts with a short 0x01 report (sticks, buttons, triggers — no IMU, no touch) and switches to
// the full 0x11 report once someone reads its calibration feature report 0x05.
//
// decode() turns the BT 0x11 report into exactly what HidP produces from the USB report, so
// controllers.json, the wizard and the IMU/touch offsets need one DS4 entry for both connections.
// Every other report (USB 0x01, BT short 0x01) goes to the fallback protocol given at construction
// — GenericHidProtocol in the app, a fake in the tests.
class Ds4Protocol final : public ControllerProtocol {
public:
    static constexpr USHORT kSonyVendorId = 0x054C;

    // BT full report: [0]=0x11, [1..2] BT header, [3..] = the USB report from its byte [1], CRC32 in
    // the last 4 bytes. Fixed length: ReadFile pads the buffer to InputReportByteLength (547) with
    // leftovers of earlier reports, so the OS-returned length is never the report's real length.
    static constexpr BYTE  kBtFullReportId  = 0x11;
    static constexpr ULONG kBtFullReportLen = 78;
    static constexpr ULONG kBtHeaderLen     = 2;     // BT byte i+2 = USB byte i (i >= 1)

    // USB input report 0x01 — the stable layout decode() produces over both connections.
    static constexpr BYTE  kUsbReportId     = 0x01;
    static constexpr ULONG kUsbReportLen    = 64;

    // Reading this feature report switches the pad to the full 0x11 report (its payload is the
    // IMU calibration). Reading needs no write access, so the DS4 doesn't need a writable handle.
    static constexpr BYTE  kActivationFeatureId = 0x05;

    // Activation is confirmed when a 0x11 report arrives: at most this many reads of
    // kConfirmReadTimeoutMs each (~30 ms observed with real hardware).
    static constexpr int   kConfirmMaxReads      = 16;
    static constexpr int   kConfirmReadTimeoutMs = 20;

    explicit Ds4Protocol(std::unique_ptr<ControllerProtocol> fallback) : m_fallback(std::move(fallback)) {}

    // Over BT: requests the full report and waits for the first 0x11. USB needs nothing. Never
    // writes to a pad whose own vendor ID isn't Sony, whatever controllers.json says.
    bool enableFullMode(HidChannel& channel, HidTransport transport) override;

    bool decode(const BYTE* report, ULONG len, RawHIDState& out) override;

private:
    std::unique_ptr<ControllerProtocol> m_fallback;

    // BT 0x11 (CRC already checked) → USB layout, same values HidP would give.
    static void decodeBtFullReport(const BYTE* report, RawHIDState& out);
};
