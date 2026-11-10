// query_parser.h — parsing the `cursorwrap <text>` command line into
// setting changes. Pure: no OS headers, no JSON — the caller maps the
// outcome onto its settings struct and persistence.
//
// Grammar (case-insensitive, zh and en, first match wins; the whole text is
// one command, there is no chaining):
//   (empty)                     -> Show (the settings list)
//   impact | 撞击               -> MenuImpact (submenu) when no value follows
//   impact on|off | 撞击 开|关  -> Set impactGate
//   speed | 速度                -> MenuSpeed (submenu) when no value follows
//   speed <n> | 速度 <n>        -> Set impactSpeedPps (with the gate on)
//   band | 区间                 -> MenuBand (submenu) when no value follows
//   band <n> | 区间 <n>         -> Set impactBandPx
//   mode | 模式                 -> MenuMode (submenu) when no value follows
//   mode both|vertical|horizontal | 模式 双向|上下|左右 -> Set wrapMode
//   ctrl | shift | always | 按住ctrl | 按住shift | 总是 -> Set activation
//   guard | 防震                -> MenuGuard (submenu) when no value follows
//   guard <n> | 防震 <n>        -> Set wrapGuardPx
//   edge | 边缘                 -> MenuEdge (submenu) when no value follows
//   edge <n> | 边缘 <n>         -> Set edgeThresholdPx
//   tol | 容差                  -> MenuTolerance (submenu) when no value follows
//   tol <n> | 容差 <n>          -> Set adjacencyTolerancePx
//   diag | 诊断                 -> Copy diagnostics (CopyDiagnostics)
//   reset | 恢复默认            -> ResetToDefaults
//   anything else               -> Unknown (an error row suggests the words)
#pragma once

#include "wrap_logic.h"

#include <optional>
#include <string>

namespace cw {

enum class CommandKind {
    Show,           // empty text: the settings list
    MenuImpact,     // impact submenu (drill-down)
    MenuSpeed,      // speed submenu
    MenuBand,       // band submenu
    MenuMode,       // mode submenu
    MenuActivation, // activation submenu
    MenuGuard,      // guard submenu
    MenuEdge,       // edge submenu
    MenuTolerance,  // tolerance submenu
    ToggleImpact,
    SetImpact,      // value: 0 = off, 1 = on
    SetImpactSpeed, // value: px/s
    SetBand,        // value: px
    CycleMode,
    SetMode,        // value: WrapMode
    SetActivation,  // value: ActivationMode
    SetGuard,       // value: px
    SetEdge,        // value: px
    SetTolerance,   // value: px
    CopyDiagnostics,
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
