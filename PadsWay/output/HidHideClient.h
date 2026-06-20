#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// Programmatic interface to the HidHide kernel filter driver.
//
// Adds/removes entries from the hidden-device list (blacklist) and the
// allowed-applications list (whitelist) without disturbing entries managed
// by other tools or by the HidHide UI.
//
// If HidHide is not installed, isAvailable() returns false and all
// operations are no-ops so the rest of PadsWay keeps working normally.
class HidHideClient {
public:
    HidHideClient();
    ~HidHideClient();   // calls unhideDevice() automatically

    bool isAvailable() const { return m_handle != INVALID_HANDLE_VALUE; }

    // Add PadsWay.exe to the whitelist so it can still read hidden devices.
    // Idempotent — safe to call if already listed.
    void addSelfToWhitelist();

    // Hide a physical device (add to blacklist) by VID+PID.
    // Looks up the device instance path via SetupDI.
    // Remembers what was hidden so unhideDevice() can undo it.
    void hideDevice(uint16_t vid, uint16_t pid);

    // Remove the device hidden by hideDevice() from the blacklist.
    // Called automatically by the destructor.
    void unhideDevice();

private:
    // Device type 32769 (0x8001), function codes 2048-2053, all FILE_READ_DATA
    // Source: HidHide/HidHideCLI/src/FilterDriverProxy.cpp
    static constexpr DWORD kIoctlGetWhitelist = CTL_CODE(32769, 2048, METHOD_BUFFERED, FILE_READ_DATA);
    static constexpr DWORD kIoctlSetWhitelist = CTL_CODE(32769, 2049, METHOD_BUFFERED, FILE_READ_DATA);
    static constexpr DWORD kIoctlGetBlacklist = CTL_CODE(32769, 2050, METHOD_BUFFERED, FILE_READ_DATA);
    static constexpr DWORD kIoctlSetBlacklist = CTL_CODE(32769, 2051, METHOD_BUFFERED, FILE_READ_DATA);
    static constexpr DWORD kIoctlGetActive    = CTL_CODE(32769, 2052, METHOD_BUFFERED, FILE_READ_DATA);
    static constexpr DWORD kIoctlSetActive    = CTL_CODE(32769, 2053, METHOD_BUFFERED, FILE_READ_DATA);

    std::vector<std::wstring> getList(DWORD ioctl);
    void                      setList(DWORD ioctl, const std::vector<std::wstring>& list);
    std::wstring              findInstancePath(uint16_t vid, uint16_t pid);
    bool                      isFilterActive();
    void                      setFilterActive(bool active);

    HANDLE       m_handle             = INVALID_HANDLE_VALUE;
    std::wstring m_hiddenInstancePath;   // empty if nothing hidden by us
    bool         m_weActivated        = false;  // true if we enabled filtering
};
