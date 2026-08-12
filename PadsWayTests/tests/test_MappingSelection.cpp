#include <catch2/catch_amalgamated.hpp>
#include "ui/MappingSelection.h"

// ─── advanceImuSweep() — H9 gyro/accel progressive-sweep arming ──────────────
// See MappingSelection.h's ImuSweepState/advanceImuSweep comment and
// MappingEditor.cpp's H9 gyro/accel block for the design rationale. This is the
// "rest -> sustained near-extreme for confirmSec" state machine that arms a
// gyro/accel half-axis reassignment in the Mapper — not yet verified with real
// hardware as of this test (see SESSION_CONTEXT.md), so these cover the state
// transitions in isolation first.

namespace {
    // Index order fixed throughout: {up, down, left, right, cw, ccw}.
    // Directions 0-3 are accel (isAccelDir), 4-5 are gyro.
    constexpr float kRest[6]      = {0.3f, 0.3f, 0.3f, 0.3f, 0.2f, 0.2f};
    constexpr float kArm[6]       = {0.6f, 0.6f, 0.6f, 0.6f, 0.5f, 0.5f};
    constexpr float kNearMaxFrac  = 0.9f;
    constexpr float kConfirmSec   = 0.2f;
}

TEST_CASE("advanceImuSweep stays idle when all directions are at rest", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    auto r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == -1);
    REQUIRE(r.bestDisplay == -1);
    REQUIRE(r.bestProgress == Catch::Approx(0.0f));
    for (const auto& sw : sweep) {
        CHECK(sw.confirmTimer == Catch::Approx(0.0f));
        CHECK(sw.reachedMax == false);
    }
}

TEST_CASE("advanceImuSweep accumulates progress without instantly arming", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // above kArm[0]=0.6, below near-max

    auto r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == -1);          // 0.05s < kConfirmSec (0.2s)
    REQUIRE(r.bestDisplay == 0);
    REQUIRE(r.bestProgress == Catch::Approx(0.05f));
    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.05f));
}

TEST_CASE("advanceImuSweep arms once the hold is sustained for confirmSec", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)   // 4 * 0.05s = 0.2s == kConfirmSec
        r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == 0);
}

TEST_CASE("advanceImuSweep resets progress when the reading drops back to rest", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float holding[6] = {0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    advanceImuSweep(sweep, holding, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);
    REQUIRE(sweep[0].confirmTimer > 0.0f);

    float atRest[6] = {0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // below kRest[0]=0.3
    auto r = advanceImuSweep(sweep, atRest, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);

    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));
    REQUIRE(r.armedIdx == -1);
    REQUIRE(r.bestDisplay == -1);
}

TEST_CASE("advanceImuSweep mid-swing (between rest and arm threshold) does not accumulate", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // above rest(0.3), below arm(0.6)

    auto r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);

    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));
    REQUIRE(r.bestDisplay == -1);
}

TEST_CASE("advanceImuSweep accel direction touching near-max is disqualified as positioning", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.95f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // >= kNearMaxFrac (0.9)

    auto r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);

    REQUIRE(sweep[0].reachedMax == true);
    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));
    REQUIRE(r.armedIdx == -1);
}

TEST_CASE("advanceImuSweep accel stays disqualified after dipping below near-max until it returns to rest", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};

    float touchNearMax[6] = {0.95f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    advanceImuSweep(sweep, touchNearMax, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);
    REQUIRE(sweep[0].reachedMax == true);

    // Still well above the arm threshold, but this ascent is already disqualified.
    float dipBelowNearMax[6] = {0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    for (int frame = 0; frame < 4; ++frame)
        advanceImuSweep(sweep, dipBelowNearMax, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);

    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));  // never accumulated
    REQUIRE(sweep[0].reachedMax == true);                   // still disqualified

    // Only a full return to rest clears the disqualification for the next ascent.
    float atRest[6] = {0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    advanceImuSweep(sweep, atRest, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);
    REQUIRE(sweep[0].reachedMax == false);

    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)
        r = advanceImuSweep(sweep, dipBelowNearMax, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.1f);
    REQUIRE(r.armedIdx == 0);  // now a clean ascent, arms normally
}

TEST_CASE("advanceImuSweep gyro directions have no near-max disqualification", "[MappingSelection][ImuSweep]") {
    // d=4 (cw) is a gyro direction: sustaining near/at max IS the deliberate "keep spinning"
    // gesture, unlike accel's "positioning" ambiguity — see the struct comment.
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.95f, 0.0f};  // near-max, but gyro dir

    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)   // 4 * 0.05s = 0.2s == kConfirmSec
        r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == 4);
    REQUIRE(sweep[4].reachedMax == false);  // never set for gyro directions
}

TEST_CASE("advanceImuSweep tracks all 6 directions independently", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // only direction 0 active

    advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    CHECK(sweep[0].confirmTimer > 0.0f);
    for (int d = 1; d < 6; ++d)
        CHECK(sweep[d].confirmTimer == Catch::Approx(0.0f));
}

