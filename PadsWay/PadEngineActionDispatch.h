#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <functional>
#include "GamepadState.h"
#include "input/ControllerConfig.h"
#include "macros/Macro.h"
#include "PadEngine.h"  // PadEvent — std::function<void(PadEvent)> needs the complete type

class BotLoader;

// Sends a key combo via SendInput — press in order, release in reverse. Moved here from
// PadEngine.cpp alongside the dispatch logic that owns most of its call sites; PadEngine.cpp's
// trigger-range dispatch (which has its own vector-indexed shape, see the comment at its call site)
// still calls these directly for its own Keyboard/MouseClick cases.
void sendKeyCombo(const std::vector<std::string>& keys, bool press);
void sendMouseButton(const std::string& btn, bool press);

// OS-level input injection used by PadEngineActionDispatch. Defaults to the real SendInput-backed
// helpers above; PadsWayTests swaps in recording fakes so the edge/dispatch logic can be exercised
// without pressing real keys on the machine running the tests.
struct OsInputSender {
    std::function<void(const std::vector<std::string>&, bool)> keyCombo    = sendKeyCombo;
    std::function<void(const std::string&, bool)>               mouseButton = sendMouseButton;
};

// ---------------------------------------------------------------------------
// PadEngineActionDispatch — shared edge-triggered dispatch mechanics for Macro/Keyboard/
// MouseClick/Bot actions. Every action-holder (button/dpad/axis/gyro/accel/touch zone/trigger)
// fires its assigned action through one of these methods instead of re-implementing the same
// press-edge/release-edge bookkeeping itself.
//
// Extracted 2026/09/16 from 5 near-identical lambdas local to PadEngine::threadFunc() — see
// SESSION_CONTEXT.md, "Refactor de codigo". Construction takes a push-event callback instead of a
// PadEngine& because PadEngine::pushEvent() is private (and this class has no other reason to know
// about PadEngine at all).
//
// `editorOpen` is passed explicitly on every call, not captured, matching the original lambdas:
// while the Mapeador is open, re-selecting an already-mapped source (e.g. holding a button on a
// touch zone that already fires a macro) must not leak real keystrokes/clicks/macro starts to the
// OS mid-edit. Bailing out *before* touching `prev` (rather than updating it and only gating the
// dispatch) means a source held across the open->close transition still fires correctly once the
// editor closes, instead of the transition being silently missed.
// ---------------------------------------------------------------------------
class PadEngineActionDispatch {
public:
    // osInput defaults to the real SendInput helpers — only tests pass their own.
    explicit PadEngineActionDispatch(std::function<void(PadEvent)> pushEvent,
                                     OsInputSender osInput = {});

    // Returns the edge that just fired: 1 = fresh press, -1 = fresh release, 0 = none/suppressed.
    int keyboard(bool editorOpen, bool active, bool& prev, const std::vector<std::string>& keys);
    int mouse(bool editorOpen, bool active, bool& prev, const std::string& btn);

    void bot(bool editorOpen, bool active, bool& prev, const std::string& botName, BotLoader& botLoader);

    // Start/stop mechanic only — does NOT tick the macro. Most callers tick right after this
    // returns (a plain `macro.tick(state);`); the button holder keeps its own separate tick pass
    // (rotation-lap auto-off counting), so ticking here would double-tick for it. Returns whether a
    // fresh press just started/toggled the macro, for callers with extra per-source bookkeeping.
    bool macro(bool editorOpen, Macro& mac, bool active, bool& prev);

    // Exactly one action out of a keyed set of exclusive ranges can be active at a time (e.g. a
    // stick axis or gyro/accel direction split into magnitude bands). Releases whichever action was
    // previously active when the current one changes (including "went back to nothing"), then
    // activates the new one.
    void rangeAction(bool editorOpen, const std::string& key, std::optional<ButtonAction>& prev,
                      const std::unordered_map<std::string, ButtonAction>& activeRangeActions,
                      std::unordered_map<std::string, Macro>& rangeMacros,
                      std::unordered_map<std::string, bool>& rangeMacroOk,
                      GamepadState& state, BotLoader& botLoader);

private:
    std::function<void(PadEvent)> m_pushEvent;
    OsInputSender                 m_osInput;
};
