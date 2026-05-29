#include <catch2/catch_amalgamated.hpp>
#include "MacroParser.h"

TEST_CASE("MacroParser: Empty string throws", "[MacroParser]") {
    Macro macro;
    bool threw = false;
    try { MacroParser::parse("", macro); } catch (const std::runtime_error&) { threw = true; } catch (...) {}
    CHECK(threw);
}

TEST_CASE("MacroParser: Simple button sequence", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("A, B, X, Y", macro);

    REQUIRE(macro.getSteps().size() == 4);
    CHECK(macro.getSteps()[0].effect.btnA == true);
    CHECK(macro.getSteps()[1].effect.btnB == true);
    CHECK(macro.getSteps()[2].effect.btnX == true);
    CHECK(macro.getSteps()[3].effect.btnY == true);

    REQUIRE(macro.getMode()    == MacroRepeatMode::Once);
    REQUIRE(macro.getTotalMs() == 0);    // totalMs unused in Once mode
    REQUIRE(macro.getCycleMs() == 800);  // 4 items * 200ms default step
}

TEST_CASE("MacroParser: Combo with explicit hold", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("A + B=1000", macro);

    REQUIRE(macro.getSteps().size() == 1);
    CHECK(macro.getSteps()[0].effect.btnA == true);
    CHECK(macro.getSteps()[0].effect.btnB == true);
    CHECK(macro.getSteps()[0].holdMs      == 1000);

    REQUIRE(macro.getMode()    == MacroRepeatMode::Once);
    REQUIRE(macro.getTotalMs() == 0);     // Once mode
    REQUIRE(macro.getCycleMs() == 1000);  // slotMs = holdMs when '=' is used
}

TEST_CASE("MacroParser: Analog axis values", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("LAX0.5, LAY-0.5, RAX1.0, RAY-1.0", macro);

    REQUIRE(macro.getSteps().size() == 4);
    CHECK(macro.getSteps()[0].effect.leftX  == 0.5f);
    CHECK(macro.getSteps()[1].effect.leftY  == -0.5f);
    CHECK(macro.getSteps()[2].effect.rightX == 1.0f);
    CHECK(macro.getSteps()[3].effect.rightY == -1.0f);

    REQUIRE(macro.getMode()    == MacroRepeatMode::Once);
    REQUIRE(macro.getTotalMs() == 0);
    REQUIRE(macro.getCycleMs() == 800);
}

TEST_CASE("MacroParser: Timed repeat group", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("(A, B)*5000", macro);

    REQUIRE(macro.getSteps().size() > 0);
    REQUIRE(macro.getMode()    == MacroRepeatMode::TimedMs);
    REQUIRE(macro.getTotalMs() == 5000);
    REQUIRE(macro.getCycleMs() == 400);  // 2 items * 200ms default step
}

TEST_CASE("MacroParser: Timed repeat group N/M", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("(A, B)*10000/30", macro);

    REQUIRE(macro.getSteps().size() > 0);
    REQUIRE(macro.getMode()    == MacroRepeatMode::TimedMs);
    REQUIRE(macro.getTotalMs() == 10000);
    REQUIRE(macro.getCycleMs() == 333);  // 10000 / 30 integer division
}

TEST_CASE("MacroParser: Repeat until release", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("(A, B)*UP", macro);

    REQUIRE(macro.getSteps().size() > 0);
    REQUIRE(macro.getMode() == MacroRepeatMode::UntilRelease);
}

TEST_CASE("MacroParser: Toggle repeat", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("(A, B)*DO", macro);

    REQUIRE(macro.getSteps().size() > 0);
    REQUIRE(macro.getMode() == MacroRepeatMode::Toggle);
}

TEST_CASE("MacroParser: Bare number is a wait with no steps", "[MacroParser]") {
    Macro macro;
    MacroParser::parse("500", macro);

    // A bare number is pure duration — no button/axis effect, no compiled step
    REQUIRE(macro.getSteps().size() == 0);
    REQUIRE(macro.getMode()    == MacroRepeatMode::Once);
    REQUIRE(macro.getCycleMs() == 500);
}

TEST_CASE("MacroParser: Unknown token throws", "[MacroParser]") {
    Macro macro;
    bool threw = false;
    try { MacroParser::parse("INVALID_TOKEN", macro); } catch (const std::runtime_error&) { threw = true; } catch (...) {}
    CHECK(threw);
}

TEST_CASE("MacroParser: Unknown repeat keyword throws", "[MacroParser]") {
    Macro macro;
    bool threw = false;
    try { MacroParser::parse("(A, B)*INVALID", macro); } catch (const std::runtime_error&) { threw = true; } catch (...) {}
    CHECK(threw);
}

TEST_CASE("MacroParser: Negative hold is rejected", "[MacroParser]") {
    // REQUIRE_THROWS_AS causes SIGSEGV in this specific path under MSVC+Catch2.
    // Manual try-catch is equivalent and stable.
    Macro macro;
    bool threw = false;
    try {
        MacroParser::parse("A=-100", macro);
    } catch (const std::runtime_error&) {
        threw = true;
    } catch (...) {}
    CHECK(threw);
}

TEST_CASE("MacroParser: Zero total time is valid, not a throw", "[MacroParser]") {
    Macro macro;
    // N=0 is valid — parser only requires M>0
    REQUIRE_NOTHROW(MacroParser::parse("(A, B)*0/30", macro));
    REQUIRE(macro.getMode()    == MacroRepeatMode::TimedMs);
    REQUIRE(macro.getTotalMs() == 0);
}
