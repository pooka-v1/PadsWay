#include "PadView.h"
#include "../config/Strings.h"

#include <wincodec.h>
#include <unordered_set>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cmath>

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
// load / setLayout / unload
// ---------------------------------------------------------------------------

bool PadView::load(ID3D11Device* device) {
    m_device = device;
    m_loaded = true;
    loadPng(device, "images/decorations/ArrowUp.png",    m_arrowUp);
    loadPng(device, "images/decorations/ArrowDown.png",  m_arrowDown);
    loadPng(device, "images/decorations/ArrowLeft.png",  m_arrowLeft);
    loadPng(device, "images/decorations/ArrowRight.png", m_arrowRight);
    loadPng(device, "images/gyroscope/gyrosspera.png",           m_gyroSphere);
    loadPng(device, "images/gyroscope/ArrowNort.png",            m_gyroArrowN);
    loadPng(device, "images/gyroscope/ArrowSouth.png",           m_gyroArrowS);
    loadPng(device, "images/gyroscope/ArrowEst.png",             m_gyroArrowE);
    loadPng(device, "images/gyroscope/ArrowWest.png",            m_gyroArrowW);
    loadPng(device, "images/gyroscope/ArrowClockwise.png",       m_gyroArrowCW);
    loadPng(device, "images/gyroscope/ArrowCounterclockwise.png",m_gyroArrowCCW);
    loadPng(device, "images/gyroscope/LevelBar.png",             m_gyroLevelBar);
    loadPng(device, "images/decorations/TouchPanel.png",         m_touchSurfaceIcon);
    loadPng(device, "images/decorations/TouchPressButton.png",   m_touchButtonIcon);
    return true;
}

void PadView::forceSetLayout(const PadLayout& layout) {
    std::string savedId = m_layout.id;
    m_layout.id = "";   // defeat the id-cache check in setLayout
    setLayout(layout);
    if (m_layout.id.empty())        // setLayout didn't touch it (device not ready)
        m_layout.id = savedId;
}

void PadView::updateLayout(const PadLayout& layout) {
    // Update geometry/bindings without touching the texture cache.
    m_layout.W          = layout.W;
    m_layout.FrontH     = layout.FrontH;
    m_layout.TopH       = layout.TopH;
    m_layout.components = layout.components;
    // m_layout.id intentionally left unchanged so setLayout still skips reloads.
}

