#include "input/TouchZones.h"          // project header first, see CLAUDE.md
#include <catch2/catch_amalgamated.hpp>

// ─── touchZoneAngleDeg() / touchZoneRadius() — coordinate convention ─────────
// See TouchZones.h's file comment: 0=East, 90=South, 180=West, 270=North (Y grows downward, same
// as GamepadState::touch1X/Y, so this is the raw atan2(dy,dx) with no sign flip).

TEST_CASE("touchZoneAngleDeg cardinal directions", "[TouchZones]") {
    CHECK(touchZoneAngleDeg(1.0f, 0.5f) == Catch::Approx(0.0f));     // East
    CHECK(touchZoneAngleDeg(0.5f, 1.0f) == Catch::Approx(90.0f));    // South
    CHECK(touchZoneAngleDeg(0.0f, 0.5f) == Catch::Approx(180.0f));   // West
    CHECK(touchZoneAngleDeg(0.5f, 0.0f) == Catch::Approx(270.0f));   // North
}

TEST_CASE("touchZoneRadius at center is zero, at edge is ~0.5", "[TouchZones]") {
    CHECK(touchZoneRadius(0.5f, 0.5f) == Catch::Approx(0.0f));
    CHECK(touchZoneRadius(1.0f, 0.5f) == Catch::Approx(0.5f));
    CHECK(touchZoneRadius(0.5f, 0.0f) == Catch::Approx(0.5f));
}

TEST_CASE("touchZoneAngleInRange handles the 360-wrap case", "[TouchZones]") {
    // East wedge in cross-x-4: [315, 45) — wraps through 0.
    CHECK(touchZoneAngleInRange(350.0f, 315.0f, 45.0f));
    CHECK(touchZoneAngleInRange(10.0f,  315.0f, 45.0f));
    CHECK(touchZoneAngleInRange(0.0f,   315.0f, 45.0f));
    CHECK_FALSE(touchZoneAngleInRange(100.0f, 315.0f, 45.0f));
    CHECK_FALSE(touchZoneAngleInRange(45.0f,  315.0f, 45.0f));   // exclusive upper bound
    CHECK(touchZoneAngleInRange(315.0f, 315.0f, 45.0f));         // inclusive lower bound

    // Non-wrapping range.
    CHECK(touchZoneAngleInRange(90.0f, 45.0f, 135.0f));
    CHECK_FALSE(touchZoneAngleInRange(200.0f, 45.0f, 135.0f));
}

// ─── hitTestTouchZone() — per-shape region matching ──────────────────────────

TEST_CASE("hitTestTouchZone single-1 matches everywhere", "[TouchZones]") {
    TouchZoneTemplate t;
    t.regions.push_back({"all", TouchZoneShape::Rect, 0.0f, 1.0f, 0.0f, 1.0f});

    for (auto [x, y] : { std::pair{0.0f,0.0f}, std::pair{0.5f,0.5f}, std::pair{1.0f,1.0f} }) {
        const auto* r = hitTestTouchZone(t, x, y);
        REQUIRE(r != nullptr);
        CHECK(r->id == "all");
    }
}

TEST_CASE("hitTestTouchZone split-lr-2 splits at the vertical midline", "[TouchZones]") {
    TouchZoneTemplate t;
    t.regions.push_back({"left",  TouchZoneShape::Rect, 0.0f, 0.5f, 0.0f, 1.0f});
    t.regions.push_back({"right", TouchZoneShape::Rect, 0.5f, 1.0f, 0.0f, 1.0f});

    CHECK(hitTestTouchZone(t, 0.1f, 0.5f)->id == "left");
    CHECK(hitTestTouchZone(t, 0.9f, 0.5f)->id == "right");
    // Boundary: first matching region wins, "left"'s xMax=0.5 is inclusive and listed first.
    CHECK(hitTestTouchZone(t, 0.5f, 0.5f)->id == "left");
}

TEST_CASE("hitTestTouchZone cross-plus-4 quadrants", "[TouchZones]") {
    TouchZoneTemplate t;
    t.regions.push_back({"nw", TouchZoneShape::Rect, 0.0f, 0.5f, 0.0f, 0.5f});
    t.regions.push_back({"ne", TouchZoneShape::Rect, 0.5f, 1.0f, 0.0f, 0.5f});
    t.regions.push_back({"sw", TouchZoneShape::Rect, 0.0f, 0.5f, 0.5f, 1.0f});
    t.regions.push_back({"se", TouchZoneShape::Rect, 0.5f, 1.0f, 0.5f, 1.0f});

    CHECK(hitTestTouchZone(t, 0.1f, 0.1f)->id == "nw");
    CHECK(hitTestTouchZone(t, 0.9f, 0.1f)->id == "ne");
    CHECK(hitTestTouchZone(t, 0.1f, 0.9f)->id == "sw");
    CHECK(hitTestTouchZone(t, 0.9f, 0.9f)->id == "se");
}

