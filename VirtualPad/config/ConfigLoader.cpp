#include "ConfigLoader.h"
#include "../nlohmann/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static ButtonAction parseButtonAction(const json& val) {
    ButtonAction action;

    if (val.is_string()) {
        // Formato corto: "b" → {physical:"b", virtual:"b"}
        action.type     = ButtonActionType::VirtualButton;
        action.name     = val.get<std::string>();
        action.physical = action.name;
        return action;
    }

    // Formato objeto — leer physical si está presente
    if (val.contains("physical"))
        action.physical = val["physical"].get<std::string>();

    // Determinar la acción
    if (val.contains("virtual")) {
        action.type = ButtonActionType::VirtualButton;
        action.name = val["virtual"].get<std::string>();
    } else if (val.contains("type")) {
        std::string type = val.at("type").get<std::string>();
        if (type == "bot") {
            action.type = ButtonActionType::Bot;
            action.name = val.at("name").get<std::string>();
        } else if (type == "macro") {
            action.type = ButtonActionType::Macro;
            action.name = val.at("name").get<std::string>();
            if (val.contains("execution"))
                action.execution = val["execution"].get<std::string>();
        } else if (type == "trigger") {
            action.type   = ButtonActionType::Trigger;
            action.target = val.at("target").get<std::string>();
            if (val.contains("axis")) action.axis = val["axis"].get<std::string>();
        } else if (type == "keyboard") {
            action.type = ButtonActionType::Keyboard;
            if (val.contains("keys"))
                for (const auto& k : val["keys"])
                    action.keys.push_back(k.get<std::string>());
        } else if (type == "mouse_click") {
            action.type        = ButtonActionType::MouseClick;
            action.mouseButton = val.value("button", "left");
        }
    }
    // else: solo physical, sin acción (type queda VirtualButton con name vacío)

    return action;
}

// Parses a buttons JSON object into a map.
// Keys starting with '_' are skipped (they are pseudo-comments).
static std::unordered_map<int, ButtonAction> parseButtonsJson(const json& buttonsJson) {
    std::unordered_map<int, ButtonAction> result;
    for (const auto& [key, val] : buttonsJson.items()) {
        if (!key.empty() && key[0] == '_') continue;
        result[std::stoi(key)] = parseButtonAction(val);
    }
    return result;
}

// ---------------------------------------------------------------------------

std::vector<ControllerConfig> loadControllerConfigs(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);

    json root = json::parse(f);
    std::vector<ControllerConfig> result;

    for (const auto& c : root.at("controllers")) {
        ControllerConfig cfg;
        cfg.vid          = static_cast<uint16_t>(std::stoul(c.at("vid").get<std::string>(), nullptr, 16));
        cfg.pid          = static_cast<uint16_t>(std::stoul(c.at("pid").get<std::string>(), nullptr, 16));
        cfg.source_name  = c.at("source_name").get<std::string>();
        cfg.mode         = c.at("mode").get<std::string>();
        cfg.dpad         = c.value("dpad", "");
        cfg.layout_id    = c.value("layout_id", "");
        cfg.connection   = c.value("connection", "");
        cfg.buttons     = parseButtonsJson(c.at("buttons"));

        for (const auto& [source, axisJson] : c.at("axes").items()) {
            AxisMapping m;
            m.target = axisJson.at("target").get<std::string>();
            m.invert = axisJson.value("invert", false);
            m.speed  = axisJson.value("speed",  15.0f);
            cfg.axes[source] = m;
        }

        if (c.contains("touchpad")) {
            const auto& tp        = c["touchpad"];
            cfg.touchpad.enabled      = tp.value("enabled",       false);
            cfg.touchpad.dataOffset   = tp.value("data_offset",   34);
            cfg.touchpad.maxX         = tp.value("max_x",         1919);
            cfg.touchpad.maxY         = tp.value("max_y",         942);
            cfg.touchpad.mouseEnabled = tp.value("mouse_enabled", false);
        }

        if (c.contains("imu")) {
            const auto& im     = c["imu"];
            cfg.imu.enabled    = im.value("enabled",     false);
            cfg.imu.gyroOffset = im.value("gyro_offset", 13);
            cfg.imu.gyroScale  = im.value("gyro_scale",  1.0f / 32768.0f);
        }

        result.push_back(std::move(cfg));
    }

    return result;
}

const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid,
                                   const std::string& connection,
                                   const std::string& sourceName) {
    const ControllerConfig* best      = nullptr;
    int                     bestScore = -1;

    for (const auto& c : configs) {
        if (c.vid != vid || c.pid != pid) continue;

        int score = 0;

        // connection: if entry declares it, incoming must match or we skip
        if (!c.connection.empty()) {
            if (connection.empty()) continue;
            if (c.connection != connection) continue;
            score += 2;
        }

        // source_name: only used when caller provides it (e.g. wizard re-pair)
        // Runtime engine always passes "" → source_name is ignored, entry stays eligible
        if (!sourceName.empty() && !c.source_name.empty()) {
            if (c.source_name != sourceName) continue; // explicit mismatch → skip
            score += 1;
        }

        if (score > bestScore) {
            bestScore = score;
            best      = &c;
        }
    }
    return best;
}

