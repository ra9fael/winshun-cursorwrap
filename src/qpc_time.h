// qpc_time.h — QueryPerformanceCounter to microseconds, without overflow.
//
// The obvious spelling is wrong:
//
//     now.QuadPart * 1'000'000LL / freq.QuadPart
//
// `freq` is 10 MHz on Windows, so `now.QuadPart * 1'000'000` overflows a
// signed 64-bit integer once the machine has been up for ~10.7 days
// (2^63 / 1e6 / 1e7 seconds). There is no symptom until it happens, and then
// every timestamp turns negative: the impact gate divides by a negative window
// and refuses every wrap. Measured on a machine up at 11.2 days: the exact
// expression produced t_us = -875612248758 and `measured_pps` = 0, which
// blocked wrapping exactly like "the plugin does nothing".
//
// Splitting the division keeps both products small:
//     whole = now / freq              (seconds, fits easily)
//     rem   = now % freq              (< freq, so rem * 1e6 fits)
//
// The result equals the exact value up to the final truncation, so a
// difference of two timestamps is exact to <1 µs — all the impact gate needs.
#pragma once

#include <cstdint>

namespace cw {

// Exact counter -> microseconds conversion. `now` and `freq` are raw
// QueryPerformanceCounter/QueryPerformanceFrequency values; `freq <= 0` means
// the counter is unusable, and 0 is returned so callers fail closed.
inline long long counterToUs(long long now, long long freq)
{
    if (freq <= 0 || now < 0)
        return 0;
    const long long whole = now / freq;
    const long long rem = now % freq;
    return whole * 1'000'000LL + rem * 1'000'000LL / freq;
}

} // namespace cw
