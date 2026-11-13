// test_impact_flash.cpp — the impact glow is decoration, and these tests are
// what keeps it that way.
#include "test_harness.h"
#include "impact_flash.h"

using namespace cw;

CW_TEST(impact_flash_starts_bright_and_fades_to_nothing)
{
    ImpactFlash f;
    CW_CHECK_EQ(f.alphaAt(1000), 0.0); // nothing has happened
    f.trigger(EdgeType::Right, 1919, 540, 1080, 1000);
    CW_CHECK(f.activeAt(1000));
    CW_CHECK(f.alphaAt(1000) > 0.9);
    // Monotonically decreasing, and gone by the end of its life.
    double prev = f.alphaAt(1000);
    for (long long t = 1000; t < 1000 + ImpactFlash::kDurationUs; t += 5000) {
        const double a = f.alphaAt(t);
        CW_CHECK(a <= prev + 1e-9);
        prev = a;
    }
    CW_CHECK_EQ(f.alphaAt(1000 + ImpactFlash::kDurationUs), 0.0);
    CW_CHECK_EQ(f.alphaAt(1000 + ImpactFlash::kDurationUs * 10), 0.0);
    CW_CHECK(!f.activeAt(1000 + ImpactFlash::kDurationUs));
}

CW_TEST(impact_flash_cancel_discards_it_immediately)
{
    // A refused arrival must not leave the previous glow running: it would read
    // as "that one wrapped too".
    ImpactFlash f;
    f.trigger(EdgeType::Left, 0, 400, 1080, 1000);
    CW_CHECK(f.activeAt(1000));
    f.cancel();
    CW_CHECK(!f.activeAt(1000));
    CW_CHECK(!f.active());
}

CW_TEST(impact_flash_survives_a_backwards_clock)
{
    // QueryPerformanceCounter is monotonic, but a caller passing 0 (unknown) or a
    // stale value must not park the glow on screen.
    ImpactFlash f;
    f.trigger(EdgeType::Top, 0, 300, 1920, 5000);
    CW_CHECK_EQ(f.alphaAt(0), 0.0);
    CW_CHECK_EQ(f.alphaAt(4000), 0.0);
}

CW_TEST(impact_flash_length_is_a_fraction_and_never_whole_edge)
{
    // A glow over the entire edge reads as an error highlight; this is a
    // confirmation of where the pointer went.
    ImpactFlash f;
    f.trigger(EdgeType::Right, 1919, 500, 1000, 1000);
    CW_CHECK_EQ(f.lengthAlong(), 400); // 40% of 1000
    CW_CHECK(f.lengthAlong() < 1000);

    // Capped on a very long edge.
    ImpactFlash longEdge;
    longEdge.trigger(EdgeType::Right, 1919, 500, 100000, 1000);
    CW_CHECK_EQ(longEdge.lengthAlong(), ImpactFlash::kMaxLengthPx);
}

CW_TEST(impact_flash_stays_on_the_edge)
{
    // Landing the glow half off the end of the edge would look like a rendering
    // fault.
    ImpactFlash atStart;
    atStart.trigger(EdgeType::Left, 0, 0, 1000, 1000);
    CW_CHECK_EQ(atStart.startAlong(), 0);

    ImpactFlash atEnd;
    atEnd.trigger(EdgeType::Left, 0, 999, 1000, 1000);
    CW_CHECK_EQ(atEnd.startAlong(), 1000 - atEnd.lengthAlong());
    CW_CHECK(atEnd.startAlong() >= 0);

    // A short edge: the cap must not push the glow past it.
    ImpactFlash tiny;
    tiny.trigger(EdgeType::Top, 0, 10, 20, 1000);
    CW_CHECK(tiny.startAlong() + tiny.lengthAlong() <= 20);
}

CW_TEST(impact_flash_retrigger_restarts_the_clock)
{
    ImpactFlash f;
    f.trigger(EdgeType::Right, 1919, 100, 1000, 1000);
    const double late = f.alphaAt(1000 + ImpactFlash::kDurationUs - 1000);
    CW_CHECK(late < 0.2);
    f.trigger(EdgeType::Right, 1919, 900, 1000, 1000 + ImpactFlash::kDurationUs - 1000);
    CW_CHECK(f.alphaAt(1000 + ImpactFlash::kDurationUs - 1000) > 0.9);
    // Centred on 900 as far as the edge allows: the glow must stay on screen, so
    // it is pushed back to the last position that fits.
    CW_CHECK_EQ(f.lengthAlong(), 400);
    CW_CHECK_EQ(f.startAlong(), 600);
}
