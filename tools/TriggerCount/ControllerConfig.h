#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

struct AxisMapping {
    std::string source;   // WinMM field name: "dwXpos", "dwYpos", "dwZpos", "dwRpos", "dwUpos", "dwVpos"
    bool        invert;   // negate the normalized value
};

struct TriggerMapping {
    std::string axis;     // analog source (empty if not available)
    int         button;   // digital fallback, 1-indexed (0 = none)
};

struct ControllerConfig {
    uint16_t    vid;
    uint16_t    pid;
    std::string source_name;    // szPname reported by WinMM
    std::string mode;           // "dinput" or "xinput"

    std::unordered_map<std::string, int>            buttons;   // button name  -> WinMM button number (1-indexed)
    std::unordered_map<std::string, AxisMapping>    axes;      // axis name    -> mapping
    std::unordered_map<std::string, TriggerMapping> triggers;  // trigger name -> mapping
    std::string dpad;           // "pov" or "" (not mapped)
};
