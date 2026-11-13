// test_hook_latency.cpp — the hot path must never wait on a reader.
//
// Why this exists
// ---------------
// The user reported "the plugin is on but wrapping works only now and then; it
// sometimes works after the mouse sits still for a while". The mouse hook is a
// WH_MOUSE_LL callback, and Windows silently removes the hook when a callback
// exceeds LowLevelHooksTimeout (300 ms by default) — after which the plugin still
// believes it is installed and NOTHING wraps until the watchdog reinstalls it
// about two seconds later. That matches the symptom, including "it works again
// after a pause".
//
// What can stall the callback: WrapEngine::onMouseMove takes the same mutex the
// GUI threads take, and under it allocates (m_history push_back/erase, a
// std::string in every decision). Meanwhile the overlay polls arrivals() at its
// 60 ms repaint cadence and the data window polls diagnostics(); both COPY a
// vector of structs containing strings while holding that mutex.
//
// This test measures the callback path under reader pressure and asserts a p99.9
// two orders of magnitude below the timeout, so a regression that reintroduces
// blocking here fails loudly in CI instead of "sometimes" on the desktop.
#include "test_harness.h"
#include "wrap_logic.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace cw;

namespace {

MonitorBox box(int64_t l, int64_t t, int64_t r, int64_t b, bool primary = false)
{
    MonitorBox m;
    m.left = l;
    m.top = t;
    m.right = r;
    m.bottom = b;
    m.primary = primary;
    return m;
}

long long micros(std::chrono::steady_clock::duration d)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

} // namespace

CW_TEST(hot_path_is_not_blocked_by_readers)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });

    std::atomic<bool> run { true };
    std::atomic<long long> reads { 0 };
    std::atomic<long long> worstRead { 0 };

    // The readers poll at the overlay's real ~60 ms repaint cadence. The failure
    // mode is not "contention is frequent", it is "ONE read held the mutex long
    // enough to blow the hook's deadline", so the writer needs enough samples for
    // the arrival trace to reach its 512-entry cap, which is when a copy is
    // largest.
    auto reader = [&] {
        while (run.load()) {
            const auto begin = std::chrono::steady_clock::now();
            (void)e.arrivals();
            (void)e.diagnostics(true);
            (void)e.decisions();
            const auto end = std::chrono::steady_clock::now();
            const long long us = micros(end - begin);
            long long prev = worstRead.load();
            while (us > prev && !worstRead.compare_exchange_weak(prev, us)) {
            }
            ++reads;
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    };
    std::thread r1(reader);
    std::thread r2(reader);

    std::vector<long long> samples;
    samples.reserve(40000);
    long long t = 1'000'000;
    int64_t x = 200;
    int dir = 1;
    for (int i = 0; i < 40000; ++i) {
        x += 8 * dir;
        if (x >= 1919) {
            dir = -1;
        } else if (x <= 1) {
            dir = 1;
        }
        const auto begin = std::chrono::steady_clock::now();
        e.onMouseMove({ x, 540, t }, false, false);
        const auto end = std::chrono::steady_clock::now();
        samples.push_back(micros(end - begin));
        t += 1000;
        // Let the readers actually take the lock. Without a yield the writer wins
        // the race every time and the measurement is meaningless.
        if (i % 1000 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    run.store(false);
    r1.join();
    r2.join();

    std::sort(samples.begin(), samples.end());
    const long long p50 = samples[samples.size() * 50 / 100];
    const long long p999 = samples[samples.size() * 999 / 1000];
    const long long worst = samples.back();
    std::printf("  onMouseMove: p50=%lldus p99.9=%lldus max=%lldus over %zu samples\n", p50,
                p999, worst, samples.size());
    std::printf("  worst reader call: %lldus (%lld reads)\n", worstRead.load(), reads.load());

    // The budget. LowLevelHooksTimeout is 300 ms; a callback under 1 ms cannot be
    // evicted even with far more contention than this, and 1 ms is already
    // generous for tens of nanoseconds of real work plus a lock.
    if (p999 > 1000) {
        ++cwtest::failures();
        std::printf("  p99.9 %lldus exceeds the 1000us budget: the hot path is blocking\n",
                    p999);
    }
    if (worst > 50'000) {
        ++cwtest::failures();
        std::printf("  worst case %lldus is dangerously close to a hook eviction\n", worst);
    }
    // Sanity: the readers really ran, so this was a contention test.
    CW_CHECK(reads.load() > 3);
}
