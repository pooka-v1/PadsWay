#ifndef NOMINMAX
#define NOMINMAX   // pulls in <windows.h> via HIDDevice.h — see CLAUDE.md, "Tests"
#endif
#include "input/DualSenseProtocol.h"
#include "input/SonyCrc.h"
#include <catch2/catch_amalgamated.hpp>
#include <deque>

// DualSenseProtocol must make a DualSense over Bluetooth look exactly like the same pad over USB
// (ARCHITECTURE.md, "Protocolos de mando" → contrato de decode()). The fixture is a real 0x31
// report from temp/sony_bt_probe/probe.log (line 397), CRC included. What SonyProtocol does for
// every model (CRC, length, fallback, activation) is covered in depth by test_Ds4Protocol.cpp;
// here, what differs: the report ID, the 1-byte header and the USB byte positions.

namespace {

const std::vector<BYTE> kRealBtReport = {
    0x31, 0xC1, 0x82, 0x7F, 0x7E, 0x7E, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x68, 0x79, 0x6C,
    0x2C, 0xFE, 0xFF, 0xFE, 0xFF, 0x02, 0x00, 0xC1, 0xFF, 0xB8, 0x1F, 0x6A, 0x05, 0xE6, 0x23, 0x55,
    0x01, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x6C, 0x2C, 0x55, 0x01, 0x09, 0x00, 0x00, 0xEF, 0x2C, 0xA3, 0x20, 0x98, 0x5B, 0xA5,
    0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x01, 0x44, 0xC8,
};

// What HIDDevice::normalizeAxis() gives for a DualSense axis (descriptor range 0..255).
float hidNormalized(int raw) { return raw / 255.0f * 2.0f - 1.0f; }

// The real report with USB byte usbIndex (as found in the USB report) set to value, CRC redone.
std::vector<BYTE> btReportWith(std::initializer_list<std::pair<ULONG, BYTE>> usbBytes) {
    std::vector<BYTE> r = kRealBtReport;
    for (auto [usbIndex, value] : usbBytes) r[usbIndex + DualSenseProtocol::kBtHeaderLen] = value;
    sony_crc::store(sony_crc::kSeedInput, r.data(), r.size());
    return r;
}

// Stands in for GenericHidProtocol: records what reached it.
class RecordingFallback final : public ControllerProtocol {
public:
    int  calls  = 0;
    BYTE lastId = 0;
    bool decode(const BYTE* report, ULONG, RawHIDState& out) override {
        ++calls;
        lastId = report[0];
        out.valid = true;
        return true;
    }
};

// Scripted Sony device: answers reads from a queue of reports, then times out.
class FakeChannel final : public HidChannel {
public:
    int reads = 0;
    std::deque<std::vector<BYTE>> queued;

    ReadResult read(int) override {
        ++reads;
        if (queued.empty()) return ReadResult::Timeout;
        m_buf = queued.front();
        queued.pop_front();
        return ReadResult::Ok;
    }
    const std::vector<BYTE>& reportBuf()     const override { return m_buf; }
    ULONG                    lastBytesRead() const override { return static_cast<ULONG>(m_buf.size()); }
    USHORT vendorId() const override { return DualSenseProtocol::kSonyVendorId; }
    bool   canWrite() const override { return false; }   // the DualSense must not need it either
    bool   sendOutputReport(const BYTE*, ULONG) override { return false; }
    bool   setFeature(const BYTE*, ULONG)       override { return false; }
    bool   getFeature(BYTE reportId, std::vector<BYTE>& out) override {
        out.assign(41, 0);
        out[0] = reportId;
        return true;
    }

private:
    std::vector<BYTE> m_buf;
};

// The BT short report, as the probe logged it (line 390): DS4-like layout, read by HidP.
std::vector<BYTE> shortReport() {
    return { 0x01, 0x82, 0x7F, 0x7E, 0x7E, 0x08, 0x00, 0x20, 0x00, 0x00 };
}

} // namespace

// ── decode(): BT 0x31 → USB layout ────────────────────────────────────────────

TEST_CASE("Sony CRC accepts a real DualSense BT report", "[DualSenseProtocol][SonyCrc]") {
    CHECK(sony_crc::check(sony_crc::kSeedInput, kRealBtReport.data(), kRealBtReport.size()));
}

TEST_CASE("DualSenseProtocol decodes a real BT 0x31 report like HidP reads the USB one",
          "[DualSenseProtocol]") {
    auto fallback = std::make_unique<RecordingFallback>();
    auto* fb = fallback.get();
    DualSenseProtocol proto(std::move(fallback));

    std::vector<BYTE> report = kRealBtReport;
    report.resize(547, 0xEE);   // ReadFile padding with leftovers — decode() must ignore it
    RawHIDState s;
    REQUIRE(proto.decode(report.data(), static_cast<ULONG>(report.size()), s));
    CHECK(fb->calls == 0);

    CHECK(s.valid);
    CHECK(s.buttonsValid);
    CHECK(s.buttonMask == 0);
    CHECK(s.hat == 0xFFFFFFFF);   // 8 = neutral

    CHECK(s.axisX  == Catch::Approx(hidNormalized(0x82)));   // LX
    CHECK(s.axisY  == Catch::Approx(hidNormalized(0x7F)));   // LY
    CHECK(s.axisZ  == Catch::Approx(hidNormalized(0x7E)));   // RX
    CHECK(s.axisRz == Catch::Approx(hidNormalized(0x7E)));   // RY
    CHECK(s.axisRx == Catch::Approx(-1.0f));                 // L2 released
    CHECK(s.axisRy == Catch::Approx(-1.0f));                 // R2 released
}

