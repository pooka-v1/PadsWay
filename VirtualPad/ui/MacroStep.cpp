#define NOMINMAX
#include "MacroStep.h"
#include <cstdio>
#include <cctype>

// ---------------------------------------------------------------------------
// Token tables
// ---------------------------------------------------------------------------
const TokenDef kButtons[] = {
    {"PressA",      "A",  "A"},
    {"PressB",      "B",  "B"},
    {"PressX",      "X",  "X"},
    {"PressY",      "Y",  "Y"},
    {"PressL1",     "L1", "LB"},
    {"PressR1",     "R1", "RB"},
    {"PressL2",     "L2", "LT"},
    {"PressR2",     "R2", "RT"},
    {"PressL3",     "L3", "LS"},
    {"PressR3",     "R3", "RS"},
    {"PressStart",  "ST", "Start"},
    {"PressSelect", "SE", "Select"},
    {"PressHome",   "HO", "Home"},
};

const TokenDef kDpad[] = {
    {"CrossMoveUp",        "CU",  "Up"},
    {"CrossMoveUpRight",   "CUR", "Up-Right"},
    {"CrossMoveRight",     "CR",  "Right"},
    {"CrossMoveDownRight", "CDR", "Down-Right"},
    {"CrossMoveDown",      "CD",  "Down"},
    {"CrossMoveDownLeft",  "CDL", "Down-Left"},
    {"CrossMoveLeft",      "CL",  "Left"},
    {"CrossMoveUpLeft",    "CUL", "Up-Left"},
};

const AnalogDirDef kAnalog[] = {
    {"AnalogicMoveUp",        0.0f,   1.0f,  "Up"},
    {"AnalogicMoveUpRight",   0.71f,  0.71f, "Up-Right"},
    {"AnalogicMoveRight",     1.0f,   0.0f,  "Right"},
    {"AnalogicMoveDownRight",  0.71f, -0.71f, "Down-Right"},
    {"AnalogicMoveDown",       0.0f,  -1.0f,  "Down"},
    {"AnalogicMoveDownLeft",  -0.71f, -0.71f, "Down-Left"},
    {"AnalogicMoveLeft",      -1.0f,   0.0f,  "Left"},
    {"AnalogicMoveUpLeft",    -0.71f,  0.71f, "Up-Left"},
};

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static size_t findMatchingParen(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if      (s[i] == '(') ++depth;
        else if (s[i] == ')') { if (--depth == 0) return i; }
    }
    return std::string::npos;
}

// Parses "*5000", "*1000/10", "*UP", "*DO" into a RepeatSpec.
static RepeatSpec parseRepeatSuffix(const std::string& s) {
    RepeatSpec r;
    if (s.empty() || s[0] != '*') return r;
    std::string body = s.substr(1);
    if (body == "UP") { r.mode = RepeatSpec::Mode::HoldButton; return r; }
    if (body == "DO") { r.mode = RepeatSpec::Mode::Toggle;     return r; }
    size_t slash = body.find('/');
    if (slash != std::string::npos) {
        std::string ms  = body.substr(0, slash);
        std::string cnt = body.substr(slash + 1);
        bool ok = !ms.empty() && !cnt.empty();
        for (char c : ms)  if (!std::isdigit((unsigned char)c)) { ok = false; break; }
        for (char c : cnt) if (!std::isdigit((unsigned char)c)) { ok = false; break; }
        if (ok) {
            r.mode  = RepeatSpec::Mode::CountInDuration;
            r.ms    = std::stoi(ms);
            r.count = std::stoi(cnt);
        }
        return r;
    }
    bool allDig = !body.empty();
    for (char c : body) if (!std::isdigit((unsigned char)c)) { allDig = false; break; }
    if (allDig) { r.mode = RepeatSpec::Mode::Duration; r.ms = std::stoi(body); }
    return r;
}

