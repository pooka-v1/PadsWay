#include "E2ECases.h"     // first: defines NOMINMAX before any <windows.h>
#include "E2EMapping.h"
#include "e2e_checks.h"
#include <catch2/catch_amalgamated.hpp>

// ─── Fase 1 — Mapeador (Normal mode) and Fase 2 — Perfiles: every row of
// PadsWayE2E/cases/assignments.json, one at a time: assign -> save -> press -> check -> undo, then
// the source must be back to its shipped output (proves the undo too). Each row is its own section,
// named after its label, so a single one runs with  "[mapeador]" -c "a -> r2"  (or "[perfiles]").

using E2ECases::AssignmentCase;
using E2ECases::TargetKind;

namespace {

struct PassthroughSample {
    const char*  name;
    GamepadState press;   // pressed on the fake DS4 and expected, unchanged, on the virtual pad
};

void assignHalfAxis(MappingModel& model, const AssignmentCase& c) {
    if (c.kind == TargetKind::MouseMove) {   // whole axis: both halves at once
        E2EMapping::assignMouseMoveAxis(model, c.source, c.mouseAxis, c.mouseSpeed);
        return;
    }
    HalfAxisAction ha;
    switch (c.kind) {
    case TargetKind::Virtual: ha = E2EMapping::halfAxisToVirtual(c.virtualTarget); break;
    case TargetKind::Trigger: ha = E2EMapping::halfAxisToTrigger(c.virtualTarget); break;
    default:                  ha = E2EMapping::halfAxisFromAction(c.action);        break;
    }
    E2EMapping::assignHalfAxis(model, c.source, ha);
}

void assignTriggerSource(MappingModel& model, const AssignmentCase& c) {
    ButtonAction act;
    switch (c.kind) {
    case TargetKind::Virtual: act = E2EMapping::triggerToVirtual(c.virtualTarget); break;
    case TargetKind::Trigger: act = E2EMapping::triggerToTrigger(c.virtualTarget); break;
    default:                  act = c.action;                                       break;
    }
    E2EMapping::assignTriggerSource(model, c.source, act);
}

void assign(MappingModel& model, const AssignmentCase& c) {
    if (c.sourceKind == E2ECases::SourceKind::HalfAxis) { assignHalfAxis(model, c);      return; }
    if (c.sourceKind == E2ECases::SourceKind::Trigger)  { assignTriggerSource(model, c); return; }
    switch (c.kind) {
    case TargetKind::Virtual: E2EMapping::assignVirtual(model, c.source, c.virtualTarget); break;
    case TargetKind::Trigger: E2EMapping::assignTrigger(model, c.source, c.virtualTarget); break;
    default:                  E2EMapping::assignAction(model, c.source, c.action);         break;
    }
}

void checkAssigned(const AssignmentCase& c) {
    switch (c.kind) {
    case TargetKind::Virtual:
    case TargetKind::Trigger:
        checkPressGives(c.sourcePress, c.expectedOutput);
        break;
    case TargetKind::Keyboard:
    case TargetKind::MouseClick:
        checkPressInjects(c.sourcePress, c.injectedOnPress, c.injectedOnRelease);
        break;
    case TargetKind::Macro:
        checkHoldPlaysOnceMacro(c.sourcePress, c.macroPlays, c.macroMinMs, c.macroMaxMs);
        break;
    case TargetKind::Bot:
        checkPressTogglesTestBot(c.sourcePress);
        break;
    case TargetKind::MouseMove:
        checkHoldMovesMouse(c.sourcePress, c.sourceDxSign, c.sourceDySign);
        checkHoldMovesMouse(c.oppositePress, -c.sourceDxSign, -c.sourceDySign);
        break;
    }
}

// After the undo, as shipped: every source gives itself — both halves for a whole-axis assignment.
// In profile mode the profile must also be gone from the engine.
void checkBackToShipped(const AssignmentCase& c, E2EMapping::SaveMode mode) {
    if (mode == E2EMapping::SaveMode::Profile) CHECK(harness().engine().getActiveProfileName().empty());
    checkPressGives(c.sourcePress, c.sourcePress);
    if (!c.oppositeSource.empty()) checkPressGives(c.oppositePress, c.oppositePress);
}

void requireBotLoaded(const AssignmentCase& c) {
    if (c.kind != TargetKind::Bot) return;
    INFO("TestBot.dll is in the sandbox but the engine didn't load it - see the engine log");
    REQUIRE(E2EMapping::isBotLoaded(c.action.name));
}

// Opens the editor, applies every assignment, saves once: one row = one step, a chain = several.
void assignAndSave(const std::vector<AssignmentCase>& steps, E2EMapping::SaveMode mode) {
    MappingModel model = E2EMapping::openEditor(mode);
    for (const AssignmentCase& step : steps) assign(model, step);
    std::string saveError;
    const bool saved = E2EMapping::saveAssignment(model, mode, saveError);
    INFO(saveError);
    REQUIRE(saved);
}

void runAssignmentCase(const AssignmentCase& c, E2EMapping::SaveMode mode) {
    requireBotLoaded(c);
    {
        E2EMapping::ScopedAssignment undo(mode);
        assignAndSave({ c }, mode);
        checkAssigned(c);
    }
    checkBackToShipped(c, mode);
}

void runChain(const E2ECases::ChainCase& chain, E2EMapping::SaveMode mode) {
    for (const AssignmentCase& step : chain.steps) requireBotLoaded(step);
    {
        E2EMapping::ScopedAssignment undo(mode);
        assignAndSave(chain.steps, mode);
        for (const AssignmentCase& step : chain.steps) {
            INFO("pressing " << step.source << " (expects: " << step.label << ")");
            checkAssigned(step);
        }
    }
    for (const AssignmentCase& step : chain.steps)
        checkBackToShipped(step, mode);
}

// Loaded once for the whole run; a malformed file throws here and fails the test with the reason.
const std::vector<AssignmentCase>& allCases() {
    static const std::vector<AssignmentCase> cases = E2ECases::load(E2ECases::assignmentsFile());
    return cases;
}

const std::vector<E2ECases::ChainCase>& allChains() {
    static const std::vector<E2ECases::ChainCase> chains = E2ECases::loadChains(E2ECases::assignmentsFile());
    return chains;
}

} // namespace

