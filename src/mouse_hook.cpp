// mouse_hook.cpp — see mouse_hook.h.
#include "mouse_hook.h"

#include "qpc_time.h"

#include <chrono>
#include <thread>

namespace cw {
namespace {

constexpr UINT WM_APP_SETTINGS = WM_APP + 1;
constexpr UINT WM_APP_REINSTALL = WM_APP + 2;
// The watchdog checks this often. Windows' own LowLevelHooksTimeout defaults
// to 300 ms; a whole second of "pointer moved, no events" cannot be a busy
// moment, it is a dropped hook.
constexpr int kWatchdogMs = 1000;
// Two consecutive misses confirm it, so an ordinary hiccup (a UAC prompt, a
// full-screen exclusive app) does not count as a dropped hook.
constexpr int kMissesToReinstall = 2;

MouseHook* g_hook = nullptr;

} // namespace

bool MouseHook::activationKeyHeld(ActivationMode mode)
{
    switch (mode) {
    case ActivationMode::WhileCtrl:
        return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    case ActivationMode::WhileShift:
        return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    case ActivationMode::Always:
        break;
    }
    return true;
}

bool MouseHook::start(WrapEngine* engine, WrapSink onWrap)
{
    if (!engine || m_running.load())
        return false;
    m_engine = engine;
    m_onWrap = std::move(onWrap);
    m_settings = engine->settings();
    m_running.store(true);
    m_thread = std::thread([this] { run(); });
    // Wait until the hook is actually installed so the status row is honest.
    for (int i = 0; i < 100 && m_running.load(); ++i) {
        if (m_installed.load())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!m_installed.load())
        return false;
    m_watchdog = std::thread([this] { watch(); });
    return true;
}

void MouseHook::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (m_watchdog.joinable())
        m_watchdog.join();
    if (m_thread.joinable())
        m_thread.join();
    m_threadId = 0;
    g_hook = nullptr;
}

void MouseHook::updateSettings(const Settings& settings)
{
    {
        std::lock_guard lock(m_pendingMutex);
        m_pendingSettings = settings;
        m_pendingValid = true;
    }
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_APP_SETTINGS, 0, 0);
}

MouseHook::Health MouseHook::health() const
{
    Health h;
    h.reinstalls = m_reinstalls.load();
    h.misses = m_misses.load();
    h.events = m_events.load();
    h.injected = m_injected.load();
    h.responsive = m_responsive.load();
    h.seenX = m_seenX.load();
    h.seenY = m_seenY.load();
    return h;
}

void MouseHook::install()
{
    // Hook-thread only. Installing twice would leak the first handle, so the
    // caller passes through here for both the first install and a repair.
    if (m_hookHandle.load())
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(m_hookHandle.load()));
    const HHOOK handle = SetWindowsHookEx(WH_MOUSE_LL, hookProcStatic,
                                          GetModuleHandle(nullptr), 0);
    m_hookHandle.store(reinterpret_cast<long long>(handle));
    m_installed.store(handle != nullptr);
    if (handle) {
        // A fresh hook reports from a position the watchdog has not seen yet.
        POINT start {};
        if (GetCursorPos(&start)) {
            m_seenX.store(start.x);
            m_seenY.store(start.y);
        }
        m_responsive.store(true);
    }
}

void MouseHook::reinstall()
{
    install();
    if (m_installed.load())
        m_responsive.store(true);
}

void MouseHook::run()
{
    m_threadId = GetCurrentThreadId();
    g_hook = this;

    install();
    if (!m_installed.load()) {
        // installed() stays false: main.cpp logs the failure and the keyword
        // path keeps working without gestures.
        m_running.store(false);
        g_hook = nullptr;
        m_threadId = 0;
        return;
    }

    MSG msg;
    while (m_running.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_SETTINGS) {
            std::lock_guard lock(m_pendingMutex);
            if (m_pendingValid) {
                m_settings = m_pendingSettings;
                m_pendingValid = false;
                if (m_engine)
                    m_engine->setSettings(m_settings);
            }
        } else if (msg.message == WM_APP_REINSTALL) {
            reinstall();
        }
    }

    if (const HHOOK handle = reinterpret_cast<HHOOK>(m_hookHandle.exchange(0)))
        UnhookWindowsHookEx(handle);
    m_installed.store(false);
    m_running.store(false);
    g_hook = nullptr;
    m_threadId = 0;
}

