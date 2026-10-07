#pragma once
#include "HidChannel.h"
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <string>

// RAII wrapper for a single HID device handle.
// Owns the Win32 handle, overlapped event, preparsed data, report buffer, and value caps.
// On disconnect (ReadFile error), closes all handles cleanly and marks itself disconnected.
// Both HIDInputSource and the Scanner can hold their own independent instance.
// final: calls through a HIDDevice (not a HidChannel&) can skip the virtual dispatch.
class HIDDevice final : public HidChannel {
public:
    struct ValueRange { LONG logMin; LONG logMax; USHORT bitSize; };

    // ReadOnly is the default: holding write access makes later opens by other apps that don't
    // share write (FILE_SHARE_READ only) fail, so only protocols that must write ask for it.
    enum class Access { ReadOnly, ReadWrite };

    // Opens the device at the given path. name is used only for log messages. ReadWrite falls
    // back to read-only if the OS refuses write access — canWrite() tells which one it got.
    HIDDevice(const std::string& path, const std::string& name = "", Access access = Access::ReadOnly);
    ~HIDDevice() override;

    HIDDevice(const HIDDevice&)            = delete;
    HIDDevice& operator=(const HIDDevice&) = delete;

    bool isConnected() const { return m_connected; }

    // See HidChannel::read. On Disconnected the handles are already closed.
    ReadResult read(int timeoutMs = 20) override;

    const std::vector<BYTE>&                      reportBuf()       const override { return m_reportBuf; }
    ULONG                                         reportLen()       const { return m_inputReportLen; }
    ULONG                                         lastBytesRead()   const override { return m_lastBytesRead; }
    void*                                         preparsed()       const { return m_preparsed; }
    BYTE                                          buttonReportId()  const { return m_buttonReportId; }
    const std::unordered_map<USHORT, ValueRange>& valueCaps()       const { return m_valueCaps; }
    const std::unordered_map<USHORT, USHORT>&     usagePage()       const { return m_usagePage; }

    // Normalize a raw HID axis value to [-1, +1] using the cached value caps.
    float normalizeAxis(USHORT usage, ULONG rawValue) const;

    // Reads one HID usage value (HidP_GetUsageValue), retrying with buttonReportId() swapped into
    // buf[0] if the descriptor's own report ID doesn't match what the device actually sent (BT vs
    // USB report-ID split — same fallback every input path needs when reading this device). buf[0]
    // is restored before returning either way. Returns false if neither attempt produced a value.
    bool getUsageValue(USHORT page, USHORT usage, PULONG value, PCHAR buf, ULONG bufLen) const;

    // HidChannel — raw writes for controller protocols (see HidChannel.h).
    USHORT vendorId() const override { return m_vendorId; }
    // Read from the device too — with vendorId(), picks the controller protocol (registry).
    USHORT productId() const { return m_productId; }
    // USB or Bluetooth, from the path this device was opened with (hidTransportFromPath).
    HidTransport transport() const { return m_transport; }
    bool   canWrite() const override { return m_canWrite; }
    bool   sendOutputReport(const BYTE* data, ULONG len) override;
    bool   setFeature(const BYTE* data, ULONG len) override;
    bool   getFeature(BYTE reportId, std::vector<BYTE>& out) override;

private:
    HANDLE            m_device           = INVALID_HANDLE_VALUE;
    HANDLE            m_event            = nullptr;
    void*             m_preparsed        = nullptr;
    ULONG             m_inputReportLen   = 0;
    ULONG             m_featureReportLen = 0;
    std::vector<BYTE> m_reportBuf;
    bool              m_connected        = false;
    bool              m_canWrite         = false;
    USHORT            m_vendorId         = 0;
    USHORT            m_productId        = 0;
    HidTransport      m_transport        = HidTransport::Unknown;
    BYTE              m_buttonReportId   = 0xFF;
    ULONG             m_lastBytesRead    = 0;

    // The overlapped read is kept pending across read() calls (never cancelled on timeout): with a
    // pad behind xinputhid (X-mode, 045E:02E0), reports arriving while no read was pending were
    // observed to get lost, and an on-change-only pad then loses presses/releases. The OS writes into m_readOv/m_pendingBuf
    // while it's pending, so both outlive read(); m_reportBuf only gets a copy of completed reports.
    OVERLAPPED        m_readOv           = {};
    std::vector<BYTE> m_pendingBuf;
    bool              m_readPending      = false;

    std::unordered_map<USHORT, ValueRange> m_valueCaps;
    std::unordered_map<USHORT, USHORT>     m_usagePage;

    // Issues the next overlapped ReadFile into m_pendingBuf. False = device gone.
    bool startRead();
    void closeHandles();
};
