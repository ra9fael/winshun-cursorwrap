// mouse_hook.cpp — see mouse_hook.h.
#include "mouse_hook.h"

#include <thread>

namespace cw {
namespace {

constexpr UINT WM_APP_SETTINGS = WM_APP + 1;

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
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return m_installed.load();
}

void MouseHook::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
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

void MouseHook::run()
{
    m_threadId = GetCurrentThreadId();
    g_hook = this;

    const HHOOK handle = SetWindowsHookEx(WH_MOUSE_LL, hookProcStatic, GetModuleHandle(nullptr), 0);
    if (!handle) {
        // installed() stays false: main.cpp logs the failure and the keyword
        // path keeps working without gestures.
        m_running.store(false);
        g_hook = nullptr;
        m_threadId = 0;
        return;
    }
    m_installed.store(true);

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
        }
    }

    UnhookWindowsHookEx(handle);
    m_installed.store(false);
    m_running.store(false);
    g_hook = nullptr;
    m_threadId = 0;
}

LRESULT CALLBACK MouseHook::hookProcStatic(int code, WPARAM wp, LPARAM lp)
{
    if (g_hook)
        return g_hook->hookProc(code, wp, lp);
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT MouseHook::hookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && wp == WM_MOUSEMOVE && m_enabled.load()) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        // Never wrap injected moves: a programmatic SetCursorPos would
        // otherwise cascade into another wrap decision.
        if (!(info->flags & LLMHF_INJECTED) && m_engine) {
            const bool modifier = activationKeyHeld(m_settings.activation);
            const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            const auto [nx, ny] = m_engine->onMouseMove({ info->pt.x, info->pt.y },
                                                        modifier, leftDown);
            if (nx != info->pt.x || ny != info->pt.y) {
                if (SetCursorPos(static_cast<int>(nx), static_cast<int>(ny))) {
                    if (m_onWrap)
                        m_onWrap(nx, ny);
                    return 1; // swallow the original move; the cursor already moved
                }
                // SetCursorPos failed: fall through and let the original
                // event through rather than eating a move without moving.
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

} // namespace cw
