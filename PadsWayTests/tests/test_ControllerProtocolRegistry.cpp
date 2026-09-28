#ifndef NOMINMAX
#define NOMINMAX   // pulls in <windows.h> via HIDDevice.h — see CLAUDE.md, "Tests"
#endif
#include "input/ControllerProtocolRegistry.h"
#include <catch2/catch_amalgamated.hpp>

// The names controllers.json may put in "protocol". Only the name check is tested here:
// createControllerProtocol() needs a real HIDDevice, covered by the E2E run instead.

TEST_CASE("generic_hid is a known protocol", "[ControllerProtocolRegistry]") {
    CHECK(isKnownControllerProtocol("generic_hid"));
    CHECK(isKnownControllerProtocol(kGenericHidProtocol));
}

TEST_CASE("unknown or malformed protocol names are not known", "[ControllerProtocolRegistry]") {
    // createControllerProtocol() warns and falls back to generic_hid for these.
    CHECK_FALSE(isKnownControllerProtocol(""));
    CHECK_FALSE(isKnownControllerProtocol("Generic_HID"));   // case-sensitive, like every JSON key
    CHECK_FALSE(isKnownControllerProtocol("generic_hid "));
    CHECK_FALSE(isKnownControllerProtocol("sony_ds4"));      // not registered yet (task 4)
}
