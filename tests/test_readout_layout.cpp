// test_readout_layout.cpp — the tuning readout cannot overflow its own window,
// and its text cannot contradict the verdict.
//
// Both defects were invisible to the suite: the window height was a literal and
// the drawing offsets were literals, so nothing could compare them.
#include "test_harness.h"
#include "readout_layout.h"

using namespace cw;

namespace {

EdgeVerdict gateOnVerdict(bool accelEnabled, bool accelMeasured, long long accel)
{
    EdgeVerdict v;
    v.monitorIndex = 0;
    v.measuredPps = 4200;
    v.thresholdPps = 3000;
    v.thresholdAccelPps2 = accelEnabled ? 1600 : 0;
    v.measuredAccelPps2 = accel;
    v.accelMeasured = accelMeasured;
    return v;
}

} // namespace

CW_TEST(readout_height_holds_the_accel_line)
{
    // The user's screenshot: the acceleration line was drawn from y=46 in a
    // 60 px window with a 13 px font, so its ink (to y≈63) crossed the bottom
    // border and left the window. The height must now cover the row's ink.
    const EdgeVerdict v = gateOnVerdict(true, true, 3479);
    CW_CHECK(showsAccelLine(v));
    const int y = readoutAccelY(v);
    // A 13 px Segoe UI line's ink is about 17 px tall from its top.
    const int inkBottom = y + 17;
    const int h = readoutHeight(v);
    CW_CHECK(h > inkBottom);           // the ink fits
    CW_CHECK(h >= inkBottom + 4);      // and so does the padding + border
}

CW_TEST(readout_height_is_small_without_the_accel_line)
{
    // Sized for what is painted: an impact gate with the acceleration condition
    // off must not reserve a row it never draws.
    const EdgeVerdict v = gateOnVerdict(false, false, 0);
    CW_CHECK(!showsAccelLine(v));
    const int h = readoutHeight(v);
    CW_CHECK(h < readoutHeight(gateOnVerdict(true, true, 0)));
    CW_CHECK(h > readoutAccelY(v) - ReadoutMetrics::bigH + 17);
}

CW_TEST(readout_height_covers_the_wrap_word)
{
    // The "guard · blocked" / "wrap" path draws a 24 px value and nothing else.
    EdgeVerdict v = gateOnVerdict(false, false, 0);
    v.guardBlocked = true;
    CW_CHECK(!showsAccelLine(v));
    const int inkBottom = readoutBigY() + 30; // 24 px font, ascender to descender
    CW_CHECK(readoutHeight(v) > inkBottom);
}

CW_TEST(readout_never_claims_a_wrap_and_a_block_together)
{
    // The invariant itself, exhaustively over the flag combinations that matter.
    for (const bool wrapped : { false, true })
        for (const bool guard : { false, true })
            for (const bool accel : { false, true }) {
                EdgeVerdict v = gateOnVerdict(true, true, 100);
                v.wrapped = wrapped;
                v.guardBlocked = guard;
                v.accelBlocked = accel;
                // The engine guarantees this (test_guard_rearm.cpp); the readout
                // must never render a state that reads as both.
                if (v.wrapped && v.guardBlocked)
                    continue;
                if (v.guardBlocked)
                    CW_CHECK(readoutCaption(v) == "guard · blocked");
                if (v.wrapped && !v.guardBlocked && !v.gateOff)
                    CW_CHECK(readoutPrimary(v) != "—");
            }
}

CW_TEST(accel_text_says_measurable_or_not)
{
    // "accel — / 1600" read as "the acceleration is 1600". It must be explicit.
    const EdgeVerdict measured = gateOnVerdict(true, true, 3479);
    CW_CHECK(accelLineText(measured).find("+3479") != std::string::npos);
    CW_CHECK(accelLineText(measured).find("1600") != std::string::npos);

    const EdgeVerdict braking = gateOnVerdict(true, true, -6227);
    CW_CHECK(accelLineText(braking).find("-6227") != std::string::npos);

    const EdgeVerdict unknown = gateOnVerdict(true, false, 0);
    const std::string text = accelLineText(unknown);
    CW_CHECK(text.find("n/a") != std::string::npos);
    CW_CHECK(text.find("—") == std::string::npos); // no dash to misread
    CW_CHECK(text.find("1600") != std::string::npos); // the threshold is still named
}

CW_TEST(readout_caption_matches_the_verdict)
{
    EdgeVerdict off;
    off.gateOff = true;
    off.wrapped = true;
    CW_CHECK(readoutCaption(off) == "edge · impact off");

    EdgeVerdict guard;
    guard.guardBlocked = true;
    CW_CHECK(readoutCaption(guard) == "guard · blocked");
    CW_CHECK(readoutPrimary(guard) == "—"); // blocked, and it never also says wrap

    EdgeVerdict accelerating = gateOnVerdict(true, true, 3479);
    accelerating.wrapped = true;
    CW_CHECK(readoutCaption(accelerating) == "approach speed");
    CW_CHECK(readoutPrimary(accelerating) == "4200 / 3000 px/s");

    EdgeVerdict slow = gateOnVerdict(true, true, 3479);
    slow.accelBlocked = true;
    CW_CHECK(readoutCaption(slow) == "not accelerating");
}
