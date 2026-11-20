/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment 2 : Seam Carving
 *  File : seamcarve_gui.cpp     Interactive Windows app  (bonus 7a + 7b)
 *
 *  - Starts empty: open any PNG/JPEG/BMP (the dialog opens in tests\, which holds the
 *    team's test photos test1-3.jpeg)
 *  - Pick the algorithm (DP backward/forward energy, greedy, graph cut Dinic/E-K) and the
 *    energy (Sobel / gradient); drag the Width / Height sliders; press Carve
 *  - "Animate" shows every seam in red before it is removed (step-by-step visualisation)
 *  - "Remove 1 vertical / horizontal seam" single-steps the algorithm
 *  - Views: result, all removed seams on the original, energy map, original
 *  - "Live width" precomputes a multi-size map once; the width slider is then real-time
 *  - "Paint protection": paint over objects (left drag; right drag erases) - seams then
 *    go around them; "Auto size" finds the smallest size before seams must cut into a
 *    protected area or the seam cost jumps, and carves to it
 *
 *  Plain Win32 + GDI, no extra libraries.  All carving runs on a worker thread; the UI
 *  thread only draws, so the window stays responsive and Stop works mid-carve.
 *
 *  RUN   seamcarve_gui.exe [image]
 * ===================================================================================== */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "../src/carver.h"
#include "../src/visualize.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace sc;

/* ==================================================================================== *
 *  IDS AND STATE                                                                       *
 * ==================================================================================== */
enum : int {
    ID_OPEN = 101, ID_SAVE, ID_DEMO, ID_RESET,
    ID_ALGO = 110, ID_ENERGY, ID_ORDER,
    ID_WSLIDER = 120, ID_HSLIDER, ID_WLABEL, ID_HLABEL,
    ID_ANIM = 130, ID_SPEED, ID_SPEEDLABEL,
    ID_CARVE = 140, ID_STOP, ID_STEPV, ID_STEPH,
    ID_LIVE = 150, ID_PAINT, ID_CLEARMASK, ID_BRUSH, ID_BRUSHLABEL, ID_AUTO,
    ID_VIEW_RESULT = 160, ID_VIEW_SEAMS, ID_VIEW_ENERGY, ID_VIEW_ORIGINAL,
    ID_STATUS = 170,
};
enum : UINT { WM_APP_FRAME = WM_APP + 1, WM_APP_DONE, WM_APP_PROGRESS, WM_APP_LIVEREADY };
enum class View { Result, Seams, Energy, Original };

struct WorkResult {                         /* filled by the worker, consumed on the UI thread */
    std::string                 error;
    Image                       image;
    std::unique_ptr<SeamCarver> carver;
    Image                       carverBase;
    std::vector<SeamRecord>     extraSeams;
    std::string                 summary;
    int                         autoW = 0, autoH = 0;  /* set by Auto size */
    bool                        isLiveMap = false;
    RemovalMap                  map;
};

static struct App {
    HWND  wnd = nullptr;
    HWND  ctl[200] = {};
    HFONT font = nullptr;
    int   dpi = 96;
    int   panelW = 260;

    Image original, current;                 /* current = what "Result" shows            */
    std::unique_ptr<SeamCarver> carver;      /* state for single steps / continued carving */
    Image carverBase;                        /* image the carver started from             */
    std::vector<SeamRecord> extraSeams;      /* seams inserted when enlarging             */
    View  view = View::Result;

    /* display cache */
    Image shown;
    std::vector<uint8_t> dib;
    bool  dirty = true;

    /* worker */
    std::thread worker;
    std::atomic<bool> busy{false}, cancel{false};
    std::mutex mtx;
    Image frame;                             /* latest animation frame (guarded by mtx)  */
    bool  haveFrame = false;
    std::unique_ptr<WorkResult> pending;     /* guarded by mtx */
    int   progressStep = 0, progressTotal = 0;

    /* live width preview */
    bool       live = false, liveReady = false;

    /* object protection, in ORIGINAL image coordinates */
    ProtectMask mask;
    bool   painting = false;
    int    dragButton = 0, lastPX = -1, lastPY = -1;
    double viewScale = 1.0;
    int    viewX = 0, viewY = 0;                 /* where the image was last drawn (client px) */
    RemovalMap map;
} G;

static int S(int px) { return MulDiv(px, G.dpi, 96); }

static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(0, n - 1)), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(0, n - 1)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static void setText(int id, const std::wstring& t) { SetWindowTextW(G.ctl[id - 100], t.c_str()); }
static HWND ctl(int id) { return G.ctl[id - 100]; }
static int  sliderPos(int id) { return static_cast<int>(SendMessageW(ctl(id), TBM_GETPOS, 0, 0)); }
static bool checked(int id) { return SendMessageW(ctl(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }

/* ==================================================================================== *
 *  OPTIONS FROM THE CONTROLS                                                           *
 * ==================================================================================== */
static CarveOptions currentOptions() {
    CarveOptions o;
    switch (SendMessageW(ctl(ID_ALGO), CB_GETCURSEL, 0, 0)) {
        case 0: o.algo = Algorithm::DP; break;
        case 1: o.algo = Algorithm::DP; o.forwardEnergy = true; break;
        case 2: o.algo = Algorithm::Greedy; break;
        case 3: o.algo = Algorithm::GraphCut; o.flow = MaxFlowAlgo::Dinic; break;
        case 4: o.algo = Algorithm::GraphCut; o.flow = MaxFlowAlgo::EdmondsKarp; break;
    }
    o.energy = SendMessageW(ctl(ID_ENERGY), CB_GETCURSEL, 0, 0) == 1 ? EnergyKind::Gradient : EnergyKind::Sobel;
    LRESULT ord = SendMessageW(ctl(ID_ORDER), CB_GETCURSEL, 0, 0);
    o.order = ord == 1 ? Order::HeightFirst : ord == 2 ? Order::Alternate : Order::WidthFirst;
    o.threads = 1;
    return o;
}

static std::wstring optionLabel(const CarveOptions& o) {
    std::wstring a = o.forwardEnergy ? L"DP forward" : o.algo == Algorithm::DP ? L"DP" : o.algo == Algorithm::Greedy ? L"Greedy"
                   : o.flow == MaxFlowAlgo::Dinic ? L"Graph cut (Dinic)" : L"Graph cut (E-K)";
    return a + L", " + (o.energy == EnergyKind::Sobel ? L"Sobel" : L"gradient");
}

/* ==================================================================================== *
 *  DRAWING                                                                             *
 * ==================================================================================== */
static void markDirty() { G.dirty = true; InvalidateRect(G.wnd, nullptr, FALSE); }

static bool hasMask() {
    return !G.mask.empty() && std::any_of(G.mask.d.begin(), G.mask.d.end(), [](uint8_t v) { return v != 0; });
}

/* Original image with the protected pixels tinted green. */
static Image withMaskOverlay(const Image& img) {
    Image out = img;
    if (G.mask.w != img.w || G.mask.h != img.h) return out;
    for (size_t i = 0; i < G.mask.d.size(); ++i)
        if (G.mask.d[i]) {
            uint8_t* q = &out.px[i * 3];
            q[0] = static_cast<uint8_t>(q[0] * 45 / 100 + 40 * 55 / 100);
            q[1] = static_cast<uint8_t>(q[1] * 45 / 100 + 220 * 55 / 100);
            q[2] = static_cast<uint8_t>(q[2] * 45 / 100 + 90 * 55 / 100);
        }
    return out;
}

static void rebuildShown() {
    bool animating = false;
    {
        std::lock_guard<std::mutex> lk(G.mtx);
        if (G.busy && G.haveFrame) { G.shown = G.frame; animating = true; }
    }
    if (!animating) {
        switch (G.view) {
            case View::Result:   G.shown = G.current; break;
            case View::Original: G.shown = withMaskOverlay(G.original); break;
            case View::Energy:   G.shown = energyToImage(computeEnergy(G.current, currentOptions().energy)); break;
            case View::Seams:
                if (G.carver && !G.carver->history().empty()) G.shown = overlaySeams(G.carverBase, G.carver->history());
                else G.shown = overlaySeams(G.original, G.extraSeams);
                break;
        }
        if (G.painting) G.shown = withMaskOverlay(G.original);      /* paint on the original */
    }
    const Image& im = G.shown;
    size_t stride = (static_cast<size_t>(im.w) * 3 + 3) & ~size_t(3);
    G.dib.assign(stride * im.h, 0);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const uint8_t* p = im.ptr(x, y);
            uint8_t* d = &G.dib[stride * y + x * 3];
            d[0] = p[2]; d[1] = p[1]; d[2] = p[0];
        }
    G.dirty = false;
}

static void paint(HDC hdc) {
    RECT rc; GetClientRect(G.wnd, &rc);
    int cw = rc.right - G.panelW, ch = rc.bottom;
    if (cw <= 0 || ch <= 0) return;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT all = {0, 0, cw, ch};
    HBRUSH bg = CreateSolidBrush(RGB(40, 42, 48));
    FillRect(mem, &all, bg);
    DeleteObject(bg);

    if (G.dirty) rebuildShown();
    if (G.original.empty()) {                       /* nothing open yet: say what to do */
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(200, 200, 210));
        HGDIOBJ of = SelectObject(mem, G.font);
        RECT tr = {0, ch / 2 - S(30), cw, ch / 2 + S(30)};
        DrawTextW(mem, L"No image open.\nClick \"Open image...\" - the test photos are in the tests folder.", -1, &tr,
                  DT_CENTER | DT_WORDBREAK);
        SelectObject(mem, of);
    }
    if (!G.shown.empty() && !G.original.empty()) {
        /* One scale for the original AND the result, so shrinking is visible on screen. */
        int refW = std::max({G.original.w, G.current.w, G.shown.w});
        int refH = std::max({G.original.h, G.current.h, G.shown.h});
        int margin = S(16);
        double sc = std::min({1.0 * (cw - 2 * margin) / refW, 1.0 * (ch - 2 * margin) / refH, 4.0});
        int dx = margin, dy = margin;
        G.viewScale = sc; G.viewX = G.panelW + dx; G.viewY = dy;
        /* dashed outline = original size */
        HPEN pen = CreatePen(PS_DOT, 1, RGB(150, 150, 160));
        HGDIOBJ op = SelectObject(mem, pen);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, dx - 1, dy - 1, dx + static_cast<int>(G.original.w * sc) + 1, dy + static_cast<int>(G.original.h * sc) + 1);
        SelectObject(mem, ob); SelectObject(mem, op); DeleteObject(pen);

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = G.shown.w;
        bi.bmiHeader.biHeight = -G.shown.h;                       /* top-down */
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(mem, HALFTONE);                 /* smooth both when shrinking and when
                                                          enlarging small images for display */
        SetBrushOrgEx(mem, 0, 0, nullptr);
        StretchDIBits(mem, dx, dy, static_cast<int>(G.shown.w * sc), static_cast<int>(G.shown.h * sc),
                      0, 0, G.shown.w, G.shown.h, G.dib.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

        std::wostringstream cap;
        cap << G.shown.w << L" x " << G.shown.h << L"   (original " << G.original.w << L" x " << G.original.h << L")";
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(200, 200, 210));
        HGDIOBJ of = SelectObject(mem, G.font);
        TextOutW(mem, dx, ch - S(22), cap.str().c_str(), static_cast<int>(cap.str().size()));
        SelectObject(mem, of);
    }
    BitBlt(hdc, G.panelW, 0, cw, ch, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ==================================================================================== *
 *  CONTROLS                                                                            *
 * ==================================================================================== */
static void updateSliderLabels() {
    int w = sliderPos(ID_WSLIDER), h = sliderPos(ID_HSLIDER);
    std::wostringstream a, b;
    if (G.original.empty()) { a << L"Width: -"; b << L"Height: -"; }
    else {
        a << L"Width: " << w << L" px  (" << 100 * w / G.original.w << L"%)";
        b << L"Height: " << h << L" px  (" << 100 * h / G.original.h << L"%)";
    }
    setText(ID_WLABEL, a.str());
    setText(ID_HLABEL, b.str());
    std::wostringstream sp; sp << L"Animation delay: " << sliderPos(ID_SPEED) << L" ms / seam";
    setText(ID_SPEEDLABEL, sp.str());
    std::wostringstream br; br << L"Brush " << sliderPos(ID_BRUSH) << L" px";
    setText(ID_BRUSHLABEL, br.str());
}

static void setStatus(const std::wstring& s) { setText(ID_STATUS, s); }

static void enableControls(bool on) {
    const bool have = !G.original.empty();          /* nothing to carve until an image is open */
    EnableWindow(ctl(ID_OPEN), on);
    for (int id : {ID_SAVE, ID_RESET, ID_ALGO, ID_ENERGY, ID_ORDER, ID_CARVE, ID_STEPV, ID_STEPH, ID_LIVE,
                   ID_PAINT, ID_CLEARMASK, ID_AUTO})
        EnableWindow(ctl(id), on && have);
    EnableWindow(ctl(ID_HSLIDER), on && have && !G.live);
    EnableWindow(ctl(ID_WSLIDER), (on && have) || (G.live && G.liveReady));
    EnableWindow(ctl(ID_STOP), !on);
}

static void resetSliders() {
    SendMessageW(ctl(ID_WSLIDER), TBM_SETRANGE, TRUE, MAKELPARAM(1, std::max(2, 2 * G.original.w)));
    SendMessageW(ctl(ID_HSLIDER), TBM_SETRANGE, TRUE, MAKELPARAM(1, std::max(2, 2 * G.original.h)));
    SendMessageW(ctl(ID_WSLIDER), TBM_SETPOS, TRUE, G.original.w);
    SendMessageW(ctl(ID_HSLIDER), TBM_SETPOS, TRUE, G.original.h);
    updateSliderLabels();
}

static void loadNewImage(Image img, const std::wstring& name) {
    G.original = std::move(img);
    G.current = G.original;
    G.mask = ProtectMask(G.original.w, G.original.h, 0);
    G.carver.reset();
    G.extraSeams.clear();
    G.live = false; G.liveReady = false;
    SendMessageW(ctl(ID_LIVE), BM_SETCHECK, BST_UNCHECKED, 0);
    resetSliders();
    enableControls(true);
    std::wostringstream s;
    s << name << L"\r\n" << G.original.w << L" x " << G.original.h << L" pixels.\r\nSet a target size and press Carve.";
    setStatus(s.str());
    markDirty();
}

/* ==================================================================================== *
 *  WORKER THREAD                                                                       *
 * ==================================================================================== */
static void joinWorker() { if (G.worker.joinable()) G.worker.join(); }

static void publishFrame(Image f) {
    { std::lock_guard<std::mutex> lk(G.mtx); G.frame = std::move(f); G.haveFrame = true; }
    PostMessageW(G.wnd, WM_APP_FRAME, 0, 0);
}

static void finish(std::unique_ptr<WorkResult> r) {
    { std::lock_guard<std::mutex> lk(G.mtx); G.pending = std::move(r); }
    PostMessageW(G.wnd, WM_APP_DONE, 0, 0);
}

static void startWork(std::function<void(WorkResult&)> job) {
    joinWorker();
    G.busy = true; G.cancel = false; G.haveFrame = false;
    enableControls(false);
    G.worker = std::thread([job] {
        auto r = std::make_unique<WorkResult>();
        try { job(*r); } catch (const std::exception& ex) { r->error = ex.what(); }
        finish(std::move(r));
    });
}

static StepCallback makeCallback(bool animate, int delayMs) {
    return [animate, delayMs](const Image& cur, const Seam& s, bool v, int step, int total) {
        if (G.cancel) return false;
        if (animate) {
            publishFrame(highlightSeam(cur, s, v));
            if (delayMs > 0) Sleep(static_cast<DWORD>(delayMs));
        } else if (step % 8 == 0) {
            G.progressStep = step; G.progressTotal = total;
            PostMessageW(G.wnd, WM_APP_PROGRESS, 0, 0);
        }
        return true;
    };
}

static void startCarve() {
    const int tw = sliderPos(ID_WSLIDER), th = sliderPos(ID_HSLIDER);
    const CarveOptions opt = currentOptions();
    const bool animate = checked(ID_ANIM);
    const int delay = sliderPos(ID_SPEED);
    const Image orig = G.original;
    const ProtectMask mask = G.mask;
    const bool useMask = hasMask();
    setStatus(L"Carving with " + optionLabel(opt) + L"...");
    startWork([=](WorkResult& r) {
        const ProtectMask* mp = useMask ? &mask : nullptr;
        auto t0 = std::chrono::steady_clock::now();
        StepCallback cb = makeCallback(animate, delay);
        CarveStats st;
        if (tw <= orig.w && th <= orig.h) {
            /* pure shrink: keep the carver so the seam overlay and single steps continue */
            r.carver = std::make_unique<SeamCarver>(orig, opt, mp);
            r.carverBase = orig;
            shrinkTo(*r.carver, tw, th, cb);
            r.image = r.carver->image();
            st = r.carver->stats();
        } else {
            r.image = carveTo(orig, tw, th, opt, cb, &st, &r.extraSeams, mp);
            r.carver = std::make_unique<SeamCarver>(r.image, opt);
            r.carverBase = r.image;
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::ostringstream s;
        s.setf(std::ios::fixed); s.precision(1);
        s << orig.w << " x " << orig.h << "  ->  " << r.image.w << " x " << r.image.h << "\r\n"
          << st.seams << " seams" << (G.cancel ? " (stopped)" : "") << (useMask ? ", protected" : "")
          << ", seam energy " << (st.totalCost >= kProtectPenalty ? std::string("(cut protection)") : std::to_string(st.totalCost)) << "\r\n"
          << "time " << ms << " ms" << (animate ? " (incl. animation)" : "") << "\r\n"
          << "  energy " << st.energyMs << "  find " << st.findMs << "  remove " << st.removeMs << " ms";
        r.summary = s.str();
    });
}

static void startStep(bool vertical) {
    const CarveOptions opt = currentOptions();
    const int delay = std::max(250, sliderPos(ID_SPEED) * 4);
    if (!G.carver) {
        bool atOriginal = G.current.w == G.original.w && G.current.h == G.original.h;
        G.carver = std::make_unique<SeamCarver>(G.current, opt, atOriginal && hasMask() ? &G.mask : nullptr);
        G.carverBase = G.current;
    }
    if ((vertical ? G.carver->width() : G.carver->height()) <= 1) { setStatus(L"Cannot remove more seams in that direction."); return; }
    auto carver = std::make_shared<std::unique_ptr<SeamCarver>>(std::move(G.carver));
    Image base = G.carverBase;
    startWork([=](WorkResult& r) {
        SeamCarver& c = **carver;
        auto t0 = std::chrono::steady_clock::now();
        Seam s = c.findSeam(vertical);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        publishFrame(highlightSeam(c.image(), s, vertical));
        Sleep(static_cast<DWORD>(delay));
        c.removeSeam(vertical, s);
        r.image = c.image();
        r.carver = std::move(*carver);
        r.carverBase = base;
        std::ostringstream o;
        o.setf(std::ios::fixed); o.precision(2);
        o << "Removed 1 " << (vertical ? "vertical" : "horizontal") << " seam\r\nseam energy " << s.cost
          << ", search took " << ms << " ms\r\nnow " << r.image.w << " x " << r.image.h;
        r.summary = o.str();
    });
}

static void startLiveMap() {
    CarveOptions opt = currentOptions();
    if (opt.algo == Algorithm::GraphCut) { opt.algo = Algorithm::DP; opt.forwardEnergy = false; }
    const Image orig = G.original;
    const ProtectMask mask = G.mask;
    const bool useMask = hasMask();
    setStatus(L"Precomputing the multi-size map (every vertical seam, once)...");
    startWork([=](WorkResult& r) {
        r.isLiveMap = true;
        r.map = buildVerticalRemovalMap(orig, orig.w - 1, opt, [](int s, int total) {
            if (G.cancel) return false;
            if (s % 16 == 0) { G.progressStep = s; G.progressTotal = total; PostMessageW(G.wnd, WM_APP_PROGRESS, 0, 0); }
            return true;
        }, useMask ? &mask : nullptr);
        r.summary = "Live width ready: drag the Width slider.\r\nEach position is rendered from the map in O(mn).";
    });
}

/* Auto size: measure seam costs on a copy, then carve (animated) to the size found. */
static void startAuto() {
    const CarveOptions opt = currentOptions();
    const bool animate = checked(ID_ANIM);
    const int delay = sliderPos(ID_SPEED);
    const Image orig = G.original;
    const ProtectMask mask = G.mask;
    const bool useMask = hasMask();
    setStatus(L"Auto size: measuring seam costs...");
    startWork([=](WorkResult& r) {
        const ProtectMask* mp = useMask ? &mask : nullptr;
        auto t0 = std::chrono::steady_clock::now();
        AutoSizeResult a = autoSize(orig, opt, mp);
        r.autoW = a.width; r.autoH = a.height;
        r.carver = std::make_unique<SeamCarver>(orig, opt, mp);
        r.carverBase = orig;
        shrinkTo(*r.carver, a.width, a.height, makeCallback(animate, delay));
        r.image = r.carver->image();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::ostringstream s;
        s.setf(std::ios::fixed); s.precision(0);
        s << "Auto size " << orig.w << " x " << orig.h << " -> " << a.width << " x " << a.height
          << (useMask ? " (protected)" : "") << "\r\n"
          << "width " << 100.0 * a.width / orig.w << "%: " << stopReasonText(a.widthReason) << "\r\n"
          << "height " << 100.0 * a.height / orig.h << "%: " << stopReasonText(a.heightReason) << "\r\n"
          << "time " << ms << " ms";
        r.summary = s.str();
    });
}

static void onWorkDone() {
    joinWorker();
    std::unique_ptr<WorkResult> r;
    { std::lock_guard<std::mutex> lk(G.mtx); r = std::move(G.pending); G.haveFrame = false; }
    G.busy = false;
    if (!r) { enableControls(true); return; }
    if (!r->error.empty()) {
        setStatus(L"Error: " + widen(r->error));
    } else if (r->isLiveMap) {
        if (G.cancel || r->map.steps == 0) { G.live = false; SendMessageW(ctl(ID_LIVE), BM_SETCHECK, BST_UNCHECKED, 0); setStatus(L"Live map cancelled."); }
        else {
            G.map = std::move(r->map);
            G.liveReady = true;
            SendMessageW(ctl(ID_WSLIDER), TBM_SETRANGE, TRUE, MAKELPARAM(G.original.w - G.map.steps, G.original.w + G.map.steps));
            SendMessageW(ctl(ID_WSLIDER), TBM_SETPOS, TRUE, G.original.w);
            G.current = G.original;
            setStatus(widen(r->summary));
        }
    } else {
        G.current = std::move(r->image);
        G.carver = std::move(r->carver);
        G.carverBase = std::move(r->carverBase);
        G.extraSeams = std::move(r->extraSeams);
        if (r->autoW > 0) {                               /* show the size Auto chose */
            SendMessageW(ctl(ID_WSLIDER), TBM_SETPOS, TRUE, r->autoW);
            SendMessageW(ctl(ID_HSLIDER), TBM_SETPOS, TRUE, r->autoH);
        }
        setStatus(widen(r->summary));
    }
    enableControls(true);
    updateSliderLabels();
    markDirty();
}

/* ==================================================================================== *
 *  WINDOW                                                                              *
 * ==================================================================================== */
static HWND makeCtl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                              G.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(G.font), TRUE);
    G.ctl[id - 100] = hw;
    return hw;
}

