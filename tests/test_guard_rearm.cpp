// test_guard_rearm.cpp — the anti-oscillation guard chain.
//
// v0.6.0 added a second guard ("still inside the band of the edge that wrapped
// means the same arrival") on top of the landing-distance guard. It expressed a
// TIME condition with a SPACE test, and its slack, max(edgeThresholdPx, 2) = 2 px,
// is the same magnitude as driver jitter. Measured consequences:
//
//   * away=2 blocks, away=3 wraps -> the outcome of one gesture depends on one
//     pixel of noise ("works now and then").
//   * After a wrap the hand keeps pushing; once it drifts 3 px the very next
//     sample wraps AGAIN, pulling the pointer back to the landing point
//     ("the pointer flies off after a wrap").
//   * The guard branch set guardBlocked but never cleared the previous wrapped,
//     so the readout could show "blocked" and "wrap" at the same time.
//
// v0.7.0 replaces it with an explicit time cooldown and makes the verdict
// per-sample.
#include "test_harness.h"
#include "wrap_logic.h"

using namespace cw;

namespace {

MonitorBox single()
{
    MonitorBox m;
    m.left = 0;
    m.top = 0;
    m.right = 1920;
    m.bottom = 1080;
    m.primary = true;
    return m;
}

// A fresh pointer position with a monotonically increasing timestamp.
struct Driver {
    WrapEngine& engine;
    long long t = 1000;
    std::pair<int64_t, int64_t> move(int64_t x, int64_t y, long long stepUs = 8000)
    {
        t += stepUs;
        return engine.onMouseMove({ x, y, t }, false, false);
    }
    EdgeVerdict verdict() const { return engine.lastVerdict(); }
};

} // namespace

// --- the re-arm boundary ---------------------------------------------------

CW_TEST(guard_rearms_a_few_pixels_off_the_edge_not_fifteen)
{
    // Wrap from the right edge, landing at x=1. The hand is still at the edge.
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    CW_CHECK_EQ(d.move(1919, 540).first, 1);

    // Pushing further right is not a new arrival either. Windows clamps the
    // pointer at the edge, so the hand keeps reporting 1919 — the case that
    // matters is "many samples, one push".
    for (int i = 0; i < 40; ++i) {
        const auto [nx, ny] = d.move(1919, 540);
        CW_CHECK_EQ(nx, 1919);
        CW_CHECK_EQ(ny, 540);
    }

    // Coming back INSIDE the monitor ends the push. The next arrival wraps again
    // — a few pixels is enough; the old latch demanded 3 px plus a lucky sample.
    CW_CHECK_EQ(d.move(1910, 540).first, 1910);
    CW_CHECK_EQ(d.move(1919, 540).first, 1);
}

CW_TEST(guard_is_symmetric_on_both_horizontal_edges)
{
    // The old space-based guard was off by one in opposite directions on the two
    // edges, so the same gesture could succeed on one side and fail on the other.
    // Both must now behave identically, and so must both vertical edges.
    for (const bool rightEdge : { true, false }) {
        WrapEngine e;
        e.rebuild({ single() });
        Driver d { e };
        const int64_t edgeX = rightEdge ? 1919 : 0;
        const int64_t out = rightEdge ? 1 : -1;
        const auto [x1, y1] = d.move(edgeX, 540);
        CW_CHECK(x1 != edgeX); // it wrapped
        CW_CHECK_EQ(y1, 540);

        // Holding / pushing further out: never a second wrap. Windows clamps the
        // coordinate, so the hand keeps reporting the edge.
        for (int i = 0; i < 20; ++i) {
            const int64_t x = std::max<int64_t>(0, std::min<int64_t>(1919, edgeX + out));
            const auto [nx, ny] = d.move(x, 540);
            CW_CHECK_EQ(nx, x);
            CW_CHECK_EQ(ny, 540);
        }
        // Coming back inside re-arms, identically on both sides.
        const int64_t inside = edgeX - out * 10;
        CW_CHECK_EQ(d.move(inside, 540).first, inside);
        const auto [x2, y2] = d.move(edgeX, 540);
        CW_CHECK(x2 != edgeX);
        CW_CHECK_EQ(y2, 540);
    }
    for (const bool bottomEdge : { true, false }) {
        WrapEngine e;
        e.rebuild({ single() });
        Driver d { e };
        const int64_t edgeY = bottomEdge ? 1079 : 0;
        const int64_t out = bottomEdge ? 1 : -1;
        const auto [x1, y1] = d.move(960, edgeY);
        CW_CHECK(y1 != edgeY);
        CW_CHECK_EQ(x1, 960);
        for (int i = 0; i < 20; ++i) {
            const int64_t y = std::max<int64_t>(0, std::min<int64_t>(1079, edgeY + out));
            const auto [nx, ny] = d.move(960, y);
            CW_CHECK_EQ(ny, y);
            CW_CHECK_EQ(nx, 960);
        }
        const int64_t inside = edgeY - out * 10;
        CW_CHECK_EQ(d.move(960, inside).second, inside);
        CW_CHECK(d.move(960, edgeY).second != edgeY);
    }
}

