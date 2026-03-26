#include "PadView.h"

#include <wincodec.h>
#include <vector>
#include <cstdio>

#include "../imgui/imgui.h"

#pragma comment(lib, "windowscodecs.lib")

// ---------------------------------------------------------------------------
// PNG loader — WIC (no extra dependencies)
// ---------------------------------------------------------------------------

bool PadView::loadPng(ID3D11Device* device, const char* path, PadTexture& out) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // safe to call multiple times

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;

    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(wpath, nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) {
        factory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        decoder->Release(); factory->Release();
        return false;
    }

    IWICFormatConverter* conv = nullptr;
    factory->CreateFormatConverter(&conv);
    conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT w, h;
    conv->GetSize(&w, &h);

    std::vector<uint8_t> pixels(w * h * 4);
    conv->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem     = pixels.data();
    sd.SysMemPitch = w * 4;

    bool ok = false;
    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&td, &sd, &tex))) {
        ok = SUCCEEDED(device->CreateShaderResourceView(tex, nullptr, &out.srv));
        tex->Release();
        if (ok) { out.w = (int)w; out.h = (int)h; }
    }

    conv->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return ok;
}

// ---------------------------------------------------------------------------
// load / unload
// ---------------------------------------------------------------------------

bool PadView::load(ID3D11Device* device) {
    auto ld = [&](const char* name, PadTexture& t) {
        char path[256];
        snprintf(path, sizeof(path), "images/%s", name);
        loadPng(device, path, t);  // non-fatal if file is missing
    };

    ld("TemplatePadSolidPSFront.png", m_tplFront);
    ld("TemplatePadSolidPSTop.png",   m_tpl);
    ld("CircularButton.png",          m_btnCircle);
    ld("SquareButton.png",            m_btnSquare);
    ld("Alalogic.png",                m_analog);
    ld("GreyCrossPS.png",             m_crossPS);
    ld("CrossUp.png",                 m_crossUp);
    ld("CrossDown.png",               m_crossDown);
    ld("CrossLeft.png",               m_crossLeft);
    ld("CrossRight.png",              m_crossRight);
    ld("L1Button.png",                m_btnL1);
    ld("R1Button.png",                m_btnR1);
    ld("LR2Button.png",               m_btnLR2);
    ld("SelectStarButon.png",         m_btnPill);
    ld("CharacterA.png",              m_symA);
    ld("CharacterB.png",              m_symB);
    ld("CharacterX.png",              m_symX);
    ld("CharacterY.png",              m_symY);
    ld("CharactersL1.png",            m_symL1);
    ld("CharactersR1.png",            m_symR1);
    ld("CharactersL2.png",            m_symL2);
    ld("CharactersR2.png",            m_symR2);
    ld("CharactersL4.png",            m_symL4);
    ld("CharactersR4.png",            m_symR4);
    ld("L5Button.png",                m_btnL5);
    ld("R5Button.png",                m_btnR5);
    ld("CharactersL5.png",            m_symL5);
    ld("CharactersR5.png",            m_symR5);
    ld("8BitDoHomeBotton.png",        m_homeIcon);

    m_loaded = true;
    return true;
}

void PadView::unload() {
    m_tplFront.release();
    m_tpl.release();
    m_btnCircle.release();
    m_btnSquare.release();
    m_analog.release();
    m_crossPS.release();
    m_crossUp.release(); m_crossDown.release();
    m_crossLeft.release(); m_crossRight.release();
    m_btnL1.release();
    m_btnR1.release();
    m_btnLR2.release();
    m_btnPill.release();
    m_symA.release();  m_symB.release();  m_symX.release();  m_symY.release();
    m_symL1.release(); m_symR1.release(); m_symL2.release(); m_symR2.release();
    m_symL4.release(); m_symR4.release();
    m_btnL5.release(); m_btnR5.release();
    m_symL5.release(); m_symR5.release();
    m_homeIcon.release();
    m_loaded = false;
}

