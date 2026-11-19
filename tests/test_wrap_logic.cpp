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
    CW_CHECK_EQ(s.taskbarGuardPx, 0);
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

// ------------------------------------------------- the taskbar guard band ---
//
// The user: "taskbar附近区域（左右）是默认不允许穿越的（可配置）". A wrap
// whose arrival point sits over the taskbar (or any other appbar) carries the
// hand's push out of an app the user is interacting with; by default that is
// refused, and `taskbarGuardPx` widens (or, at -1, disables) the band. Only
// horizontal wraps: the appbar band runs along the horizontal edges, where a
// vertical wrap's arrival cannot be.

MonitorBox taskbar_monitor_box()
{
    MonitorBox m;
    m.left = 0;
    m.top = 0;
    m.right = 1920;
    m.bottom = 1080;
    m.primary = true;
    m.hasWorkArea = true; // a 48 px taskbar along the bottom
    m.workLeft = 0;
    m.workTop = 0;
    m.workRight = 1920;
    m.workBottom = 1032;
    return m;
}

CW_TEST(taskbar_band_blocks_horizontal_wraps_by_default)
{
    WrapEngine e;
    e.rebuild({ taskbar_monitor_box() });
    // Above the taskbar: wraps normally.
    CW_CHECK_EQ(e.onMouseMove({ 1919, 540 }, false, false).first, 1);
    // Back inside first (releasing the push latch), then over the taskbar
    // (y >= 1032): refused, and named.
    CW_CHECK_EQ(e.onMouseMove({ 960, 540 }, false, false).first, 960);
    const auto [x, y] = e.onMouseMove({ 1919, 1060 }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(y, 1060);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Taskbar)], 1);
}

CW_TEST(taskband_widens_the_band_only_where_an_appbar_is)
{
    WrapEngine e;
    e.rebuild({ taskbar_monitor_box() });
    Settings s = e.settings();
    s.taskbarGuardPx = 40; // 40 px into the work area above the taskbar
    e.setSettings(s);
    // 40 px above the work-area bottom is now inside the widened band...
    const auto [x, y] = e.onMouseMove({ 1919, 1000 }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(y, 1000);
    // ...but the top-right corner is not: there is no appbar there to widen
    // from, and a corner match on the top edge wraps downward as before.
    // (The previous sample sits at the widened band's edge, so the direction
    // into this corner is up; the corner picks the top edge, whose landing is
    // the outer Bottom edge, i.e. the taskbar row clamped out of it.)
    const auto [cx, cy] = e.onMouseMove({ 1919, 0 }, false, false);
    CW_CHECK_EQ(cx, 1919);
    CW_CHECK_EQ(cy, 1031);
}

CW_TEST(taskbar_guard_can_be_turned_off)
{
    WrapEngine e;
    e.rebuild({ taskbar_monitor_box() });
    Settings s = e.settings();
    s.taskbarGuardPx = -1; // wrap over the appbar too (the pre-guard behaviour)
    e.setSettings(s);
    const auto [x, y] = e.onMouseMove({ 1919, 1060 }, false, false);
    CW_CHECK(x != 1919); // it wrapped
    // And the landing is pulled out of the taskbar, as it always was.
    CW_CHECK_EQ(y, 1031);
}

CW_TEST(taskbar_guard_needs_a_work_area_to_believe)
{
    WrapEngine e;
    // No work area declared: there is no appbar information at all.
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 1060 }, false, false);
    CW_CHECK(x != 1919); // wraps as before
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Taskbar)], 0);
    // A work area larger than the monitor (a broken report) says nothing too.
    MonitorBox broken = box(0, 0, 1920, 1080, true);
    broken.hasWorkArea = true;
    broken.workLeft = -100;
    broken.workTop = -100;
    broken.workRight = 3000;
    broken.workBottom = 2000;
    e.rebuild({ broken });
    const auto [x2, y2] = e.onMouseMove({ 1, 540, }, false, false);
    (void)x2;
    (void)y2;
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Taskbar)], 0);
}

// ------------------------------------------------------- drag suppression ---
//
// The user: "capture插件在拖动框选区域的时候也不能穿越". Holding the left
// button is `disableDuringDrag` (on by default); with it off, a capture-style
// marquee drag wraps like any other move, and the engine never treated the
// drag itself specially.

CW_TEST(drag_suppression_follows_the_setting)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) });
    // Default: the left button held suppresses the wrap, and the reason says so.
    CW_CHECK_EQ(e.onMouseMove({ 3839, 540 }, true, true).first, 3839);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Dragging)], 1);
    // With the setting off (the capture-plugin case): the drag wraps, to the
    // other desktop's far edge.
    Settings s = e.settings();
    s.disableDuringDrag = false;
    e.setSettings(s);
    CW_CHECK_EQ(e.onMouseMove({ 3839, 540 }, true, true).first, 1);
    // And a release-time move inside the layout stays put (an inner edge never
    // wraps): the drag setting changes nothing about the topology itself.
    CW_CHECK_EQ(e.onMouseMove({ 1920, 540 }, true, false).first, 1920);
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
