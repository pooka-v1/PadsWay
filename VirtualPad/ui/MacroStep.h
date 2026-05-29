#pragma once
#include <string>
#include <vector>

struct TokenDef {
    const char* img;
    const char* token;
    const char* tip;
};

struct AnalogDirDef {
    const char* img;
    float       x, y;
    const char* tip;
};

extern const TokenDef     kButtons[];
extern const TokenDef     kDpad[];
extern const AnalogDirDef kAnalog[];

constexpr int kButtonsCount = 13;
constexpr int kDpadCount    = 8;
constexpr int kAnalogCount  = 8;

std::string analogToken(bool rightStick, float x, float y);
std::string tokenToImageName(const std::string& token);

// ---------------------------------------------------------------------------
// RepeatSpec — attached to Press, Group and MacroRef steps
// ---------------------------------------------------------------------------
struct RepeatSpec {
    enum class Mode { None, Duration, CountInDuration, HoldButton, Toggle };
    Mode mode  = Mode::None;
    int  ms    = 0;    // Duration and CountInDuration
    int  count = 0;    // CountInDuration: the N of *ms/N

    bool        isActive() const { return mode != Mode::None; }
    std::string toSuffix() const; // e.g. "*5000", "*1000/10", "*UP", "*DO"
};

// ---------------------------------------------------------------------------
// MacroStepItem
// ---------------------------------------------------------------------------
struct MacroStepItem {
    enum class Kind { Press, Wait, Group, MacroRef };
    Kind kind = Kind::Press;

    // Press: DSL token strings, e.g. {"A","CD"} -> "A+CD"
    // Analog compound tokens stored as a single string, e.g. "LAX0.71+LAY0.71"
    std::vector<std::string> tokens;
    int holdMs = 0;   // 0 = default (parser uses DEFAULT_PRESS_MS)

    // Wait
    int waitMs = 200;

    // Group: child steps (UI enforces one level — no nested Groups from the editor)
    std::vector<MacroStepItem> children;

    // MacroRef: name shown in the grid; expandedDsl is the macro's DSL at insert time
    std::string macroName;
    std::string expandedDsl;

    // Repeat spec — applies to Press, Group, MacroRef (not Wait)
    RepeatSpec repeat;

    std::string toDsl() const;
};

// Parses DSL into steps. Returns empty only for genuinely malformed input.
// Groups, repeat specs (*, *UP, *DO, *N/M) and MacroRefs are parsed into
// their respective Kind values.
std::vector<MacroStepItem> tryParseSimpleDsl(const std::string& rawDsl);
