#include "PadEngineActionDispatch.h"  // pulls in PadEngine.h for PadEvent
#include "bots/BotLoader.h"
// spdlog directly (not the project's Log.h wrapper) — same reasoning as PadEngineActionHolder.h:
// Log.h pulls in Paths.h for its init() helper, which this file never calls.
#include <spdlog/spdlog.h>

#include <windows.h>

// ---------------------------------------------------------------------------
// Keyboard / mouse send helpers — moved from PadEngine.cpp alongside the dispatch methods below.
// ---------------------------------------------------------------------------

static WORD keyNameToVK(const std::string& name) {
    if (name == "alt")        return VK_MENU;
    if (name == "ctrl")       return VK_CONTROL;
    if (name == "shift")      return VK_SHIFT;
    if (name == "win")        return VK_LWIN;
    if (name == "tab")        return VK_TAB;
    if (name == "enter")      return VK_RETURN;
    if (name == "esc" || name == "escape") return VK_ESCAPE;
    if (name == "space")      return VK_SPACE;
    if (name == "backspace")  return VK_BACK;
    if (name == "delete")     return VK_DELETE;
    if (name == "insert")     return VK_INSERT;
    if (name == "home_key")   return VK_HOME;
    if (name == "end")        return VK_END;
    if (name == "pageup")     return VK_PRIOR;
    if (name == "pagedown")   return VK_NEXT;
    if (name == "up")         return VK_UP;
    if (name == "down")       return VK_DOWN;
    if (name == "left")       return VK_LEFT;
    if (name == "right")      return VK_RIGHT;
    if (name == "f1")  return VK_F1;  if (name == "f2")  return VK_F2;
    if (name == "f3")  return VK_F3;  if (name == "f4")  return VK_F4;
    if (name == "f5")  return VK_F5;  if (name == "f6")  return VK_F6;
    if (name == "f7")  return VK_F7;  if (name == "f8")  return VK_F8;
    if (name == "f9")  return VK_F9;  if (name == "f10") return VK_F10;
    if (name == "f11") return VK_F11; if (name == "f12") return VK_F12;
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return static_cast<WORD>('A' + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return static_cast<WORD>(c);
        if (c >= '0' && c <= '9') return static_cast<WORD>(c);
    }
    return 0;
}

