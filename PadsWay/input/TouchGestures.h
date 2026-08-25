#pragma once
#include <string>
#include <cmath>

// ─── Movimiento (Gestos) — classification — see ARCHITECTURE.md "Touchpad" -> "Movimiento" ──
//
// Pure, hardware-free classification of a completed touch session (finger down -> up) into one
// of the 14 discrete gesture ids from MappingEditor.cpp's kGestureIcons catalog. Deliberately
// mirrors TouchZones.h's shape: no D3D11/HWND/PadEngine dependency, testable in isolation.
//
// dx/dy inputs are the SAME physical-unit deltas HIDInputSource::logTouchSession() already
// computes for the [TOUCH][sess] harness (raw sensor units, i.e. normalized delta * maxX/maxY) —
// not a normalized [0,1] traversal of the whole surface. Classification is entirely relative to
// where each session started, never to an absolute edge/center: a swipe that starts already
// inside the pad and ends short of any edge still classifies correctly, and a pinch that can
// never physically reach the center (fingers block each other) still reads as pinch_close/open
// from the shrinking gap alone. Same principle covers twist: it doesn't matter how far short of
// a full rotation the fingers get, only that they disagree on vertical direction.

// Angle convention: 0deg = up (dy<0, since Y grows downward), increasing clockwise through
// right (90), down (180), left (270) — matches kGestureIcons' row-1 order exactly.
//
// aspectRatio (maxX/maxY, default 1 for a square surface) rescales dy before computing the angle.
// The DS4 touchpad is much wider than tall (maxX=1919, maxY=942, ratio ~2.04) — a "pure up" swipe
// has far less raw-unit room to travel before the thumb runs off the (short) top/bottom edge than
// a "pure right" swipe has before running off the (long) left/right edge, so the same small,
// natural sideways wobble produces a much bigger angular deviation for up/down than the equivalent
// wobble produces for left/right — up/down kept misreading as a diagonal (found with real
// hardware 2026/08/26; the user's framing: the finger should have to travel roughly the same
// FRACTION of the pad's width as of its height, not the same raw distance). Scaling dy by
// aspectRatio measures both axes as a fraction of their own available travel instead of raw units.
inline float touchGestureAngleDeg(float dx, float dy, float aspectRatio = 1.0f) {
    float deg = std::atan2(dx, -dy * aspectRatio) * (180.0f / 3.14159265358979323846f);
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
// displacement doesn't clear minDist. aspectRatio is forwarded to touchGestureAngleDeg() as-is —
// see its comment. minDist is still checked against the raw (un-rescaled) distance: this is about
// direction only, not about how far the finger had to travel to count at all.
inline std::string classifyLinearGesture(float dx, float dy, float minDist,
                                          float aspectRatio = 1.0f) {
    static const char* kCardinalIds[4] = { "up", "right", "down", "left" };
    static const char* kDiagonalIds[4] = { "up_right", "down_right", "down_left", "up_left" };

    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < minDist) return "";

    float angleDeg = touchGestureAngleDeg(dx, dy, aspectRatio);  // [0,360)
    for (int c = 0; c < 4; ++c) {
        float center = c * 90.0f;
        float diff = std::fabs(angleDeg - center);
        if (diff > 180.0f) diff = 360.0f - diff;  // wrap (e.g. 350 vs 0 is really 10 apart)
        if (diff <= kCardinalHalfWidthDeg) return kCardinalIds[c];
    }
    int diagIdx = static_cast<int>(angleDeg / 90.0f) % 4;
    return kDiagonalIds[diagIdx];
}

// Classifies a completed 2-finger session into parallel_up/parallel_down/pinch_close/pinch_open/
// twist_up_down/twist_down_up, or "" if it doesn't match any (below threshold, or a
// diagonal/mismatched pair — excluded from the catalog by design, see ARCHITECTURE.md "Excluida
// diagonal con 2 dedos").
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
    // verticalActivity: how much vertical motion happened, regardless of whether the two fingers
    // agree (parallel) or disagree (twist) on direction. commonDy alone can't be used to decide
    // whether to even enter the vertical branch below — for a twist, leftDy/rightDy have opposite
    // signs and average out to ~0 no matter how far each finger moved, so any small, natural
    // horizontal drift (spreadDx != 0) used to win the comparison and misclassify a twist as a
    // pinch (bug found 2026/08/25 with real hardware: twist rarely fired, pinch_close fired almost
    // every attempt — see SESSION_CONTEXT.md). Summing absolute values instead doesn't cancel.
    float verticalActivity = (std::fabs(leftDy) + std::fabs(rightDy)) * 0.5f;

    if (verticalActivity >= std::fabs(spreadDx)) {
        if ((f1dy < 0.0f) != (f2dy < 0.0f)) {
            // Fingers disagree on vertical direction — not a parallel swipe, it's a twist (one
            // finger up, the other down). rightDy is already normalized by x0 (which finger is on
            // the right), so its sign alone decides which of the 2 twist ids this is, regardless of
            // which touch ID (f1/f2) happened to be on which side. Naming matches the existing
            // display strings (strings_es/en.json "touch_gesture_twist_up_down/down_up"): "up_down"
            // = right finger up / left down, "down_up" = right finger down / left up.
            return (rightDy < 0.0f) ? "twist_up_down" : "twist_down_up";
        }
        return (commonDy < 0.0f) ? "parallel_up" : "parallel_down";
    }
    if (spreadDx < 0.0f) return "pinch_close";
    if (spreadDx > 0.0f) return "pinch_open";
    return "";
}
