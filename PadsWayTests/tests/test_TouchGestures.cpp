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

TEST_CASE("classifyTwoFingerGesture rejects below-threshold or contradictory pairs", "[TouchGestures]") {
    // Finger 2 doesn't move enough.
    CHECK(classifyTwoFingerGesture(300.0f, 0.0f, 5.0f, 0.0f, 0.2f, 0.8f, 200.0f) == "");
    // Same magnitude vertical motion, opposite direction — commonDy cancels out to ~0, and even
    // though that ties/loses against spreadDx (also 0 here), the direction-agreement check still
    // catches it: not a recognized parallel pair.
    CHECK(classifyTwoFingerGesture(0.0f, -300.0f, 0.0f, 300.0f, 0.3f, 0.6f, 200.0f) == "");
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
