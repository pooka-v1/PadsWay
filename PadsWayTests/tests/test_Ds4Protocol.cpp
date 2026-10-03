#ifndef NOMINMAX
#define NOMINMAX   // pulls in <windows.h> via HIDDevice.h — see CLAUDE.md, "Tests"
#endif
#include "input/Ds4Protocol.h"
#include "input/SonyCrc.h"
#include <catch2/catch_amalgamated.hpp>
#include <deque>

// Ds4Protocol must make a DS4 over Bluetooth look exactly like the same pad over USB
// (ARCHITECTURE.md, "Protocolos de mando" → contrato de decode()). The fixture is a real 0x11
// report from temp/sony_bt_probe/probe.log (line 701), CRC included.

namespace {

const std::vector<BYTE> kRealBtReport = {
    0x11, 0xC0, 0x00, 0x80, 0x85, 0x7D, 0x7B, 0x08, 0x00, 0x50, 0x00, 0x00, 0x05, 0xF6, 0x0E, 0xF2,
    0xFF, 0x02, 0x00, 0x06, 0x00, 0xF5, 0xFF, 0xCA, 0x20, 0x7F, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x01, 0x7A, 0x82, 0xF2, 0x16, 0x13, 0x83, 0xC0, 0xA0, 0x12, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x0C, 0x4E, 0x6B,
};

// What HIDDevice::normalizeAxis() gives for a DS4 axis (descriptor range 0..255).
float hidNormalized(int raw) { return raw / 255.0f * 2.0f - 1.0f; }

// The real report with USB byte usbIndex (as found in the USB report) set to value, CRC redone.
std::vector<BYTE> btReportWith(std::initializer_list<std::pair<ULONG, BYTE>> usbBytes) {
    std::vector<BYTE> r = kRealBtReport;
    for (auto [usbIndex, value] : usbBytes) r[usbIndex + Ds4Protocol::kBtHeaderLen] = value;
    sony_crc::store(sony_crc::kSeedInput, r.data(), r.size());
    return r;
}

// ReadFile hands back InputReportByteLength (547 on the DS4 BT) whatever the report's real size.
std::vector<BYTE> paddedLikeTheOs(std::vector<BYTE> r) {
    r.resize(547, 0xEE);   // leftovers, not zeros — decode() must ignore them
    return r;
}

// Stands in for GenericHidProtocol: records what reached it.
class RecordingFallback final : public ControllerProtocol {
public:
    int  calls      = 0;
    BYTE lastId     = 0;
    bool returnFlag = true;
    bool decode(const BYTE* report, ULONG, RawHIDState& out) override {
        ++calls;
        lastId = report[0];
        out.valid = true;
        return returnFlag;
    }
};

// Scripted device: answers reads from a queue of reports, then times out.
class FakeChannel final : public HidChannel {
public:
    USHORT vid            = Ds4Protocol::kSonyVendorId;
    bool   featureWorks   = true;
    bool   disconnected   = false;
    int    featureCalls   = 0;
    BYTE   lastFeatureId  = 0;
    int    reads          = 0;
    std::deque<std::vector<BYTE>> queued;

    ReadResult read(int) override {
        ++reads;
        if (disconnected) return ReadResult::Disconnected;
        if (queued.empty()) return ReadResult::Timeout;
        m_buf = queued.front();
        queued.pop_front();
        return ReadResult::Ok;
    }
    const std::vector<BYTE>& reportBuf()     const override { return m_buf; }
    ULONG                    lastBytesRead() const override { return static_cast<ULONG>(m_buf.size()); }
    USHORT vendorId() const override { return vid; }
    bool   canWrite() const override { return false; }   // the DS4 must not need it
    bool   sendOutputReport(const BYTE*, ULONG) override { return false; }
    bool   setFeature(const BYTE*, ULONG)       override { return false; }
    bool   getFeature(BYTE reportId, std::vector<BYTE>& out) override {
        ++featureCalls;
        lastFeatureId = reportId;
        if (!featureWorks) return false;
        out.assign(41, 0);
        out[0] = reportId;
        return true;
    }

private:
    std::vector<BYTE> m_buf;
};

std::vector<BYTE> shortReport() {
    return { 0x01, 0x80, 0x84, 0x7D, 0x7B, 0x08, 0x00, 0x00, 0x00, 0x00 };
}

} // namespace

// ── CRC ───────────────────────────────────────────────────────────────────────

