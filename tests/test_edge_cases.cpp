// test_edge_cases.cpp — adversarial and boundary inputs: degenerate
// geometries, extreme coordinates, coordinate gaps, injected event
// semantics, settings fuzzing. Complements the scenario tests in
// test_wrap_topology.cpp.
#include "test_harness.h"
#include "wrap_logic.h"

using namespace cw;

namespace {
MonitorBox box(int64_t l, int64_t t, int64_t r, int64_t b, bool primary = false)
{
    MonitorBox m;
    m.left = l;
    m.top = t;
    m.right = r;
    m.bottom = b;
    m.primary = primary;
    return m;
}
} // namespace

CW_TEST(edge_degenerate_monitor_zero_size)
{
    Topology t;
    t.initialize({ box(0, 0, 0, 0, true) });
    // A zero-area monitor contributes no usable edges; nothing may crash.
    EdgeType edge;
    CursorDirection d { 1, 0 };
    t.onOuterEdge(0, 0, 0, edge, WrapMode::Both, &d);
    const auto [x, y] = t.wrapDestination(0, 0, 0, EdgeType::Left, WrapMode::Both);
    (void)x;
    (void)y;
}

CW_TEST(edge_monitor_with_negative_coordinates)
{
    // A monitor left of the primary has negative coordinates; its edges
    // must still be found and used.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true), box(-1920, 0, 0, 1080) });
    CW_CHECK_EQ(t.outerEdges().size(), size_t(6));
    EdgeType edge;
    CursorDirection left { -5, 0 };
    CW_CHECK(t.onOuterEdge(1, -1920, 540, edge, WrapMode::Both, &left));
    const auto [x, y] = t.wrapDestination(1, -1920, 540, EdgeType::Left, WrapMode::Both);
    CW_CHECK_EQ(x, 1919); // the primary's right edge position
    CW_CHECK_EQ(y, 540);
}

CW_TEST(edge_extreme_coordinates_no_overflow)
{
    Topology t;
    t.initialize({ box(-2147483000, -2147483000, 2147483000, 2147483000, true) });
    EdgeType edge;
    CursorDirection right { 5, 0 };
    const bool on = t.onOuterEdge(0, 2147482999, 0, edge, WrapMode::Both, &right);
    CW_CHECK(on);
    const auto [x, y] = t.wrapDestination(0, 2147482999, 0, EdgeType::Right, WrapMode::Both);
    CW_CHECK_EQ(x, -2147483000);
    (void)y;
}

CW_TEST(edge_three_in_a_row_middle_wraps_nowhere_horizontally)
{
    // Three side-by-side monitors: the middle one has both horizontal edges
    // inner, only top/bottom wrap horizontally-speaking.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080),
                  box(3840, 0, 5760, 1080) });
    EdgeType edge;
    CursorDirection right { 5, 0 };
    CW_CHECK(!t.onOuterEdge(1, 1919, 540, edge, WrapMode::Both, &right));
    CW_CHECK(!t.onOuterEdge(1, 3840, 540, edge, WrapMode::Both, &right));
    CW_CHECK_EQ(t.outerEdges().size(), size_t(8));
    // The outermost right edge wraps to the outermost left edge.
    const auto [x, y] = t.wrapDestination(2, 5759, 540, EdgeType::Right, WrapMode::Both);
    CW_CHECK_EQ(x, 0);
    CW_CHECK_EQ(y, 540);
}

CW_TEST(edge_horizontal_mode_vertical_layout_still_wraps_within)
{
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true) });
    EdgeType edge;
    CursorDirection right { 5, 0 };
    CW_CHECK(!t.onOuterEdge(0, 0, 540, edge, WrapMode::VerticalOnly, &right));
    const auto [x, y] = t.wrapDestination(0, 0, 540, EdgeType::Left, WrapMode::VerticalOnly);
    // wrapDestination honours the mode only through onOuterEdge; direct
    // destination queries return the geometric opposite.
    CW_CHECK_EQ(x, 1919);
    (void)y;
}

