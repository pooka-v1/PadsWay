#pragma once
#include "ControllerConfig.h"
#include "../GamepadState.h"
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// Reads a physical value (0..1) for a stick_slots source identifier.
// phys must be the physical (pre-remapping) GamepadState.
// Trigger sources: analog unless the trigger has range actions (digital then).
static inline float stickSlotSourceValue(
    const std::string& src, const GamepadState& phys,
    bool trigLHasRanges, bool trigRHasRanges)
{
    if (src == "l2") { float v = phys.triggerL; return trigLHasRanges ? (v > 0.5f ? 1.0f : 0.0f) : v; }
    if (src == "r2") { float v = phys.triggerR; return trigRHasRanges ? (v > 0.5f ? 1.0f : 0.0f) : v; }
    if (src == "dpad_up")    return phys.dpadUp    ? 1.0f : 0.0f;
    if (src == "dpad_down")  return phys.dpadDown  ? 1.0f : 0.0f;
    if (src == "dpad_left")  return phys.dpadLeft  ? 1.0f : 0.0f;
    if (src == "dpad_right") return phys.dpadRight ? 1.0f : 0.0f;
    if (src == "a")      return phys.btnA     ? 1.0f : 0.0f;
    if (src == "b")      return phys.btnB     ? 1.0f : 0.0f;
    if (src == "x")      return phys.btnX     ? 1.0f : 0.0f;
    if (src == "y")      return phys.btnY     ? 1.0f : 0.0f;
    if (src == "l1")     return phys.btnLB    ? 1.0f : 0.0f;
    if (src == "r1")     return phys.btnRB    ? 1.0f : 0.0f;
    if (src == "select") return phys.btnBack  ? 1.0f : 0.0f;
    if (src == "start")  return phys.btnStart ? 1.0f : 0.0f;
    if (src == "home")   return phys.btnHome  ? 1.0f : 0.0f;
    if (src == "l3")     return phys.btnL3    ? 1.0f : 0.0f;
    if (src == "r3")     return phys.btnR3    ? 1.0f : 0.0f;
    if (src == "l4")     return phys.btnL4    ? 1.0f : 0.0f;
    if (src == "r4")     return phys.btnR4    ? 1.0f : 0.0f;
    if (src == "lp")     return phys.btnLP    ? 1.0f : 0.0f;
    if (src == "rp")     return phys.btnRP    ? 1.0f : 0.0f;
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Applies stick_slots overrides to state.leftX/leftY/rightX/rightY.
//
// For each virtual stick pair (left, right):
//   - Slots with an entry override that half-axis from axes.
//   - Slots without an entry keep the current axes-driven half value.
//   - If any slot for a pair is defined, the resulting (X,Y) vector is
//     normalized when magnitude > 1 (circular complement behavior).
// OR-max of all sources for a slot: 1.0 if any source is active.
static inline float slotMaxValue(
    const std::vector<std::string>& sources, const GamepadState& phys,
    bool trigLR, bool trigRR)
{
    float v = 0.0f;
    for (const auto& src : sources) {
        float s = stickSlotSourceValue(src, phys, trigLR, trigRR);
        if (s > v) v = s;
    }
    return v;
}

static inline void applyStickSlots(
    const ControllerConfig& cfg, const GamepadState& phys, GamepadState& state)
{
    if (cfg.stickSlots.empty()) return;
    const bool trigLR = !cfg.triggerLRanges.empty();
    const bool trigRR = !cfg.triggerRRanges.empty();

    for (int p = 0; p < 2; ++p) {
        const char* xAxis = (p == 0) ? "left_x"  : "right_x";
        const char* yAxis = (p == 0) ? "left_y"  : "right_y";
        float&      sx    = (p == 0) ? state.leftX  : state.rightX;
        float&      sy    = (p == 0) ? state.leftY  : state.rightY;

        const std::string xp = std::string(xAxis) + "_pos";
        const std::string xn = std::string(xAxis) + "_neg";
        const std::string yp = std::string(yAxis) + "_pos";
        const std::string yn = std::string(yAxis) + "_neg";

        const bool hasAny = cfg.stickSlots.count(xp) > 0 || cfg.stickSlots.count(xn) > 0 ||
                            cfg.stickSlots.count(yp) > 0 || cfg.stickSlots.count(yn) > 0;
        if (!hasAny) continue;

        // Base halves from the current axes-driven value
        float vxPos = sx  > 0.0f ?  sx : 0.0f;
        float vxNeg = sx  < 0.0f ? -sx : 0.0f;
        float vyPos = sy  > 0.0f ?  sy : 0.0f;
        float vyNeg = sy  < 0.0f ? -sy : 0.0f;

        // OR-max slot value with the axes-driven base (slot wins only if higher).
        {
            auto it = cfg.stickSlots.find(xp);
            if (it != cfg.stickSlots.end()) { float s = slotMaxValue(it->second, phys, trigLR, trigRR); if (s > vxPos) vxPos = s; }
        }
        {
            auto it = cfg.stickSlots.find(xn);
            if (it != cfg.stickSlots.end()) { float s = slotMaxValue(it->second, phys, trigLR, trigRR); if (s > vxNeg) vxNeg = s; }
        }
        {
            auto it = cfg.stickSlots.find(yp);
            if (it != cfg.stickSlots.end()) { float s = slotMaxValue(it->second, phys, trigLR, trigRR); if (s > vyPos) vyPos = s; }
        }
        {
            auto it = cfg.stickSlots.find(yn);
            if (it != cfg.stickSlots.end()) { float s = slotMaxValue(it->second, phys, trigLR, trigRR); if (s > vyNeg) vyNeg = s; }
        }

        float fx  = vxPos - vxNeg;
        float fy  = vyPos - vyNeg;
        float mag = sqrtf(fx * fx + fy * fy);
        if (mag > 1.0f) { fx /= mag; fy /= mag; }
        sx = fx;
        sy = fy;
    }

    // Suppress dpad/trigger sources at the state level.
    // Button sources are suppressed earlier (per input source, before button
    // processing loops) so that only the slot-source button is suppressed —
    // not unrelated buttons that happen to remap to the same virtual target.
    for (const auto& [slot, srcs] : cfg.stickSlots) {
        for (const auto& src : srcs) {
            if      (src == "dpad_up")    state.dpadUp    = false;
            else if (src == "dpad_down")  state.dpadDown  = false;
            else if (src == "dpad_left")  state.dpadLeft  = false;
            else if (src == "dpad_right") state.dpadRight = false;
            else if (src == "l2")         state.triggerL  = 0.0f;
            else if (src == "r2")         state.triggerR  = 0.0f;
        }
    }
}
