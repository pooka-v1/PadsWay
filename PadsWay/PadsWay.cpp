#include <windows.h>
#include <filesystem>
#include <cstdio>
#include "Log.h"
#include "Paths.h"
#include "config/ConfigLoader.h"
#include "PadEngine.h"
#include "AppWindow.h"

// All data/image paths in the app are relative ("data/...", "images/...") and
// resolve against the process working directory. When launched by double-click
// the cwd happens to be the exe's folder, but a Start-menu shortcut (or any
// launch from another folder) sets a different cwd and nothing would be found.
//
// If the current cwd already contains "data" we leave it untouched: this keeps
// debugging from Visual Studio working (cwd = project dir, which holds data/)
// and a normal double-click (cwd = exe folder, which holds data/). Only when
// data/ is not reachable from the cwd do we anchor to the exe's own folder,
// covering shortcuts and launches from an unrelated directory.
static void anchorWorkingDirectoryToExe() {
    std::error_code ec;
    if (std::filesystem::exists("data", ec)) return;  // cwd already correct, don't move it

    wchar_t exePath[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return;  // call failed or path truncated: leave cwd untouched
    std::filesystem::current_path(std::filesystem::path(exePath).parent_path(), ec);
}

int main() {
    anchorWorkingDirectoryToExe();

    // First run on a normal install starts with an empty %LOCALAPPDATA%\PadsWay,
    // so copy the factory files shipped next to the exe across before we read
    // any of them (no-op in portable mode and once the files already exist).
    Paths::seedUserDataFromFactory();

    VirtualPadConfig vpCfg;
    try { vpCfg = loadVirtualPadConfig(Paths::userData("data/virtualpad.json")); } catch (...) {}

    // The app is built as a Windows (GUI) subsystem app, so no console appears by
    // default. When "console": true is set in virtualpad.json we allocate one and
    // route stdio to it, giving live logs for development/debugging.
    if (vpCfg.console) {
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
        SetConsoleOutputCP(CP_UTF8);
    }

    Log::init(vpCfg.logLevel, vpCfg.console);

    PadEngine engine;
    AppWindow window(engine);
    return window.run();
}
