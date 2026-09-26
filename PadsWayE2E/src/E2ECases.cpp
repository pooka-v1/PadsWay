#include "E2ECases.h"
#include "E2EMapping.h"
#include "E2ESandbox.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace E2ECases {

namespace {

// Short name -> that component fully pressed. The same field serves both ends: pressing it on the
// fake DS4 (GamepadState in DS4 positions, see E2EHarness.h) and expecting it on the virtual pad,
// because controllers.json uses one vocabulary for physical and virtual names.
// "home" is left out on purpose — see README.md.
bool setPressed(const std::string& name, GamepadState& s) {
    if      (name == "a")           s.btnA      = true;
    else if (name == "b")           s.btnB      = true;
    else if (name == "x")           s.btnX      = true;
    else if (name == "y")           s.btnY      = true;
    else if (name == "l1")          s.btnLB     = true;
    else if (name == "r1")          s.btnRB     = true;
    else if (name == "select")      s.btnBack   = true;
    else if (name == "start")       s.btnStart  = true;
    else if (name == "l3")          s.btnL3     = true;
    else if (name == "r3")          s.btnR3     = true;
    else if (name == "dpad_up")     s.dpadUp    = true;
    else if (name == "dpad_down")   s.dpadDown  = true;
    else if (name == "dpad_left")   s.dpadLeft  = true;
    else if (name == "dpad_right")  s.dpadRight = true;
    else if (name == "l2")          s.triggerL  = 1.0f;
    else if (name == "r2")          s.triggerR  = 1.0f;
    else if (name == "left_x_pos")  s.leftX     = 1.0f;
    else if (name == "left_x_neg")  s.leftX     = -1.0f;
    else if (name == "left_y_pos")  s.leftY     = 1.0f;
    else if (name == "left_y_neg")  s.leftY     = -1.0f;
    else if (name == "right_x_pos") s.rightX    = 1.0f;
    else if (name == "right_x_neg") s.rightX    = -1.0f;
    else if (name == "right_y_pos") s.rightY    = 1.0f;
    else if (name == "right_y_neg") s.rightY    = -1.0f;
    else return false;
    return true;
}

// Buttons and dpad directions: keyed by their physical short name in MappingModel's
// buttonEdits/actionEdits — the model itself routes "dpad_*" keys to controllers.json's dpad_remap
// on save, so the same assign* helpers serve both.
bool isButtonOrDpadSource(const std::string& name) {
    static const char* const kSources[] = { "a", "b", "x", "y", "l1", "r1", "select", "start", "l3", "r3",
                                            "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
    for (const char* s : kSources)
        if (name == s) return true;
    return false;
}

// Stick half-axes: their own path (axisActionEdits -> axis_actions), see E2EMapping::assignHalfAxis.
// Triggers ("l2"/"r2") have a third one (trigActionEdits), see E2EMapping::assignTriggerSource.
bool isHalfAxisSource(const std::string& name) {
    static const char* const kSources[] = { "left_x_pos", "left_x_neg", "left_y_pos", "left_y_neg",
                                            "right_x_pos", "right_x_neg", "right_y_pos", "right_y_neg" };
    for (const char* s : kSources)
        if (name == s) return true;
    return false;
}

// Keyboard key name (the Mapeador's capture names) -> virtual-key code as the hooks report it.
// Written out independently of the engine's own table on purpose: a test that borrowed the engine's
// mapping could never catch a wrong entry in it.
bool keyVk(const std::string& name, WORD& vk) {
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') { vk = static_cast<WORD>('A' + (c - 'a')); return true; }
        if (c >= '0' && c <= '9') { vk = static_cast<WORD>(c); return true; }
    }
    if (name == "shift") { vk = VK_SHIFT;   return true; }
    if (name == "ctrl")  { vk = VK_CONTROL; return true; }
    if (name == "alt")   { vk = VK_MENU;    return true; }
    if (name == "space") { vk = VK_SPACE;   return true; }
    if (name.size() >= 2 && name[0] == 'f') {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) { vk = static_cast<WORD>(VK_F1 + n - 1); return true; }
    }
    return false;
}

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::runtime_error(where + ": " + what);
}