CW_TEST(edge_zero_direction_at_corner)
{
    // First-ever sample lands exactly on a corner with no movement history:
    // zero direction, still on the edge, destination exists.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true) });
    EdgeType edge;
    CW_CHECK(t.onOuterEdge(0, 0, 0, edge, WrapMode::Both, nullptr));
    const auto [x, y] = t.wrapDestination(0, 0, 0, edge, WrapMode::Both);
    CW_CHECK(x != 0 || y != 0);
}

CW_TEST(engine_wraps_only_mousemove_semantics)
{
    // The engine consumes one sample at a time. Repeated samples at the same
    // edge keep wrapping because the guard measures distance from the last
    // wrap destination (0,540) and the incoming sample (1919,540) is far from
    // it: real Windows never delivers that sequence, but the engine must not
    // loop forever on its own.
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    for (int i = 0; i < 100; ++i)
        e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK(e.stats().wraps >= 1);
    CW_CHECK(e.stats().wraps <= 100);
    // A sample delivered at the guard centre is absorbed.
    const long long before = e.stats().wraps;
    for (int i = 0; i < 20; ++i)
        e.onMouseMove({ 0, 540 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, before);
}

CW_TEST(engine_alternating_edges_guard_prevents_ping_pong)
{
    // A wrap from the right edge lands on the left edge; without the guard
    // the next sample would wrap straight back. With it, only the first
    // wrap happens until the pointer leaves the guard radius.
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x1, y1] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x1, 0);
    const auto [x2, y2] = e.onMouseMove({ 0, 540 }, false, false); // same spot, in guard
    CW_CHECK_EQ(x2, 0);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(engine_shift_activation)
{
    Settings s;
    s.activation = ActivationMode::WhileShift;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x1, y1] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x1, 1919); // Shift not held
    const auto [x2, y2] = e.onMouseMove({ 1919, 540 }, true, false);
    CW_CHECK_EQ(x2, 0); // Shift held
}

CW_TEST(engine_toggle_enabled_resets_guard)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, 1);
    e.resetState(); // what the plugin's toggle action calls
    // Guard cleared: an immediate wrap can happen again.
    const auto [x, y] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x, 0);
    CW_CHECK_EQ(e.stats().wraps, 2);
}

CW_TEST(settings_type_confusion_ignored)
{
    // A hand-edited settings file with wrong types must not throw or
    // corrupt: strings for numbers and bools for enums are ignored.
    RawSettings raw;
    raw.wrapMode = "";
    raw.hasWrapMode = true; // empty string: not a mode name
    raw.activationMode = "";
    raw.hasActivation = true;
    Settings s;
    s.wrapMode = WrapMode::HorizontalOnly;
    applySettings(raw, s, true);
    CW_CHECK(s.wrapMode == WrapMode::HorizontalOnly); // unchanged
    CW_CHECK(s.activation == ActivationMode::Always); // default from construction
}

CW_TEST(settings_nan_rejected)
{
    RawSettings raw;
    const double nan = 0.0 / 0.0;
    raw.edgeThresholdPx = nan;
    raw.hasEdgeThreshold = true;
    raw.wrapGuardPx = 1.0 / 0.0; // +inf
    raw.hasWrapGuard = true;
    Settings s;
    applySettings(raw, s, false);
    CW_CHECK_EQ(s.edgeThresholdPx, 1);  // NaN -> default
    CW_CHECK_EQ(s.wrapGuardPx, 50);     // inf -> default
}

CW_TEST(topology_sort_determinism)
{
    // The same layout given in different orders produces the same outer
    // edge set (indices may differ, geometry must not).
    const std::vector<MonitorBox> a { box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) };
    const std::vector<MonitorBox> b { box(1920, 0, 3840, 1080), box(0, 0, 1920, 1080, true) };
    Topology ta;
    ta.initialize(a);
    Topology tb;
    tb.initialize(b);
    CW_CHECK_EQ(ta.outerEdges().size(), tb.outerEdges().size());
    // Same geometry per index after sorting (primary first).
    for (size_t i = 0; i < 2; ++i) {
        CW_CHECK_EQ(ta.monitors()[i].left, tb.monitors()[i].left);
        CW_CHECK_EQ(ta.monitors()[i].top, tb.monitors()[i].top);
    }
}
