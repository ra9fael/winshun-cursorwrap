// test_wrap_logic.cpp — settings semantics, guard arithmetic and protocol
// payload shaping that wrap_topology.cpp's engine tests do not cover.
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

CW_TEST(settings_defaults)
{
    const Settings s;
    CW_CHECK(s.wrapMode == WrapMode::Both);
    CW_CHECK(s.activation == ActivationMode::Always);
    CW_CHECK_EQ(s.disableDuringDrag, true);
    CW_CHECK_EQ(s.disableOnSingleMonitor, false);
    CW_CHECK_EQ(s.edgeThresholdPx, 1);
    CW_CHECK_EQ(s.wrapGuardPx, 50);
    CW_CHECK_EQ(s.adjacencyTolerancePx, 50);
}

CW_TEST(settings_raw_empty_keeps_previous)
{
    Settings s;
    s.wrapMode = WrapMode::HorizontalOnly;
    applySettings(RawSettings {}, s, true);
    CW_CHECK(s.wrapMode == WrapMode::HorizontalOnly); // nothing touched
    CW_CHECK_EQ(s.edgeThresholdPx, 1);
}

CW_TEST(settings_boundary_values_accepted)
{
    RawSettings raw;
    raw.edgeThresholdPx = 20;
    raw.hasEdgeThreshold = true;
    raw.wrapGuardPx = 10;
    raw.hasWrapGuard = true;
    raw.adjacencyTolerancePx = 200;
    raw.hasAdjacency = true;
    Settings s;
    applySettings(raw, s, false);
    CW_CHECK_EQ(s.edgeThresholdPx, 20);
    CW_CHECK_EQ(s.wrapGuardPx, 10);
    CW_CHECK_EQ(s.adjacencyTolerancePx, 200);

    raw.edgeThresholdPx = 21;
    raw.wrapGuardPx = 9;
    raw.adjacencyTolerancePx = 201;
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.edgeThresholdPx, 1);  // out of range -> manifest default
    CW_CHECK_EQ(s.wrapGuardPx, 50);
    CW_CHECK_EQ(s.adjacencyTolerancePx, 50);

    raw.edgeThresholdPx = 0;            // zero is out of range, not a clamp
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.edgeThresholdPx, 1);
}

CW_TEST(guard_distance_squared_not_magnitude)
{
    // The guard is a radius: a diagonal point at distance <= wrapGuardPx is
    // inside. (1920,1080) single-monitor layout, wrap from (1919,540) to (0,540).
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540 }, false, false); // wrap to (0,540)
    CW_CHECK_EQ(e.stats().wraps, 1);
    // (30,560): dx=30, dy=20 -> ~36px, inside the 50px guard; on no edge
    // anyway. Verify the sample moves normally without a wrap.
    const auto [x, y] = e.onMouseMove({ 30, 560 }, false, false);
    CW_CHECK_EQ(x, 30);
    CW_CHECK_EQ(y, 560);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(guard_cleared_far_away)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, 1);
    e.onMouseMove({ 960, 540 }, false, false); // far from the wrap point
    const auto [wx, wy] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(wx, 1);
    CW_CHECK_EQ(e.stats().wraps, 2);
}

CW_TEST(engine_reset_after_rebuild)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, 1);
    // Display change: topology rebuilt, movement history cleared; the wrap
    // counter survives (it is a session statistic, not movement state).
    e.rebuild({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) });
    CW_CHECK_EQ(e.stats().rebuilt, 2);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(engine_no_monitors_is_noop)
{
    WrapEngine e;
    e.onMouseMove({ 100, 100 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, 0);
    const auto [x, y] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x, 1919);
}

CW_TEST(engine_direction_feeds_edge_priority)
{
    // Dual horizontal: corner of the right monitor at (3839, 0) matches
    // Right and Top; moving up should pick Top.
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) });
    // Approach the corner moving up along the right edge.
    e.onMouseMove({ 3839, 40 }, false, false);
    const auto [x, y] = e.onMouseMove({ 3839, 0 }, false, false);
    // Top edge of monitor 1 is outer and wraps to the outer Bottom edge with
    // the largest position (1079).
    CW_CHECK_EQ(x, 3839);
    CW_CHECK_EQ(y, 1078);
}

CW_TEST(diagnostics_counts_and_gaps)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true), box(1950, 0, 3870, 1080) });
    e.onMouseMove({ 100, 100 }, false, false);
    const Diagnostics d = e.diagnostics(false);
    CW_CHECK_EQ(d.monitors, 2);
    CW_CHECK_EQ(d.outerEdges, 6); // 30px gap is within the default tolerance
    CW_CHECK(!d.hookOk);
    CW_CHECK_EQ(d.gaps.size(), size_t(1));
    CW_CHECK_EQ(d.stats.moves, 1);
}
