#ifndef NOMINMAX
#define NOMINMAX   // BluetoothLink.h pulls in <windows.h> — see CLAUDE.md, "Tests"
#endif
#include "input/BluetoothLink.h"
#include <catch2/catch_amalgamated.hpp>
#include <string>
#include <vector>

// BluetoothLink's pure rules: they decide which pads the scanner lists and when an open BT pad is
// dropped. A bug here either hides a live pad or keeps a switched-off one "connected", so each rule
// is pinned with the real cases seen on hardware (DS4 v2, 8BitDo Pro 2 X-mode).

using BluetoothLink::DeviceState;
using Device = HIDScanner::DeviceInfo;

namespace {

Device makeDevice(const std::string& connectionType, const std::string& btAddress, WORD pid = 0x09CC) {
    Device dev{};
    dev.vid            = 0x054C;
    dev.pid            = pid;
    dev.path           = connectionType + "-" + btAddress;   // unique enough to tell nodes apart
    dev.connectionType = connectionType;
    dev.btAddress      = btAddress;
    return dev;
}

std::vector<std::string> pathsOf(const std::vector<Device>& devices) {
    std::vector<std::string> paths;
    for (const auto& dev : devices) paths.push_back(dev.path);
    return paths;
}

} // namespace

// ---------------------------------------------------------------------------
// addressFromInstanceId
// ---------------------------------------------------------------------------

TEST_CASE("addressFromInstanceId: DS4 v2 BTHENUM parent (uppercase)", "[BluetoothLink]") {
    const char* id = R"(BTHENUM\{00001124-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&09CC\7&1E9FADCE&0&8430958169AC_C00000000)";
    CHECK(BluetoothLink::addressFromInstanceId(id) == "8430958169AC");
}

TEST_CASE("addressFromInstanceId: Pro 2 X-mode parent (lowercase GUID and IDs)", "[BluetoothLink]") {
    const char* id = R"(BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002045e_PID&02e0\7&1e9fadce&0&E417D8E14151_C00000000)";
    CHECK(BluetoothLink::addressFromInstanceId(id) == "E417D8E14151");
}

TEST_CASE("addressFromInstanceId: lowercase address comes back uppercase", "[BluetoothLink]") {
    const char* id = R"(BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc\7&1e9fadce&0&8430958169ac_C00000000)";
    CHECK(BluetoothLink::addressFromInstanceId(id) == "8430958169AC");
}

TEST_CASE("addressFromInstanceId: no address gives empty", "[BluetoothLink]") {
    CHECK(BluetoothLink::addressFromInstanceId(R"(USB\VID_054C&PID_09CC\5&2A3B4C5D&0&3)").empty());
    CHECK(BluetoothLink::addressFromInstanceId(R"(BTH\MS_BTHBRB\6&6f86e6c&1&1)").empty());
    CHECK(BluetoothLink::addressFromInstanceId("no underscore at all").empty());
    CHECK(BluetoothLink::addressFromInstanceId("ABC_C00000000").empty());   // fewer than 12 chars before '_'
    CHECK(BluetoothLink::addressFromInstanceId("").empty());
}

// ---------------------------------------------------------------------------
// isLinkDown
// ---------------------------------------------------------------------------

TEST_CASE("isLinkDown: only a definite answer drops the pad", "[BluetoothLink]") {
    DeviceState state;

    SECTION("stack answered, link down -> ghost") {
        state.known = true; state.connected = false;
        CHECK(BluetoothLink::isLinkDown(state));
    }
    SECTION("stack answered, link up -> alive") {
        state.known = true; state.connected = true;
        CHECK_FALSE(BluetoothLink::isLinkDown(state));
    }
    SECTION("stack didn't answer -> assumed alive, whatever 'connected' holds") {
        state.known = false; state.connected = false;
        CHECK_FALSE(BluetoothLink::isLinkDown(state));
    }
}

// ---------------------------------------------------------------------------
// isLinkCheckDue
// ---------------------------------------------------------------------------

