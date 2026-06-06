#pragma once
#include "bot_api.h"
#include <string>
#include <vector>
#include <memory>
#include <windows.h>

// Represents one loaded bot DLL.
struct BotInstance {
    std::string name;
    HMODULE     dll    = nullptr;
    BotHandle   handle = nullptr;
    bool        active = false;

    PFN_bot_name          fn_name          = nullptr;
    PFN_bot_create        fn_create        = nullptr;
    PFN_bot_destroy       fn_destroy       = nullptr;
    PFN_bot_start         fn_start         = nullptr;
    PFN_bot_stop          fn_stop          = nullptr;
    PFN_bot_tick          fn_tick          = nullptr;
    PFN_bot_on_trigger    fn_on_trigger    = nullptr;  // optional
    PFN_bot_resolve_macro fn_resolve_macro = nullptr;  // optional

    void start();
    void stop();
    bool tick(BotOutput* out);
    bool isActive() const { return active; }
    void toggle();
};

// Scans a directory for bot DLLs and manages their lifecycle.
class BotLoader {
public:
    ~BotLoader();

    // Scan dir for *.dll; load and validate each. Logs warnings on failures, never throws.
    void scan(const std::string& dir);

    // Find a loaded bot by name (as returned by bot_name()). Returns nullptr if not found.
    BotInstance* find(const std::string& name);

    // Stop all active bots (e.g. on device or profile change).
    void stopAll();

    const std::vector<std::unique_ptr<BotInstance>>& bots() const { return m_bots; }

private:
    std::vector<std::unique_ptr<BotInstance>> m_bots;
};
