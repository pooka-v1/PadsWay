#include "XInputFeed.h"
#include "../Log.h"
#include <initguid.h>   // makes devpkey.h below define the DEVPKEY_* constants in this file
#include <devpkey.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")

namespace {

// The interface xinputhid (and the wired Xbox driver) registers for XInput to find pads.
const GUID kXusbInterfaceGuid =
    { 0xEC87F1E3, 0xC13B, 0x4100, { 0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB } };

// XUSB "get gamepad state" — undocumented; what xinput1_4.dll sends for XInputGetState. The
// request is the protocol version (0x0101, little-endian) and a device index, always 0 here.
// The answer layout below was checked against real pads with temp/gameinput-probe/xusb_probe:
//   [0-1] version  [2] status (1 = connected)  [5-8] packet counter
//   [11-22] the 12 bytes of XINPUT_GAMEPAD (buttons, LT, RT, LX, LY, RX, RY)
constexpr DWORD kIoctlXusbGetGamepadState = 0x8000E00C;
constexpr BYTE  kXusbStateRequest[]       = { 0x01, 0x01, 0x00 };
constexpr DWORD kXusbStatusOffset         = 2;
constexpr BYTE  kXusbStatusConnected      = 0x01;
constexpr DWORD kXusbGamepadOffset        = 11;

constexpr ULONGLONG kReopenMs = 1000;

// Not in xinput.h: the Guide button, which the XUSB answer carries like any other button.
constexpr WORD kXInputGamepadGuide = 0x0400;

// Button N+1 of the layout (see XInputFeed.h) = bit N of RawHIDState::buttonMask.
constexpr WORD kButtonOrder[] = {
    XINPUT_GAMEPAD_A,             XINPUT_GAMEPAD_B,
    XINPUT_GAMEPAD_X,             XINPUT_GAMEPAD_Y,
    XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
    XINPUT_GAMEPAD_BACK,          XINPUT_GAMEPAD_START,
    XINPUT_GAMEPAD_LEFT_THUMB,    XINPUT_GAMEPAD_RIGHT_THUMB,
    kXInputGamepadGuide,
};

// std::clamp, not std::max: windows.h's max macro is live in this file (no NOMINMAX).
float stickAxis(SHORT v)    { return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f); }
float triggerAxis(BYTE v)   { return static_cast<float>(v) / 127.5f - 1.0f; }   // 0..255 -> -1..1
uint16_t axisBit(RawAxis a) { return static_cast<uint16_t>(1u << static_cast<int>(a)); }

int pressed(WORD buttons, WORD bit) { return (buttons & bit) ? 1 : 0; }

// D-pad bits -> hat value, same numbering HIDInputSource::parseHIDDpad reads. Opposite directions
// pressed together cancel out, as they can't on a physical hat.
ULONG hatFromDpad(WORD buttons) {
    const int x = pressed(buttons, XINPUT_GAMEPAD_DPAD_RIGHT) - pressed(buttons, XINPUT_GAMEPAD_DPAD_LEFT);
    const int y = pressed(buttons, XINPUT_GAMEPAD_DPAD_DOWN)  - pressed(buttons, XINPUT_GAMEPAD_DPAD_UP);
    constexpr ULONG kCentred = 0xFFFFFFFF;
    constexpr ULONG kHatByDirection[3][3] = {   // [y + 1][x + 1]
        { 7, 0,        1 },   // up:     NW  N  NE
        { 6, kCentred, 2 },   // middle: W   -  E
        { 5, 4,        3 },   // down:   SW  S  SE
    };
    return kHatByDirection[y + 1][x + 1];
}

} // namespace

// ---------------------------------------------------------------------------

bool xusbStateToGamepad(const BYTE* answer, DWORD size, XINPUT_GAMEPAD& pad) {
    if (size < kXusbGamepadOffset + sizeof(XINPUT_GAMEPAD)) return false;
    if (answer[kXusbStatusOffset] != kXusbStatusConnected) return false;
    std::memcpy(&pad, answer + kXusbGamepadOffset, sizeof(XINPUT_GAMEPAD));
    return true;
}

