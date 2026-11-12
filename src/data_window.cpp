// data_window.cpp — see data_window.h.
#include "data_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace cw {
namespace {

constexpr wchar_t kClassName[] = L"WinShunCursorWrapData";
constexpr int kTimerId = 0xC0DE;
constexpr int kRefreshMs = 150;
// Log-bucketed histogram: the buckets are the overlay's scale, so the two
// views read the same numbers.
constexpr int kBuckets = 22;
constexpr long long kLogFloor = 500;

std::atomic<DataWindow*> g_window = nullptr;

COLORREF blend(COLORREF a, COLORREF b, int percentB)
{
    const int r = (GetRValue(a) * (100 - percentB) + GetRValue(b) * percentB) / 100;
    const int g = (GetGValue(a) * (100 - percentB) + GetGValue(b) * percentB) / 100;
    const int bl = (GetBValue(a) * (100 - percentB) + GetBValue(b) * percentB) / 100;
    return RGB(static_cast<BYTE>(r), static_cast<BYTE>(g), static_cast<BYTE>(bl));
}

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                        need);
    return out;
}

std::wstring num(long long value)
{
    return std::to_wstring(value);
}

const wchar_t* edgeNameW(EdgeType edge)
{
    switch (edge) {
    case EdgeType::Left: return L"left";
    case EdgeType::Right: return L"right";
    case EdgeType::Top: return L"top";
    case EdgeType::Bottom: return L"bottom";
    }
    return L"?";
}

// What one arrival or skip looks like on its row.
std::wstring verdictText(const Decision& d)
{
    if (d.wrapped)
        return L"wrap";
    std::string reason = skipReasonName(d.reason);
    return widen(reason);
}

} // namespace

void DataWindow::start(Provider provider)
{
    if (m_running.exchange(true))
        return;
    // The previous run may have ended by itself (the X button clears
    // m_running), leaving a joinable thread behind. Assigning over it is
    // std::terminate, so join first.
    if (m_thread.joinable())
        m_thread.join();
    m_provider = std::move(provider);
    m_thread = std::thread([this] { run(); });
}

void DataWindow::stop()
{
    // Join whatever is joinable, never depend on m_running: the window's own
    // WM_CLOSE handler and the CreateWindowEx failure path both clear it, and a
    // joinable std::thread reaches std::terminate in its destructor. That is
    // exactly what a real host logged: closing the window with its X, then
    // "terminate called without an active exception".
    m_running.store(false);
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (m_thread.joinable())
        m_thread.join();
    m_threadId = 0;
    m_hwnd = nullptr;
}

void DataWindow::setColors(const Colors& colors)
{
    std::lock_guard lock(m_colorsMutex);
    m_colors = colors;
}

