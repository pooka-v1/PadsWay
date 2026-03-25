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

    ld("TemplatePadSolidPS.png", m_tpl);
    ld("Button.png",             m_btnCircle);
    ld("Alalogic.png",           m_analog);
    ld("GreyCrossTri.png",       m_crossPS);
    ld("L1Button.png",           m_btnL1);
    ld("R1Button.png",           m_btnR1);
    ld("LR2Button.png",          m_btnLR2);
    ld("SelectStarButon.png",    m_btnPill);
    ld("CharacterA.png",         m_symA);
    ld("CharacterB.png",         m_symB);
    ld("CharacterX.png",         m_symX);
    ld("CharacterY.png",         m_symY);
    ld("CharactersL1.png",       m_symL1);
    ld("CharactersR1.png",       m_symR1);
    ld("CharactersL2.png",       m_symL2);
    ld("CharactersR2.png",       m_symR2);

    m_loaded = true;
    return true;
}

void PadView::unload() {
    m_tpl.release();
    m_btnCircle.release();
    m_analog.release();
    m_crossPS.release();
    m_btnL1.release();
    m_btnR1.release();
    m_btnLR2.release();
    m_btnPill.release();
    m_symA.release();  m_symB.release();  m_symX.release();  m_symY.release();
    m_symL1.release(); m_symR1.release(); m_symL2.release(); m_symR2.release();
    m_loaded = false;
}

// ---------------------------------------------------------------------------
// Layout constants for TemplatePadSolidPS.png (480x400)
//
// All values are CENTER coordinates of each element on the template.
// *** Calibrate these values after the first build to match your artwork. ***
// ---------------------------------------------------------------------------

namespace Layout {
    constexpr float W = 480.0f, H = 400.0f;

    // Face buttons — right cluster
    constexpr float FaceCx = 356.0f, FaceCy = 185.0f;  // cluster center
    constexpr float FaceR  = 34.0f;                     // center-to-button distance
    constexpr float BtnYx  = FaceCx,          BtnYy = FaceCy - FaceR;  // △/Y  top
    constexpr float BtnBx  = FaceCx + FaceR,  BtnBy = FaceCy;          // ○/B  right
    constexpr float BtnAx  = FaceCx,          BtnAy = FaceCy + FaceR;  // ×/A  bottom
    constexpr float BtnXx  = FaceCx - FaceR,  BtnXy = FaceCy;          // □/X  left
    constexpr float FaceSize = 34.0f;   // rendered diameter

    // D-pad — left side
    constexpr float DpadCx   = 124.0f, DpadCy   = 183.0f;
    constexpr float DpadSize = 74.0f;
    constexpr float DpadArmR = 22.0f;  // center-to-arm-center, for direction highlights

    // Analog sticks
    constexpr float LStickCx = 180.0f, LStickCy = 270.0f;
    constexpr float RStickCx = 290.0f, RStickCy = 270.0f;
    constexpr float StickSize      = 66.0f;
    constexpr float StickMaxOffset = 12.0f;  // max pixel deflection of the stick head

    // L1/R1 bumpers
    constexpr float L1Cx = 124.0f,  L1Cy = 80.0f;
    constexpr float R1Cx = 356.0f,  R1Cy = 80.0f;
    constexpr float BumperW = 80.0f, BumperH = 26.0f;

    // L2/R2 triggers (top edge of top-down template)
    constexpr float L2Cx = 124.0f,   L2Cy = 44.0f;
    constexpr float R2Cx = 356.0f,   R2Cy = 44.0f;
    constexpr float TriggerW = 72.0f, TriggerH = 40.0f;

    // Select / Start pills — centrados en 240px, separación = 1/3 del ancho de píldora (12px)
    constexpr float BackCx  = 216.0f, BackCy  = 185.0f;
    constexpr float StartCx = 264.0f, StartCy = 185.0f;
    constexpr float PillW = 36.0f,    PillH   = 14.0f;

    // Home button (drawn with a primitive, no asset yet)
    constexpr float HomeCx   = 356.0f, HomeCy   = 253.0f;
    constexpr float HomeSize = 20.0f;
}

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------

