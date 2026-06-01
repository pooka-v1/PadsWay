#include "PadView.h"
#include "../config/Strings.h"

#include <wincodec.h>
#include <unordered_set>
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
// load / setLayout / unload
// ---------------------------------------------------------------------------

bool PadView::load(ID3D11Device* device) {
    m_device = device;
    m_loaded = true;
    loadPng(device, "images/decorations/ArrowUp.png",    m_arrowUp);
    loadPng(device, "images/decorations/ArrowDown.png",  m_arrowDown);
    loadPng(device, "images/decorations/ArrowLeft.png",  m_arrowLeft);
    loadPng(device, "images/decorations/ArrowRight.png", m_arrowRight);
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
            // Gyroscope visualisation: bubble-level ball inside a circular cage.
            // stateX maps to horizontal axis (gyroZ = roll), stateY to vertical (gyroX = pitch).
            float dx = resolveFloat(state, c.stateX);
            float dy = resolveFloat(state, c.stateY);
            float r   = c.size > 0.0f ? c.size * 0.5f : 35.0f;
            float off = c.maxOffset > 0.0f ? c.maxOffset : r * 0.65f;

            ImVec2 center = { origin.x + c.cx, origin.y + c.cy };

            // Cage background
            ImU32 bgCol  = state.gyroActive ? IM_COL32(30, 30, 40, 210) : IM_COL32(20, 20, 25, 130);
            ImU32 rimCol = state.gyroActive ? IM_COL32(110, 130, 160, 200) : IM_COL32(70, 70, 80, 140);
            dl->AddCircleFilled(center, r, bgCol, 48);
            dl->AddCircle(center, r, rimCol, 48, 2.0f);

            // Reference cross and inner ring
            ImU32 crossCol = IM_COL32(60, 70, 90, 160);
            dl->AddLine({ center.x - r * 0.88f, center.y }, { center.x + r * 0.88f, center.y }, crossCol, 1.0f);
            dl->AddLine({ center.x, center.y - r * 0.88f }, { center.x, center.y + r * 0.88f }, crossCol, 1.0f);
            dl->AddCircle(center, r * 0.45f, IM_COL32(55, 65, 85, 120), 32, 1.0f);

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

                // Ball color: blue at rest, orange at strong tilt
                float intensity = sqrtf(dx * dx + dy * dy);
                float blend = intensity * 1.4f < 1.0f ? intensity * 1.4f : 1.0f;
                ImU32 ballFill = IM_COL32(
                    (uint8_t)(80  + (int)(blend * 175)),
                    (uint8_t)(160 - (int)(blend * 100)),
                    (uint8_t)(255 - (int)(blend * 175)),
                    230);

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
