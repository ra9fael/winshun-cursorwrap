// test_tuner.cpp — the edge-arrival verdict the tuning overlay consumes:
// the gate's measurement, the landing destination and the state flags.
#include "test_harness.h"
#include "wrap_logic.h"

using namespace cw;

namespace {
MonitorBox box(int64_t l, int64_t t, int64_t r, int64_t b, bool primary = false)
{
    MonitorBox m;
    m.left = l;
    m.top = t;
    m.bottom = b;
    m.right = r;
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

CW_TEST(verdict_gate_off_reports_edge)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540, 1'000'000 }, false, false);
    CW_CHECK_EQ(x, 1); // wrapped, one pixel inside the destination (left) edge
    const EdgeVerdict v = e.lastVerdict();
    CW_CHECK(v.tUs == 1'000'000);
    CW_CHECK(v.gateOff);
    CW_CHECK(v.wrapped);
    CW_CHECK(v.monitorIndex == 0);
    CW_CHECK(v.edge == EdgeType::Right);
    CW_CHECK(v.edgeCoord == 1919);
    CW_CHECK(v.thresholdPps == 0);
    CW_CHECK(v.destX == x); // the overlay's guard circle sits at the landing
    (void)y;
}

CW_TEST(verdict_gate_blocked_reports_speed)
{
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    for (int x = 1807; x <= 1919; ++x) {
        e.onMouseMove({ x, 540, t }, false, false);
        t += 4000; // 250 px/s: under the gate
    }
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1919); // suppressed
    const EdgeVerdict v = e.lastVerdict();
    CW_CHECK(!v.wrapped);
    CW_CHECK(!v.gateOff);
    CW_CHECK(v.measuredPps > 0 && v.measuredPps < 2000);
    CW_CHECK(v.thresholdPps == 2000);
    CW_CHECK(v.tUs == t);
    (void)y;
}

CW_TEST(verdict_fast_hit_reports_wrap_and_destination)
{
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 2000;
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1);
    const EdgeVerdict v = e.lastVerdict();
    CW_CHECK(v.wrapped);
    CW_CHECK(v.measuredPps >= 2000);
    CW_CHECK(v.destX == x);
    CW_CHECK(v.destY == y);
    (void)y;
}

CW_TEST(verdict_guard_blocked_flag)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540, 1'000'000 }, false, false); // wraps, lands at x=1
    CW_CHECK_EQ(e.stats().wraps, 1);
    // Pushing further OUT from the same edge: the guard absorbs it and no new
    // wrap happens. The verdict must now describe THIS sample, so `wrapped` is
    // false — before v0.7.0 it kept the previous sample's true, which is why the
    // readout could show "guard blocked" and "wrap" together.
    e.onMouseMove({ 1920, 540, 1'008'000 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, 1);
    const EdgeVerdict v = e.lastVerdict();
    CW_CHECK(v.guardBlocked);
    CW_CHECK(!v.wrapped);
}

CW_TEST(verdict_resets_with_state)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540, 1'000 }, false, false);
    CW_CHECK(e.lastVerdict().tUs == 1'000);
    e.resetState();
    CW_CHECK(e.lastVerdict().tUs == 0);
}

// --- the tuning trace ------------------------------------------------------
//
// A trace of one row per edge arrival, and the suggestion derived from it:
// the gate belongs in the widest gap between the "intentional push" cluster
// and the "incidental drift" cluster.

CW_TEST(engine_records_one_arrival_per_edge_hit)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1910, 540, 1'000'000 }, false, false);
    e.onMouseMove({ 1919, 540, 1'008'000 }, false, false);
    e.onMouseMove({ 1919, 541, 1'016'000 }, false, false); // on the edge, different y
    const auto trace = e.arrivals();
    CW_CHECK(trace.size() >= 1);
    // Every recorded row is an edge arrival, never a sample from the middle.
    for (const Arrival& a : trace)
        CW_CHECK(a.seq > 0 && a.tUs > 0);
    CW_CHECK(trace.back().wrapped || trace.back().gateOff || trace.back().measuredPps >= 0);
}

CW_TEST(engine_trace_is_bounded)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    for (int i = 0; i < 900; ++i) {
        e.onMouseMove({ 1919, 100 + (i % 800), t + i * 1000 }, false, false);
    }
    CW_CHECK(e.arrivals().size() <= WrapEngine::kTraceMax);
    CW_CHECK(!e.arrivals().empty());
}

CW_TEST(engine_clear_arrivals_empties_the_trace)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    e.onMouseMove({ 1919, 540, 1'000'000 }, false, false);
    CW_CHECK(!e.arrivals().empty());
    e.clearArrivals();
    CW_CHECK(e.arrivals().empty());
}

