#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <vector>

#include "EightBitDoInputSource.h"
#include "ConfigLoader.h"
#include "FlashMonitor.h"

#pragma comment(lib, "WinMM.lib")

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
    std::cout << "=== TriggerCount ===\n";
    std::cout << "Monitors screen flashes and measures your A-button press duration.\n";
    std::cout << "  Button 6 : toggle flash monitor ON/OFF\n";
    std::cout << "  Hold ESC : exit\n\n";

    // --- Step 1: Find the real controller. ---
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

    // --- Step 2: Load controller config. ---
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

    // --- Step 3: Start flash monitor (idle until toggled ON with button 6). ---
    FlashMonitor monitor;

    std::cout << "Ready. Press button 6 on the controller to start flash monitoring.\n\n";

    GamepadState state;
    bool         btn6Prev  = false;
    bool         btnAPrev  = false;
    ULONGLONG    aPressStart = 0;   // timestamp of A rising edge
    ULONGLONG    escSince    = 0;

    while (true) {
        // Exit: hold ESC for 1 second
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            if (escSince == 0) escSince = GetTickCount64();
            if (GetTickCount64() - escSince >= 1000) break;
        } else {
            escSince = 0;
        }

        if (!input.isConnected()) {
            printf("\r[%s] disconnected. Waiting...    ", cfg->source_name.c_str());
            Sleep(500);
            continue;
        }

        // --- Button 6: toggle flash monitor ---
        {
            JOYINFOEX raw = {};
            raw.dwSize  = sizeof(JOYINFOEX);
            raw.dwFlags = JOY_RETURNBUTTONS;
            bool btn6 = false;
            if (joyGetPosEx(joyPort, &raw) == JOYERR_NOERROR)
                btn6 = (raw.dwButtons >> 5) & 1u;

            if (btn6 && !btn6Prev) {
                monitor.toggle();
                printf("\n[MON] Flash monitor %s\n",
                       monitor.isActive() ? "ON" : "OFF");
            }
            btn6Prev = btn6;
        }

        if (input.read(state)) {
            ULONGLONG now = GetTickCount64();

            // --- A button: measure press duration ---
            if (state.btnA && !btnAPrev) {
                // Rising edge: record when the press started
                aPressStart = now;
                printf("[A]  Pressed   T=%llu  (flashes so far: %d)\n",
                       now, monitor.flashCount());
            }
            if (!state.btnA && btnAPrev) {
                // Falling edge: compute and print hold duration
                ULONGLONG held = now - aPressStart;
                printf("[A]  Released  T=%llu  held=%llums\n", now, held);
            }
            btnAPrev = state.btnA;
        }

        Sleep(8);
    }

    printf("\nTotal flashes detected: %d\n", monitor.flashCount());
    std::cout << "Goodbye.\n";
    return 0;
}
