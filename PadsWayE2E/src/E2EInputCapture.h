#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// One keyboard/mouse event injected with SendInput (by the engine, in practice) and caught by the
// low-level hooks before Windows acted on it.
struct InjectedInput {
    enum class Kind { Key, MouseButton, MouseMove };
    Kind        kind = Kind::Key;
    bool        down = false;   // Key / MouseButton: press (true) or release
    WORD        vk   = 0;       // Key: virtual-key code, left/right variants folded (VK_LSHIFT -> VK_SHIFT)
    std::string button;         // MouseButton: "left" "right" "middle" "x1" "x2" (ButtonAction::mouseButton names)
    LONG        dx = 0, dy = 0; // MouseMove: where the cursor would have gone, after Windows pointer ballistics

    bool operator==(const InjectedInput&) const = default;
};

InjectedInput keyDown(WORD vk);
InjectedInput keyUp(WORD vk);
InjectedInput mouseDown(const std::string& button);
InjectedInput mouseUp(const std::string& button);

// Compact form for failure messages, e.g. "SHIFT down, K down". Empty reads as "(nothing)".
std::string describe(const std::vector<InjectedInput>& inputs);

// ---------------------------------------------------------------------------
// E2EInputCapture — WH_KEYBOARD_LL + WH_MOUSE_LL hooks that record every INJECTED keyboard/mouse
// event and swallow it, so the engine's keyboard/mouse actions are observable and never reach the
// desktop (no stuck Shift, no cursor jumping around). Real (non-injected) input passes untouched.
//
// Only one instance may be running at a time (the hook callbacks are plain functions that reach it
// through a file-level pointer).
// ---------------------------------------------------------------------------
class E2EInputCapture {
public:
    E2EInputCapture() = default;
    E2EInputCapture(const E2EInputCapture&) = delete;
    E2EInputCapture& operator=(const E2EInputCapture&) = delete;
    ~E2EInputCapture() { stop(); }

    bool start(std::string& error);
    void stop();

    void clear();
    std::vector<InjectedInput> recorded() const;
    // Polls the recording until `pred` holds or `timeoutMs` passes. `lastSeen` (optional) receives
    // the last polled recording either way, for failure messages.
    bool waitFor(const std::function<bool(const std::vector<InjectedInput>&)>& pred, int timeoutMs,
                 std::vector<InjectedInput>* lastSeen = nullptr) const;

    // Called from the hook callbacks only.
    void record(const InjectedInput& input);

private:
    // Installs the hooks, reports the outcome through `installed` (empty string = OK), then pumps
    // messages until stop() posts WM_QUIT.
    void hookThreadMain(std::promise<std::string> installed);

    std::thread                m_hookThread;
    DWORD                      m_hookThreadId = 0;
    mutable std::mutex         m_mutex;
    std::vector<InjectedInput> m_recorded;
};
