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

static constexpr int BOT_SCREEN_W       = 1920;
static constexpr int BOT_LINE_Y         = 1000;

static constexpr int BOT_FLASH_THR      = 210;
static constexpr int BOT_BLUE_EXCESS    = 40;
static constexpr int BOT_UNIFORMITY_GAP = 80;
static constexpr int BOT_SPIKE_THR      = 40;

static constexpr int BOT_RECOVERY_DROP  = 40;

static constexpr int BOT_PRESS_FRAMES   = 25;
static constexpr int BOT_TIMEOUT_MS     = 120;
static constexpr int BOT_PRESS_DELAY_MS = 140;
static constexpr int BOT_COOLDOWN_MS    = 1500;
static constexpr int BOT_POLL_MS        = 16;

static constexpr int BOT_TARGET_DODGES  = 200;
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
        m_dodgeCount  = 0;
        m_pressFrames = 0;
    } else {
        m_pressFrames = 0;
    }
}

bool LightningBot::consumePressA() {
    int frames = m_pressFrames.load();
    if (frames <= 0) return false;
    m_pressFrames.fetch_sub(1);
    return true;
}

int LightningBot::sampleBrightness(bool* outIsFlash) const {
    if (outIsFlash) *outIsFlash = false;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return 0;

    HDC     hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdcScreen, BOT_SCREEN_W, 1);
    HBITMAP hOld   = (HBITMAP)SelectObject(hdcMem, hBmp);

    BitBlt(hdcMem, 0, 0, BOT_SCREEN_W, 1, hdcScreen, 0, BOT_LINE_Y, SRCCOPY);

    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = BOT_SCREEN_W;
    bi.biHeight      = -1;
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
        int b = px[off], g = px[off + 1], r = px[off + 2];
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
        *outIsFlash = (avgBrightness                   >  BOT_FLASH_THR)
                   && (avgBrightness - minBrightness   <= BOT_UNIFORMITY_GAP)
                   && ((avgB - avgR)                   >  BOT_BLUE_EXCESS);

    return avgBrightness;
}

void LightningBot::threadFunc() {
    timeBeginPeriod(1);

    enum class State { IDLE, FLASH_SEEN };
    State     state           = State::IDLE;
    ULONGLONG flashTime       = 0;
    int       flashBrightness = 0;
    int       flashSpike      = 0;
    int       prevBrightness  = 0;

    while (m_running) {
        Sleep(BOT_POLL_MS);

        if (!m_active) {
            state          = State::IDLE;
            prevBrightness = 0;
            continue;
        }

        bool isFlash   = false;
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
                if (BOT_PRESS_DELAY_MS > 0) Sleep(BOT_PRESS_DELAY_MS);
                m_pressFrames = BOT_PRESS_FRAMES;
                int count = ++m_dodgeCount;
                ULONGLONG now = GetTickCount64();
                printf("[BOT] #%-3d  T=%llu  spike=+%d  +%llums [%s]  bright=%d\n",
                       count, now, flashSpike, now - flashTime,
                       timedOut ? "TIMEOUT" : "threshold", brightness);
                if (count >= BOT_TARGET_DODGES)
                    printf("[BOT] *** GOAL REACHED: %d dodges! ***\n", BOT_TARGET_DODGES);
                state = State::IDLE;
                Sleep(BOT_COOLDOWN_MS);
                prevBrightness = 0;
            }
            break;
        }
        }

        prevBrightness = brightness;
    }

    timeEndPeriod(1);
}
