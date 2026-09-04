#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include "../nlohmann/json.hpp"

// ─── Touch zone geometry — see ARCHITECTURE.md "Touchpad" -> "Zonas" ─────────
//
// A TouchZoneTemplate divides the touchpad surface (normalized [0,1] coordinates, same
// convention as GamepadState::touch1X/Y — origin top-left, Y grows downward) into named regions
// for the Zonas surfaceMode. Every region is one of three shapes:
//   Rect   — axis-aligned bounding box in surface space (xMin/xMax/yMin/yMax).
//   Wedge  — angular slice measured from the template's geometric center (0.5, 0.5). Angle 0 is
//            East (+X), increasing through South/West/North (90/180/270) — the raw
//            atan2(dy,dx) convention with no sign flip, which lands there precisely because Y
//            already grows downward (see PhysicalTouchpad::process()'s identical note on the
//            Analog surfaceMode's Y axis). Wedges carry no radius of their own.
//   Circle — disk of the given radius around the center.
// A template with a carved-out center (the "-5-center"/"compass-8-center" variants) lists that
// region first with shape=Circle; hitTestTouchZone() returns the first matching region, so the
// center always wins over the wedges/quadrants around it without each of them needing its own
// radius test — region order in TouchZoneTemplate::regions matters, first match wins.

enum class TouchZoneShape { Rect, Wedge, Circle };

struct TouchZoneRegion {
    std::string    id;             // "left", "n", "center", "r1c2", ... — stable key for
                                    // per-instance zones/actions later, not just a display label.
    TouchZoneShape shape = TouchZoneShape::Rect;
    // Rect, surface-space [0,1], inclusive bounds.
    float xMin = 0.0f, xMax = 1.0f, yMin = 0.0f, yMax = 1.0f;
    // Wedge, degrees, see the file comment for the angle convention. Wraps across 360 when
    // angleTo < angleFrom (e.g. the East wedge in cross-x-4: angleFrom=315, angleTo=45).
    float angleFromDeg = 0.0f, angleToDeg = 0.0f;
    // Circle, normalized radius from center (0.5, 0.5).
    float radius = 0.0f;
    // Per-instance only (template regions are always enabled): explicit off switch so a disabled
    // region can still occupy its slot in TouchpadConfig::zones instead of being deleted and
    // losing its adjusted bounds/action — see ARCHITECTURE.md "Zonas", "never size==0 as a magic
    // value".
    bool enabled = true;
};

struct TouchZoneTemplate {
    std::string id;                        // "single-1", "split-lr-2", ...
    std::vector<TouchZoneRegion> regions;
};