TEST_CASE("hitTestTouchZone cross-x-4 wedges, including the wrap-around East wedge", "[TouchZones]") {
    TouchZoneTemplate t;
    t.regions.push_back({"e", TouchZoneShape::Wedge, 0,0,0,0, 315.0f, 45.0f});
    t.regions.push_back({"s", TouchZoneShape::Wedge, 0,0,0,0, 45.0f,  135.0f});
    t.regions.push_back({"w", TouchZoneShape::Wedge, 0,0,0,0, 135.0f, 225.0f});
    t.regions.push_back({"n", TouchZoneShape::Wedge, 0,0,0,0, 225.0f, 315.0f});

    CHECK(hitTestTouchZone(t, 1.0f, 0.5f)->id == "e");   // due east
    CHECK(hitTestTouchZone(t, 0.9f, 0.55f)->id == "e");  // just off-axis, still within the wedge
    CHECK(hitTestTouchZone(t, 0.5f, 1.0f)->id == "s");   // due south
    CHECK(hitTestTouchZone(t, 0.0f, 0.5f)->id == "w");   // due west
    CHECK(hitTestTouchZone(t, 0.5f, 0.0f)->id == "n");   // due north
}

TEST_CASE("hitTestTouchZone '-5-center' template: the center circle wins over the wedges around it",
          "[TouchZones]") {
    TouchZoneTemplate t;
    t.regions.push_back({"center", TouchZoneShape::Circle, 0,0,0,0, 0,0, 0.2f});
    t.regions.push_back({"e", TouchZoneShape::Wedge, 0,0,0,0, 315.0f, 45.0f});
    t.regions.push_back({"s", TouchZoneShape::Wedge, 0,0,0,0, 45.0f,  135.0f});
    t.regions.push_back({"w", TouchZoneShape::Wedge, 0,0,0,0, 135.0f, 225.0f});
    t.regions.push_back({"n", TouchZoneShape::Wedge, 0,0,0,0, 225.0f, 315.0f});

    // Dead center and a point just inside the radius, even due east, land in "center" first.
    CHECK(hitTestTouchZone(t, 0.5f, 0.5f)->id == "center");
    CHECK(hitTestTouchZone(t, 0.6f, 0.5f)->id == "center");  // radius 0.1 < 0.2
    // Past the radius, the wedges take over exactly as in the plain cross-x-4 case.
    CHECK(hitTestTouchZone(t, 1.0f, 0.5f)->id == "e");
}

TEST_CASE("hitTestTouchZone grid-6 rows and columns", "[TouchZones]") {
    TouchZoneTemplate t;
    t.regions.push_back({"r0c0", TouchZoneShape::Rect, 0.0f,      0.333333f, 0.0f, 0.5f});
    t.regions.push_back({"r0c1", TouchZoneShape::Rect, 0.333333f, 0.666667f, 0.0f, 0.5f});
    t.regions.push_back({"r0c2", TouchZoneShape::Rect, 0.666667f, 1.0f,      0.0f, 0.5f});
    t.regions.push_back({"r1c0", TouchZoneShape::Rect, 0.0f,      0.333333f, 0.5f, 1.0f});
    t.regions.push_back({"r1c1", TouchZoneShape::Rect, 0.333333f, 0.666667f, 0.5f, 1.0f});
    t.regions.push_back({"r1c2", TouchZoneShape::Rect, 0.666667f, 1.0f,      0.5f, 1.0f});

    CHECK(hitTestTouchZone(t, 0.5f, 0.25f)->id == "r0c1");
    CHECK(hitTestTouchZone(t, 0.9f, 0.75f)->id == "r1c2");
}

TEST_CASE("hitTestTouchZone returns nullptr when no region matches", "[TouchZones]") {
    TouchZoneTemplate t;   // no regions at all
    CHECK(hitTestTouchZone(t, 0.5f, 0.5f) == nullptr);
}

// ─── parseTouchZoneTemplate() — JSON parsing ─────────────────────────────────

TEST_CASE("parseTouchZoneTemplate parses a compass-8-center-shaped template", "[TouchZones]") {
    nlohmann::json j = nlohmann::json::parse(R"({
        "id": "compass-8-center",
        "regions": [
            { "id": "center", "shape": "circle", "radius": 0.2 },
            { "id": "e",  "shape": "wedge", "angle_from": 337.5, "angle_to": 22.5 },
            { "id": "n",  "shape": "wedge", "angle_from": 247.5, "angle_to": 292.5 }
        ]
    })");

    TouchZoneTemplate t = parseTouchZoneTemplate(j);
    REQUIRE(t.id == "compass-8-center");
    REQUIRE(t.regions.size() == 3);
    CHECK(t.regions[0].shape == TouchZoneShape::Circle);
    CHECK(t.regions[0].radius == Catch::Approx(0.2f));
    CHECK(t.regions[1].shape == TouchZoneShape::Wedge);
    CHECK(t.regions[1].angleFromDeg == Catch::Approx(337.5f));

    // Round-trip through hitTestTouchZone to confirm the parsed fields are wired correctly,
    // not just present.
    CHECK(hitTestTouchZone(t, 0.5f, 0.5f)->id == "center");
    CHECK(hitTestTouchZone(t, 1.0f, 0.5f)->id == "e");
    CHECK(hitTestTouchZone(t, 0.5f, 0.0f)->id == "n");
}

