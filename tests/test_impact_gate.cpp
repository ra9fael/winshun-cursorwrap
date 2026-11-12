// test_impact_gate.cpp — the impact/acceleration mode: reaching the edge
// must look like a hit (enough perpendicular speed over the recent window),
// not a drift. Timestamps are microseconds, exactly what the hook passes.
#include "test_harness.h"
#include "qpc_time.h"
#include "wrap_logic.h"

#include <algorithm>
#include <utility>
#include <vector>

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

// --- regression: the clamped tail at an edge -------------------------------
//
// Windows clamps the pointer to the desktop, so once the hand reaches an edge
// every further event repeats the same coordinate. Those repeats are the
// wall, not deceleration, and averaging them in made a genuine 2000 px/s hit
// measure 1986, 1787, 1625 ... 0 while the hand kept pushing — the bug that
// made wrapping "completely stop working" on the real machine.

CW_TEST(impact_clamped_tail_keeps_the_measured_speed)
{
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 2560, 1440, true) });
    long long t = 1'000'000;
    // 16 px every 8 ms = 2000 px/s, exactly at the gate, up to the edge.
    for (int i = 0; i < 40; ++i) {
        const int x = 2400 + i * 16 > 2559 ? 2559 : 2400 + i * 16;
        const auto [nx, ny] = e.onMouseMove({ x, 720, t }, false, false);
        if (x == 2559) {
            CW_CHECK_EQ(nx, 1); // wrapped on arrival to the same monitor's left edge
            CW_CHECK_EQ(e.stats().wraps, 1);
            return;
        }
        CW_CHECK_EQ(nx, x); // no wrap yet
        t += 8000;
    }
    CW_CHECK(false && "the cursor never reached the edge");
}

CW_TEST(impact_clamped_tail_does_not_dilute_a_marginal_hit)
{
    // 2100 px/s: just over a 2000 px/s gate. Twelve repeats at the edge must
    // not drag it under the gate.
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 2560, 1440, true) });
    long long t = 1'000'000;
    int64_t x = 2400;
    int wraps = 0;
    for (int i = 0; i < 60 && !wraps; ++i) {
        const auto [nx, ny] = e.onMouseMove({ x, 720, t }, false, false);
        if (nx != x)
            ++wraps;
        t += 8000;
        x = x + 17 > 2559 ? 2559 : x + 17;
    }
    CW_CHECK_EQ(wraps, 1);
}

CW_TEST(impact_still_fails_closed_when_only_repeats_exist)
{
    // Standing still at the edge produces nothing but repeats: no measurable
    // stroke means no wrap, which is the point of the gate.
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 2560, 1440, true) });
    long long t = 1'000'000;
    for (int i = 0; i < 40; ++i) {
        const auto [nx, ny] = e.onMouseMove({ 2559, 720, t }, false, false);
        CW_CHECK_EQ(nx, 2559);
        t += 8000;
    }
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(impact_clamped_tail_error_readout_is_the_stroke_speed)
{
    // The verdict the overlay shows must be the stroke speed, not a value
    // diluted by the clamped samples: 24 px in 8 ms is 3000 px/s.
    WrapEngine e(gateOn(2000, 200));
    e.rebuild({ box(0, 0, 2560, 1440, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 2530, 720, t }, false, false);
    t += 8000;
    e.onMouseMove({ 2554, 720, t }, false, false);
    t += 8000;
    e.onMouseMove({ 2559, 720, t }, false, false); // the hit
    const EdgeVerdict v = e.lastVerdict();
    CW_CHECK(v.gateOff == false);
    CW_CHECK(v.wrapped || v.measuredPps >= 3000);
    CW_CHECK(v.measuredPps >= 3000);
}

//
// Per-reason accounting: "it does not wrap at all" is only answerable when
// the diagnostic names the gate that keeps refusing. A single `suppressed`
// total cannot tell a dragging suppression from an impact-gate rejection.
//

CW_TEST(skips_are_counted_per_reason)
{
    Settings s;
    s.disableDuringDrag = true;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });

    // Dragging: suppressed as Dragging, not merely "suppressed".
    e.onMouseMove({ 900, 540, 1000 }, false, true);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Dragging)], 1);
    // Off the edge: NotOnEdge.
    e.onMouseMove({ 900, 540, 2000 }, false, false);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::NotOnEdge)], 1);
    // The per-reason counts must add up to the legacy total.
    const Stats st = e.stats();
    long long sum = 0;
    for (int i = 1; i < kSkipReasonCount; ++i)
        sum += st.skipped[i];
    CW_CHECK_EQ(sum, st.suppressed);
}

