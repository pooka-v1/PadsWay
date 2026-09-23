#include "E2EHarness.h"
#include "E2ESandbox.h"
#include "input/HIDScanner.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace {

E2EHarness g_harness;

using Clock = std::chrono::steady_clock;

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// The fake pad as HIDScanner (i.e. the engine) sees it, or nullptr-equivalent (vid 0) if absent.
HIDScanner::DeviceInfo findFakePadHid() {
    for (const auto& dev : HIDScanner::scan())
        if (dev.vid == E2ESandbox::kFakePadVid && dev.pid == E2ESandbox::kFakePadPid)
            return dev;
    return HIDScanner::DeviceInfo{};
}

} // namespace

E2EHarness& harness() { return g_harness; }

bool isNeutral(const GamepadState& s) {
    const bool anyButton =
        s.btnA || s.btnB || s.btnX || s.btnY || s.btnLB || s.btnRB || s.btnStart || s.btnBack ||
        s.btnHome || s.btnL3 || s.btnR3 || s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight;
    const bool anyAnalog =
        s.triggerL > 0.05f || s.triggerR > 0.05f ||
        std::fabs(s.leftX) > 0.1f || std::fabs(s.leftY) > 0.1f ||
        std::fabs(s.rightX) > 0.1f || std::fabs(s.rightY) > 0.1f;
    return !anyButton && !anyAnalog;
}

bool sameVirtualOutput(const GamepadState& a, const GamepadState& e, float tolerance) {
    auto closeEnough = [tolerance](float x, float y) { return std::fabs(x - y) <= tolerance; };
    return a.btnA == e.btnA && a.btnB == e.btnB && a.btnX == e.btnX && a.btnY == e.btnY &&
           a.btnLB == e.btnLB && a.btnRB == e.btnRB && a.btnStart == e.btnStart &&
           a.btnBack == e.btnBack && a.btnHome == e.btnHome && a.btnL3 == e.btnL3 && a.btnR3 == e.btnR3 &&
           a.dpadUp == e.dpadUp && a.dpadDown == e.dpadDown &&
           a.dpadLeft == e.dpadLeft && a.dpadRight == e.dpadRight &&
           closeEnough(a.triggerL, e.triggerL) && closeEnough(a.triggerR, e.triggerR) &&
           closeEnough(a.leftX, e.leftX) && closeEnough(a.leftY, e.leftY) &&
           closeEnough(a.rightX, e.rightX) && closeEnough(a.rightY, e.rightY);
}

std::string describe(const GamepadState& s) {
    std::string out;
    auto flag = [&out](bool on, const char* name) { if (on) { out += name; out += ' '; } };
    auto axis = [&out](float v, const char* name, float threshold) {
        if (std::fabs(v) <= threshold) return;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s=%.2f ", name, v);
        out += buf;
    };
    flag(s.btnA, "A");   flag(s.btnB, "B");   flag(s.btnX, "X");   flag(s.btnY, "Y");
    flag(s.btnLB, "LB"); flag(s.btnRB, "RB"); flag(s.btnBack, "BACK"); flag(s.btnStart, "START");
    flag(s.btnHome, "HOME"); flag(s.btnL3, "L3"); flag(s.btnR3, "R3");
    flag(s.dpadUp, "DPAD_UP"); flag(s.dpadDown, "DPAD_DOWN");
    flag(s.dpadLeft, "DPAD_LEFT"); flag(s.dpadRight, "DPAD_RIGHT");
    axis(s.triggerL, "LT", 0.02f); axis(s.triggerR, "RT", 0.02f);
    axis(s.leftX, "LX", 0.05f);    axis(s.leftY, "LY", 0.05f);
    axis(s.rightX, "RX", 0.05f);   axis(s.rightY, "RY", 0.05f);
    if (out.empty()) return "(neutral)";
    out.pop_back();
    return out;
}

bool E2EHarness::start(std::string& error) {
    m_fakePad = std::make_unique<ViGEmDs4OutputAdapter>(E2ESandbox::kFakePadVid, E2ESandbox::kFakePadPid);
    if (!m_fakePad->isReady()) {
        error = "could not plug the fake DS4 in through ViGEm (is ViGEmBus installed?)";
        return false;
    }
    m_fakePad->update(Ds4Input{});

    // Plug-and-play enumeration of the new HID device takes a moment.
    HIDScanner::DeviceInfo fakeHid;
    for (auto until = Clock::now() + std::chrono::seconds(10); Clock::now() < until; sleepMs(250)) {
        fakeHid = findFakePadHid();
        if (fakeHid.vid != 0) break;
    }
    if (fakeHid.vid == 0) {
        error = "the fake DS4 (054C:09CC) never showed up as a HID gamepad";
        return false;
    }

    m_deviceHub = std::make_unique<DeviceHub>();
    m_engine    = std::make_unique<PadEngine>(*m_deviceHub);
    m_engine->start();

    for (auto until = Clock::now() + std::chrono::seconds(15); Clock::now() < until; sleepMs(100)) {
        const DeviceCandidate active = m_engine->getActiveDevice();
        if (m_engine->getPhase() == EnginePhase::Running &&
            active.vid == E2ESandbox::kFakePadVid && active.pid == E2ESandbox::kFakePadPid) {
            sleepMs(300);   // let the first reports flow before any test asserts on them
            return true;
        }
    }
    error = "engine never reached Running on the fake DS4. Engine status: '" + m_engine->getStatus() +
            "'. Fake pad seen by HIDScanner as name='" + fakeHid.productName + "' connection='" +
            fakeHid.connectionType + "' — compare with the DS4 entry's product_name/connection.";
    return false;
}

void E2EHarness::stop() {
    if (m_engine) m_engine->stop();   // also unhides the fake pad in HidHide
    m_engine.reset();
    m_deviceHub.reset();
    m_fakePad.reset();                // unplugs the fake pad
}

void E2EHarness::press(const Ds4Input& input) { m_fakePad->update(input); }

bool E2EHarness::releaseAll() {
    press(Ds4Input{});
    return waitForVirtual(isNeutral);
}

void E2EHarness::clearEvents() { m_engine->pollEvents(); }

bool E2EHarness::waitForEvent(const std::function<bool(const PadEvent&)>& pred, int timeoutMs) {
    const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        for (const auto& e : m_engine->pollEvents())
            if (pred(e)) return true;
        sleepMs(5);
    } while (Clock::now() < until);
    return false;
}

GamepadState E2EHarness::virtualState() const { return m_engine->getLastVirtualState(); }

bool E2EHarness::waitForVirtual(const std::function<bool(const GamepadState&)>& pred, int timeoutMs,
                                GamepadState* lastSeen) const {
    const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    GamepadState s;
    do {
        s = virtualState();
        if (pred(s)) {
            if (lastSeen) *lastSeen = s;
            return true;
        }
        sleepMs(5);
    } while (Clock::now() < until);
    if (lastSeen) *lastSeen = s;
    return false;
}
