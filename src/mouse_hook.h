// mouse_hook.h — the WH_MOUSE_LL hook on its own message-pumping thread.
//
// Contract (docs/spec.md §Architecture):
//  * The callback does O(µs) work only: read the point and flags, check the
//    gates, run the pure engine, SetCursorPos on a wrap, swallow that one
//    mousemove. Everything else passes through via CallNextHookEx.
//  * NO I/O on the hook thread, ever — not even a log line. A low-level mouse
//    hook runs under a watchdog inside Windows: a callback that takes longer
//    than LowLevelHooksTimeout makes Windows silently unhook it, after which
//    the plugin believes it is installed and nothing wraps. The wrap sink
//    therefore only sets a flag; the protocol thread coalesces it.
//  * Swallowing is the one behaviour this plugin exists for: a wrap replaces
//    the original move with the destination position. Buttons and wheel are
//    never touched, and injected moves (LLMHF_INJECTED) are observed but
//    never wrapped (a programmatic SetCursorPos must not cascade).
//  * Config is confined to the hook thread: settingsChanged posts a message
//    and the hook thread applies it between events. No locks on the hot path.
//  * Health: a 1 s watchdog compares the real pointer position against the
//    last position the hook reported. A pointer that moved with no events
//    means Windows dropped the hook; the watchdog asks the hook thread to
//    reinstall it and counts the event, so the diagnostic can say so.
#pragma once

#include "wrap_logic.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <windows.h>

namespace cw {

class MouseHook {
public:
    // Runs on the hook thread; must be non-blocking (SetCursorPos only).
    using WrapSink = std::function<void(int64_t x, int64_t y)>;

    MouseHook() = default;
    ~MouseHook() { stop(); }
    MouseHook(const MouseHook&) = delete;
    MouseHook& operator=(const MouseHook&) = delete;

    bool start(WrapEngine* engine, WrapSink onWrap);
    void stop();
    bool active() const { return m_running.load(); }
    bool installed() const { return m_installed.load(); }

    // Enable gate: the command's toggle action flips this; the hook checks
    // it first so a disabled plugin costs one atomic load per event.
    void setEnabled(bool on) { m_enabled.store(on); }
    bool enabled() const { return m_enabled.load(); }

    // Thread-safe: stashes the settings and wakes the hook thread to apply.
    void updateSettings(const Settings& settings);

    // Current activation key state, sampled by the hook thread.
    static bool activationKeyHeld(ActivationMode mode);

    // Health counters, filled by the hook and the watchdog (any thread may
    // read them; all atomics).
    struct Health {
        long long reinstalls = 0; // times the hook was reinstalled
        long long misses = 0;     // watchdog cycles that saw a moved pointer
                                  // with no event: Windows had dropped the hook
        long long events = 0;     // mouse-move events the hook delivered
        long long injected = 0;   // of those, programmatic (never wrapped)
        bool responsive = true;   // false while events are not arriving
        // The pointer position the hook last saw (seeded at install), so the
        // data window can show it before the first real move arrives.
        long long seenX = 0;
        long long seenY = 0;
    };
    Health health() const;

    // SetCursorPos failures: a wrap was decided but the pointer did not move.
    long long setCursorFailures() const { return m_setCursorFailures.load(); }

private:
    void run();
    void watch(); // the watchdog thread
    void install(); // hook thread only
    void reinstall(); // hook thread only
    static LRESULT CALLBACK hookProcStatic(int code, WPARAM wp, LPARAM lp);
    LRESULT hookProc(int code, WPARAM wp, LPARAM lp);

    std::thread m_thread;
    std::thread m_watchdog;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_installed { false };
    std::atomic<bool> m_enabled { true };
    DWORD m_threadId = 0;
    WrapEngine* m_engine = nullptr;
    WrapSink m_onWrap;

    // Liveness, written by the hook callback (atomics only), read by the
    // watchdog and by the diagnostics.
    std::atomic<long long> m_events { 0 };
    std::atomic<long long> m_injected { 0 };
    std::atomic<long long> m_seenX { 0 };
    std::atomic<long long> m_seenY { 0 };
    std::atomic<long long> m_reinstalls { 0 };
    std::atomic<long long> m_misses { 0 };
    std::atomic<long long> m_setCursorFailures { 0 };
    std::atomic<bool> m_responsive { true };
    // The hook handle, owned by the hook thread but readable at shutdown.
    std::atomic<long long> m_hookHandle { 0 };

    // Brief: written by any thread under a lock, read by the hook thread only
    // after it sees WM_APP_SETTINGS.
    std::mutex m_pendingMutex;
    Settings m_pendingSettings;
    bool m_pendingValid = false;

    // Hook-thread-only state.
    Settings m_settings;
};

} // namespace cw
