// overlay.cpp — see overlay.h.
//
// One thread owns all overlay windows and a 30 ms repaint timer. Painting is
// plain GDI through SetLayeredWindowAttributes(LWA_COLORKEY): the windows
// fill with an unlikely key colour, everything drawn over it shows, the key
// stays invisible. That keeps memory flat (no per-frame DIBs — a 4K monitor's
// 32bpp surface would be ~33 MB and the plugin's job caps memory) and the
// repaint is a handful of GDI calls. The windows are click-through,
// non-activating tool windows.
#include "overlay.h"

#include <cmath>

namespace cw {
namespace {

constexpr UINT kRepaintTimerId = 1;
constexpr UINT kRepaintMs = 30;

const wchar_t kClassName[] = L"WinShunCursorWrapOverlay";
// The colour key: an unlikely deep-teal black nobody draws on purpose.
const COLORREF kKey = RGB(0x01, 0x02, 0x03);

std::atomic<Overlay*> g_overlay = nullptr; // one instance per process

void circleOutline(HDC dc, int cx, int cy, int r, COLORREF c)
{
    HGDIOBJ oldPen = SelectObject(dc, CreatePen(PS_SOLID, 2, c));
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, cx - r, cy - r, cx + r, cy + r, 0, 0, 0, 0);
    SelectObject(dc, oldBrush);
    DeleteObject(SelectObject(dc, oldPen));
}

} // namespace

void Overlay::setColors(const Colors& colors)
{
    std::lock_guard lock(m_colorsMutex);
    m_colors = colors;
}

void Overlay::start(const std::vector<MonitorBox>& monitors)
{
    if (m_running.load())
        stop();
    m_monitors = monitors;
    if (m_monitors.empty())
        return;
    m_running.store(true);
    m_thread = std::thread([this] { run(); });
}

void Overlay::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (m_thread.joinable())
        m_thread.join();
    m_threadId = 0;
    m_windows.clear();
}

void Overlay::run()
{
    g_overlay.store(this);
    m_threadId = GetCurrentThreadId();

    WNDCLASSEXW wc { sizeof wc };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Overlay::wndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = nullptr;
    wc.hbrBackground = CreateSolidBrush(kKey);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // stays registered; Windows cleans up at exit

    const DWORD style = WS_POPUP;
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
        | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    for (const MonitorBox& box : m_monitors) {
        HWND hwnd = CreateWindowExW(exStyle, kClassName, L"cursorwrap overlay", style,
                                    static_cast<int>(box.left), static_cast<int>(box.top),
                                    static_cast<int>(box.width()), static_cast<int>(box.height()),
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd)
            continue;
        // Colour-key transparency: kKey pixels are invisible, everything
        // else opaque. No per-pixel alpha, so no per-frame big allocations.
        SetLayeredWindowAttributes(hwnd, kKey, 0, LWA_COLORKEY);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, kRepaintTimerId, kRepaintMs, nullptr);
        m_windows.push_back(hwnd);
    }
    if (m_windows.empty()) {
        g_overlay.store(nullptr);
        return;
    }

    MSG msg;
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (const HWND hwnd : m_windows) {
        KillTimer(hwnd, kRepaintTimerId);
        DestroyWindow(hwnd);
    }
    m_windows.clear();
    g_overlay.store(nullptr);
}

LRESULT CALLBACK Overlay::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Overlay* self = g_overlay.load();
    if (self)
        return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Overlay::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == kRepaintTimerId) {
            for (size_t i = 0; i < m_windows.size() && i < m_monitors.size(); ++i)
                if (m_windows[i] == hwnd)
                    paintWindow(hwnd, m_monitors[i]);
        }
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT; // clicks fall through
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void Overlay::paintWindow(HWND hwnd, const MonitorBox& box)
{
    if (!m_engine)
        return;
    const Settings settings = m_engine->settings();
    const EdgeVerdict verdict = m_engine->lastVerdict();
    Colors colors;
    {
        std::lock_guard lock(m_colorsMutex);
        colors = m_colors;
    }

    HDC dc = GetDC(hwnd);
    const int w = static_cast<int>(box.width());
    const int h = static_cast<int>(box.height());

    // 1. The impact measuring band along every edge (gate on only).
    if (settings.impactGate) {
        const int band = static_cast<int>(settings.impactBandPx);
        HBRUSH b = CreateSolidBrush(colors.accent);
        RECT r;
        r = { 0, 0, band, h };        FillRect(dc, &r, b); // left
        r = { w - band, 0, w, h };    FillRect(dc, &r, b); // right
        r = { 0, 0, w, band };        FillRect(dc, &r, b); // top
        r = { 0, h - band, w, h };    FillRect(dc, &r, b); // bottom
        DeleteObject(b);
    }

    // 2. The guard circle around the last wrap's landing point.
    const bool verdictOnThisMonitor = verdict.monitorIndex >= 0
        && static_cast<size_t>(verdict.monitorIndex) < m_monitors.size()
        && m_monitors[static_cast<size_t>(verdict.monitorIndex)].left == box.left
        && m_monitors[static_cast<size_t>(verdict.monitorIndex)].top == box.top;
    if (verdict.wrapped && verdict.tUs > 0 && verdictOnThisMonitor) {
        const int cx = static_cast<int>(verdict.destX - box.left);
        const int cy = static_cast<int>(verdict.destY - box.top);
        circleOutline(dc, cx, cy, static_cast<int>(settings.wrapGuardPx),
                      RGB(0xe0, 0xa0, 0x20));
    }

    // 3. The live verdict readout, centred on the edge it came from.
    std::wstring label;
    COLORREF state = colors.text;
    if (verdict.tUs > 0 && verdictOnThisMonitor) {
        if (verdict.gateOff) {
            label = L"edge";
            state = colors.text;
        } else if (verdict.wrapped) {
            label = L"wrap";
            state = RGB(0x30, 0xc0, 0x60);
        } else {
            // pps/100 keeps the number short (2143/2000 -> 21/20).
            label = std::to_wstring(verdict.measuredPps / 100) + L"/"
                + std::to_wstring(verdict.thresholdPps / 100);
            state = RGB(0xe0, 0xa0, 0x20);
        }
    }
    if (!label.empty()) {
        HFONT font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, state);
        SIZE sz {};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &sz);
        int tx = w / 2 - sz.cx / 2;
        int ty = h / 2 - sz.cy / 2;
        switch (verdict.edge) {
        case EdgeType::Left: tx = 8; ty = h / 2 - sz.cy / 2; break;
        case EdgeType::Right: tx = w - sz.cx - 8; ty = h / 2 - sz.cy / 2; break;
        case EdgeType::Top: tx = w / 2 - sz.cx / 2; ty = 6; break;
        case EdgeType::Bottom: tx = w / 2 - sz.cx / 2; ty = h - sz.cy - 6; break;
        }
        TextOutW(dc, tx, ty, label.c_str(), static_cast<int>(label.size()));
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    ReleaseDC(hwnd, dc);
}

} // namespace cw