void MouseHook::watch()
{
    // Independent of the hook thread on purpose: the failure it detects is
    // precisely the hook thread going quiet.
    int consecutive = 0;
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kWatchdogMs));
        if (!m_running.load())
            break;
        if (!m_installed.load() || !m_enabled.load()) {
            consecutive = 0;
            continue;
        }
        POINT actual {};
        if (!GetCursorPos(&actual)) {
            consecutive = 0;
            continue;
        }
        const long long seenX = m_seenX.load();
        const long long seenY = m_seenY.load();
        const bool moved = actual.x != seenX || actual.y != seenY;
        if (!moved) {
            // The pointer is exactly where the hook last put it: quiet is
            // indistinguishable from a dropped hook, so reset the streak.
            consecutive = 0;
            m_responsive.store(true);
            continue;
        }
        ++consecutive;
        if (consecutive < kMissesToReinstall)
            continue;
        consecutive = 0;
        ++m_misses;
        m_responsive.store(false);
        // Ask the hook thread to reinstall. It is its own message loop, so a
        // posted message is honoured between events; the hook handle is owned
        // by that thread and swapped there.
        ++m_reinstalls;
        if (m_threadId)
            PostThreadMessage(m_threadId, WM_APP_REINSTALL, 0, 0);
        // Remember the current position: the repaired hook must not be
        // judged against the position it was rebuilt at.
        m_seenX.store(actual.x);
        m_seenY.store(actual.y);
    }
}

LRESULT CALLBACK MouseHook::hookProcStatic(int code, WPARAM wp, LPARAM lp)
{
    if (g_hook)
        return g_hook->hookProc(code, wp, lp);
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT MouseHook::hookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code < 0 || !m_enabled.load())
        return CallNextHookEx(nullptr, code, wp, lp);
    // HC_ACTION is 0; anything else carries no useful hook data.
    if (code != HC_ACTION || wp != WM_MOUSEMOVE)
        return CallNextHookEx(nullptr, code, wp, lp);

    const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
    // Every delivered event refreshes the watchdog's idea of "alive", even
    // when it is programmatic: a live hook is a live hook.
    m_seenX.store(info->pt.x);
    m_seenY.store(info->pt.y);
    ++m_events;
    // Never wrap injected moves: a programmatic SetCursorPos would otherwise
    // cascade into another wrap decision.
    if (info->flags & LLMHF_INJECTED) {
        ++m_injected;
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    if (!m_engine)
        return CallNextHookEx(nullptr, code, wp, lp);

    const bool modifier = activationKeyHeld(m_settings.activation);
    const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    // QueryPerformanceCounter, not MSLLHOOKSTRUCT.time: that field is
    // millisecond-resolution and wraps, and the impact gate measures speeds
    // over a 100 ms window.
    LARGE_INTEGER now {};
    QueryPerformanceCounter(&now);
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f {};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    // counterToUs splits the division: the naive `now * 1'000'000 / freq`
    // overflows int64 after ~10.7 days of uptime and turns every timestamp
    // negative, which made the impact gate refuse every wrap.
    const long long tUs = counterToUs(now.QuadPart, freq.QuadPart);
    // MSLLHOOKSTRUCT.time is when the DRIVER produced the event; tUs is when
    // this callback ran. They differ when events were queued, and the engine
    // needs the larger span so a queue cannot inflate the measured speed.
    const auto [nx, ny] = m_engine->onMouseMove(
        { info->pt.x, info->pt.y, tUs, static_cast<long long>(info->time) }, modifier,
        leftDown);
    if (nx == info->pt.x && ny == info->pt.y)
        return CallNextHookEx(nullptr, code, wp, lp);
    if (SetCursorPos(static_cast<int>(nx), static_cast<int>(ny))) {
        // The landing point is what the hook will report next; recording it
        // now keeps the watchdog from mistaking our own wrap for a dropped
        // hook.
        m_seenX.store(nx);
        m_seenY.store(ny);
        if (m_onWrap)
            m_onWrap(nx, ny);
        return 1; // swallow the original move; the cursor already moved
    }
    ++m_setCursorFailures;
    // SetCursorPos failed: let the original event through rather than eating
    // a move without moving.
    return CallNextHookEx(nullptr, code, wp, lp);
}

} // namespace cw
