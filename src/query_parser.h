// query_parser.h — parsing the `cursorwrap <text>` command line into
// setting changes. Pure: no OS headers, no JSON — the caller maps the
// outcome onto its settings struct and persistence.
//
// Grammar (case-insensitive, zh and en, first match wins; the whole text is
// one command, there is no chaining):
//   (empty)                     -> Show (the status row)
//   impact | 撞击               -> Toggle impactGate
//   impact on|off | 撞击 开|关  -> Set impactGate
//   speed <n> | 速度 <n>        -> Set impactSpeedPps (with the gate on)
//   mode | 模式                 -> Cycle wrapMode
//   mode both|vertical|horizontal | 模式 双向|上下|左右
//   ctrl | shift | always | 按住ctrl | 按住shift | 总是 -> Set activation
//   guard <n> | 防震 <n>        -> Set wrapGuardPx
//   edge <n> | 边缘 <n>         -> Set edgeThresholdPx
//   tol <n> | 容差 <n>          -> Set adjacencyTolerancePx
//   reset | 恢复默认            -> ResetToDefaults
//   anything else               -> Unknown (an error row suggests the words)
#pragma once

#include "wrap_logic.h"

#include <optional>
#include <string>

namespace cw {

enum class CommandKind {
    Show,           // empty text: just the status row
    ToggleImpact,
    SetImpact,      // value: 0 = off, 1 = on
    SetImpactSpeed, // value: px/s
    CycleMode,
    SetMode,        // value: WrapMode
    SetActivation,  // value: ActivationMode
    SetGuard,       // value: px
    SetEdge,        // value: px
    SetTolerance,   // value: px
    ResetToDefaults,
    Unknown,
};

struct ParsedCommand {
    CommandKind kind = CommandKind::Show;
    long long value = 0; // SetImpact: 0/1; the rest: the clamped number or enum int
    std::string word;    // the token that matched (for the echo in the row)
};

// Lowercase ASCII; zh words match byte-wise, so no case folding is needed
// for them.
std::string asciiLower(const std::string& text);

// Parse the query text (the part after the keyword). Pure and total: any
// input yields a command; numbers outside a setting's documented range clamp
// into it (the same ranges applySettings enforces).
ParsedCommand parseCommand(const std::string& text);

} // namespace cw
