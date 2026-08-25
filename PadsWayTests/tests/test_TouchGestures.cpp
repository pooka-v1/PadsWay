#include "input/TouchGestures.h"        // project header first, see CLAUDE.md
#include <catch2/catch_amalgamated.hpp>

// ─── classifyLinearGesture() — 8-way, asymmetric sectors, Y grows downward ───
// See TouchGestures.h's file comment: 0deg=up, increasing clockwise through right/down/left,
// matching kGestureIcons' row-1 order in MappingEditor.cpp. Cardinals get a narrower window
// (+-15deg) than diagonals (the remaining 60deg between two cardinals) — see kCardinalHalfWidthDeg.

TEST_CASE("classifyLinearGesture recognizes all 8 directions", "[TouchGestures]") {
    CHECK(classifyLinearGesture(0.0f,   -300.0f, 200.0f) == "up");
    CHECK(classifyLinearGesture(300.0f, -300.0f, 200.0f) == "up_right");
    CHECK(classifyLinearGesture(300.0f,  0.0f,   200.0f) == "right");
    CHECK(classifyLinearGesture(300.0f,  300.0f, 200.0f) == "down_right");
    CHECK(classifyLinearGesture(0.0f,    300.0f, 200.0f) == "down");
    CHECK(classifyLinearGesture(-300.0f, 300.0f, 200.0f) == "down_left");
    CHECK(classifyLinearGesture(-300.0f, 0.0f,   200.0f) == "left");
    CHECK(classifyLinearGesture(-300.0f, -300.0f, 200.0f) == "up_left");
}

TEST_CASE("classifyLinearGesture gives diagonals a wider, more forgiving window than cardinals",
          "[TouchGestures]") {
    // A real thumb swipe aimed at "up_right" rarely lands on an exact 1:1 dx:dy ratio — it tends
    // to be skewed toward whichever axis already dominates. dx=300/dy=-100 (angle ~71.6°) used to
    // fall inside "right"'s even 45° slice; with the asymmetric split it correctly reads as
    // up_right (only within 15° of exact "right" counts as cardinal now).
    CHECK(classifyLinearGesture(300.0f, -100.0f, 200.0f) == "up_right");
    CHECK(classifyLinearGesture(100.0f, -300.0f, 200.0f) == "up_right");
    // Still cleanly cardinal well inside the +-15deg window.
    CHECK(classifyLinearGesture(300.0f, -50.0f, 200.0f) == "right");
}

TEST_CASE("classifyLinearGesture rejects displacement below minDist", "[TouchGestures]") {
    CHECK(classifyLinearGesture(50.0f, 0.0f, 200.0f) == "");
    CHECK(classifyLinearGesture(0.0f,  0.0f, 200.0f) == "");
}

TEST_CASE("classifyLinearGesture's aspectRatio compensates for a wider-than-tall pad",
          "[TouchGestures]") {
    // Bug found with real hardware 2026/08/26: on the DS4 (maxX=1919, maxY=942 — much wider than
    // tall), "up"/"down" attempts kept reading as a diagonal even though left/right and all 4
    // diagonals were fine. Root cause: without correction, a swipe with a small, natural sideways
    // wobble (dx) produces a much bigger angular deviation when the intentional motion is vertical
    // (dy is capped by the short axis) than when it's horizontal (dx has much more room on the
    // long axis) — the same raw wobble simply "costs" more degrees against a shorter dy. Passing
    // the real aspectRatio (maxX/maxY) rescales dy so both axes are measured as a fraction of their
    // own available travel, matching the user's framing: the finger should have to travel roughly
    // the same FRACTION of the pad's width as of its height, not the same raw distance.
    constexpr float kDs4AspectRatio = 1919.0f / 942.0f;  // ~2.04

    // dx=150 wobble against dy=-400 (up): without correction this is >15deg off vertical and
    // used to fall through to the diagonal bucket ("up_right"); with the real aspect ratio it
    // reads as a clean "up".
    CHECK(classifyLinearGesture(150.0f, -400.0f, 200.0f) == "up_right");  // old behavior, ratio=1
    CHECK(classifyLinearGesture(150.0f, -400.0f, 200.0f, kDs4AspectRatio) == "up");

    // A normal-sized right swipe (dx uses a realistic chunk of the long axis, same wobble
    // magnitude as above) stays cleanly "right" either way — the correction only matters when the
    // intentional motion is itself short relative to its own axis, which a swipe along the long
    // axis rarely is. NOTE: rescaling dy up to fix "up" is not free for a SHORT right/left swipe —
    // it makes the fixed dy wobble count for proportionally more against a small dx too (that's
    // the same physics working in both directions), so a timid right swipe with the same raw
    // numbers as the up case above (dx=400, dy=150) would also drift toward "up_right" under the
    // real ratio. That's expected, not a regression: production swipes along the long axis
    // naturally cover much more of it than 400/1919, exactly like real "up" swipes rarely cover
    // more than a fraction of the short 942-unit axis.
    CHECK(classifyLinearGesture(900.0f, -100.0f, 200.0f) == "right");
    CHECK(classifyLinearGesture(900.0f, -100.0f, 200.0f, kDs4AspectRatio) == "right");
}