CW_TEST(impact_gate_rejection_is_its_own_reason)
{
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    for (int x = 1807; x <= 1919; ++x) {
        e.onMouseMove({ x, 540, t }, false, false);
        t += 4000; // 250 px/s, well under the gate
    }
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
    CW_CHECK(e.stats().skipped[static_cast<int>(SkipReason::ImpactGate)] >= 1);
    // The reason name is what the data window and the diagnostics print.
    CW_CHECK(std::string(skipReasonName(SkipReason::ImpactGate)) == "impact-gate");
}

CW_TEST(decisions_record_arrivals_and_new_skip_runs)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    // A run of off-edge moves is one decision, not one per sample: a 1000 Hz
    // mouse would otherwise flood the log.
    for (int i = 0; i < 20; ++i)
        e.onMouseMove({ 100 + i, 540, 1000 + i * 1000 }, false, false);
    CW_CHECK_EQ(e.decisions().size(), 1u);
    CW_CHECK(e.decisions().back().reason == SkipReason::NotOnEdge);
    CW_CHECK(!e.decisions().back().wrapped);
    // The wrap is recorded as a decision with reason None.
    const auto [x, y] = e.onMouseMove({ 1919, 540, 9'000'000 }, false, false);
    CW_CHECK_EQ(x, 1);
    CW_CHECK_EQ(e.decisions().size(), 2u);
    CW_CHECK(e.decisions().back().wrapped);
    CW_CHECK(e.decisions().back().reason == SkipReason::None);
    CW_CHECK(e.decisions().back().onEdge);
}

CW_TEST(decisions_are_bounded)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1000;
    // Alternate between two edges' territory so every move is a new run.
    for (int i = 0; i < 200; ++i) {
        const int64_t x = (i % 2 == 0) ? 500 : 1919;
        const int64_t y = (i % 2 == 0) ? 500 : 540;
        e.onMouseMove({ x, y, t }, false, false);
        t += 1000;
    }
    CW_CHECK(e.decisions().size() <= WrapEngine::kDecisionMax);
}

CW_TEST(last_sample_reports_even_skipped_moves)
{
    Settings s;
    s.disableDuringDrag = true;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    Sample seen;
    CW_CHECK(!e.lastSample(seen)); // nothing yet
    e.onMouseMove({ 640, 480, 1000 }, false, true); // suppressed by the drag
    CW_CHECK(e.lastSample(seen));
    CW_CHECK_EQ(seen.x, 640);
    CW_CHECK_EQ(seen.y, 480);
}

CW_TEST(window_settings_round_trip)
{
    // The two runtime windows are settings so the plugin's settings page can
    // turn them on: the same key must survive a RawSettings round trip.
    RawSettings raw;
    raw.tuningOverlay = true;
    raw.hasTuningOverlay = true;
    raw.dataWindow = true;
    raw.hasDataWindow = true;
    Settings out;
    applySettings(raw, out, false);
    CW_CHECK(out.tuningOverlay);
    CW_CHECK(out.dataWindow);

    // Merging a partial update must leave them alone.
    RawSettings partial;
    partial.impactGate = true;
    partial.hasImpactGate = true;
    applySettings(partial, out, true);
    CW_CHECK(out.tuningOverlay);
    CW_CHECK(out.dataWindow);
    CW_CHECK(out.impactGate);
}

