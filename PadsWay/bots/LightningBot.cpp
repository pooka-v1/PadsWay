#include "LightningBot.h"
#include <windows.h>
#include <cstdio>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#include <timeapi.h>

// ---------------------------------------------------------------------------
// Tuning constants — adjust here without touching logic
// ---------------------------------------------------------------------------

// Horizontal line sampling — reads a full 1920-px row at a fixed Y coordinate.
// A real lightning flash turns the entire screen lavender uniformly.
// Fog covers some pixels brightly but leaves terrain pixels dark → high avg-min gap.
static constexpr int BOT_SCREEN_W      = 1920;  // game resolution width (pixels)
static constexpr int BOT_LINE_Y        = 1000;  // Y coordinate of the sample line

// Flash detection (screen goes lavender, R=206 G=206 B=254, brightness=222)
static constexpr int BOT_FLASH_THR     = 210;   // avg brightness above this = flash candidate
static constexpr int BOT_BLUE_EXCESS   = 40;    // avg B must exceed avg R by this (lavender signature)
static constexpr int BOT_UNIFORMITY_GAP = 80;   // max allowed (avgBrightness - minBrightness)
                                                 // flash: uniform ~222 → gap ~10-20
                                                 // fog:   bright patches + dark terrain → gap >80
static constexpr int BOT_SPIKE_THR     = 40;    // min brightness jump vs previous poll to count as flash
                                                 // (fog rises ~10-15/frame gradually; real bolt spikes 80+)

// Recovery detection: after the flash the screen fades back as the bolt strikes.
// We use a RELATIVE drop from the flash peak rather than a fixed threshold,
// so it adapts regardless of ambient brightness (fog, different areas, etc.).
static constexpr int BOT_RECOVERY_DROP = 40;    // brightness must fall this many points from flash peak
                                                 // flash peak ~222 → trigger when avg < 182
                                                 // (full 1920-px line stays high ~150ms then drops sharply;
                                                 //  drop=40 should catch it before the timeout fires)

static constexpr int BOT_PRESS_FRAMES  = 25;    // frames to hold A (~200ms at 8ms/frame)
                                                 // wider press window gives more margin for bolt timing variance
static constexpr int BOT_TIMEOUT_MS    = 120;   // max ms to wait after flash before pressing anyway
                                                 // fallback fires at ~260ms total (120+140 delay)
static constexpr int BOT_PRESS_DELAY_MS = 140;  // extra delay after recovery before pressing (ms)
                                                 // threshold: ~94+140=234ms from flash start (human-range timing)
                                                 // timeout:  ~120+140=260ms from flash start
static constexpr int BOT_COOLDOWN_MS   = 1500;  // min gap between A presses (ms)
static constexpr int BOT_POLL_MS       = 16;    // screen poll interval (~60 fps, requires timeBeginPeriod(1) for accuracy)

static constexpr int BOT_TARGET_DODGES = 200;   // goal for the minigame
// ---------------------------------------------------------------------------

LightningBot::LightningBot() {
    m_running = true;
    m_thread  = std::thread(&LightningBot::threadFunc, this);
}

LightningBot::~LightningBot() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void LightningBot::toggle() {
    bool next = !m_active.load();
    m_active  = next;
    if (next) {
        m_dodgeCount  = 0;   // reset counter each time bot is activated
        m_pressFrames = 0;
    } else {
        m_pressFrames = 0;   // cancel any pending press on deactivation
    }
}

bool LightningBot::consumePressA() {
    int frames = m_pressFrames.load();
    if (frames <= 0) return false;
    m_pressFrames.fetch_sub(1);
    return true;
}