CW_TEST(suggest_gate_needs_enough_arrivals)
{
    std::vector<Arrival> few;
    for (int i = 0; i < 5; ++i) {
        Arrival a;
        a.measuredPps = 1000 + i * 1000;
        few.push_back(a);
    }
    CW_CHECK_EQ(adviseGate(few, 0).pps, 0);
    CW_CHECK_EQ(adviseGate(few, 0).considered, 5);
}

CW_TEST(suggest_gate_finds_the_gap_between_clusters)
{
    // Twenty slow drifts around 700 px/s, twenty pushes around 5000 px/s.
    std::vector<Arrival> trace;
    for (int i = 0; i < 20; ++i) {
        Arrival a;
        a.measuredPps = 620 + (i % 7) * 20; // 620..740
        trace.push_back(a);
    }
    for (int i = 0; i < 20; ++i) {
        Arrival a;
        a.measuredPps = 4800 + (i % 9) * 40; // 4800..5120
        trace.push_back(a);
    }
    const GateAdvice advice = adviseGate(trace, 0);
    CW_CHECK(advice.pps > 740);
    CW_CHECK(advice.pps < 4800);
}

CW_TEST(suggest_gate_says_fits_when_the_gate_already_separates)
{
    // The user asked why a good gate kept getting a different suggestion: the
    // answer "keep it" must win over a second number.
    std::vector<Arrival> trace;
    for (int i = 0; i < 20; ++i) {
        Arrival a;
        a.measuredPps = 620 + (i % 7) * 20; // drifts 620..740
        trace.push_back(a);
    }
    for (int i = 0; i < 20; ++i) {
        Arrival a;
        a.measuredPps = 4800 + (i % 9) * 40; // pushes 4800..5120
        trace.push_back(a);
    }
    // 2900 sits in the 740..4800 gap; sqrt(740*4800) ≈ 1884 would otherwise be
    // printed next to a gate that already works.
    const GateAdvice advice = adviseGate(trace, 2900);
    CW_CHECK(advice.fits);
    CW_CHECK_EQ(advice.pps, 0);
    // A gate above the fast cluster refuses real flicks, so it gets a number.
    const GateAdvice other = adviseGate(trace, 6000);
    CW_CHECK(!other.fits);
    CW_CHECK(other.pps > 0);
}

CW_TEST(suggest_gate_is_silent_for_one_cluster)
{
    // A single tight cluster has no meaningful gap: no suggestion.
    std::vector<Arrival> trace;
    for (int i = 0; i < 40; ++i) {
        Arrival a;
        a.measuredPps = 3000 + (i % 25) * 8;
        trace.push_back(a);
    }
    const GateAdvice advice = adviseGate(trace, 3000);
    CW_CHECK_EQ(advice.pps, 0);
    CW_CHECK(!advice.fits);
}

CW_TEST(suggest_gate_needs_a_multiplicative_gap)
{
    // The regression behind "gate 3000, best 908": a 25%-of-value bump between
    // neighbouring speeds in ONE cluster was treated as two populations, and
    // the per-mille metric favoured exactly the low end. A gap must be a real
    // separation (≥ 1.5×) now.
    std::vector<Arrival> trace;
    for (int i = 0; i < 40; ++i) {
        Arrival a;
        // 800, 1015, 1230, ... a smooth spread with no 1.5× step anywhere.
        a.measuredPps = 800 + i * 215;
        trace.push_back(a);
    }
    const GateAdvice advice = adviseGate(trace, 0);
    CW_CHECK_EQ(advice.pps, 0);
}

CW_TEST(suggest_gate_ignores_unmeasurable_arrivals)
{
    std::vector<Arrival> trace;
    for (int i = 0; i < 10; ++i) {
        Arrival a;
        a.measuredPps = 0; // the gate was off / could not measure
        a.gateOff = true;
        trace.push_back(a);
    }
    CW_CHECK_EQ(adviseGate(trace, 0).pps, 0);
    CW_CHECK_EQ(adviseGate(trace, 0).considered, 0);
}

CW_TEST(suggest_gate_stays_inside_the_setting_range)
{
    std::vector<Arrival> trace;
    for (int i = 0; i < 10; ++i) {
        Arrival a;
        a.measuredPps = 120 + i; // absurdly slow
        trace.push_back(a);
    }
    for (int i = 0; i < 10; ++i) {
        Arrival a;
        a.measuredPps = 40000 + i; // absurdly fast
        trace.push_back(a);
    }
    const GateAdvice advice = adviseGate(trace, 0);
    CW_CHECK(advice.pps == 0 || (advice.pps >= 500 && advice.pps <= 20000));
}