std::unordered_map<std::string, std::string> loadMacroLibrary(const std::string& path) {
    std::unordered_map<std::string, std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;  // optional file — no error if missing
    json root = json::parse(f);
    for (const auto& [name, val] : root.items())
        result[name] = val.get<std::string>();
    return result;
}

VirtualPadConfig loadVirtualPadConfig(const std::string& path) {
    VirtualPadConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;  // optional file — return defaults
    json root = json::parse(f);
    if (root.contains("virtual_vid"))
        cfg.vid = static_cast<uint16_t>(std::stoul(root["virtual_vid"].get<std::string>(), nullptr, 16));
    if (root.contains("virtual_pid"))
        cfg.pid = static_cast<uint16_t>(std::stoul(root["virtual_pid"].get<std::string>(), nullptr, 16));
    if (root.contains("log_level"))
        cfg.logLevel = root["log_level"].get<std::string>();
    return cfg;
}

GameProfile loadGameProfile(const std::string& path) {
    GameProfile profile;
    std::ifstream f(path);
    if (!f.is_open()) return profile;

    json root = json::parse(f);
    profile.profile_name = root.value("profile_name", "");

    for (const auto& ov : root.value("overrides", json::array())) {
        GameProfile::Override o;
        o.vid = static_cast<uint16_t>(std::stoul(ov.at("vid").get<std::string>(), nullptr, 16));
        o.pid = static_cast<uint16_t>(std::stoul(ov.at("pid").get<std::string>(), nullptr, 16));
        if (ov.contains("buttons"))
            o.buttons = parseButtonsJson(ov.at("buttons"));
        if (ov.contains("axes")) {
            for (const auto& [source, axisJson] : ov.at("axes").items()) {
                AxisMapping m;
                m.target = axisJson.at("target").get<std::string>();
                m.invert = axisJson.value("invert", false);
                m.speed  = axisJson.value("speed",  15.0f);
                o.axes[source] = m;
            }
        }
        profile.overrides.push_back(std::move(o));
    }
    return profile;
}