// press=true  → press all keys in order
// press=false → release all keys in reverse order
//
// Fills both wVk and wScan (+ KEYEVENTF_SCANCODE). Message-based consumers (WM_KEYDOWN, e.g. menu
// navigation) only ever needed wVk and keep working; games that read hardware scan codes via
// DirectInput/Raw Input (e.g. No Man's Sky movement) were getting a wScan of 0 and never saw the
// keypress at all — see [BUG-KEYBOARD-WASD-SCANCODE], SESSION_CONTEXT.md.
void sendKeyCombo(const std::vector<std::string>& keys, bool press) {
    if (keys.empty()) return;
    std::vector<INPUT> inputs;
    inputs.reserve(keys.size());
    auto addKey = [&](const std::string& k, bool up) {
        WORD vk = keyNameToVK(k);
        if (vk == 0) return;
        UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
        INPUT inp = {};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wVk     = vk;
        inp.ki.wScan   = static_cast<WORD>(scan & 0xFF);
        inp.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
        if ((scan >> 8) == 0xE0 || (scan >> 8) == 0xE1)
            inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        inputs.push_back(inp);
    };
    if (press) {
        for (const auto& k : keys)          addKey(k, false);
    } else {
        for (int i = (int)keys.size()-1; i >= 0; --i) addKey(keys[i], true);
    }
    if (!inputs.empty())
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void sendMouseButton(const std::string& btn, bool press) {
    INPUT inp = {};
    inp.type = INPUT_MOUSE;
    if      (btn == "left")   inp.mi.dwFlags = press ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;
    else if (btn == "right")  inp.mi.dwFlags = press ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;
    else if (btn == "middle") inp.mi.dwFlags = press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    else if (btn == "x1") { inp.mi.dwFlags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; inp.mi.mouseData = XBUTTON1; }
    else if (btn == "x2") { inp.mi.dwFlags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; inp.mi.mouseData = XBUTTON2; }
    else return;
    SendInput(1, &inp, sizeof(INPUT));
}

// ---------------------------------------------------------------------------
// PadEngineActionDispatch
// ---------------------------------------------------------------------------

PadEngineActionDispatch::PadEngineActionDispatch(std::function<void(PadEvent)> pushEvent,
                                                 OsInputSender osInput)
    : m_pushEvent(std::move(pushEvent)), m_osInput(std::move(osInput)) {}

int PadEngineActionDispatch::keyboard(bool editorOpen, bool active, bool& prev,
                                       const std::vector<std::string>& keys) {
    if (editorOpen) return 0;
    int edge = 0;
    if (active && !prev) {
        m_osInput.keyCombo(keys, true);
        std::string combo;
        for (const auto& k : keys) { if (!combo.empty()) combo += '+'; combo += k; }
        m_pushEvent({ PadEventType::KeyboardAction, combo, true });
        edge = 1;
    } else if (!active && prev) {
        m_osInput.keyCombo(keys, false);
        edge = -1;
    }
    prev = active;
    return edge;
}

int PadEngineActionDispatch::mouse(bool editorOpen, bool active, bool& prev, const std::string& btn) {
    if (editorOpen || active == prev) return 0;
    m_osInput.mouseButton(btn, active);
    if (active) m_pushEvent({ PadEventType::MouseAction, btn + " click", true });
    prev = active;
    return active ? 1 : -1;
}

void PadEngineActionDispatch::bot(bool editorOpen, bool active, bool& prev,
                                   const std::string& botName, BotLoader& botLoader) {
    if (editorOpen) return;
    if (active && !prev) {
        if (auto* b = botLoader.find(botName)) {
            b->toggle();
            spdlog::info("[BOT] '{}' {}", botName, b->isActive() ? "ON" : "OFF");
            m_pushEvent({ PadEventType::BotToggle, botName, b->isActive() });
        } else {
            spdlog::warn("[BOT] '{}' not loaded.", botName);
        }
    }
    prev = active;
}

bool PadEngineActionDispatch::macro(bool editorOpen, Macro& mac, bool active, bool& prev) {
    if (editorOpen) return false;
    bool freshPress = false;
    if (active && !prev) {
        if (mac.getMode() == MacroRepeatMode::UntilRelease) mac.start();
        else mac.toggle();
        freshPress = true;
    } else if (!active && prev) {
        if (mac.getMode() == MacroRepeatMode::UntilRelease) mac.stop();
    }
    prev = active;
    return freshPress;
}

void PadEngineActionDispatch::rangeAction(bool editorOpen, const std::string& key,
                                           std::optional<ButtonAction>& prev,
                                           const std::unordered_map<std::string, ButtonAction>& activeRangeActions,
                                           std::unordered_map<std::string, Macro>& rangeMacros,
                                           std::unordered_map<std::string, bool>& rangeMacroOk,
                                           GamepadState& state, BotLoader& botLoader) {
    if (editorOpen) return;
    auto it = activeRangeActions.find(key);
    bool isActive = (it != activeRangeActions.end());
    bool changed  = isActive
        ? (!prev.has_value() ||
           prev->type        != it->second.type        ||
           prev->name        != it->second.name        ||
           prev->mouseButton != it->second.mouseButton ||
           prev->keys        != it->second.keys)
        : prev.has_value();
    if (!changed) return;

    if (prev.has_value()) {
        if (prev->type == ButtonActionType::Keyboard)
            m_osInput.keyCombo(prev->keys, false);
        else if (prev->type == ButtonActionType::MouseClick)
            m_osInput.mouseButton(prev->mouseButton, false);
        else if (prev->type == ButtonActionType::Macro) {
            auto mit = rangeMacros.find(key + "|" + prev->name);
            if (mit != rangeMacros.end() && rangeMacroOk[key + "|" + prev->name])
                if (mit->second.getMode() == MacroRepeatMode::UntilRelease)
                    mit->second.stop();
        }
    }
    if (isActive) {
        const ButtonAction& cur = it->second;
        if (cur.type == ButtonActionType::Keyboard) {
            m_osInput.keyCombo(cur.keys, true);
            std::string combo;
            for (const auto& k : cur.keys) { if (!combo.empty()) combo += '+'; combo += k; }
            m_pushEvent({ PadEventType::KeyboardAction, combo, true });
        } else if (cur.type == ButtonActionType::MouseClick) {
            m_osInput.mouseButton(cur.mouseButton, true);
            m_pushEvent({ PadEventType::MouseAction, cur.mouseButton + " click", true });
        } else if (cur.type == ButtonActionType::Macro) {
            std::string mkey = key + "|" + cur.name;
            auto mit = rangeMacros.find(mkey);
            if (mit != rangeMacros.end() && rangeMacroOk[mkey]) {
                if (mit->second.getMode() == MacroRepeatMode::UntilRelease)
                    mit->second.start();
                else
                    mit->second.toggle();
                m_pushEvent({ PadEventType::MacroToggle, cur.name, mit->second.isActive() });
            }
        } else if (cur.type == ButtonActionType::Bot) {
            if (auto* b = botLoader.find(cur.name)) {
                b->toggle();
                spdlog::info("[BOT] '{}' {}", cur.name, b->isActive() ? "ON" : "OFF");
                m_pushEvent({ PadEventType::BotToggle, cur.name, b->isActive() });
            } else {
                spdlog::warn("[BOT] '{}' not loaded.", cur.name);
            }
        }
        prev = cur;
    } else {
        prev = std::nullopt;
    }
}
