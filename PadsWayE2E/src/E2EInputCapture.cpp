#include "E2EInputCapture.h"
#include <chrono>
#include <cstdio>

namespace {

using Clock = std::chrono::steady_clock;

// The running capture, for the hook callbacks (SetWindowsHookEx takes a plain function pointer, no
// user context). Written only while the hook thread is not running, so no atomic needed.
E2EInputCapture* g_capture = nullptr;

// The engine sends keys as scan codes (KEYEVENTF_SCANCODE), so Windows works the virtual-key out
// from the scan code and reports the sided variant: the engine's "shift" arrives as VK_LSHIFT.
// Tests speak in the generic codes the engine's key names map to.
WORD foldSides(DWORD vk) {
    switch (vk) {
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    default:                            return static_cast<WORD>(vk);
    }
}

// Reserved virtual-key that Windows itself injects when the Xbox Guide button of an XInput pad is
// pressed (it's how the Game Bar hears about it) — so any Home on the virtual pad produces one.
// Not the engine's doing: swallowed like the rest, but never recorded.
constexpr DWORD kXboxGuideNotifyVk = 0x07;

// Low-level hook callbacks run on the hook thread, inside its GetMessage loop, with the system
// waiting on them — keep them short. Returning non-zero swallows the event.
LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (code == HC_ACTION && g_capture && (key->flags & LLKHF_INJECTED)) {
        if (key->vkCode == kXboxGuideNotifyVk) return 1;
        InjectedInput in;
        in.kind = InjectedInput::Kind::Key;
        in.down = (key->flags & LLKHF_UP) == 0;
        in.vk   = foldSides(key->vkCode);
        g_capture->record(in);
        return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam) {
    const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    if (code != HC_ACTION || !g_capture || !(mouse->flags & LLMHF_INJECTED))
        return CallNextHookEx(nullptr, code, wParam, lParam);

    InjectedInput in;
    in.kind = InjectedInput::Kind::MouseButton;
    switch (wParam) {
    case WM_LBUTTONDOWN: in.down = true;  in.button = "left";   break;
    case WM_LBUTTONUP:   in.down = false; in.button = "left";   break;
    case WM_RBUTTONDOWN: in.down = true;  in.button = "right";  break;
    case WM_RBUTTONUP:   in.down = false; in.button = "right";  break;
    case WM_MBUTTONDOWN: in.down = true;  in.button = "middle"; break;
    case WM_MBUTTONUP:   in.down = false; in.button = "middle"; break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        in.down   = (wParam == WM_XBUTTONDOWN);
        in.button = (HIWORD(mouse->mouseData) == XBUTTON1) ? "x1" : "x2";
        break;
    case WM_MOUSEMOVE: {
        // `pt` is where the cursor is about to go; it hasn't moved yet (and won't: swallowed), so
        // the current position is the start point.
        POINT current{};
        GetCursorPos(&current);
        in.kind = InjectedInput::Kind::MouseMove;
        in.dx   = mouse->pt.x - current.x;
        in.dy   = mouse->pt.y - current.y;
        break;
    }
    default:
        return 1;   // wheel etc.: nothing the engine sends — swallow without recording
    }
    g_capture->record(in);
    return 1;
}

std::string keyName(WORD vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    switch (vk) {
    case VK_SHIFT:   return "SHIFT";
    case VK_CONTROL: return "CTRL";
    case VK_MENU:    return "ALT";
    default: {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
        return buf;
    }
    }
}

} // namespace

InjectedInput keyDown(WORD vk) {
    InjectedInput in;
    in.kind = InjectedInput::Kind::Key;
    in.down = true;
    in.vk   = vk;
    return in;
}

InjectedInput keyUp(WORD vk) {
    InjectedInput in = keyDown(vk);
    in.down = false;
    return in;
}

InjectedInput mouseDown(const std::string& button) {
    InjectedInput in;
    in.kind   = InjectedInput::Kind::MouseButton;
    in.down   = true;
    in.button = button;
    return in;
}

InjectedInput mouseUp(const std::string& button) {
    InjectedInput in = mouseDown(button);
    in.down = false;
    return in;
}

std::string describe(const std::vector<InjectedInput>& inputs) {
    if (inputs.empty()) return "(nothing)";
    std::string out;
    for (const auto& in : inputs) {
        if (!out.empty()) out += ", ";
        switch (in.kind) {
        case InjectedInput::Kind::Key:
            out += keyName(in.vk) + (in.down ? " down" : " up");
            break;
        case InjectedInput::Kind::MouseButton:
            out += "mouse " + in.button + (in.down ? " down" : " up");
            break;
        case InjectedInput::Kind::MouseMove: {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "move(%ld,%ld)", in.dx, in.dy);
            out += buf;
            break;
        }
        }
    }
    return out;
}

bool E2EInputCapture::start(std::string& error) {
    if (g_capture) {
        error = "another E2EInputCapture is already running";
        return false;
    }
    g_capture = this;

    std::promise<std::string> installed;
    std::future<std::string>  outcome = installed.get_future();
    m_hookThread = std::thread(&E2EInputCapture::hookThreadMain, this, std::move(installed));
    error = outcome.get();
    if (!error.empty()) {
        m_hookThread.join();
        g_capture = nullptr;
        return false;
    }
    return true;
}

void E2EInputCapture::stop() {
    if (!m_hookThread.joinable()) return;
    PostThreadMessageW(m_hookThreadId, WM_QUIT, 0, 0);
    m_hookThread.join();
    g_capture = nullptr;
}

void E2EInputCapture::hookThreadMain(std::promise<std::string> installed) {
    // A thread only gets a message queue on its first message call — create it now, so stop()'s
    // PostThreadMessage can never arrive before there's a queue to land in.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    m_hookThreadId = GetCurrentThreadId();

    // Low-level hooks are not injected into other processes: Windows calls them back on THIS
    // thread, through its message queue. That's why this thread exists and pumps messages — if it
    // stopped pumping, every keystroke on the machine would stall until Windows gave up on the hook.
    const HINSTANCE self = GetModuleHandleW(nullptr);
    HHOOK keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, self, 0);
    HHOOK mouseHook    = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, self, 0);
    if (!keyboardHook || !mouseHook) {
        const DWORD err = GetLastError();
        if (keyboardHook) UnhookWindowsHookEx(keyboardHook);
        if (mouseHook)    UnhookWindowsHookEx(mouseHook);
        installed.set_value("SetWindowsHookEx failed, error " + std::to_string(err));
        return;
    }
    installed.set_value({});

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(mouseHook);
    UnhookWindowsHookEx(keyboardHook);
}

void E2EInputCapture::record(const InjectedInput& input) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recorded.push_back(input);
}

void E2EInputCapture::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recorded.clear();
}

std::vector<InjectedInput> E2EInputCapture::recorded() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recorded;
}

bool E2EInputCapture::waitFor(const std::function<bool(const std::vector<InjectedInput>&)>& pred,
                              int timeoutMs, std::vector<InjectedInput>* lastSeen) const {
    const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<InjectedInput> seen;
    do {
        seen = recorded();
        if (pred(seen)) {
            if (lastSeen) *lastSeen = seen;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < until);
    if (lastSeen) *lastSeen = seen;
    return false;
}