static void createControls() {
    const int X = 12, W = 236;
    int y = 10;
    makeCtl(L"BUTTON", L"Open image...", BS_PUSHBUTTON, ID_OPEN, X, y, 115, 28);
    makeCtl(L"BUTTON", L"Save result...", BS_PUSHBUTTON, ID_SAVE, X + 121, y, 115, 28); y += 32;
    makeCtl(L"BUTTON", L"Reset to original", BS_PUSHBUTTON, ID_RESET, X, y, W, 28); y += 40;

    makeCtl(L"STATIC", L"Algorithm", 0, 180, X, y, W, 18); y += 18;
    HWND a = makeCtl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_ALGO, X, y, W, 200); y += 30;
    for (const wchar_t* s : {L"Dynamic programming", L"DP + forward energy", L"Greedy (row by row)",
                             L"Graph cut - Dinic", L"Graph cut - Edmonds-Karp"})
        SendMessageW(a, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(a, CB_SETCURSEL, 0, 0);
    makeCtl(L"STATIC", L"Energy", 0, 181, X, y, 110, 18);
    makeCtl(L"STATIC", L"Seam order", 0, 182, X + 121, y, 115, 18); y += 18;
    HWND e = makeCtl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, ID_ENERGY, X, y, 115, 200);
    SendMessageW(e, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Sobel"));
    SendMessageW(e, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gradient"));
    SendMessageW(e, CB_SETCURSEL, 0, 0);
    HWND o = makeCtl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, ID_ORDER, X + 121, y, 115, 200); y += 38;
    for (const wchar_t* s : {L"Width first", L"Height first", L"Alternate"}) SendMessageW(o, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(o, CB_SETCURSEL, 0, 0);

    makeCtl(L"STATIC", L"Width", 0, ID_WLABEL, X, y, W, 18); y += 18;
    makeCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, ID_WSLIDER, X - 4, y, W + 8, 28); y += 32;
    makeCtl(L"STATIC", L"Height", 0, ID_HLABEL, X, y, W, 18); y += 18;
    makeCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, ID_HSLIDER, X - 4, y, W + 8, 28); y += 36;

    makeCtl(L"BUTTON", L"Animate seams (step-by-step)", BS_AUTOCHECKBOX, ID_ANIM, X, y, W, 22); y += 24;
    SendMessageW(ctl(ID_ANIM), BM_SETCHECK, BST_CHECKED, 0);
    makeCtl(L"STATIC", L"", 0, ID_SPEEDLABEL, X, y, W, 18); y += 18;
    makeCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, ID_SPEED, X - 4, y, W + 8, 28); y += 34;
    SendMessageW(ctl(ID_SPEED), TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(ctl(ID_SPEED), TBM_SETPOS, TRUE, 10);

    makeCtl(L"BUTTON", L"Carve", BS_DEFPUSHBUTTON, ID_CARVE, X, y, 115, 32);
    makeCtl(L"BUTTON", L"Stop", BS_PUSHBUTTON, ID_STOP, X + 121, y, 115, 32); y += 36;
    makeCtl(L"BUTTON", L"-1 vertical seam", BS_PUSHBUTTON, ID_STEPV, X, y, 115, 28);
    makeCtl(L"BUTTON", L"-1 horizontal seam", BS_PUSHBUTTON, ID_STEPH, X + 121, y, 115, 28); y += 36;
    makeCtl(L"BUTTON", L"Live width (precompute map)", BS_AUTOCHECKBOX, ID_LIVE, X, y, W, 22); y += 28;
    makeCtl(L"BUTTON", L"Paint protection", BS_AUTOCHECKBOX, ID_PAINT, X, y, 140, 24);
    makeCtl(L"BUTTON", L"Clear mask", BS_PUSHBUTTON, ID_CLEARMASK, X + 146, y, 90, 24); y += 28;
    makeCtl(L"STATIC", L"", 0, ID_BRUSHLABEL, X, y + 4, 70, 18);
    makeCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, ID_BRUSH, X + 66, y, 80, 26);
    SendMessageW(ctl(ID_BRUSH), TBM_SETRANGE, TRUE, MAKELPARAM(2, 60));
    SendMessageW(ctl(ID_BRUSH), TBM_SETPOS, TRUE, 12);
    makeCtl(L"BUTTON", L"Auto size", BS_PUSHBUTTON, ID_AUTO, X + 146, y, 90, 26); y += 34;

    makeCtl(L"STATIC", L"Show", 0, 183, X, y, W, 18); y += 18;
    makeCtl(L"BUTTON", L"Result", BS_AUTORADIOBUTTON | WS_GROUP, ID_VIEW_RESULT, X, y, 115, 22);
    makeCtl(L"BUTTON", L"Removed seams", BS_AUTORADIOBUTTON, ID_VIEW_SEAMS, X + 121, y, 115, 22); y += 22;
    makeCtl(L"BUTTON", L"Energy map", BS_AUTORADIOBUTTON, ID_VIEW_ENERGY, X, y, 115, 22);
    makeCtl(L"BUTTON", L"Original", BS_AUTORADIOBUTTON, ID_VIEW_ORIGINAL, X + 121, y, 115, 22); y += 30;
    SendMessageW(ctl(ID_VIEW_RESULT), BM_SETCHECK, BST_CHECKED, 0);

    makeCtl(L"STATIC", L"", SS_LEFT, ID_STATUS, X, y, W, 110);
}