GamepadState pressedOrFail(const std::string& name, const std::string& where) {
    if (name == "home")
        fail(where, "'home' is excluded from the E2E cases on purpose (see PadsWayE2E/cases/README.md)");
    GamepadState s;
    if (!setPressed(name, s)) fail(where, "unknown short name '" + name + "' (see PadsWayE2E/cases/README.md)");
    return s;
}

void parseTarget(const json& target, const json& row, AssignmentCase& c, const std::string& where) {
    if (target.is_string()) {
        c.virtualTarget  = target.get<std::string>();
        c.expectedOutput = pressedOrFail(c.virtualTarget, where);
        c.kind  = (c.virtualTarget == "l2" || c.virtualTarget == "r2") ? TargetKind::Trigger : TargetKind::Virtual;
        c.label = c.source + " -> " + c.virtualTarget;
        return;
    }
    if (!target.is_object() || !target.contains("type"))
        fail(where, "'target' must be a short name or an object with a 'type'");

    const std::string type = target["type"].get<std::string>();
    if (type == "keyboard") {
        const auto keys = target.value("keys", std::vector<std::string>{});
        if (keys.empty()) fail(where, "keyboard target without 'keys'");
        c.kind   = TargetKind::Keyboard;
        c.action = E2EMapping::keyboardAction(keys);
        std::string combo;
        for (const auto& k : keys) {
            WORD vk = 0;
            if (!keyVk(k, vk)) fail(where, "unknown key '" + k + "' (add it to keyVk in E2ECases.cpp)");
            c.injectedOnPress.push_back(keyDown(vk));
            c.injectedOnRelease.insert(c.injectedOnRelease.begin(), keyUp(vk));   // released in reverse
            combo += (combo.empty() ? "" : "+") + k;
        }
        c.label = c.source + " -> keyboard " + combo;
    } else if (type == "mouse_click") {
        const std::string button = target.value("button", std::string{});
        if (button != "left" && button != "right" && button != "middle" && button != "x1" && button != "x2")
            fail(where, "mouse_click 'button' must be left/right/middle/x1/x2");
        c.kind              = TargetKind::MouseClick;
        c.action            = E2EMapping::mouseClickAction(button);
        c.injectedOnPress   = { mouseDown(button) };
        c.injectedOnRelease = { mouseUp(button) };
        c.label             = c.source + " -> mouse " + button;
    } else if (type == "macro") {
        const std::string execution = target.value("execution", std::string{});
        if (execution.empty()) fail(where, "macro target without 'execution'");
        const json expect = row.value("expect", json());
        if (!expect.is_object() || !expect.contains("plays") || !expect.contains("minMs") || !expect.contains("maxMs"))
            fail(where, "macro case needs \"expect\": { \"plays\": [...], \"minMs\": n, \"maxMs\": n }");
        for (const auto& entry : expect["plays"]) {
            const std::string name = entry.get<std::string>();
            pressedOrFail(name, where);   // validation only (unknown names, "home")
            setPressed(name, c.macroPlays);
        }
        c.kind       = TargetKind::Macro;
        c.action     = E2EMapping::inlineMacroAction(execution);
        c.macroMinMs = expect["minMs"].get<int>();
        c.macroMaxMs = expect["maxMs"].get<int>();
        c.label      = c.source + " -> macro " + execution;
    } else if (type == "bot") {
        const std::string name = target.value("name", std::string{});
        if (name != E2ESandbox::kTestBotName)
            fail(where, std::string("bot target must be \"") + E2ESandbox::kTestBotName +
                        "\": it's the only bot whose output the check knows");
        c.kind   = TargetKind::Bot;
        c.action = E2EMapping::botAction(name);
        c.label  = c.source + " -> bot " + name;
    } else if (type == "mouse_move") {
        if (c.sourceKind != SourceKind::HalfAxis)
            fail(where, "mouse_move needs a stick half-axis source (\"left_x_pos\"...)");
        const std::string axis = target.value("axis", std::string{});
        if (axis != "x" && axis != "y") fail(where, "mouse_move 'axis' must be \"x\" or \"y\"");
        const bool sourceIsPos = c.source.ends_with("_pos");
        const int  halfSign    = sourceIsPos ? 1 : -1;
        c.kind           = TargetKind::MouseMove;
        c.mouseAxis      = "mouse_" + axis;
        c.mouseSpeed     = target.value("speed", 15.0f);
        c.oppositeSource = c.source.substr(0, c.source.size() - 4) + (sourceIsPos ? "_neg" : "_pos");
        c.oppositePress  = pressedOrFail(c.oppositeSource, where);
        // _pos halves push +mouseX / +mouseY; the engine flips Y for the screen (stick up = cursor up).
        c.sourceDxSign   = (axis == "x") ?  halfSign : 0;
        c.sourceDySign   = (axis == "y") ? -halfSign : 0;
        c.label          = c.source + " -> mouse_move " + axis;
    } else {
        fail(where, "unknown target type '" + type + "'");
    }
}