TEST_CASE("classifyLinearGesture is relative to the swipe's own start, not the pad's edges",
          "[TouchGestures]") {
    // Same physical displacement should classify the same whether the swipe happened to start
    // near the center or already deep inside the pad — see the user's physical-constraint note
    // in SESSION_CONTEXT.md 2026/08/23 (a finger rarely travels edge-to-edge).
    CHECK(classifyLinearGesture(300.0f, 0.0f, 200.0f) ==
          classifyLinearGesture(300.0f, 0.0f, 200.0f));
}

// ─── classifyTwoFingerGesture() — parallel vertical / horizontal pinch ───────

TEST_CASE("classifyTwoFingerGesture recognizes parallel_up/parallel_down", "[TouchGestures]") {
    // Both fingers move mostly vertically, same sign — x0 doesn't matter for parallel.
    CHECK(classifyTwoFingerGesture(10.0f, -300.0f, -10.0f, -280.0f, 0.3f, 0.6f, 200.0f)
          == "parallel_up");
    CHECK(classifyTwoFingerGesture(10.0f, 300.0f, -10.0f, 280.0f, 0.3f, 0.6f, 200.0f)
          == "parallel_down");
}

TEST_CASE("classifyTwoFingerGesture recognizes pinch_close/pinch_open regardless of touch-id order",
          "[TouchGestures]") {
    // f1 starts on the left (x0=0.2), f2 on the right (x0=0.8): both move toward each other.
    CHECK(classifyTwoFingerGesture(250.0f, 10.0f, -260.0f, -5.0f, 0.2f, 0.8f, 200.0f)
          == "pinch_close");
    // Both move away from each other.
    CHECK(classifyTwoFingerGesture(-250.0f, 0.0f, 260.0f, 5.0f, 0.2f, 0.8f, 200.0f)
          == "pinch_open");
    // Same physical gesture, but finger 1 happens to be the one on the right this time (touch ID
    // assignment shouldn't matter, only x0 ordering) — still pinch_close.
    CHECK(classifyTwoFingerGesture(-260.0f, -5.0f, 250.0f, 10.0f, 0.8f, 0.2f, 200.0f)
          == "pinch_close");
}

TEST_CASE("classifyTwoFingerGesture rejects below-threshold pairs", "[TouchGestures]") {
    // Finger 2 doesn't move enough.
    CHECK(classifyTwoFingerGesture(300.0f, 0.0f, 5.0f, 0.0f, 0.2f, 0.8f, 200.0f) == "");
}

TEST_CASE("classifyTwoFingerGesture recognizes twist_up_down/twist_down_up regardless of touch-id "
          "order", "[TouchGestures]") {
    // Naming matches strings_es/en.json's "touch_gesture_twist_up_down/down_up" display strings:
    // "up_down" = right finger up / left finger down, "down_up" = right finger down / left up.

    // f1 on the left (x0=0.3) moves down, f2 on the right (x0=0.6) moves up: right-up/left-down.
    CHECK(classifyTwoFingerGesture(0.0f, 300.0f, 0.0f, -300.0f, 0.3f, 0.6f, 200.0f)
          == "twist_up_down");
    // f1 on the left moves up, f2 on the right moves down: right-down/left-up.
    CHECK(classifyTwoFingerGesture(0.0f, -300.0f, 0.0f, 300.0f, 0.3f, 0.6f, 200.0f)
          == "twist_down_up");
    // Same physical gesture as the first check (right-up/left-down), but finger 1 happens to be
    // the one on the right this time (touch ID assignment shouldn't matter, only x0 ordering) —
    // still twist_up_down.
    CHECK(classifyTwoFingerGesture(0.0f, -300.0f, 0.0f, 300.0f, 0.6f, 0.3f, 200.0f)
          == "twist_up_down");
}

TEST_CASE("classifyTwoFingerGesture still recognizes twist with realistic horizontal jitter",
          "[TouchGestures]") {
    // Regression test for a bug found with real hardware 2026/08/25: a genuine twist's opposite
    // vertical deltas (leftDy/rightDy) used to be averaged with SIGN (commonDy), which cancels to
    // ~0 no matter how far each finger moved — so any small, natural horizontal drift (a real
    // thumb never moves perfectly straight) beat that near-zero value and the gesture fell through
    // to pinch_close/pinch_open instead. Same vertical motion as the first check above (300 each
    // way), but with a small, honest horizontal jitter (well under the 200 minDist on its own)
    // that used to be enough to hijack the classification.
    CHECK(classifyTwoFingerGesture(50.0f, -300.0f, -40.0f, 300.0f, 0.3f, 0.6f, 200.0f)
          == "twist_down_up");
}

TEST_CASE("classifyTwoFingerGesture picks whichever signal (common vertical vs spread horizontal) "
          "is stronger, even for an imprecise/diagonal-ish pair", "[TouchGestures]") {
    // One finger swipes purely up, the other purely right — genuinely diagonal 2-finger motion,
    // excluded as its own gesture by design (ARCHITECTURE.md "Excluida diagonal con 2 dedos").
    // There's no dedicated outcome for this, so rather than silently firing nothing (the old
    // per-finger-dominance design did — see SESSION_CONTEXT.md 2026/08/23, "los horizontales
    // nada"), whichever signal is bigger wins: here the horizontal spread (300) beats the common
    // vertical motion (150), so it reads as a pinch.
    CHECK(classifyTwoFingerGesture(0.0f, -300.0f, 300.0f, 0.0f, 0.2f, 0.8f, 200.0f) == "pinch_open");
}