// ---------------------------------------------------------------------------
// Layout constants
//
// All values are CENTER coordinates within the full canvas.
// Canvas is portrait: front-edge strip on top, top-down view below.
//
// *** Calibrate these values after the first build to match your artwork. ***
// ---------------------------------------------------------------------------

namespace Layout {

    constexpr float W = 480.0f;

    // -- Front view strip (TemplatePadSolidPSFront.png) --------------------
    constexpr float FrontH = 160.0f;
    constexpr float FrontTplCx = W * 0.5f;
    constexpr float FrontTplCy = FrontH * 0.5f;

    // Triggers (L2/R2):
    //   - Bajados su propia altura (FTriggerH) respecto al borrador inicial.
    //   - L2 alineado con L1 por la derecha, R2 con R1 por la izquierda.
    constexpr float FTriggerW = 72.0f, FTriggerH = 44.0f;
    constexpr float FBumperW  = 80.0f, FBumperH  = 28.0f;
    constexpr float FPaddleW  = 37.0f, FPaddleH  = 29.0f;
    constexpr float FPaddleLongW = 72.0f, FPaddleLongH = 56.0f;

    // Bumpers (L1/R1) — bajados 10px respecto a borrador.
    constexpr float FL1Cx = 105.0f, FL1Cy = 128.0f;
    constexpr float FR1Cx = 375.0f, FR1Cy = 128.0f;

    // Triggers (L2/R2) — bajados FTriggerH, alineados con L1/R1 por su borde exterior.
    //   Derecha de L2 = derecha de L1: FL2Cx = FL1Cx + FBumperW/2 - FTriggerW/2
    //   Izquierda de R2 = izquierda de R1: FR2Cx = FR1Cx - FBumperW/2 + FTriggerW/2
    constexpr float FL2Cx = FL1Cx + FBumperW * 0.5f - FTriggerW * 0.5f + 2.0f;  // = 111
    constexpr float FL2Cy = 48.0f + FTriggerH;                                  // = 92
    constexpr float FR2Cx = FR1Cx - FBumperW * 0.5f + FTriggerW * 0.5f - 2.0f; // = 369
    constexpr float FR2Cy = FL2Cy;

    // Paddles cortos (L4/R4) — misma altura que L1/R1, L4 a la derecha de L1, R4 a la izquierda de R1.
    constexpr float FL4Cx = FL1Cx + FBumperW * 0.5f + FPaddleW * 0.5f;   // = 161
    constexpr float FL4Cy = FL1Cy;
    constexpr float FR4Cx = FR1Cx - FBumperW * 0.5f - FPaddleW * 0.5f;   // = 319
    constexpr float FR4Cy = FR1Cy;

    // Paddles largos (L5=Lp / R5=Rp) — subidos 30px, L5 a la izquierda 50px, R5 a la derecha 50px.
    constexpr float FL5Cx = FL2Cx + FTriggerW * 0.5f + FPaddleLongW * 0.5f - 50.0f;
    constexpr float FL5Cy = FL2Cy - 28.0f;
    constexpr float FR5Cx = FR2Cx - FTriggerW * 0.5f - FPaddleLongW * 0.5f + 50.0f;
    constexpr float FR5Cy = FR2Cy - 28.0f;

    // -- Top-down view (TemplatePadSolidPSTop.png) -------------------------
    constexpr float TopH = 400.0f;
    constexpr float TopY = FrontH;
    constexpr float H    = FrontH + TopH;

    // Face buttons — subidos DpadSize (74px) respecto al borrador.
    constexpr float FaceCx = 356.0f, FaceCy = TopY + 131.0f;
    constexpr float FaceR  = 34.0f;
    constexpr float BtnYx  = FaceCx,          BtnYy = FaceCy - FaceR;
    constexpr float BtnBx  = FaceCx + FaceR,  BtnBy = FaceCy;
    constexpr float BtnAx  = FaceCx,          BtnAy = FaceCy + FaceR;
    constexpr float BtnXx  = FaceCx - FaceR,  BtnXy = FaceCy;
    constexpr float FaceSize = 34.0f;