TEST_CASE("advanceImuSweep negative magnitude never satisfies a threshold", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {-0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    auto r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));
    REQUIRE(r.bestDisplay == -1);
}

TEST_CASE("advanceImuSweep bestDisplay follows whichever direction is most advanced", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};

    // Both directions 0 and 1 start climbing together...
    float both[6] = {0.7f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f};
    auto r1 = advanceImuSweep(sweep, both, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);
    REQUIRE(r1.bestDisplay == 0);  // tie: first direction found wins

    // ...then direction 0 drops out, direction 1 keeps going and overtakes.
    float onlySecond[6] = {0.0f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f};
    auto r2 = advanceImuSweep(sweep, onlySecond, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r2.bestDisplay == 1);
    REQUIRE(r2.bestProgress == Catch::Approx(0.10f));
}

TEST_CASE("advanceImuSweep blocks left/right while pitch is pinned near max", "[MappingSelection][ImuSweep]") {
    // Reproduces the real CW/CCW bug: getting into position to spin the controller tilts it
    // back near the pitch ceiling (index 0, "up") as a side effect, and that same wrist motion
    // nudges roll (index 3, "right") past its own arm threshold. Left/right must not accumulate
    // while pitch stays pinned near max, even though roll itself never reaches its near-max.
    std::array<ImuSweepState, 6> sweep{};
    float spinningUp[6] = {0.95f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f};  // up >= nearMax, right >= arm(0.6)

    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)   // 4 * 0.05s = 0.2s == kConfirmSec
        r = advanceImuSweep(sweep, spinningUp, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == -1);
    REQUIRE(sweep[3].confirmTimer == Catch::Approx(0.0f));  // right never accumulated
    REQUIRE(sweep[0].reachedMax == true);                   // up disqualified as usual
}

TEST_CASE("advanceImuSweep lets left/right accumulate again once pitch drops below near max", "[MappingSelection][ImuSweep]") {
    // "Vuelve a posición normal → se reactiva": pitch settling back below near-max (even while
    // still above rest) must immediately unblock roll again, same frame.
    std::array<ImuSweepState, 6> sweep{};
    float spinningUp[6] = {0.95f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f};
    advanceImuSweep(sweep, spinningUp, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);
    REQUIRE(sweep[3].confirmTimer == Catch::Approx(0.0f));

    // Pitch settles back down (still above rest, just not pinned near max); right keeps holding.
    float settled[6] = {0.4f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f};
    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)
        r = advanceImuSweep(sweep, settled, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == 3);
}

TEST_CASE("advanceImuSweep blocks up/down while roll is pinned near max", "[MappingSelection][ImuSweep]") {
    // Symmetric counterpart: the wrist rotation coupling can go the other way too — roll
    // pinned near max should block pitch from arming mid-spin, same as the reverse case above.
    std::array<ImuSweepState, 6> sweep{};
    float spinningRight[6] = {0.7f, 0.0f, 0.0f, 0.95f, 0.0f, 0.0f};  // right >= nearMax, up >= arm(0.6)

    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)
        r = advanceImuSweep(sweep, spinningRight, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == -1);
    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));  // up never accumulated
    REQUIRE(sweep[3].reachedMax == true);                   // right disqualified as usual
}

TEST_CASE("advanceImuSweep lets up/down accumulate again once roll drops below near max", "[MappingSelection][ImuSweep]") {
    std::array<ImuSweepState, 6> sweep{};
    float spinningRight[6] = {0.7f, 0.0f, 0.0f, 0.95f, 0.0f, 0.0f};
    advanceImuSweep(sweep, spinningRight, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);
    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.0f));

    // Roll settles back down (still above rest, just not pinned near max); up keeps holding.
    float settled[6] = {0.7f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f};
    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)
        r = advanceImuSweep(sweep, settled, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == 0);
}

TEST_CASE("advanceImuSweep starts a fresh sweep after the caller resets the array post-arm", "[MappingSelection][ImuSweep]") {
    // Mirrors MappingEditor.cpp: m_sel.h9ImuSweep = {} once armedIdx >= 0, so the next
    // gesture requires a full new rest-to-max ascent rather than re-arming instantly.
    std::array<ImuSweepState, 6> sweep{};
    float mags[6] = {0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    ImuSweepResult r;
    for (int frame = 0; frame < 4; ++frame)
        r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);
    REQUIRE(r.armedIdx == 0);

    sweep = {};  // caller's reset after consuming the arm event
    r = advanceImuSweep(sweep, mags, kRest, kArm, kNearMaxFrac, kConfirmSec, 0.05f);

    REQUIRE(r.armedIdx == -1);
    REQUIRE(sweep[0].confirmTimer == Catch::Approx(0.05f));  // one fresh frame of progress, not re-armed
}
