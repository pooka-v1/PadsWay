#pragma once
#include <string>
#include "Macro.h"

// Parses a compact macro execution string into a compiled Macro.
//
// Syntax summary:
//   Buttons  : A B X Y  L1 R1 L2 R2 L3 R3  CU CD CL CR CUR CUL CDR CDL  ST SE HO
//   Analog   : LAX<v>  LAY<v>  RAX<v>  RAY<v>   (value in [-1.0 .. 1.0])
//   Combo    : token + token + ...            (simultaneous)
//   Sequence : item , item , ...             (sequential, DEFAULT_STEP_MS apart)
//   Hold     : B=1000                        (hold B for 1000 ms)
//   Wait     : 500                           (bare number = pause 500 ms)
//   Repeat   : expr*N                        (loop for N ms, default interval)
//              expr*N/M                      (loop N ms, M times)
//              expr*UP                       (loop while trigger is held)
//              expr*DO                       (toggle loop on/off)
//   Group    : ( sequence )*repeat_spec      (repeat the whole sequence)
//
// Examples:
//   "CU, CUR, CR + X"
//   "A + Y"
//   "B=1000"
//   "(A, B, C)*5000"
//   "(RAX0 + RAY1, 30, RAX1 + RAY0, 30, RAX0 + RAY-1, 30, RAX-1 + RAY0, 30)*10000"
//
class MacroParser {
public:
    // Parses 'execution' and configures 'macro'.
    // Throws std::runtime_error on any syntax error.
    static void parse(const std::string& execution, Macro& macro);

private:
    // Default timings (all in milliseconds)
    static constexpr int DEFAULT_STEP_MS  = 200;  // slot per comma-separated item
    static constexpr int DEFAULT_PRESS_MS =  80;  // button hold duration within a slot

    // --- Internal state ---
    std::string m_src;
    int         m_pos = 0;

    explicit MacroParser(const std::string& src) : m_src(src), m_pos(0) {}

    // --- Sub-results ---
    struct ParsedSeq  { std::vector<CompiledStep> steps; int durationMs = 0; };
    struct RepeatSpec { MacroRepeatMode mode = MacroRepeatMode::Once; int totalMs = 0; int cycleMs = 0; };

    // --- Top-level ---
    void doParse(Macro& macro);

    // --- Grammar levels ---
    ParsedSeq   parseSequence();
    ParsedSeq   parseItem();          // returns 0-based steps + duration
    MacroEffect parseCombo();
    void        parseToken(MacroEffect& effect);
    RepeatSpec  parseRepeatSpec(int innerCycleMs);

    // --- Unrolls a timed group into a flat step list ---
    ParsedSeq unroll(const ParsedSeq& inner, const RepeatSpec& rs);

    // --- Number parsers ---
    int   parseInt();
    float parseFloat();

    // --- Utilities ---
    void skipWS();
    bool atEnd() const;
    char peek() const;
    char consume();
    void expectChar(char c);
};
