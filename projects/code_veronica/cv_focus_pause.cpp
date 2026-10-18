/* Pause when the game window loses focus (the Jogo page's option, on by
 * default; cv_focus_pause.h holds the decision).
 *
 * The frame clock (main.cpp) polls this every pass. It runs on the thread
 * that owns the game window and keeps pumping its messages while paused, so
 * a click back on the game is seen at once. A pause:
 *   - holds the guest clock (ps3_guest_clock_hold): mftb, the system time and
 *     the GCM timestamps stand still, so the engine's real-time frame delta,
 *     the game's timers and the SRT's time measure no gap on resume;
 *   - parks the guest threads that sleep or yield (the 30 fps limiter does
 *     both), and the frame clock stops raising vblank and flip;
 *   - stops cellAudio's rings: the device gets silence, the title's mixer gets
 *     no event, and sound resumes on the sample it stopped on.
 * Nothing is suspended from outside, so no thread stops while holding a lock.
 * A small "Paused" notice sits over the game window (owned by it, never
 * activated, clicks pass through to the game).
 *
 * CV_FOCUS_PAUSE=0/1 overrides the launcher's setting (autonomous tests run
 * with the window in the background). CV_FOCUS_PAUSE_SELFTEST=1 pretends the
 * focus leaves for 4 s every 12 s, to measure a pause without touching the
 * desktop.
 */
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>
#include "cv_focus_pause.h"

extern "C" {
void* rsx_d3d12_window(void);
void  ps3_guest_clock_hold(int on);
void  cellAudio_set_paused(int on);
long long ps3_guest_qpc(void);
unsigned ppu_boot_frames_presented(void);   /* main.cpp */
}

namespace {

std::atomic<int> g_enabled{1};
std::atomic<int> g_held{0};   /* a port window that pauses the game while open (the F6 item menu) */
bool g_applied = false;       /* the pause in effect: focus or held */
SRWLOCK g_label_lock = SRWLOCK_INIT;
wchar_t g_label[64] = L"Paused";
cv_focus::Tracker g_tracker;
ULONGLONG g_paused_at = 0;
long long g_guest_at = 0;     /* guest clock and frames shown when the pause began, */
unsigned g_frames_at = 0;     /* for the log line on resume */
HWND g_notice = nullptr;
RECT g_notice_at = {};

int env_choice(const char* name)
{
    const char* e = std::getenv(name);
    return e && *e ? (*e != '0') : -1;
}

LRESULT CALLBACK notice_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(w, msg, wp, lp);
}

/* The notice as a premultiplied BGRA image: a dark translucent plate with a
 * red edge, a pause sign and the label. GDI draws the text white on black;
 * its brightness is the coverage. */
void paint_notice(HWND game)
{
    UINT dpi = 96;
    if (HMODULE u = GetModuleHandleW(L"user32.dll"))
        if (auto f = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(u, "GetDpiForWindow")))
            if (UINT d = f(game)) dpi = d;
    const float k = dpi / 96.0f;
    wchar_t label[64];
    AcquireSRWLockShared(&g_label_lock); wcscpy_s(label, g_label); ReleaseSRWLockShared(&g_label_lock);

    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    HFONT font = CreateFontW(-int(22 * k), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(mem, font);
    SIZE ts{}; GetTextExtentPoint32W(mem, label, int(wcslen(label)), &ts);
    const int pad = int(16 * k), bar = int(4 * k), icon = int(14 * k), gap = int(12 * k);
    const int w = bar + pad + icon + gap + ts.cx + pad, h = ts.cy + 2 * int(10 * k);

    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib) {
        HGDIOBJ old_bmp = SelectObject(mem, dib);
        uint32_t* px = static_cast<uint32_t*>(bits);
        std::fill(px, px + size_t(w) * h, 0u);
        SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(255, 255, 255));
        const int tx = bar + pad + icon + gap;
        TextOutW(mem, tx, (h - ts.cy) / 2, label, int(wcslen(label)));
        GdiFlush();
        /* the pause sign: two upright bars, the height of a capital */
        const int iy0 = h / 2 - int(8 * k), iy1 = h / 2 + int(8 * k), ix = bar + pad, iw = int(5 * k);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint32_t& p = px[size_t(y) * w + x];
                float c = (p & 255) / 255.0f;                                /* text coverage */
                if (y >= iy0 && y < iy1 && ((x >= ix && x < ix + iw) || (x >= ix + icon - iw && x < ix + icon)))
                    c = 1.0f;
                const bool edge = x < bar;
                const float pa = edge ? 0.95f : 0.78f;                       /* plate */
                const float pr = edge ? 176 : 18, pg = edge ? 24 : 12, pb = edge ? 24 : 12;
                const float a = c + pa * (1 - c);
                const auto ch = [&](float plate) { return uint32_t(c * 255 + (1 - c) * pa * plate + 0.5f); };
                p = uint32_t(a * 255 + 0.5f) << 24 | ch(pr) << 16 | ch(pg) << 8 | ch(pb);
            }
        SIZE size{ w, h }; POINT src{ 0, 0 };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(g_notice, screen, nullptr, &size, mem, &src, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old_bmp); DeleteObject(dib);
    }
    SelectObject(mem, old_font); DeleteObject(font);
    DeleteDC(mem); ReleaseDC(nullptr, screen);
}