TEST_CASE("Sony CRC accepts a real DS4 BT report and rejects a changed one", "[Ds4Protocol][SonyCrc]") {
    CHECK(sony_crc::check(sony_crc::kSeedInput, kRealBtReport.data(), kRealBtReport.size()));
    CHECK_FALSE(sony_crc::check(sony_crc::kSeedOutput, kRealBtReport.data(), kRealBtReport.size()));

    std::vector<BYTE> changed = kRealBtReport;
    changed[3] ^= 0x01;
    CHECK_FALSE(sony_crc::check(sony_crc::kSeedInput, changed.data(), changed.size()));

    sony_crc::store(sony_crc::kSeedInput, changed.data(), changed.size());
    CHECK(sony_crc::check(sony_crc::kSeedInput, changed.data(), changed.size()));
}

// ── decode(): BT 0x11 → USB layout ────────────────────────────────────────────

TEST_CASE("Ds4Protocol decodes a real BT 0x11 report like HidP reads the USB one", "[Ds4Protocol]") {
    auto fallback = std::make_unique<RecordingFallback>();
    auto* fb = fallback.get();
    Ds4Protocol proto(std::move(fallback));

    const auto report = paddedLikeTheOs(kRealBtReport);
    RawHIDState s;
    REQUIRE(proto.decode(report.data(), static_cast<ULONG>(report.size()), s));
    CHECK(fb->calls == 0);

    CHECK(s.valid);
    CHECK(s.buttonsValid);
    CHECK(s.buttonMask == 0);
    CHECK(s.hat == 0xFFFFFFFF);   // 8 = neutral

    CHECK(s.hasAxis(RawAxis::X));
    CHECK(s.hasAxis(RawAxis::Y));
    CHECK(s.hasAxis(RawAxis::Z));
    CHECK(s.hasAxis(RawAxis::Rx));
    CHECK(s.hasAxis(RawAxis::Ry));
    CHECK(s.hasAxis(RawAxis::Rz));
    CHECK_FALSE(s.hasAxis(RawAxis::Brake));
    CHECK_FALSE(s.hasAxis(RawAxis::Accel));

    CHECK(s.axisX  == Catch::Approx(hidNormalized(0x80)));   // LX
    CHECK(s.axisY  == Catch::Approx(hidNormalized(0x85)));   // LY
    CHECK(s.axisZ  == Catch::Approx(hidNormalized(0x7D)));   // RX
    CHECK(s.axisRz == Catch::Approx(hidNormalized(0x7B)));   // RY
    CHECK(s.axisRx == Catch::Approx(-1.0f));                 // L2 released
    CHECK(s.axisRy == Catch::Approx(-1.0f));                 // R2 released
}

TEST_CASE("Ds4Protocol rebuilds the USB report in raw, without the BT padding", "[Ds4Protocol]") {
    Ds4Protocol proto(std::make_unique<RecordingFallback>());
    const auto report = paddedLikeTheOs(kRealBtReport);
    RawHIDState s;
    REQUIRE(proto.decode(report.data(), static_cast<ULONG>(report.size()), s));

    // IMU/touch offsets in controllers.json are USB offsets: raw must be the USB report.
    REQUIRE(s.raw.size() == Ds4Protocol::kUsbReportLen);
    CHECK(s.raw[0] == Ds4Protocol::kUsbReportId);
    for (ULONG i = 1; i < Ds4Protocol::kUsbReportLen; ++i) {
        INFO("USB byte " << i);
        CHECK(s.raw[i] == kRealBtReport[i + Ds4Protocol::kBtHeaderLen]);
    }
}

TEST_CASE("Ds4Protocol maps BT buttons, hat and triggers to the USB button numbers", "[Ds4Protocol]") {
    Ds4Protocol proto(std::make_unique<RecordingFallback>());
    RawHIDState s;

    SECTION("face buttons are buttons 1-4 (high nibble of byte 5)") {
        const auto r = btReportWith({ { 5, 0xF8 } });   // all four, hat neutral
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == 0x000F);
        CHECK(s.hat == 0xFFFFFFFF);
    }
    SECTION("byte 6 is buttons 5-12, byte 7 bits 0-1 are PS and touch click") {
        const auto r = btReportWith({ { 5, 0x08 }, { 6, 0xFF }, { 7, 0xFF } });   // counter bits set too
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == 0x3FF0);   // buttons 5-14; the counter (bits 2-7) is not a button
    }
    SECTION("single buttons land on their own bit") {
        const auto r = btReportWith({ { 5, 0x28 }, { 6, 0x80 }, { 7, 0x02 } });   // Cross, R3, touch click
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == ((1u << 1) | (1u << 11) | (1u << 13)));
    }
    SECTION("hat directions 0-7 pass through, anything above is neutral") {
        for (BYTE dir = 0; dir <= 7; ++dir) {
            const auto r = btReportWith({ { 5, dir } });
            REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
            CHECK(s.hat == dir);
        }
        const auto r = btReportWith({ { 5, 0x0F } });
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.hat == 0xFFFFFFFF);
    }
    SECTION("triggers fully pressed read +1, like HidP over USB") {
        const auto r = btReportWith({ { 8, 0xFF }, { 9, 0xFF } });
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.axisRx == Catch::Approx(1.0f));
        CHECK(s.axisRy == Catch::Approx(1.0f));
    }
}

