// impact_flash.h — the state of the impact-wrap glow, as a pure state machine.
//
// Kept out of overlay.cpp so it is testable: the glow must (a) never trigger
// unless the impact gate is ON and the wrap SUCCEEDED, (b) fade out on a fixed
// clock so a slow frame cannot leave it stuck, and (c) never be able to affect a
// wrap decision. It is decoration; a bug in it must cost nothing but the glow.
#ifndef CW_IMPACT_FLASH_H
#define CW_IMPACT_FLASH_H

#include "wrap_logic.h"

namespace cw {

class ImpactFlash {
public:
    // How long the glow lives. Short enough not to trail the hand, long enough
    // to catch the corner of the eye.
    static constexpr long long kDurationUs = 180 * 1000;
    // The glow runs along this fraction of the edge, capped in pixels: a light
    // over the WHOLE edge reads as an error highlight, and this is a
    // confirmation of where the pointer went.
    static constexpr double kLengthFraction = 0.40;
    static constexpr int kMaxLengthPx = 520;
    static constexpr int kThickness = 4;

    // Called after a wrap succeeded. `edgeLength` is the length of the edge the
    // pointer came through, so the glow can be sized for it.
    void trigger(EdgeType edge, int64_t edgeCoord, int64_t alongCoord, int edgeLength,
                 long long nowUs);

    // Called after a refused arrival (speed floor, acceleration floor, guard):
    // nothing to show.
    void cancel() { m_active = false; }

    // 0.0 when nothing is showing, otherwise 1.0 fading to 0.0.
    double alphaAt(long long nowUs) const;
    bool activeAt(long long nowUs) const { return alphaAt(nowUs) > 0.0; }
    bool active() const { return m_active; }
    EdgeType edge() const { return m_edge; }
    int64_t edgeCoord() const { return m_edgeCoord; }
    // Where along the edge the glow starts, and how long it runs.
    int64_t startAlong() const { return m_startAlong; }
    int64_t lengthAlong() const { return m_lengthAlong; }

private:
    bool m_active = false;
    EdgeType m_edge = EdgeType::Left;
    int64_t m_edgeCoord = 0;
    int64_t m_startAlong = 0;
    int64_t m_lengthAlong = 0;
    long long m_startUs = 0;
};

} // namespace cw

#endif // CW_IMPACT_FLASH_H