static const ImVec4 kInactive = { 0.38f, 0.38f, 0.38f, 0.82f };
static const ImVec4 kColA     = { 0.20f, 0.90f, 0.20f, 1.0f };   // green  ×/A
static const ImVec4 kColB     = { 0.90f, 0.20f, 0.20f, 1.0f };   // red    ○/B
static const ImVec4 kColX     = { 0.20f, 0.50f, 1.00f, 1.0f };   // blue   □/X
static const ImVec4 kColY     = { 1.00f, 0.80f, 0.00f, 1.0f };   // yellow △/Y
static const ImVec4 kColWhite = { 1.00f, 1.00f, 1.00f, 1.0f };
static const ImVec4 kSymDim   = { 0.00f, 0.00f, 0.00f, 0.90f };

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

    // ── Template background ───────────────────────────────────────────────
    img(m_tpl,
        Layout::W * 0.5f, Layout::H * 0.5f,
        Layout::W, Layout::H,
        { 1, 1, 1, 1 });

    // ── L2 / R2 triggers ─────────────────────────────────────────────────
    img(m_btnLR2, Layout::L2Cx, Layout::L2Cy,
        Layout::TriggerW, Layout::TriggerH,
        state.triggerL > 0.05f ? kColWhite : kInactive);
    img(m_symL2, Layout::L2Cx, Layout::L2Cy,
        Layout::TriggerW * 0.62f, Layout::TriggerH * 0.62f,
        state.triggerL > 0.05f ? kColWhite : kSymDim);

    img(m_btnLR2, Layout::R2Cx, Layout::R2Cy,
        Layout::TriggerW, Layout::TriggerH,
        state.triggerR > 0.05f ? kColWhite : kInactive);
    img(m_symR2, Layout::R2Cx, Layout::R2Cy,
        Layout::TriggerW * 0.62f, Layout::TriggerH * 0.62f,
        state.triggerR > 0.05f ? kColWhite : kSymDim);

    // ── L1 / R1 bumpers ───────────────────────────────────────────────────
    img(m_btnL1, Layout::L1Cx, Layout::L1Cy,
        Layout::BumperW, Layout::BumperH,
        state.btnLB ? kColWhite : kInactive);
    img(m_symL1, Layout::L1Cx, Layout::L1Cy,
        Layout::BumperW * 0.68f, Layout::BumperH * 0.90f,
        state.btnLB ? kColWhite : kSymDim);

    img(m_btnR1, Layout::R1Cx, Layout::R1Cy,
        Layout::BumperW, Layout::BumperH,
        state.btnRB ? kColWhite : kInactive);
    img(m_symR1, Layout::R1Cx, Layout::R1Cy,
        Layout::BumperW * 0.68f, Layout::BumperH * 0.90f,
        state.btnRB ? kColWhite : kSymDim);

    // ── D-pad ─────────────────────────────────────────────────────────────
    {
        bool anyDir = state.dpadUp || state.dpadDown || state.dpadLeft || state.dpadRight;
        img(m_crossPS,
            Layout::DpadCx, Layout::DpadCy,
            Layout::DpadSize, Layout::DpadSize,
            anyDir ? kColWhite : kInactive);

        // Small dot on the pressed arm — visible even without arrow assets.
        auto armDot = [&](bool pressed, float ax, float ay) {
            if (!pressed) return;
            ImVec2 c = { origin.x + ax, origin.y + ay };
            dl->AddCircleFilled(c, 6.5f, toU32({ 0.45f, 0.35f, 0.0f, 0.90f }));
        };
        armDot(state.dpadUp,    Layout::DpadCx,                Layout::DpadCy - Layout::DpadArmR);
        armDot(state.dpadDown,  Layout::DpadCx,                Layout::DpadCy + Layout::DpadArmR);
        armDot(state.dpadLeft,  Layout::DpadCx - Layout::DpadArmR, Layout::DpadCy);
        armDot(state.dpadRight, Layout::DpadCx + Layout::DpadArmR, Layout::DpadCy);
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
            // Stick head — small filled circle offset by deflection
            float hx = cx + dx * Layout::StickMaxOffset;
            float hy = cy - dy * Layout::StickMaxOffset;  // Y axis: up is positive in state, down on screen
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

    // ── Home — no asset yet, drawn with primitives ─────────────────────────
    {
        ImVec2 hc = { origin.x + Layout::HomeCx, origin.y + Layout::HomeCy };
        float  hr = Layout::HomeSize * 0.5f;
        if (state.btnHome) {
            dl->AddCircleFilled(hc, hr, toU32(kColWhite));
        } else {
            dl->AddCircleFilled(hc, hr, toU32({ 0.25f, 0.25f, 0.25f, 0.85f }));
            dl->AddCircle(hc, hr, toU32(kInactive), 16, 1.5f);
        }
    }

    // Advance ImGui layout cursor past the drawn area.
    ImGui::Dummy({ Layout::W, Layout::H });
}
