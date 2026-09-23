#include "E2ESandbox.h"
#include "nlohmann/json.hpp"
#include <fstream>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace E2ESandbox {

namespace {

fs::path sandboxDir() { return repoRoot() / "temp" / "e2e" / "sandbox"; }

bool writeText(const fs::path& path, const std::string& text, std::string& error) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) { error = "cannot write " + path.string(); return false; }
    f << text;
    return true;
}

unsigned long parseHex(const json& value) {
    return value.is_string() ? std::stoul(value.get<std::string>(), nullptr, 16) : 0;
}

// The fake pad's entry, taken verbatim from the repo's controllers.json: the suite checks the
// DS4 as shipped (e.g. uncustomized Cross -> A), so nothing in it is adapted here.
bool extractFakePadEntry(json& entry, std::string& error) {
    const fs::path source = repoRoot() / "PadsWay" / "data" / "controllers.json";
    std::ifstream f(source);
    if (!f.is_open()) { error = "cannot read " + source.string(); return false; }
    json root = json::parse(f, nullptr, false);
    if (root.is_discarded() || !root.contains("controllers")) {
        error = source.string() + " is not a valid controllers file";
        return false;
    }
    for (const auto& c : root["controllers"]) {
        if (parseHex(c.value("vid", json())) == kFakePadVid && parseHex(c.value("pid", json())) == kFakePadPid) {
            entry = c;
            return true;
        }
    }
    error = "no 054C:09CC (DualShock 4 USB) entry in " + source.string();
    return false;
}

} // namespace

fs::path repoRoot() {
    // __FILE__ = <repo>/PadsWayE2E/src/E2ESandbox.cpp
    return fs::path(__FILE__).parent_path().parent_path().parent_path();
}

fs::path baselineControllersPath() { return sandboxDir() / "controllers.baseline.json"; }

bool prepare(std::string& error) {
    if (!fs::exists(repoRoot() / "PadsWay.slnx")) {
        error = "repo root not found from __FILE__ (" + repoRoot().string() + ")";
        return false;
    }

    const fs::path dir = sandboxDir();
    std::error_code ec;
    fs::remove_all(dir, ec);   // always under <repo>/temp/e2e/ — never user data
    if (ec) { error = "cannot clear " + dir.string() + ": " + ec.message(); return false; }
    fs::create_directories(dir / "data" / "profiles", ec);
    fs::create_directories(dir / "data" / "bots", ec);
    fs::create_directories(dir / "logs", ec);
    if (ec) { error = "cannot create " + dir.string() + ": " + ec.message(); return false; }

    json entry;
    if (!extractFakePadEntry(entry, error)) return false;
    const std::string controllers = json{ { "controllers", json::array({ entry }) } }.dump(2);

    const json virtualPad = {
        { "virtual_x_vid", "5650" },      { "virtual_x_pid", "0001" },
        { "virtual_direct_vid", "054C" }, { "virtual_direct_pid", "05C4" },
        { "output_type", "xbox" },        { "log_level", "debug" },
        { "console", false },
    };

    // Explicit key assignment on purpose: brace-initializing {{name, dsl}} from two const char*
    // variables came out as an empty object, not {"name": dsl}.
    json macros = json::object();
    macros[kComboMacroName] = kComboMacroDsl;

    if (!writeText(dir / "portable.txt", "", error) ||
        !writeText(dir / "data" / "controllers.json", controllers, error) ||
        !writeText(baselineControllersPath(), controllers, error) ||
        !writeText(dir / "data" / "macros.json", macros.dump(2), error) ||
        !writeText(dir / "data" / "virtualpad.json", virtualPad.dump(2), error))
        return false;

    fs::current_path(dir, ec);
    if (ec) { error = "cannot chdir into " + dir.string() + ": " + ec.message(); return false; }
    return true;
}

}