void xinputGamepadToRawState(const XINPUT_GAMEPAD& pad, RawHIDState& out) {
    out.buttonMask = 0;
    for (size_t i = 0; i < std::size(kButtonOrder); ++i)
        if (pad.wButtons & kButtonOrder[i]) out.buttonMask |= (1u << i);
    out.buttonsValid = true;

    out.axisX     =  stickAxis(pad.sThumbLX);
    out.axisY     = -stickAxis(pad.sThumbLY);   // XInput's Y grows upwards, HID's downwards
    out.axisRx    =  stickAxis(pad.sThumbRX);
    out.axisRy    = -stickAxis(pad.sThumbRY);
    out.axisZ     = (static_cast<float>(pad.bLeftTrigger) - static_cast<float>(pad.bRightTrigger)) / 255.0f;
    out.axisBrake = triggerAxis(pad.bLeftTrigger);
    out.axisAccel = triggerAxis(pad.bRightTrigger);
    out.axisMask  = static_cast<uint16_t>(axisBit(RawAxis::X)  | axisBit(RawAxis::Y)  | axisBit(RawAxis::Z)
                                        | axisBit(RawAxis::Rx) | axisBit(RawAxis::Ry)
                                        | axisBit(RawAxis::Brake) | axisBit(RawAxis::Accel));

    out.hat = hatFromDpad(pad.wButtons);

    out.raw.assign(1 + sizeof(XINPUT_GAMEPAD), 0);
    std::memcpy(out.raw.data() + 1, &pad, sizeof(XINPUT_GAMEPAD));

    out.valid = true;
}

// ---------------------------------------------------------------------------

XInputFeed::XInputFeed(const std::wstring& xusbInterfacePath, const std::string& name)
    : m_xusbPath(xusbInterfacePath), m_name(name)
{
    spdlog::info("[XInput][{}] Behind xinputhid — reading it through its XUSB interface", m_name);
}

XInputFeed::~XInputFeed() {
    close();
}

bool XInputFeed::open() {
    // Read and write access: the IOCTL's access bits ask for both, as for XInput itself.
    m_handle = CreateFileW(m_xusbPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    return m_handle != INVALID_HANDLE_VALUE;
}

void XInputFeed::close() {
    if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
}

bool XInputFeed::poll(RawHIDState& out) {
    if (m_handle == INVALID_HANDLE_VALUE) {
        const ULONGLONG now = GetTickCount64();
        if (now < m_nextOpenMs) return false;
        m_nextOpenMs = now + kReopenMs;
        if (!open()) {
            if (!m_reportedFailure)
                spdlog::warn("[XInput][{}] Can't open the XUSB interface (error {}) — retrying every {} ms",
                             m_name, GetLastError(), kReopenMs);
            m_reportedFailure = true;
            return false;
        }
    }

    BYTE  answer[64] = {};
    DWORD got        = 0;
    XINPUT_GAMEPAD pad = {};
    if (!DeviceIoControl(m_handle, kIoctlXusbGetGamepadState, const_cast<BYTE*>(kXusbStateRequest),
                         sizeof(kXusbStateRequest), answer, sizeof(answer), &got, nullptr)
            || !xusbStateToGamepad(answer, got, pad)) {
        if (!m_reportedFailure)
            spdlog::warn("[XInput][{}] XUSB state unreadable (error {}, {} bytes) — reopening",
                         m_name, GetLastError(), got);
        m_reportedFailure = true;
        close();
        return false;
    }
    if (m_reportedFailure) spdlog::info("[XInput][{}] XUSB state readable again", m_name);
    m_reportedFailure = false;

    xinputGamepadToRawState(pad, out);
    return true;
}

// ---------------------------------------------------------------------------

std::wstring findXusbInterface(const std::string& hidDevicePath) {
    // Interface paths are plain ASCII: widening char by char is enough.
    const std::wstring hidPath(hidDevicePath.begin(), hidDevicePath.end());

    wchar_t     instanceId[MAX_DEVICE_ID_LEN] = {};
    ULONG       size = sizeof(instanceId);
    DEVPROPTYPE type = 0;
    if (CM_Get_Device_Interface_PropertyW(hidPath.c_str(), &DEVPKEY_Device_InstanceId, &type,
                                          reinterpret_cast<PBYTE>(instanceId), &size, 0) != CR_SUCCESS)
        return {};

    // Only the XUSB interfaces of that same device node.
    GUID  xusbGuid = kXusbInterfaceGuid;   // the CM API takes a non-const GUID*
    ULONG listLen  = 0;
    if (CM_Get_Device_Interface_List_SizeW(&listLen, &xusbGuid, instanceId,
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS
            || listLen <= 1)
        return {};   // an empty list is a single terminating NUL
    std::vector<wchar_t> list(listLen + 1, 0);
    if (CM_Get_Device_Interface_ListW(&xusbGuid, instanceId, list.data(), listLen,
                                      CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
        return {};
    return list.data();   // the first path of the NUL-separated list; a node has only one
}
