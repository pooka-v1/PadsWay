#pragma once
#include <string>

// Centralised file-location policy for PadsWay.
//
// The app touches two kinds of files:
//
//   * Read-only assets shipped with the program (images/, data/strings/,
//     data/state_map.json, data/bots/*.dll). These stay RELATIVE to the
//     working directory (anchored to the exe folder in main()), exactly as
//     before — do NOT route them through here.
//
//   * User-writable data (controllers.json, profiles/, macros.json,
//     pad_layouts.json, virtualpad.json, logs/). These live under a per-user
//     folder so the app keeps working even when installed into a
//     UAC-protected location such as Program Files (where writing next to the
//     exe fails and used to crash startup).
//
// Normal mode  : %LOCALAPPDATA%\PadsWay\          (created on demand)
// Portable mode: if a file named "portable.txt" sits in the working directory
//                the user folder collapses back to "" (i.e. relative to the
//                exe, the old behaviour). Handy for a USB stick and for
//                in-IDE development, where we keep writing into the project's
//                own data/ folder.
namespace Paths {
    // True when portable.txt is present next to the (anchored) working dir.
    // Decided once and cached.
    bool isPortable();

    // Root for user-writable data, always ending in a separator:
    //   ""  in portable mode (relative to cwd),
    //   "<localappdata>\\PadsWay\\"  otherwise.
    const std::string& userDataDir();

    // userDataDir() + relative. Use for any file the app WRITES, e.g.
    //   Paths::userData("data/controllers.json")
    //   Paths::userData("logs/padsway.log")
    std::string userData(const std::string& relative);

    // On first run the per-user folder is empty, so the factory files that
    // ship next to the exe (pad_layouts.json — needed to draw any pad — plus
    // controllers.json / virtualpad.json if present) are copied across once.
    // No-op in portable mode and on files that already exist. Safe every launch.
    void seedUserDataFromFactory();
}