//
// The user's real configuration, reproduced exactly (report: "on the real
// dual-monitor machine wrapping does not happen at all, with or without the
// impact gate"). Two 2560x1440 displays side by side, wrapMode horizontal,
// guard 100, tolerance 10 — the settings this machine actually ran with.
//
namespace {
Settings userConfig(bool gateOn)
{
    Settings s;
    s.wrapMode = WrapMode::HorizontalOnly;
    s.adjacencyTolerancePx = 10;
    s.wrapGuardPx = 100;
    s.impactBandPx = 100;
    s.impactGate = gateOn;
    s.impactSpeedPps = 2000;
    s.edgeThresholdPx = 1;
    s.disableDuringDrag = true;
    return s;
}

std::vector<MonitorBox> dualScreens()
{
    // DISPLAY2 is to the LEFT of the primary: [-2560,0..0,1440] and
    // [0,0..2560,1440], exactly what EnumDisplayMonitors reports here.
    return { box(0, 0, 2560, 1440, true), box(-2560, 0, 0, 1440) };
}
} // namespace

CW_TEST(user_config_gate_off_wraps_between_two_displays)
{
    // Gate off: one sample at the right edge of the primary must land one
    // pixel inside the left edge of the display to its left.
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    const auto [x, y] = e.onMouseMove({ 2559, 720, 1'000'000 }, false, false);
    CW_CHECK_EQ(x, -2559);
    CW_CHECK_EQ(y, 720);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(user_config_gate_off_wraps_both_ways)
{
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    // Right edge of the primary -> the left display.
    CW_CHECK_EQ(e.onMouseMove({ 2559, 300, 1'000'000 }, false, false).first, -2559);
    // ...and the other way: the left display's left edge -> the primary.
    CW_CHECK_EQ(e.onMouseMove({ -2560, 900, 2'000'000 }, false, false).first, 2558);
}

CW_TEST(user_config_inner_edges_never_wrap)
{
    // The seam between the two displays is not an outer edge: Windows already
    // handles it, and wrapping there would fight it.
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    // x=0 is the primary's left edge and x=-1 is the left display's right
    // edge: adjacent within the 10 px tolerance, so neither is outer.
    CW_CHECK_EQ(e.onMouseMove({ 0, 720, 1'000'000 }, false, false).first, 0);
    CW_CHECK_EQ(e.onMouseMove({ -1, 720, 2'000'000 }, false, false).first, -1);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(user_config_fast_hit_wraps_with_the_gate_on)
{
    // A realistic 1000 Hz hand: 20 px every 4 ms = 5000 px/s. Inside the
    // 100 px band the measured speed is the hand's, so a 2000 px/s gate
    // accepts it.
    WrapEngine e(userConfig(true));
    e.rebuild(dualScreens());
    long long t = 1'000'000;
    int64_t x = 2400;
    int wraps = 0;
    int64_t landing = 0;
    for (int i = 0; i < 40 && !wraps; ++i) {
        const auto [nx, ny] = e.onMouseMove({ x, 720, t }, false, false);
        if (nx != x) {
            ++wraps;
            landing = nx;
        }
        t += 4000;
        x = x + 20 > 2559 ? 2559 : x + 20;
    }
    CW_CHECK_EQ(wraps, 1);
    CW_CHECK_EQ(landing, -2559);
    CW_CHECK(e.lastVerdict().measuredPps >= 2000);
}

CW_TEST(user_config_slow_drift_is_the_only_thing_the_gate_blocks)
{
    // The gate must be the ONLY reason a slow drift is refused, and it must
    // say so: that is what makes the report actionable. The counters count
    // every move; the decision log keeps one entry per run.
    WrapEngine e(userConfig(true));
    e.rebuild(dualScreens());
    long long t = 1'000'000;
    for (int64_t x = 2459; x <= 2559; ++x) {
        e.onMouseMove({ x, 720, t }, false, false);
        t += 4000; // 250 px/s
    }
    CW_CHECK_EQ(e.stats().wraps, 0);
    CW_CHECK(e.stats().skipped[static_cast<int>(SkipReason::ImpactGate)] >= 1);
    CW_CHECK(e.stats().skipped[static_cast<int>(SkipReason::NotOnEdge)] >= 1);
    // Nothing else was in the way: no guard, no drag, no modifier, no
    // single-monitor rule.
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Guard)], 0);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Dragging)], 0);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::NoModifier)], 0);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::SingleMonitor)], 0);
    // The decision log names the arrival as an impact-gate rejection, with the
    // speed that failed it — the number that tells the user which way to move
    // the gate.
    bool sawGateRejection = false;
    for (const Decision& d : e.decisions()) {
        if (d.reason == SkipReason::ImpactGate && d.onEdge) {
            sawGateRejection = true;
            CW_CHECK(d.measuredPps > 0);
            CW_CHECK(d.gatePps == 2000);
            CW_CHECK(d.measuredPps < d.gatePps);
        }
    }
    CW_CHECK(sawGateRejection);
}

