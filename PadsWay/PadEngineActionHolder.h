#pragma once
// Macro.h pulls in <windows.h>, whose min/max macros corrupt any std::max(...)/Xxx::min()/
// Xxx::max() that appears textually afterward in the same translation unit — including inside
// catch2/catch_amalgamated.hpp's own template bodies, which is exactly what breaks Catch2 test
// files that include this header before catch2 (per CLAUDE.md's "project headers before catch2"
// rule). NOMINMAX suppresses those macros so windows.h stops shadowing std::min/std::max entirely
// — same fix already used in AppWindow.cpp for the same reason.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <unordered_map>
#include <optional>
#include <type_traits>
#include "input/ControllerConfig.h"
#include "macros/Macro.h"
#include "macros/MacroParser.h"
// spdlog directly (not the project's Log.h wrapper) — Log.h pulls in Paths.h for its init()
// helper, which this header never calls; including it here would add an untested dependency to
// whatever translation unit uses this header (e.g. Catch2 tests) for no reason. spdlog::info/warn/
// error work the same either way, using spdlog's built-in default logger when no custom one (via
// Log::init()) has been installed yet.
#include <spdlog/spdlog.h>

// ---------------------------------------------------------------------------
// ActionHolderState<KeyT> — shared state for one "action holder": a physical/virtual source
// (button, dpad direction, touch zone, touch gesture, axis half, gyro/accel half) that can drive
// Macro/Keyboard/MouseClick/Bot, and (axis/gyro/accel only) Ranges. KeyT is int for the button
// holder (HID bit index), std::string for every other holder.
//
// Extracted 2026/09/07 from 6 near-identical struct-of-maps in PadEngine.cpp (button/dpad/touch
// zone/touch gesture/axis, plus the pre-existing gyro/accel ImuActionState) — see
// SESSION_CONTEXT.md, "Refactor de codigo", tarea 2. Pure data + construction here, no OS calls —
// safe to build/inspect in Catch2 without a live PadEngine/DeviceHub/ViGEm. The per-frame tick
// (which calls into real keyboard/mouse/bot dispatch) stays local to PadEngine::threadFunc(),
// same as before — this header only covers the part that doesn't need OS/hardware access.
//
// Range fields are always std::string-keyed regardless of KeyT: Ranges only ever exists on
// HalfAxisActionType (axis/gyro/accel), and every holder that uses HalfAxisAction is itself
// string-keyed in practice (axis_actions/gyro_actions/accel_actions), so this never mismatches.
// For the int-keyed button holder these three maps simply stay empty (ButtonActionType has no
// Ranges member at all).
// ---------------------------------------------------------------------------
template <typename KeyT>
struct ActionHolderState {
    std::unordered_map<KeyT, Macro>       macros;
    std::unordered_map<KeyT, bool>        macroPrev;
    std::unordered_map<KeyT, std::string> macroNames;
    std::unordered_map<KeyT, bool>        kbPrev;
    std::unordered_map<KeyT, bool>        mousePrev;
    std::unordered_map<KeyT, std::string> botNames;
    std::unordered_map<KeyT, bool>        botPrev;

    std::unordered_map<std::string, std::optional<ButtonAction>> rangePrev;
    std::unordered_map<std::string, Macro> rangeMacros;
    std::unordered_map<std::string, bool>  rangeMacroOk;

    void clear() {
        macros.clear();    macroPrev.clear(); macroNames.clear();
        kbPrev.clear();    mousePrev.clear();
        botNames.clear();  botPrev.clear();
        rangePrev.clear(); rangeMacros.clear(); rangeMacroOk.clear();
    }
};

// Builds an ActionHolderState<KeyT> from a config actions map (values are ButtonAction or
// HalfAxisAction) + the macro library (name -> execution string). `nameField(action)` returns the
// field to use as the macro/bot name — ButtonAction uses `.name`, HalfAxisAction uses `.target`;
// the two action types don't share a field name here, so the caller supplies the accessor.
// `holderTag` names the holder in log lines ("button", "dpad", "axis direction", "touch zone",
// "gesture") — the exact wording is consolidated to one generic phrasing per action type, slightly
// different text from the original per-holder log lines but the same information.
template <typename KeyT, typename ActionMapT, typename NameFieldFn>
void initActionHolderState(ActionHolderState<KeyT>& st, const ActionMapT& actions,
                           const std::unordered_map<std::string, std::string>& macroLibrary,
                           NameFieldFn nameField, const char* holderTag) {
    st.clear();
    for (const auto& [key, action] : actions) {
        using TypeT = std::decay_t<decltype(action.type)>;

        if (action.type == TypeT::Keyboard)   st.kbPrev[key]    = false;
        if (action.type == TypeT::MouseClick) st.mousePrev[key] = false;

        if (action.type == TypeT::Bot) {
            st.botNames[key] = nameField(action);
            st.botPrev[key]  = false;
            spdlog::info("Bot '{}' assigned to {} {}.", nameField(action), holderTag, key);
        }

        if (action.type == TypeT::Macro) {
            std::string execution = action.execution;
            if (execution.empty()) {
                auto it = macroLibrary.find(nameField(action));
                if (it == macroLibrary.end()) {
                    spdlog::warn("Macro '{}' ({} {}) not found in library.", nameField(action), holderTag, key);
                    continue;
                }
                execution = it->second;
            }
            try {
                Macro m;
                MacroParser::parse(execution, m);
                st.macros[key]     = std::move(m);
                st.macroPrev[key]  = false;
                st.macroNames[key] = nameField(action);
                spdlog::info("Macro '{}' assigned to {} {}.", nameField(action), holderTag, key);
            } catch (const std::exception& ex) {
                spdlog::error("Error parsing macro '{}': {}", nameField(action), ex.what());
            }
        }

        if constexpr (std::is_same_v<TypeT, HalfAxisActionType>) {
            if (action.type == HalfAxisActionType::Ranges) {
                st.rangePrev[key] = std::nullopt;
                for (const auto& r : action.ranges) {
                    if (!r.hasAction || r.action.type != ButtonActionType::Macro) continue;
                    std::string mkey = key + "|" + r.action.name;
                    auto it = macroLibrary.find(r.action.name);
                    if (it == macroLibrary.end()) {
                        spdlog::warn("Macro '{}' ({} range {}) not found.", r.action.name, holderTag, key);
                        st.rangeMacroOk[mkey] = false;
                        continue;
                    }
                    try {
                        Macro m;
                        MacroParser::parse(it->second, m);
                        st.rangeMacros[mkey]  = std::move(m);
                        st.rangeMacroOk[mkey] = true;
                    } catch (...) {
                        spdlog::warn("Failed to parse macro '{}' ({} range {}).", r.action.name, holderTag, key);
                        st.rangeMacroOk[mkey] = false;
                    }
                }
            }
        }
    }
}
