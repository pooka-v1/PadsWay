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

void assign(MappingModel& model, const AssignmentCase& c) {
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
    }
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
        checkPressGives(c.sourcePress, c.sourcePress);   // shipped DS4: every source gives itself
    }
}