CW_TEST(user_config_drag_suppression_is_named)
{
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    // Dragging a selection to the edge must not teleport it.
    CW_CHECK_EQ(e.onMouseMove({ 2559, 720, 1'000'000 }, false, true).first, 2559);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Dragging)], 1);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

//
// A pointer outside the union of the monitor rects. The real machine logged
// 1008 moves as "off-monitor" and refused to wrap: the wrap path used the
// exact containment test, so any sample outside every rect disabled wrapping
// everywhere instead of being attributed to the nearest display.
//

CW_TEST(pointer_outside_every_rect_still_has_a_monitor)
{
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true), box(-1920, 0, 0, 1080) }, 10);
    // Exact containment is unchanged: -1 outside, which the layout tests and
    // gap detection rely on.
    CW_CHECK_EQ(t.monitorAt(5000, 100), -1);
    CW_CHECK_EQ(t.monitorAt(-5000, 100), -1);
    // ...but the pointer path always lands on the nearest display.
    CW_CHECK_EQ(t.nearestMonitor(5000, 100), 0);
    CW_CHECK_EQ(t.nearestMonitor(-5000, 100), 1);
    CW_CHECK_EQ(t.nearestMonitor(0, 5000), 0);
    // Inside: identical, and the nearest wins a tie at a shared seam.
    CW_CHECK_EQ(t.nearestMonitor(100, 100), 0);
    CW_CHECK_EQ(t.nearestMonitor(-100, 100), 1);
    // No monitors at all is still -1.
    Topology empty;
    CW_CHECK_EQ(empty.nearestMonitor(0, 0), -1);
}

CW_TEST(pointer_one_pixel_below_the_layout_does_not_disable_wrapping)
{
    // A stale topology (the watcher has not rebuilt yet) puts the real
    // pointer below the reported desktop. The old code returned -1 and
    // suppressed the move; now it attributes the sample to the primary.
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    const auto [x, y] = e.onMouseMove({ 2559, 1440, 1'000'000 }, false, false);
    CW_CHECK(x != 2559 && x != 1440); // it wrapped, it did not fall through
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::OffMonitor)], 0);
}

//
// Remote Desktop compatibility. An RDP session reports ONE virtual display and
// fires WM_DISPLAYCHANGE while it reconnects or resizes — and during that
// window EnumDisplayMonitors can legitimately return nothing. Applying an
// empty enumeration would wipe a working topology and leave the plugin inert
// for the rest of the session.
//

CW_TEST(rdp_single_display_wraps_to_its_own_opposite_edge)
{
    // What an RDP session actually reports: 1920x1080, nothing else.
    Settings s;
    s.wrapMode = WrapMode::Both;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    // Right edge wraps to the same display's left edge, one pixel inside.
    CW_CHECK_EQ(e.onMouseMove({ 1919, 540, 1'000'000 }, false, false).first, 1);
    CW_CHECK_EQ(e.stats().wraps, 1);
    // Leave the anti-oscillation radius before the next attempt, or the guard
    // (correctly) eats it: the landing point was x=1.
    e.onMouseMove({ 960, 540, 1'500'000 }, false, false);
    CW_CHECK_EQ(e.onMouseMove({ 0, 540, 2'000'000 }, false, false).first, 1918);
    CW_CHECK_EQ(e.stats().wraps, 2);
}

CW_TEST(rdp_single_display_needs_the_switch_that_does_not_disable_it)
{
    // `disableOnSingleMonitor` is the one setting that silently turns wrapping
    // off on a one-display machine — which is every RDP session. The engine
    // must attribute it to that reason so the status row can say so.
    Settings s;
    s.disableOnSingleMonitor = true;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    CW_CHECK_EQ(e.onMouseMove({ 1919, 540, 1'000'000 }, false, false).first, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::SingleMonitor)], 1);
}

