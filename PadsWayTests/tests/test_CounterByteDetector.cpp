#include "ui/CounterByteDetector.h"
#include <catch2/catch_amalgamated.hpp>
#include <cstdint>
#include <vector>

// The gyro wizard's Baseline capture marks an int16 offset "alive" by its peak-to-peak alone, so a
// counter byte sitting in the low half of one slipped through (DS4 over BT, sensor timestamp high
// byte at 11). These cases pin down what CounterByteDetector calls a counter.

namespace {

constexpr size_t kReportLen  = 4;
constexpr int    kFrameCount = 270;   // one Baseline capture

// Byte 0: timestamp-like, +12 per report, wrapping. Byte 1: resting jitter around 128.
// Byte 2: constant. Byte 3: battery-like, drops once mid-capture.
std::vector<uint8_t> restingReport(int frame) {
    static const int kJitter[] = { 1, -2, 0, 2, -1, 1, -1, 0, -2, 2 };
    return {
        static_cast<uint8_t>(frame * 12),
        static_cast<uint8_t>(128 + kJitter[frame % 10]),
        0x5A,
        static_cast<uint8_t>(frame < kFrameCount / 2 ? 10 : 9),
    };
}

CounterByteDetector capture() {
    CounterByteDetector detector;
    for (int f = 0; f < kFrameCount; ++f) detector.addReport(restingReport(f));
    return detector;
}

} // namespace

TEST_CASE("CounterByteDetector flags a wrapping counter as a counter", "[CounterByteDetector]") {
    REQUIRE(capture().isCounter(0));
}

TEST_CASE("CounterByteDetector leaves resting jitter, constants and battery alone", "[CounterByteDetector]") {
    const CounterByteDetector detector = capture();
    CHECK_FALSE(detector.isCounter(1));
    CHECK_FALSE(detector.isCounter(2));
    CHECK_FALSE(detector.isCounter(3));
}

TEST_CASE("CounterByteDetector flags a counter that steps backwards", "[CounterByteDetector]") {
    // Seen modulo 256, a step of +200 reads as -56 every time: still one way only.
    CounterByteDetector detector;
    for (int f = 0; f < kFrameCount; ++f)
        detector.addReport({ static_cast<uint8_t>(f * 200), 0, 0, 0 });
    REQUIRE(detector.isCounter(0));
}

TEST_CASE("CounterByteDetector needs enough steps before judging", "[CounterByteDetector]") {
    CounterByteDetector detector;
    for (int f = 0; f < CounterByteDetector::kMinMoves; ++f)   // first report only primes it
        detector.addReport({ static_cast<uint8_t>(f), 0, 0, 0 });
    CHECK_FALSE(detector.isCounter(0));
    detector.addReport({ static_cast<uint8_t>(CounterByteDetector::kMinMoves), 0, 0, 0 });
    CHECK(detector.isCounter(0));
}

TEST_CASE("CounterByteDetector starts over after reset or a new report length", "[CounterByteDetector]") {
    CounterByteDetector detector = capture();
    detector.reset();
    CHECK_FALSE(detector.isCounter(0));

    detector = capture();
    detector.addReport(std::vector<uint8_t>(kReportLen + 1, 0));
    CHECK_FALSE(detector.isCounter(0));
    CHECK_FALSE(detector.isCounter(-1));
    CHECK_FALSE(detector.isCounter(static_cast<int>(kReportLen) + 1));
}
