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
// --- the gated landing guard's time expiry ---------------------------------
//
// An R->L wrap lands ON the destination monitor's right edge. The next L->R
// gesture pins the pointer there: clamped, it can never travel the guard's
// 50 px, so the landing circle refused it forever ("从左到右无法穿越"). With
// the impact gate on, the circle expires after a short cooldown; the gate
// itself refuses a pinned push (no travel -> unmeasurable -> fail closed), so
// the guard is not needed to stop a bounce-back.

CW_TEST(gated_landing_guard_expires_but_normal_mode_does_not)
{
    WrapEngine gated;
    {
        Settings s;
        s.impactGate = true;
        s.impactSpeedPps = 2000;
        s.impactBandPx = 400;
        gated.setSettings(s);
    }
    gated.rebuild({ single() });
    Driver d { gated };
    // A fast approach so the gate can measure. The approach starts INSIDE the
    // band (400 px): the band is a hard limit, and a stroke that starts outside
    // it measures nothing — that is documented behaviour, not the trap here.
    // Wrap at the right edge, landing at x=1 — which is ON the left outer edge
    // (threshold 1), exactly the trap shape the user hit on their right
    // monitor's right edge.
    CW_CHECK_EQ(d.move(1600, 540).first, 1600);
    CW_CHECK_EQ(d.move(1919, 540).first, 1);

    // Right after the wrap the circle still holds: a sample pinned at the
    // landing is guard-blocked, not a gate verdict.
    const auto [bx, by] = d.move(1, 540, 100'000); // +100 ms
    (void)bx;
    (void)by;
    CW_CHECK(d.verdict().guardBlocked);

    // Past the cooldown the same pinned push is no longer guard-blocked: it
    // reaches the gate, which measures zero travel and refuses fail-closed.
    const auto [cx, cy] = d.move(1, 540, 300'000); // +400 ms since the wrap
    (void)cx;
    (void)cy;
    CW_CHECK(!d.verdict().guardBlocked);
    CW_CHECK(!d.verdict().wrapped);
    CW_CHECK_EQ(d.verdict().measuredPps, 0);

    // Normal mode (the gate off) keeps the pure space guard: the circle never
    // expires there, because nothing else stands between a bounce and a wrap.
    WrapEngine plain;
    plain.rebuild({ single() });
    Driver p { plain };
    CW_CHECK_EQ(p.move(1919, 540).first, 1);
    const auto [nx, ny] = p.move(1, 540, 500'000); // half a second later
    (void)nx;
    (void)ny;
    CW_CHECK(p.verdict().guardBlocked);
}