TEST_CASE("Mapeador assignments from cases/assignments.json", "[e2e][mapeador]") {
    const AssignmentCase& c = GENERATE_REF(from_range(allCases()));
    DYNAMIC_SECTION(c.label) { runAssignmentCase(c, E2EMapping::SaveMode::Normal); }
}

// ─── Chains: several assignments from the "chains" array active at the same time (a -> b plus
// b -> y). Each source, pressed alone, must give exactly its own target — a gives b, never y — so a
// mapping stage that re-reads its own output as physical input (or mixes up physical and virtual
// names) shows up here and not in the one-assignment-at-a-time cases above.
TEST_CASE("Mapeador chained assignments from cases/assignments.json", "[e2e][mapeador][chain]") {
    const E2ECases::ChainCase& chain = GENERATE_REF(from_range(allChains()));
    DYNAMIC_SECTION(chain.label) { runChain(chain, E2EMapping::SaveMode::Normal); }
}

// ─── Fase 2 — Perfiles: the same rows and chains, stored as a game profile (only the diff against
// controllers.json) and applied by making it the active profile, as the Perfiles tab does.
// Undo = no active profile + file deleted; the source must then be back to controllers.json.

// Precondition: with no profile the pad is exactly what controllers.json says, and a profile that
// changes nothing (just a name) keeps it that way — so every difference the cases below see comes
// from the assignment, not from the profile machinery itself.
TEST_CASE("Perfiles: no profile, or an empty one, leaves controllers.json as is", "[e2e][perfiles]") {
    const PassthroughSample samples[] = {
        { "Cross -> A",        with([](GamepadState& s) { s.btnA = true; }) },
        { "Dpad up",           with([](GamepadState& s) { s.dpadUp = true; }) },
        { "L2 full",           with([](GamepadState& s) { s.triggerL = 1.0f; }) },
        { "Left stick right",  with([](GamepadState& s) { s.leftX = 1.0f; }) },
    };
    auto checkShipped = [&samples]() {
        for (const auto& sample : samples) {
            CAPTURE(sample.name);
            checkPressGives(sample.press, sample.press);
        }
    };

    REQUIRE(harness().engine().getActiveProfileName().empty());
    checkShipped();
    {
        E2EMapping::ScopedAssignment undo(E2EMapping::SaveMode::Profile);
        assignAndSave({}, E2EMapping::SaveMode::Profile);
        checkShipped();
    }
    CHECK(harness().engine().getActiveProfileName().empty());
    checkShipped();
}

TEST_CASE("Perfiles assignments from cases/assignments.json", "[e2e][perfiles]") {
    const AssignmentCase& c = GENERATE_REF(from_range(allCases()));
    DYNAMIC_SECTION(c.label) { runAssignmentCase(c, E2EMapping::SaveMode::Profile); }
}

TEST_CASE("Perfiles chained assignments from cases/assignments.json", "[e2e][perfiles][chain]") {
    const E2ECases::ChainCase& chain = GENERATE_REF(from_range(allChains()));
    DYNAMIC_SECTION(chain.label) { runChain(chain, E2EMapping::SaveMode::Profile); }
}
