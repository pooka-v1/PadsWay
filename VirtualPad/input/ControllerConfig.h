#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

enum class ButtonActionType { VirtualButton, Trigger, Bot, Macro };

struct ButtonAction {
    ButtonActionType type      = ButtonActionType::VirtualButton;
    std::string      name;       // virtual button ("a","b",...), bot/macro name
    std::string      axis;       // trigger only: WinMM source ("dwUpos", "dwVpos")
    std::string      target;     // trigger only: "l2" or "r2"
    std::string      execution;  // macro only: compact execution string
};

struct AxisMapping {
    std::string target;
    bool        invert = false;
};

struct ControllerConfig {
    uint16_t    vid = 0;
    uint16_t    pid = 0;
    std::string source_name;
    std::string mode;

    std::unordered_map<int, ButtonAction>        buttons;  // physical bit (1-indexed) -> action
    std::unordered_map<std::string, AxisMapping> axes;     // WinMM source name -> mapping
    std::string dpad;
};