/* <folder of the exe>\..\tests if it exists (bin\ sits next to tests\), else the exe's folder. */
static std::wstring testsFolder() {
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    for (const std::wstring& cand : {dir + L"\\..\\tests", dir + L"\\tests"}) {
        DWORD a = GetFileAttributesW(cand.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return cand;
    }
    return dir;
}

/* Load a photo for interactive use: very large images are scaled to a 1000-px longest side. */
static Image loadForGui(const std::wstring& path) {
    Image img = loadImage(narrow(path));
    if (static_cast<long long>(img.w) * img.h > 1200LL * 900)
        img = resizeBilinear(img, img.w >= img.h ? 1000 : 1000 * img.w / img.h, img.w >= img.h ? 1000 * img.h / img.w : 1000);
    return img;
}

static void openFile() {
    wchar_t buf[MAX_PATH] = L"";
    std::wstring initial = testsFolder();
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = G.wnd; ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initial.c_str();
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ppm\0All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    try {
        loadNewImage(loadForGui(buf), buf);
    } catch (const std::exception& ex) {
        MessageBoxW(G.wnd, widen(ex.what()).c_str(), L"Cannot open image", MB_ICONERROR);
    }
}

static void saveFile() {
    wchar_t buf[MAX_PATH] = L"carved.png";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = G.wnd; ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PNG\0*.png\0JPEG\0*.jpg\0BMP\0*.bmp\0";
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    try {
        if (G.dirty) rebuildShown();
        saveImage(G.shown, narrow(buf));                 /* saves what is on screen (any view) */
        setStatus(std::wstring(L"Saved ") + buf);
    } catch (const std::exception& ex) {
        MessageBoxW(G.wnd, widen(ex.what()).c_str(), L"Cannot save image", MB_ICONERROR);
    }
}

#define GET_X_LPARAM_(lp) static_cast<int>(static_cast<short>(LOWORD(lp)))
#define GET_Y_LPARAM_(lp) static_cast<int>(static_cast<short>(HIWORD(lp)))

/* Stamp a disc of the brush radius at a client point (and along the stroke since the
 * previous point, so fast drags leave no gaps).  Left button protects, right erases. */
static void paintMask(int cx, int cy) {
    if (G.mask.empty() || G.viewScale <= 0) return;
    const double fx = (cx - G.viewX) / G.viewScale, fy = (cy - G.viewY) / G.viewScale;
    const int r = sliderPos(ID_BRUSH);
    const uint8_t v = G.dragButton == 1 ? 1 : 0;
    auto stamp = [&](double px, double py) {
        for (int y = std::max(0, int(py) - r); y <= std::min(G.mask.h - 1, int(py) + r); ++y)
            for (int x = std::max(0, int(px) - r); x <= std::min(G.mask.w - 1, int(px) + r); ++x)
                if ((x - px) * (x - px) + (y - py) * (y - py) <= double(r) * r) G.mask.at(x, y) = v;
    };
    if (G.lastPX >= 0) {
        double dx = fx - G.lastPX, dy = fy - G.lastPY, len = std::sqrt(dx * dx + dy * dy);
        int n = std::max(1, int(len / std::max(1.0, r / 2.0)));
        for (int i = 1; i <= n; ++i) stamp(G.lastPX + dx * i / n, G.lastPY + dy * i / n);
    } else {
        stamp(fx, fy);
    }
    G.lastPX = int(fx); G.lastPY = int(fy);
    markDirty();
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            G.wnd = hwnd;
            HDC dc = GetDC(hwnd); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(hwnd, dc);
            G.panelW = S(260);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof ncm;
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
            G.font = CreateFontIndirectW(&ncm.lfMessageFont);
            createControls();
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (HIWORD(wp) == CBN_SELCHANGE && id == ID_ENERGY) { markDirty(); return 0; }
            switch (id) {
                case ID_OPEN:  openFile(); break;
                case ID_SAVE:  saveFile(); break;
                case ID_RESET: loadNewImage(G.original, L"Reset to original"); break;
                case ID_CARVE: if (!G.busy) { if (G.live) { G.live = false; G.liveReady = false; SendMessageW(ctl(ID_LIVE), BM_SETCHECK, BST_UNCHECKED, 0); resetSliders(); } startCarve(); } break;
                case ID_STOP:  G.cancel = true; break;
                case ID_STEPV: if (!G.busy) startStep(true); break;
                case ID_STEPH: if (!G.busy) startStep(false); break;
                case ID_PAINT:
                    G.painting = checked(ID_PAINT);
                    if (G.painting) setStatus(L"Paint over what must survive (left drag).\r\nRight drag erases. Then Carve or Auto size.");
                    markDirty();
                    break;
                case ID_CLEARMASK:
                    std::fill(G.mask.d.begin(), G.mask.d.end(), uint8_t(0));
                    markDirty();
                    break;
                case ID_AUTO:
                    if (!G.busy) {
                        if (G.live) { G.live = false; G.liveReady = false; SendMessageW(ctl(ID_LIVE), BM_SETCHECK, BST_UNCHECKED, 0); resetSliders(); }
                        G.painting = false; SendMessageW(ctl(ID_PAINT), BM_SETCHECK, BST_UNCHECKED, 0);
                        startAuto();
                    }
                    break;
                case ID_LIVE:
                    G.live = checked(ID_LIVE);
                    if (G.live) startLiveMap();
                    else { G.liveReady = false; resetSliders(); G.current = G.original; G.carver.reset(); enableControls(true); markDirty(); }
                    break;
                case ID_VIEW_RESULT:   G.view = View::Result;   markDirty(); break;
                case ID_VIEW_SEAMS:    G.view = View::Seams;    markDirty(); break;
                case ID_VIEW_ENERGY:   G.view = View::Energy;   markDirty(); break;
                case ID_VIEW_ORIGINAL: G.view = View::Original; markDirty(); break;
            }
            return 0;
        }
        case WM_HSCROLL:
            updateSliderLabels();
            if (reinterpret_cast<HWND>(lp) == ctl(ID_WSLIDER) && G.live && G.liveReady) {
                G.current = renderWidth(G.original, G.map, sliderPos(ID_WSLIDER));      /* real-time */
                G.carver.reset();
                markDirty();
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            if (G.painting && !G.busy) {
                G.dragButton = msg == WM_LBUTTONDOWN ? 1 : 2;
                G.lastPX = G.lastPY = -1;
                SetCapture(hwnd);
                paintMask(GET_X_LPARAM_(lp), GET_Y_LPARAM_(lp));
            }
            return 0;
        case WM_MOUSEMOVE:
            if (G.dragButton) paintMask(GET_X_LPARAM_(lp), GET_Y_LPARAM_(lp));
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            if (G.dragButton) { G.dragButton = 0; ReleaseCapture(); }
            return 0;
        case WM_APP_FRAME:    markDirty(); return 0;
        case WM_APP_DONE:     onWorkDone(); return 0;
        case WM_APP_PROGRESS: {
            std::wostringstream s; s << L"Working... seam " << G.progressStep << L" / " << G.progressTotal;
            setStatus(s.str());
            return 0;
        }
        case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT panel = {0, 0, G.panelW, 0};
            RECT rc; GetClientRect(hwnd, &rc); panel.bottom = rc.bottom;
            FillRect(hdc, &panel, GetSysColorBrush(COLOR_BTNFACE));
            paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = S(700); mm->ptMinTrackSize.y = S(790);
            return 0;
        }
        case WM_DESTROY:
            G.cancel = true;
            joinWorker();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"SeamCarvingGUI";
    RegisterClassW(&wc);

    HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Seam Carving - CSD3130 Assignment 2 (T07)",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, S(1280), S(840),
                                nullptr, nullptr, inst, nullptr);

    /* open the file given on the command line; otherwise start empty */
    resetSliders();
    enableControls(true);
    setStatus(L"Open an image to start.\r\nTest photos: tests\\test1-3.jpeg");
    std::wstring arg = cmd ? cmd : L"";
    if (!arg.empty() && arg.front() == L'"') arg = arg.substr(1, arg.find(L'"', 1) - 1);
    if (!arg.empty()) {
        try {
            loadNewImage(loadForGui(arg), arg);
        } catch (const std::exception& ex) {
            MessageBoxW(hwnd, widen(ex.what()).c_str(), L"Cannot open image", MB_ICONERROR);
        }
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return 0;
}