TEST_CASE("DualSenseProtocol rebuilds the USB report in raw from a 1-byte header", "[DualSenseProtocol]") {
    DualSenseProtocol proto(std::make_unique<RecordingFallback>());
    RawHIDState s;
    REQUIRE(proto.decode(kRealBtReport.data(), static_cast<ULONG>(kRealBtReport.size()), s));

    // IMU/touch offsets in controllers.json are USB offsets (gyro 16, touch 33): raw must be the
    // USB report.
    REQUIRE(s.raw.size() == DualSenseProtocol::kUsbReportLen);
    CHECK(s.raw[0] == DualSenseProtocol::kUsbReportId);
    for (ULONG i = 1; i < DualSenseProtocol::kUsbReportLen; ++i) {
        INFO("USB byte " << i);
        CHECK(s.raw[i] == kRealBtReport[i + DualSenseProtocol::kBtHeaderLen]);
    }
}

TEST_CASE("DualSenseProtocol reads buttons, hat and triggers from the DualSense positions",
          "[DualSenseProtocol]") {
    DualSenseProtocol proto(std::make_unique<RecordingFallback>());
    RawHIDState s;

    SECTION("face buttons are buttons 1-4 (high nibble of byte 8)") {
        const auto r = btReportWith({ { 8, 0xF8 } });   // all four, hat neutral
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == 0x000F);
        CHECK(s.hat == 0xFFFFFFFF);
    }
    SECTION("byte 9 is buttons 5-12, byte 10 bits 0-2 are PS, touch click and mute") {
        const auto r = btReportWith({ { 8, 0x08 }, { 9, 0xFF }, { 10, 0xFF } });   // unused bits set too
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == 0x7FF0);   // buttons 5-15; bits 3-7 of byte 10 are not buttons
    }
    SECTION("single buttons land on their own bit") {
        const auto r = btReportWith({ { 8, 0x28 }, { 9, 0x80 }, { 10, 0x04 } });   // Cross, R3, mute
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == ((1u << 1) | (1u << 11) | (1u << 14)));
    }
    SECTION("the sequence counter (byte 7) is not input") {
        const auto r = btReportWith({ { 7, 0xFF } });
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.buttonMask == 0);
        CHECK(s.hat == 0xFFFFFFFF);
    }
    SECTION("hat directions 0-7 pass through, anything above is neutral") {
        for (BYTE dir = 0; dir <= 7; ++dir) {
            const auto r = btReportWith({ { 8, dir } });
            REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
            CHECK(s.hat == dir);
        }
        const auto r = btReportWith({ { 8, 0x0F } });
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.hat == 0xFFFFFFFF);
    }
    SECTION("triggers are bytes 5-6, fully pressed read +1 like HidP over USB") {
        const auto r = btReportWith({ { 5, 0xFF }, { 6, 0xFF } });
        REQUIRE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
        CHECK(s.axisRx == Catch::Approx(1.0f));
        CHECK(s.axisRy == Catch::Approx(1.0f));
    }
}

TEST_CASE("DualSenseProtocol drops a 0x31 with a bad CRC", "[DualSenseProtocol]") {
    DualSenseProtocol proto(std::make_unique<RecordingFallback>());
    std::vector<BYTE> r = kRealBtReport;
    r[10] = 0x01;   // a button pressed, CRC not redone
    RawHIDState s;
    CHECK_FALSE(proto.decode(r.data(), static_cast<ULONG>(r.size()), s));
    CHECK_FALSE(s.valid);
}

TEST_CASE("DualSenseProtocol hands the BT short report and the DS4 report ID to the fallback",
          "[DualSenseProtocol]") {
    auto fallback = std::make_unique<RecordingFallback>();
    auto* fb = fallback.get();
    DualSenseProtocol proto(std::move(fallback));
    RawHIDState s;

    const auto shortR = shortReport();
    CHECK(proto.decode(shortR.data(), static_cast<ULONG>(shortR.size()), s));
    CHECK(fb->lastId == 0x01);

    std::vector<BYTE> ds4Id = kRealBtReport;
    ds4Id[0] = 0x11;   // not this model's full report
    CHECK(proto.decode(ds4Id.data(), static_cast<ULONG>(ds4Id.size()), s));
    CHECK(fb->lastId == 0x11);
    CHECK(fb->calls == 2);
}

// ── enableFullMode() ──────────────────────────────────────────────────────────

TEST_CASE("DualSenseProtocol::enableFullMode over BT waits for 0x31", "[DualSenseProtocol]") {
    DualSenseProtocol proto(nullptr);
    FakeChannel ch;

    SECTION("confirmed when the first 0x31 arrives") {
        ch.queued = { shortReport(), kRealBtReport };
        CHECK(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.reads == 2);
    }
    SECTION("a DS4 full report (0x11) does not confirm it") {
        std::vector<BYTE> ds4Id = kRealBtReport;
        ds4Id[0] = 0x11;
        ch.queued = { ds4Id };
        CHECK_FALSE(proto.enableFullMode(ch, HidTransport::Bluetooth));
        CHECK(ch.reads == DualSenseProtocol::kConfirmMaxReads);
    }
}
