#pragma once
#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include "input/ControllerConfig.h"
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// E2ECases — assignment cases read from PadsWayE2E/cases/assignments.json, one row per
// "source -> target", with the expected result worked out here from the target itself (no expected
// output written in the file, except the macro's). File format and name vocabulary: see
// PadsWayE2E/cases/README.md — keep it in sync with the tables in E2ECases.cpp.
// ---------------------------------------------------------------------------
namespace E2ECases {

enum class TargetKind { Virtual, Trigger, Keyboard, MouseClick, Macro, Bot, MouseMove };

struct AssignmentCase {
    std::string  label;          // "a -> r2", "l3 -> keyboard shift+k"... report name, -c filter
    std::string  source;         // physical short name ("a", "dpad_up", "left_x_pos")
    GamepadState sourcePress;    // `source` fully pressed on the fake DS4 — and, as shipped, its output
    bool         halfAxisSource = false;   // stick half-axis: assigned via axisActionEdits, not buttonEdits
    TargetKind   kind = TargetKind::Virtual;

    std::string  virtualTarget;  // Virtual / Trigger: target short name ("b", "dpad_up", "r2")
    GamepadState expectedOutput; // Virtual / Trigger: what the virtual pad must show, nothing else

    ButtonAction action;         // Keyboard / MouseClick / Macro / Bot: what the Mapeador stores

    std::vector<InjectedInput> injectedOnPress;    // Keyboard / MouseClick
    std::vector<InjectedInput> injectedOnRelease;

    GamepadState macroPlays;     // Macro (Once mode): what it shows while playing, and for how long
    int          macroMinMs = 0;
    int          macroMaxMs = 0;

    // MouseMove — whole axis, as the Mapeador assigns it: the source half AND its opposite half.
    std::string  mouseAxis;              // "mouse_x" / "mouse_y"
    float        mouseSpeed = 15.0f;
    std::string  oppositeSource;         // other half of the source axis ("left_x_neg" for "left_x_pos")
    GamepadState oppositePress;
    int          sourceDxSign = 0;       // expected cursor direction pressing `source` (-1/0/+1, screen
    int          sourceDySign = 0;       // coordinates: +dy = down); pressing the opposite half negates both
};

// Several assignments active AT ONCE, e.g. a -> b plus b -> y. Each step must still give exactly
// its own target: pressing a gives b, never y (no chaining through another assignment), and each
// source's own output stays gone. Catches physical and virtual names getting crossed in the engine.
struct ChainCase {
    std::string                 label;   // the steps' labels joined: "a -> b, b -> y"
    std::vector<AssignmentCase> steps;
};

std::filesystem::path assignmentsFile();

// Parse and validate the file's "cases" / "chains" arrays. Throw std::runtime_error naming the
// offending row (and the unknown name, if that's the problem) so a typo fails the run instead of
// skipping a case.
std::vector<AssignmentCase> load(const std::filesystem::path& file);
std::vector<ChainCase>      loadChains(const std::filesystem::path& file);

}
