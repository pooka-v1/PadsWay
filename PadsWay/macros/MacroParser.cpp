#include "MacroParser.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

// ============================================================
//  Public entry point
// ============================================================

void MacroParser::parse(const std::string& execution, Macro& macro) {
    if (execution.empty())
        throw std::runtime_error("Macro execution string is empty");
    std::string upper(execution);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    MacroParser p(upper);
    p.doParse(macro);
}

// ============================================================
//  Top-level parse
// ============================================================

void MacroParser::doParse(Macro& macro) {
    skipWS();

    ParsedSeq       seq;
    MacroRepeatMode mode    = MacroRepeatMode::Once;
    int             totalMs = 0;
    int             cycleMs = 0;

    if (!atEnd() && peek() == '(') {
        // Parenthesized group: (sequence) [*repeat_spec]
        consume();                          // '('
        seq = parseSequence();
        skipWS();
        expectChar(')');
        skipWS();
        if (!atEnd() && peek() == '*') {
            consume();                      // '*'
            auto rs = parseRepeatSpec(seq.durationMs);
            mode    = rs.mode;
            totalMs = rs.totalMs;
            cycleMs = rs.cycleMs > 0 ? rs.cycleMs : seq.durationMs;
        } else {
            mode    = MacroRepeatMode::Once;
            cycleMs = seq.durationMs;
        }
    } else {
        // Bare sequence, optionally followed by *repeat_spec
        seq = parseSequence();
        skipWS();
        if (!atEnd() && peek() == '*') {
            consume();                      // '*'
            auto rs = parseRepeatSpec(seq.durationMs);
            mode    = rs.mode;
            totalMs = rs.totalMs;
            cycleMs = rs.cycleMs > 0 ? rs.cycleMs : seq.durationMs;
        } else {
            mode    = MacroRepeatMode::Once;
            cycleMs = seq.durationMs;
        }
    }

    // tick() uses pos = elapsed % cycleMs to match steps, so all step timings
    // must fit within [0, cycleMs). Scale proportionally when N/M caused cycleMs
    // to differ from the inner sequence duration (e.g. (L1,R1)*5000/30).
    if (seq.durationMs > 0 && cycleMs != seq.durationMs) {
        for (auto& s : seq.steps) {
            s.startMs = (int)((long long)s.startMs * cycleMs / seq.durationMs);
            s.endMs   = (int)((long long)s.endMs   * cycleMs / seq.durationMs);
            s.holdMs  = (int)((long long)s.holdMs  * cycleMs / seq.durationMs);
            int slot  = s.endMs - s.startMs;
            if (s.holdMs > slot) s.holdMs = slot;
            if (s.holdMs < 1)   s.holdMs = 1;
        }
    }

    macro.setup(std::move(seq.steps), mode, totalMs, cycleMs);
}

// ============================================================
//  Sequence  ::=  item ( ',' item )*
//  Each item returns 0-based steps; parseSequence applies the cursor offset.
// ============================================================

MacroParser::ParsedSeq MacroParser::parseSequence() {
    ParsedSeq result;
    int cursor = 0;

    while (true) {
        skipWS();
        if (atEnd() || peek() == ')') break;

        ParsedSeq item = parseItem();

        // Offset the item's steps to the current position in the sequence
        for (auto s : item.steps) {
            s.startMs += cursor;
            s.endMs   += cursor;
            result.steps.push_back(s);
        }
        cursor += item.durationMs;

        skipWS();
        if (!atEnd() && peek() == ',')
            consume();
        else
            break;
    }

    result.durationMs = cursor;
    return result;
}

// ============================================================
//  Item  ::=  NUMBER                       (wait)
//           | '(' sequence ')' ['*' spec]  (inline group, possibly repeated)
//           | combo ['=' NUMBER]           (press / hold)
//  Returns steps with offsets from 0.
// ============================================================

MacroParser::ParsedSeq MacroParser::parseItem() {
    skipWS();

    // Bare number → wait (no effect, just duration)
    if (!atEnd() && std::isdigit((unsigned char)peek())) {
        int ms = parseInt();
        return { {}, ms };
    }

    // Inline group: ( sequence ) [ * repeat_spec ]
    if (!atEnd() && peek() == '(') {
        consume();  // '('
        ParsedSeq inner = parseSequence();
        skipWS();
        expectChar(')');
        skipWS();
        if (!atEnd() && peek() == '*') {
            consume();  // '*'
            RepeatSpec rs = parseRepeatSpec(inner.durationMs);
            return unroll(inner, rs);
        }
        return inner;  // group without repeat → run once inline
    }

    // Regular combo: one or more tokens joined by '+'
    MacroEffect effect = parseCombo();

    skipWS();
    int holdMs = DEFAULT_PRESS_MS;
    int slotMs = DEFAULT_STEP_MS;
    if (!atEnd() && peek() == '=') {
        consume();  // '='
        holdMs = parseInt();
        slotMs = holdMs;
    }

    CompiledStep step;
    step.startMs = 0;
    step.holdMs  = holdMs;
    step.endMs   = slotMs;
    step.effect  = effect;
    return { {step}, slotMs };
}

