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
    // Start away from every edge (a corner turn needs the pointer to arrive at
    // the corner, not to be sitting on the right edge already), then move up
    // into the top-right corner.
    e.onMouseMove({ 3800, 60 }, false, false);
    e.onMouseMove({ 3820, 30 }, false, false);
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

// ------------------------------------------------- SetCursorPos refused a wrap
//
// The user: "it is on but wrapping works only now and then, and sometimes works
// after the mouse sits still for a while". When SetCursorPos fails the plugin
// swallowed the move and kept the wrap it had recorded, so the guard sat at a
// landing point the pointer never reached and suppressed every further attempt
// from the same edge until the pointer wandered far enough away. These tests pin
// the rollback.

CW_TEST(engine_failed_wrap_does_not_arm_the_guard)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x1, y1] = e.onMouseMove({ 1919, 540, 1000 }, false, false);
    CW_CHECK_EQ(x1, 1); // the decision was a wrap
    CW_CHECK_EQ(e.stats().wraps, 1);

    // The OS refused it. The pointer is still at 1919 and the hand keeps pushing.
    e.onWrapFailed();
    CW_CHECK_EQ(e.stats().wraps, 0); // the wrap never happened

    // The very next push from the same place must be free to wrap again. Before
    // onWrapFailed this returned 1919 (suppressed by the guard) for as long as the
    // hand stayed at the edge.
    const auto [x2, y2] = e.onMouseMove({ 1919, 540, 2000 }, false, false);
    CW_CHECK_EQ(x2, 1);
    CW_CHECK_EQ(y2, 540);
}

CW_TEST(engine_failed_wrap_is_not_recorded_as_an_arrival)
{
    // A refused wrap must not teach the tuner that this speed wraps, or
    // suggestGate would recommend a threshold calibrated on nothing.
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540, 1000 }, false, false);
    const size_t before = e.arrivals().size();
    e.onWrapFailed();
    CW_CHECK(e.arrivals().size() < before);
}

CW_TEST(engine_wrap_failure_then_success_wraps_exactly_once)
{
    // Failure, then success, then the guard must behave normally (no double wrap).
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540, 1000 }, false, false);
    e.onWrapFailed();

    const auto [x, y] = e.onMouseMove({ 1919, 540, 2000 }, false, false);
    CW_CHECK_EQ(x, 1);
    CW_CHECK_EQ(e.stats().wraps, 1);
    // Now the landing is real, so the guard applies as usual.
    const auto [x2, y2] = e.onMouseMove({ 1919, 540, 3000 }, false, false);
    CW_CHECK_EQ(x2, 1919);
    CW_CHECK_EQ(y2, 540);
}