void DataWindow::run()
{
    g_window.store(this);
    m_threadId = GetCurrentThreadId();

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &DataWindow::wndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr; // the window paints every pixel itself
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    Colors colors;
    {
        std::lock_guard lock(m_colorsMutex);
        colors = m_colors;
    }

    // WS_EX_NOACTIVATE + WS_EX_TOOLWINDOW: the window never takes the caret
    // and never shows in the taskbar or Alt+Tab. It is still closable.
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect { 0, 0, kWidth, kHeight };
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    // Bottom-right of the primary work area, clear of the taskbar.
    RECT work {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.right - w - 24;
    const int y = work.bottom - h - 24;
    m_hwnd = CreateWindowExW(exStyle, kClassName, L"Cursor wrap — data",
                             style, x < work.left ? work.left : x,
                             y < work.top ? work.top : y, w, h, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) {
        m_running.store(false);
        g_window.store(nullptr);
        m_threadId = 0;
        return;
    }

    m_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_bold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_mono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(m_hwnd, kTimerId, kRefreshMs, nullptr);

    MSG msg;
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (m_hwnd) {
        KillTimer(m_hwnd, kTimerId);
        DestroyWindow(m_hwnd);
    }
    for (HFONT* font : { &m_font, &m_bold, &m_mono }) {
        if (*font) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
    m_hwnd = nullptr;
    g_window.store(nullptr);
}

LRESULT CALLBACK DataWindow::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DataWindow* self = g_window.load();
    if (self)
        return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT DataWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1; // paint() fills everything: no flicker
    case WM_PAINT:
        paint();
        return 0;
    case WM_CLOSE:
        // Closing hides the window. The window thread then stops and the
        // plugin's poll sees the request and clears the setting, so the two
        // surfaces (settings page and search bar) both say "off".
        ShowWindow(hwnd, SW_HIDE);
        KillTimer(hwnd, kTimerId);
        m_closeRequested.store(true);
        m_running.store(false);
        return 0;
    case WM_DESTROY:
        m_closeRequested.store(true);
        m_running.store(false);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int DataWindow::bucketize(const std::vector<Arrival>& arrivals, int bucketCount, int* buckets)
{
    for (int i = 0; i < bucketCount; ++i)
        buckets[i] = 0;
    if (arrivals.empty() || bucketCount <= 0)
        return 0;
    // Log scale so slow drifts and fast hits are both legible; the top of the
    // scale is 20 000 px/s, well past the 20 000 gate maximum.
    constexpr double logCeil = 20000.0;
    int peak = 0;
    for (const Arrival& a : arrivals) {
        const double speed = a.measuredPps <= 0 ? static_cast<double>(kLogFloor)
                                                : static_cast<double>(a.measuredPps);
        const double clamped = std::max(static_cast<double>(kLogFloor),
                                        std::min(speed, logCeil));
        const double t = (std::log(clamped) - std::log(static_cast<double>(kLogFloor)))
            / (std::log(logCeil) - std::log(static_cast<double>(kLogFloor)));
        int index = static_cast<int>(t * (bucketCount - 1) + 0.5);
        index = std::max(0, std::min(bucketCount - 1, index));
        ++buckets[index];
        peak = std::max(peak, buckets[index]);
    }
    return peak;
}

void DataWindow::paintHistogram(HDC dc, int x, int y, int w, int h,
                                const DataSnapshot& snap)
{
    Colors colors;
    {
        std::lock_guard lock(m_colorsMutex);
        colors = m_colors;
    }
    const COLORREF bar = blend(colors.background, colors.accent, 55);
    const COLORREF hot = RGB(0x30, 0xc0, 0x60);
    const COLORREF gateColor = RGB(0xe0, 0xa0, 0x20);

    int buckets[kBuckets] = {};
    const int peak = bucketize(snap.arrivals, kBuckets, buckets);
    if (peak <= 0) {
        SetTextColor(dc, blend(colors.text, colors.background, 45));
        SelectObject(dc, m_font);
        RECT r { x, y + h / 2 - 8, x + w, y + h / 2 + 8 };
        DrawTextW(dc, L"no arrivals yet — push a few edges", -1, &r,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    // Gate lines: current gate and the suggested one, so the two clusters can
    // be read against them.
    const long long suggested = suggestGate(snap.arrivals);
    const auto logPos = [&](long long pps) -> int {
        constexpr double logCeil = 20000.0;
        const double clamped = std::max(static_cast<double>(kLogFloor),
                                        std::min(static_cast<double>(pps), logCeil));
        const double t = (std::log(clamped) - std::log(static_cast<double>(kLogFloor)))
            / (std::log(logCeil) - std::log(static_cast<double>(kLogFloor)));
        return x + static_cast<int>(t * (w - 1));
    };

    const int slot = w / kBuckets;
    for (int i = 0; i < kBuckets; ++i) {
        if (buckets[i] <= 0)
            continue;
        const int height = std::max(2, buckets[i] * (h - 4) / peak);
        RECT r { x + i * slot, y + h - height, x + i * slot + slot - 2, y + h };
        HBRUSH brush = CreateSolidBrush(i == kBuckets - 1 ? hot : bar);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    }
    const auto gateLine = [&](long long pps, COLORREF color, int offset) {
        if (pps <= 0)
            return;
        int gx = logPos(pps) + offset;
        if (gx < x || gx >= x + w)
            return;
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, gx, y, nullptr);
        LineTo(dc, gx, y + h);
        SelectObject(dc, old);
        DeleteObject(pen);
    };
    if (snap.settings.impactGate)
        gateLine(snap.settings.impactSpeedPps, gateColor, 0);
    gateLine(suggested, hot, 1);
}

void DataWindow::paint()
{
    PAINTSTRUCT ps {};
    HDC dc = BeginPaint(m_hwnd, &ps);
    if (!dc)
        return;

    RECT client {};
    GetClientRect(m_hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;

    Colors colors;
    {
        std::lock_guard lock(m_colorsMutex);
        colors = m_colors;
    }
    const COLORREF panel = colors.dark ? blend(colors.background, RGB(255, 255, 255), 6)
                                       : blend(colors.background, RGB(0, 0, 0), 4);
    const COLORREF muted = blend(colors.text, colors.background, 45);
    const COLORREF ok = RGB(0x30, 0xc0, 0x60);
    const COLORREF warn = RGB(0xe0, 0xa0, 0x20);
    const COLORREF bad = RGB(0xe0, 0x50, 0x50);

    // Whole background first: WM_ERASEBKGND is suppressed, so a single fill
    // avoids flicker between the panels.
    HBRUSH bg = CreateSolidBrush(colors.background);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    const DataSnapshot snap = m_provider ? m_provider() : DataSnapshot {};

    int y = 12;
    const int left = 14;
    const int right = width - 14;

    // --- header -----------------------------------------------------------
    {
        SelectObject(dc, m_bold);
        SetTextColor(dc, colors.text);
        RECT r { left, y, right, y + 20 };
        DrawTextW(dc, L"Cursor wrap — live data", -1, &r, DT_LEFT | DT_SINGLELINE);
        y += 22;

        SelectObject(dc, m_font);
        std::wstring state = snap.enabled ? L"enabled" : L"disabled";
        state += L"  ·  hook ";
        state += !snap.hookOk ? L"FAILED" : (snap.hookResponsive ? L"live" : L"repaired");
        state += L"  ·  " + num(snap.monitors) + L" monitor(s), "
            + num(snap.outerEdges) + L" outer edge(s)";
        SetTextColor(dc, !snap.hookOk ? bad : (snap.hookResponsive ? ok : warn));
        RECT r2 { left, y, right, y + 18 };
        DrawTextW(dc, state.c_str(), -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += 20;
    }

    // --- the pointer and the last verdict ---------------------------------
    {
        RECT box { left, y, right, y + 58 };
        HBRUSH brush = CreateSolidBrush(panel);
        FillRect(dc, &box, brush);
        DeleteObject(brush);

        SelectObject(dc, m_mono);
        SetTextColor(dc, colors.text);
        if (snap.hasPointer) {
            std::wstring line = L"pointer  " + num(snap.pointer.x) + L", "
                + num(snap.pointer.y);
            RECT r { left + 8, y + 5, right - 8, y + 23 };
            DrawTextW(dc, line.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE);
        }
        // The most recent decision is the answer to "why did that not wrap?".
        std::wstring last = L"last     ";
        if (!snap.decisions.empty()) {
            const Decision& d = snap.decisions.back();
            last += verdictText(d);
            if (d.onEdge) {
                last += L" on ";
                last += edgeNameW(d.edge);
                if (d.measuredPps > 0) {
                    last += L" @ " + num(d.measuredPps) + L" px/s";
                    if (d.gatePps > 0)
                        last += L" (gate " + num(d.gatePps) + L")";
                }
            }
        } else {
            last += L"no moves seen yet";
        }
        RECT r2 { left + 8, y + 24, right - 8, y + 42 };
        SetTextColor(dc, muted);
        DrawTextW(dc, last.c_str(), -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        std::wstring counts = L"moves " + num(snap.stats.moves) + L"   wraps "
            + num(snap.stats.wraps) + L"   skipped " + num(snap.stats.suppressed);
        RECT r3 { left + 8, y + 41, right - 8, y + 57 };
        DrawTextW(dc, counts.c_str(), -1, &r3, DT_LEFT | DT_SINGLELINE);
        y += 64;
    }

    // --- the histogram ----------------------------------------------------
    {
        SelectObject(dc, m_font);
        SetTextColor(dc, colors.text);
        const long long suggested = suggestGate(snap.arrivals);
        std::wstring title = L"arrival speeds (" + num(static_cast<long long>(snap.arrivals.size()))
            + L" hits)";
        if (suggested > 0)
            title += L"   suggested gate " + num(suggested) + L" px/s";
        RECT r { left, y, right, y + 18 };
        DrawTextW(dc, title.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE);
        y += 20;
        const int graphH = 84;
        RECT box { left, y, right, y + graphH };
        HBRUSH brush = CreateSolidBrush(panel);
        FillRect(dc, &box, brush);
        DeleteObject(brush);
        paintHistogram(dc, left + 6, y + 4, right - left - 12, graphH - 8, snap);
        y += graphH + 8;
        SelectObject(dc, m_mono);
        SetTextColor(dc, muted);
        RECT axis { left, y, right, y + 16 };
        DrawTextW(dc, L"500        2k        5k        10k       20k", -1, &axis,
                  DT_LEFT | DT_SINGLELINE);
        y += 20;
    }

    // --- per-reason counters ---------------------------------------------
    {
        SelectObject(dc, m_font);
        SetTextColor(dc, colors.text);
        std::wstring line = L"skipped by:";
        bool any = false;
        for (int i = 1; i < kSkipReasonCount; ++i) {
            const long long n = snap.stats.skipped[i];
            if (n <= 0)
                continue;
            any = true;
            line += L"  ";
            line += widen(skipReasonName(static_cast<SkipReason>(i)));
            line += L" " + num(n);
        }
        if (!any)
            line += L"  nothing yet";
        RECT r { left, y, right, y + 18 };
        SetTextColor(dc, any ? colors.text : muted);
        DrawTextW(dc, line.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += 22;
    }

    // --- the decision log -------------------------------------------------
    {
        SelectObject(dc, m_bold);
        SetTextColor(dc, colors.text);
        RECT r { left, y, right, y + 18 };
        DrawTextW(dc, L"recent decisions (newest first)", -1, &r, DT_LEFT | DT_SINGLELINE);
        y += 20;

        RECT box { left, y, right, height - 12 };
        HBRUSH brush = CreateSolidBrush(panel);
        FillRect(dc, &box, brush);
        DeleteObject(brush);

        SelectObject(dc, m_mono);
        const int rowH = 16;
        int rowY = y + 4;
        const int maxRows = (height - 12 - rowY) / rowH;
        int drawn = 0;
        for (auto it = snap.decisions.rbegin();
             it != snap.decisions.rend() && drawn < maxRows; ++it, ++drawn) {
            const Decision& d = *it;
            std::wstring text = edgeNameW(d.edge);
            text += L"  ";
            text += d.measuredPps > 0 ? num(d.measuredPps) + L" px/s" : L"     —";
            text += L"  ";
            text += verdictText(d);
            if (d.monitorIndex >= 0)
                text += L"  m" + num(d.monitorIndex);
            SetTextColor(dc, d.wrapped ? ok : muted);
            RECT r2 { left + 8, rowY, right - 8, rowY + rowH };
            DrawTextW(dc, text.c_str(), -1, &r2,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            rowY += rowH;
        }
        if (drawn == 0) {
            SetTextColor(dc, muted);
            RECT r2 { left + 8, rowY, right - 8, rowY + rowH };
            DrawTextW(dc, L"nothing recorded yet", -1, &r2, DT_LEFT | DT_SINGLELINE);
        }
    }

    EndPaint(m_hwnd, &ps);
}

} // namespace cw