// One { "source", "target" [, "expect"] } object — a row of "cases", or one step of a chain.
AssignmentCase parseAssignment(const json& row, std::string where) {
    if (!row.is_object() || !row.contains("source") || !row.contains("target"))
        fail(where, "needs 'source' and 'target'");

    AssignmentCase c;
    c.source = row["source"].get<std::string>();
    where += " (" + c.source + ")";
    c.sourcePress = pressedOrFail(c.source, where);
    if      (isButtonOrDpadSource(c.source))    c.sourceKind = SourceKind::ButtonOrDpad;
    else if (isHalfAxisSource(c.source))        c.sourceKind = SourceKind::HalfAxis;
    else if (c.source == "l2" || c.source == "r2") c.sourceKind = SourceKind::Trigger;
    else fail(where, "'" + c.source + "' can't be a source");
    parseTarget(row["target"], row, c, where);
    if ((c.kind == TargetKind::Virtual || c.kind == TargetKind::Trigger) && c.virtualTarget == c.source)
        fail(where, "source and target are the same: the case would prove nothing");
    return c;
}

// The array `key` of the file, which must exist and not be empty.
json readArray(const std::filesystem::path& file, const char* key) {
    std::ifstream f(file);
    if (!f.is_open()) throw std::runtime_error("cannot read " + file.string());
    const json root = json::parse(f, nullptr, false);
    if (root.is_discarded() || !root.contains(key) || !root[key].is_array())
        throw std::runtime_error(file.string() + ": not valid JSON, or no \"" + key + "\" array");
    if (root[key].empty()) throw std::runtime_error(file.string() + ": \"" + key + "\" is empty");
    return root[key];
}

} // namespace

std::filesystem::path assignmentsFile() {
    return E2ESandbox::repoRoot() / "PadsWayE2E" / "cases" / "assignments.json";
}

std::vector<AssignmentCase> load(const std::filesystem::path& file) {
    const json rows = readArray(file, "cases");
    std::vector<AssignmentCase> cases;
    for (size_t i = 0; i < rows.size(); ++i)
        cases.push_back(parseAssignment(rows[i], file.filename().string() + ", case #" + std::to_string(i + 1)));
    return cases;
}

std::vector<ChainCase> loadChains(const std::filesystem::path& file) {
    const json rows = readArray(file, "chains");
    std::vector<ChainCase> chains;
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string where = file.filename().string() + ", chain #" + std::to_string(i + 1);
        const json& steps = rows[i];
        if (!steps.is_array() || steps.size() < 2)
            fail(where, "a chain is an array of at least 2 { \"source\", \"target\" } assignments");

        ChainCase chain;
        std::vector<std::string> claimed;   // sources already assigned (a mouse_move claims both halves)
        for (size_t s = 0; s < steps.size(); ++s) {
            AssignmentCase step = parseAssignment(steps[s], where + ", step #" + std::to_string(s + 1));
            for (const std::string& src : { step.source, step.oppositeSource }) {
                if (src.empty()) continue;
                if (std::find(claimed.begin(), claimed.end(), src) != claimed.end())
                    fail(where, "source '" + src + "' assigned twice: the second would overwrite the first");
                claimed.push_back(src);
            }
            chain.label += (chain.label.empty() ? "" : ", ") + step.label;
            chain.steps.push_back(std::move(step));
        }
        chains.push_back(std::move(chain));
    }
    return chains;
}

}
