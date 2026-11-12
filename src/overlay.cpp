// overlay.cpp — see overlay.h.
//
// What is drawn, and what is deliberately not:
//   * Each OUTER edge gets a strip `impactBandPx` wide (min 8px) along that
//     edge, running the full length of the edge. The strip is a translucent
//     tint with a crisp 2px line on its outer side — this is the "线 / 比较透明
//     的遮罩" the user asked for.
//   * Inner edges (two monitors touching) get nothing: nothing wraps there,
//     and covering them was most of the old "whole screen is tinted" effect.
//   * The middle of every monitor stays completely untouched.
//   * One small floating panel near whichever edge was hit last shows the
//     measured approach speed against the gate; it hides again after a
//     couple of seconds.
//
// Why per-pixel alpha and not a colour key: a colour-keyed window can only be
// fully transparent or fully opaque per pixel, so a translucent band is
// impossible — the old version had to paint an opaque sheet. Per-pixel alpha
// needs UpdateLayeredWindow, which needs a DIB. The trap that cost a crash
// before: allocating a monitor-sized (33 MB on 4K) DIB per frame blew the
// plugin's 64 MB job cap. Here every strip owns ONE small DIB (a 150px band on
// a 2560x1440 panel is ~1.5 MB) that is painted once and presented once; the
// readout panel is 208x74 and only repaints when its numbers change.
#include "overlay.h"

