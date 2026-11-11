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

Settings gateOn(int speedPps = 2000, int bandPx = 150)
{
    Settings s;
    s.impactGate = true;
    s.impactSpeedPps = speedPps;
    s.impactBandPx = bandPx;
    return s;
}
} // namespace

CW_TEST(impact_off_by_default_fast_still_wraps)
{
    // Gate off (default): the fast burst wraps exactly as before.
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540, 1000 }, false, false);
    CW_CHECK_EQ(x, 1);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_slow_drift_does_not_wrap)
{
    // ~250 px/s toward the right edge, entirely inside the band: under the
    // 2000 px/s gate and over the minimal-travel bar.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    for (int x = 1807; x <= 1919; ++x) {
        e.onMouseMove({ x, 540, t }, false, false);
        t += 4000; // 1 px per 4 ms = 250 px/s, continuously to the edge
    }
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919); // suppressed
    CW_CHECK_EQ(e.stats().wraps, 0);
    CW_CHECK(e.stats().suppressed >= 1);
}

CW_TEST(impact_fast_hit_wraps)
{
    // Fast inside the band (~6000 px/s): over the gate. An earlier fast
    // flight OUTSIDE the band must not inflate the measurement.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1700, 540, t }, false, false); // outside the 150px band
    t += 16000; // 200 px in 16 ms = 12500 px/s
    e.onMouseMove({ 1800, 540, t }, false, false); // band start (dist 119)
    t += 2000; // 100 px in 2 ms = 50000 px/s
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    // In-band stroke: 1800 -> 1919 = 119 px in 2 ms. The gate only sees the
    // in-band part; either way it is far over the 2000 px/s gate.
    CW_CHECK_EQ(x, 1); // wrapped
    CW_CHECK_EQ(e.stats().wraps, 1);
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
    t += 16000; // 100 px in 16 ms, ends at the band edge
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 2000; // 119 px in 2 ms inside the band: a fast hit
    const auto [x1, y1] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x1, 1); // fast hit wraps
    // Immediately push to the left edge with slow samples: no wrap, and the
    // previous burst must not count (history was cleared at the wrap).
    t += 100'000;
    const auto [x2, y2] = e.onMouseMove({ 0, 540, t }, false, false);
    CW_CHECK_EQ(x2, 0); // slow after the wrap: the gate blocks, no wrap
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
    CW_CHECK_EQ(s.impactBandPx, 150);
    RawSettings raw;
    raw.impactGate = true;
    raw.hasImpactGate = true;
    Settings merged;
    applySettings(raw, merged, true);
    CW_CHECK_EQ(merged.impactGate, true); // partial update applies
    CW_CHECK_EQ(merged.impactSpeedPps, 2000); // untouched field keeps default
    CW_CHECK_EQ(merged.impactBandPx, 150);
}

// --- approach band + stroke splitting + minimal travel ---

