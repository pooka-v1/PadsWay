#pragma once
#include <string>
#include <vector>

// A single visual element within a pad layout.
// The renderer iterates the component list in order (back-to-front) and draws each one.
struct PadComponent {
    std::string id;
    std::string view;   // "front" | "top"
    std::string type;   // "template" | "button" | "stick" | "dpad" | "decoration"

    // Images (filenames relative to images/)
    std::string image;
    std::string overlay;
    float overlayScaleX = 1.0f;   // overlay drawn at w*scaleX, h*scaleY
    float overlayScaleY = 1.0f;

    // Dpad arm images
    std::string imageUp, imageDown, imageLeft, imageRight;

    // Position / size (canvas coords, center-based)
    float cx = 0.0f, cy = 0.0f;
    float w  = 0.0f, h  = 0.0f;   // template / button
    float size      = 0.0f;        // stick diameter
    float maxOffset = 0.0f;        // stick max deflection in pixels

    // State bindings (string names resolved against GamepadState at render time)
    std::string state;             // bool or float field name
    float       threshold = 0.05f; // float-as-button: pressed if state > threshold
    std::string stateX, stateY, stateClick;           // stick
    std::string stateUp, stateDown, stateLeft, stateRight;  // dpad

    // Colors [r, g, b, a]
    float colorR         = 0.38f, colorG         = 0.38f, colorB         = 0.38f, colorA         = 1.0f;
    float activeColorR   = 1.00f, activeColorG   = 1.00f, activeColorB   = 1.00f, activeColorA   = 1.0f;
    float ovColorR       = 0.00f, ovColorG       = 0.00f, ovColorB       = 0.00f, ovColorA       = 0.90f;
    float activeOvColorR = 1.00f, activeOvColorG = 1.00f, activeOvColorB = 1.00f, activeOvColorA = 1.0f;
};

struct PadLayout {
    std::string id;
    float W      = 480.0f;
    float FrontH = 200.0f;
    float TopH   = 320.0f;
    std::vector<PadComponent> components;
};
