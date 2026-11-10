// test_impact_gate.cpp — the impact/acceleration mode: reaching the edge
// must look like a hit (enough perpendicular speed over the recent window),
// not a drift. Timestamps are microseconds, exactly what the hook passes.
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

Settings gateOn(int speedPps = 2000)
{
    Settings s;
    s.impactGate = true;
    s.impactSpeedPps = speedPps;
    return s;
}
} // namespace

CW_TEST(impact_off_by_default_fast_still_wraps)
{
    // Gate off (default): the fast burst wraps exactly as before.
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540, 1000 }, false, false);
    CW_CHECK_EQ(x, 0);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_slow_drift_does_not_wrap)
{
    // ~250 px/s toward the right edge: under a 2000 px/s gate.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    for (int i = 0; i < 6; ++i) {
        e.onMouseMove({ 1900 + i, 540, t }, false, false);
        t += 4000; // 1 px per 4 ms = 250 px/s
    }
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919); // suppressed
    CW_CHECK_EQ(e.stats().wraps, 0);
    CW_CHECK(e.stats().suppressed >= 1);
}

CW_TEST(impact_fast_hit_wraps)
{
    // ~6000 px/s toward the right edge: over the gate.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1700, 540, t }, false, false);
    // Jump near the edge quickly, then the final sample crosses onto the edge.
    t += 16000; // 200 px in 16 ms = 12500 px/s
    const auto [x, y] = e.onMouseMove({ 1900, 540, t }, false, false);
    t += 3000; // 19 px in 3 ms ≈ 6300 px/s
    const auto [x2, y2] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x2, 0); // wrapped
    CW_CHECK_EQ(e.stats().wraps, 1);
    (void)x;
    (void)y;
}

CW_TEST(impact_measures_only_the_perpendicular_axis)
{
    // Slide along the top edge fast (large x speed) but approach it slowly
    // in y: the gate looks at the y speed and must not wrap. The final
    // sample stays on the top edge (y=0), far from the right edge.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 500, 2, t }, false, false);
    t += 5000;
    e.onMouseMove({ 1500, 1, t }, false, false); // 200000 px/s in x, 200 px/s in y
    t += 5000;
    const auto [x, y] = e.onMouseMove({ 1600, 0, t }, false, false);
    CW_CHECK_EQ(x, 1600); // suppressed: y approach was slow
    CW_CHECK_EQ(y, 0);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_sliding_along_edge_slow_axis_stays)
{
    // Same as above but approaching the right edge: only dx counts.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1910, 100, t }, false, false);
    t += 5000;
    e.onMouseMove({ 1917, 900, t }, false, false); // dx fast, dy fast but perpendicular
    t += 5000;
    const auto [x, y] = e.onMouseMove({ 1919, 1000, t }, false, false);
    // dx over the window: 9 px in 10 ms = 900 px/s < 2000 gate.
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_untimestamped_fails_closed)
{
    // tUs == 0 samples cannot prove speed: the gate must not wrap, even for
    // a huge jump (which is exactly what a programmatic teleport looks like).
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540, 0 }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_mixed_timestamps_fail_closed)
{
    // A window whose oldest sample has no timestamp: cannot measure.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1000, 540, 0 }, false, false); // untimed history
    const auto [x, y] = e.onMouseMove({ 1919, 540, 2'000'000 }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_history_resets_after_wrap)
{
    // After a wrap the movement history clears, so the speed of the run
    // that caused the wrap cannot leak into the next decision.
    Settings s = gateOn();
    s.disableDuringDrag = false;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1700, 540, t }, false, false);
    t += 16000;
    e.onMouseMove({ 1900, 540, t }, false, false);
    t += 3000;
    const auto [x1, y1] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x1, 0); // fast hit wraps
    // Immediately push to the left edge with slow samples: no wrap, and the
    // previous burst must not count (history was cleared at the wrap).
    t += 100'000;
    const auto [x2, y2] = e.onMouseMove({ 0, 540, t }, false, false);
    CW_CHECK_EQ(x2, 0); // at the left edge...
    // ...but a second slow sample at the edge is guarded anyway; the point
    // of this test is that nothing wrapped from stale history.
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_gate_away_from_edge_not_counted)
{
    // Fast movement in the middle of the screen never reaches the gate:
    // it is only consulted once the point is on an outer edge.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 300, 540, t }, false, false);
    t += 2000;
    e.onMouseMove({ 900, 540, t }, false, false); // 300000 px/s, but mid-screen
    CW_CHECK_EQ(e.stats().wraps, 0);
    const auto [x, y] = e.onMouseMove({ 1500, 540, t + 2000 }, false, false);
    CW_CHECK_EQ(x, 1500);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_speed_boundary_value_accepted)
{
    RawSettings raw;
    raw.impactGate = true;
    raw.hasImpactGate = true;
    raw.impactSpeedPps = 500; // the low boundary
    raw.hasImpactSpeed = true;
    Settings s;
    applySettings(raw, s, false);
    CW_CHECK_EQ(s.impactSpeedPps, 500);
    CW_CHECK_EQ(s.impactGate, true);

    raw.impactSpeedPps = 499; // out of range -> default
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.impactSpeedPps, 2000);

    raw.impactSpeedPps = 20000; // the high boundary
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.impactSpeedPps, 20000);
    raw.impactSpeedPps = 20001;
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.impactSpeedPps, 2000);
}

CW_TEST(impact_settings_defaults)
{
    const Settings s;
    CW_CHECK_EQ(s.impactGate, false);
    CW_CHECK_EQ(s.impactSpeedPps, 2000);
    RawSettings raw;
    raw.impactGate = true;
    raw.hasImpactGate = true;
    Settings merged;
    applySettings(raw, merged, true);
    CW_CHECK_EQ(merged.impactGate, true); // partial update applies
    CW_CHECK_EQ(merged.impactSpeedPps, 2000); // untouched field keeps default
}
