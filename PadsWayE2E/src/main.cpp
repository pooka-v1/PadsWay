#include "E2EHarness.h"   // first: defines NOMINMAX before any <windows.h>
#include "E2ESandbox.h"
#include "input/HIDScanner.h"
#include "Log.h"
#include <cstdio>
#include <catch2/catch_amalgamated.hpp>

// ---------------------------------------------------------------------------
// PadsWayE2E — end-to-end mapping tests. Own main() (CATCH_AMALGAMATED_CUSTOM_MAIN) because the
// whole run shares one sandbox + fake pad + engine: starting the engine takes seconds, far too
// slow to repeat per test case. Requirements: ViGEmBus + HidHide installed, PadsWay closed, no
// real DualShock 4 v2 connected, and hands off keyboard/mouse while it runs.
// Exit code: Catch2's result, or 2 if the environment could not be set up.
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // A real DS4 v2 shares the fake pad's VID/PID: the engine could pick it instead, and HidHide
    // could end up hiding the wrong device.
    for (const auto& dev : HIDScanner::scan()) {
        if (dev.vid == E2ESandbox::kFakePadVid && dev.pid == E2ESandbox::kFakePadPid) {
            std::fprintf(stderr, "[E2E] A DualShock 4 v2 (054C:09CC) is connected ('%s'). "
                                 "Disconnect it and run again.\n", dev.productName.c_str());
            return 2;
        }
    }

    std::string error;
    if (!E2ESandbox::prepare(error)) {
        std::fprintf(stderr, "[E2E] Sandbox setup failed: %s\n", error.c_str());
        return 2;
    }
    Log::init("debug", false);   // file only: temp/e2e/sandbox/logs/padsway.log
    std::printf("[E2E] Sandbox: %s\n", std::filesystem::current_path().string().c_str());

    if (!harness().start(error)) {
        std::fprintf(stderr, "[E2E] Harness start failed: %s\n", error.c_str());
        harness().stop();
        return 2;
    }
    std::printf("[E2E] Engine running on the fake DualShock 4. Hands off keyboard and mouse.\n");

    const int result = Catch::Session().run(argc, argv);
    harness().stop();
    return result;
}
