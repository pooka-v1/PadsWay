#pragma once
#include <d3d11.h>
#include "../GamepadState.h"

// RAII wrapper for a single D3D11 shader resource view loaded from a PNG.
struct PadTexture {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;

    bool valid() const { return srv != nullptr; }
    void release() {
        if (srv) { srv->Release(); srv = nullptr; }
        w = h = 0;
    }

    PadTexture() = default;
    PadTexture(const PadTexture&) = delete;
    PadTexture& operator=(const PadTexture&) = delete;
};

// Renders a visual representation of the active gamepad state.
// D1: top-down read-only view + D2: front view strip above it. Buttons light up when pressed.
class PadView {
public:
    // Load all PNG assets from images/. Non-fatal if individual files are missing.
    bool load(ID3D11Device* device);

    // Render the pad view inside the current ImGui window at the current cursor position.
    void render(const GamepadState& state);

    // Release all D3D11 resources. Call before releasing the D3D device.
    void unload();

private:
    static bool loadPng(ID3D11Device* device, const char* path, PadTexture& out);

    // --- Templates ---
    PadTexture m_tplFront;    // TemplatePadSolidPSFront.png  — front/edge view strip
    PadTexture m_tpl;         // TemplatePadSolidPSTop.png    — top-down view

    // --- Button shapes ---
    PadTexture m_btnCircle;   // CircularButton.png  — face button shape
    PadTexture m_btnSquare;   // SquareButton.png    — paddle L4/R4 shape
    PadTexture m_analog;      // Alalogic.png        — analog stick base
    PadTexture m_crossPS;     // GreyCrossPS.png     — d-pad shape (fallback)
    PadTexture m_crossUp;    // CrossUp.png    — brazo cruceta arriba
    PadTexture m_crossDown;  // CrossDown.png  — brazo cruceta abajo
    PadTexture m_crossLeft;  // CrossLeft.png  — brazo cruceta izquierda
    PadTexture m_crossRight; // CrossRight.png — brazo cruceta derecha
    PadTexture m_btnL1;       // L1Button.png
    PadTexture m_btnR1;       // R1Button.png
    PadTexture m_btnLR2;      // LR2Button.png       — shared L2/R2 shape
    PadTexture m_btnPill;     // SelectStarButon.png — select/start pill

    // --- Symbols / labels ---
    PadTexture m_symA, m_symB, m_symX, m_symY;
    PadTexture m_symL1, m_symR1, m_symL2, m_symR2;
    PadTexture m_symL4, m_symR4;
    PadTexture m_btnL5;       // L5Button.png  — paddle largo izquierdo (Lp)
    PadTexture m_btnR5;       // R5Button.png  — paddle largo derecho  (Rp)
    PadTexture m_symL5, m_symR5;
    PadTexture m_homeIcon;    // 8BitDoHomeBotton.png — home symbol (overlaid on CircularButton)

    bool m_loaded = false;
};
