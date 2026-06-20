#define NOMINMAX
#include "ui/MacroStep.h"
#include <catch2/catch_amalgamated.hpp>

// =============================================================================
// analogToken
// =============================================================================

TEST_CASE("analogToken: left stick cardinal directions", "[MacroStep][analogToken]") {
    CHECK(analogToken(false,  0.0f,  1.0f) == "LAX0+LAY1");
    CHECK(analogToken(false,  1.0f,  0.0f) == "LAX1+LAY0");
    CHECK(analogToken(false,  0.0f, -1.0f) == "LAX0+LAY-1");
    CHECK(analogToken(false, -1.0f,  0.0f) == "LAX-1+LAY0");
}

TEST_CASE("analogToken: right stick cardinal directions", "[MacroStep][analogToken]") {
    CHECK(analogToken(true, 0.0f,  1.0f) == "RAX0+RAY1");
    CHECK(analogToken(true, 1.0f,  0.0f) == "RAX1+RAY0");
    CHECK(analogToken(true, 0.0f, -1.0f) == "RAX0+RAY-1");
}

TEST_CASE("analogToken: diagonal 0.71 values", "[MacroStep][analogToken]") {
    CHECK(analogToken(false,  0.71f,  0.71f) == "LAX0.71+LAY0.71");
    CHECK(analogToken(false, -0.71f, -0.71f) == "LAX-0.71+LAY-0.71");
    CHECK(analogToken(true,   0.71f,  0.71f) == "RAX0.71+RAY0.71");
}

TEST_CASE("analogToken: zero vector", "[MacroStep][analogToken]") {
    CHECK(analogToken(false, 0.0f, 0.0f) == "LAX0+LAY0");
    CHECK(analogToken(true,  0.0f, 0.0f) == "RAX0+RAY0");
}

// =============================================================================
// tokenToImageName
// =============================================================================

TEST_CASE("tokenToImageName: button tokens", "[MacroStep][tokenToImageName]") {
    CHECK(tokenToImageName("A")  == "PressA");
    CHECK(tokenToImageName("B")  == "PressB");
    CHECK(tokenToImageName("X")  == "PressX");
    CHECK(tokenToImageName("Y")  == "PressY");
    CHECK(tokenToImageName("L1") == "PressL1");
    CHECK(tokenToImageName("R1") == "PressR1");
    CHECK(tokenToImageName("L2") == "PressL2");
    CHECK(tokenToImageName("R2") == "PressR2");
    CHECK(tokenToImageName("ST") == "PressStart");
    CHECK(tokenToImageName("SE") == "PressSelect");
    CHECK(tokenToImageName("HO") == "PressHome");
}

TEST_CASE("tokenToImageName: dpad tokens", "[MacroStep][tokenToImageName]") {
    CHECK(tokenToImageName("CU")  == "CrossMoveUp");
    CHECK(tokenToImageName("CR")  == "CrossMoveRight");
    CHECK(tokenToImageName("CD")  == "CrossMoveDown");
    CHECK(tokenToImageName("CL")  == "CrossMoveLeft");
    CHECK(tokenToImageName("CUR") == "CrossMoveUpRight");
    CHECK(tokenToImageName("CDR") == "CrossMoveDownRight");
    CHECK(tokenToImageName("CDL") == "CrossMoveDownLeft");
    CHECK(tokenToImageName("CUL") == "CrossMoveUpLeft");
}

TEST_CASE("tokenToImageName: analog compound tokens (left stick)", "[MacroStep][tokenToImageName]") {
    CHECK(tokenToImageName("LAX0+LAY1")       == "AnalogicMoveUp");
    CHECK(tokenToImageName("LAX1+LAY0")       == "AnalogicMoveRight");
    CHECK(tokenToImageName("LAX0+LAY-1")      == "AnalogicMoveDown");
    CHECK(tokenToImageName("LAX-1+LAY0")      == "AnalogicMoveLeft");
    CHECK(tokenToImageName("LAX0.71+LAY0.71") == "AnalogicMoveUpRight");
}

