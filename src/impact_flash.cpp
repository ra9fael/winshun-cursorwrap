// impact_flash.cpp — see impact_flash.h.
#include "impact_flash.h"

#include <algorithm>
#include <cmath>

namespace cw {

void ImpactFlash::trigger(EdgeType edge, int64_t edgeCoord, int64_t alongCoord,
                          int edgeLength, long long nowUs)
{
    m_active = true;
    m_edge = edge;
    m_edgeCoord = edgeCoord;
    m_startUs = nowUs;

    const int64_t wanted = std::min<int64_t>(
        kMaxLengthPx, static_cast<int64_t>(edgeLength * kLengthFraction));
    m_lengthAlong = std::max<int64_t>(1, wanted);

    // Centre the glow on where the pointer left, then keep it on the edge: a
    // glow hanging off the end of the screen looks like a rendering fault.
    int64_t start = alongCoord - m_lengthAlong / 2;
    const int64_t maxStart = std::max<int64_t>(0, edgeLength - m_lengthAlong);
    m_startAlong = std::max<int64_t>(0, std::min(start, maxStart));
}

double ImpactFlash::alphaAt(long long nowUs) const
{
    if (!m_active)
        return 0.0;
    // A clock that went backwards (or an unknown timestamp) means the glow is
    // stale: let it fade rather than stick on screen forever.
    const long long elapsed = nowUs - m_startUs;
    if (elapsed < 0 || elapsed >= kDurationUs) {
        // `m_active` is not mutable const state here on purpose: alphaAt is a
        // pure query, and the caller stops drawing once this reaches 0.
        return 0.0;
    }
    // Ease out: bright immediately, then a long tail. A linear fade reads as a
    // flicker; an ease-in reads as nothing happening.
    const double t = static_cast<double>(elapsed) / static_cast<double>(kDurationUs);
    return (1.0 - t) * (1.0 - t * 0.35);
}

} // namespace cw
