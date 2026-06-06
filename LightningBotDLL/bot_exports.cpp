#include "bot_api.h"
#include "LightningBot.h"

extern "C" {

__declspec(dllexport) const char* __cdecl bot_name() {
    return "LightningBot";
}

__declspec(dllexport) BotHandle __cdecl bot_create() {
    return new LightningBot();
}

__declspec(dllexport) void __cdecl bot_destroy(BotHandle h) {
    delete static_cast<LightningBot*>(h);
}

__declspec(dllexport) void __cdecl bot_start(BotHandle h) {
    auto* bot = static_cast<LightningBot*>(h);
    if (!bot->isActive()) bot->toggle();
}

__declspec(dllexport) void __cdecl bot_stop(BotHandle h) {
    auto* bot = static_cast<LightningBot*>(h);
    if (bot->isActive()) bot->toggle();
}

__declspec(dllexport) int __cdecl bot_tick(BotHandle h, BotOutput* out) {
    auto* bot = static_cast<LightningBot*>(h);
    if (bot->consumePressA()) {
        out->buttons |= BOT_BTN_A;
        return 1;
    }
    return 0;
}

} // extern "C"
