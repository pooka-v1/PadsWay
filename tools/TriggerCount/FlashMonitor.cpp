#include "FlashMonitor.h"
#include <windows.h>
#include <cstdio>
#include <vector>

#pragma comment(lib, "gdi32.lib")

// ---------------------------------------------------------------------------
// Tuning constants — same values as LightningBot for consistency
// ---------------------------------------------------------------------------
static constexpr int MON_SCREEN_W       = 1920;
static constexpr int MON_LINE_Y         = 1000;

static constexpr int MON_FLASH_THR      = 210;
static constexpr int MON_BLUE_EXCESS    = 40;
static constexpr int MON_UNIFORMITY_GAP = 80;
static constexpr int MON_SPIKE_THR      = 40;

static constexpr int MON_RECOVERY_DROP  = 40;
static constexpr int MON_COOLDOWN_MS    = 1500;
static constexpr int MON_POLL_MS        = 16;
// ---------------------------------------------------------------------------

FlashMonitor::FlashMonitor() {
    m_running = true;
    m_thread  = std::thread(&FlashMonitor::threadFunc, this);
}

FlashMonitor::~FlashMonitor() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void FlashMonitor::toggle() {
    bool next = !m_active.load();
    m_active  = next;
    if (next)
        m_flashCount = 0;   // reset counter each time monitoring is activated
}

// Captures a full horizontal line (MON_SCREEN_W x 1 px) at Y = MON_LINE_Y via BitBlt.
// Returns average brightness (0-255). Sets *outIsFlash if all three flash conditions hold:
//   1. avg brightness  > MON_FLASH_THR        — bright enough
//   2. avg-min gap     <= MON_UNIFORMITY_GAP   — uniform (real flash, not patchy fog)
//   3. avg B - avg R   > MON_BLUE_EXCESS       — lavender colour signature
int FlashMonitor::sampleBrightness(bool* outIsFlash) const {
    if (outIsFlash) *outIsFlash = false;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return 0;

    HDC     hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdcScreen, MON_SCREEN_W, 1);
    HBITMAP hOld   = (HBITMAP)SelectObject(hdcMem, hBmp);

    BitBlt(hdcMem, 0, 0, MON_SCREEN_W, 1, hdcScreen, 0, MON_LINE_Y, SRCCOPY);

    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = MON_SCREEN_W;
    bi.biHeight      = -1;   // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;
    bi.biCompression = BI_RGB;

    const int rowBytes = ((MON_SCREEN_W * 3 + 3) & ~3);
    std::vector<BYTE> px(rowBytes);
    GetDIBits(hdcMem, hBmp, 0, 1, px.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    long long totalB = 0, totalG = 0, totalR = 0;
    int minBrightness = 255;

    for (int x = 0; x < MON_SCREEN_W; ++x) {
        int off = x * 3;
        int b = px[off], g = px[off + 1], r = px[off + 2];  // BGR
        int brightness = (r + g + b) / 3;
        totalB += b;
        totalG += g;
        totalR += r;
        if (brightness < minBrightness) minBrightness = brightness;
    }

    int avgB          = (int)(totalB / MON_SCREEN_W);
    int avgG          = (int)(totalG / MON_SCREEN_W);
    int avgR          = (int)(totalR / MON_SCREEN_W);
    int avgBrightness = (avgR + avgG + avgB) / 3;

    if (outIsFlash)
        *outIsFlash = (avgBrightness                 >  MON_FLASH_THR)
                   && (avgBrightness - minBrightness <= MON_UNIFORMITY_GAP)
                   && ((avgB - avgR)                 >  MON_BLUE_EXCESS);

    return avgBrightness;
}

// Two-phase state machine (identical logic to LightningBot but no button press):
//   IDLE       → wait for flash
//   FLASH_SEEN → wait for recovery → log flash, enter cooldown
void FlashMonitor::threadFunc() {
    enum class State { IDLE, FLASH_SEEN };
    State     state           = State::IDLE;
    ULONGLONG flashTime       = 0;
    int       flashBrightness = 0;
    int       flashSpike      = 0;
    int       prevBrightness  = 0;

    while (m_running) {
        Sleep(MON_POLL_MS);

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
                if (spike >= MON_SPIKE_THR) {
                    state           = State::FLASH_SEEN;
                    flashTime       = GetTickCount64();
                    flashBrightness = brightness;
                    flashSpike      = spike;
                }
            }
            break;

        case State::FLASH_SEEN: {
            ULONGLONG elapsed     = GetTickCount64() - flashTime;
            bool      recoveryDrop = (brightness < flashBrightness - MON_RECOVERY_DROP);
            bool      timedOut     = (elapsed >= 120);   // same 120 ms timeout as bot
            if (recoveryDrop || timedOut) {
                int count = ++m_flashCount;
                printf("[FLASH] #%-3d  T=%llu  spike=+%d  +%llums [%s]\n",
                       count, GetTickCount64(), flashSpike, elapsed,
                       timedOut ? "TIMEOUT" : "threshold");
                state = State::IDLE;
                Sleep(MON_COOLDOWN_MS);
                prevBrightness = 0;
            }
            break;
        }
        }

        prevBrightness = brightness;
    }
}
