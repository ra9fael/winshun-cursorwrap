// overlay.cpp — see overlay.h.
//
// One thread owns all overlay windows and a 30 ms repaint timer. Each
// repaint polls the engine's last verdict and current settings (cheap,
// locked copies), draws into a per-pixel-alpha 32bpp DIB and hands it to
// UpdateLayeredWindow. The windows are click-through, non-activating tool
// windows: they never take focus or input.
#include "overlay.h"

#include <cmath>
#include <cstring>

namespace cw {
namespace {

constexpr UINT kRepaintTimerId = 1;
constexpr UINT kRepaintMs = 30;

const wchar_t kClassName[] = L"WinShunCursorWrapOverlay";

std::atomic<Overlay*> g_overlay = nullptr; // one instance per process

struct Rgba {
    BYTE b, g, r, a;
};

// A 32bpp top-down straight-alpha surface.
struct Surface {
    int w = 0;
    int h = 0;
    std::vector<Rgba> px;

    Surface(int width, int height)
        : w(width), h(height), px(static_cast<size_t>(width) * static_cast<size_t>(height))
    {
    }

    void blend(int x, int y, COLORREF c, BYTE alpha)
    {
        if (x < 0 || y < 0 || x >= w || y >= h || alpha == 0)
            return;
        Rgba& p = px[static_cast<size_t>(y) * static_cast<size_t>(w)
                     + static_cast<size_t>(x)];
        const BYTE sr = GetRValue(c);
        const BYTE sg = GetGValue(c);
        const BYTE sb = GetBValue(c);
        const BYTE inv = static_cast<BYTE>(255 - alpha);
        p.r = static_cast<BYTE>((sr * alpha + p.r * inv) / 255);
        p.g = static_cast<BYTE>((sg * alpha + p.g * inv) / 255);
        p.b = static_cast<BYTE>((sb * alpha + p.b * inv) / 255);
        if (alpha > p.a)
            p.a = alpha;
    }

    void rect(int x0, int y0, int x1, int y1, COLORREF c, BYTE alpha)
    {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                blend(x, y, c, alpha);
    }

    void circleOutline(int cx, int cy, int r, COLORREF c, BYTE alpha)
    {
        for (int a = 0; a < 360; a += 2) {
            const double rad = a * 3.14159265358979323846 / 180.0;
            const int px = cx + static_cast<int>(r * std::cos(rad));
            const int py = cy + static_cast<int>(r * std::sin(rad));
            for (int t = -1; t <= 1; ++t)
                for (int u = -1; u <= 1; ++u)
                    blend(px + t, py + u, c, alpha);
        }
    }
};

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
    case WM_PAINT:
        return 0; // all painting is UpdateLayeredWindow on the timer
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

    const int w = static_cast<int>(box.width());
    const int h = static_cast<int>(box.height());
    Surface s(w, h);

    // 1. The impact measuring band along every edge (gate on only).
    if (settings.impactGate) {
        const int band = settings.impactBandPx;
        const COLORREF bandColor = colors.accent;
        s.rect(0, 0, band, h, bandColor, 55);          // left
        s.rect(w - band, 0, w, h, bandColor, 55);      // right
        s.rect(0, 0, w, band, bandColor, 55);          // top
        s.rect(0, h - band, w, h, bandColor, 55);      // bottom
    }

    // 2. The guard circle around the last wrap's landing point.
    const bool verdictOnThisMonitor = verdict.monitorIndex >= 0
        && static_cast<size_t>(verdict.monitorIndex) < m_monitors.size()
        && m_monitors[static_cast<size_t>(verdict.monitorIndex)].left == box.left
        && m_monitors[static_cast<size_t>(verdict.monitorIndex)].top == box.top;
    if (verdict.wrapped && verdict.tUs > 0 && verdictOnThisMonitor) {
        const int cx = static_cast<int>(verdict.destX - box.left);
        const int cy = static_cast<int>(verdict.destY - box.top);
        s.circleOutline(cx, cy, settings.wrapGuardPx, RGB(0xe0, 0xa0, 0x20), 170);
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
            // pps/100 keeps the number short (e.g. 2143/2000 -> 21/20).
            label = std::to_wstring(verdict.measuredPps / 100) + L"/"
                + std::to_wstring(verdict.thresholdPps / 100);
            state = RGB(0xe0, 0xa0, 0x20);
        }
    }
    if (!label.empty()) {
        // Draw the label with GDI into a scratch DIB keyed on (1,1,2), then
        // copy the non-key pixels onto the surface as opaque text.
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bitsPtr = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP bmp = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bitsPtr, nullptr, 0);
        if (bmp && bitsPtr) {
            HDC mem = CreateCompatibleDC(screen);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            HBRUSH key = CreateSolidBrush(RGB(1, 1, 2));
            RECT full { 0, 0, w, h };
            FillRect(mem, &full, key);
            DeleteObject(key);
            HFONT font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(mem, font);
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, state);
            SIZE sz {};
            GetTextExtentPoint32W(mem, label.c_str(), static_cast<int>(label.size()), &sz);
            int tx = w / 2 - sz.cx / 2;
            int ty = h / 2 - sz.cy / 2;
            switch (verdict.edge) {
            case EdgeType::Left: tx = 8; ty = h / 2 - sz.cy / 2; break;
            case EdgeType::Right: tx = w - sz.cx - 8; ty = h / 2 - sz.cy / 2; break;
            case EdgeType::Top: tx = w / 2 - sz.cx / 2; ty = 6; break;
            case EdgeType::Bottom: tx = w / 2 - sz.cx / 2; ty = h - sz.cy - 6; break;
            }
            TextOutW(mem, tx, ty, label.c_str(), static_cast<int>(label.size()));
            SelectObject(mem, oldFont);
            DeleteObject(font);
            SelectObject(mem, oldBmp);
            // Blit: every pixel not near the key colour is text.
            const Rgba* src = reinterpret_cast<const Rgba*>(bitsPtr);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const Rgba& p = src[static_cast<size_t>(y) * static_cast<size_t>(w)
                                        + static_cast<size_t>(x)];
                    if (p.r <= 0x10 && p.g <= 0x10 && p.b >= 0xf0)
                        continue; // the key
                    s.blend(x, y, RGB(p.r, p.g, p.b), 255);
                }
            }
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        ReleaseDC(nullptr, screen);
    }

    // Upload: pre-multiply for UpdateLayeredWindow's AC_SRC_ALPHA.
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bitsPtr = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bitsPtr, nullptr, 0);
    if (bmp && bitsPtr) {
        Rgba* dst = reinterpret_cast<Rgba*>(bitsPtr);
        for (size_t i = 0; i < s.px.size(); ++i) {
            const Rgba& src = s.px[i];
            Rgba d;
            d.r = static_cast<BYTE>(src.r * src.a / 255);
            d.g = static_cast<BYTE>(src.g * src.a / 255);
            d.b = static_cast<BYTE>(src.b * src.a / 255);
            d.a = src.a;
            dst[i] = d;
        }
        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        POINT zero { 0, 0 };
        SIZE size { w, h };
        BLENDFUNCTION blend { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(hwnd, screen, nullptr, &size, mem, &zero, 0, &blend, ULW_ALPHA);
        SelectObject(mem, oldBmp);
        DeleteDC(mem);
    }
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
}

} // namespace cw