TEST_CASE("tokenToImageName: analog compound tokens (right stick)", "[MacroStep][tokenToImageName]") {
    CHECK(tokenToImageName("RAX0+RAY1")         == "AnalogicMoveUp");
    CHECK(tokenToImageName("RAX0.71+RAY0.71")   == "AnalogicMoveUpRight");
    CHECK(tokenToImageName("RAX-0.71+RAY-0.71") == "AnalogicMoveDownLeft");
}

TEST_CASE("tokenToImageName: unknown token returns empty string", "[MacroStep][tokenToImageName]") {
    CHECK(tokenToImageName("UNKNOWN") == "");
    CHECK(tokenToImageName("")        == "");
    CHECK(tokenToImageName("a")       == ""); // case-sensitive
    CHECK(tokenToImageName("LAX0")    == ""); // incomplete compound
}

// =============================================================================
// RepeatSpec::toSuffix + isActive
// =============================================================================

TEST_CASE("RepeatSpec::toSuffix: None mode", "[MacroStep][RepeatSpec]") {
    RepeatSpec r;
    CHECK(r.toSuffix()  == "");
    CHECK(r.isActive()  == false);
}

TEST_CASE("RepeatSpec::toSuffix: Duration mode", "[MacroStep][RepeatSpec]") {
    RepeatSpec r;
    r.mode = RepeatSpec::Mode::Duration;
    r.ms   = 5000;
    CHECK(r.toSuffix() == "*5000");
    CHECK(r.isActive() == true);
}

TEST_CASE("RepeatSpec::toSuffix: CountInDuration mode", "[MacroStep][RepeatSpec]") {
    RepeatSpec r;
    r.mode  = RepeatSpec::Mode::CountInDuration;
    r.ms    = 1000;
    r.count = 10;
    CHECK(r.toSuffix() == "*1000/10");
    CHECK(r.isActive() == true);
}

TEST_CASE("RepeatSpec::toSuffix: HoldButton mode", "[MacroStep][RepeatSpec]") {
    RepeatSpec r;
    r.mode = RepeatSpec::Mode::HoldButton;
    CHECK(r.toSuffix() == "*UP");
    CHECK(r.isActive() == true);
}

TEST_CASE("RepeatSpec::toSuffix: Toggle mode", "[MacroStep][RepeatSpec]") {
    RepeatSpec r;
    r.mode = RepeatSpec::Mode::Toggle;
    CHECK(r.toSuffix() == "*DO");
    CHECK(r.isActive() == true);
}

// =============================================================================
// MacroStepItem::toDsl — Wait
// =============================================================================

TEST_CASE("toDsl Wait: emits milliseconds as integer string", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Wait;
    step.waitMs = 200;
    CHECK(step.toDsl() == "200");
}

TEST_CASE("toDsl Wait: zero ms", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Wait;
    step.waitMs = 0;
    CHECK(step.toDsl() == "0");
}

// =============================================================================
// MacroStepItem::toDsl — Press
// =============================================================================

TEST_CASE("toDsl Press: single token, no hold, no repeat", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Press;
    step.tokens = {"A"};
    CHECK(step.toDsl() == "A");
}

TEST_CASE("toDsl Press: multiple tokens, no hold, no repeat", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Press;
    step.tokens = {"A", "B"};
    CHECK(step.toDsl() == "A+B");
}

TEST_CASE("toDsl Press: three tokens joined by +", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Press;
    step.tokens = {"A", "CD", "L1"};
    CHECK(step.toDsl() == "A+CD+L1");
}

TEST_CASE("toDsl Press: single token with hold time", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Press;
    step.tokens = {"A"};
    step.holdMs = 500;
    CHECK(step.toDsl() == "A=500");
}

TEST_CASE("toDsl Press: multiple tokens with hold time", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind   = MacroStepItem::Kind::Press;
    step.tokens = {"A", "B"};
    step.holdMs = 300;
    CHECK(step.toDsl() == "A+B=300");
}

