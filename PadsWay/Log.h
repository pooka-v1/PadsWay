#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <windows.h>
#include <memory>
#include <string>

// One-time logger initialisation.
// Call from main() before anything else.
// After this, use spdlog::info / debug / warn / error everywhere.
namespace Log {
    inline void init(const std::string& level = "info") {
        CreateDirectoryA("logs", nullptr);   // no-op if already exists

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink    = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/padsway.log", 1024 * 1024, 3);  // 1 MB, keep 3 files

        auto logger = std::make_shared<spdlog::logger>(
            "vp", spdlog::sinks_init_list{ consoleSink, fileSink });

        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");
        spdlog::set_level(spdlog::level::from_str(level));
        spdlog::info("PadsWay starting — log level: {}", level);
    }
}
