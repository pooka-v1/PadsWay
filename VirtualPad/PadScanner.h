#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <vector>

// Stateless utility for enumerating WinMM joystick devices and reading raw input.
// All methods are static — no threading, no state.
// WinMM reads are non-exclusive, so this can run alongside PadEngine safely.
class PadScanner {
public:
    struct DeviceInfo {
        UINT    port;
        UINT    axes;
        UINT    buttons;
        WORD    vid;
        WORD    pid;
        wchar_t name[MAXPNAMELEN];
    };

    struct RawInput {
        DWORD buttons;                          // button bitmask
        DWORD xpos, ypos, zpos, rpos, upos, vpos;  // raw axis values [0..65535]
        DWORD pov;                              // POV hat (hundredths of a degree, JOY_POVCENTERED if none)
        bool  valid;                            // false if the read failed
    };

    // Scans all WinMM slots and returns the ones that respond.
    static std::vector<DeviceInfo> scan();

    // Reads a full raw input report from the given port.
    static RawInput readRaw(UINT port);
};