#include "qpc_time.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cw {
namespace {

constexpr wchar_t kClassName[] = L"WinShunCursorWrapTuner";
constexpr UINT_PTR kTimerId = 7;
constexpr UINT kRepaintMs = 60;
constexpr int kMinBandPx = 8;
constexpr int kReadoutW = 196;
constexpr int kReadoutH = 60;
// A band alpha this low reads as a tint over any wallpaper instead of a panel.
constexpr uint8_t kBandAlpha = 24;
constexpr uint8_t kLineAlpha = 235;
constexpr uint8_t kPanelAlpha = 240;
// The readout fades out this long after the last edge arrival.
constexpr long long kReadoutTtlUs = 2'000'000;

// The trace panel answers "which gate is optimal?" — a histogram of the
// recorded arrival speeds with the current gate and the suggested gate drawn
// in. Bottom-right of the primary monitor, so it never sits where the cursor
// works. A separate small window keeps it out of the edge strips.
constexpr int kTraceW = 372;
constexpr int kTraceH = 176;
constexpr int kTraceBuckets = 26;
// Speeds under this are bucketed linearly; above it, logarithmically — the
// interesting range is 500…12000 px/s and a linear axis would waste the width
// on the tail.
constexpr long long kTraceLogFloor = 500;

std::atomic<Overlay*> g_overlay = nullptr;

COLORREF dim(COLORREF c, int percent)
{
    return RGB(GetRValue(c) * percent / 100, GetGValue(c) * percent / 100,
               GetBValue(c) * percent / 100);
}

long long nowUs()
{
    static const long long freq = [] {
        LARGE_INTEGER f {};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 0;
    }();
    if (freq <= 0)
        return 0;
    LARGE_INTEGER c {};
    QueryPerformanceCounter(&c);
    // Splitting the division avoids the int64 overflow the naive
    // `c * 1'000'000 / freq` hits after ~10.7 days of uptime.
    return counterToUs(c.QuadPart, freq);
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
    // stop() joins, but be defensive: assigning over a joinable std::thread is
    // std::terminate, and this method is called again whenever the settings or
    // the layout change.
    if (m_thread.joinable())
        m_thread.join();
    m_monitors = monitors;
    if (m_monitors.empty())
        return;
    m_running.store(true);
    m_thread = std::thread([this] { run(); });
}

void Overlay::stop()
{
    // Join whatever is joinable, never depend on m_running: run() can clear it
    // on its own, and a joinable std::thread reaches std::terminate in its
    // destructor.
    m_running.store(false);
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (m_thread.joinable())
        m_thread.join();
    m_threadId = 0;
}

void Overlay::run()
{
    g_overlay.store(this);
    m_threadId = GetCurrentThreadId();

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &Overlay::wndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // Windows unregisters it at process exit

    Colors colors;
    {
        std::lock_guard lock(m_colorsMutex);
        colors = m_colors;
    }
    const Settings settings = m_engine ? m_engine->settings() : Settings {};
    rebuildStrips(settings, colors);

    // One timer drives the readout; the strips are static once painted.
    HWND anchor = m_strips.empty() ? nullptr : m_strips.front().layer.hwnd;
    if (anchor)
        SetTimer(anchor, kTimerId, kRepaintMs, nullptr);

    MSG msg;
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (Strip& strip : m_strips) {
        if (strip.layer.hwnd) {
            KillTimer(strip.layer.hwnd, kTimerId);
            DestroyWindow(strip.layer.hwnd);
        }
        destroyLayer(strip.layer);
    }
    m_strips.clear();
    if (m_readout.hwnd) {
        DestroyWindow(m_readout.hwnd);
        destroyLayer(m_readout);
    }
    m_readout = {};
    m_readoutShown = false;
    if (m_trace.hwnd) {
        DestroyWindow(m_trace.hwnd);
        destroyLayer(m_trace);
    }
    m_trace = {};
    m_drawnArrivals = 0;
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
        if (wp == kTimerId && m_engine) {
            Colors colors;
            {
                std::lock_guard lock(m_colorsMutex);
                colors = m_colors;
            }
            repaint(m_engine->settings(), colors);
            SetTimer(hwnd, kTimerId, kRepaintMs, nullptr);
        }
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT; // clicks fall straight through
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool Overlay::createLayer(Layer& layer, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (!layer.hwnd) {
        const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
            | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        layer.hwnd = CreateWindowExW(exStyle, kClassName, L"cursorwrap tuner", WS_POPUP,
                                     x, y, w, h, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        if (!layer.hwnd)
            return false;
    }
    layer.x = x;
    layer.y = y;
    layer.w = w;
    layer.h = h;

    // Reuse the surface when the size did not change.
    if (layer.dib && layer.dc && layer.screenDc)
        return true;

    layer.screenDc = GetDC(nullptr);
    layer.dc = CreateCompatibleDC(layer.screenDc);
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    layer.dib = CreateDIBSection(layer.screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!layer.dib || !bits) {
        destroyLayer(layer);
        return false;
    }
    layer.bits = static_cast<uint32_t*>(bits);
    layer.oldBitmap = SelectObject(layer.dc, layer.dib);
    std::memset(layer.bits, 0, static_cast<size_t>(w) * h * 4);
    return true;
}

void Overlay::destroyLayer(Layer& layer)
{
    if (layer.dc) {
        if (layer.oldBitmap)
            SelectObject(layer.dc, layer.oldBitmap);
        DeleteDC(layer.dc);
        layer.dc = nullptr;
    }
    if (layer.dib) {
        DeleteObject(layer.dib);
        layer.dib = nullptr;
    }
    if (layer.screenDc) {
        ReleaseDC(nullptr, layer.screenDc);
        layer.screenDc = nullptr;
    }
    layer.oldBitmap = nullptr;
    layer.bits = nullptr;
    layer.w = layer.h = 0;
    layer.shown = false;
}

void Overlay::present(Layer& layer)
{
    if (!layer.hwnd || !layer.dc || !layer.bits)
        return;
    // GDI left 0x00RRGGBB; the paint step stored the coverage in the top
    // byte. UpdateLayeredWindow wants premultiplied BGRA.
    const size_t count = static_cast<size_t>(layer.w) * layer.h;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t v = layer.bits[i];
        const uint32_t a = v >> 24;
        if (a == 0) {
            layer.bits[i] = 0;
            continue;
        }
        if (a == 255) {
            layer.bits[i] = v | 0xFF000000u;
            continue;
        }
        const uint32_t r = (v >> 16) & 0xFF;
        const uint32_t g = (v >> 8) & 0xFF;
        const uint32_t b = v & 0xFF;
        layer.bits[i] = (a << 24) | (((r * a) / 255) << 16) | (((g * a) / 255) << 8)
            | ((b * a) / 255);
    }
    POINT src { 0, 0 };
    SIZE size { layer.w, layer.h };
    POINT dst { layer.x, layer.y };
    BLENDFUNCTION blend {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(layer.hwnd, layer.screenDc, &dst, &size, layer.dc, &src, 0,
                        &blend, ULW_ALPHA);
    if (!layer.shown) {
        ShowWindow(layer.hwnd, SW_SHOWNOACTIVATE);
        layer.shown = true;
    }
}

void Overlay::setAlpha(const Layer& layer, int x0, int y0, int x1, int y1, uint8_t alpha)
{
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(layer.w, x1);
    y1 = std::min(layer.h, y1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t& v = layer.bits[static_cast<size_t>(y) * layer.w + x];
            v = (v & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
        }
    }
}

void Overlay::rebuildStrips(const Settings& settings, const Colors& colors)
{
    for (Strip& strip : m_strips) {
        if (strip.layer.hwnd) {
            KillTimer(strip.layer.hwnd, kTimerId);
            DestroyWindow(strip.layer.hwnd);
        }
        destroyLayer(strip.layer);
    }
    m_strips.clear();

    if (m_engine) {
        m_monitors = m_engine->monitorsSnapshot();
        m_edges = m_engine->outerEdgesSnapshot();
    }
    m_bandPx = std::max(kMinBandPx, settings.impactBandPx);

    const COLORREF tint = dim(colors.accent, 70);
    for (const MonitorEdge& edge : m_edges) {
        if (edge.monitorIndex < 0
            || static_cast<size_t>(edge.monitorIndex) >= m_monitors.size())
            continue;
        const MonitorBox& box = m_monitors[static_cast<size_t>(edge.monitorIndex)];
        Strip strip;
        strip.monitorIndex = edge.monitorIndex;
        strip.edge = edge.type;

        int x = static_cast<int>(box.left);
        int y = static_cast<int>(box.top);
        int w = static_cast<int>(box.width());
        int h = static_cast<int>(box.height());
        switch (edge.type) {
        case EdgeType::Left: w = m_bandPx; break;
        case EdgeType::Right: x = static_cast<int>(box.right) - m_bandPx; w = m_bandPx; break;
        case EdgeType::Top: h = m_bandPx; break;
        case EdgeType::Bottom: y = static_cast<int>(box.bottom) - m_bandPx; h = m_bandPx; break;
        }
        if (!createLayer(strip.layer, x, y, w, h)) {
            destroyLayer(strip.layer);
            continue;
        }

        // The tint across the whole strip, then the crisp line on the outer
        // side; the alpha ramp is applied after, per region.
        HBRUSH brush = CreateSolidBrush(tint);
        RECT all { 0, 0, w, h };
        FillRect(strip.layer.dc, &all, brush);
        DeleteObject(brush);
        brush = CreateSolidBrush(colors.accent);
        RECT line { 0, 0, w, h };
        switch (edge.type) {
        case EdgeType::Left: line = { 0, 0, 2, h }; break;
        case EdgeType::Right: line = { w - 2, 0, w, h }; break;
        case EdgeType::Top: line = { 0, 0, w, 2 }; break;
        case EdgeType::Bottom: line = { 0, h - 2, w, h }; break;
        }
        FillRect(strip.layer.dc, &line, brush);
        DeleteObject(brush);

        // Translucent tint, opaque line: two ranges instead of per-pixel work.
        setAlpha(strip.layer, 0, 0, w, h, kBandAlpha);
        switch (edge.type) {
        case EdgeType::Left: setAlpha(strip.layer, 0, 0, 2, h, kLineAlpha); break;
        case EdgeType::Right: setAlpha(strip.layer, w - 2, 0, w, h, kLineAlpha); break;
        case EdgeType::Top: setAlpha(strip.layer, 0, 0, w, 2, kLineAlpha); break;
        case EdgeType::Bottom: setAlpha(strip.layer, 0, h - 2, w, h, kLineAlpha); break;
        }
        present(strip.layer);
        m_strips.push_back(std::move(strip));
    }
}

void Overlay::buildReadout(const EdgeVerdict& verdict, const Settings& settings,
                           const Colors& colors)
{
    if (verdict.monitorIndex < 0
        || static_cast<size_t>(verdict.monitorIndex) >= m_monitors.size())
        return;
    const MonitorBox& box = m_monitors[static_cast<size_t>(verdict.monitorIndex)];
    const int w = kReadoutW;
    const int h = kReadoutH;
    const int margin = 20;
    int x = static_cast<int>(verdict.x) + margin;
    int y = static_cast<int>(verdict.y) - h / 2;
    switch (verdict.edge) {
    case EdgeType::Left: x = static_cast<int>(box.left) + m_bandPx + margin; break;
    case EdgeType::Right: x = static_cast<int>(box.right) - m_bandPx - margin - w; break;
    case EdgeType::Top: y = static_cast<int>(box.top) + m_bandPx + margin; break;
    case EdgeType::Bottom: y = static_cast<int>(box.bottom) - m_bandPx - margin - h; break;
    }
    x = std::max<int>(static_cast<int>(box.left) + 4,
                      std::min<int>(x, static_cast<int>(box.right) - w - 4));
    y = std::max<int>(static_cast<int>(box.top) + 4,
                      std::min<int>(y, static_cast<int>(box.bottom) - h - 4));

    if (!createLayer(m_readout, x, y, w, h))
        return;
    Layer& layer = m_readout;
    SetWindowPos(layer.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

    const bool ok = verdict.wrapped;
    const COLORREF state = ok ? RGB(0x30, 0xc0, 0x60) : RGB(0xe0, 0xa0, 0x20);

    HBRUSH bg = CreateSolidBrush(dim(colors.background, 92));
    RECT all { 0, 0, w, h };
    FillRect(layer.dc, &all, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(state);
    RECT edges[4] = { { 0, 0, w, 2 }, { 0, h - 2, w, h }, { 0, 0, 2, h }, { w - 2, 0, w, h } };
    for (const RECT& r : edges)
        FillRect(layer.dc, &r, border);
    DeleteObject(border);

    SetBkMode(layer.dc, TRANSPARENT);
    HFONT small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT big = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    SelectObject(layer.dc, small);
    SetTextColor(layer.dc, colors.text);
    const wchar_t* caption = verdict.gateOff ? L"edge · impact off"
        : verdict.guardBlocked                 ? L"guard · blocked"
                                               : L"approach speed";
    TextOutW(layer.dc, 10, 7, caption, static_cast<int>(wcslen(caption)));

    SelectObject(layer.dc, big);
    SetTextColor(layer.dc, state);
    wchar_t line[96];
    if (verdict.gateOff || verdict.guardBlocked || verdict.thresholdPps <= 0) {
        const wchar_t* word = verdict.wrapped ? L"wrap" : L"—";
        TextOutW(layer.dc, 10, 20, word, static_cast<int>(wcslen(word)));
    } else {
        swprintf(line, 96, L"%lld / %lld px/s", verdict.measuredPps, verdict.thresholdPps);
        TextOutW(layer.dc, 10, 20, line, static_cast<int>(wcslen(line)));
        // The acceleration line: only when the user turned that condition on, so
        // the panel does not show a meaningless 0 for everyone else. A negative
        // value means the hand was braking — the reason it did not wrap.
        if (verdict.thresholdAccelPps2 > 0) {
            SelectObject(layer.dc, small);
            SetTextColor(layer.dc, verdict.accelBlocked ? state : colors.text);
            if (verdict.accelMeasured)
                swprintf(line, 96, L"accel %+lld / %lld px/s²", verdict.measuredAccelPps2,
                         verdict.thresholdAccelPps2);
            else
                swprintf(line, 96, L"accel — / %lld px/s²", verdict.thresholdAccelPps2);
            TextOutW(layer.dc, 10, 46, line, static_cast<int>(wcslen(line)));
        }
    }
    DeleteObject(small);
    DeleteObject(big);

    setAlpha(layer, 0, 0, w, h, kPanelAlpha);
    present(layer);
    (void)settings;
    m_readoutShown = true;
}

void Overlay::hideReadout()
{
    if (m_readout.hwnd && m_readout.shown) {
        ShowWindow(m_readout.hwnd, SW_HIDE);
        m_readout.shown = false;
    }
    m_readoutShown = false;
}

void Overlay::hideTrace()
{
    if (m_trace.hwnd && m_trace.shown) {
        ShowWindow(m_trace.hwnd, SW_HIDE);
        m_trace.shown = false;
    }
    m_drawnArrivals = 0;
    m_drawnSuggest = -1;
}

// Bucket the arrival speeds into `bucketCount` bins spanning
// [kTraceLogFloor, 20000] with a log scale above the floor, and return the
// peak so the caller can scale the bars. Unmeasurable arrivals are skipped:
// they carry no information about where the gate belongs.
int Overlay::bucketize(const std::vector<Arrival>& arrivals, int bucketCount, int* buckets)
{
    for (int i = 0; i < bucketCount; ++i)
        buckets[i] = 0;
    const double lo = std::log(static_cast<double>(kTraceLogFloor));
    const double hi = std::log(20000.0);
    int peak = 0;
    for (const Arrival& a : arrivals) {
        if (a.gateOff || a.measuredPps <= 0)
            continue;
        const double v = static_cast<double>(std::max<long long>(a.measuredPps, kTraceLogFloor));
        int index = static_cast<int>((std::log(v) - lo) / (hi - lo) * bucketCount);
        index = std::max(0, std::min(bucketCount - 1, index));
        ++buckets[index];
        peak = std::max(peak, buckets[index]);
    }
    return peak;
}

// Map a speed onto the histogram's x axis, matching bucketize's scale.
static int traceX(long long pps, int left, int width)
{
    const double lo = std::log(static_cast<double>(kTraceLogFloor));
    const double hi = std::log(20000.0);
    const double v = static_cast<double>(std::max<long long>(pps, kTraceLogFloor));
    double t = (std::log(v) - lo) / (hi - lo);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return left + static_cast<int>(t * width);
}

void Overlay::buildTrace(const std::vector<Arrival>& arrivals, const Settings& settings,
                         const Colors& colors)
{
    if (m_monitors.empty())
        return;
    // Anchor to the primary monitor's bottom-right corner of its WORK AREA, so
    // the taskbar never cuts the panel in half.
    const MonitorBox* primary = &m_monitors.front();
    for (const MonitorBox& m : m_monitors)
        if (m.primary)
            primary = &m;
    RECT work { static_cast<LONG>(primary->left), static_cast<LONG>(primary->top),
                static_cast<LONG>(primary->right), static_cast<LONG>(primary->bottom) };
    POINT probe { static_cast<LONG>(primary->left + 4), static_cast<LONG>(primary->top + 4) };
    MONITORINFO mi {};
    mi.cbSize = sizeof mi;
    if (HMONITOR hm = MonitorFromPoint(probe, MONITOR_DEFAULTTONEAREST);
        hm && GetMonitorInfoW(hm, &mi))
        work = mi.rcWork;
    const int x = static_cast<int>(work.right) - kTraceW - 16;
    const int y = static_cast<int>(work.bottom) - kTraceH - 16;
    if (!createLayer(m_trace, x, y, kTraceW, kTraceH))
        return;
    Layer& layer = m_trace;
    SetWindowPos(layer.hwnd, HWND_TOPMOST, x, y, kTraceW, kTraceH, SWP_NOACTIVATE);

    const COLORREF panel = dim(colors.background, 92);
    HBRUSH bg = CreateSolidBrush(panel);
    RECT all { 0, 0, kTraceW, kTraceH };
    FillRect(layer.dc, &all, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(dim(colors.accent, 80));
    RECT rects[4] = { { 0, 0, kTraceW, 1 }, { 0, kTraceH - 1, kTraceW, kTraceH },
        { 0, 0, 1, kTraceH }, { kTraceW - 1, 0, kTraceW, kTraceH } };
    for (const RECT& r : rects)
        FillRect(layer.dc, &r, border);
    DeleteObject(border);

    HFONT small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT bold = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SetBkMode(layer.dc, TRANSPARENT);

    int buckets[kTraceBuckets] = {};
    const int peak = bucketize(arrivals, kTraceBuckets, buckets);
    // The arrival count, and how many the gate would have accepted.
    int measured = 0;
    int accepted = 0;
    for (const Arrival& a : arrivals) {
        if (a.gateOff || a.measuredPps <= 0)
            continue;
        ++measured;
        if (a.measuredPps >= settings.impactSpeedPps)
            ++accepted;
    }
    const long long suggestion = suggestGate(arrivals);

    // Title line.
    SelectObject(layer.dc, bold);
    SetTextColor(layer.dc, colors.text);
    TextOutW(layer.dc, 10, 7, L"edge arrivals", 13);
    SelectObject(layer.dc, small);
    SetTextColor(layer.dc, colors.text);
    wchar_t head[96];
    swprintf(head, 96, L"%d hits · %d wrap", measured, accepted);
    TextOutW(layer.dc, kTraceW - 10 - 120, 9, head, static_cast<int>(wcslen(head)));

    // Histogram area.
    const int plotL = 10, plotR = kTraceW - 10, plotW = plotR - plotL;
    const int plotT = 30, plotB = kTraceH - 40, plotH = plotB - plotT;
    if (peak > 0) {
        HBRUSH bar = CreateSolidBrush(dim(colors.accent, 85));
        for (int i = 0; i < kTraceBuckets; ++i) {
            if (!buckets[i])
                continue;
            const int bx0 = plotL + plotW * i / kTraceBuckets;
            const int bx1 = plotL + plotW * (i + 1) / kTraceBuckets - 1;
            const int bh = std::max(1, buckets[i] * plotH / peak);
            RECT r { bx0, plotB - bh, bx1, plotB };
            FillRect(layer.dc, &r, bar);
        }
        DeleteObject(bar);
    } else {
        SetTextColor(layer.dc, dim(colors.text, 70));
        const wchar_t* empty = L"push to an edge a few times";
        TextOutW(layer.dc, plotL + 4, plotT + plotH / 2 - 8, empty,
                 static_cast<int>(wcslen(empty)));
    }

    // Baseline.
    HBRUSH axis = CreateSolidBrush(dim(colors.text, 35));
    RECT base { plotL, plotB, plotR, plotB + 1 };
    FillRect(layer.dc, &base, axis);
    DeleteObject(axis);

    // The current gate, as a vertical accent line + label.
    const int gateX = traceX(settings.impactSpeedPps, plotL, plotW);
    HBRUSH gateBrush = CreateSolidBrush(colors.accent);
    RECT gate { gateX, plotT - 2, gateX + 2, plotB };
    FillRect(layer.dc, &gate, gateBrush);
    DeleteObject(gateBrush);
    SetTextColor(layer.dc, colors.text);
    wchar_t gbuf[64];
    swprintf(gbuf, 64, L"gate %lld", settings.impactSpeedPps);
    int gx = gateX + 4;
    if (gx + 78 > plotR)
        gx = gateX - 82;
    TextOutW(layer.dc, gx, plotT - 4, gbuf, static_cast<int>(wcslen(gbuf)));

    // The suggestion, drawn only when the trace actually separates.
    if (suggestion > 0) {
        const int sx = traceX(suggestion, plotL, plotW);
        HBRUSH sugg = CreateSolidBrush(RGB(0x30, 0xc0, 0x60));
        RECT s { sx, plotT - 2, sx + 2, plotB };
        FillRect(layer.dc, &s, sugg);
        DeleteObject(sugg);
        SetTextColor(layer.dc, RGB(0x30, 0xc0, 0x60));
        wchar_t sbuf[64];
        swprintf(sbuf, 64, L"best %lld", suggestion);
        int tx = sx + 4;
        if (tx + 76 > plotR)
            tx = sx - 80;
        TextOutW(layer.dc, tx, plotT + plotH - 18, sbuf, static_cast<int>(wcslen(sbuf)));
    }

    // Axis labels: the bucket edges, sparse enough to stay readable.
    SetTextColor(layer.dc, dim(colors.text, 65));
    const long long marks[] = { 500, 1000, 2000, 4000, 8000, 16000 };
    for (const long long mark : marks) {
        const int mx = traceX(mark, plotL, plotW);
        wchar_t mbuf[32];
        swprintf(mbuf, 32, L"%lld", mark);
        TextOutW(layer.dc, mx - 10, plotB + 3, mbuf, static_cast<int>(wcslen(mbuf)));
    }

    DeleteObject(small);
    DeleteObject(bold);

    setAlpha(layer, 0, 0, kTraceW, kTraceH, kPanelAlpha);
    present(layer);
    m_drawnArrivals = arrivals.size();
    m_drawnSuggest = suggestion;
    m_drawnGate = settings.impactSpeedPps;
}

void Overlay::repaint(const Settings& settings, const Colors& colors)
{
    if (!m_engine)
        return;

    // The band width is a setting, and the monitor layout can change: rebuild
    // the static strips when either moves.
    const int band = std::max(kMinBandPx, settings.impactBandPx);
    const std::vector<MonitorEdge> edges = m_engine->outerEdgesSnapshot();
    bool stale = band != m_bandPx || edges.size() != m_edges.size();
    if (!stale) {
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].monitorIndex != m_edges[i].monitorIndex
                || edges[i].type != m_edges[i].type
                || edges[i].position != m_edges[i].position) {
                stale = true;
                break;
            }
        }
    }
    if (stale) {
        rebuildStrips(settings, colors);
        // rebuildStrips destroys the windows that owned the repaint timer:
        // re-arm it on the new first strip, or the panel freezes forever.
        if (!m_strips.empty())
            SetTimer(m_strips.front().layer.hwnd, kTimerId, kRepaintMs, nullptr);
    }

    // The trace panel changes when the arrivals change, or when a moving part
    // it draws (the gate line) changes.
    const std::vector<Arrival> arrivals = m_engine->arrivals();
    const long long suggestion = suggestGate(arrivals);
    const bool traceStale = !arrivals.empty()
        && (arrivals.size() != m_drawnArrivals || suggestion != m_drawnSuggest
            || settings.impactSpeedPps != m_drawnGate);
    if (traceStale)
        buildTrace(arrivals, settings, colors);
    else if (arrivals.empty())
        hideTrace();

    const EdgeVerdict verdict = m_engine->lastVerdict();
    const bool fresh = verdict.tUs > 0 && nowUs() - verdict.tUs < kReadoutTtlUs;
    if (!fresh) {
        if (m_readoutShown)
            hideReadout();
        return;
    }
    if (verdict.tUs == m_drawnVerdictUs && verdict.measuredPps == m_drawnPps
        && m_readoutShown)
        return;
    m_drawnVerdictUs = verdict.tUs;
    m_drawnPps = verdict.measuredPps;
    buildReadout(verdict, settings, colors);
}

} // namespace cw
