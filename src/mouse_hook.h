// mouse_hook.h — the WH_MOUSE_LL hook on its own message-pumping thread.
//
// Contract (docs/spec.md §Architecture):
//  * The callback does O(µs) work only: read the point and flags, check the
//    gates, run the pure engine, SetCursorPos on a wrap, swallow that one
//    mousemove. Everything else passes through via CallNextHookEx.
//  * Swallowing is the one behaviour this plugin exists for: a wrap replaces
//    the original move with the destination position. Buttons and wheel are
//    never touched, and injected moves (LLMHF_INJECTED) are observed but
//    never wrapped (a programmatic SetCursorPos must not cascade).
//  * Config is confined to the hook thread: settingsChanged posts a message
//    and the hook thread applies it between events. No locks on the hot path.
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

private:
    void run();
    static LRESULT CALLBACK hookProcStatic(int code, WPARAM wp, LPARAM lp);
    LRESULT hookProc(int code, WPARAM wp, LPARAM lp);

    std::thread m_thread;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_installed { false };
    std::atomic<bool> m_enabled { true };
    DWORD m_threadId = 0;
    WrapEngine* m_engine = nullptr;
    WrapSink m_onWrap;

    // Brief: written by any thread under a lock, read by the hook thread only
    // after it sees WM_APP_SETTINGS.
    std::mutex m_pendingMutex;
    Settings m_pendingSettings;
    bool m_pendingValid = false;

    // Hook-thread-only state.
    Settings m_settings;
};

} // namespace cw
