#include "config/ControllerModes.h"
#include <cstdio>
#include <fstream>
#include <catch2/catch_amalgamated.hpp>

// ─── ControllerModes — deducing a pad's mode family ("d"/"x"/"s") to preselect it in the wizard.
// Three layers, first hit wins: known_controllers.json, the user's own entries, vendor rules
// (ARCHITECTURE.md "Tarea 6", mode).

namespace {

std::vector<KnownControllerId> loadFrom(const std::string& text, std::vector<std::string>* warnings) {
    const std::string path = "test_tmp_known_controllers.json";
    { std::ofstream f(path); f << text; }
    auto result = loadKnownControllers(path, warnings);
    std::remove(path.c_str());
    return result;
}

ControllerConfig userEntry(uint16_t vid, uint16_t pid, const std::string& mode,
                           const std::string& productName = "") {
    ControllerConfig cfg;
    cfg.config_id = "user-entry";
    cfg.ids.push_back({ vid, pid, "" });
    cfg.mode = mode;
    cfg.product_name = productName;
    return cfg;
}

} // namespace

TEST_CASE("isKnownControllerMode accepts d/x/s only", "[ControllerModes]") {
    CHECK(isKnownControllerMode("d"));
    CHECK(isKnownControllerMode("x"));
    CHECK(isKnownControllerMode("s"));
    CHECK_FALSE(isKnownControllerMode(""));
    CHECK_FALSE(isKnownControllerMode("hid"));
    CHECK_FALSE(isKnownControllerMode("X"));
}

TEST_CASE("loadKnownControllers reads every valid row", "[ControllerModes]") {
    std::vector<std::string> warnings;
    const auto known = loadFrom(R"({ "controllers": [
        { "vid": "2DC8", "pid": "310B", "mode": "x", "name": "Pro 3 X-mode" },
        { "vid": "054c", "pid": "05c4", "mode": "d" }
    ]})", &warnings);
    CHECK(warnings.empty());
    REQUIRE(known.size() == 2);
    CHECK(known[0].vid == 0x2DC8);
    CHECK(known[0].pid == 0x310B);
    CHECK(known[0].mode == "x");
    CHECK(known[0].name == "Pro 3 X-mode");
    CHECK(known[1].vid == 0x054C);   // lowercase hex is fine
    CHECK(known[1].pid == 0x05C4);
    CHECK(known[1].name.empty());
}

TEST_CASE("loadKnownControllers skips bad rows and keeps the rest", "[ControllerModes]") {
    // The file only feeds a proposal: one broken row must not cost the others.
    std::vector<std::string> warnings;
    const auto known = loadFrom(R"({ "controllers": [
        { "vid": "2DC8", "pid": "6009", "mode": "d" },
        { "vid": "2DC8", "pid": "6009", "mode": "hid" },
        { "vid": "12zz", "pid": "6009", "mode": "d" },
        { "vid": "2DC8", "mode": "d" },
        { "vid": 11720, "pid": "6009", "mode": "d" },
        "not an object"
    ]})", &warnings);
    REQUIRE(known.size() == 1);
    CHECK(known[0].pid == 0x6009);
    CHECK(warnings.size() == 5);
}

TEST_CASE("loadKnownControllers never throws on a missing or broken file", "[ControllerModes]") {
    std::vector<std::string> warnings;
    SECTION("missing") {
        CHECK(loadKnownControllers("__nonexistent_known__.json", &warnings).empty());
    }
    SECTION("not JSON") {
        CHECK(loadFrom("{ this is not json", &warnings).empty());
    }
    SECTION("no controllers list") {
        CHECK(loadFrom(R"({ "something": [] })", &warnings).empty());
    }
    CHECK(warnings.size() == 1);
}

TEST_CASE("deduceMode: known ids win over the user's entries", "[ControllerModes]") {
    // A user who once declared a known X-mode pad as "d" gets "x" proposed again.
    const std::vector<KnownControllerId> known = { { 0x2DC8, 0x310B, "x", "" } };
    const std::vector<ControllerConfig> user = { userEntry(0x2DC8, 0x310B, "d") };
    CHECK(deduceMode(0x2DC8, 0x310B, "", known, user) == "x");
}

TEST_CASE("deduceMode: the user's entry fills what known ids don't cover", "[ControllerModes]") {
    const std::vector<ControllerConfig> user = { userEntry(0x1234, 0x0001, "s") };
    CHECK(deduceMode(0x1234, 0x0001, "", {}, user) == "s");
}

TEST_CASE("deduceMode: a user entry without a mode says nothing", "[ControllerModes]") {
    // Falls through to the vendor rule instead of returning the entry's empty mode.
    const std::vector<ControllerConfig> user = { userEntry(0x054C, 0x1111, "") };
    CHECK(deduceMode(0x054C, 0x1111, "", {}, user) == "d");
}

TEST_CASE("deduceMode: the user's entry must match the product name like findConfig", "[ControllerModes]") {
    // 045E:02E0 is shared (Pro 2, Zero 2): another pad's entry must not lend its mode.
    const std::vector<ControllerConfig> user = { userEntry(0x2DC8, 0x5555, "s", "8BitDo Pro 2") };
    CHECK(deduceMode(0x2DC8, 0x5555, "8BitDo Zero 2 gamepad", {}, user).empty());
    CHECK(deduceMode(0x2DC8, 0x5555, "8BitDo Pro 2", {}, user) == "s");
}

TEST_CASE("deduceMode: vendor rules, and nothing for an unknown vendor", "[ControllerModes]") {
    CHECK(deduceMode(0x045E, 0x0B13, "", {}, {}) == "x");   // Microsoft
    CHECK(deduceMode(0x054C, 0x0268, "", {}, {}) == "d");   // Sony
    CHECK(deduceMode(0x057E, 0x2009, "", {}, {}) == "s");   // Nintendo
    CHECK(deduceMode(0x2DC8, 0x9999, "", {}, {}).empty());  // 8BitDo: X-mode doesn't imply a VID
}