// Parses "TOKEN1+TOKEN2=holdMs" into tokens + holdMs.
// Merges adjacent LA*/RA* axis pairs into compound tokens.
static std::vector<std::string> parsePressTokens(const std::string& combo, int& outHoldMs) {
    outHoldMs = 0;
    std::string body = combo;
    size_t eqPos = body.rfind('=');
    if (eqPos != std::string::npos) {
        std::string after = body.substr(eqPos + 1);
        bool allDig = !after.empty();
        for (char c : after) if (!std::isdigit((unsigned char)c)) { allDig = false; break; }
        if (allDig) { outHoldMs = std::stoi(after); body = body.substr(0, eqPos); }
    }
    std::vector<std::string> toks;
    std::string tok;
    for (char c : body) {
        if (c == '+') { auto t = trim(tok); if (!t.empty()) toks.push_back(t); tok.clear(); }
        else tok += c;
    }
    { auto t = trim(tok); if (!t.empty()) toks.push_back(t); }

    std::vector<std::string> merged;
    for (const auto& t : toks) {
        bool isAxis = t.size() >= 3 && (t[0] == 'L' || t[0] == 'R') && t[1] == 'A'
                      && (t[2] == 'X' || t[2] == 'Y');
        if (isAxis && !merged.empty()) {
            const auto& prev = merged.back();
            bool prevAxis = prev.size() >= 3 && prev[0] == t[0] && prev[1] == 'A'
                            && (prev[2] == 'X' || prev[2] == 'Y') && prev[2] != t[2];
            if (prevAxis) { merged.back() += "+" + t; continue; }
        }
        merged.push_back(t);
    }
    return merged;
}

static std::vector<std::string> splitTopLevel(const std::string& s) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if      (c == '(') ++depth;
        else if (c == ')') --depth;
        if (c == ',' && depth == 0) { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

// ---------------------------------------------------------------------------
// Analog token / image helpers
// ---------------------------------------------------------------------------
static std::string fmtFloat(float v) {
    if (v ==  0.0f) return "0";
    if (v ==  1.0f) return "1";
    if (v == -1.0f) return "-1";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2g", v);
    return buf;
}

std::string analogToken(bool rightStick, float x, float y) {
    const char* prefix = rightStick ? "RA" : "LA";
    return std::string(prefix) + "X" + fmtFloat(x)
         + "+" + prefix + "Y" + fmtFloat(y);
}

std::string tokenToImageName(const std::string& token) {
    for (int i = 0; i < kButtonsCount; ++i)
        if (token == kButtons[i].token) return kButtons[i].img;
    for (int i = 0; i < kDpadCount; ++i)
        if (token == kDpad[i].token) return kDpad[i].img;
    for (int i = 0; i < kAnalogCount; ++i) {
        if (token == analogToken(false, kAnalog[i].x, kAnalog[i].y)) return kAnalog[i].img;
        if (token == analogToken(true,  kAnalog[i].x, kAnalog[i].y)) return kAnalog[i].img;
    }
    return {};
}

// ---------------------------------------------------------------------------
// RepeatSpec::toSuffix
// ---------------------------------------------------------------------------
std::string RepeatSpec::toSuffix() const {
    switch (mode) {
    case Mode::Duration:        return "*" + std::to_string(ms);
    case Mode::CountInDuration: return "*" + std::to_string(ms) + "/" + std::to_string(count);
    case Mode::HoldButton:      return "*UP";
    case Mode::Toggle:          return "*DO";
    default:                    return {};
    }
}

// ---------------------------------------------------------------------------
// MacroStepItem::toDsl
// ---------------------------------------------------------------------------
std::string MacroStepItem::toDsl() const {
    switch (kind) {
    case Kind::Wait:
        return std::to_string(waitMs);

    case Kind::Press: {
        std::string body;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) body += "+";
            body += tokens[i];
        }
        if (holdMs > 0) body += "=" + std::to_string(holdMs);
        if (!repeat.isActive()) return body;
        // parens needed when there are multiple tokens, hold time, or both
        bool needsParens = tokens.size() > 1 || holdMs > 0;
        return needsParens ? "(" + body + ")" + repeat.toSuffix()
                           :        body       + repeat.toSuffix();
    }

    case Kind::Group: {
        std::string inner;
        for (size_t i = 0; i < children.size(); ++i) {
            if (i > 0) inner += ", ";
            inner += children[i].toDsl();
        }
        if (!repeat.isActive()) return inner;
        return "(" + inner + ")" + repeat.toSuffix();
    }

    case Kind::MacroRef: {
        if (expandedDsl.empty()) return {};
        if (!repeat.isActive()) return expandedDsl;
        return "(" + expandedDsl + ")" + repeat.toSuffix();
    }
    }
    return {};
}

