#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "Paths.h"

// One-time logger initialisation.
// Call from main() before anything else.
// After this, use spdlog::info / debug / warn / error everywhere.
namespace Log {
    inline void init(const std::string& level = "info", bool withConsole = false) {
        // The console sink is only added when a console window exists (see main()):
        // a windowed build with no console has nowhere to write colored stdout.
        std::vector<spdlog::sink_ptr> sinks;
        if (withConsole)
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

        // The file sink is best-effort. A read-only / locked log folder must
        // NEVER crash startup: a failing rotating_file_sink throwing here is
        // exactly what made the app die under Program Files. If it can't open,
        // we run with whatever sinks we have (possibly none) instead of dying.
        try {
            std::error_code ec;
            std::filesystem::create_directories(Paths::userData("logs"), ec);
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                Paths::userData("logs/padsway.log"), 1024 * 1024, 3));  // 1 MB, keep 3
        } catch (const std::exception& e) {
            if (withConsole) std::fprintf(stderr, "Log: file sink disabled: %s\n", e.what());
        }

        auto logger = std::make_shared<spdlog::logger>("vp", sinks.begin(), sinks.end());

        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");
        spdlog::set_level(spdlog::level::from_str(level));
        spdlog::info("PadsWay starting — log level: {} — data dir: {}", level,
                     Paths::isPortable() ? std::string("portable (next to exe)")
                                         : Paths::userDataDir());
    }
}