TEST_CASE("toDsl Press: single token + Duration repeat — no parens", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::Press;
    step.tokens      = {"A"};
    step.repeat.mode = RepeatSpec::Mode::Duration;
    step.repeat.ms   = 5000;
    CHECK(step.toDsl() == "A*5000");
}

TEST_CASE("toDsl Press: single token + Toggle repeat — no parens", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::Press;
    step.tokens      = {"X"};
    step.repeat.mode = RepeatSpec::Mode::Toggle;
    CHECK(step.toDsl() == "X*DO");
}

TEST_CASE("toDsl Press: single token + HoldButton repeat — no parens", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::Press;
    step.tokens      = {"A"};
    step.repeat.mode = RepeatSpec::Mode::HoldButton;
    CHECK(step.toDsl() == "A*UP");
}

TEST_CASE("toDsl Press: multiple tokens + Duration repeat — parens added", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::Press;
    step.tokens      = {"A", "B"};
    step.repeat.mode = RepeatSpec::Mode::Duration;
    step.repeat.ms   = 3000;
    CHECK(step.toDsl() == "(A+B)*3000");
}

TEST_CASE("toDsl Press: single token + hold + HoldButton repeat — parens added", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::Press;
    step.tokens      = {"L1"};
    step.holdMs      = 100;
    step.repeat.mode = RepeatSpec::Mode::HoldButton;
    CHECK(step.toDsl() == "(L1=100)*UP");
}

TEST_CASE("toDsl Press: multiple tokens + hold + CountInDuration repeat", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind         = MacroStepItem::Kind::Press;
    step.tokens       = {"A", "CD"};
    step.holdMs       = 50;
    step.repeat.mode  = RepeatSpec::Mode::CountInDuration;
    step.repeat.ms    = 1000;
    step.repeat.count = 10;
    CHECK(step.toDsl() == "(A+CD=50)*1000/10");
}

// =============================================================================
// MacroStepItem::toDsl — Group
// =============================================================================

TEST_CASE("toDsl Group: no repeat emits children joined by ', ' without parens", "[MacroStep][toDsl]") {
    MacroStepItem c1, c2;
    c1.kind = MacroStepItem::Kind::Press; c1.tokens = {"A"};
    c2.kind = MacroStepItem::Kind::Press; c2.tokens = {"B"};

    MacroStepItem grp;
    grp.kind     = MacroStepItem::Kind::Group;
    grp.children = {c1, c2};
    CHECK(grp.toDsl() == "A, B");
}

TEST_CASE("toDsl Group: with Duration repeat wraps in parens", "[MacroStep][toDsl]") {
    MacroStepItem c1, c2;
    c1.kind = MacroStepItem::Kind::Press; c1.tokens = {"A"};
    c2.kind = MacroStepItem::Kind::Wait;  c2.waitMs  = 100;

    MacroStepItem grp;
    grp.kind        = MacroStepItem::Kind::Group;
    grp.children    = {c1, c2};
    grp.repeat.mode = RepeatSpec::Mode::Duration;
    grp.repeat.ms   = 5000;
    CHECK(grp.toDsl() == "(A, 100)*5000");
}

TEST_CASE("toDsl Group: with HoldButton repeat", "[MacroStep][toDsl]") {
    MacroStepItem c1, c2, c3;
    c1.kind = MacroStepItem::Kind::Press; c1.tokens = {"CD"};
    c2.kind = MacroStepItem::Kind::Wait;  c2.waitMs  = 40;
    c3.kind = MacroStepItem::Kind::Press; c3.tokens = {"A"};

    MacroStepItem grp;
    grp.kind        = MacroStepItem::Kind::Group;
    grp.children    = {c1, c2, c3};
    grp.repeat.mode = RepeatSpec::Mode::HoldButton;
    CHECK(grp.toDsl() == "(CD, 40, A)*UP");
}