    // D-pad — bajado 20px, 20% más grande.
    constexpr float DpadCx   = 124.0f, DpadCy = TopY + 129.0f;
    constexpr float DpadSize = 89.0f;
    constexpr float DpadArmR = 22.0f;

    // Analog sticks — al 60% del alto de la sección superior.
    constexpr float StickSize      = 66.0f;
    constexpr float StickMaxOffset = 12.0f;
    constexpr float LStickCx = 180.0f, LStickCy = TopY + TopH * 0.6f - StickSize * 0.25f;
    constexpr float RStickCx = 290.0f, RStickCy = TopY + TopH * 0.6f - StickSize * 0.25f;

    // Select / Start — bajados 20px.
    constexpr float BackCx  = 216.0f, BackCy  = TopY + 131.0f;
    constexpr float StartCx = 264.0f, StartCy = TopY + 131.0f;
    constexpr float PillW = 36.0f,    PillH   = 14.0f;

    // Home — debajo del botón A, centrado en A, desplazado su propio alto hacia abajo.
    constexpr float HomeSize = 26.0f;
    constexpr float HomeCx   = FaceCx;
    constexpr float HomeCy   = BtnAy + HomeSize + 25.0f;
}

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------

static const ImVec4 kInactive = { 0.38f, 0.38f, 0.38f, 1.00f };
static const ImVec4 kColA     = { 0.20f, 0.90f, 0.20f, 1.0f };   // green  ×/A
static const ImVec4 kColB     = { 0.90f, 0.20f, 0.20f, 1.0f };   // red    ○/B
static const ImVec4 kColX     = { 0.20f, 0.50f, 1.00f, 1.0f };   // blue   □/X
static const ImVec4 kColY     = { 1.00f, 0.80f, 0.00f, 1.0f };   // yellow △/Y
static const ImVec4 kColWhite = { 1.00f, 1.00f, 1.00f, 1.0f };
static const ImVec4 kSymDim   = { 0.00f, 0.00f, 0.00f, 0.90f };
static const ImVec4 kTplTint  = { 1.00f, 0.50f, 0.08f, 1.0f };   // orange body