// ---------------------------------------------------------------------------
// tryParseSimpleDsl
// ---------------------------------------------------------------------------
std::vector<MacroStepItem> tryParseSimpleDsl(const std::string& rawDsl) {
    std::vector<MacroStepItem> result;
    if (rawDsl.empty()) return result;

    for (const auto& rawPart : splitTopLevel(rawDsl)) {
        std::string part = trim(rawPart);
        if (part.empty()) continue;

        // --- Group or combo-with-repeat: starts with '(' ---
        if (part[0] == '(') {
            size_t close = findMatchingParen(part, 0);
            if (close == std::string::npos) return {};
            std::string inner  = trim(part.substr(1, close - 1));
            std::string suffix = trim(part.substr(close + 1));
            RepeatSpec rep = parseRepeatSuffix(suffix);

            bool hasTopComma = false;
            int d = 0;
            for (char c : inner) {
                if (c == '(') ++d; else if (c == ')') --d;
                if (c == ',' && d == 0) { hasTopComma = true; break; }
            }

            if (hasTopComma) {
                // Sequence with optional repeat -> Group
                auto children = tryParseSimpleDsl(inner);
                if (children.empty() && !inner.empty()) return {};
                MacroStepItem grp;
                grp.kind     = MacroStepItem::Kind::Group;
                grp.children = std::move(children);
                grp.repeat   = rep;
                result.push_back(std::move(grp));
            } else {
                // Combo (tokens joined by +) with optional repeat -> Press
                int holdMs = 0;
                auto tokens = parsePressTokens(inner, holdMs);
                if (tokens.empty()) return {};
                MacroStepItem press;
                press.kind   = MacroStepItem::Kind::Press;
                press.tokens = std::move(tokens);
                press.holdMs = holdMs;
                press.repeat = rep;
                result.push_back(std::move(press));
            }
            continue;
        }

        // --- Token with top-level '*': single token or combo with repeat ---
        size_t starPos = part.find('*');
        if (starPos != std::string::npos) {
            std::string tokenPart = trim(part.substr(0, starPos));
            RepeatSpec rep = parseRepeatSuffix(part.substr(starPos));
            if (rep.mode == RepeatSpec::Mode::None) return {};
            int holdMs = 0;
            auto tokens = parsePressTokens(tokenPart, holdMs);
            if (tokens.empty()) return {};
            MacroStepItem press;
            press.kind   = MacroStepItem::Kind::Press;
            press.tokens = std::move(tokens);
            press.holdMs = holdMs;
            press.repeat = rep;
            result.push_back(std::move(press));
            continue;
        }

        // --- Pure number -> Wait ---
        bool isNum = !part.empty();
        for (char c : part) if (!std::isdigit((unsigned char)c)) { isNum = false; break; }
        if (isNum) {
            MacroStepItem step;
            step.kind   = MacroStepItem::Kind::Wait;
            step.waitMs = std::stoi(part);
            result.push_back(step);
            continue;
        }

        // --- Press step ---
        int holdMs = 0;
        auto tokens = parsePressTokens(part, holdMs);
        if (tokens.empty()) return {};
        MacroStepItem step;
        step.kind   = MacroStepItem::Kind::Press;
        step.tokens = std::move(tokens);
        step.holdMs = holdMs;
        result.push_back(step);
    }
    return result;
}