TEST_CASE("toDsl Group: with Toggle repeat", "[MacroStep][toDsl]") {
    MacroStepItem c1;
    c1.kind = MacroStepItem::Kind::Press; c1.tokens = {"X"};

    MacroStepItem grp;
    grp.kind        = MacroStepItem::Kind::Group;
    grp.children    = {c1};
    grp.repeat.mode = RepeatSpec::Mode::Toggle;
    CHECK(grp.toDsl() == "(X)*DO");
}

// =============================================================================
// MacroStepItem::toDsl — MacroRef
// =============================================================================

TEST_CASE("toDsl MacroRef: returns expandedDsl when no repeat", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::MacroRef;
    step.expandedDsl = "A, 100, B";
    CHECK(step.toDsl() == "A, 100, B");
}

TEST_CASE("toDsl MacroRef: empty expandedDsl returns empty", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::MacroRef;
    step.expandedDsl = "";
    CHECK(step.toDsl() == "");
}

TEST_CASE("toDsl MacroRef: with HoldButton repeat wraps in parens", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::MacroRef;
    step.expandedDsl = "A, 100, B";
    step.repeat.mode = RepeatSpec::Mode::HoldButton;
    CHECK(step.toDsl() == "(A, 100, B)*UP");
}

TEST_CASE("toDsl MacroRef: with Duration repeat wraps in parens", "[MacroStep][toDsl]") {
    MacroStepItem step;
    step.kind        = MacroStepItem::Kind::MacroRef;
    step.expandedDsl = "CD, 40, CR, 40, CU, 40, A";
    step.repeat.mode = RepeatSpec::Mode::Duration;
    step.repeat.ms   = 3000;
    CHECK(step.toDsl() == "(CD, 40, CR, 40, CU, 40, A)*3000");
}

// =============================================================================
// tryParseSimpleDsl — basic
// =============================================================================

TEST_CASE("tryParseSimpleDsl: empty string returns empty result", "[MacroStep][parse]") {
    CHECK(tryParseSimpleDsl("").empty());
}

TEST_CASE("tryParseSimpleDsl: single wait", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("200");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind   == MacroStepItem::Kind::Wait);
    CHECK(steps[0].waitMs == 200);
}

TEST_CASE("tryParseSimpleDsl: zero wait", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("0");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind   == MacroStepItem::Kind::Wait);
    CHECK(steps[0].waitMs == 0);
}

TEST_CASE("tryParseSimpleDsl: single press token", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind == MacroStepItem::Kind::Press);
    REQUIRE(steps[0].tokens.size() == 1);
    CHECK(steps[0].tokens[0]       == "A");
    CHECK(steps[0].holdMs          == 0);
    CHECK(steps[0].repeat.isActive() == false);
}

TEST_CASE("tryParseSimpleDsl: combo A+B", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A+B");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind == MacroStepItem::Kind::Press);
    REQUIRE(steps[0].tokens.size() == 2);
    CHECK(steps[0].tokens[0] == "A");
    CHECK(steps[0].tokens[1] == "B");
}

TEST_CASE("tryParseSimpleDsl: press with explicit hold time", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A=500");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind      == MacroStepItem::Kind::Press);
    CHECK(steps[0].tokens[0] == "A");
    CHECK(steps[0].holdMs    == 500);
}

TEST_CASE("tryParseSimpleDsl: combo with hold time", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A+CD=100");
    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].tokens.size() == 2);
    CHECK(steps[0].tokens[0] == "A");
    CHECK(steps[0].tokens[1] == "CD");
    CHECK(steps[0].holdMs    == 100);
}

// =============================================================================
// tryParseSimpleDsl — sequence
// =============================================================================

TEST_CASE("tryParseSimpleDsl: press-wait-press sequence", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A, 200, B");
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].kind      == MacroStepItem::Kind::Press);
    CHECK(steps[0].tokens[0] == "A");
    CHECK(steps[1].kind      == MacroStepItem::Kind::Wait);
    CHECK(steps[1].waitMs    == 200);
    CHECK(steps[2].kind      == MacroStepItem::Kind::Press);
    CHECK(steps[2].tokens[0] == "B");
}

