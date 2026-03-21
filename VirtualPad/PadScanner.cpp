#include "PadScanner.h"

std::vector<PadScanner::DeviceInfo> PadScanner::scan() {
    std::vector<DeviceInfo> result;
    UINT numDevs = joyGetNumDevs();

    for (UINT id = 0; id < numDevs; ++id) {
        JOYINFOEX info = {};
        info.dwSize  = sizeof(JOYINFOEX);
        info.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(id, &info) != JOYERR_NOERROR) continue;

        DeviceInfo dev = {};
        dev.port = id;

        JOYCAPS caps = {};
        if (joyGetDevCaps(id, &caps, sizeof(caps)) == JOYERR_NOERROR) {
            dev.axes    = caps.wNumAxes;
            dev.buttons = caps.wNumButtons;
            dev.vid     = caps.wMid;
            dev.pid     = caps.wPid;
            wcsncpy_s(dev.name, caps.szPname, MAXPNAMELEN);
        } else {
            wcscpy_s(dev.name, L"(unknown)");
        }

        result.push_back(dev);
    }

    return result;
}

PadScanner::RawInput PadScanner::readRaw(UINT port) {
    RawInput r = {};
    r.pov = JOY_POVCENTERED;

    JOYINFOEX info = {};
    info.dwSize  = sizeof(JOYINFOEX);
    info.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(port, &info) != JOYERR_NOERROR) {
        r.valid = false;
        return r;
    }

    r.valid   = true;
    r.buttons = info.dwButtons;
    r.xpos    = info.dwXpos;
    r.ypos    = info.dwYpos;
    r.zpos    = info.dwZpos;
    r.rpos    = info.dwRpos;
    r.upos    = info.dwUpos;
    r.vpos    = info.dwVpos;
    r.pov     = info.dwPOV;

    return r;
}
