#pragma once
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <string>

// RAII wrapper for a single HID device handle.
// Owns the Win32 handle, overlapped event, preparsed data, report buffer, and value caps.
// On disconnect (ReadFile error), closes all handles cleanly and marks itself disconnected.
// Both HIDInputSource and the Scanner can hold their own independent instance.
class HIDDevice {
public:
    struct ValueRange { LONG logMin; LONG logMax; USHORT bitSize; };

    enum class ReadResult { Ok, Timeout, Disconnected };

    // Opens the device at the given path. name is used only for log messages.
    HIDDevice(const std::string& path, const std::string& name = "");
    ~HIDDevice();

    HIDDevice(const HIDDevice&)            = delete;
    HIDDevice& operator=(const HIDDevice&) = delete;

    bool isConnected() const { return m_connected; }

    // Blocking read with timeout. Returns:
    //   Ok          — new report is in reportBuf()
    //   Timeout     — no new data within timeoutMs; last reportBuf() unchanged
    //   Disconnected — device gone; handles are already closed
    ReadResult read(int timeoutMs = 20);

    const std::vector<BYTE>&                      reportBuf()       const { return m_reportBuf; }
    ULONG                                         reportLen()       const { return m_inputReportLen; }
    ULONG                                         lastBytesRead()   const { return m_lastBytesRead; }
    void*                                         preparsed()       const { return m_preparsed; }
    BYTE                                          buttonReportId()  const { return m_buttonReportId; }
    const std::unordered_map<USHORT, ValueRange>& valueCaps()       const { return m_valueCaps; }
    const std::unordered_map<USHORT, USHORT>&     usagePage()       const { return m_usagePage; }

    // Normalize a raw HID axis value to [-1, +1] using the cached value caps.
    float normalizeAxis(USHORT usage, ULONG rawValue) const;

private:
    HANDLE            m_device         = INVALID_HANDLE_VALUE;
    HANDLE            m_event          = nullptr;
    void*             m_preparsed      = nullptr;
    ULONG             m_inputReportLen = 0;
    std::vector<BYTE> m_reportBuf;
    bool              m_connected      = false;
    BYTE              m_buttonReportId = 0xFF;
    ULONG             m_lastBytesRead  = 0;

    std::unordered_map<USHORT, ValueRange> m_valueCaps;
    std::unordered_map<USHORT, USHORT>     m_usagePage;

    void closeHandles();
};
