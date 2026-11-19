// test_query_parser.cpp — the `cursorwrap <text>` grammar: keyword matching,
// zh/en aliases, number clamping, boundary gluing, unknown words.
#include "test_harness.h"
#include "query_parser.h"

using namespace cw;

CW_TEST(parser_empty_text_shows)
{
    const auto a = parseCommand("");
    CW_CHECK(a.kind == CommandKind::Show);
    const auto b = parseCommand("   ");
    CW_CHECK(b.kind == CommandKind::Show);
    const auto c = parseCommand("\t ");
    CW_CHECK(c.kind == CommandKind::Show);
}

CW_TEST(parser_impact_menu_and_set)
{
    CW_CHECK(parseCommand("impact").kind == CommandKind::MenuImpact);
    CW_CHECK(parseCommand("撞击").kind == CommandKind::MenuImpact);
    CW_CHECK(parseCommand("IMPACT").kind == CommandKind::MenuImpact); // case-insensitive
    const auto on = parseCommand("impact on");
    CW_CHECK(on.kind == CommandKind::SetImpact && on.value == 1);
    const auto off = parseCommand("impact off");
    CW_CHECK(off.kind == CommandKind::SetImpact && off.value == 0);
    const auto zhOn = parseCommand("撞击 开");
    CW_CHECK(zhOn.kind == CommandKind::SetImpact && zhOn.value == 1);
    const auto zhOff = parseCommand("撞击 关");
    CW_CHECK(zhOff.kind == CommandKind::SetImpact && zhOff.value == 0);
}

CW_TEST(parser_speed_menu_and_clamps)
{
    const auto a = parseCommand("speed 3000");
    CW_CHECK(a.kind == CommandKind::SetImpactSpeed && a.value == 3000);
    const auto b = parseCommand("速度 500");
    CW_CHECK(b.kind == CommandKind::SetImpactSpeed && b.value == 500);
    const auto lo = parseCommand("speed 1");       // below range -> clamped in
    CW_CHECK(lo.value == 500);
    const auto hi = parseCommand("speed 99999999"); // above range -> clamped in
    CW_CHECK(hi.value == 20000);
    CW_CHECK(parseCommand("speed").kind == CommandKind::MenuSpeed);
    CW_CHECK(parseCommand("速度").kind == CommandKind::MenuSpeed);
    // A word where the number goes is not a value: the submenu.
    const auto junk = parseCommand("speed abc");
    CW_CHECK(junk.kind == CommandKind::MenuSpeed);
}

CW_TEST(parser_band_menu_and_clamps)
{
    const auto a = parseCommand("band 300");
    CW_CHECK(a.kind == CommandKind::SetBand && a.value == 300);
    const auto lo = parseCommand("band 10");  // below 50 -> clamped in
    CW_CHECK(lo.value == 50);
    const auto hi = parseCommand("区间 9999"); // above 500 -> clamped in
    CW_CHECK(hi.value == 500);
    CW_CHECK(parseCommand("band").kind == CommandKind::MenuBand);
    CW_CHECK(parseCommand("区间").kind == CommandKind::MenuBand);
}

CW_TEST(parser_mode_menu_and_set)
{
    CW_CHECK(parseCommand("mode").kind == CommandKind::MenuMode);
    CW_CHECK(parseCommand("模式").kind == CommandKind::MenuMode);
    const auto both = parseCommand("mode both");
    CW_CHECK(both.kind == CommandKind::SetMode && both.value == static_cast<long long>(WrapMode::Both));
    const auto vert = parseCommand("mode vertical");
    CW_CHECK(vert.kind == CommandKind::SetMode && vert.value == static_cast<long long>(WrapMode::VerticalOnly));
    const auto horiz = parseCommand("mode horizontal");
    CW_CHECK(horiz.kind == CommandKind::SetMode && horiz.value == static_cast<long long>(WrapMode::HorizontalOnly));
    const auto zh = parseCommand("模式 左右");
    CW_CHECK(zh.kind == CommandKind::SetMode && zh.value == static_cast<long long>(WrapMode::HorizontalOnly));
}

