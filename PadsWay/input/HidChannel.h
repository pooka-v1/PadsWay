#pragma once
#include <windows.h>
#include <vector>

// Raw report I/O over one HID device, as a controller protocol needs it: read what the pad sends,
// and write the output/feature reports that switch it to its full mode (ARCHITECTURE.md,
// "Protocolos de mando"). An interface so protocols can be unit-tested against a fake device.
class HidChannel {
public:
    enum class ReadResult { Ok, Timeout, Disconnected };

    virtual ~HidChannel() = default;

    // Blocking read with timeout:
    //   Ok           — new report is in reportBuf(); lastBytesRead() says how much the OS returned
    //                  (padded to the descriptor's longest input report, so not the report's real length)
    //   Timeout      — no new data within timeoutMs; last reportBuf() unchanged
    //   Disconnected — device gone
    virtual ReadResult               read(int timeoutMs)   = 0;
    virtual const std::vector<BYTE>& reportBuf()     const = 0;
    virtual ULONG                    lastBytesRead() const = 0;

    // Vendor ID reported by the device itself, not by config — protocols check it before writing,
    // so a mis-edited controllers.json entry can't send vendor requests to another brand's pad.
    virtual USHORT vendorId() const = 0;

    // False when the handle is read-only (write access not requested, or refused by the OS).
    // sendOutputReport/setFeature then fail without touching the device.
    virtual bool canWrite() const = 0;

    // data[0] is the report ID. False on failure (read-only handle, device gone, report refused).
    virtual bool sendOutputReport(const BYTE* data, ULONG len) = 0;
    virtual bool setFeature(const BYTE* data, ULONG len)       = 0;

    // Fetches feature report reportId into out (resized to the device's feature report length,
    // out[0] = reportId). False on failure.
    virtual bool getFeature(BYTE reportId, std::vector<BYTE>& out) = 0;
};