static ImU32 toU32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void PadView::render(const GamepadState& state) {
    if (!m_loaded) {
        ImGui::TextDisabled("Assets not loaded.");
        return;
    }

    ImVec2       origin = ImGui::GetCursorScreenPos();
    ImDrawList*  dl     = ImGui::GetWindowDrawList();

    // Draw a texture centered at (cx, cy) with the given size and tint.
    auto img = [&](const PadTexture& t,
                   float cx, float cy, float w, float h,
                   ImVec4 tint) {
        if (!t.valid()) return;
        ImVec2 p0 = { origin.x + cx - w * 0.5f, origin.y + cy - h * 0.5f };
        ImVec2 p1 = { p0.x + w, p0.y + h };
        dl->AddImage((ImTextureID)(intptr_t)t.srv, p0, p1,
                     { 0, 0 }, { 1, 1 }, toU32(tint));
    };

    // Draw a round face button: shape + symbol on top.
    auto faceBtn = [&](const PadTexture& sym,
                       float cx, float cy,
                       bool pressed, ImVec4 activeCol) {
        img(m_btnCircle, cx, cy, Layout::FaceSize, Layout::FaceSize,
            pressed ? activeCol : kInactive);
        if (sym.valid()) {
            float ssz = Layout::FaceSize * 0.68f;
            img(sym, cx, cy, ssz, ssz,
                pressed ? kColWhite : kSymDim);
        }
    };

    // =========================================================================
    // FRONT VIEW — edge strip (TemplatePadSolidPSFront.png)
    // =========================================================================

    img(m_tplFront,
        Layout::FrontTplCx, Layout::FrontTplCy,
        Layout::W, Layout::FrontH,
        kTplTint);

    // ── L2 / R2 (front view) ─────────────────────────────────────────────
    img(m_btnLR2, Layout::FL2Cx, Layout::FL2Cy,
        Layout::FTriggerW, Layout::FTriggerH,
        state.triggerL > 0.05f ? kColWhite : kInactive);
    img(m_symL2, Layout::FL2Cx, Layout::FL2Cy,
        Layout::FTriggerW * 0.62f, Layout::FTriggerH * 0.62f,
        state.triggerL > 0.05f ? kColWhite : kSymDim);

    img(m_btnLR2, Layout::FR2Cx, Layout::FR2Cy,
        Layout::FTriggerW, Layout::FTriggerH,
        state.triggerR > 0.05f ? kColWhite : kInactive);
    img(m_symR2, Layout::FR2Cx, Layout::FR2Cy,
        Layout::FTriggerW * 0.62f, Layout::FTriggerH * 0.62f,
        state.triggerR > 0.05f ? kColWhite : kSymDim);

    // ── L1 / R1 (front view) ─────────────────────────────────────────────
    img(m_btnL1, Layout::FL1Cx, Layout::FL1Cy,
        Layout::FBumperW, Layout::FBumperH,
        state.btnLB ? kColWhite : kInactive);
    img(m_symL1, Layout::FL1Cx, Layout::FL1Cy,
        Layout::FBumperW * 0.68f, Layout::FBumperH * 0.90f,
        state.btnLB ? kColWhite : kSymDim);

    img(m_btnR1, Layout::FR1Cx, Layout::FR1Cy,
        Layout::FBumperW, Layout::FBumperH,
        state.btnRB ? kColWhite : kInactive);
    img(m_symR1, Layout::FR1Cx, Layout::FR1Cy,
        Layout::FBumperW * 0.68f, Layout::FBumperH * 0.90f,
        state.btnRB ? kColWhite : kSymDim);

    // ── L4 / R4 paddles cortos (front view) — misma altura que L1/R1 ──────
    img(m_btnSquare, Layout::FL4Cx, Layout::FL4Cy,
        Layout::FPaddleW, Layout::FPaddleH,
        state.btnL4 ? kColWhite : kInactive);
    img(m_symL4, Layout::FL4Cx, Layout::FL4Cy,
        Layout::FPaddleW * 0.72f, Layout::FPaddleH * 0.72f,
        state.btnL4 ? kColWhite : kSymDim);

    img(m_btnSquare, Layout::FR4Cx, Layout::FR4Cy,
        Layout::FPaddleW, Layout::FPaddleH,
        state.btnR4 ? kColWhite : kInactive);
    img(m_symR4, Layout::FR4Cx, Layout::FR4Cy,
        Layout::FPaddleW * 0.72f, Layout::FPaddleH * 0.72f,
        state.btnR4 ? kColWhite : kSymDim);

    // ── L5 / R5 paddles largos (front view) — al lado de L2/R2 ────────────
    img(m_btnL5, Layout::FL5Cx, Layout::FL5Cy,
        Layout::FPaddleLongW, Layout::FPaddleLongH,
        state.btnLP ? kColWhite : kInactive);
    img(m_symL5, Layout::FL5Cx, Layout::FL5Cy,
        Layout::FPaddleLongW * 0.72f, Layout::FPaddleLongH * 0.72f,
        state.btnLP ? kColWhite : kSymDim);

    img(m_btnR5, Layout::FR5Cx, Layout::FR5Cy,
        Layout::FPaddleLongW, Layout::FPaddleLongH,
        state.btnRP ? kColWhite : kInactive);
    img(m_symR5, Layout::FR5Cx, Layout::FR5Cy,
        Layout::FPaddleLongW * 0.72f, Layout::FPaddleLongH * 0.72f,
        state.btnRP ? kColWhite : kSymDim);

    // =========================================================================
    // TOP VIEW — top-down (TemplatePadSolidPSTop.png)
    // =========================================================================

    img(m_tpl,
        Layout::W * 0.5f, Layout::TopY + Layout::TopH * 0.5f,
        Layout::W, Layout::TopH,
        kTplTint);

    // ── D-pad — 4 brazos independientes ───────────────────────────────────
    {
        // Punto de unión = (DpadCx, DpadCy). Cada brazo tiene su extremo de unión
        // en el borde contrario a la dirección, por lo que el centro del asset se
        // desplaza la mitad del ancho/alto desde el punto central.
        auto dpadArm = [&](const PadTexture& t, float cx, float cy, bool pressed) {
            if (!t.valid()) return;
            img(t, cx, cy, (float)t.w, (float)t.h,
                pressed ? kColWhite : kInactive);
        };
        dpadArm(m_crossUp,    Layout::DpadCx,                              Layout::DpadCy - m_crossUp.h    * 0.5f + 2.0f, state.dpadUp);
        dpadArm(m_crossDown,  Layout::DpadCx,                              Layout::DpadCy + m_crossDown.h  * 0.5f, state.dpadDown);
        dpadArm(m_crossLeft,  Layout::DpadCx - m_crossLeft.w  * 0.5f,     Layout::DpadCy,                         state.dpadLeft);
        dpadArm(m_crossRight, Layout::DpadCx + m_crossRight.w * 0.5f,     Layout::DpadCy,                         state.dpadRight);
    }

    // ── Face buttons ──────────────────────────────────────────────────────
    faceBtn(m_symY, Layout::BtnYx, Layout::BtnYy, state.btnY, kColY);
    faceBtn(m_symB, Layout::BtnBx, Layout::BtnBy, state.btnB, kColB);
    faceBtn(m_symA, Layout::BtnAx, Layout::BtnAy, state.btnA, kColA);
    faceBtn(m_symX, Layout::BtnXx, Layout::BtnXy, state.btnX, kColX);

    // ── Analog sticks ─────────────────────────────────────────────────────
    {
        auto drawStick = [&](float cx, float cy, float dx, float dy, bool click) {
            img(m_analog, cx, cy,
                Layout::StickSize, Layout::StickSize,
                click ? kColWhite : kInactive);
            float hx = cx + dx * Layout::StickMaxOffset;
            float hy = cy - dy * Layout::StickMaxOffset;
            ImVec2 hc = { origin.x + hx, origin.y + hy };
            dl->AddCircleFilled(hc, 5.5f, toU32(click ? kColWhite : ImVec4{ 0.72f, 0.72f, 0.72f, 1.0f }));
            dl->AddCircle(hc, 5.5f, toU32({ 0.15f, 0.15f, 0.15f, 1.0f }), 12, 1.5f);
        };
        drawStick(Layout::LStickCx, Layout::LStickCy, state.leftX,  state.leftY,  state.btnL3);
        drawStick(Layout::RStickCx, Layout::RStickCy, state.rightX, state.rightY, state.btnR3);
    }

    // ── Select / Start ────────────────────────────────────────────────────
    img(m_btnPill, Layout::BackCx,  Layout::BackCy,  Layout::PillW, Layout::PillH,
        state.btnBack  ? kColWhite : kInactive);
    img(m_btnPill, Layout::StartCx, Layout::StartCy, Layout::PillW, Layout::PillH,
        state.btnStart ? kColWhite : kInactive);

    // ── Home — CircularButton + 8BitDo icon ───────────────────────────────
    img(m_btnCircle, Layout::HomeCx, Layout::HomeCy,
        Layout::HomeSize, Layout::HomeSize,
        state.btnHome ? kColWhite : kInactive);
    if (m_homeIcon.valid()) {
        float iconSz = Layout::HomeSize * 0.72f;
        img(m_homeIcon, Layout::HomeCx, Layout::HomeCy, iconSz, iconSz,
            kTplTint);
    }

    // Advance ImGui layout cursor past the drawn area.
    ImGui::Dummy({ Layout::W, Layout::H });
}
