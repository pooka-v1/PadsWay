#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

enum class ButtonActionType { VirtualButton, Trigger, Bot, Macro, Keyboard, MouseClick };

struct ButtonAction {
    ButtonActionType     type      = ButtonActionType::VirtualButton;
    std::string          name;        // virtual button ("a","b",...), bot/macro name
    std::string          physical;    // nombre del botón en el mando físico ("a","l4","rp"...)
                                     // independiente de la acción — nunca sobreescrito por perfiles
    std::string          axis;        // trigger only: WinMM source ("dwUpos", "dwVpos")
    std::string          target;      // trigger only: "l2" or "r2"
    std::string          execution;   // macro only: compact execution string
    std::vector<std::string> keys;    // keyboard only: e.g. ["alt","tab"]
    std::string          mouseButton; // mouse_click only: "left","right","middle"
};

struct AxisMapping {
    std::string target;
    bool        invert = false;
    float       speed  = 15.0f;  // mouse_x/mouse_y only: pixels per tick at full deflection
};

struct TouchpadConfig {
    bool enabled      = false;
    int  dataOffset   = 34;    // byte index of finger-1 data in raw HID report (DS4 USB: 34)
    int  maxX         = 1919;  // DS4 touchpad horizontal resolution
    int  maxY         = 942;   // DS4 touchpad vertical resolution
    bool mouseEnabled = false; // route surface movement → mouse (delta-based)
};

struct ImuConfig {
    bool  enabled     = false;
    int   gyroOffset  = 13;    // byte index of gyro X in raw HID report (DS4 USB: 13)
    float gyroScale   = 1.0f / 32768.0f;  // int16 raw → normalized [-1..1]
};

struct ControllerConfig {
    uint16_t    vid = 0;
    uint16_t    pid = 0;
    std::string source_name;
    std::string mode;
    std::string connection;    // "usb" / "bt" / "" = match any

    std::unordered_map<int, ButtonAction>        buttons;  // physical bit (1-indexed) -> action
    std::unordered_map<std::string, AxisMapping> axes;     // WinMM source name -> mapping
    std::string    dpad;
    std::string    layout_id;  // references an entry in data/pad_layouts.json; empty = use defaults
    TouchpadConfig touchpad;
    ImuConfig      imu;
};