// Captures a full horizontal line (BOT_SCREEN_W × 1 px) at Y = BOT_LINE_Y via BitBlt
// and returns the average brightness (0-255).
// Flash detection uses three conditions on that row:
//   1. avg brightness  > BOT_FLASH_THR       — bright enough to be a flash
//   2. avg-min gap     <= BOT_UNIFORMITY_GAP  — uniform (real flash); patchy fog fails this
//   3. avg B - avg R   > BOT_BLUE_EXCESS      — lavender colour signature
int LightningBot::sampleBrightness(bool* outIsFlash) const {
    if (outIsFlash) *outIsFlash = false;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return 0;

    HDC     hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdcScreen, BOT_SCREEN_W, 1);
    HBITMAP hOld   = (HBITMAP)SelectObject(hdcMem, hBmp);

    BitBlt(hdcMem, 0, 0, BOT_SCREEN_W, 1, hdcScreen, 0, BOT_LINE_Y, SRCCOPY);

    // Read pixels — BGR, rows DWORD-aligned
    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = BOT_SCREEN_W;
    bi.biHeight      = -1;   // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;
    bi.biCompression = BI_RGB;

    const int rowBytes = ((BOT_SCREEN_W * 3 + 3) & ~3);
    std::vector<BYTE> px(rowBytes);
    GetDIBits(hdcMem, hBmp, 0, 1, px.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    long long totalB = 0, totalG = 0, totalR = 0;
    int minBrightness = 255;

    for (int x = 0; x < BOT_SCREEN_W; ++x) {
        int off = x * 3;
        int b = px[off], g = px[off + 1], r = px[off + 2];  // BGR
        int brightness = (r + g + b) / 3;
        totalB += b;
        totalG += g;
        totalR += r;
        if (brightness < minBrightness) minBrightness = brightness;
    }

    int avgB          = (int)(totalB / BOT_SCREEN_W);
    int avgG          = (int)(totalG / BOT_SCREEN_W);
    int avgR          = (int)(totalR / BOT_SCREEN_W);
    int avgBrightness = (avgR + avgG + avgB) / 3;

    if (outIsFlash)
        *outIsFlash = (avgBrightness       >  BOT_FLASH_THR)
                   && (avgBrightness - minBrightness <= BOT_UNIFORMITY_GAP)
                   && ((avgB - avgR)       >  BOT_BLUE_EXCESS);

    return avgBrightness;
}

// Two-phase state machine:
//   IDLE       → wait for flash (brightness > FLASH_THR, lavender colour)
//   FLASH_SEEN → wait for screen to recover (brightness < RECOVERY_THR) → press A
void LightningBot::threadFunc() {
    timeBeginPeriod(1);   // set Windows timer resolution to 1ms so Sleep(8) actually sleeps ~8ms

    enum class State { IDLE, FLASH_SEEN };
    State     state          = State::IDLE;
    ULONGLONG flashTime      = 0;
    int       flashBrightness = 0;  // peak brightness at flash detection (for relative recovery)
    int       flashSpike      = 0;  // brightness spike at flash detection (for combined log line)
    int       prevBrightness  = 0;  // previous poll brightness (for spike detection)

    while (m_running) {
        Sleep(BOT_POLL_MS);

        if (!m_active) {
            state          = State::IDLE;    // reset if bot disabled mid-sequence
            prevBrightness = 0;
            continue;
        }

        bool isFlash  = false;
        int brightness = sampleBrightness(&isFlash);

        switch (state) {
        case State::IDLE:
            if (isFlash) {
                int spike = brightness - prevBrightness;
                if (spike >= BOT_SPIKE_THR) {
                    state           = State::FLASH_SEEN;
                    flashTime       = GetTickCount64();
                    flashBrightness = brightness;
                    flashSpike      = spike;
                }
            }
            break;

        case State::FLASH_SEEN: {
            ULONGLONG elapsed = GetTickCount64() - flashTime;
            bool recoveryDrop = (brightness < flashBrightness - BOT_RECOVERY_DROP);
            bool timedOut     = (elapsed >= BOT_TIMEOUT_MS);
            if (recoveryDrop || timedOut) {
                // Screen recovered → bolt is striking → wait then press A
                if (BOT_PRESS_DELAY_MS > 0) Sleep(BOT_PRESS_DELAY_MS);
                m_pressFrames = BOT_PRESS_FRAMES;
                int count = ++m_dodgeCount;
                ULONGLONG now = GetTickCount64();
                printf("[BOT] #%-3d  T=%llu  spike=+%d  +%llums [%s]  bright=%d\n",
                       count, now, flashSpike, now - flashTime,
                       timedOut ? "TIMEOUT" : "threshold", brightness);
                if (count >= BOT_TARGET_DODGES)
                    printf("[BOT] *** GOAL REACHED: %d dodges! ***\n",
                           BOT_TARGET_DODGES);
                state = State::IDLE;
                Sleep(BOT_COOLDOWN_MS);
                prevBrightness = 0;   // reset after cooldown to avoid stale delta
            }
            break;
        }
        }   // end switch

        prevBrightness = brightness;
    }       // end while

    timeEndPeriod(1);
}