TEST_CASE("Ds4Protocol drops a corrupted or truncated 0x11 untouched", "[Ds4Protocol]") {
    auto fallback = std::make_unique<RecordingFallback>();
    auto* fb = fallback.get();
    Ds4Protocol proto(std::move(fallback));

    RawHIDState s;
    s.buttonMask = 0x1234;

    SECTION("bad CRC") {
        std::vector<BYTE> r = kRealBtReport;
        r[8] = 0x01;   // a button pressed, CRC not redone
        CHECK_FALSE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
    }
    SECTION("shorter than the 0x11 report") {
        CHECK_FALSE(proto.decode(kRealBtReport.data(), Ds4Protocol::kBtFullReportLen - 1, s));
    }
    CHECK(s.buttonMask == 0x1234);
    CHECK_FALSE(s.valid);
    CHECK(fb->calls == 0);   // a broken 0x11 is not handed to HidP either
}

// ── decode(): everything else goes to the fallback ────────────────────────────

TEST_CASE("Ds4Protocol hands USB and BT short reports to the fallback", "[Ds4Protocol]") {
    auto fallback = std::make_unique<RecordingFallback>();
    auto* fb = fallback.get();
    Ds4Protocol proto(std::move(fallback));

    const auto r = shortReport();
    RawHIDState s;
    CHECK(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
    CHECK(fb->calls == 1);
    CHECK(fb->lastId == 0x01);

    fb->returnFlag = false;
    CHECK_FALSE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));   // its answer is kept
}

TEST_CASE("Ds4Protocol without a fallback rejects reports it doesn't own", "[Ds4Protocol]") {
    Ds4Protocol proto(nullptr);
    const auto r = shortReport();
    RawHIDState s;
    CHECK_FALSE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
    CHECK_FALSE(proto.decode(nullptr, 10, s));
}

// ── enableFullMode() ──────────────────────────────────────────────────────────

TEST_CASE("Ds4Protocol::enableFullMode over USB does nothing", "[Ds4Protocol]") {
    Ds4Protocol proto(nullptr);
    FakeChannel ch;
    CHECK(proto.enableFullMode(ch, HidTransport::Usb));
    CHECK(ch.featureCalls == 0);
    CHECK(ch.reads == 0);
}

TEST_CASE("Ds4Protocol::enableFullMode over BT reads feature 0x05 and waits for 0x11", "[Ds4Protocol]") {
    Ds4Protocol proto(nullptr);
    FakeChannel ch;

    SECTION("confirmed when the first 0x11 arrives, after some short reports") {
        ch.queued = { shortReport(), shortReport(), paddedLikeTheOs(kRealBtReport) };
        CHECK(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.featureCalls == 1);
        CHECK(ch.lastFeatureId == Ds4Protocol::kActivationFeatureId);
        CHECK(ch.reads == 3);
    }
    SECTION("fails if the pad stays in short mode") {
        for (int i = 0; i < Ds4Protocol::kConfirmMaxReads + 5; ++i) ch.queued.push_back(shortReport());
        CHECK_FALSE(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.reads == Ds4Protocol::kConfirmMaxReads);   // bounded, never hangs
    }
    SECTION("fails if nothing arrives") {
        CHECK_FALSE(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.reads == Ds4Protocol::kConfirmMaxReads);
    }
    SECTION("fails at once if the feature read fails") {
        ch.featureWorks = false;
        CHECK_FALSE(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.reads == 0);
    }
    SECTION("fails at once if the pad disconnects") {
        ch.disconnected = true;
        CHECK_FALSE(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.reads == 1);
    }
}

TEST_CASE("Ds4Protocol::enableFullMode never touches a pad that isn't Sony", "[Ds4Protocol]") {
    Ds4Protocol proto(nullptr);
    FakeChannel ch;
    ch.vid = 0x2DC8;   // 8BitDo — e.g. a mis-edited "protocol" in controllers.json
    ch.queued = { paddedLikeTheOs(kRealBtReport) };
    CHECK_FALSE(proto.enableFullMode(ch, HidTransport::Bluetooth));
    CHECK(ch.featureCalls == 0);
    CHECK(ch.reads == 0);
}