CW_TEST(rdp_empty_enumeration_keeps_the_last_good_layout)
{
    // The reconnect window: EnumDisplayMonitors returns nothing.
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    const Stats before = e.stats();
    e.rebuild({}); // what a WM_DISPLAYCHANGE during a reconnect delivers
    CW_CHECK_EQ(e.monitorCount(), 2u); // the layout survived
    CW_CHECK_EQ(e.stats().rebuilt, before.rebuilt); // and was not restarted
    CW_CHECK_EQ(e.stats().rebuildRefused, before.rebuildRefused + 1);
    // Wrapping still works on the retained layout.
    CW_CHECK_EQ(e.onMouseMove({ 2559, 720, 1'000'000 }, false, false).first, -2559);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(rdp_original_layout_returns_after_the_session_ends)
{
    // Disconnect the RDP session and the physical dual-display layout comes
    // back. A genuine change must still be applied — only an EMPTY list is
    // refused.
    WrapEngine e(userConfig(false));
    e.rebuild(dualScreens());
    e.rebuild({});
    CW_CHECK_EQ(e.monitorCount(), 2u);
    e.rebuild({ box(0, 0, 1920, 1080, true) }); // RDP's single display
    CW_CHECK_EQ(e.monitorCount(), 1u);
    CW_CHECK_EQ(e.onMouseMove({ 1919, 540, 1'000'000 }, false, false).first, 1);
    e.rebuild(dualScreens()); // back on the desk
    CW_CHECK_EQ(e.monitorCount(), 2u);
    CW_CHECK_EQ(e.onMouseMove({ 2559, 720, 2'000'000 }, false, false).first, -2559);
}

CW_TEST(rdp_impact_gate_measures_on_a_single_display)
{
    // The gate must be usable over RDP too: a fast hit on a single display
    // wraps, a drift does not, and the speed is measured from real timestamps.
    Settings s = userConfig(true);
    s.wrapMode = WrapMode::Both;
    WrapEngine fast(s);
    fast.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    int64_t x = 1750;
    int wraps = 0;
    for (int i = 0; i < 40 && !wraps; ++i) {
        const auto [nx, ny] = fast.onMouseMove({ x, 540, t }, false, false);
        if (nx != x)
            ++wraps;
        t += 4000;
        x = x + 20 > 1919 ? 1919 : x + 20;
    }
    CW_CHECK_EQ(wraps, 1);
    CW_CHECK(fast.lastVerdict().measuredPps >= 2000);

    WrapEngine slow(s);
    slow.rebuild({ box(0, 0, 1920, 1080, true) });
    t = 1'000'000;
    for (int64_t cx = 1819; cx <= 1919; ++cx) {
        slow.onMouseMove({ cx, 540, t }, false, false);
        t += 4000; // 250 px/s
    }
    CW_CHECK_EQ(slow.stats().wraps, 0);
    CW_CHECK(slow.stats().skipped[static_cast<int>(SkipReason::ImpactGate)] >= 1);
}

// The timestamp source must survive a machine up for weeks. The naive
// `counter * 1'000'000 / freq` overflows int64 after ~10.7 days (2^63 / 1e6 /
// 1e7 s), turning every timestamp negative. Measured on a machine up 11.2
// days: t_us = -875612248758 and measured_pps = 0, so the impact gate refused
// every wrap -- "the plugin does nothing" with the gate on.
CW_TEST(qpc_counter_to_us_does_not_overflow_after_weeks_of_uptime)
{
    constexpr long long kFreq = 10'000'000; // 10 MHz, what Windows reports
    // 30 days of uptime: the naive expression would overflow int64 by then.
    const long long now = kFreq * 60LL * 60 * 24 * 30;
    const long long us = counterToUs(now, kFreq);
    CW_CHECK(us > 0);
    CW_CHECK_EQ(us, 60LL * 60 * 24 * 30 * 1'000'000);

    // And a 1 ms step still measures as 1000 us, i.e. 1000 px/s per px.
    const long long a = counterToUs(now, kFreq);
    const long long b = counterToUs(now + kFreq / 1000, kFreq);
    CW_CHECK_EQ(b - a, 1000);
}

CW_TEST(qpc_counter_to_us_at_eleven_days_matches_the_real_value)
{
    // The exact counter value read on the machine that exposed the bug,
    // 11.2 days of uptime at 10 MHz.
    constexpr long long kFreq = 10'000'000;
    const long long now = 9690621586130LL;
    const long long us = counterToUs(now, kFreq);
    CW_CHECK(us > 0);
    // 9690621586130 / 1e7 s = 969062.158613 s = 969062158613 us.
    CW_CHECK_EQ(us, 969062158613LL);
    // The naive expression wrapped to this; assert we do not reproduce it.
    CW_CHECK(us != -875612248758LL);
}

CW_TEST(qpc_counter_to_us_fails_closed_on_a_bad_frequency)
{
    CW_CHECK_EQ(counterToUs(12345678, 0), 0);
    CW_CHECK_EQ(counterToUs(12345678, -1), 0);
    CW_CHECK_EQ(counterToUs(-5, 10'000'000), 0);
}

// End to end: a gate-on engine fed timestamps from a long-running counter
// still measures speed and still wraps a real hit.
CW_TEST(impact_gate_still_wraps_with_a_long_running_clock)
{
    constexpr long long kFreq = 10'000'000;
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    // 11 days in: the bug turned these negative.
    long long counter = kFreq * 60LL * 60 * 24 * 11;
    const long long stepUs = 2000; // 2 ms per 12 px = 6000 px/s
    for (int64_t x = 1811; x < 1919; x += 12) {
        e.onMouseMove({ x, 540, counterToUs(counter, kFreq) }, false, false);
        counter += stepUs * kFreq / 1'000'000;
    }
    const auto [nx, ny] = e.onMouseMove({ 1919, 540, counterToUs(counter, kFreq) },
                                        false, false);
    CW_CHECK_EQ(nx, 1); // wrapped
    CW_CHECK_EQ(e.stats().wraps, 1);
    CW_CHECK(e.lastVerdict().measuredPps >= 2000);
}

// --- regression: the speed must be measured AT the edge ---------------------
//
// The user reported two things about the impact gate: a fast flight from far
// away that BRAKES into the edge still wrapped ("but the speed was not fast"),
// and readings in the tens of thousands ("the wrap speed is counted"). Both
// were real. The measurement used to average the whole approach band, and the
// hook stamped events when they were DELIVERED, so queued events covering real
// distance microseconds apart read as absurd speeds.

namespace {
// A hand that decelerates linearly in DISTANCE from v0 at x0 to v1 at the edge.
// Returns every whole-pixel position with its timestamp, which is what the OS
// actually reports.
std::vector<std::pair<int64_t, long long>> brakingStroke(double v0, double v1,
                                                         long long dtUs)
{
    std::vector<std::pair<int64_t, long long>> out;
    const double x0 = 1700.0, x1 = 1919.0;
    double x = x0;
    long long t = 0;
    int64_t lastPx = (int64_t)x;
    while (x < x1) {
        const double frac = std::min(1.0, (x - x0) / (x1 - x0));
        const double v = v0 + (v1 - v0) * frac;
        x += v * (double)dtUs / 1'000'000.0;
        t += dtUs;
        const int64_t px = static_cast<int64_t>(x > x1 ? x1 : x);
        if (px != lastPx) {
            out.push_back({ px, t });
            lastPx = px;
        }
    }
    return out;
}

bool wrapsStroke(const std::vector<std::pair<int64_t, long long>>& stroke, int band)
{
    WrapEngine e(gateOn(2000, band));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    for (const auto& [x, t] : stroke) {
        const auto [nx, ny] = e.onMouseMove({ x, 540, 1'000'000 + t }, false, false);
        if (nx != x)
            return true;
    }
    return false;
}
} // namespace

CW_TEST(impact_braking_into_the_edge_does_not_wrap)
{
    // The user's first complaint. The hand travels at 4000 px/s far out but is
    // at 250 px/s by the time it reaches the edge. It is not a hit at the edge
    // and must not wrap, whatever the flight speed was.
    for (long long dt : { 1000LL, 2000LL, 4000LL, 8000LL })
        CW_CHECK(!wrapsStroke(brakingStroke(4000, 250, dt), 100));

    // Same with a wider band: the band must no longer decide this at all.
    for (int band : { 50, 100, 150, 250 })
        CW_CHECK(!wrapsStroke(brakingStroke(6000, 150, 8000), band));
}

CW_TEST(impact_uniform_fast_flight_still_wraps_at_every_polling_rate)
{
    // The counterpart: a hand that is genuinely fast AT the edge still wraps,
    // from 1000 Hz down to 125 Hz. Widening the measuring window until it is
    // meaningful is what makes the coarse rates work.
    for (long long dt : { 1000LL, 2000LL, 4000LL, 8000LL }) {
        const auto stroke = brakingStroke(3000, 3000, dt); // constant 3000 px/s
        CW_CHECK(!stroke.empty());
        CW_CHECK(wrapsStroke(stroke, 100));
    }
}

CW_TEST(impact_uniform_slow_approach_never_wraps)
{
    for (long long dt : { 1000LL, 4000LL, 8000LL })
        CW_CHECK(!wrapsStroke(brakingStroke(900, 900, dt), 100));
}

CW_TEST(impact_speed_is_clamped_to_a_plausible_maximum)
{
    // The user's second complaint: readings in the tens of thousands. Whatever
    // the timestamps say, the number shown must stay plausible.
    WrapEngine e(gateOn());
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 1000;
    e.onMouseMove({ 1899, 540, t }, false, false); // 99 px in 1 ms
    t += 1000;
    e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK(e.lastVerdict().measuredPps <= WrapEngine::kImpactSpeedMaxPps);
    CW_CHECK(e.lastVerdict().measuredPps > 0);
}

CW_TEST(impact_queued_delivery_uses_the_driver_timestamp)
{
    // Events that sat in a queue carry near-identical delivery timestamps while
    // their positions cover real distance: 20 px per event, delivered 500 us
    // apart, is 40000 px/s on paper for a hand doing 2500 px/s. osMs is the
    // driver's own tick and must win, so the reading is the hand's speed.
    Settings s = gateOn();
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    long long os = 500'000;
    int64_t x = 1919 - 20 * 5;
    for (int i = 0; i < 5; ++i) {
        e.onMouseMove({ x, 540, t, os }, false, false);
        x += 20;
        t += 500;   // delivery 0.5 ms apart
        os += 8;    // driver 8 ms apart -> 2500 px/s
    }
    const auto [nx, ny] = e.onMouseMove({ 1919, 540, t, os }, false, false);
    CW_CHECK_EQ(nx, 1);
    CW_CHECK(e.lastVerdict().measuredPps <= 3000);
    CW_CHECK(e.lastVerdict().measuredPps >= 2000);
}

CW_TEST(impact_driver_timestamp_can_only_lower_the_speed)
{
    // A stale or wrapped osMs value must never inflate a reading: the elapsed
    // time is the LARGER of the two spans, so the driver tick can only help.
    Settings s = gateOn();
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    // osMs going BACKWARDS across the 32-bit wrap must not produce a negative
    // or enormous window. 0xFFFFFFF0 -> 0x00000010 is a 32 ms real gap.
    long long os = 0xFFFFFFF0LL;
    int64_t x = 1700;
    while (x < 1919) {
        e.onMouseMove({ x, 540, t, os }, false, false);
        x = x + 20 > 1919 ? 1919 : x + 20;
        t += 8000;
        os += 8;
        if (os > 0xFFFFFFFFLL)
            os -= 0x100000000LL;
    }
    // The final sample is the arrival at the edge.
    e.onMouseMove({ x, 540, t, os }, false, false);
    CW_CHECK(e.lastVerdict().measuredPps > 0);
    CW_CHECK(e.lastVerdict().measuredPps <= WrapEngine::kImpactSpeedMaxPps);
}
