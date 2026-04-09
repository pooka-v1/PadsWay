#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Stateless utility for enumerating HID joystick/gamepad devices.
// Only returns devices with HID Generic Desktop usage page (0x01),
// joystick (0x04) or gamepad (0x05) — filters out keyboards, mice, etc.
class HIDScanner {
public:
    struct DeviceInfo {
        WORD        vid;
        WORD        pid;
        USHORT      usagePage;
        USHORT      usage;           // 0x04 = Joystick, 0x05 = Gamepad
        std::string path;            // device interface path (for CreateFile)
        std::string productName;
        std::string connectionType;  // "usb" or "bt" (detected from path)
    };

    static std::vector<DeviceInfo> scan();
};