CW_TEST(guard_cooldown_is_short_enough_to_feel_instant)
{
    // A user who bumps the edge twice in a row must get two wraps, so the
    // cooldown has to be well under human pause time. 120 ms is the shipped
    // value; 150 ms must already be enough.
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    CW_CHECK_EQ(d.move(1919, 540).first, 1);
    // Leave the landing point entirely, then come back after 150 ms.
    d.move(960, 540);
    CW_CHECK_EQ(d.move(1919, 540, 150 * 1000).first, 1);
}

// --- the readout must never contradict itself ------------------------------

CW_TEST(verdict_is_per_sample_never_stale)
{
    // A guard-suppressed sample must not inherit the previous sample's `wrapped`.
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    CW_CHECK_EQ(d.move(1919, 540).first, 1);
    CW_CHECK(d.verdict().wrapped);
    CW_CHECK(!d.verdict().guardBlocked);

    // Immediately back at the edge: the guard refuses, and the verdict must say
    // ONLY that.
    const auto [nx, ny] = d.move(1920, 540);
    CW_CHECK_EQ(nx, 1920);
    CW_CHECK_EQ(ny, 540);
    const EdgeVerdict v = e.lastVerdict();
    CW_CHECK(v.guardBlocked);
    CW_CHECK(!v.wrapped); // <- the bug: this used to stay true
}

CW_TEST(verdict_wrapped_and_guard_blocked_are_mutually_exclusive)
{
    // Exhaustively: whatever the sequence, the two flags cannot both be true.
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    const int64_t xs[] = { 1919, 1920, 1921, 1922, 1915, 1900, 0, 1, 2, 5 };
    for (int rep = 0; rep < 40; ++rep) {
        for (const int64_t x : xs) {
            d.move(x, 540 + (rep % 7));
            const EdgeVerdict v = e.lastVerdict();
            CW_CHECK(!(v.wrapped && v.guardBlocked));
        }
    }
}

// --- consecutive crossings at the left and right edges ----------------------
//
// The user's "连续在屏幕左右两边边缘穿越的时候有概率不成功": cross left, then
// cross right, then left again — each without first sweeping far away. Every
// wrap lands the pointer ON an outer edge, so without a time expiry the
// landing circle of the previous wrap sat on exactly the edge the next
// gesture pushes into, and normal mode (no gate to fail a pinned push closed)
// refused it forever.

