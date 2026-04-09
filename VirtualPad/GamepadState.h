#pragma once

// Normalized representation of a gamepad state.
// This is the common "language" that all input sources produce
// and the output adapter consumes.
//
// Convention for analog values:
//   Sticks:   -1.0 = fully left / fully down
//             +1.0 = fully right / fully up
//   Triggers:  0.0 = released
//              1.0 = fully pressed

struct GamepadState {
    // --- Face buttons ---
    bool btnA = false;
    bool btnB = false;
    bool btnX = false;
    bool btnY = false;

    // --- Shoulder buttons ---
    bool btnLB = false;
    bool btnRB = false;

    // --- Analog triggers [0.0 .. 1.0] ---
    float triggerL = 0.0f;
    float triggerR = 0.0f;

    // --- Menu buttons ---
    bool btnStart = false;
    bool btnBack  = false;
    bool btnHome  = false;

    // --- Stick clicks ---
    bool btnL3 = false;
    bool btnR3 = false;

    // --- D-Pad (diagonals allowed: up+right simultaneously, etc.) ---
    bool dpadUp    = false;
    bool dpadDown  = false;
    bool dpadLeft  = false;
    bool dpadRight = false;

    // --- Left stick [-1.0 .. 1.0] ---
    float leftX = 0.0f;
    float leftY = 0.0f;

    // --- Right stick [-1.0 .. 1.0] ---
    float rightX = 0.0f;
    float rightY = 0.0f;

    // --- Mouse movement [-1.0 .. 1.0] (populated when an axis maps to mouse_x/mouse_y) ---
    float mouseX = 0.0f;
    float mouseY = 0.0f;

    // --- Extra buttons (8BitDo Pro 3 paddles) ---
    bool btnL4 = false;   // paddle corto izquierdo (al lado del L1)
    bool btnR4 = false;   // paddle corto derecho   (al lado del R1)
    bool btnLP = false;   // paddle largo izquierdo (Lp / L5)
    bool btnRP = false;   // paddle largo derecho   (Rp / R5)

    // --- Touchpad (DS4-style physical touchpad) ---
    bool  btnTouch     = false;  // physical touchpad press
    bool  touch1Active = false;  // finger 1 on pad surface
    float touch1X      = 0.0f;   // finger 1 absolute X position [0..1]
    float touch1Y      = 0.0f;   // finger 1 absolute Y position [0..1]
    bool  touch2Active = false;  // finger 2 on pad surface
    float touch2X      = 0.0f;   // finger 2 absolute X position [0..1]
    float touch2Y      = 0.0f;   // finger 2 absolute Y position [0..1]
    float touchDeltaX  = 0.0f;   // finger 1 delta for mouse routing (derived each frame)
    float touchDeltaY  = 0.0f;

    // --- IMU (gyroscope) [-1.0 .. 1.0] ---
    bool  gyroActive = false;  // true when the device is reporting IMU data this frame
    float gyroX      = 0.0f;  // pitch  (tilt forward/back)
    float gyroY      = 0.0f;  // yaw    (rotate left/right)
    float gyroZ      = 0.0f;  // roll   (tilt left/right)
};
