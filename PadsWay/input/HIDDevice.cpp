#include "HIDDevice.h"
#include "../Log.h"
#include <hidsdi.h>
#include <algorithm>

#pragma comment(lib, "hid.lib")

#define PREPARSED  (static_cast<PHIDP_PREPARSED_DATA>(m_preparsed))

// ---------------------------------------------------------------------------

HIDDevice::HIDDevice(const std::string& path, const std::string& name, Access access)
    : m_transport(hidTransportFromPath(path))
{
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    if (access == Access::ReadWrite) {
        m_device = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, share,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        m_canWrite = (m_device != INVALID_HANDLE_VALUE);
        if (!m_canWrite)
            spdlog::warn("[HIDDevice] Write access refused for '{}' (error {}) — falling back to read-only",
                         name, GetLastError());
    }
    if (m_device == INVALID_HANDLE_VALUE)
        m_device = CreateFileA(path.c_str(), GENERIC_READ, share,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

    if (m_device == INVALID_HANDLE_VALUE) {
        spdlog::error("[HIDDevice] Failed to open '{}' (error {})", name, GetLastError());
        return;
    }

    m_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_event) {
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return;
    }

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(m_device, &preparsed)) {
        spdlog::error("[HIDDevice] Failed to get preparsed data for '{}'", name);
        CloseHandle(m_event);  m_event  = nullptr;
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return;
    }
    m_preparsed = preparsed;

    HIDP_CAPS caps = {};
    if (HidP_GetCaps(PREPARSED, &caps) != HIDP_STATUS_SUCCESS) {
        closeHandles();
        return;
    }

    m_inputReportLen   = caps.InputReportByteLength;
    m_featureReportLen = caps.FeatureReportByteLength;
    m_reportBuf.resize(m_inputReportLen, 0);

    HIDD_ATTRIBUTES attr = {};
    attr.Size = sizeof(attr);
    if (HidD_GetAttributes(m_device, &attr)) {
        m_vendorId  = attr.VendorID;
        m_productId = attr.ProductID;
    }

    // Build value caps map (usage → logical range + page).
    // Handles range caps (e.g. 8BitDo Pro 3 D-mode) and page collisions
    // (e.g. DS4 BT exposes usage 0x30 on both standard and vendor pages —
    // standard page wins).
    USHORT numCaps = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> valueCaps(numCaps);
    HidP_GetValueCaps(HidP_Input, valueCaps.data(), &numCaps, PREPARSED);

    auto insertCap = [&](USHORT u, USHORT page, const ValueRange& vr) {
        auto it = m_usagePage.find(u);
        if (it == m_usagePage.end()) {
            m_valueCaps.emplace(u, vr);
            m_usagePage.emplace(u, page);
        } else {
            bool existingIsStandard = (it->second < 0xFF00);
            bool newIsStandard      = (page       < 0xFF00);
            if (newIsStandard && !existingIsStandard) {
                m_valueCaps[u] = vr;
                m_usagePage[u] = page;
            }
        }
    };

    for (USHORT ci = 0; ci < numCaps; ++ci) {
        ValueRange vr = { valueCaps[ci].LogicalMin, valueCaps[ci].LogicalMax, valueCaps[ci].BitSize };
        USHORT     page = valueCaps[ci].UsagePage;
        if (valueCaps[ci].IsRange) {
            for (USHORT u = valueCaps[ci].Range.UsageMin; u <= valueCaps[ci].Range.UsageMax; ++u)
                insertCap(u, page, vr);
        } else {
            insertCap(valueCaps[ci].NotRange.Usage, page, vr);
        }
    }

    // Find report ID used for buttons (may differ from the report ID in the packet).
    USHORT numBtnCaps = caps.NumberInputButtonCaps;
    if (numBtnCaps > 0) {
        std::vector<HIDP_BUTTON_CAPS> btnCaps(numBtnCaps);
        if (HidP_GetButtonCaps(HidP_Input, btnCaps.data(), &numBtnCaps, PREPARSED) == HIDP_STATUS_SUCCESS
            && numBtnCaps > 0)
            m_buttonReportId = btnCaps[0].ReportID;
    }

    // Shrink the OS input report queue from its default (~32) to the minimum.
    // read() returns the OLDEST queued report, so a deep queue means we always
    // forward stale input when the device emits faster than the read loop
    // consumes (USB at 250-1000 Hz, BT ~100 Hz vs our ~125 Hz). With a queue of
    // 2, the report we read is at most ~1-2 reports old → input stays current
    // regardless of the device's report rate.
    if (!HidD_SetNumInputBuffers(m_device, 2))
        spdlog::warn("[HIDDevice] SetNumInputBuffers(2) failed (error {}) — using OS default queue",
                     GetLastError());

    spdlog::info("[HIDDevice] Opened: {}  ReportLen={}  ValueCaps={}  BtnCaps={}  Write={}",
        name.empty() ? path : name, m_inputReportLen, numCaps, numBtnCaps, m_canWrite);

    m_connected = true;
}

HIDDevice::~HIDDevice()
{
    closeHandles();
}

// ---------------------------------------------------------------------------