ControllerConfig applyProfile(const ControllerConfig& base, const GameProfile& profile) {
    ControllerConfig result = base;
    for (const auto& ov : profile.overrides) {
        if (ov.vid != base.vid || ov.pid != base.pid) continue;
        for (const auto& [bit, override_action] : ov.buttons) {
            // Preservar el physical del botón base aunque el perfil sobreescriba la acción
            std::string phys;
            auto it = result.buttons.find(bit);
            if (it != result.buttons.end())
                phys = it->second.physical;

            result.buttons[bit] = override_action;

            if (!phys.empty() && result.buttons[bit].physical.empty())
                result.buttons[bit].physical = phys;
        }
        for (const auto& [source, mapping] : ov.axes)
            result.axes[source] = mapping;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Pad layouts
// ---------------------------------------------------------------------------

static float jf(const json& obj, const char* key, float def) {
    return obj.contains(key) ? obj[key].get<float>() : def;
}

std::vector<PadLayout> loadPadLayouts(const std::string& path) {
    std::vector<PadLayout> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;

    json root = json::parse(f);

    auto parseColor4 = [](const json& obj, const char* key,
                          float& r, float& g, float& b, float& a) {
        if (!obj.contains(key) || !obj[key].is_array() || obj[key].size() < 3) return;
        const auto& arr = obj[key];
        r = arr[0].get<float>();
        g = arr[1].get<float>();
        b = arr[2].get<float>();
        a = arr.size() >= 4 ? arr[3].get<float>() : 1.0f;
    };

    for (const auto& j : root.value("layouts", json::array())) {
        PadLayout L;
        L.id = j.value("id", "");
        if (j.contains("canvas")) {
            L.W      = jf(j["canvas"], "W",      L.W);
            L.FrontH = jf(j["canvas"], "FrontH", L.FrontH);
            L.TopH   = jf(j["canvas"], "TopH",   L.TopH);
        }

        for (const auto& cj : j.value("components", json::array())) {
            if (!cj.contains("type")) continue;  // skip comment-only entries

            PadComponent c;
            c.id      = cj.value("id",      "");
            c.view    = cj.value("view",    "top");
            c.type    = cj.value("type",    "");
            c.image   = cj.value("image",   "");
            c.overlay = cj.value("overlay", "");
            c.cx      = jf(cj, "cx", 0.0f);
            c.cy      = jf(cj, "cy", 0.0f);
            c.w       = jf(cj, "w",  0.0f);
            c.h       = jf(cj, "h",  0.0f);
            c.size      = jf(cj, "size",       0.0f);
            c.maxOffset = jf(cj, "max_offset", 0.0f);
            c.threshold = jf(cj, "threshold",  0.05f);

            if (cj.contains("overlay_scale")) {
                const auto& os = cj["overlay_scale"];
                if (os.is_array() && os.size() >= 2) {
                    c.overlayScaleX = os[0].get<float>();
                    c.overlayScaleY = os[1].get<float>();
                } else if (os.is_number()) {
                    c.overlayScaleX = c.overlayScaleY = os.get<float>();
                }
            }

            c.state      = cj.value("state",        "");
            c.stateX     = cj.value("state_x",      "");
            c.stateY     = cj.value("state_y",      "");
            c.stateClick = cj.value("state_click",  "");
            c.stateUp    = cj.value("state_up",     "");
            c.stateDown  = cj.value("state_down",   "");
            c.stateLeft  = cj.value("state_left",   "");
            c.stateRight = cj.value("state_right",  "");
            c.imageUp    = cj.value("image_up",     "");
            c.imageDown  = cj.value("image_down",   "");
            c.imageLeft  = cj.value("image_left",   "");
            c.imageRight = cj.value("image_right",  "");

            parseColor4(cj, "color",                c.colorR,         c.colorG,         c.colorB,         c.colorA);
            parseColor4(cj, "active_color",         c.activeColorR,   c.activeColorG,   c.activeColorB,   c.activeColorA);
            parseColor4(cj, "overlay_color",        c.ovColorR,       c.ovColorG,       c.ovColorB,       c.ovColorA);
            parseColor4(cj, "active_overlay_color", c.activeOvColorR, c.activeOvColorG, c.activeOvColorB, c.activeOvColorA);

            L.components.push_back(std::move(c));
        }
        result.push_back(std::move(L));
    }
    return result;
}

const PadLayout* findLayout(const std::vector<PadLayout>& layouts, const std::string& id) {
    for (const auto& L : layouts)
        if (L.id == id) return &L;
    return nullptr;
}

void savePadLayouts(const std::string& path, const std::vector<PadLayout>& layouts) {
    auto writeColor4 = [](json& obj, const char* key, float r, float g, float b, float a) {
        obj[key] = json::array({ r, g, b, a });
    };

    json root;
    json jarr = json::array();

    for (const auto& L : layouts) {
        json jl;
        jl["id"]     = L.id;
        jl["canvas"] = json{ {"W", L.W}, {"FrontH", L.FrontH}, {"TopH", L.TopH} };

        json jcomps = json::array();
        for (const auto& c : L.components) {
            json jc;
            if (!c.id.empty())  jc["id"]   = c.id;
            jc["view"]  = c.view;
            jc["type"]  = c.type;

            if (!c.image.empty())    jc["image"]   = c.image;
            if (!c.overlay.empty())  jc["overlay"]  = c.overlay;

            jc["cx"] = c.cx;
            jc["cy"] = c.cy;

            if (c.w         != 0.0f)  jc["w"]          = c.w;
            if (c.h         != 0.0f)  jc["h"]          = c.h;
            if (c.size      != 0.0f)  jc["size"]        = c.size;
            if (c.maxOffset != 0.0f)  jc["max_offset"]  = c.maxOffset;
            if (c.threshold != 0.05f) jc["threshold"]   = c.threshold;

            if (c.overlayScaleX != 1.0f || c.overlayScaleY != 1.0f) {
                if (c.overlayScaleX == c.overlayScaleY)
                    jc["overlay_scale"] = c.overlayScaleX;
                else
                    jc["overlay_scale"] = json::array({ c.overlayScaleX, c.overlayScaleY });
            }

            if (!c.state.empty())      jc["state"]       = c.state;
            if (!c.stateX.empty())     jc["state_x"]     = c.stateX;
            if (!c.stateY.empty())     jc["state_y"]     = c.stateY;
            if (!c.stateClick.empty()) jc["state_click"]  = c.stateClick;
            if (!c.stateUp.empty())    jc["state_up"]    = c.stateUp;
            if (!c.stateDown.empty())  jc["state_down"]  = c.stateDown;
            if (!c.stateLeft.empty())  jc["state_left"]  = c.stateLeft;
            if (!c.stateRight.empty()) jc["state_right"] = c.stateRight;

            if (!c.imageUp.empty())    jc["image_up"]    = c.imageUp;
            if (!c.imageDown.empty())  jc["image_down"]  = c.imageDown;
            if (!c.imageLeft.empty())  jc["image_left"]  = c.imageLeft;
            if (!c.imageRight.empty()) jc["image_right"] = c.imageRight;

            writeColor4(jc, "color", c.colorR, c.colorG, c.colorB, c.colorA);
            if (c.type == "button" || c.type == "stick" || c.type == "dpad") {
                writeColor4(jc, "active_color",
                            c.activeColorR, c.activeColorG, c.activeColorB, c.activeColorA);
                if (c.type == "button") {
                    writeColor4(jc, "overlay_color",
                                c.ovColorR, c.ovColorG, c.ovColorB, c.ovColorA);
                    writeColor4(jc, "active_overlay_color",
                                c.activeOvColorR, c.activeOvColorG, c.activeOvColorB, c.activeOvColorA);
                }
            }

            jcomps.push_back(std::move(jc));
        }
        jl["components"] = std::move(jcomps);
        jarr.push_back(std::move(jl));
    }

    root["layouts"] = std::move(jarr);

    std::ofstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot write pad_layouts: " + path);
    f << root.dump(4) << '\n';
}
