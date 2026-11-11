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
    e.onMouseMove({ 1919, 540 }, false, false); // wraps, lands at x=1
    CW_CHECK_EQ(e.stats().wraps, 1);
    // Immediately back at the destination: guard absorbs, no new verdict.
    e.onMouseMove({ 0, 540 }, false, false);
    CW_CHECK_EQ(e.stats().wraps, 1);
    const EdgeVerdict v = e.lastVerdict();
    // The verdict still describes the previous (wrapped) arrival: guard
    // arrivals never reach the edge decision.
    CW_CHECK(v.wrapped);
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
