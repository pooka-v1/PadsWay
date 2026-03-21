#include <iostream>
#include <cmath>
#include <windows.h>
#include <mmsystem.h>
#include <vector>

#include "EightBitDoInputSource.h"
#include "ViGEmOutputAdapter.h"
#include "ConfigLoader.h"
#include "LightningBot.h"
#include "Macro.h"
#include "MacroParser.h"
#include <unordered_map>
#include <string>

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

// Returns the physical bit (1-indexed) assigned to a bot by name, or 0 if not found.
static int findBotBit(const ControllerConfig& cfg, const std::string& botName) {
    for (const auto& [bit, action] : cfg.buttons)
        if (action.type == ButtonActionType::Bot && action.name == botName)
            return bit;
    return 0;
}

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

    std::unordered_map<std::string, std::string> macroLibrary;
    try {
        macroLibrary = loadMacroLibrary("configs/macros.json");
        if (!macroLibrary.empty())
            printf("Macro library loaded: %zu macros.\n", macroLibrary.size());
    } catch (const std::exception& ex) {
        fprintf(stderr, "Warning: could not load macro library: %s\n", ex.what());
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
    LightningBot       bot;
    if (!output.isReady()) {
        std::cerr << "Aborting: could not create virtual pad.\n";
        return 1;
    }

    int lightningBotBit = findBotBit(*cfg, "LightningBot");
    if (lightningBotBit > 0)
        printf("LightningBot assigned to button %d.\n", lightningBotBit);

    // --- Step 4: Parse and compile all macros from config. ---
    std::unordered_map<int, Macro>       macros;
    std::unordered_map<int, bool>        macroPrevBtn;
    std::unordered_map<int, std::string> macroNames;
    std::unordered_map<int, int>         macroRotCount;   // rotation counter per macro
    std::unordered_map<int, float>       macroLastRX;     // last logged right stick X
    std::unordered_map<int, float>       macroLastRY;     // last logged right stick Y

    for (const auto& [bit, action] : cfg->buttons) {
        if (action.type != ButtonActionType::Macro) continue;
        std::string execution = action.execution;
        if (execution.empty()) {
            auto it = macroLibrary.find(action.name);
            if (it == macroLibrary.end()) {
                fprintf(stderr, "Warning: macro '%s' (button %d) not found in library — skipped.\n",
                        action.name.c_str(), bit);
                continue;
            }
            execution = it->second;
        }
        try {
            Macro m;
            MacroParser::parse(execution, m);
            macros[bit]       = std::move(m);
            macroPrevBtn[bit] = false;
            macroNames[bit]   = action.name;
            macroRotCount[bit] = 0;
            macroLastRX[bit]   = 0.0f;
            macroLastRY[bit]   = 0.0f;
            printf("Macro '%s' assigned to button %d.\n", action.name.c_str(), bit);
        } catch (const std::exception& ex) {
            fprintf(stderr, "Error parsing macro '%s': %s\n", action.name.c_str(), ex.what());
        }
    }

    std::cout << "Forwarding input. Hold ESC for 1 second to exit.\n\n";

    GamepadState state;
    bool         botBtnPrev = false;
    bool         btnAPrev   = false;
    ULONGLONG    escSince   = 0;  // timestamp when ESC was first seen held down

    while (true) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            if (escSince == 0) escSince = GetTickCount64();
            if (GetTickCount64() - escSince >= 1000) break;  // held for 1 s → exit
        } else {
            escSince = 0;   // released → reset timer
        }

        if (!input.isConnected()) {
            printf("\r[%s] disconnected. Waiting...    ", cfg->source_name.c_str());
            Sleep(500);
            continue;
        }

        // --- Read raw buttons once for all special actions ---
        {
            JOYINFOEX raw = {};
            raw.dwSize  = sizeof(JOYINFOEX);
            raw.dwFlags = JOY_RETURNBUTTONS;
            DWORD btns  = 0;
            if (joyGetPosEx(joyPort, &raw) == JOYERR_NOERROR)
                btns = raw.dwButtons;

            if (lightningBotBit > 0) {
                bool pressed = (btns & (1u << (lightningBotBit - 1))) != 0;
                if (pressed && !botBtnPrev) {
                    bot.toggle();
                    printf("\n[BOT] Lightning bot %s\n", bot.isActive() ? "ON" : "OFF");
                }
                botBtnPrev = pressed;
            }

            for (auto& [bit, macro] : macros) {
                bool pressed = (btns & (1u << (bit - 1))) != 0;
                bool& prev   = macroPrevBtn[bit];

                if (pressed && !prev) {
                    // Rising edge: start or toggle
                    if (macro.getMode() == MacroRepeatMode::UntilRelease)
                        macro.start();
                    else
                        macro.toggle();
                    if (macro.isActive()) {
                        macroRotCount[bit] = 0;
                        macroLastRX[bit]   = 0.0f;
                        macroLastRY[bit]   = 0.0f;
                    }
                    printf("\n[MACRO][%llu] '%s' %s\n",
                           GetTickCount64(), macroNames[bit].c_str(),
                           macro.isActive() ? "ON" : "OFF");
                }
                if (!pressed && prev) {
                    // Falling edge: stop if UntilRelease
                    if (macro.getMode() == MacroRepeatMode::UntilRelease)
                        macro.stop();
                }
                prev = pressed;
            }
        }

        if (input.read(state)) {
            // Log manual A press (rising edge, before bot OR)
            if (state.btnA && !btnAPrev)
                printf("\n[MAN][%llu] Manual A press\n", GetTickCount64());
            btnAPrev = state.btnA;

            // OR in the bot's A press so it works alongside the real controller
            bool botPressed = bot.consumePressA();
            if (botPressed)
                state.btnA = true;

            // Tick all active macros (they OR/override their effects into state)
            for (auto& [bit, macro] : macros) {
                bool wasActive = macro.isActive();
                macro.tick(state);

                // Count laps by detecting arrival at north position (X≈0, Y≈1)
                if (macro.isActive()
                    && (state.rightX != macroLastRX[bit] || state.rightY != macroLastRY[bit])) {
                    bool atNorth    = (fabsf(state.rightX) < 0.1f && state.rightY > 0.9f);
                    bool wasAtNorth = (fabsf(macroLastRX[bit]) < 0.1f && macroLastRY[bit] > 0.9f);
                    if (atNorth && !wasAtNorth) {
                        macroRotCount[bit]++;
                        printf("[MACRO][%llu] '%s' lap=%d\n",
                               GetTickCount64(), macroNames[bit].c_str(), macroRotCount[bit]);
                    }
                    macroLastRX[bit] = state.rightX;
                    macroLastRY[bit] = state.rightY;
                }

                if (wasActive && !macro.isActive())
                    printf("\n[MACRO][%llu] '%s' AUTO-OFF (laps: %d)\n",
                           GetTickCount64(), macroNames[bit].c_str(), macroRotCount[bit]);
            }

            output.update(state);
        }

        Sleep(8);
    }

    std::cout << "\nGoodbye.\n";
    return 0;
}
