#include "E2ECases.h"     // first: defines NOMINMAX before any <windows.h>
#include "E2EMapping.h"
#include "e2e_checks.h"
#include <catch2/catch_amalgamated.hpp>

// ─── Fase 1 — Mapeador (Normal mode): every row of PadsWayE2E/cases/assignments.json, one at a
// time: assign -> save -> press -> check -> undo, then the source must be back to its shipped
// output (proves the undo too). Each row is its own section, named after its label, so a single
// one runs with  -c "a -> r2".

using E2ECases::AssignmentCase;
using E2ECases::TargetKind;

namespace {

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

void assign(MappingModel& model, const AssignmentCase& c) {
    if (c.halfAxisSource) { assignHalfAxis(model, c); return; }
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
void checkBackToShipped(const AssignmentCase& c) {
    checkPressGives(c.sourcePress, c.sourcePress);
    if (!c.oppositeSource.empty()) checkPressGives(c.oppositePress, c.oppositePress);
}

} // namespace

TEST_CASE("Mapeador assignments from cases/assignments.json", "[e2e][mapeador]") {
    // Loaded once for the whole run; a malformed file throws here and fails the test with the reason.
    static const std::vector<AssignmentCase> cases = E2ECases::load(E2ECases::assignmentsFile());
    const AssignmentCase& c = GENERATE_REF(from_range(cases));

    DYNAMIC_SECTION(c.label) {
        if (c.kind == TargetKind::Bot) {
            INFO("TestBot.dll is in the sandbox but the engine didn't load it - see the engine log");
            REQUIRE(E2EMapping::isBotLoaded(c.action.name));
        }
        {
            E2EMapping::ScopedAssignment undo;
            MappingModel model = E2EMapping::openMapeador();
            assign(model, c);
            E2EMapping::saveNormalMode(model);
            checkAssigned(c);
        }
        checkBackToShipped(c);
    }
}

// ─── Chains: several assignments from the "chains" array active at the same time (a -> b plus
// b -> y). Each source, pressed alone, must give exactly its own target — a gives b, never y — so a
// mapping stage that re-reads its own output as physical input (or mixes up physical and virtual
// names) shows up here and not in the one-assignment-at-a-time cases above.
TEST_CASE("Mapeador chained assignments from cases/assignments.json", "[e2e][mapeador][chain]") {
    static const std::vector<E2ECases::ChainCase> chains = E2ECases::loadChains(E2ECases::assignmentsFile());
    const E2ECases::ChainCase& chain = GENERATE_REF(from_range(chains));

    DYNAMIC_SECTION(chain.label) {
        for (const AssignmentCase& step : chain.steps)
            if (step.kind == TargetKind::Bot) {
                INFO("TestBot.dll is in the sandbox but the engine didn't load it - see the engine log");
                REQUIRE(E2EMapping::isBotLoaded(step.action.name));
            }
        {
            E2EMapping::ScopedAssignment undo;
            MappingModel model = E2EMapping::openMapeador();
            for (const AssignmentCase& step : chain.steps) assign(model, step);
            E2EMapping::saveNormalMode(model);
            for (const AssignmentCase& step : chain.steps) {
                INFO("pressing " << step.source << " (expects: " << step.label << ")");
                checkAssigned(step);
            }
        }
        for (const AssignmentCase& step : chain.steps)
            checkBackToShipped(step);
    }
}
