#pragma once
#include <stdint.h>

#define BOT_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// Button bitmask for BotOutput.buttons.
#define BOT_BTN_A          (1u << 0)
#define BOT_BTN_B          (1u << 1)
#define BOT_BTN_X          (1u << 2)
#define BOT_BTN_Y          (1u << 3)
#define BOT_BTN_LB         (1u << 4)
#define BOT_BTN_RB         (1u << 5)
#define BOT_BTN_BACK       (1u << 6)
#define BOT_BTN_START      (1u << 7)
#define BOT_BTN_HOME       (1u << 8)
#define BOT_BTN_L3         (1u << 9)
#define BOT_BTN_R3         (1u << 10)
#define BOT_BTN_DPAD_UP    (1u << 11)
#define BOT_BTN_DPAD_DOWN  (1u << 12)
#define BOT_BTN_DPAD_LEFT  (1u << 13)
#define BOT_BTN_DPAD_RIGHT (1u << 14)

// Output produced by a bot each tick.
// buttons: OR-mask of BOT_BTN_* to inject.
// Analog fields: applied only when non-zero (lt/rt > 0, lx/ly/rx/ry != 0).
typedef struct {
    uint32_t version;       // must equal BOT_API_VERSION
    uint32_t buttons;
    float    lt;            // left trigger  [0, 1]
    float    rt;            // right trigger [0, 1]
    float    lx;            // left stick X  [-1, 1]
    float    ly;            // left stick Y  [-1, 1]
    float    rx;            // right stick X [-1, 1]
    float    ry;            // right stick Y [-1, 1]
    uint32_t _reserved[4];
} BotOutput;

typedef void* BotHandle;

// Mandatory exports — every bot DLL must provide all six.
typedef const char* (__cdecl *PFN_bot_name)();
typedef BotHandle   (__cdecl *PFN_bot_create)();
typedef void        (__cdecl *PFN_bot_destroy)(BotHandle);
typedef void        (__cdecl *PFN_bot_start)(BotHandle);
typedef void        (__cdecl *PFN_bot_stop)(BotHandle);
// Returns non-zero if *out was filled and should be applied; zero = no output.
typedef int         (__cdecl *PFN_bot_tick)(BotHandle, BotOutput*);

// Optional exports.
typedef void        (__cdecl *PFN_bot_on_trigger)(BotHandle);     // Patapon-style external trigger
typedef const char* (__cdecl *PFN_bot_resolve_macro)(BotHandle);  // FacingBot: macro name or nullptr

#ifdef __cplusplus
}
#endif
