#pragma once
// ---------------------------------------------------------------------------
// MappingHelpers — pure utility functions shared between AppWindow and
// MappingEditor (previously static functions in AppWindow.cpp).
// All functions are inline to avoid ODR violations when included from
// multiple translation units.
// ---------------------------------------------------------------------------
#include <string>
#include <utility>
#include <cmath>
#include "../GamepadState.h"
#include "../ui/PadView.h"   // PadLayout, PadComponent

inline void readStickXY(const GamepadState& s, const std::string& stateX, float& outX, float& outY) {
    if (stateX == "leftX")  { outX = s.leftX;  outY = s.leftY;  return; }
    if (stateX == "rightX") { outX = s.rightX; outY = s.rightY; return; }
    outX = outY = 0.0f;
}

inline std::pair<std::string,std::string> stickIdsFromStateX(const std::string& stateX) {
    if (stateX == "leftX")  return {"left_x",  "left_y"};
    if (stateX == "rightX") return {"right_x", "right_y"};
    return {"", ""};
}

inline std::string dpadDirToState(const PadComponent& c, const std::string& dir) {
    if (dir == "up")    return c.stateUp;
    if (dir == "down")  return c.stateDown;
    if (dir == "left")  return c.stateLeft;
    if (dir == "right") return c.stateRight;
    return {};
}

inline std::string dpadDirFromMouse(ImVec2 mouse, float cx, float cy) {
    float dx = mouse.x - cx;
    float dy = mouse.y - cy;
    return (std::abs(dx) >= std::abs(dy))
        ? (dx >= 0 ? "right" : "left")
        : (dy >= 0 ? "down"  : "up");
}

inline const char* xboxBtnLabel(const std::string& key) {
    static const std::pair<const char*, const char*> kLabels[] = {
        {"a","A"}, {"b","B"}, {"x","X"}, {"y","Y"},
        {"l1","LB"}, {"r1","RB"}, {"l3","L3"}, {"r3","R3"},
        {"select","Select"}, {"start","Start"}, {"home","Home"},
    };
    for (auto& [k, v] : kLabels) if (key == k) return v;
    return key.c_str();
}

inline bool isStateActive(const GamepadState& s, const std::string& n) {
    if (n == "btnA")      return s.btnA;
    if (n == "btnB")      return s.btnB;
    if (n == "btnX")      return s.btnX;
    if (n == "btnY")      return s.btnY;
    if (n == "btnLB")     return s.btnLB;
    if (n == "btnRB")     return s.btnRB;
    if (n == "btnBack")   return s.btnBack;
    if (n == "btnStart")  return s.btnStart;
    if (n == "btnHome")   return s.btnHome;
    if (n == "btnL3")     return s.btnL3;
    if (n == "btnR3")     return s.btnR3;
    if (n == "btnL4")     return s.btnL4;
    if (n == "btnR4")     return s.btnR4;
    if (n == "btnLP")     return s.btnLP;
    if (n == "btnRP")     return s.btnRP;
    if (n == "btnTouch")  return s.btnTouch;
    if (n == "dpadUp")    return s.dpadUp;
    if (n == "dpadDown")  return s.dpadDown;
    if (n == "dpadLeft")  return s.dpadLeft;
    if (n == "dpadRight") return s.dpadRight;
    return false;
}

inline std::string shortToState(const std::string& s) {
    static const std::pair<const char*, const char*> kBtnNames[] = {
        {"a","btnA"},     {"b","btnB"},       {"x","btnX"},     {"y","btnY"},
        {"l1","btnLB"},   {"r1","btnRB"},
        {"select","btnBack"}, {"start","btnStart"}, {"home","btnHome"},
        {"l3","btnL3"},   {"r3","btnR3"},     {"l4","btnL4"},   {"r4","btnR4"},
        {"lp","btnLP"},   {"rp","btnRP"},     {"touch_btn","btnTouch"},
        {"dpad_up","dpadUp"}, {"dpad_down","dpadDown"},
        {"dpad_left","dpadLeft"}, {"dpad_right","dpadRight"},
    };
    for (auto& [k, v] : kBtnNames) if (k == s) return v;
    return s;
}

inline std::string stateToShort(const std::string& s) {
    static const std::pair<const char*, const char*> kBtnNames[] = {
        {"a","btnA"},     {"b","btnB"},       {"x","btnX"},     {"y","btnY"},
        {"l1","btnLB"},   {"r1","btnRB"},
        {"select","btnBack"}, {"start","btnStart"}, {"home","btnHome"},
        {"l3","btnL3"},   {"r3","btnR3"},     {"l4","btnL4"},   {"r4","btnR4"},
        {"lp","btnLP"},   {"rp","btnRP"},     {"touch_btn","btnTouch"},
        {"dpad_up","dpadUp"}, {"dpad_down","dpadDown"},
        {"dpad_left","dpadLeft"}, {"dpad_right","dpadRight"},
    };
    for (auto& [k, v] : kBtnNames) if (v == s) return k;
    return s;
}

inline int findCompByState(const PadLayout& layout, const std::string& stateName) {
    for (int i = 0; i < (int)layout.components.size(); ++i) {
        const PadComponent& c = layout.components[i];
        if (c.state == stateName) return i;
        if (c.type == "stick" && c.stateClick == stateName) return i;
        if (c.type == "dpad" && stateName.rfind("dpad", 0) == 0) return i;
    }
    return -1;
}

inline void activateState(GamepadState& s, const std::string& name) {
    if      (name == "btnA")      s.btnA      = true;
    else if (name == "btnB")      s.btnB      = true;
    else if (name == "btnX")      s.btnX      = true;
    else if (name == "btnY")      s.btnY      = true;
    else if (name == "btnLB")     s.btnLB     = true;
    else if (name == "btnRB")     s.btnRB     = true;
    else if (name == "btnL3")     s.btnL3     = true;
    else if (name == "btnR3")     s.btnR3     = true;
    else if (name == "btnBack")   s.btnBack   = true;
    else if (name == "btnStart")  s.btnStart  = true;
    else if (name == "btnHome")   s.btnHome   = true;
    else if (name == "btnTouch")  s.btnTouch  = true;
    else if (name == "btnL4")     s.btnL4     = true;
    else if (name == "btnR4")     s.btnR4     = true;
    else if (name == "btnLP")     s.btnLP     = true;
    else if (name == "btnRP")     s.btnRP     = true;
    else if (name == "triggerL")  s.triggerL  = 1.0f;
    else if (name == "triggerR")  s.triggerR  = 1.0f;
    else if (name == "dpadUp")    s.dpadUp    = true;
    else if (name == "dpadDown")  s.dpadDown  = true;
    else if (name == "dpadLeft")  s.dpadLeft  = true;
    else if (name == "dpadRight") s.dpadRight = true;
}
