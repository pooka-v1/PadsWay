#ifndef NOMINMAX
#define NOMINMAX   // pulls in <windows.h> via HIDDevice.h — see CLAUDE.md, "Tests"
#endif
#include "input/ControllerProtocolRegistry.h"
#include <catch2/catch_amalgamated.hpp>

// Which protocol a pad gets (ARCHITECTURE.md, "Protocolos de mando" → decisiones de la tarea 4):
// by its own VID/PID, with controllers.json's "protocol" as an override. createControllerProtocol()
// needs a real HIDDevice and is covered by the E2E run; the pure selection is tested here.

namespace {
constexpr USHORT kSony   = 0x054C;
constexpr USHORT k8BitDo = 0x2DC8;
}

TEST_CASE("known protocol names", "[ControllerProtocolRegistry]") {
    CHECK(isKnownControllerProtocol("generic_hid"));
    CHECK(isKnownControllerProtocol(kGenericHidProtocol));
    CHECK(isKnownControllerProtocol("dualshock4"));
    CHECK(isKnownControllerProtocol(kDualShock4Protocol));
    CHECK(isKnownControllerProtocol("dualsense"));
    CHECK(isKnownControllerProtocol(kDualSenseProtocol));
}

TEST_CASE("unknown or malformed protocol names are not known", "[ControllerProtocolRegistry]") {
    CHECK_FALSE(isKnownControllerProtocol(""));
    CHECK_FALSE(isKnownControllerProtocol("Generic_HID"));   // case-sensitive, like every JSON key
    CHECK_FALSE(isKnownControllerProtocol("generic_hid "));
    CHECK_FALSE(isKnownControllerProtocol("sony_ds5"));
}

TEST_CASE("protocolForHardware picks the Sony models by VID/PID only", "[ControllerProtocolRegistry]") {
    CHECK(protocolForHardware(kSony, 0x05C4) == kDualShock4Protocol);   // DS4 v1
    CHECK(protocolForHardware(kSony, 0x09CC) == kDualShock4Protocol);   // DS4 v2
    CHECK(protocolForHardware(kSony, 0x0BA0) == kDualShock4Protocol);   // wireless adapter
    CHECK(protocolForHardware(kSony, 0x0CE6) == kDualSenseProtocol);   // DualSense
    CHECK(protocolForHardware(k8BitDo, 0x09CC) == kGenericHidProtocol); // same PID, other brand
    CHECK(protocolForHardware(k8BitDo, 0x0CE6) == kGenericHidProtocol);
    CHECK(protocolForHardware(0, 0) == kGenericHidProtocol);
}

TEST_CASE("resolveControllerProtocol: config overrides, empty or unknown goes by hardware",
          "[ControllerProtocolRegistry]") {
    SECTION("no protocol in the config") {
        CHECK(resolveControllerProtocol("", kSony, 0x09CC)   == kDualShock4Protocol);
        CHECK(resolveControllerProtocol("", k8BitDo, 0x6012) == kGenericHidProtocol);
    }
    SECTION("an explicit known name wins over the hardware") {
        CHECK(resolveControllerProtocol("generic_hid", kSony, 0x09CC) == kGenericHidProtocol);
        // Forcing dualshock4 on another brand is allowed here: Ds4Protocol itself refuses to write
        // to a non-Sony pad (vendor ID read from the device), see test_Ds4Protocol.cpp.
        CHECK(resolveControllerProtocol("dualshock4", k8BitDo, 0x6012) == kDualShock4Protocol);
    }
    SECTION("a typo never leaves the pad without its family") {
        CHECK(resolveControllerProtocol("sony_ds5", kSony, 0x09CC)  == kDualShock4Protocol);
        CHECK(resolveControllerProtocol("DUALSHOCK4", k8BitDo, 0x6012) == kGenericHidProtocol);
    }
}

TEST_CASE("hidTransportFromPath tells Bluetooth HID nodes from the rest", "[ControllerProtocolRegistry]") {
    // Real path shapes (lowercase as SetupAPI returns them).
    CHECK(hidTransportFromPath(
        R"(\\?\hid#{00001124-0000-1000-8000-00805f9b34fb}_vid&0002054c_pid&09cc#9&2a5c1b1e&0&0000#)"
        R"({4d1e55b2-f16f-11cf-88cb-001111000030})") == HidTransport::Bluetooth);
    CHECK(hidTransportFromPath(R"(\\?\hid#bthenum#something)") == HidTransport::Bluetooth);
    CHECK(hidTransportFromPath(
        R"(\\?\hid#vid_054c&pid_09cc&mi_03#8&1b2f3a4&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})")
        == HidTransport::Usb);
    CHECK(hidTransportFromPath("") == HidTransport::Usb);
}
