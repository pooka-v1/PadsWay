#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <vector>

#include "EightBitDoInputSource.h"
#include "ViGEmOutputAdapter.h"
#include "ConfigLoader.h"

#pragma comment(lib, "ViGEmClient.lib")
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

struct JoyEntry {
    UINT    id;
    UINT    axes;
    UINT    buttons;
    WORD    wMid;
    WORD    wPid;
    wchar_t name[MAXPNAMELEN];
};

// Returns all WinMM ports that respond to joyGetPosEx.
static std::vector<JoyEntry> scanPorts() {
    std::vector<JoyEntry> result;
    UINT numDevs = joyGetNumDevs();
    for (UINT id = 0; id < numDevs; ++id) {
        JOYINFOEX info = {};
        info.dwSize  = sizeof(JOYINFOEX);
        info.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(id, &info) != JOYERR_NOERROR) continue;

        JoyEntry e = {};
        e.id = id;
        JOYCAPS caps = {};
        if (joyGetDevCaps(id, &caps, sizeof(caps)) == JOYERR_NOERROR) {
            e.axes    = caps.wNumAxes;
            e.buttons = caps.wNumButtons;
            e.wMid    = caps.wMid;
            e.wPid    = caps.wPid;
            wcsncpy_s(e.name, caps.szPname, MAXPNAMELEN);
        } else {
            wcscpy_s(e.name, L"(unknown)");
        }
        result.push_back(e);
    }
    return result;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "=== VirtualPad ===\n";
    std::cout << "Press ESC to exit.\n\n";

    // --- Step 1: Find the real controller BEFORE creating the virtual one.
    // ViGEm's virtual Xbox 360 competes with the real controller for WinMM slots;
    // scanning first lets the real device claim its port before ViGEm occupies one.
    std::cout << "Scanning for joystick devices...\n";

    UINT     joyPort = UINT_MAX;
    JoyEntry selected = {};

    while (joyPort == UINT_MAX) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) return 0;

        auto entries = scanPorts();

        if (entries.empty()) {
            std::cout << "\rNo joystick found. Connect the controller in D-mode.    ";
            Sleep(500);
            continue;
        }

        if (entries.size() == 1) {
            selected = entries[0];
            joyPort  = selected.id;
            printf("\nAuto-selected port %u: %ls (%u axes, %u buttons) [VID=%04X PID=%04X]\n",
                joyPort, selected.name, selected.axes, selected.buttons,
                selected.wMid, selected.wPid);
        } else {
            std::cout << "\nMultiple joystick devices found:\n";
            for (auto& e : entries)
                printf("  Port %u: %ls (%u axes, %u buttons) [VID=%04X PID=%04X]\n",
                    e.id, e.name, e.axes, e.buttons, e.wMid, e.wPid);
            std::cout << "Enter port number: ";
            std::cin >> joyPort;
            for (auto& e : entries)
                if (e.id == joyPort) { selected = e; break; }
        }
    }

    // --- Step 2: Load controller config and find the one matching the selected device. ---
    std::vector<ControllerConfig> configs;
    try {
        configs = loadControllerConfigs("configs/controllers.json");
    } catch (const std::exception& ex) {
        std::cerr << "Error loading config: " << ex.what() << "\n";
        return 1;
    }

    const ControllerConfig* cfg = findConfig(configs, selected.wMid, selected.wPid);
    if (!cfg) {
        fprintf(stderr,
            "No config found for VID=%04X PID=%04X (%ls).\n"
            "Add an entry to configs/controllers.json and restart.\n",
            selected.wMid, selected.wPid, selected.name);
        return 1;
    }
    printf("Config loaded: %s\n", cfg->source_name.c_str());

    EightBitDoInputSource input(joyPort, *cfg);

    // --- Step 3: Initialize ViGEm (virtual Xbox 360) after the real port is secured. ---
    ViGEmOutputAdapter output;
    if (!output.isReady()) {
        std::cerr << "Aborting: could not create virtual pad.\n";
        return 1;
    }

    std::cout << "Forwarding input. Press ESC to exit.\n\n";

    GamepadState state;

    while (true) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            break;

        if (!input.isConnected()) {
            printf("\r[%s] disconnected. Waiting...    ", cfg->source_name.c_str());
            Sleep(500);
            continue;
        }

        if (input.read(state)) {
            output.update(state);
            printf("\r  A:%d B:%d X:%d Y:%d L1:%d R1:%d Bk:%d St:%d | LX:%+.2f LY:%+.2f | RX:%+.2f RY:%+.2f | L2:%.2f R2:%.2f",
                state.btnA, state.btnB, state.btnX, state.btnY,
                state.btnLB, state.btnRB, state.btnBack, state.btnStart,
                state.leftX, state.leftY,
                state.rightX, state.rightY,
                state.triggerL, state.triggerR);
        }

        Sleep(8);
    }

    std::cout << "\nGoodbye.\n";
    return 0;
}