TEST_CASE("tryParseSimpleDsl: QCF-like sequence (quarter circle forward)", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("CD, 40, CDR, 40, CR, 40, A");
    REQUIRE(steps.size() == 7);
    CHECK(steps[0].tokens[0] == "CD");
    CHECK(steps[1].waitMs    == 40);
    CHECK(steps[2].tokens[0] == "CDR");
    CHECK(steps[4].tokens[0] == "CR");
    CHECK(steps[6].tokens[0] == "A");
}

// =============================================================================
// tryParseSimpleDsl — repeat specs
// =============================================================================

TEST_CASE("tryParseSimpleDsl: token with Duration repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A*5000");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind         == MacroStepItem::Kind::Press);
    CHECK(steps[0].tokens[0]   == "A");
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::Duration);
    CHECK(steps[0].repeat.ms   == 5000);
}

TEST_CASE("tryParseSimpleDsl: token with HoldButton repeat (*UP)", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A*UP");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::HoldButton);
}

TEST_CASE("tryParseSimpleDsl: token with Toggle repeat (*DO)", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A*DO");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::Toggle);
}

TEST_CASE("tryParseSimpleDsl: token with CountInDuration repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A*1000/10");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].repeat.mode  == RepeatSpec::Mode::CountInDuration);
    CHECK(steps[0].repeat.ms    == 1000);
    CHECK(steps[0].repeat.count == 10);
}

TEST_CASE("tryParseSimpleDsl: parenthesized combo with Duration repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(A+B)*5000");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind == MacroStepItem::Kind::Press);
    REQUIRE(steps[0].tokens.size() == 2);
    CHECK(steps[0].tokens[0]   == "A");
    CHECK(steps[0].tokens[1]   == "B");
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::Duration);
    CHECK(steps[0].repeat.ms   == 5000);
}

TEST_CASE("tryParseSimpleDsl: parenthesized combo + hold + HoldButton repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(A+B=100)*UP");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind        == MacroStepItem::Kind::Press);
    CHECK(steps[0].holdMs      == 100);
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::HoldButton);
    REQUIRE(steps[0].tokens.size() == 2);
}

// =============================================================================
// tryParseSimpleDsl — Group
// =============================================================================

TEST_CASE("tryParseSimpleDsl: group with Duration repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(A, 100, B)*5000");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind        == MacroStepItem::Kind::Group);
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::Duration);
    CHECK(steps[0].repeat.ms   == 5000);
    REQUIRE(steps[0].children.size() == 3);
    CHECK(steps[0].children[0].tokens[0] == "A");
    CHECK(steps[0].children[1].waitMs    == 100);
    CHECK(steps[0].children[2].tokens[0] == "B");
}

TEST_CASE("tryParseSimpleDsl: group without repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(A, B)");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind              == MacroStepItem::Kind::Group);
    CHECK(steps[0].repeat.isActive() == false);
    REQUIRE(steps[0].children.size() == 2);
}

TEST_CASE("tryParseSimpleDsl: group with HoldButton repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(CD, 40, A)*UP");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind        == MacroStepItem::Kind::Group);
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::HoldButton);
    REQUIRE(steps[0].children.size() == 3);
}

TEST_CASE("tryParseSimpleDsl: group with Toggle repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(A, 200, B)*DO");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind        == MacroStepItem::Kind::Group);
    CHECK(steps[0].repeat.mode == RepeatSpec::Mode::Toggle);
    REQUIRE(steps[0].children.size() == 3);
}

TEST_CASE("tryParseSimpleDsl: group with CountInDuration repeat", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("(A, 100)*1000/10");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind         == MacroStepItem::Kind::Group);
    CHECK(steps[0].repeat.mode  == RepeatSpec::Mode::CountInDuration);
    CHECK(steps[0].repeat.ms    == 1000);
    CHECK(steps[0].repeat.count == 10);
}

// =============================================================================
// tryParseSimpleDsl — analog axis merging
// =============================================================================