void HIDDevice::closeHandles()
{
    if (m_preparsed) {
        HidD_FreePreparsedData(PREPARSED);
        m_preparsed = nullptr;
    }
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
    if (m_device != INVALID_HANDLE_VALUE) {
        CloseHandle(m_device);
        m_device = INVALID_HANDLE_VALUE;
    }
    m_connected = false;
    m_canWrite  = false;
}

// ---------------------------------------------------------------------------

HIDDevice::ReadResult HIDDevice::read(int timeoutMs)
{
    if (!m_connected) return ReadResult::Disconnected;

    ResetEvent(m_event);
    OVERLAPPED ov = {};
    ov.hEvent = m_event;

    DWORD bytesRead = 0;
    BOOL  readOk    = ReadFile(m_device, m_reportBuf.data(), m_inputReportLen, &bytesRead, &ov);

    if (!readOk) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            closeHandles();
            return ReadResult::Disconnected;
        }
        DWORD wait = WaitForSingleObject(m_event, static_cast<DWORD>(timeoutMs));
        if (wait != WAIT_OBJECT_0) {
            CancelIo(m_device);
            WaitForSingleObject(m_event, INFINITE);
            return ReadResult::Timeout;
        }
        if (!GetOverlappedResult(m_device, &ov, &bytesRead, FALSE)) {
            closeHandles();
            return ReadResult::Disconnected;
        }
    }

    m_lastBytesRead = static_cast<ULONG>(bytesRead);
    return ReadResult::Ok;
}

// ---------------------------------------------------------------------------

float HIDDevice::normalizeAxis(USHORT usage, ULONG rawValue) const
{
    auto it = m_valueCaps.find(usage);
    if (it == m_valueCaps.end()) return 0.0f;

    LONG logMin = it->second.logMin;
    LONG logMax = it->second.logMax;

    // Unsigned range: descriptor reports [0, -1] meaning [0, 2^BitSize - 1]
    if (logMax < logMin) {
        USHORT bits = it->second.bitSize;
        ULONG  uMax = (bits > 0 && bits < 32) ? (1UL << bits) - 1 : 0xFFFFFFFFUL;
        if (uMax == 0) return 0.0f;
        float norm = static_cast<float>(rawValue) / static_cast<float>(uMax) * 2.0f - 1.0f;
        return std::clamp(norm, -1.0f, 1.0f);
    }

    LONG range = logMax - logMin;
    if (range <= 0) return 0.0f;

    float norm = (static_cast<float>(static_cast<LONG>(rawValue) - logMin) / range) * 2.0f - 1.0f;
    return std::clamp(norm, -1.0f, 1.0f);
}

bool HIDDevice::getUsageValue(USHORT page, USHORT usage, PULONG value, PCHAR buf, ULONG bufLen) const
{
    NTSTATUS status = HidP_GetUsageValue(HidP_Input, page, 0, usage, value, PREPARSED, buf, bufLen);
    if (status == HIDP_STATUS_INCOMPATIBLE_REPORT_ID && m_buttonReportId != 0xFF) {
        char savedId = buf[0];
        buf[0] = static_cast<char>(m_buttonReportId);
        status = HidP_GetUsageValue(HidP_Input, page, 0, usage, value, PREPARSED, buf, bufLen);
        buf[0] = savedId;
    }
    return status == HIDP_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// HidChannel writes. HidD_* are synchronous IOCTLs and work on the overlapped handle as-is,
// without touching m_event (which belongs to read()).

bool HIDDevice::sendOutputReport(const BYTE* data, ULONG len)
{
    if (!m_connected || !m_canWrite || !data || len == 0) return false;
    // Control pipe, report sent at its own length: validated on DS4 / DualSense BT (78-byte
    // 0x11/0x31 against a 547-byte OutputReportByteLength). A pad that only accepts interrupt-pipe
    // writes (WriteFile, padded to OutputReportByteLength) would need a second path here.
    if (HidD_SetOutputReport(m_device, const_cast<BYTE*>(data), len)) return true;
    spdlog::warn("[HIDDevice] SetOutputReport 0x{:02X} failed (error {})", data[0], GetLastError());
    return false;
}

bool HIDDevice::setFeature(const BYTE* data, ULONG len)
{
    if (!m_connected || !m_canWrite || !data || len == 0) return false;
    if (HidD_SetFeature(m_device, const_cast<BYTE*>(data), len)) return true;
    spdlog::warn("[HIDDevice] SetFeature 0x{:02X} failed (error {})", data[0], GetLastError());
    return false;
}

bool HIDDevice::getFeature(BYTE reportId, std::vector<BYTE>& out)
{
    // Not gated on m_canWrite: reading a feature report doesn't need write access — but note that
    // on Sony pads this very read is what switches them to full mode.
    if (!m_connected || m_featureReportLen == 0) return false;
    out.assign(m_featureReportLen, 0);
    out[0] = reportId;
    if (HidD_GetFeature(m_device, out.data(), m_featureReportLen)) return true;
    spdlog::warn("[HIDDevice] GetFeature 0x{:02X} failed (error {})", reportId, GetLastError());
    return false;
}
