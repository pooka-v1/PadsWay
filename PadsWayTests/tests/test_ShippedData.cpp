// macros/Macro.h pulls in <windows.h> — NOMINMAX first (see CLAUDE.md, "Tests").
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "config/ConfigLoader.h"
#include "macros/MacroParser.h"
#include "input/TouchZones.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <catch2/catch_amalgamated.hpp>

// ─── Shipped data — the JSON files under PadsWay/data/ that go out with a release. These tests load
// the real files (not fixtures) and check they parse and reference each other consistently: every
// macro in the library compiles, every profile macro name exists in the library, every layout_id
// exists in pad_layouts.json, both UI string files have the same keys, and pad_layouts.json
// survives a save/load round trip (the layout editor rewrites it).
//
// The data folder is located from this source file's own path; if it can't be found (e.g. the test
// exe was copied elsewhere and built without full paths in __FILE__) the tests SKIP instead of fail.

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path shippedDataDir() {
    const fs::path candidates[] = {
        fs::path(__FILE__).parent_path().parent_path().parent_path() / "PadsWay" / "data",
        fs::path("..") / "PadsWay" / "data",   // VS default working dir: $(ProjectDir)
        fs::path("PadsWay") / "data",          // run from the solution dir
    };
    for (const auto& dir : candidates) {
        std::error_code ec;
        if (fs::exists(dir / "pad_layouts.json", ec))
            return dir;
    }
    return {};
}

#define REQUIRE_SHIPPED_DATA(dir)                                                   \
    const fs::path dir = shippedDataDir();                                          \
    if (dir.empty()) SKIP("PadsWay/data not found from the test's working directory")

json readJson(const fs::path& path) {
    std::ifstream f(path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

// Same flattening as Strings.cpp: nested objects become dot-separated keys, only string leaves count.
void flattenStringKeys(const json& node, const std::string& prefix, std::set<std::string>& out) {
    for (const auto& [k, v] : node.items()) {
        const std::string key = prefix.empty() ? k : prefix + "." + k;
        if (v.is_object())      flattenStringKeys(v, key, out);
        else if (v.is_string()) out.insert(key);
    }
}

// Every {"type": "macro", "name": X} with no inline "execution" anywhere in the document — those
// resolve against macros.json by name at load time (see initActionHolderState).
void collectLibraryMacroRefs(const json& node, std::vector<std::string>& out) {
    if (node.is_object()) {
        auto stringField = [&node](const char* key) -> std::string {
            return (node.contains(key) && node[key].is_string()) ? node[key].get<std::string>() : "";
        };
        if (stringField("type") == "macro" && !stringField("name").empty() &&
            stringField("execution").empty())
            out.push_back(stringField("name"));
        for (const auto& [k, v] : node.items()) collectLibraryMacroRefs(v, out);
    } else if (node.is_array()) {
        for (const auto& v : node) collectLibraryMacroRefs(v, out);
    }
}

void collectLayoutIds(const json& node, std::vector<std::string>& out) {
    if (node.is_object()) {
        if (node.contains("layout_id") && node["layout_id"].is_string())
            out.push_back(node["layout_id"].get<std::string>());
        for (const auto& [k, v] : node.items()) collectLayoutIds(v, out);
    } else if (node.is_array()) {
        for (const auto& v : node) collectLayoutIds(v, out);
    }
}

std::vector<fs::path> profileFiles(const fs::path& dataDir) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dataDir / "profiles"))
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    return files;
}

} // namespace

TEST_CASE("Shipped macros.json: every macro in the library compiles", "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    const auto library = loadMacroLibrary((dataDir / "macros.json").string());
    REQUIRE_FALSE(library.empty());

    for (const auto& [name, execution] : library) {
        CAPTURE(name, execution);
        Macro macro;
        CHECK_NOTHROW(MacroParser::parse(execution, macro));
        CHECK_FALSE(macro.getSteps().empty());
    }
}

TEST_CASE("Shipped profiles: every profile loads and its macro names exist in macros.json",
          "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    const auto library = loadMacroLibrary((dataDir / "macros.json").string());
    const auto files = profileFiles(dataDir);
    REQUIRE_FALSE(files.empty());

    for (const auto& file : files) {
        const std::string fileName = file.filename().string();
        CAPTURE(fileName);
        CHECK_NOTHROW(loadGameProfile(file.string()));

        std::vector<std::string> refs;
        collectLibraryMacroRefs(readJson(file), refs);
        for (const auto& macroName : refs) {
            CAPTURE(macroName);
            CHECK(library.count(macroName) == 1);
        }
    }
}