/* In the top-right corner of the game's picture, re-placed when the window
 * moves. A borderless or fullscreen window on a monitor that is not 16:9 has
 * black bars at the sides; the corner is the window's either way. */
void place_notice(HWND game)
{
    if (!g_notice || !IsWindowVisible(g_notice)) return;
    RECT c, n; GetClientRect(game, &c); GetWindowRect(g_notice, &n);
    POINT o{ 0, 0 }; ClientToScreen(game, &o);
    const int w = n.right - n.left, margin = (std::max)(12, int(c.bottom / 30));
    const RECT at{ o.x + c.right - w - margin, o.y + margin, 0, 0 };
    if (at.left == g_notice_at.left && at.top == g_notice_at.top) return;
    g_notice_at = at;
    SetWindowPos(g_notice, nullptr, at.left, at.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void show_notice(HWND game, bool on)
{
    if (!on) { if (g_notice) ShowWindow(g_notice, SW_HIDE); return; }
    if (!game) return;
    if (!g_notice) {
        WNDCLASSEXW wc{ sizeof wc };
        wc.lpfnWndProc = notice_proc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"cv_pause_notice";
        RegisterClassExW(&wc);
        /* Owned by the game window: always just above it, never above the
         * window the player switched to. */
        g_notice = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                   L"cv_pause_notice", L"", WS_POPUP, 0, 0, 1, 1, game, nullptr,
                                   wc.hInstance, nullptr);
        if (!g_notice) return;
    }
    paint_notice(game);   /* the language may have changed since the last pause */
    g_notice_at = {};
    ShowWindow(g_notice, SW_SHOWNOACTIVATE);
    place_notice(game);
}

void apply(bool pause, HWND game, bool held)
{
    if (pause) {
        cellAudio_set_paused(1);
        ps3_guest_clock_hold(1);
        g_paused_at = GetTickCount64();
        g_guest_at = ps3_guest_qpc();
        g_frames_at = ppu_boot_frames_presented();
        std::fprintf(stderr, held ? "[pause] held by a port window: paused\n" : "[pause] the game window lost focus: paused\n");
    } else {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        const double guest_ms = double(ps3_guest_qpc() - g_guest_at) * 1000.0 / double(f.QuadPart);
        ps3_guest_clock_hold(0);
        cellAudio_set_paused(0);
        std::fprintf(stderr, "[pause] focus back: resumed after %.1f s (guest clock moved %.1f ms, %u frames shown)\n",
                     (GetTickCount64() - g_paused_at) / 1000.0, guest_ms,
                     ppu_boot_frames_presented() - g_frames_at);
    }
    show_notice(game, pause && !held);   /* the window that holds the pause says so itself */
}

}  // namespace

extern "C" void cv_focus_pause_enable(int on) { g_enabled.store(on ? 1 : 0); }

/* Pause while a port window is open, whatever the option says; the frame
 * clock applies it on its next poll. */
extern "C" void cv_focus_pause_hold(int on) { g_held.store(on ? 1 : 0); }

extern "C" void cv_focus_pause_label(const wchar_t* text)
{
    AcquireSRWLockExclusive(&g_label_lock);
    wcsncpy_s(g_label, text && *text ? text : L"Paused", _TRUNCATE);
    ReleaseSRWLockExclusive(&g_label_lock);
}

extern "C" int cv_focus_pause_poll(void)
{
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 15) return g_applied;   /* the frame clock runs every millisecond */
    last = now;

    static const int forced = env_choice("CV_FOCUS_PAUSE");
    static const bool selftest = env_choice("CV_FOCUS_PAUSE_SELFTEST") == 1;
    static const ULONGLONG t0 = now;
    HWND game = static_cast<HWND>(rsx_d3d12_window());
    cv_focus::Sample s;
    s.game_window = game && IsWindow(game);
    if (s.game_window) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        s.foreground_ours = fg && pid == GetCurrentProcessId();
        s.minimized = IsIconic(game) != 0;
        if (selftest) { s.foreground_ours = (now - t0) % 12000 < 8000; s.minimized = false; }
    }
    const bool enabled = forced >= 0 ? forced != 0 : g_enabled.load() != 0;
    const bool held = g_held.load() != 0;
    const bool paused = g_tracker.update(enabled, s, now) || held;
    if (paused != g_applied) { g_applied = paused; apply(paused, game, held); }
    else if (paused && !held) {   /* the menu closed with the game still out of focus */
        if (g_notice && IsWindowVisible(g_notice)) place_notice(game);
        else show_notice(game, true);
    }
    return paused;
}