TEST_CASE("parseTouchZoneTemplate parses compass-8 (no center) with full coverage", "[TouchZones]") {
    nlohmann::json j = nlohmann::json::parse(R"({
        "id": "compass-8",
        "regions": [
            { "id": "e",  "shape": "wedge", "angle_from": 337.5, "angle_to": 22.5 },
            { "id": "se", "shape": "wedge", "angle_from": 22.5,  "angle_to": 67.5 },
            { "id": "s",  "shape": "wedge", "angle_from": 67.5,  "angle_to": 112.5 },
            { "id": "sw", "shape": "wedge", "angle_from": 112.5, "angle_to": 157.5 },
            { "id": "w",  "shape": "wedge", "angle_from": 157.5, "angle_to": 202.5 },
            { "id": "nw", "shape": "wedge", "angle_from": 202.5, "angle_to": 247.5 },
            { "id": "n",  "shape": "wedge", "angle_from": 247.5, "angle_to": 292.5 },
            { "id": "ne", "shape": "wedge", "angle_from": 292.5, "angle_to": 337.5 }
        ]
    })");

    TouchZoneTemplate t = parseTouchZoneTemplate(j);
    REQUIRE(t.id == "compass-8");
    REQUIRE(t.regions.size() == 8);
    for (const auto& r : t.regions) CHECK(r.id != "center");  // the whole point vs compass-8-center

    // Cardinal directions land in their own wedge, same as compass-8-center.
    CHECK(hitTestTouchZone(t, 1.0f, 0.5f)->id == "e");
    CHECK(hitTestTouchZone(t, 0.5f, 1.0f)->id == "s");
    CHECK(hitTestTouchZone(t, 0.0f, 0.5f)->id == "w");
    CHECK(hitTestTouchZone(t, 0.5f, 0.0f)->id == "n");

    // No center carved out: the exact geometric center still resolves to a wedge (whichever the
    // angle convention picks at (0.5,0.5) itself), never nullptr -- this is the behavioral
    // difference from compass-8-center, where the same point hits "center" instead.
    CHECK(hitTestTouchZone(t, 0.5f, 0.5f) != nullptr);
}

TEST_CASE("loadTouchZoneTemplates returns empty on a missing file", "[TouchZones]") {
    auto templates = loadTouchZoneTemplates("this/path/does/not/exist.json");
    CHECK(templates.empty());
}

// ─── enabled — per-instance zones (TouchpadConfig::zones), not template regions ──────────────

TEST_CASE("hitTestTouchZone skips a disabled region and falls through to the next match", "[TouchZones]") {
    std::vector<TouchZoneRegion> zones;
    TouchZoneRegion left{"left", TouchZoneShape::Rect, 0.0f, 0.5f, 0.0f, 1.0f};
    left.enabled = false;
    zones.push_back(left);
    zones.push_back({"right", TouchZoneShape::Rect, 0.5f, 1.0f, 0.0f, 1.0f});
    // Whole surface as a fallback behind the disabled "left" — proves a disabled region is
    // skipped rather than matched-but-ignored (which would still return nullptr here).
    zones.push_back({"all", TouchZoneShape::Rect, 0.0f, 1.0f, 0.0f, 1.0f});

    CHECK(hitTestTouchZone(zones, 0.1f, 0.5f)->id == "all");  // "left" disabled, falls through
    CHECK(hitTestTouchZone(zones, 0.9f, 0.5f)->id == "right");
}

TEST_CASE("parseTouchZoneRegion defaults enabled to true and honors an explicit false", "[TouchZones]") {
    TouchZoneRegion implicit = parseTouchZoneRegion(nlohmann::json::parse(R"({"id":"a"})"));
    CHECK(implicit.enabled == true);

    TouchZoneRegion explicitOff = parseTouchZoneRegion(
        nlohmann::json::parse(R"({"id":"b","enabled":false})"));
    CHECK(explicitOff.enabled == false);
}

TEST_CASE("touchZoneRegionToJson round-trips through parseTouchZoneRegion", "[TouchZones]") {
    TouchZoneRegion r{"n", TouchZoneShape::Wedge, 0,0,0,0, 225.0f, 315.0f};
    r.enabled = false;

    TouchZoneRegion roundTripped = parseTouchZoneRegion(touchZoneRegionToJson(r));
    CHECK(roundTripped.id == "n");
    CHECK(roundTripped.shape == TouchZoneShape::Wedge);
    CHECK(roundTripped.angleFromDeg == Catch::Approx(225.0f));
    CHECK(roundTripped.angleToDeg == Catch::Approx(315.0f));
    CHECK(roundTripped.enabled == false);
}