CW_TEST(parser_activation_aliases)
{
    const auto ctrl = parseCommand("ctrl");
    CW_CHECK(ctrl.kind == CommandKind::SetActivation
             && ctrl.value == static_cast<long long>(ActivationMode::WhileCtrl));
    const auto shift = parseCommand("SHIFT");
    CW_CHECK(shift.kind == CommandKind::SetActivation
             && shift.value == static_cast<long long>(ActivationMode::WhileShift));
    const auto always = parseCommand("always");
    CW_CHECK(always.kind == CommandKind::SetActivation
             && always.value == static_cast<long long>(ActivationMode::Always));
    const auto zhCtrl = parseCommand("按住ctrl");
    CW_CHECK(zhCtrl.kind == CommandKind::SetActivation
             && zhCtrl.value == static_cast<long long>(ActivationMode::WhileCtrl));
    const auto zhAlways = parseCommand("总是");
    CW_CHECK(zhAlways.kind == CommandKind::SetActivation
             && zhAlways.value == static_cast<long long>(ActivationMode::Always));
    // Bare 按住 means Ctrl (the common case).
    const auto bare = parseCommand("按住");
    CW_CHECK(bare.kind == CommandKind::SetActivation
             && bare.value == static_cast<long long>(ActivationMode::WhileCtrl));
}

CW_TEST(parser_numeric_settings_clamp)
{
    // Numeric keywords with a trailing number set the value; bare ones open
    // their submenu.
    CW_CHECK(parseCommand("guard").kind == CommandKind::MenuGuard);
    CW_CHECK(parseCommand("防震").kind == CommandKind::MenuGuard);
    CW_CHECK(parseCommand("edge").kind == CommandKind::MenuEdge);
    CW_CHECK(parseCommand("边缘").kind == CommandKind::MenuEdge);
    CW_CHECK(parseCommand("tol").kind == CommandKind::MenuTolerance);
    CW_CHECK(parseCommand("容差").kind == CommandKind::MenuTolerance);
    CW_CHECK(parseCommand("taskbar").kind == CommandKind::MenuTaskbar);
    CW_CHECK(parseCommand("任务栏").kind == CommandKind::MenuTaskbar);

    const auto guard = parseCommand("guard 120");
    CW_CHECK(guard.kind == CommandKind::SetGuard && guard.value == 120);
    const auto guardLo = parseCommand("防震 1");
    CW_CHECK(guardLo.value == 10);
    const auto guardHi = parseCommand("guard 999");
    CW_CHECK(guardHi.value == 200);

    const auto edge = parseCommand("edge 5");
    CW_CHECK(edge.kind == CommandKind::SetEdge && edge.value == 5);
    const auto edgeLo = parseCommand("边缘 0");
    CW_CHECK(edgeLo.value == 1);
    const auto edgeHi = parseCommand("edge 50");
    CW_CHECK(edgeHi.value == 20);

    const auto tol = parseCommand("tol 80");
    CW_CHECK(tol.kind == CommandKind::SetTolerance && tol.value == 80);
    const auto tolLo = parseCommand("容差 5");
    CW_CHECK(tolLo.value == 10);
    const auto tolHi = parseCommand("tol 500");
    CW_CHECK(tolHi.value == 200);

    const auto taskbarDefault = parseCommand("taskbar 0");
    CW_CHECK(taskbarDefault.kind == CommandKind::SetTaskbar && taskbarDefault.value == 0);
    const auto taskbarOff = parseCommand("taskbar -1");
    CW_CHECK(taskbarOff.kind == CommandKind::SetTaskbar && taskbarOff.value == -1);
    const auto taskbarLo = parseCommand("任务栏 -2");
    CW_CHECK(taskbarLo.kind == CommandKind::SetTaskbar && taskbarLo.value == -1);
    const auto taskbarHi = parseCommand("taskbar 900");
    CW_CHECK(taskbarHi.kind == CommandKind::SetTaskbar && taskbarHi.value == 500);
}