// Angle in degrees [0,360) of (x,y) from the surface's geometric center — see the file comment
// for the convention (0=East, 90=South, 180=West, 270=North).
inline float touchZoneAngleDeg(float x, float y) {
    float dx = x - 0.5f, dy = y - 0.5f;
    float deg = std::atan2(dy, dx) * (180.0f / 3.14159265358979323846f);
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

inline float touchZoneRadius(float x, float y) {
    float dx = x - 0.5f, dy = y - 0.5f;
    return std::sqrt(dx * dx + dy * dy);
}

// True if angleDeg falls within [fromDeg, toDeg), wrapping across 360 when toDeg < fromDeg.
inline bool touchZoneAngleInRange(float angleDeg, float fromDeg, float toDeg) {
    if (fromDeg <= toDeg) return angleDeg >= fromDeg && angleDeg < toDeg;
    return angleDeg >= fromDeg || angleDeg < toDeg;   // wraps through 0
}

// Finds which region of regions contains (x, y) (both normalized [0,1], same convention as
// GamepadState::touch1X/Y). Disabled regions (TouchZoneRegion::enabled == false) never match —
// a touch that would have landed there simply hits nothing, same as landing outside the surface.
// Returns nullptr if no region matches — shouldn't happen for a well-formed template covering the
// full surface, but callers must handle it (a hand-edited template, a coordinate slightly outside
// [0,1] from raw sensor noise, or every covering region disabled, could miss).
inline const TouchZoneRegion* hitTestTouchZone(const std::vector<TouchZoneRegion>& regions,
                                                 float x, float y) {
    float angle  = touchZoneAngleDeg(x, y);
    float radius = touchZoneRadius(x, y);
    for (const auto& r : regions) {
        if (!r.enabled) continue;
        switch (r.shape) {
            case TouchZoneShape::Rect:
                if (x >= r.xMin && x <= r.xMax && y >= r.yMin && y <= r.yMax) return &r;
                break;
            case TouchZoneShape::Wedge:
                if (touchZoneAngleInRange(angle, r.angleFromDeg, r.angleToDeg)) return &r;
                break;
            case TouchZoneShape::Circle:
                if (radius <= r.radius) return &r;
                break;
        }
    }
    return nullptr;
}

inline const TouchZoneRegion* hitTestTouchZone(const TouchZoneTemplate& tmpl, float x, float y) {
    return hitTestTouchZone(tmpl.regions, x, y);
}

// Parses one region — shared by the template catalog (data/touch_zone_templates.json) and
// TouchpadConfig::zones' per-instance JSON (same field shapes, "enabled" only meaningful for the
// latter since template regions are always enabled).
inline TouchZoneRegion parseTouchZoneRegion(const nlohmann::json& rj) {
    TouchZoneRegion r;
    r.id = rj.value("id", std::string{});
    std::string shape = rj.value("shape", std::string("rect"));
    if      (shape == "wedge")  r.shape = TouchZoneShape::Wedge;
    else if (shape == "circle") r.shape = TouchZoneShape::Circle;
    else                        r.shape = TouchZoneShape::Rect;
    r.xMin         = rj.value("x_min",     0.0f);
    r.xMax         = rj.value("x_max",     1.0f);
    r.yMin         = rj.value("y_min",     0.0f);
    r.yMax         = rj.value("y_max",     1.0f);
    r.angleFromDeg = rj.value("angle_from", 0.0f);
    r.angleToDeg   = rj.value("angle_to",   0.0f);
    r.radius       = rj.value("radius",     0.0f);
    r.enabled      = rj.value("enabled",    true);
    return r;
}

// Inverse of parseTouchZoneRegion — used to persist TouchpadConfig::zones (the catalog in
// data/touch_zone_templates.json is read-only, never written back by the app).
inline nlohmann::json touchZoneRegionToJson(const TouchZoneRegion& r) {
    nlohmann::json rj;
    rj["id"] = r.id;
    switch (r.shape) {
        case TouchZoneShape::Wedge:  rj["shape"] = "wedge";  break;
        case TouchZoneShape::Circle: rj["shape"] = "circle"; break;
        default:                     rj["shape"] = "rect";   break;
    }
    rj["x_min"]      = r.xMin;
    rj["x_max"]      = r.xMax;
    rj["y_min"]      = r.yMin;
    rj["y_max"]      = r.yMax;
    rj["angle_from"] = r.angleFromDeg;
    rj["angle_to"]   = r.angleToDeg;
    rj["radius"]     = r.radius;
    rj["enabled"]    = r.enabled;
    return rj;
}

// Parses one template from a JSON object shaped like data/touch_zone_templates.json's array
// entries: {"id": "...", "regions": [{"id":"...", "shape":"rect"/"wedge"/"circle", ...}, ...]}.
inline TouchZoneTemplate parseTouchZoneTemplate(const nlohmann::json& j) {
    TouchZoneTemplate t;
    t.id = j.value("id", std::string{});
    for (const auto& rj : j.value("regions", nlohmann::json::array()))
        t.regions.push_back(parseTouchZoneRegion(rj));
    return t;
}

// Loads the shared catalog (data/touch_zone_templates.json) — same spirit as
// loadPadLayouts()/pad_layouts.json (ConfigLoader.cpp): reusable geometry, not duplicated per
// controller instance. Returns an empty vector if the file is missing (optional file, matching
// the other loaders' convention).
inline std::vector<TouchZoneTemplate> loadTouchZoneTemplates(const std::string& path) {
    std::vector<TouchZoneTemplate> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;
    nlohmann::json root = nlohmann::json::parse(f);
    for (const auto& tj : root.value("templates", nlohmann::json::array()))
        result.push_back(parseTouchZoneTemplate(tj));
    return result;
}