TEST_CASE("Shipped controllers.json loads and every layout_id exists in pad_layouts.json",
          "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    const auto layouts = loadPadLayouts((dataDir / "pad_layouts.json").string());
    REQUIRE_FALSE(layouts.empty());

    const auto configs = loadControllerConfigs((dataDir / "controllers.json").string());
    REQUIRE_FALSE(configs.empty());

    // The wizard's templates are also copied into new entries, so their layout_id must resolve too.
    for (const char* name : { "controllers.json", "controllers_template_en.json",
                              "controllers_template_es.json" }) {
        CAPTURE(name);
        std::vector<std::string> ids;
        collectLayoutIds(readJson(dataDir / name), ids);
        CHECK_FALSE(ids.empty());
        for (const auto& id : ids) {
            CAPTURE(id);
            CHECK(findLayout(layouts, id) != nullptr);
        }
    }
}

TEST_CASE("Shipped pad_layouts.json has unique layout ids", "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    const auto layouts = loadPadLayouts((dataDir / "pad_layouts.json").string());
    std::set<std::string> seen;
    for (const auto& layout : layouts) {
        CAPTURE(layout.id);
        CHECK_FALSE(layout.id.empty());
        CHECK(seen.insert(layout.id).second);
        CHECK_FALSE(layout.components.empty());
    }
}

TEST_CASE("Shipped pad_layouts.json survives a save/load round trip", "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    const auto original = loadPadLayouts((dataDir / "pad_layouts.json").string());
    const std::string tmp = "test_tmp_shipped_layouts_roundtrip.json";
    savePadLayouts(tmp, original);
    const auto reloaded = loadPadLayouts(tmp);
    std::remove(tmp.c_str());

    REQUIRE(reloaded.size() == original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        const auto& a = original[i];
        const auto& b = reloaded[i];
        CAPTURE(a.id);
        CHECK(b.id == a.id);
        CHECK(b.W == a.W);
        CHECK(b.FrontH == a.FrontH);
        CHECK(b.TopH == a.TopH);
        REQUIRE(b.components.size() == a.components.size());
        for (size_t c = 0; c < a.components.size(); ++c) {
            const auto& ca = a.components[c];
            const auto& cb = b.components[c];
            CAPTURE(c, ca.id);
            CHECK(cb.id == ca.id);
            CHECK(cb.type == ca.type);
            CHECK(cb.view == ca.view);
            CHECK(cb.image == ca.image);
            CHECK(cb.overlay == ca.overlay);
            CHECK(cb.cx == ca.cx);
            CHECK(cb.cy == ca.cy);
            CHECK(cb.w == ca.w);
            CHECK(cb.h == ca.h);
            CHECK(cb.size == ca.size);
            CHECK(cb.state == ca.state);
            CHECK(cb.stateX == ca.stateX);
            CHECK(cb.stateY == ca.stateY);
            CHECK(cb.stateClick == ca.stateClick);
            CHECK(cb.stateUp == ca.stateUp);
            CHECK(cb.stateRight == ca.stateRight);
        }
    }
}

TEST_CASE("Shipped UI strings: strings_en.json and strings_es.json have the same keys",
          "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    std::set<std::string> en, es;
    flattenStringKeys(readJson(dataDir / "strings" / "strings_en.json"), "", en);
    flattenStringKeys(readJson(dataDir / "strings" / "strings_es.json"), "", es);
    REQUIRE_FALSE(en.empty());

    for (const auto& key : en) { CAPTURE(key); CHECK(es.count(key) == 1); }
    for (const auto& key : es) { CAPTURE(key); CHECK(en.count(key) == 1); }
}

TEST_CASE("Shipped touch_zone_templates.json: every template has regions with unique ids",
          "[ShippedData]") {
    REQUIRE_SHIPPED_DATA(dataDir);
    const auto templates = loadTouchZoneTemplates((dataDir / "touch_zone_templates.json").string());
    REQUIRE_FALSE(templates.empty());

    std::set<std::string> templateIds;
    for (const auto& tpl : templates) {
        CAPTURE(tpl.id);
        CHECK(templateIds.insert(tpl.id).second);
        CHECK_FALSE(tpl.regions.empty());
        std::set<std::string> regionIds;
        for (const auto& region : tpl.regions) {
            CAPTURE(region.id);
            CHECK_FALSE(region.id.empty());
            CHECK(regionIds.insert(region.id).second);
        }
    }
}
