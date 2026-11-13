// arrival_hist.h — the one arrival-speed histogram, shared by every surface.
//
// There used to be two: the overlay's trace panel and the data window each
// bucketed the arrivals themselves, with different bucket counts, different
// handling of unmeasurable rows and — the one that bit — different axis
// mappings, so the same speed landed at different x positions in the two
// views and the gate line could sit over the wrong bar. One function now:
// slots are the unit of the axis, and every position is derived from them.
#ifndef CW_ARRIVAL_HIST_H
#define CW_ARRIVAL_HIST_H

#include "wrap_logic.h"

namespace cw {

constexpr int kHistBuckets = 26;
// Speeds under the floor are bucketed linearly; above it, logarithmically —
// the interesting range is 500…20000 px/s and a linear axis would waste the
// width on the tail.
constexpr long long kHistFloorPps = 500;
constexpr long long kHistCeilPps = 20000;

// One histogram over the recorded arrivals. `wrapped` is what actually
// happened (the verdict of the moment); `wouldWrap` is the prediction at
// `gatePps` — the old overlay panel printed the prediction under the label
// "wrap", which read as "low speeds wrapped too" and started the confusion.
struct Histogram {
    int total[kHistBuckets] = {};    // every usable arrival
    int wrapped[kHistBuckets] = {};  // the ones that wrapped when they happened
    int peak = 0;                    // max(total), for scaling the bars
    int measured = 0;                // usable rows
    int wrappedCount = 0;            // rows that wrapped
    int wouldWrap = 0;               // rows at or above gatePps
    int totalRows = 0;               // every arrival, including unmeasurable
};

// Unmeasurable rows (gateOff, speed 0) are skipped: they carry no information
// about where the gate belongs, and the old data window slammed them into the
// floor bucket, which made the leftmost bar lie.
Histogram buildHistogram(const std::vector<Arrival>& arrivals, long long gatePps);

// The bucket a speed falls into, 0..kHistBuckets-1. Monotonic; clamped.
int histSlotForSpeed(long long pps);

// The x of a speed on a plot of `width` px starting at `left`, matching the
// slot scale exactly (a slot boundary is a boundary of this scale).
int histXForSpeed(long long pps, int left, int width);

} // namespace cw

#endif // CW_ARRIVAL_HIST_H
