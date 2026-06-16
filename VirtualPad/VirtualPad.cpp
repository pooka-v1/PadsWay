#include <windows.h>
#include <filesystem>
#include "Log.h"
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
    SetConsoleOutputCP(CP_UTF8);
    anchorWorkingDirectoryToExe();

    VirtualPadConfig vpCfg;
    try { vpCfg = loadVirtualPadConfig("data/virtualpad.json"); } catch (...) {}

    Log::init(vpCfg.logLevel);

    PadEngine engine;
    AppWindow window(engine);
    return window.run();
}
