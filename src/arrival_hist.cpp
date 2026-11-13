// arrival_hist.cpp — see arrival_hist.h.
#include "arrival_hist.h"

#include <algorithm>
#include <cmath>

namespace cw {

int histSlotForSpeed(long long pps)
{
    const double v = static_cast<double>(
        std::min(std::max<long long>(pps, kHistFloorPps), kHistCeilPps));
    const double t = (std::log(v) - std::log(static_cast<double>(kHistFloorPps)))
        / (std::log(static_cast<double>(kHistCeilPps))
            - std::log(static_cast<double>(kHistFloorPps)));
    int index = static_cast<int>(t * kHistBuckets);
    return std::max(0, std::min(kHistBuckets - 1, index));
}

int histXForSpeed(long long pps, int left, int width)
{
    const double v = static_cast<double>(
        std::min(std::max<long long>(pps, kHistFloorPps), kHistCeilPps));
    const double t = (std::log(v) - std::log(static_cast<double>(kHistFloorPps)))
        / (std::log(static_cast<double>(kHistCeilPps))
            - std::log(static_cast<double>(kHistFloorPps)));
    const double clamped = std::max(0.0, std::min(1.0, t));
    return left + static_cast<int>(clamped * width);
}

Histogram buildHistogram(const std::vector<Arrival>& arrivals, long long gatePps)
{
    Histogram h;
    h.totalRows = static_cast<int>(arrivals.size());
    for (const Arrival& a : arrivals) {
        if (a.gateOff || a.measuredPps <= 0)
            continue;
        const int slot = histSlotForSpeed(a.measuredPps);
        ++h.total[slot];
        if (a.wrapped)
            ++h.wrapped[slot];
        ++h.measured;
        if (a.wrapped)
            ++h.wrappedCount;
        if (a.measuredPps >= gatePps)
            ++h.wouldWrap;
    }
    for (const int t : h.total)
        h.peak = std::max(h.peak, t);
    return h;
}

} // namespace cw