void PadView::setLayout(const PadLayout& layout) {
    if (!m_device || !m_loaded) { m_layout = layout; return; }
    if (layout.id == m_layout.id) return;

    // Collect all image names referenced by the new layout
    std::unordered_set<std::string> needed;
    for (const auto& c : layout.components) {
        if (!c.image.empty())       needed.insert(c.image);
        if (!c.overlay.empty())     needed.insert(c.overlay);
        if (!c.imageUp.empty())    { needed.insert(c.imageUp);
                                     needed.insert(c.imageDown);
                                     needed.insert(c.imageLeft);
                                     needed.insert(c.imageRight); }
    }

    // Release textures no longer needed
    for (auto it = m_textures.begin(); it != m_textures.end(); ) {
        if (needed.find(it->first) == needed.end()) {
            it->second.release();
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }

    // Load any texture not yet present
    for (const auto& name : needed) {
        if (m_textures.find(name) != m_textures.end()) continue;
        PadTexture tex;
        char path[256];
        snprintf(path, sizeof(path), "images/%s", name.c_str());
        loadPng(m_device, path, tex);           // non-fatal if file missing
        m_textures.emplace(name, std::move(tex));
    }

    m_layout = layout;
}

void PadView::unload() {
    for (auto& [name, tex] : m_textures)
        tex.release();
    m_textures.clear();
    m_arrowUp.release();
    m_arrowDown.release();
    m_arrowLeft.release();
    m_arrowRight.release();
    m_gyroSphere.release();
    m_gyroArrowN.release();
    m_gyroArrowS.release();
    m_gyroArrowE.release();
    m_gyroArrowW.release();
    m_gyroArrowCW.release();
    m_gyroArrowCCW.release();
    m_gyroLevelBar.release();
    m_touchSurfaceIcon.release();
    m_touchButtonIcon.release();
    m_device = nullptr;
    m_loaded = false;
}

// ---------------------------------------------------------------------------
// State resolvers
// ---------------------------------------------------------------------------

static bool resolveState(const GamepadState& s, const std::string& name, float threshold) {
    if (name == "btnA")      return s.btnA;
    if (name == "btnB")      return s.btnB;
    if (name == "btnX")      return s.btnX;
    if (name == "btnY")      return s.btnY;
    if (name == "btnLB")     return s.btnLB;
    if (name == "btnRB")     return s.btnRB;
    if (name == "btnL3")     return s.btnL3;
    if (name == "btnR3")     return s.btnR3;
    if (name == "btnL4")     return s.btnL4;
    if (name == "btnR4")     return s.btnR4;
    if (name == "btnLP")     return s.btnLP;
    if (name == "btnRP")     return s.btnRP;
    if (name == "btnBack")   return s.btnBack;
    if (name == "btnStart")  return s.btnStart;
    if (name == "btnHome")   return s.btnHome;
    if (name == "dpadUp")    return s.dpadUp;
    if (name == "dpadDown")  return s.dpadDown;
    if (name == "dpadLeft")  return s.dpadLeft;
    if (name == "dpadRight") return s.dpadRight;
    if (name == "triggerL")    return s.triggerL > threshold;
    if (name == "triggerR")    return s.triggerR > threshold;
    if (name == "btnTouch")    return s.btnTouch;
    if (name == "touch1Active") return s.touch1Active;
    if (name == "touch2Active") return s.touch2Active;
    return false;
}

static float resolveFloat(const GamepadState& s, const std::string& name) {
    if (name == "leftX")    return s.leftX;
    if (name == "leftY")    return s.leftY;
    if (name == "rightX")   return s.rightX;
    if (name == "rightY")   return s.rightY;
    if (name == "triggerL") return s.triggerL;
    if (name == "triggerR") return s.triggerR;
    if (name == "touch1X")  return s.touch1X;
    if (name == "touch1Y")  return s.touch1Y;
    if (name == "touch2X")  return s.touch2X;
    if (name == "touch2Y")  return s.touch2Y;
    if (name == "gyroX")    return s.gyroX;
    if (name == "gyroY")    return s.gyroY;
    if (name == "gyroZ")    return s.gyroZ;
    return 0.0f;
}

// ---------------------------------------------------------------------------
// getTex
// ---------------------------------------------------------------------------

const PadTexture* PadView::getTex(const std::string& name) const {
    if (name.empty()) return nullptr;
    auto it = m_textures.find(name);
    return (it != m_textures.end() && it->second.valid()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

bool PadView::getTextureSize(const std::string& name, int& w, int& h) const {
    const PadTexture* t = getTex(name);
    if (!t) return false;
    w = t->w;
    h = t->h;
    return true;
}

int PadView::hitTest(ImVec2 mousePos, ImVec2 origin) const {
    const PadLayout& L = m_layout;
    // Iterate in reverse so the topmost-drawn component (highest index) is tested first.
    for (int i = (int)L.components.size() - 1; i >= 0; --i) {
        const PadComponent& c = L.components[i];
        float hw, hh;
        if (c.type == "stick" || c.type == "gyro") {
            hw = hh = c.size > 0.0f ? c.size * 0.5f : 20.0f;
        } else if (c.type == "dpad" || c.type == "analog_dpad") {
            float dpadScale = (c.size > 0.0f) ? c.size : 1.0f;
            hw = hh = 40.0f * dpadScale;
        } else {
            hw = c.w > 0.0f ? c.w * 0.5f : 20.0f;
            hh = c.h > 0.0f ? c.h * 0.5f : 20.0f;
        }
        float sx = origin.x + c.cx;
        float sy = origin.y + c.cy;
        if (mousePos.x >= sx - hw && mousePos.x <= sx + hw &&
            mousePos.y >= sy - hh && mousePos.y <= sy + hh)
            return i;
    }
    return -1;
}

void PadView::render(const GamepadState& state, int selectedComp) {
    if (!m_loaded) {
        ImGui::TextDisabled("%s", tr("padview.assets_not_loaded"));
        return;
    }

    const PadLayout& L  = m_layout;
    ImVec2      origin  = ImGui::GetCursorScreenPos();
    ImDrawList* dl      = ImGui::GetWindowDrawList();

    // Draw a texture centered at (cx, cy).
    auto img = [&](const PadTexture& t,
                   float cx, float cy, float w, float h,
                   ImVec4 tint) {
        if (!t.valid()) return;
        ImVec2 p0 = { origin.x + cx - w * 0.5f, origin.y + cy - h * 0.5f };
        ImVec2 p1 = { p0.x + w, p0.y + h };
        dl->AddImage((ImTextureID)(intptr_t)t.srv, p0, p1,
                     { 0, 0 }, { 1, 1 }, ImGui::ColorConvertFloat4ToU32(tint));
    };

    // Draw a texture centered at (cx, cy) rotated by angleRad (clockwise, 0 = unrotated) —
    // used by the gyro widget's yaw hand, the only element that needs to orbit rather than
    // sit axis-aligned. ImGui has no rotated AddImage, so this rotates the 4 destination
    // corners manually and submits them as a quad.
    auto imgRotated = [&](const PadTexture& t,
                          float cx, float cy, float w, float h,
                          float angleRad, ImVec4 tint) {
        if (!t.valid()) return;
        float hw = w * 0.5f, hh = h * 0.5f;
        float s = sinf(angleRad), c = cosf(angleRad);
        ImVec2 center = { origin.x + cx, origin.y + cy };
        auto rot = [&](float lx, float ly) {
            return ImVec2{ center.x + lx * c - ly * s, center.y + lx * s + ly * c };
        };
        ImVec2 p0 = rot(-hw, -hh), p1 = rot(hw, -hh), p2 = rot(hw, hh), p3 = rot(-hw, hh);
        ImU32 col32 = ImGui::ColorConvertFloat4ToU32(tint);
        dl->AddImageQuad((ImTextureID)(intptr_t)t.srv, p0, p1, p2, p3,
                          { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, col32);
    };

    for (const auto& c : L.components) {
        const ImVec4 col         = { c.colorR,         c.colorG,         c.colorB,         c.colorA         };
        const ImVec4 activeCol   = { c.activeColorR,   c.activeColorG,   c.activeColorB,   c.activeColorA   };
        const ImVec4 ovCol       = { c.ovColorR,       c.ovColorG,       c.ovColorB,       c.ovColorA       };
        const ImVec4 activeOvCol = { c.activeOvColorR, c.activeOvColorG, c.activeOvColorB, c.activeOvColorA };

        if (c.type == "template") {
            const PadTexture* t = getTex(c.image);
            if (t) img(*t, c.cx, c.cy, c.w, c.h, col);
        }
        else if (c.type == "button") {
            bool pressed = resolveState(state, c.state, c.threshold);
            const PadTexture* t  = getTex(c.image);
            const PadTexture* ov = getTex(c.overlay);
            if (t)  img(*t,  c.cx, c.cy, c.w, c.h,
                        pressed ? activeCol : col);
            if (ov) img(*ov, c.cx, c.cy, c.w * c.overlayScaleX, c.h * c.overlayScaleY,
                        pressed ? activeOvCol : ovCol);
        }
        else if (c.type == "stick") {
            float dx    = resolveFloat(state, c.stateX);
            float dy    = resolveFloat(state, c.stateY);
            bool  click = resolveState(state, c.stateClick, 0.5f);
            const PadTexture* t = getTex(c.image);
            if (t) img(*t, c.cx, c.cy, c.size, c.size, click ? activeCol : col);
            float hx = c.cx + dx * c.maxOffset;
            float hy = c.cy - dy * c.maxOffset;
            ImVec2 hc = { origin.x + hx, origin.y + hy };
            // Dot color: brightened inactive color when released, active color when clicked
            ImVec4 dotCol = click ? activeCol
                                  : ImVec4{ c.colorR + 0.35f, c.colorG + 0.35f, c.colorB + 0.35f, 1.0f };
            dl->AddCircleFilled(hc, 5.5f, ImGui::ColorConvertFloat4ToU32(dotCol));
            dl->AddCircle(hc, 5.5f, ImGui::ColorConvertFloat4ToU32({ 0.15f, 0.15f, 0.15f, 1.0f }), 12, 1.5f);
        }
        else if (c.type == "decoration") {
            // Non-interactive visual element (USB port, logo, label, etc.).
            // Uses natural image size if w/h are not specified in the layout.
            const PadTexture* t = getTex(c.image);
            if (t) {
                float dw = c.w > 0.0f ? c.w : (float)t->w;
                float dh = c.h > 0.0f ? c.h : (float)t->h;
                img(*t, c.cx, c.cy, dw, dh, col);
            }
        }
        else if (c.type == "touchpad") {
            // Background image (the physical touchpad surface)
            bool clicked = state.btnTouch;
            const PadTexture* t = getTex(c.image);
            if (t) img(*t, c.cx, c.cy, c.w, c.h, clicked ? activeCol : col);

            // Finger dots — drawn in touchpad-local coordinates
            float padL = origin.x + c.cx - c.w * 0.5f;
            float padT = origin.y + c.cy - c.h * 0.5f;
            constexpr float kDotR = 7.0f;

            auto drawFinger = [&](float normX, float normY, ImU32 fillColor) {
                ImVec2 pos = { padL + normX * c.w, padT + normY * c.h };
                dl->AddCircleFilled(pos, kDotR, fillColor);
                dl->AddCircle(pos, kDotR, IM_COL32(255, 255, 255, 200), 16, 1.5f);
            };
            if (state.touch1Active)
                drawFinger(state.touch1X, state.touch1Y, IM_COL32(80, 180, 255, 220));  // azul
            if (state.touch2Active)
                drawFinger(state.touch2X, state.touch2Y, IM_COL32(255, 140, 60, 220));  // naranja
        }
        else if (c.type == "gyro") {
            // Gyroscope widget: sphere background + tilt ball (roll/pitch) + yaw clock-hand bar
            // + 6 direction arrows (4 cardinal for roll/pitch, 2 rotational for yaw). See
            // REFERENCE.md, "Widget visual del giroscopio - spec UI completa".
            // stateX/stateY/stateZ map to roll/pitch/yaw (gyroZ/gyroX/gyroY by default).
            float dx = resolveFloat(state, c.stateX);
            float dy = resolveFloat(state, c.stateY);
            float dz = resolveFloat(state, c.stateZ);
            float r   = c.size > 0.0f ? c.size * 0.5f : 35.0f;
            float off = c.maxOffset > 0.0f ? c.maxOffset : r * 0.65f;

            ImVec2 center = { origin.x + c.cx, origin.y + c.cy };

            // Fixed tints, independent of the component's configured color — the gyro widget
            // renders with fixed colors by design (see LayoutEditor.cpp, "gyro renders with
            // fixed colors, nothing to edit"). Alpha (not color) carries "dim vs lit".
            const ImVec4 gyroDim    = { 1.0f, 1.0f, 1.0f, 0.65f };
            const ImVec4 gyroLit    = { 1.0f, 0.95f, 0.65f, 1.0f };
            // Sphere drawn first (furthest back) already — light blue (brighter than the small
            // ball's dark rest color) so it stands out without competing with the arrows on top.
            const ImVec4 gyroSphere = { 0.55f, 0.75f, 1.0f, 0.55f };
            constexpr float kGyroDeadzone = 0.12f;

            // "Calm blue -> alert red" ramp shared by the tilt ball and the yaw bar, driven by
            // movement magnitude (0 = at rest, 1 = full deflection).
            auto gyroIntensityColor = [](float intensity) {
                float blend = intensity * 1.4f < 1.0f ? intensity * 1.4f : 1.0f;
                return ImVec4{
                    (50.0f  + blend * 100.0f) / 255.0f,
                    (95.0f  - blend * 55.0f)  / 255.0f,
                    (150.0f - blend * 105.0f) / 255.0f,
                    1.0f
                };
            };

            // The sphere and the 6 arrows are all authored on the same canvas — each arrow PNG
            // already has its own arrow positioned pointing outward relative to that shared
            // frame — so they draw correctly by stacking at native pixel size, all centered on
            // the component's position. Only the yaw bar is meant to move (see below).
            auto drawGyroNative = [&](const PadTexture& t, ImVec4 tint) {
                img(t, c.cx, c.cy, (float)t.w, (float)t.h, tint);
            };

            drawGyroNative(m_gyroSphere, gyroSphere);
            drawGyroNative(m_gyroArrowN,   dy >  kGyroDeadzone ? gyroLit : gyroDim);
            drawGyroNative(m_gyroArrowS,   dy < -kGyroDeadzone ? gyroLit : gyroDim);
            drawGyroNative(m_gyroArrowE,   dx >  kGyroDeadzone ? gyroLit : gyroDim);
            drawGyroNative(m_gyroArrowW,   dx < -kGyroDeadzone ? gyroLit : gyroDim);
            drawGyroNative(m_gyroArrowCW,  dz >  kGyroDeadzone ? gyroLit : gyroDim);
            drawGyroNative(m_gyroArrowCCW, dz < -kGyroDeadzone ? gyroLit : gyroDim);

            // Yaw bar: same center as everything else, but rotates in place (clock-hand style)
            // instead of staying static — angle mapped from dz (-1..1), 0 = "12 o'clock" / no
            // rotation, clockwise = positive per the wizard's invert convention.
            constexpr float kYawBarMaxAngle = 1.4f; // ~80 deg, radians
            float barAngle = std::clamp(dz, -1.0f, 1.0f) * kYawBarMaxAngle;
            ImVec4 gyroBarTint = gyroIntensityColor(fabsf(dz));
            gyroBarTint.w = 0.90f;
            imgRotated(m_gyroLevelBar, c.cx, c.cy,
                       (float)m_gyroLevelBar.w, (float)m_gyroLevelBar.h, barAngle, gyroBarTint);

            // Tilt ball — drawn last so it sits above the sphere/arrows.
            if (state.gyroActive) {
                float bx = center.x + dx * off;
                float by = center.y - dy * off;  // screen Y is down; positive pitch moves ball up

                // Clamp ball inside cage
                float distSq = (bx - center.x) * (bx - center.x) + (by - center.y) * (by - center.y);
                float maxR   = r - 9.0f;
                if (distSq > maxR * maxR) {
                    float dist = sqrtf(distSq);
                    bx = center.x + (bx - center.x) / dist * maxR;
                    by = center.y + (by - center.y) / dist * maxR;
                }

                // Ball color: dark blue at rest, dark red at strong tilt
                ImVec4 ballColor = gyroIntensityColor(sqrtf(dx * dx + dy * dy));
                ImU32  ballFill  = ImGui::ColorConvertFloat4ToU32(
                    { ballColor.x, ballColor.y, ballColor.z, 230.0f / 255.0f });

                dl->AddCircleFilled({ bx + 1.5f, by + 2.5f }, 9.0f, IM_COL32(0, 0, 0, 90));   // shadow
                dl->AddCircleFilled({ bx, by }, 9.0f, ballFill, 24);                            // ball
                dl->AddCircleFilled({ bx - 3.0f, by - 3.0f }, 3.0f, IM_COL32(255, 255, 255, 100), 12); // specular
                dl->AddCircle({ bx, by }, 9.0f, IM_COL32(200, 210, 230, 160), 24, 1.0f);       // rim
                dl->AddCircleFilled(center, 2.5f, IM_COL32(90, 100, 120, 180));                 // center pip
            } else {
                dl->AddCircleFilled(center, 3.0f, IM_COL32(70, 70, 80, 180));
            }
        }
        else if (c.type == "dpad") {
            // Each arm is offset from the dpad center and scaled uniformly.
            // c.size is the scale factor (0 or 1 = natural texture size).
            float dpadScale = (c.size > 0.0f) ? c.size : 1.0f;
            auto drawArm = [&](const std::string& imgName, const std::string& stName,
                               float acx, float acy) {
                const PadTexture* t = getTex(imgName);
                if (!t) return;
                bool pressed = resolveState(state, stName, 0.5f);
                img(*t, acx, acy, (float)t->w * dpadScale, (float)t->h * dpadScale,
                    pressed ? activeCol : col);
            };
            const PadTexture* tUp    = getTex(c.imageUp);
            const PadTexture* tDown  = getTex(c.imageDown);
            const PadTexture* tLeft  = getTex(c.imageLeft);
            const PadTexture* tRight = getTex(c.imageRight);
            if (tUp)    drawArm(c.imageUp,    c.stateUp,
                                c.cx, c.cy - tUp->h    * dpadScale * 0.5f + 2.0f * dpadScale);
            if (tDown)  drawArm(c.imageDown,  c.stateDown,
                                c.cx, c.cy + tDown->h  * dpadScale * 0.5f);
            if (tLeft)  drawArm(c.imageLeft,  c.stateLeft,
                                c.cx - tLeft->w  * dpadScale * 0.5f, c.cy);
            if (tRight) drawArm(c.imageRight, c.stateRight,
                                c.cx + tRight->w * dpadScale * 0.5f, c.cy);
        }
        else if (c.type == "analog_dpad") {
            // Like dpad visually, but driven by float axes (stateX/stateY) instead of bool states.
            // Joystick convention (same as stick): positive Y = up, negative Y = down.
            // The wizard calibrates with invert_if_positive:true so this always holds after setup.
            float dx = resolveFloat(state, c.stateX);
            float dy = resolveFloat(state, c.stateY);
            float dpadScale = (c.size > 0.0f) ? c.size : 1.0f;
            float thr = (c.threshold > 0.0f) ? c.threshold : 0.5f;
            auto drawArm = [&](const std::string& imgName, bool active,
                               float acx, float acy) {
                const PadTexture* t = getTex(imgName);
                if (!t) return;
                img(*t, acx, acy, (float)t->w * dpadScale, (float)t->h * dpadScale,
                    active ? activeCol : col);
            };
            const PadTexture* tUp    = getTex(c.imageUp);
            const PadTexture* tDown  = getTex(c.imageDown);
            const PadTexture* tLeft  = getTex(c.imageLeft);
            const PadTexture* tRight = getTex(c.imageRight);
            if (tUp)    drawArm(c.imageUp,    dy > thr,
                                c.cx, c.cy - tUp->h    * dpadScale * 0.5f + 2.0f * dpadScale);
            if (tDown)  drawArm(c.imageDown,  dy < -thr,
                                c.cx, c.cy + tDown->h  * dpadScale * 0.5f);
            if (tLeft)  drawArm(c.imageLeft,  dx < -thr,
                                c.cx - tLeft->w  * dpadScale * 0.5f, c.cy);
            if (tRight) drawArm(c.imageRight, dx > thr,
                                c.cx + tRight->w * dpadScale * 0.5f, c.cy);
        }
    }

    // Selection highlight (editor use)
    if (selectedComp >= 0 && selectedComp < (int)L.components.size()) {
        const PadComponent& c = L.components[selectedComp];
        float hw, hh;
        if (c.type == "stick" || c.type == "gyro") {
            hw = hh = c.size > 0.0f ? c.size * 0.5f : 20.0f;
        } else if (c.type == "dpad" || c.type == "analog_dpad") {
            float dpadScale = (c.size > 0.0f) ? c.size : 1.0f;
            hw = hh = 40.0f * dpadScale;
        } else {
            hw = c.w > 0.0f ? c.w * 0.5f : 20.0f;
            hh = c.h > 0.0f ? c.h * 0.5f : 20.0f;
        }
        ImVec2 p0 = { origin.x + c.cx - hw, origin.y + c.cy - hh };
        ImVec2 p1 = { origin.x + c.cx + hw, origin.y + c.cy + hh };
        dl->AddRect(p0, p1, IM_COL32(255, 220, 0, 220), 2.0f, 0, 2.0f);
        // Corner handles
        constexpr float hSz = 4.0f;
        auto corner = [&](float x, float y) {
            dl->AddRectFilled({ x - hSz, y - hSz }, { x + hSz, y + hSz },
                              IM_COL32(255, 220, 0, 255));
        };
        corner(p0.x, p0.y); corner(p1.x, p0.y);
        corner(p0.x, p1.y); corner(p1.x, p1.y);
    }

    // Advance ImGui layout cursor past the drawn area.
    ImGui::Dummy({ L.W, L.FrontH + L.TopH });
}

// ---------------------------------------------------------------------------
// renderStickArrows / hitTestStickArrow
// ---------------------------------------------------------------------------

// Distance constants shared by render and hit-test.
static constexpr float kArrowRenderSz = 16.0f;  // draw size (px)
static constexpr float kArrowGap      = 5.0f;   // gap between stick edge and arrow center
static constexpr float kArrowHitSz    = 11.0f;  // hit half-size (slightly larger than render)

// Returns the screen-space center of a directional arrow for a stick at (cx,cy).
static ImVec2 arrowCenter(float cx, float cy, float stickR, const char* dir) {
    float dist = stickR + kArrowGap + kArrowRenderSz * 0.5f;
    if      (dir[0] == 'u') return { cx,        cy - dist };  // up
    else if (dir[0] == 'd') return { cx,        cy + dist };  // down
    else if (dir[0] == 'l') return { cx - dist, cy        };  // left
    else                    return { cx + dist, cy        };  // right
}

void PadView::renderStickArrows(ImVec2 canvasOrigin, int selectedComp, const std::string& selDir) {
    if (!m_loaded) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const PadTexture* arrowTex[4] = { &m_arrowUp, &m_arrowDown, &m_arrowLeft, &m_arrowRight };
    const char*       dirs[4]     = { "up", "down", "left", "right" };

    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const PadComponent& c = m_layout.components[i];
        if (c.type != "stick") continue;

        float stickR = c.size > 0.0f ? c.size * 0.5f : 20.0f;
        float cx     = canvasOrigin.x + c.cx;
        float cy     = canvasOrigin.y + c.cy;
        bool  isSel  = (i == selectedComp);

        for (int d = 0; d < 4; ++d) {
            if (!arrowTex[d]->valid()) continue;
            bool dirSel = isSel && (selDir == dirs[d]);
            float alpha = dirSel ? 1.0f : 0.40f;
            ImVec4 tint = dirSel ? ImVec4{ 1.0f, 0.88f, 0.15f, alpha }
                                 : ImVec4{ 1.0f, 1.0f,  1.0f,  alpha };
            ImVec2 ac = arrowCenter(cx, cy, stickR, dirs[d]);
            ImVec2 p0 = { ac.x - kArrowRenderSz * 0.5f, ac.y - kArrowRenderSz * 0.5f };
            ImVec2 p1 = { p0.x + kArrowRenderSz,        p0.y + kArrowRenderSz        };
            dl->AddImage((ImTextureID)(intptr_t)arrowTex[d]->srv, p0, p1,
                         { 0, 0 }, { 1, 1 }, ImGui::ColorConvertFloat4ToU32(tint));
        }
    }
}

void PadView::renderTouchpadHints(ImVec2 canvasOrigin, int selectedComp, bool surfaceSelected) {
    if (!m_loaded) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto drawIcon = [&](const PadTexture& t, float cx, float cy, float sz, bool bright) {
        if (!t.valid()) return;
        float alpha = bright ? 1.0f : 0.40f;   // same dim level as renderStickArrows
        ImVec2 p0 = { cx - sz * 0.5f, cy - sz * 0.5f };
        ImVec2 p1 = { p0.x + sz,      p0.y + sz };
        dl->AddImage((ImTextureID)(intptr_t)t.srv, p0, p1,
                     { 0, 0 }, { 1, 1 }, ImGui::ColorConvertFloat4ToU32({ 1.0f, 1.0f, 1.0f, alpha }));
    };

    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const PadComponent& c = m_layout.components[i];
        if (c.type != "touchpad") continue;

        bool isSel = (i == selectedComp);
        float cx   = canvasOrigin.x + c.cx;
        float cy   = canvasOrigin.y + c.cy;
        float sz   = c.h * 0.6f;
        drawIcon(m_touchSurfaceIcon, cx - c.w * 0.25f, cy, sz, isSel && surfaceSelected);
        drawIcon(m_touchButtonIcon,  cx + c.w * 0.25f, cy, sz, isSel && !surfaceSelected);
    }
}

void PadView::renderTouchZoneOverlay(ImVec2 canvasOrigin, const std::vector<TouchZoneRegion>& zones,
                                      const std::string& selectedRegionId,
                                      const std::string& hoveredRegionId) {
    if (!m_loaded || zones.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const PadComponent& c = m_layout.components[i];
        if (c.type != "touchpad") continue;

        // Same padL/padT/c.w/c.h mapping the finger dots use (render()'s "touchpad" branch) —
        // normalized [0,1] surface coords -> screen pixels.
        float padL = canvasOrigin.x + c.cx - c.w * 0.5f;
        float padT = canvasOrigin.y + c.cy - c.h * 0.5f;
        auto toScreen = [&](float normX, float normY) -> ImVec2 {
            return { padL + normX * c.w, padT + normY * c.h };
        };
        // Wedge regions have no radius of their own (see TouchZones.h) — approximate their spokes
        // reaching most of the way to the pad's edge along their angle, close enough to read as
        // pie slices at this scale without needing exact rect-boundary intersection math.
        const float kWedgeReach = 0.9f;
        ImVec2 center = toScreen(0.5f, 0.5f);

        for (const auto& r : zones) {
            bool isSel     = (r.id == selectedRegionId);
            bool isHovered = (r.id == hoveredRegionId);
            float alpha    = isSel ? 0.95f : (isHovered ? 0.75f : 0.35f);
            ImU32 lineCol  = ImGui::ColorConvertFloat4ToU32({ 0.4f, 0.85f, 1.0f, alpha });
            ImU32 fillCol  = ImGui::ColorConvertFloat4ToU32({ 0.4f, 0.85f, 1.0f, isSel ? 0.28f : (isHovered ? 0.16f : 0.0f) });
            float thickness = isSel ? 2.5f : 1.5f;

            switch (r.shape) {
                case TouchZoneShape::Rect: {
                    ImVec2 p0 = toScreen(r.xMin, r.yMin);
                    ImVec2 p1 = toScreen(r.xMax, r.yMax);
                    if (fillCol & IM_COL32_A_MASK) dl->AddRectFilled(p0, p1, fillCol);
                    dl->AddRect(p0, p1, lineCol, 0.0f, 0, thickness);
                    break;
                }
                case TouchZoneShape::Wedge: {
                    float radFrom = r.angleFromDeg * (3.14159265358979323846f / 180.0f);
                    float radTo   = r.angleToDeg   * (3.14159265358979323846f / 180.0f);
                    ImVec2 edgeFrom = { center.x + std::cos(radFrom) * c.w * 0.5f * kWedgeReach,
                                        center.y + std::sin(radFrom) * c.h * 0.5f * kWedgeReach };
                    ImVec2 edgeTo   = { center.x + std::cos(radTo)   * c.w * 0.5f * kWedgeReach,
                                        center.y + std::sin(radTo)   * c.h * 0.5f * kWedgeReach };
                    if (fillCol & IM_COL32_A_MASK)
                        dl->AddTriangleFilled(center, edgeFrom, edgeTo, fillCol);
                    dl->AddLine(center, edgeFrom, lineCol, thickness);
                    dl->AddLine(center, edgeTo,   lineCol, thickness);
                    break;
                }
                case TouchZoneShape::Circle: {
                    float rx = r.radius * c.w, ry = r.radius * c.h;
                    constexpr int kSegs = 32;
                    ImVec2 pts[kSegs];
                    for (int s = 0; s < kSegs; ++s) {
                        float t = (2.0f * 3.14159265358979323846f) * (float)s / (float)kSegs;
                        pts[s] = { center.x + std::cos(t) * rx, center.y + std::sin(t) * ry };
                    }
                    if (fillCol & IM_COL32_A_MASK)
                        dl->AddConvexPolyFilled(pts, kSegs, fillCol);
                    dl->AddPolyline(pts, kSegs, lineCol, ImDrawFlags_Closed, thickness);
                    break;
                }
            }
        }
    }
}

int PadView::hitTestZoneRegion(ImVec2 mousePos, ImVec2 canvasOrigin,
                                const std::vector<TouchZoneRegion>& zones,
                                std::string& outRegionId) const {
    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const PadComponent& c = m_layout.components[i];
        if (c.type != "touchpad") continue;

        float padL = canvasOrigin.x + c.cx - c.w * 0.5f;
        float padT = canvasOrigin.y + c.cy - c.h * 0.5f;
        if (mousePos.x < padL || mousePos.x > padL + c.w ||
            mousePos.y < padT || mousePos.y > padT + c.h)
            continue;

        float normX = (mousePos.x - padL) / c.w;
        float normY = (mousePos.y - padT) / c.h;
        const TouchZoneRegion* r = hitTestTouchZone(zones, normX, normY);
        if (!r) continue;
        outRegionId = r->id;
        return i;
    }
    return -1;
}

int PadView::hitTestStickArrow(ImVec2 mousePos, ImVec2 canvasOrigin, std::string& outDir) const {
    const char* dirs[4] = { "up", "down", "left", "right" };

    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const PadComponent& c = m_layout.components[i];
        if (c.type != "stick") continue;

        float stickR = c.size > 0.0f ? c.size * 0.5f : 20.0f;
        float cx     = canvasOrigin.x + c.cx;
        float cy     = canvasOrigin.y + c.cy;

        for (int d = 0; d < 4; ++d) {
            ImVec2 ac = arrowCenter(cx, cy, stickR, dirs[d]);
            if (mousePos.x >= ac.x - kArrowHitSz && mousePos.x <= ac.x + kArrowHitSz &&
                mousePos.y >= ac.y - kArrowHitSz && mousePos.y <= ac.y + kArrowHitSz) {
                outDir = dirs[d];
                return i;
            }
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// renderGyroArrows / hitTestGyroArrow
// ---------------------------------------------------------------------------
// The gyro widget's own N/S/E/W/CW/CCW arrows (drawn natively, see render()'s "gyro" branch) sit
// near the rim of the sphere, all stacked at the same center as the widget. Confirmed against two
// real screenshots (the second with the debug zone colors on): the straight N/S/E/W arrows sit
// well INSIDE the rim (not at it), while the curved CW/CCW arrows sweep the TOP arc, right at the
// rim, further out than the straight ones. Mirrored: the "turn right" (cw) curve sits on the LEFT
// of the widget, "turn left" (ccw) on the RIGHT.
static constexpr float kGyroCardinalRadiusFactor = 0.55f; // straight arrows: well inside the rim
static constexpr float kGyroCardinalExtraPx      = 9.0f;  // + nudge further out, per user feedback
static constexpr float kGyroRotRadiusFactor      = 1.0f;  // curved arrows: at the rim
static constexpr float kGyroRotAngleMinDeg       = 15.0f; // sweep start, degrees off straight-up (N)
static constexpr float kGyroRotAngleMaxDeg       = 75.0f; // sweep end, degrees off straight-up (N)
static constexpr int   kGyroRotSamples           = 4;     // hit squares per half (top/bottom mirror)
static constexpr float kGyroCwOffsetPx           = -18.0f; // cw (left curve) nudged further left
static constexpr float kGyroCcwOffsetPx          =  19.0f; // ccw (right curve) nudged further right

static ImVec2 gyroCardinalCenter(float cx, float cy, float r, const char* dir) {
    float dist = r * kGyroCardinalRadiusFactor + kGyroCardinalExtraPx;
    if      (dir[0] == 'u') return { cx,        cy - dist };  // up (pitch+)
    else if (dir[0] == 'd') return { cx,        cy + dist };  // down (pitch-)
    else if (dir[0] == 'l') return { cx - dist, cy        };  // left (roll-)
    else                    return { cx + dist, cy        };  // right (roll+)
}

// One point on the cw/ccw arc. t in [0,1] sweeps from straight-up (N, t=0) toward the side
// (E for ccw / W for cw, t=1); mirrorBottom reflects that same sweep onto the bottom half, to
// cover the rest of the curve as it wraps down the side. cw -> left half (+ a fixed leftward
// px nudge), ccw -> right half (+ a fixed rightward px nudge) — mirrored glyph convention.
static ImVec2 gyroRotCenterAt(float cx, float cy, float r, bool cw, float t, bool mirrorBottom) {
    float angleDeg = kGyroRotAngleMinDeg + t * (kGyroRotAngleMaxDeg - kGyroRotAngleMinDeg);
    float rad  = angleDeg * 3.14159265f / 180.0f;
    float dist = r * kGyroRotRadiusFactor;
    float sx   = cw ? -sinf(rad) : sinf(rad);
    float sy   = -cosf(rad);
    if (mirrorBottom) sy = -sy;
    float offsetPx = cw ? kGyroCwOffsetPx : kGyroCcwOffsetPx;
    return { cx + sx * dist + offsetPx, cy + sy * dist };
}

void PadView::renderGyroArrows(ImVec2 canvasOrigin, int selectedComp, const std::string& selDir) {
    if (!m_loaded || selectedComp < 0 || selDir.empty()) return;
    if (selectedComp >= (int)m_layout.components.size()) return;
    const PadComponent& c = m_layout.components[selectedComp];
    if (c.type != "gyro") return;

    // Redraw the same native arrow texture the gyro widget itself draws for this direction, on
    // top, with an "active" (selected) tint — same idea as a stick component turning active-color
    // when clicked, not a separate marker shape. Native size/position matches render()'s "gyro"
    // branch exactly, so this lines up with the visible arrowhead regardless of where within the
    // texture it's drawn.
    const PadTexture* tex = nullptr;
    if      (selDir == "up")    tex = &m_gyroArrowN;
    else if (selDir == "down")  tex = &m_gyroArrowS;
    else if (selDir == "right") tex = &m_gyroArrowE;
    else if (selDir == "left")  tex = &m_gyroArrowW;
    else if (selDir == "cw")    tex = &m_gyroArrowCW;
    else if (selDir == "ccw")   tex = &m_gyroArrowCCW;
    if (!tex || !tex->valid()) return;

    float cx = canvasOrigin.x + c.cx;
    float cy = canvasOrigin.y + c.cy;
    ImVec2 p0 = { cx - tex->w * 0.5f, cy - tex->h * 0.5f };
    ImVec2 p1 = { p0.x + tex->w, p0.y + tex->h };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImage((ImTextureID)(intptr_t)tex->srv, p0, p1, { 0, 0 }, { 1, 1 },
                 ImGui::ColorConvertFloat4ToU32({ 1.0f, 0.88f, 0.15f, 1.0f }));
}

int PadView::hitTestGyroArrow(ImVec2 mousePos, ImVec2 canvasOrigin, std::string& outDir) const {
    const char* dirs[4] = { "up", "down", "left", "right" };

    for (int i = 0; i < (int)m_layout.components.size(); ++i) {
        const PadComponent& c = m_layout.components[i];
        if (c.type != "gyro") continue;

        float r  = c.size > 0.0f ? c.size * 0.5f : 35.0f;
        float cx = canvasOrigin.x + c.cx;
        float cy = canvasOrigin.y + c.cy;

        for (int d = 0; d < 4; ++d) {
            ImVec2 ac = gyroCardinalCenter(cx, cy, r, dirs[d]);
            if (mousePos.x >= ac.x - kArrowHitSz && mousePos.x <= ac.x + kArrowHitSz &&
                mousePos.y >= ac.y - kArrowHitSz && mousePos.y <= ac.y + kArrowHitSz) {
                outDir = dirs[d];
                return i;
            }
        }

        const char* rotDirs[2] = { "cw", "ccw" };
        for (int d = 0; d < 2; ++d) {
            for (int mirror = 0; mirror < 2; ++mirror) {
                for (int s = 0; s < kGyroRotSamples; ++s) {
                    float t  = (kGyroRotSamples > 1) ? (float)s / (float)(kGyroRotSamples - 1) : 0.0f;
                    ImVec2 rc = gyroRotCenterAt(cx, cy, r, d == 0, t, mirror == 1);
                    if (mousePos.x >= rc.x - kArrowHitSz && mousePos.x <= rc.x + kArrowHitSz &&
                        mousePos.y >= rc.y - kArrowHitSz && mousePos.y <= rc.y + kArrowHitSz) {
                        outDir = rotDirs[d];
                        return i;
                    }
                }
            }
        }
    }
    return -1;
}
