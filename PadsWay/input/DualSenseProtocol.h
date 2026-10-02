#pragma once
#include "SonyProtocol.h"

// Sony DualSense (ARCHITECTURE.md, "Protocolos de mando", task 5). Activation, CRC and the rebuild
// of the USB report live in SonyProtocol; this class only says what is DualSense-specific: its BT
// full report 0x31 with a 1-byte header, and where its USB report keeps buttons, hat and axes.
class DualSenseProtocol final : public SonyProtocol {
public:
    // BT full report: [0]=0x31, [1] sequence tag, [2..] = the USB report from its byte [1].
    static constexpr BYTE  kBtFullReportId = 0x31;
    static constexpr ULONG kBtHeaderLen    = 1;     // BT byte i+1 = USB byte i (i >= 1)

    explicit DualSenseProtocol(std::unique_ptr<ControllerProtocol> fallback)
        : SonyProtocol(std::move(fallback), kBtFullReportId, kBtHeaderLen) {}

protected:
    void decodeUsbReport(const std::vector<uint8_t>& usb, RawHIDState& out) const override;
};