CW_TEST(parser_tuner_commands)
{
    auto n = parseCommand("tune");
    CW_CHECK(n.kind == CommandKind::MenuTuner);
    CW_CHECK(parseCommand("调参").kind == CommandKind::MenuTuner);
    CW_CHECK(parseCommand("tuner").kind == CommandKind::MenuTuner);
    CW_CHECK(parseCommand("可视化").kind == CommandKind::MenuTuner);
    n = parseCommand("tune on");
    CW_CHECK(n.kind == CommandKind::SetOverlay && n.value == 1);
    n = parseCommand("tune off");
    CW_CHECK(n.kind == CommandKind::SetOverlay && n.value == 0);
    n = parseCommand("调参 开");
    CW_CHECK(n.kind == CommandKind::SetOverlay && n.value == 1);
}

CW_TEST(parser_data_window)
{
    CW_CHECK(parseCommand("data").kind == CommandKind::MenuData);
    CW_CHECK(parseCommand("数据").kind == CommandKind::MenuData);
    CW_CHECK(parseCommand("window").kind == CommandKind::MenuData);
    CW_CHECK(parseCommand("窗口").kind == CommandKind::MenuData);
    const auto on = parseCommand("data on");
    CW_CHECK(on.kind == CommandKind::SetData && on.value == 1);
    const auto off = parseCommand("data off");
    CW_CHECK(off.kind == CommandKind::SetData && off.value == 0);
    const auto zhOn = parseCommand("数据 开");
    CW_CHECK(zhOn.kind == CommandKind::SetData && zhOn.value == 1);
    const auto zhOff = parseCommand("数据 关");
    CW_CHECK(zhOff.kind == CommandKind::SetData && zhOff.value == 0);
    // "database" glues onto "data": not a data command.
    CW_CHECK(parseCommand("database").kind == CommandKind::Unknown);
}

CW_TEST(parser_impact_flash)
{
    auto n = parseCommand("flash");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == -1);
    n = parseCommand("glow");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == -1);
    n = parseCommand("光效");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == -1);
    n = parseCommand("flash on");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == 1);
    n = parseCommand("glow off");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == 0);
    n = parseCommand("光效 开");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == 1);
    n = parseCommand("光效 关");
    CW_CHECK(n.kind == CommandKind::SetImpactFlash && n.value == 0);
}

CW_TEST(parser_reset)
{
    CW_CHECK(parseCommand("reset").kind == CommandKind::ResetToDefaults);
    CW_CHECK(parseCommand("RESET").kind == CommandKind::ResetToDefaults);
    CW_CHECK(parseCommand("恢复默认").kind == CommandKind::ResetToDefaults);
    CW_CHECK(parseCommand("恢复").kind == CommandKind::ResetToDefaults);
}

CW_TEST(parser_unknown_and_boundary_gluing)
{
    const auto unknown = parseCommand("hello world");
    CW_CHECK(unknown.kind == CommandKind::Unknown);
    CW_CHECK(unknown.word == "hello world");
    // "modes" glues onto "mode": not a mode command.
    CW_CHECK(parseCommand("modes").kind == CommandKind::Unknown);
    // "impacts" glues onto "impact".
    CW_CHECK(parseCommand("impacts").kind == CommandKind::Unknown);
    // But "impact:" with a colon is fine (a trailing separator) — the menu.
    CW_CHECK(parseCommand("impact:").kind == CommandKind::MenuImpact);
    // Leading spaces are trimmed.
    CW_CHECK(parseCommand("  impact  ").kind == CommandKind::MenuImpact);
    // A zh word followed by more zh characters is not the keyword.
    CW_CHECK(parseCommand("撞击模式").kind == CommandKind::Unknown);
}

CW_TEST(parser_first_match_wins)
{
    // "impact on" must win over bare "impact" (longest match first).
    const auto on = parseCommand("impact on");
    CW_CHECK(on.kind == CommandKind::SetImpact && on.value == 1);
    // "mode vertical" must win over bare "mode".
    const auto vert = parseCommand("mode vertical");
    CW_CHECK(vert.kind == CommandKind::SetMode);
}