CW_TEST(impact_band_ignores_distant_sprint)
{
    // A fast flight far out, then a slow crawl inside the band: the old
    // whole-window average could still pass; the band must measure only
    // the crawl and fail it.
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1750, 540, t }, false, false); // 169 px out: outside the band
    t += 2000;
    e.onMouseMove({ 1900, 540, t }, false, false); // 150 px in 2 ms — "fast"
    // Crawl inside the band, then step onto the edge: the stroke restarts
    // at 1900 and totals 19 px — under the minimal-travel bar.
    for (int i = 0; i < 15; ++i) {
        t += 4000;
        e.onMouseMove({ 1901 + i, 540, t }, false, false); // 1 px / 4 ms
    }
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_band_ignores_distant_sprint_when_braking)
{
    // The motivating case: fly to the band, brake hard inside it, arrive
    // slowly. Only the in-band stroke counts: 169 px over 100 ms ≈
    // 1690 px/s < the 2000 gate.
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    // Fast approach to the band edge: 150 px in 25 ms = 6000 px/s.
    e.onMouseMove({ 1600, 540, t }, false, false);
    t += 25000;
    e.onMouseMove({ 1750, 540, t }, false, false); // band boundary (dist 169)
    // Brake: 100 px in 40 ms = 2500 px/s, then coast 69 px in 60 ms.
    t += 40000;
    e.onMouseMove({ 1850, 540, t }, false, false);
    t += 60000;
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_edge_jitter_fails_minimal_travel)
{
    // A 2 px bounce at the edge: instantaneous "speed" is huge (e.g.
    // 2 px / 1 ms = 2000 px/s) but the stroke traveled almost nothing.
    // Minimal travel must fail it even with a zero gate.
    WrapEngine e(gateOn(500));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1910, 540, t }, false, false);
    t += 1000;
    e.onMouseMove({ 1912, 540, t }, false, false);
    t += 1000;
    e.onMouseMove({ 1911, 540, t }, false, false); // tiny reversal
    t += 1000;
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    // Last stroke: 1911 -> 1919 = 8 px < 20 minimal travel.
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_bounce_then_hit_measures_last_stroke)
{
    // Slow first hit (suppressed), bounce off, come back fast: the
    // measurement must restart at the reversal and measure the final hit,
    // not average across the whole sequence.
    WrapEngine e(gateOn(2000));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 80000;
    e.onMouseMove({ 1919, 540, t }, false, false); // slow first hit: suppressed (~1488 px/s)
    t += 2000;
    e.onMouseMove({ 1880, 540, t }, false, false); // bounce back out
    t += 2000;
    // Final stroke: 1880 -> 1919 = 39 px in 2 ms = 19500 px/s > gate.
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_bounce_then_slow_measures_last_stroke_fail)
{
    // Same shape, but the final stroke is slow: it must fail even though
    // the average of the whole sequence was fast.
    WrapEngine e(gateOn(2000));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 80000;
    e.onMouseMove({ 1919, 540, t }, false, false); // slow first hit: suppressed
    t += 2000;
    e.onMouseMove({ 1880, 540, t }, false, false); // bounce back out
    // Slow crawl back: 39 px over 80 ms ≈ 490 px/s.
    t += 40000;
    e.onMouseMove({ 1900, 540, t }, false, false);
    t += 40000;
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_pause_resets_stroke)
{
    // A pause inside the window: the stroke restarts after it, so speed is
    // measured over the post-pause travel only.
    WrapEngine e(gateOn(2000));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false); // inside the band
    t += 2000;
    e.onMouseMove({ 1850, 540, t }, false, false); // 50 px in 2 ms: fast
    t += 120'000; // pause longer than the 100 ms window
    e.onMouseMove({ 1900, 540, t }, false, false);
    t += 2000;
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    // Post-pause stroke: 19 px < 20 minimal travel; even ignoring that,
    // 19 px / 2 ms alone would pass — the pause must cut it off.
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_left_edge_band_symmetry)
{
    // The same band/stroke rules on the left edge (decreasing x).
    WrapEngine e(gateOn(2000));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 120, 540, t }, false, false); // inside the band
    t += 2000;
    const auto [x, y] = e.onMouseMove({ 0, 540, t }, false, false);
    // Stroke: 120 px in 2 ms = 60000 px/s: wraps.
    CW_CHECK_EQ(x, 1918);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_top_edge_band_symmetry)
{
    // And on the top edge (decreasing y).
    WrapEngine e(gateOn(2000));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 960, 100, t }, false, false);
    t += 2000;
    const auto [x, y] = e.onMouseMove({ 960, 0, t }, false, false);
    CW_CHECK_EQ(y, 1078);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_exactly_minimal_travel_passes)
{
    // 20 px is the bar (stroke < 20 fails); exactly 20 px at gate-clearing
    // speed wraps.
    WrapEngine e(gateOn(2000));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1899, 540, t }, false, false); // 20 px from the edge
    t += 4000; // 20 px in 4 ms = 5000 px/s
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(impact_band_boundary_value_settings)
{
    RawSettings raw;
    raw.impactBandPx = 50;
    raw.hasImpactBand = true;
    Settings s;
    applySettings(raw, s, false);
    CW_CHECK_EQ(s.impactBandPx, 50);
    raw.impactBandPx = 500;
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.impactBandPx, 500);
    raw.impactBandPx = 49; // out of range -> default
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.impactBandPx, 150);
    raw.impactBandPx = 501;
    applySettings(raw, s, true);
    CW_CHECK_EQ(s.impactBandPx, 150);
}