TEST_CASE("isLinkCheckDue: waits for silence, then asks at most once per interval", "[BluetoothLink]") {
    const ULONGLONG silence  = BluetoothLink::kSilenceBeforeLinkCheckMs;
    const ULONGLONG interval = BluetoothLink::kLinkCheckIntervalMs;
    const ULONGLONG lastReport = 100000;
    const ULONGLONG neverChecked = 0;

    SECTION("reports still recent -> no check") {
        CHECK_FALSE(BluetoothLink::isLinkCheckDue(lastReport + silence - 1, lastReport, neverChecked));
    }
    SECTION("silent long enough, never checked -> check") {
        CHECK(BluetoothLink::isLinkCheckDue(lastReport + silence, lastReport, neverChecked));
    }
    SECTION("silent, but checked less than an interval ago -> no check") {
        const ULONGLONG lastCheck = lastReport + silence;
        CHECK_FALSE(BluetoothLink::isLinkCheckDue(lastCheck + interval - 1, lastReport, lastCheck));
    }
    SECTION("silent, last check an interval ago -> check again") {
        const ULONGLONG lastCheck = lastReport + silence;
        CHECK(BluetoothLink::isLinkCheckDue(lastCheck + interval, lastReport, lastCheck));
    }
    SECTION("a report after the last check resets the silence") {
        const ULONGLONG lastCheck     = lastReport + silence;
        const ULONGLONG newReport     = lastCheck + 10;
        CHECK_FALSE(BluetoothLink::isLinkCheckDue(newReport + interval, newReport, lastCheck));
    }
}

// ---------------------------------------------------------------------------
// dropUsbTwinsOfBtNodes
// ---------------------------------------------------------------------------

TEST_CASE("dropUsbTwinsOfBtNodes: same pad on BT and USB -> only the BT node stays", "[BluetoothLink]") {
    std::vector<Device> devices = { makeDevice("usb", "8430958169AC"), makeDevice("bt", "8430958169AC") };
    BluetoothLink::dropUsbTwinsOfBtNodes(devices);
    CHECK(pathsOf(devices) == std::vector<std::string>{ "bt-8430958169AC" });
}

TEST_CASE("dropUsbTwinsOfBtNodes: two pads of the same model, one per transport -> both stay", "[BluetoothLink]") {
    std::vector<Device> devices = { makeDevice("bt", "8430958169AC"), makeDevice("usb", "AABBCCDDEEFF") };
    BluetoothLink::dropUsbTwinsOfBtNodes(devices);
    CHECK(pathsOf(devices) == std::vector<std::string>{ "bt-8430958169AC", "usb-AABBCCDDEEFF" });
}

TEST_CASE("dropUsbTwinsOfBtNodes: USB node without address is never dropped", "[BluetoothLink]") {
    std::vector<Device> devices = { makeDevice("bt", "8430958169AC"), makeDevice("usb", "", 0x6006) };
    BluetoothLink::dropUsbTwinsOfBtNodes(devices);
    CHECK(pathsOf(devices) == std::vector<std::string>{ "bt-8430958169AC", "usb-" });
}

TEST_CASE("dropUsbTwinsOfBtNodes: USB pad alone (no BT node) stays", "[BluetoothLink]") {
    // The ghost-BT case once the scanner has skipped the dead BT node: only the USB node is left.
    std::vector<Device> devices = { makeDevice("usb", "8430958169AC") };
    BluetoothLink::dropUsbTwinsOfBtNodes(devices);
    CHECK(pathsOf(devices) == std::vector<std::string>{ "usb-8430958169AC" });
}

TEST_CASE("dropUsbTwinsOfBtNodes: order of the survivors is kept", "[BluetoothLink]") {
    std::vector<Device> devices = {
        makeDevice("usb", "111111111111"),
        makeDevice("usb", "8430958169AC"),
        makeDevice("bt",  "222222222222"),
        makeDevice("bt",  "8430958169AC"),
    };
    BluetoothLink::dropUsbTwinsOfBtNodes(devices);
    CHECK(pathsOf(devices) == std::vector<std::string>{
        "usb-111111111111", "bt-222222222222", "bt-8430958169AC" });
}