// ============================================================
//  Unroll: expand a timed group into a flat step list.
//  Used when (seq)*N appears inside a larger sequence.
//  *UP and *DO cannot be unrolled — only allowed at the top level.
// ============================================================

MacroParser::ParsedSeq MacroParser::unroll(const ParsedSeq& inner, const RepeatSpec& rs) {
    if (rs.mode == MacroRepeatMode::UntilRelease || rs.mode == MacroRepeatMode::Toggle)
        throw std::runtime_error("*UP and *DO are only allowed at the top level, not inside a sequence");

    int cycleMs = rs.cycleMs > 0 ? rs.cycleMs : inner.durationMs;
    if (cycleMs <= 0) cycleMs = 1;

    ParsedSeq result;
    int cursor = 0;

    // When cycleMs differs from the inner duration (e.g. (L1,R1)*5000/30),
    // scale step timings proportionally so every step fits within the cycle.
    bool needsScale = (inner.durationMs > 0 && inner.durationMs != cycleMs);

    while (cursor < rs.totalMs) {
        int cycleEnd = (std::min)(cursor + cycleMs, rs.totalMs);

        for (const auto& s : inner.steps) {
            int sStart, sHold, sEnd;
            if (needsScale) {
                sStart = (int)((long long)s.startMs * cycleMs / inner.durationMs);
                sEnd   = (int)((long long)s.endMs   * cycleMs / inner.durationMs);
                sHold  = (int)((long long)s.holdMs  * cycleMs / inner.durationMs);
                int slot = sEnd - sStart;
                if (sHold > slot) sHold = slot;
                if (sHold < 1)   sHold = 1;
            } else {
                sStart = s.startMs;
                sHold  = s.holdMs;
                sEnd   = s.endMs;
            }

            int absStart = sStart + cursor;
            if (absStart >= rs.totalMs) break;

            CompiledStep step = s;
            step.startMs = absStart;
            step.holdMs  = (std::min)(sHold, (std::max)(0, cycleEnd - absStart));
            step.endMs   = (std::min)(sEnd + cursor, cycleEnd);

            if (step.holdMs > 0)
                result.steps.push_back(step);
        }
        cursor += cycleMs;
    }

    result.durationMs = rs.totalMs;
    return result;
}

// ============================================================
//  Combo  ::=  token ( '+' token )*
// ============================================================

MacroEffect MacroParser::parseCombo() {
    MacroEffect effect;
    while (true) {
        skipWS();
        parseToken(effect);
        skipWS();
        if (!atEnd() && peek() == '+')
            consume();  // '+' → another token in this combo
        else
            break;
    }
    return effect;
}

// ============================================================
//  Token  ::=  button_name  |  analog_name float_value
// ============================================================

void MacroParser::parseToken(MacroEffect& effect) {
    skipWS();

    // Read the alphabetic part of the token name
    std::string name;
    while (!atEnd() && std::isupper((unsigned char)peek()))
        name += consume();

    if (name.empty())
        throw std::runtime_error("Expected token name at position " + std::to_string(m_pos));

    // ---- Analog token: exactly [LR]A[XY] followed by a float ----
    if (name.size() == 3
        && (name[0] == 'L' || name[0] == 'R')
        && name[1] == 'A'
        && (name[2] == 'X' || name[2] == 'Y'))
    {
        float val = parseFloat();
        // Clamp to [-1, 1]
        if (val < -1.0f) val = -1.0f;
        if (val >  1.0f) val =  1.0f;

        bool isRight = (name[0] == 'R');
        bool isX     = (name[2] == 'X');
        if (isRight) {
            if (isX) effect.rightX = val; else effect.rightY = val;
            effect.hasRightStick = true;
        } else {
            if (isX) effect.leftX  = val; else effect.leftY  = val;
            effect.hasLeftStick  = true;
        }
        return;
    }

    // ---- Button tokens that end with a digit (L1, R1, L2, R2, L3, R3) ----
    if (!atEnd() && std::isdigit((unsigned char)peek()))
        name += consume();  // read exactly one digit

    // ---- Match against known button names ----
    if      (name == "A")   effect.btnA  = true;
    else if (name == "B")   effect.btnB  = true;
    else if (name == "X")   effect.btnX  = true;
    else if (name == "Y")   effect.btnY  = true;
    else if (name == "L1")  effect.btnL1 = true;
    else if (name == "R1")  effect.btnR1 = true;
    else if (name == "L2")  effect.btnL2 = true;
    else if (name == "R2")  effect.btnR2 = true;
    else if (name == "L3")  effect.btnL3 = true;
    else if (name == "R3")  effect.btnR3 = true;
    else if (name == "CU")  { effect.dpadU = true; effect.hasDpad = true; }
    else if (name == "CD")  { effect.dpadD = true; effect.hasDpad = true; }
    else if (name == "CL")  { effect.dpadL = true; effect.hasDpad = true; }
    else if (name == "CR")  { effect.dpadR = true; effect.hasDpad = true; }
    else if (name == "CUR") { effect.dpadU = true; effect.dpadR = true; effect.hasDpad = true; }
    else if (name == "CUL") { effect.dpadU = true; effect.dpadL = true; effect.hasDpad = true; }
    else if (name == "CDR") { effect.dpadD = true; effect.dpadR = true; effect.hasDpad = true; }
    else if (name == "CDL") { effect.dpadD = true; effect.dpadL = true; effect.hasDpad = true; }
    else if (name == "ST")  effect.btnSt = true;
    else if (name == "SE")  effect.btnSe = true;
    else if (name == "HO")  { /* home button — not injectable on Xbox 360 virtual pad */ }
    else
        throw std::runtime_error("Unknown token: '" + name + "' at position " + std::to_string(m_pos));
}

