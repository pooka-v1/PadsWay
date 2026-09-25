#pragma once
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// E2ESandbox — throwaway portable-mode data folder the engine runs against, so the E2E suite
// never reads or writes the developer's real data/ files.
//
// Layout (rebuilt from scratch on every run, under the gitignored temp/ folder):
//   temp/e2e/sandbox/portable.txt              -> Paths::isPortable() == true
//   temp/e2e/sandbox/data/controllers.json     -> ONLY the DS4 USB entry, copied from the repo
//   temp/e2e/sandbox/data/macros.json          -> empty library (test macros are inline, per case)
//   temp/e2e/sandbox/data/virtualpad.json      -> Xbox output, default virtual identity
//   temp/e2e/sandbox/data/bots/TestBot.dll     -> copied from next to PadsWayE2E.exe (bot tests)
//   temp/e2e/sandbox/data/profiles/, logs/
//
// prepare() also makes the sandbox the process working directory. It MUST run before anything
// calls into Paths:: — Paths::isPortable()/userDataDir() cache their answer on first use.
// ---------------------------------------------------------------------------
namespace E2ESandbox {

// VID/PID of the controller entry copied into the sandbox, and of the fake physical pad.
constexpr unsigned short kFakePadVid = 0x054C;   // Sony
constexpr unsigned short kFakePadPid = 0x09CC;   // DualShock 4 v2 (USB)

// Bot used for bot-assignment tests: TestBot.dll (TestBotDLL project, a build dependency of
// PadsWayE2E). While ON it loops A alone kTestBotPhaseMs, then B alone kTestBotPhaseMs — a fixed
// output the tests can watch. Missing DLL -> prepare() fails the run.
constexpr const char* kTestBotName    = "TestBot";
constexpr int         kTestBotPhaseMs = 200;   // keep in sync with kPhaseMs in TestBotDLL/TestBot.cpp

// Repo root, located from this source file's own path (PadsWayE2E/src/ -> repo).
std::filesystem::path repoRoot();

// Builds the sandbox and chdirs into it. Returns false with a human-readable reason on failure.
bool prepare(std::string& error);

// Pristine copy of the sandbox controllers.json written by prepare(); tests restore it to undo
// a Normal-mode (Mapeador) assignment.
std::filesystem::path baselineControllersPath();

}
