#include "BotLoader.h"
#include "../Log.h"
#include <windows.h>

// --- BotInstance ---

void BotInstance::start() {
    if (!active && fn_start) {
        fn_start(handle);
        active = true;
    }
}

void BotInstance::stop() {
    if (active && fn_stop) {
        fn_stop(handle);
        active = false;
    }
}

bool BotInstance::tick(BotOutput* out) {
    if (!active || !fn_tick) return false;
    return fn_tick(handle, out) != 0;
}

void BotInstance::toggle() {
    if (active) stop(); else start();
}

// --- BotLoader ---

BotLoader::~BotLoader() {
    for (auto& bot : m_bots) {
        if (bot->active && bot->fn_stop)
            bot->fn_stop(bot->handle);
        if (bot->handle && bot->fn_destroy)
            bot->fn_destroy(bot->handle);
        if (bot->dll)
            FreeLibrary(bot->dll);
    }
}

void BotLoader::scan(const std::string& dir) {
    std::string pattern = dir + "\\*.dll";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        std::string path = dir + "\\" + fd.cFileName;
        HMODULE dll = LoadLibraryA(path.c_str());
        if (!dll) {
            spdlog::warn("[BotLoader] Failed to load '{}': error {}", fd.cFileName, GetLastError());
            continue;
        }

        auto bot = std::make_unique<BotInstance>();
        bot->dll = dll;

        bot->fn_name    = (PFN_bot_name)    GetProcAddress(dll, "bot_name");
        bot->fn_create  = (PFN_bot_create)  GetProcAddress(dll, "bot_create");
        bot->fn_destroy = (PFN_bot_destroy) GetProcAddress(dll, "bot_destroy");
        bot->fn_start   = (PFN_bot_start)   GetProcAddress(dll, "bot_start");
        bot->fn_stop    = (PFN_bot_stop)    GetProcAddress(dll, "bot_stop");
        bot->fn_tick    = (PFN_bot_tick)    GetProcAddress(dll, "bot_tick");

        if (!bot->fn_name || !bot->fn_create || !bot->fn_destroy ||
            !bot->fn_start || !bot->fn_stop  || !bot->fn_tick) {
            spdlog::warn("[BotLoader] '{}' is missing mandatory exports — skipped.", fd.cFileName);
            FreeLibrary(dll);
            continue;
        }

        bot->fn_on_trigger    = (PFN_bot_on_trigger)    GetProcAddress(dll, "bot_on_trigger");
        bot->fn_resolve_macro = (PFN_bot_resolve_macro) GetProcAddress(dll, "bot_resolve_macro");

        bot->name   = bot->fn_name();
        bot->handle = bot->fn_create();

        if (!bot->handle) {
            spdlog::warn("[BotLoader] '{}' bot_create() returned null — skipped.", fd.cFileName);
            FreeLibrary(dll);
            continue;
        }

        spdlog::info("[BotLoader] Loaded bot '{}' from '{}'.", bot->name, fd.cFileName);
        m_bots.push_back(std::move(bot));
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

BotInstance* BotLoader::find(const std::string& name) {
    for (auto& bot : m_bots)
        if (bot->name == name) return bot.get();
    return nullptr;
}

void BotLoader::stopAll() {
    for (auto& bot : m_bots)
        bot->stop();
}