CW_TEST(consecutive_pinned_crossings_wrap_in_normal_mode)
{
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    // First crossing: push right at the right edge.
    CW_CHECK_EQ(d.move(1919, 540).first, 1);
    // The hand rests 400 ms (the landing circle expires), then pushes LEFT at
    // the landing edge — pinned, the samples repeat x=1.
    CW_CHECK_EQ(d.move(1, 540, 400'000).first, 1918);
    // And the third crossing the same way.
    CW_CHECK_EQ(d.move(1919, 540, 400'000).first, 1);
    CW_CHECK_EQ(e.stats().wraps, 3);
    // No other reason contributed: the guard is not the answer any more.
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::Guard)], 0);
    CW_CHECK_EQ(e.stats().skipped[static_cast<int>(SkipReason::NotOnEdge)], 0);
}

CW_TEST(consecutive_crossings_still_refuse_inside_the_cooldown)
{
    // The circle must keep swallowing the residual push: within the cooldown a
    // pinned sample at the landing edge is guard-blocked, not wrapped, so the
    // pointer cannot oscillate between the two edges on one continuous push.
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    CW_CHECK_EQ(d.move(1919, 540).first, 1);
    const auto [nx, ny] = d.move(1, 540, 50'000); // 50 ms after the wrap
    (void)nx;
    (void)ny;
    CW_CHECK(d.verdict().guardBlocked);
    CW_CHECK_EQ(e.stats().wraps, 1);
}

CW_TEST(consecutive_crossings_survive_high_report_rates)
{
    // The same gestures at 1000 Hz (1 ms steps): the expiry must work off the
    // timestamps, not off a sample count — and the ONE wrap the expiry allows
    // re-arms the push latch, so the still-continuous push cannot wrap again
    // until the hand leaves the edge (one continuous push is one wrap).
    WrapEngine e;
    e.rebuild({ single() });
    Driver d { e };
    CW_CHECK_EQ(d.move(1919, 540, 1000).first, 1);
    for (int i = 0; i < 400; ++i) // 400 ms of pinned push at the landing edge
        CW_CHECK_EQ(d.move(1, 540, 1000).first, i == 300 ? 1918 : 1);
    CW_CHECK_EQ(e.stats().wraps, 2);
}
// --- the landing guard's time expiry ----------------------------------------
//
// An R->L wrap lands ON the destination monitor's right edge. The next L->R
// gesture pins the pointer there: clamped, it can never travel the guard's
// 50 px, so the landing circle refused it forever ("连续在左右边缘穿越有概率
// 不成功"). The circle expires by TIME in both modes now: the hand's residual
// push points away from the landing edge (it was the motion that wrapped), so
// after the cooldown only a deliberate return reaches the circle. On the gated
// path the gate itself still refuses a pinned push (no travel -> unmeasurable
// -> fail closed); on the plain path the push at the landing edge IS the
// deliberate gesture and wraps.

CW_TEST(landing_guard_expires_in_both_modes)
{
    for (const bool gateOn : { true, false }) {
        WrapEngine e;
        {
            Settings s;
            s.impactGate = gateOn;
            s.impactSpeedPps = 2000;
            s.impactBandPx = 400;
            e.setSettings(s);
        }
        e.rebuild({ single() });
        Driver d { e };
        // A run-up so the gated path can measure; then wrap at the right edge,
        // landing at x=1 — which is ON the left outer edge (threshold 1),
        // exactly the trap shape the user hit on their right monitor's edge.
        CW_CHECK_EQ(d.move(1600, 540).first, 1600);
        CW_CHECK_EQ(d.move(1919, 540).first, 1);

        // Right after the wrap the circle still holds: a sample pinned at the
        // landing is guard-blocked, not a new gesture.
        const auto [bx, by] = d.move(1, 540, 100'000); // +100 ms
        (void)bx;
        (void)by;
        CW_CHECK(d.verdict().guardBlocked);

        // Past the cooldown the same spot is no longer guard-blocked. On the
        // gated path it reaches the gate, which measures zero travel and
        // refuses fail-closed; on the plain path it wraps — the deliberate
        // second push the user asked for.
        const auto [cx, cy] = d.move(1, 540, 300'000); // +400 ms since the wrap
        (void)cx;
        (void)cy;
        CW_CHECK(!d.verdict().guardBlocked);
        if (gateOn) {
            CW_CHECK(!d.verdict().wrapped);
            CW_CHECK_EQ(d.verdict().measuredPps, 0);
        } else {
            CW_CHECK(d.verdict().wrapped);
        }
    }
}