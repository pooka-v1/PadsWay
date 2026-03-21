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
};
