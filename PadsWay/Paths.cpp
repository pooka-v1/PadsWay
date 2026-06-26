#include "Paths.h"

#include <windows.h>
#include <shlobj.h>          // SHGetKnownFolderPath, FOLDERID_LocalAppData
#include <filesystem>
#include "Log.h"

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")    // CoTaskMemFree

namespace {

namespace fs = std::filesystem;

// The whole codebase uses narrow (char) paths, so we keep that assumption and
// down-convert the wide LOCALAPPDATA path through the system ANSI code page.
// Works for usernames in the system codepage (incl. accented Latin); exotic
// scripts (Cyrillic/CJK) are the one unsupported case — documented, rare.
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

// "C:\Users\<name>\AppData\Local" (no trailing separator), or "" on failure.
std::string localAppDataRoot() {
    PWSTR path = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) && path)
        result = narrow(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace

namespace Paths {

bool isPortable() {
    // exists() runs against the cwd, which main() has already anchored to the
    // exe folder, so this finds a portable.txt sitting next to the program.
    static const bool portable = std::filesystem::exists("portable.txt");
    return portable;
}

const std::string& userDataDir() {
    static const std::string dir = []() -> std::string {
        if (isPortable()) return std::string();          // "" -> relative to cwd
        std::string root = localAppDataRoot();
        if (root.empty()) return std::string();          // can't resolve: act portable
        std::string base = root + "\\PadsWay\\";
        std::error_code ec;
        std::filesystem::create_directories(base + "data\\profiles", ec);
        std::filesystem::create_directories(base + "logs", ec);
        return base;
    }();
    return dir;
}

std::string userData(const std::string& relative) {
    return userDataDir() + relative;
}

void seedUserDataFromFactory() {
    if (isPortable()) return;                 // dev / USB: factory data already in place
    const std::string& dst = userDataDir();
    if (dst.empty()) return;

    // Factory copies ship next to the exe under data/. pad_layouts.json is the
    // critical one (without it no pad is drawn); the others are convenience.
    const char* seeds[] = {
        "data/pad_layouts.json",
        "data/controllers.json",
        "data/virtualpad.json",
    };
    std::error_code ec;
    for (const char* rel : seeds) {
        fs::path from = rel;                  // relative to anchored cwd (exe folder)
        fs::path to   = dst + rel;
        if (!fs::exists(from) || fs::exists(to)) continue;
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(from, to, fs::copy_options::skip_existing, ec);
        if (ec) spdlog::warn("Paths: could not seed {}: {}", rel, ec.message());
        else    spdlog::info("Paths: seeded factory {}", rel);
    }
}

} // namespace Paths