CW_TEST(engine_trace_records_blocked_arrivals_too)
{
    // A drift the gate rejects must still appear in the trace: without the
    // slow cluster the suggestion can never find the gap.
    WrapEngine e(gateOn(4000, 300));
    e.rebuild({ box(0, 0, 2560, 1440, true) });
    long long t = 1'000'000;
    int64_t x = 2450;
    for (int i = 0; i < 40; ++i) {
        e.onMouseMove({ x, 720, t }, false, false);
        t += 16'000;                     // 4 px / 16 ms = 250 px/s
        x = x + 4 > 2559 ? 2559 : x + 4; // reaches the edge and stays there
    }
    const auto trace = e.arrivals();
    CW_CHECK(!trace.empty());          // the arrival is recorded...
    CW_CHECK_EQ(e.stats().wraps, 0);   // ...but it did not wrap
    bool sawBlocked = false;
    for (const Arrival& a : trace)
        if (!a.wrapped)
            sawBlocked = true;
    CW_CHECK(sawBlocked);
}

CW_TEST(engine_trace_counts_one_arrival_per_edge_run)
{
    // The pointer clamped at an edge for 500 samples is ONE arrival: the hand
    // pushed there once.
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 2560, 1440, true) });
    long long t = 1'000'000;
    for (int i = 0; i < 500; ++i) {
        e.onMouseMove({ 2559, 720, t }, false, false);
        t += 1000;
    }
    CW_CHECK(e.arrivals().size() <= 2);
}

// --- the trace's lifetime ---------------------------------------------------
//
// A row measured under an old gate says nothing about the new one. Mixing eras
// is what once put "best 908" next to a gate of 3000: the slow rows were real,
// but they belonged to a gate that no longer existed.

CW_TEST(engine_trace_resets_when_the_impact_settings_change)
{
    WrapEngine e(gateOn(2000, 300));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    for (int64_t x = 1500; x < 1919; x += 30) {
        e.onMouseMove({ x, 540, t }, false, false);
        t += 8'000;
    }
    e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK(!e.arrivals().empty());

    // A gate change wipes the trace...
    e.setSettings(gateOn(4000, 300));
    CW_CHECK(e.arrivals().empty());
    // ...and re-arms the arrival dedup, so the next push is a fresh row.
    e.onMouseMove({ 1919, 540, t += 8'000 }, false, false);
    e.onMouseMove({ 1919, 541, t += 8'000 }, false, false);
    CW_CHECK(e.arrivals().size() <= 1);

    // Pushing the SAME values again must not wipe it: a settings push is a
    // full snapshot, not a change.
    const size_t before = e.arrivals().size();
    e.setSettings(gateOn(4000, 300));
    CW_CHECK_EQ(e.arrivals().size(), before);
    // The band and the acceleration floor count as changes too.
    e.setSettings(gateOn(4000, 350));
    CW_CHECK(e.arrivals().empty());
}

// --- the wrap latch ---------------------------------------------------------
//
// The impact glow used to be triggered from the per-sample verdict, which lives
// for one sample: a 60 ms repaint poll missed it essentially every time on a
// real machine (the shell never saw this — its scripted hand stops after the
// wrap). The engine now latches the wrap.

CW_TEST(wrap_event_latches_and_survives_guard_samples)
{
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 2000;
    const auto [x, y] = e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(x, 1); // wrapped
    (void)y;
    const WrapEvent ev = e.lastWrapEvent();
    CW_CHECK_EQ(ev.seq, 1);
    CW_CHECK(ev.edge == EdgeType::Right);
    CW_CHECK_EQ(ev.edgeCoord, 1919);
    CW_CHECK_EQ(ev.along, 540);

    // The samples that follow are guard-blocked — a real hand keeps pushing —
    // and must NOT cancel the latch (this is the sequence the old poll lost).
    e.onMouseMove({ 1919, 541, t += 4'000 }, false, false);
    CW_CHECK(e.lastVerdict().guardBlocked);
    CW_CHECK_EQ(e.lastWrapEvent().seq, 1);
    // The latched arrival verdict still describes the wrap, so the readout can
    // show the measured numbers instead of flickering away.
    CW_CHECK(e.lastArrivalVerdict().wrapped);
    CW_CHECK(e.lastArrivalVerdict().measuredPps >= 2000);
    // A second wrap bumps the sequence: the consumer sees a NEW event.
    e.onMouseMove({ 1919, 540, t += 4'000 }, false, false); // still the same edge
    CW_CHECK_EQ(e.stats().wraps, 1); // the guard holds: one push, one wrap
}

CW_TEST(wrap_event_drops_when_the_landing_fails)
{
    WrapEngine e(gateOn(2000, 150));
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    long long t = 1'000'000;
    e.onMouseMove({ 1800, 540, t }, false, false);
    t += 2000;
    e.onMouseMove({ 1919, 540, t }, false, false);
    CW_CHECK_EQ(e.lastWrapEvent().seq, 1);
    // The OS refused the landing: the wrap never happened, so the glow must
    // not light an edge for it.
    e.onWrapFailed();
    CW_CHECK_EQ(e.lastWrapEvent().seq, 0);
    CW_CHECK(e.arrivals().empty()); // the phantom row is gone from the trace too
}