TEST_CASE("tryParseSimpleDsl: adjacent LA axes merged into compound token", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("LAX0.71+LAY0.71");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].kind == MacroStepItem::Kind::Press);
    REQUIRE(steps[0].tokens.size() == 1);
    CHECK(steps[0].tokens[0] == "LAX0.71+LAY0.71");
}

TEST_CASE("tryParseSimpleDsl: adjacent RA axes merged into compound token", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("RAX1+RAY0");
    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].tokens.size() == 1);
    CHECK(steps[0].tokens[0] == "RAX1+RAY0");
}

TEST_CASE("tryParseSimpleDsl: same LA axis repeated is NOT merged", "[MacroStep][parse]") {
    // LAX + LAX: same axis — merging requires X+Y or Y+X pair
    auto steps = tryParseSimpleDsl("LAX1+LAX0");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].tokens.size() == 2);
}

TEST_CASE("tryParseSimpleDsl: button + analog axes — button stays, axes merge", "[MacroStep][parse]") {
    auto steps = tryParseSimpleDsl("A+LAX1+LAY0");
    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].tokens.size() == 2);
    CHECK(steps[0].tokens[0] == "A");
    CHECK(steps[0].tokens[1] == "LAX1+LAY0");
}

// =============================================================================
// tryParseSimpleDsl — malformed input
// =============================================================================

TEST_CASE("tryParseSimpleDsl: unmatched open paren returns empty", "[MacroStep][parse]") {
    CHECK(tryParseSimpleDsl("(A, B").empty());
}

TEST_CASE("tryParseSimpleDsl: empty parens returns empty", "[MacroStep][parse]") {
    CHECK(tryParseSimpleDsl("()").empty());
}

TEST_CASE("tryParseSimpleDsl: invalid repeat suffix returns empty", "[MacroStep][parse]") {
    CHECK(tryParseSimpleDsl("A*INVALID").empty());
}

TEST_CASE("tryParseSimpleDsl: star with no tokens returns empty", "[MacroStep][parse]") {
    CHECK(tryParseSimpleDsl("*5000").empty());
}

// =============================================================================
// Roundtrip: parse -> toDsl must match canonical DSL
// =============================================================================

static std::string roundtrip(const std::string& dsl) {
    auto steps = tryParseSimpleDsl(dsl);
    std::string out;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) out += ", ";
        out += steps[i].toDsl();
    }
    return out;
}

TEST_CASE("Roundtrip: single press token", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A") == "A");
}

TEST_CASE("Roundtrip: wait", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("200") == "200");
}

TEST_CASE("Roundtrip: combo with hold", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A+CD=100") == "A+CD=100");
}

TEST_CASE("Roundtrip: single + Duration repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A*5000") == "A*5000");
}

TEST_CASE("Roundtrip: single + HoldButton repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A*UP") == "A*UP");
}

TEST_CASE("Roundtrip: single + Toggle repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A*DO") == "A*DO");
}

TEST_CASE("Roundtrip: single + CountInDuration repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A*1000/10") == "A*1000/10");
}

TEST_CASE("Roundtrip: combo + Duration repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("(A+B)*5000") == "(A+B)*5000");
}

TEST_CASE("Roundtrip: combo + hold + HoldButton repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("(A+CD=50)*UP") == "(A+CD=50)*UP");
}

TEST_CASE("Roundtrip: group + Duration repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("(A, 100, B)*5000") == "(A, 100, B)*5000");
}

TEST_CASE("Roundtrip: group + HoldButton repeat", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("(CD, 40, A)*UP") == "(CD, 40, A)*UP");
}

TEST_CASE("Roundtrip: sequence press-wait-press", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("A, 200, B") == "A, 200, B");
}

TEST_CASE("Roundtrip: analog compound token preserves merge", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("LAX0.71+LAY0.71") == "LAX0.71+LAY0.71");
}

TEST_CASE("Roundtrip: QCF sequence", "[MacroStep][roundtrip]") {
    CHECK(roundtrip("CD, 40, CDR, 40, CR, 40, A") == "CD, 40, CDR, 40, CR, 40, A");
}
