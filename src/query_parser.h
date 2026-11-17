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
//   accel | 加速度              -> MenuAccel (submenu) when no value follows
//   accel <n> | 加速度 <n>      -> Set impactAccelPps2 (px/s²; 0 turns the
//                                  acceleration condition off)
//   mode | 模式                 -> MenuMode (submenu) when no value follows
//   mode both|vertical|horizontal | 模式 双向|上下|左右 -> Set wrapMode
//   ctrl | shift | always | 按住ctrl | 按住shift | 总是 -> Set activation
//   activation | 激活           -> MenuActivation (submenu) when no value follows
//   guard | 防震                -> MenuGuard (submenu) when no value follows
//   guard <n> | 防震 <n>        -> Set wrapGuardPx
//   edge | 边缘                 -> MenuEdge (submenu) when no value follows
//   edge <n> | 边缘 <n>         -> Set edgeThresholdPx
//   tol | 容差                  -> MenuTolerance (submenu) when no value follows
//   tol <n> | 容差 <n>          -> Set adjacencyTolerancePx
//   drag | 拖拽                 -> MenuDrag (submenu) when no value follows
//   drag on|off | 拖拽 开|关    -> Set disableDuringDrag
//   single | 单屏               -> MenuSingle (submenu) when no value follows
//   single on|off | 单屏 开|关  -> Set disableOnSingleMonitor
//   tune | tuner | 调参 | 可视化 -> MenuTuner (the overlay's own submenu)
//   tune on|off | 调参 开|关    -> Set overlayOn (turn the overlay on or off)
//   trace | 轨迹                -> Copy the arrival trace as CSV (CopyTrace)
//   clear | 清除轨迹            -> Drop the recorded arrivals (ClearTrace)
//   best | 最优                 -> MenuBest (the suggested gate; applying it is
//                                  an explicit action, never a side effect)
//   data | window | 数据 | 窗口 -> MenuData (the live data window's submenu)
//   data on|off | 数据 开|关    -> SetDataWindow (open or close the window)
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
    MenuAccel,      // acceleration submenu
    MenuMode,       // mode submenu
    MenuActivation, // activation submenu
    MenuGuard,      // guard submenu
    MenuEdge,       // edge submenu
    MenuTolerance,  // tolerance submenu
    MenuDrag,       // "no wrap while dragging" submenu
    MenuSingle,     // "disable on a single monitor" submenu
    MenuTuner,      // tuning-overlay submenu
    MenuBest,       // the suggested gate: show it; applying it is an action
    MenuData,       // the live data window's submenu
    ToggleImpact,
    SetImpact,      // value: 0 = off, 1 = on
    SetImpactSpeed, // value: px/s
    SetBand,        // value: px
    SetAccel,       // value: px/s² (0 = off)
    CycleMode,
    SetMode,        // value: WrapMode
    SetActivation,  // value: ActivationMode
    SetGuard,       // value: px
    SetEdge,        // value: px
    SetTolerance,   // value: px
    SetDrag,        // value: 0 = wrap while dragging, 1 = do not
    SetSingle,      // value: 0 = wrap on one monitor, 1 = do not
    SetOverlay,     // value: 0 = overlay off, 1 = overlay on
    SetData,        // value: 0 = data window closed, 1 = open
    SetImpactFlash, // value: 0 = off, 1 = on
    CopyDiagnostics,
    CopyTrace,   // copy the recorded arrivals as CSV
    ClearTrace,  // drop the recorded arrivals
    ToggleTuner, // kept for compatibility: same as SetOverlay with the inverse
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
