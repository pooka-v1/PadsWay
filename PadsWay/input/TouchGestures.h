#pragma once
#include <string>
#include <cmath>

// ─── Movimiento (Gestos) — classification — see ARCHITECTURE.md "Touchpad" -> "Movimiento" ──
//
// Pure, hardware-free classification of a completed touch session (finger down -> up) into one
// of the 12 discrete gesture ids from MappingEditor.cpp's kGestureIcons catalog. Deliberately
// mirrors TouchZones.h's shape: no D3D11/HWND/PadEngine dependency, testable in isolation.
//
// dx/dy inputs are the SAME physical-unit deltas HIDInputSource::logTouchSession() already
// computes for the [TOUCH][sess] harness (raw sensor units, i.e. normalized delta * maxX/maxY) —
// not a normalized [0,1] traversal of the whole surface. Classification is entirely relative to
// where each session started, never to an absolute edge/center: a swipe that starts already
// inside the pad and ends short of any edge still classifies correctly, and a pinch that can
// never physically reach the center (fingers block each other) still reads as pinch_close/open
// from the shrinking gap alone.
//
// The 2 twist gestures ("twist_up_down"/"twist_down_up") are NOT handled here — they're a
// continuous rotation signal, not a one-shot release classification (see ARCHITECTURE.md).

// Angle convention: 0deg = up (dy<0, since Y grows downward), increasing clockwise through
// right (90), down (180), left (270) — matches kGestureIcons' row-1 order exactly.
inline float touchGestureAngleDeg(float dx, float dy) {
    float deg = std::atan2(dx, -dy) * (180.0f / 3.14159265358979323846f);
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

// Cardinals get a narrower acceptance window than diagonals, instead of splitting the circle into
// 8 even 45° sectors. Natural thumb swipes are imprecise and tend to already favor whichever axis
// (X or Y) dominates the motion, so an even split left diagonals hard to actually land on — a
// swipe aimed at "up_right" easily reads dx/dy skewed enough (e.g. 300/-100) to fall in "right"'s
// 45° slice instead (see SESSION_CONTEXT.md 2026/08/23, "las diagonales cuestan"). Cardinals keep
// +-kCardinalHalfWidthDeg around their exact angle (30° wide total); diagonals get the rest (60°
// wide total) — tunable by feel like kGestureMinDist, not derived from the harness yet.
constexpr float kCardinalHalfWidthDeg = 15.0f;

// Classifies a completed 1-finger session into one of the 8 linear gesture ids, or "" if the
// displacement doesn't clear minDist.
inline std::string classifyLinearGesture(float dx, float dy, float minDist) {
    static const char* kCardinalIds[4] = { "up", "right", "down", "left" };
    static const char* kDiagonalIds[4] = { "up_right", "down_right", "down_left", "up_left" };

    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < minDist) return "";

    float angleDeg = touchGestureAngleDeg(dx, dy);  // [0,360)
    for (int c = 0; c < 4; ++c) {
        float center = c * 90.0f;
        float diff = std::fabs(angleDeg - center);
        if (diff > 180.0f) diff = 360.0f - diff;  // wrap (e.g. 350 vs 0 is really 10 apart)
        if (diff <= kCardinalHalfWidthDeg) return kCardinalIds[c];
    }
    int diagIdx = static_cast<int>(angleDeg / 90.0f) % 4;
    return kDiagonalIds[diagIdx];
}

// Classifies a completed 2-finger session into parallel_up/parallel_down/pinch_close/pinch_open,
// or "" if it doesn't match any (below threshold, or a diagonal/mismatched pair — excluded from
// the catalog by design, see ARCHITECTURE.md "Excluida diagonal con 2 dedos").
// f1x0/f2x0 (normalized [0,1] starting X) are only used to tell which finger started on the left
// vs the right, so pinch direction (closing vs opening) doesn't depend on which touch ID happened
// to be finger 1 vs finger 2.
inline std::string classifyTwoFingerGesture(float f1dx, float f1dy, float f2dx, float f2dy,
                                             float f1x0, float f2x0, float minDist) {
    float dist1 = std::sqrt(f1dx * f1dx + f1dy * f1dy);
    float dist2 = std::sqrt(f2dx * f2dx + f2dy * f2dy);
    if (dist1 < minDist || dist2 < minDist) return "";

    // Decide parallel-vs-pinch from how the fingers move RELATIVE to each other, not from each
    // finger's own dominant axis in isolation — a real 2-finger swipe rarely tracks a straight
    // line, and pinch specifically fights finger-on-finger friction (see SESSION_CONTEXT.md
    // 2026/08/23: requiring each finger to independently be "horizontal-dominant" meant pinch
    // almost never qualified — any Y wobble pushed a finger into the "vertical" bucket instead).
    // commonDy = how much the pair moved together vertically (parallel signal). spreadDx = how
    // much the horizontal gap between them changed (pinch signal, >0 growing / <0 shrinking).
    // Whichever signal is bigger wins — no separate per-finger dominance test.
    float leftDx  = (f1x0 <= f2x0) ? f1dx : f2dx;
    float rightDx = (f1x0 <= f2x0) ? f2dx : f1dx;
    float leftDy  = (f1x0 <= f2x0) ? f1dy : f2dy;
    float rightDy = (f1x0 <= f2x0) ? f2dy : f1dy;

    float commonDy = (leftDy + rightDy) * 0.5f;
    float spreadDx = rightDx - leftDx;

    if (std::fabs(commonDy) >= std::fabs(spreadDx)) {
        if ((f1dy < 0.0f) != (f2dy < 0.0f)) return "";  // fingers disagree on vertical direction
        return (commonDy < 0.0f) ? "parallel_up" : "parallel_down";
    }
    if (spreadDx < 0.0f) return "pinch_close";
    if (spreadDx > 0.0f) return "pinch_open";
    return "";
}
