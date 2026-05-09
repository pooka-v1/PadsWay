#pragma once
#include <d3d11.h>
#include <unordered_map>
#include <string>
#include "../GamepadState.h"
#include "PadLayout.h"
#include "../imgui/imgui.h"

// RAII wrapper for a single D3D11 shader resource view loaded from a PNG.
struct PadTexture {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;

    bool valid() const { return srv != nullptr; }
    void release() { if (srv) { srv->Release(); srv = nullptr; } w = h = 0; }

    PadTexture() = default;
    PadTexture(const PadTexture&)            = delete;
    PadTexture& operator=(const PadTexture&) = delete;
    PadTexture(PadTexture&& o) noexcept
        : srv(o.srv), w(o.w), h(o.h) { o.srv = nullptr; o.w = o.h = 0; }
    PadTexture& operator=(PadTexture&& o) noexcept {
        if (this != &o) { release(); srv = o.srv; w = o.w; h = o.h; o.srv = nullptr; o.w = o.h = 0; }
        return *this;
    }
};

// Renders a visual representation of the active gamepad state.
// Fully data-driven: the component list in PadLayout describes every element to draw.
class PadView {
public:
    // Initialise D3D device reference. Textures are loaded on the first setLayout() call.
    bool load(ID3D11Device* device);

    // Switch to a new layout: loads any new textures, releases unused ones.
    // Ignored if layout.id is unchanged.
    void setLayout(const PadLayout& layout);

    // Like setLayout but always applies the new layout regardless of the id.
    // Use when the component list has changed but the id is the same.
    void forceSetLayout(const PadLayout& layout);

    // Update component positions/properties without reloading textures.
    // Safe to call every frame during drag operations.
    void updateLayout(const PadLayout& layout);

    // Render the pad view inside the current ImGui window at the current cursor position.
    // selectedComp: index of component to draw a selection highlight on (-1 = none).
    void render(const GamepadState& state, int selectedComp = -1);

    // Returns the natural pixel size of a loaded texture, or false if not found.
    bool getTextureSize(const std::string& name, int& w, int& h) const;

    // Returns the index of the topmost component at the given screen position, or -1.
    // canvasOrigin must be the screen-space position of the canvas top-left corner,
    // as returned by ImGui::GetCursorScreenPos() before calling render().
    int hitTest(ImVec2 mousePos, ImVec2 canvasOrigin) const;

    // Returns the current layout (for component introspection after hitTest).
    const PadLayout& getLayout() const { return m_layout; }

    // Release all D3D11 resources. Call before releasing the D3D device.
    void unload();

    // Render directional arrows around every stick component.
    // canvasOrigin: screen-space top-left of the canvas (captured before render()).
    // selectedComp: stick component index whose arrows are shown selected (-1 = none).
    // selDir: which direction is highlighted ("up","down","left","right","" = none).
    void renderStickArrows(ImVec2 canvasOrigin, int selectedComp, const std::string& selDir);

    // Returns the stick component index if a click falls on one of its directional arrows,
    // and sets outDir to "up"/"down"/"left"/"right". Returns -1 if no arrow was hit.
    int hitTestStickArrow(ImVec2 mousePos, ImVec2 canvasOrigin, std::string& outDir) const;

    static bool loadPng(ID3D11Device* device, const char* path, PadTexture& out);

private:
    const PadTexture* getTex(const std::string& name) const;

    ID3D11Device* m_device = nullptr;
    bool          m_loaded = false;
    PadLayout     m_layout;

    std::unordered_map<std::string, PadTexture> m_textures;

    // Directional arrow overlays (loaded once in load(), used by renderStickArrows)
    PadTexture m_arrowUp, m_arrowDown, m_arrowLeft, m_arrowRight;
};