// ============================================================
//  Repeat spec (called after consuming '*')
//  Formats:  N       → TimedMs, totalMs=N, cycleMs=innerCycleMs
//            N/M     → TimedMs, totalMs=N, cycleMs=N/M
//            UP      → UntilRelease
//            DO      → Toggle
// ============================================================

MacroParser::RepeatSpec MacroParser::parseRepeatSpec(int innerCycleMs) {
    skipWS();
    RepeatSpec rs;

    // Keyword: UP or DO
    if (!atEnd() && std::isupper((unsigned char)peek())) {
        std::string kw;
        while (!atEnd() && std::isupper((unsigned char)peek()))
            kw += consume();

        if (kw == "UP") {
            rs.mode    = MacroRepeatMode::UntilRelease;
            rs.cycleMs = innerCycleMs;
        } else if (kw == "DO") {
            rs.mode    = MacroRepeatMode::Toggle;
            rs.cycleMs = innerCycleMs;
        } else {
            throw std::runtime_error("Unknown repeat mode '" + kw + "' — expected UP or DO");
        }
        return rs;
    }

    // Numeric: N  or  N/M
    int N = parseInt();
    skipWS();

    if (!atEnd() && peek() == '/') {
        consume();  // '/'
        int M = parseInt();
        if (M <= 0)
            throw std::runtime_error("Repeat count M must be > 0");
        rs.mode    = MacroRepeatMode::TimedMs;
        rs.totalMs = N;
        rs.cycleMs = N / M;
        if (rs.cycleMs < 1) rs.cycleMs = 1;
    } else {
        rs.mode    = MacroRepeatMode::TimedMs;
        rs.totalMs = N;
        rs.cycleMs = innerCycleMs;  // keep the inner sequence tempo
    }

    return rs;
}

// ============================================================
//  Number parsers
// ============================================================

int MacroParser::parseInt() {
    skipWS();
    if (atEnd() || !std::isdigit((unsigned char)peek()))
        throw std::runtime_error("Expected integer at position " + std::to_string(m_pos));
    int val = 0;
    while (!atEnd() && std::isdigit((unsigned char)peek()))
        val = val * 10 + (consume() - '0');
    return val;
}

float MacroParser::parseFloat() {
    skipWS();
    float sign = 1.0f;
    if (!atEnd() && peek() == '-') { consume(); sign = -1.0f; }

    if (atEnd() || !std::isdigit((unsigned char)peek()))
        throw std::runtime_error("Expected number at position " + std::to_string(m_pos));

    float val = 0.0f;
    while (!atEnd() && std::isdigit((unsigned char)peek()))
        val = val * 10.0f + (float)(consume() - '0');

    if (!atEnd() && peek() == '.') {
        consume();
        float div = 10.0f;
        while (!atEnd() && std::isdigit((unsigned char)peek())) {
            val += (float)(consume() - '0') / div;
            div *= 10.0f;
        }
    }
    return sign * val;
}

// ============================================================
//  Utilities
// ============================================================

void MacroParser::skipWS() {
    while (!atEnd() && std::isspace((unsigned char)m_src[m_pos]))
        ++m_pos;
}

bool MacroParser::atEnd() const {
    return m_pos >= (int)m_src.size();
}

char MacroParser::peek() const {
    return m_src[m_pos];
}

char MacroParser::consume() {
    return m_src[m_pos++];
}

void MacroParser::expectChar(char c) {
    skipWS();
    if (atEnd() || peek() != c)
        throw std::runtime_error(
            std::string("Expected '") + c + "' at position " + std::to_string(m_pos));
    consume();
}
