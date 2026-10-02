#pragma once
#include "SonyProtocol.h"

// Sony DualShock 4 (ARCHITECTURE.md, "Protocolos de mando", task 4). Activation, CRC and the
// rebuild of the USB report live in SonyProtocol; this class only says what is DS4-specific: its
// BT full report 0x11 with a 2-byte header, and where its USB report keeps buttons, hat and axes.
class Ds4Protocol final : public SonyProtocol {
public:
    // BT full report: [0]=0x11, [1..2] BT header, [3..] = the USB report from its byte [1].
    static constexpr BYTE  kBtFullReportId = 0x11;
    static constexpr ULONG kBtHeaderLen    = 2;     // BT byte i+2 = USB byte i (i >= 1)

    explicit Ds4Protocol(std::unique_ptr<ControllerProtocol> fallback)
        : SonyProtocol(std::move(fallback), kBtFullReportId, kBtHeaderLen) {}

protected:
    void decodeUsbReport(const std::vector<uint8_t>& usb, RawHIDState& out) const override;
};
