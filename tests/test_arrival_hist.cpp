// test_arrival_hist.cpp — the one arrival histogram: slot mapping, the
// wrapped/would-wrap split, and the unmeasurable rows it must skip.
#include "test_harness.h"
#include "arrival_hist.h"

#include <cmath>

using namespace cw;

namespace {
Arrival at(long long pps, bool wrapped = false, bool gateOff = false)
{
    Arrival a;
    a.measuredPps = pps;
    a.wrapped = wrapped;
    a.gateOff = gateOff;
    return a;
}
} // namespace

CW_TEST(hist_slot_is_monotonic_and_clamped)
{
    long long prev = -1;
    for (long long pps = 100; pps <= 40000; pps += 137) {
        const int slot = histSlotForSpeed(pps);
        CW_CHECK(slot >= 0 && slot < kHistBuckets);
        CW_CHECK(slot >= prev);
        prev = slot;
    }
    CW_CHECK_EQ(histSlotForSpeed(1), histSlotForSpeed(kHistFloorPps));
    CW_CHECK_EQ(histSlotForSpeed(999999), histSlotForSpeed(kHistCeilPps));
}

CW_TEST(hist_x_matches_the_slot_scale)
{
    // A slot boundary is a boundary of the x scale: slot i covers
    // [x(i/n), x((i+1)/n)). Check the speed at each boundary maps to the same
    // fraction — this is what used to differ between the two old histograms.
    const int width = 448;
    for (int i = 0; i <= kHistBuckets; ++i) {
        // Invert: the speed whose t equals i/n is 500 * (20000/500)^(i/n).
        const double t = static_cast<double>(i) / kHistBuckets;
        const double v = 500.0 * std::pow(20000.0 / 500.0, t);
        const int x = histXForSpeed(static_cast<long long>(v), 0, width);
        const int expected = static_cast<int>(t * width);
        CW_CHECK(std::abs(x - expected) <= 1);
    }
}

CW_TEST(hist_counts_wrapped_refused_and_prediction_separately)
{
    std::vector<Arrival> trace;
    trace.push_back(at(8000, true));           // wrapped, above any gate
    trace.push_back(at(9000, true));
    trace.push_back(at(700, false));           // refused drift
    trace.push_back(at(750, false));
    trace.push_back(at(0, false));             // unmeasurable: skipped
    trace.push_back(at(0, false, true));       // gate was off: skipped
    const Histogram h = buildHistogram(trace, 3000);
    CW_CHECK_EQ(h.measured, 4);
    CW_CHECK_EQ(h.wrappedCount, 2);
    CW_CHECK_EQ(h.wouldWrap, 2);
    CW_CHECK_EQ(h.totalRows, 6);
    int sum = 0;
    for (const int t : h.total)
        sum += t;
    CW_CHECK_EQ(sum, 4);
    int sumWrapped = 0;
    for (const int w : h.wrapped)
        sumWrapped += w;
    CW_CHECK_EQ(sumWrapped, 2);
    CW_CHECK_EQ(h.peak, 2);
}

CW_TEST(hist_separates_slow_wraps_from_fast_refusals_by_colour_not_magic)
{
    // The regression behind the merge: a bar bucket can hold BOTH outcomes, so
    // a reader must see the split instead of one number called "wrap".
    std::vector<Arrival> trace;
    for (int i = 0; i < 3; ++i)
        trace.push_back(at(3000, true));
    for (int i = 0; i < 2; ++i)
        trace.push_back(at(3080, false)); // same bucket, refused
    const Histogram h = buildHistogram(trace, 3500);
    CW_CHECK_EQ(h.wrappedCount, 3);
    CW_CHECK_EQ(h.wouldWrap, 0);
    const int slot = histSlotForSpeed(3000);
    CW_CHECK_EQ(slot, histSlotForSpeed(3080)); // the premise: one bucket
    CW_CHECK_EQ(h.total[slot], 5);
    CW_CHECK_EQ(h.wrapped[slot], 3);
}
