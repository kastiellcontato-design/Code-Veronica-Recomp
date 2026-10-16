/* rsx_perf_overlay.c -- see rsx_perf_overlay.h.
 *
 * Twice a second the numbers are taken and the panel is drawn on the CPU (GDI
 * lettering, an antialiased rounded box) into a small image at the back
 * buffer's own pixels; the image goes to the GPU as runs of equal pixels, the
 * same flat quads the UI batch draws. GPU load comes from the "GPU Engine"
 * performance counters, read on a thread of its own since a read can take
 * tens of milliseconds.
 */
#include "rsx_perf_overlay.h"
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")

enum { OV_FPS = 1, OV_MS = 2, OV_CPU = 4, OV_GPU = 8, OV_RAM = 16, OV_VRAM = 32 };

static unsigned overlay_items(void)
{
    const char* e = getenv("PERF_OVERLAY");
    static const struct { const char* id; unsigned bit; } k[] = {
        { "fps", OV_FPS }, { "ms", OV_MS }, { "cpu", OV_CPU }, { "gpu", OV_GPU }, { "ram", OV_RAM }, { "vram", OV_VRAM } };
    unsigned items = 0;
    while (e && *e) {
        const char* end = strchr(e, ',');
        const size_t n = end ? (size_t)(end - e) : strlen(e);
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
            if (strlen(k[i].id) == n && !_strnicmp(e, k[i].id, n)) items |= k[i].bit;
        e = end ? end + 1 : NULL;
    }
    return items;
}

/* ---- the numbers -------------------------------------------------------- */
static LARGE_INTEGER s_qpf, s_win_start;
static unsigned s_win_frames;

void perf_overlay_frame(void)
{
    if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); QueryPerformanceCounter(&s_win_start); }
    s_win_frames++;
}

/* GPU: the 3D engines' utilization over every process, once a second. */
static volatile LONG s_gpu_pct = -1;
static DWORD WINAPI gpu_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        PDH_HQUERY q = NULL; PDH_HCOUNTER c = NULL;
        if (PdhOpenQueryW(NULL, 0, &q) != ERROR_SUCCESS) { Sleep(5000); continue; }
        if (PdhAddEnglishCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &c) != ERROR_SUCCESS) {
            PdhCloseQuery(q); Sleep(10000); continue;
        }
        PdhCollectQueryData(q);
        /* the instances (one per process and engine) are expanded when the
         * counter is added: start over now and then to see new ones */
        for (int i = 0; i < 10; i++) {
            Sleep(1000);
            if (PdhCollectQueryData(q) != ERROR_SUCCESS) break;
            DWORD size = 0, n = 0;
            if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &n, NULL) != PDH_MORE_DATA) continue;
            PDH_FMT_COUNTERVALUE_ITEM_W* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(size);
            if (!items) continue;
            if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &n, items) == ERROR_SUCCESS) {
                double sum = 0;
                for (DWORD k = 0; k < n; k++)
                    if (items[k].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[k].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
                        sum += items[k].FmtValue.doubleValue;
                InterlockedExchange(&s_gpu_pct, (LONG)(sum > 100 ? 100 : sum + 0.5));
            }
            free(items);
        }
        PdhCloseQuery(q);
    }
}

/* VRAM: this process's use of each hardware adapter's local memory. */
static const GUID k_iid_factory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static const GUID k_iid_adapter3 = { 0x645967a4, 0x1392, 0x4310, { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };
static unsigned long long vram_used(void)
{
    static IDXGIAdapter3* adapters[8];
    static int n = -1;
    if (n < 0) {
        n = 0;
        IDXGIFactory1* f = NULL;
        if (SUCCEEDED(CreateDXGIFactory1(&k_iid_factory1, (void**)&f))) {
            IDXGIAdapter1* a = NULL;
            for (UINT i = 0; n < 8 && f->lpVtbl->EnumAdapters1(f, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
                DXGI_ADAPTER_DESC1 d;
                if (SUCCEEDED(a->lpVtbl->GetDesc1(a, &d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                    if (SUCCEEDED(a->lpVtbl->QueryInterface(a, &k_iid_adapter3, (void**)&adapters[n]))) n++;
                    else adapters[n] = NULL;
                }
                a->lpVtbl->Release(a);
            }
            f->lpVtbl->Release(f);
        }
    }
    unsigned long long used = 0;
    for (int i = 0; i < n; i++) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info;
        if (SUCCEEDED(adapters[i]->lpVtbl->QueryVideoMemoryInfo(adapters[i], 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
            used += info.CurrentUsage;
    }
    return used;
}

/* CPU: the whole system's busy share since the last call. */
static int cpu_pct(void)
{
    static unsigned long long last_idle, last_total;
    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi, &fk, &fu)) return -1;
    const unsigned long long idle = ((unsigned long long)fi.dwHighDateTime << 32) | fi.dwLowDateTime;
    const unsigned long long total = (((unsigned long long)fk.dwHighDateTime << 32) | fk.dwLowDateTime)
                                   + (((unsigned long long)fu.dwHighDateTime << 32) | fu.dwLowDateTime);   /* kernel includes idle */
    const unsigned long long di = idle - last_idle, dt = total - last_total;
    const int first = last_total == 0;
    last_idle = idle; last_total = total;
    if (first || !dt) return -1;
    return (int)((dt - di) * 100 / dt);
}

/* ---- the panel ---------------------------------------------------------- */
typedef struct { float r, g, b, a; } Px;   /* premultiplied */

typedef struct { rsx_ui_quad* q; uint32_t n, cap; } QuadList;
static QuadList s_panel, s_note, s_both;
static void put_quad(QuadList* l, float x, float y, float w, float h, uint32_t argb)
{
    if (l->n == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2 : 2048;
        rsx_ui_quad* q = (rsx_ui_quad*)realloc(l->q, cap * sizeof *q);
        if (!q) return;
        l->q = q; l->cap = cap;
    }
    rsx_ui_quad* q = &l->q[l->n++];
    q->x = x; q->y = y; q->w = w; q->h = h; q->argb = argb; q->corner_a = 0;
}

/* Coverage of a rounded rectangle (0,0)-(w,h), radius r, at pixel (x, y). */
static float rr_cover(float x, float y, float w, float h, float r)
{
    const float px = x + 0.5f, py = y + 0.5f;
    const float cx = px < r ? r : px > w - r ? w - r : px, cy = py < r ? r : py > h - r ? h - r : py;
    const float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
    if (px >= r && px <= w - r) return fminf(1.0f, fmaxf(0.0f, fminf(py, h - py) + 0.5f));
    if (py >= r && py <= h - r) return fminf(1.0f, fmaxf(0.0f, fminf(px, w - px) + 0.5f));
    return fminf(1.0f, fmaxf(0.0f, r - d + 0.5f));
}

typedef struct { wchar_t label[16], value[24]; } Seg;

static uint32_t px_argb(const Px* p)
{
    if (p->a <= 0.002f) return 0;
    const long a = lroundf(p->a * 255), r = lroundf(p->r / p->a), g = lroundf(p->g / p->a), b = lroundf(p->b / p->a);
    return (uint32_t)(a > 255 ? 255 : a) << 24 | (uint32_t)(r > 255 ? 255 : r) << 16
         | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
}

/* An image at (ox, oy) as runs of equal pixels; a run the row above also has
 * extends its quad. */
static void emit_image(QuadList* l, const Px* img, int W, int H, float ox, float oy)
{
    uint32_t* open_prev = (uint32_t*)malloc((size_t)W * sizeof(uint32_t));
    uint32_t* open_cur = (uint32_t*)malloc((size_t)W * sizeof(uint32_t));
    int nprev = 0;
    for (int y = 0; y < H && open_prev && open_cur; y++) {
        int ncur = 0;
        for (int x = 0; x < W; ) {
            const uint32_t argb = px_argb(&img[(size_t)y * W + x]);
            int end = x + 1;
            while (end < W && px_argb(&img[(size_t)y * W + end]) == argb) end++;
            if (argb) {
                uint32_t k = UINT32_MAX;
                for (int o = 0; o < nprev && k == UINT32_MAX; o++) {
                    const rsx_ui_quad* u = &l->q[open_prev[o]];
                    if (u->x == ox + x && u->w == (float)(end - x) && u->argb == argb) k = open_prev[o];
                }
                if (k != UINT32_MAX) l->q[k].h += 1;
                else { put_quad(l, ox + x, oy + y, (float)(end - x), 1, argb); k = l->n - 1; }
                open_cur[ncur++] = k;
            }
            x = end;
        }
        uint32_t* t = open_prev; open_prev = open_cur; open_cur = t; nprev = ncur;
    }
    free(open_prev); free(open_cur);
}

static void build(uint32_t bw, uint32_t bh, unsigned items, const Seg* segs, int nseg)
{
    s_panel.n = 0;
    const float s = bh / 720.0f < 0.75f ? 0.75f : bh / 720.0f;
    const int font_px = (int)lroundf(16 * s), pad = (int)lroundf(12 * s), gap = (int)lroundf(16 * s), lgap = (int)lroundf(5 * s);
    const int H = (int)lroundf(32 * s), margin = (int)lroundf(14 * s);
    (void)items;

    HDC dc = CreateCompatibleDC(NULL);
    HFONT font = CreateFontW(-font_px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    /* where each piece goes */
    int lx[8], vx[8], W = pad;
    for (int i = 0; i < nseg; i++) {
        SIZE sz;
        if (i) W += gap;
        if (segs[i].label[0]) { lx[i] = W; GetTextExtentPoint32W(dc, segs[i].label, (int)wcslen(segs[i].label), &sz); W += sz.cx + lgap; }
        else lx[i] = -1;
        vx[i] = W; GetTextExtentPoint32W(dc, segs[i].value, (int)wcslen(segs[i].value), &sz); W += sz.cx;
    }
    W += pad;

    /* two masks: names (grey) and numbers (white) */
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB } };
    Px* img = (Px*)calloc((size_t)W * H, sizeof(Px));
    for (int pass = 0; pass < 2 && img; pass++) {
        void* bits = NULL;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!bmp) continue;
        HGDIOBJ old_bmp = SelectObject(dc, bmp);
        memset(bits, 0, (size_t)W * H * 4);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
        const int ty = (H - tm.tmHeight) / 2;
        for (int i = 0; i < nseg; i++) {
            if (pass == 0 && lx[i] >= 0) TextOutW(dc, lx[i], ty, segs[i].label, (int)wcslen(segs[i].label));
            if (pass == 1) TextOutW(dc, vx[i], ty, segs[i].value, (int)wcslen(segs[i].value));
        }
        GdiFlush();
        if (pass == 0) {   /* the box first: dark, translucent, a faint rim */
            const float r = 8 * s;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    const float outer = rr_cover((float)x, (float)y, (float)W, (float)H, r);
                    const float inner = rr_cover((float)x - 1, (float)y - 1, (float)W - 2, (float)H - 2, r - 1);
                    const float rim = outer - inner, fill = inner;
                    Px* p = &img[(size_t)y * W + x];
                    const float af = fill * 0.74f, ar = rim * 0.30f;
                    p->r = 16 * af + 255 * ar; p->g = 17 * af + 255 * ar; p->b = 20 * af + 255 * ar; p->a = af + ar;
                }
        }
        const float col[2][3] = { { 175, 179, 188 }, { 255, 255, 255 } };
        const unsigned char* m = (const unsigned char*)bits;
        for (int i = 0; i < W * H; i++) {
            const float t = m[i * 4 + 1] / 255.0f;   /* grey text: any channel is the coverage */
            if (t <= 0) continue;
            Px* p = &img[i];
            p->r = col[pass][0] * t + p->r * (1 - t); p->g = col[pass][1] * t + p->g * (1 - t);
            p->b = col[pass][2] * t + p->b * (1 - t); p->a = t + p->a * (1 - t);
        }
        SelectObject(dc, old_bmp);
        DeleteObject(bmp);
    }
    SelectObject(dc, old_font);
    DeleteObject(font);
    DeleteDC(dc);
    if (!img) return;

    emit_image(&s_panel, img, W, H, (float)bw - margin - W, (float)margin);
    free(img);
}

/* ---- the shader loading note ---------------------------------------------
 * Bottom right, while the shaders saved by earlier sessions load: a spinning
 * ring, "Loading shaders" and the share done; then the ring closes, "Shaders
 * loaded", and the note fades out. Loading usually takes a fraction of a
 * second, so each phase is held long enough to be read. SHADER_PRELOAD_NOTE=0
 * hides it (read every frame: the launcher's switch acts at once);
 * SHADER_NOTE_TEXT="<loading>|<loaded>" (UTF-8) gives the launcher's
 * language. Redrawn every frame while it shows: it moves. */
extern int rsx_shader_preload_state(int* done, int* total, unsigned long long* finished_ms);

typedef struct { wchar_t text[96]; int font_px, h, w; float* a; } TextMask;

/* GDI lettering as coverage, kept until the text or size changes. */
static void text_mask(TextMask* m, const wchar_t* t, int font_px, int h)
{
    if (m->a && m->font_px == font_px && m->h == h && !wcscmp(m->text, t)) return;
    free(m->a); m->a = NULL; m->w = 0;
    wcsncpy(m->text, t, 95); m->text[95] = 0;
    m->font_px = font_px; m->h = h;
    const int len = (int)wcslen(m->text);
    HDC dc = CreateCompatibleDC(NULL);
    HFONT font = CreateFontW(-font_px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc, m->text, len, &sz);
    const int w = sz.cx + 2;
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB } };
    void* bits = NULL;
    HBITMAP bmp = len ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (bmp) {
        HGDIOBJ old_bmp = SelectObject(dc, bmp);
        memset(bits, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
        TextOutW(dc, 1, (h - tm.tmHeight) / 2, m->text, len);
        GdiFlush();
        if ((m->a = (float*)malloc((size_t)w * h * sizeof(float))) != NULL) {
            const unsigned char* px = (const unsigned char*)bits;
            for (int i = 0; i < w * h; i++) m->a[i] = px[i * 4 + 1] / 255.0f;
            m->w = w;
        }
        SelectObject(dc, old_bmp);
        DeleteObject(bmp);
    }
    SelectObject(dc, old_font);
    DeleteObject(font);
    DeleteDC(dc);
}

static void over(Px* p, float r, float g, float b, float a)
{
    p->r = r * a + p->r * (1 - a); p->g = g * a + p->g * (1 - a);
    p->b = b * a + p->b * (1 - a); p->a = a + p->a * (1 - a);
}
static void put_mask(Px* img, int W, int H, const TextMask* m, int x0, float r, float g, float b)
{
    if (!m->a) return;
    for (int y = 0; y < H && y < m->h; y++)
        for (int x = 0; x < m->w && x0 + x < W; x++) {
            const float t = m->a[(size_t)y * m->w + x];
            if (t > 0) over(&img[(size_t)y * W + x0 + x], r, g, b, t);
        }
}

static void note_texts(wchar_t* loading, wchar_t* loaded)
{
    wcscpy(loading, L"Loading shaders"); wcscpy(loaded, L"Shaders loaded");
    const char* e = getenv("SHADER_NOTE_TEXT");
    const char* bar = e ? strchr(e, '|') : NULL;
    if (!bar) return;
    wchar_t a[80], b[80];
    const int na = MultiByteToWideChar(CP_UTF8, 0, e, (int)(bar - e), a, 79);
    const int nb = MultiByteToWideChar(CP_UTF8, 0, bar + 1, (int)strlen(bar + 1), b, 79);
    if (na <= 0 || nb <= 0) return;
    a[na] = 0; b[nb] = 0;
    wcscpy(loading, a); wcscpy(loaded, b);
}

/* One pill, bottom right, raised by `up` pixels: the dark box, an icon, a
 * label and an optional value. Icons: 0 a spinning ring, 1 a closed green
 * ring, 2 a green dot (on), 3 a grey ring (off). Returns its height. */
enum { ICON_SPIN, ICON_DONE, ICON_ON, ICON_OFF };
static int draw_pill(QuadList* l, uint32_t bw, uint32_t bh, int up, int icon, TextMask* ml, const wchar_t* label,
                     TextMask* mv, const wchar_t* value, float alpha, ULONGLONG now)
{
    const float s = bh / 720.0f < 0.75f ? 0.75f : bh / 720.0f;
    const int font_px = (int)lroundf(15 * s), H = (int)lroundf(40 * s), pad = (int)lroundf(14 * s);
    const int d = (int)lroundf(18 * s), gap = (int)lroundf(10 * s), space = (int)lroundf(8 * s);
    const int margin = (int)lroundf(18 * s);
    text_mask(ml, label, font_px, H);
    if (value) text_mask(mv, value, font_px, H);
    const int lx = pad + d + gap, vx = lx + ml->w + space;
    const int W = (value ? vx + mv->w : lx + ml->w) + pad;
    Px* img = (Px*)calloc((size_t)W * H, sizeof(Px));
    if (!img) return H;
    /* the box: dark, translucent, a faint rim (the panel's look) */
    const float rr = 8 * s;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const float outer = rr_cover((float)x, (float)y, (float)W, (float)H, rr);
            const float inner = rr_cover((float)x - 1, (float)y - 1, (float)W - 2, (float)H - 2, rr - 1);
            const float rim = outer - inner, af = inner * 0.78f, ar = rim * 0.30f;
            Px* p = &img[(size_t)y * W + x];
            p->r = 16 * af + 255 * ar; p->g = 17 * af + 255 * ar; p->b = 20 * af + 255 * ar; p->a = af + ar;
        }
    /* the icon */
    const float cx = pad + d * 0.5f, cy = H * 0.5f, th = fmaxf(2.0f, 2.6f * s), R = d * 0.5f - th * 0.5f;
    const float head = (float)fmod((double)now * 0.0069, 6.283185307);
    for (int y = (int)(cy - d); y <= (int)(cy + d); y++)
        for (int x = (int)(cx - d); x <= (int)(cx + d); x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, r = sqrtf(dx * dx + dy * dy);
            Px* p = &img[(size_t)y * W + x];
            if (icon == ICON_ON) {   /* a filled dot */
                const float cov = fminf(1.0f, fmaxf(0.0f, d * 0.36f - r + 0.5f));
                if (cov > 0) over(p, 120, 196, 112, cov);
                continue;
            }
            const float cov = fminf(1.0f, fmaxf(0.0f, th * 0.5f - fabsf(r - R) + 0.5f));
            if (cov <= 0) continue;
            if (icon == ICON_DONE) { over(p, 120, 196, 112, cov); continue; }
            if (icon == ICON_OFF) { over(p, 150, 150, 156, cov); continue; }
            over(p, 255, 255, 255, cov * 0.18f);   /* the spinner: a faint track, a red arc with a fading tail */
            float delta = head - atan2f(dy, dx);
            delta = fmodf(delta, 6.283185307f);
            if (delta < 0) delta += 6.283185307f;
            const float tail = 4.712389f;   /* three quarters of a turn */
            if (delta < tail) over(p, 226, 52, 60, cov * (1.0f - delta / tail));
        }
    put_mask(img, W, H, ml, lx, 214, 217, 224);
    if (value) put_mask(img, W, H, mv, vx, 255, 255, 255);
    if (alpha < 1.0f)
        for (int i = 0; i < W * H; i++) { img[i].r *= alpha; img[i].g *= alpha; img[i].b *= alpha; img[i].a *= alpha; }
    emit_image(l, img, W, H, (float)bw - margin - W, (float)bh - margin - H - up);
    free(img);
    return H + (int)lroundf(8 * s);
}

/* The shader loading note. Returns the height it takes (0 when hidden). */
static int shader_note(uint32_t bw, uint32_t bh, ULONGLONG now)
{
    enum { MIN_LOADING = 1500, LOADED_HOLD = 1600, FADE = 700, FADE_IN = 200 };
    static ULONGLONG t0;
    static int over_for_good;
    static TextMask m_label, m_pct;
    int done = 0, total = 0;
    unsigned long long fin = 0;
    const int st = rsx_shader_preload_state(&done, &total, &fin);
    if (over_for_good || !st || !total) return 0;
    const char* e = getenv("SHADER_PRELOAD_NOTE");
    if (e && e[0] == '0') return 0;
    if (!t0) t0 = now;
    float alpha = fminf(1.0f, (float)(now - t0) / FADE_IN);
    int loaded = 0;
    if (st == 2) {
        const ULONGLONG end = fin > t0 + MIN_LOADING ? fin : t0 + MIN_LOADING;
        if (now >= end) {
            const ULONGLONG since = now - end;
            if (since >= LOADED_HOLD + FADE) { over_for_good = 1; return 0; }
            loaded = 1;
            if (since > LOADED_HOLD) alpha = fminf(alpha, 1.0f - (float)(since - LOADED_HOLD) / FADE);
        }
    }
    /* The count climbs over the time the loading phase is held, never ahead of
     * the real progress: done in 0.1 s, it would otherwise sit on 100%. */
    int pct = st == 2 ? 100 : (int)((long long)done * 100 / total);
    { const int shown = (int)((now - t0) * 100 / MIN_LOADING); if (shown < pct) pct = shown; }
    wchar_t loading_t[80], loaded_t[80], pct_t[16];
    note_texts(loading_t, loaded_t);
    swprintf(pct_t, 16, L"%d%%", pct);
    return draw_pill(&s_note, bw, bh, 0, loaded ? ICON_DONE : ICON_SPIN, &m_label, loaded ? loaded_t : loading_t,
                     &m_pct, loaded ? NULL : pct_t, alpha, now);
}

/* A short notice (perf_overlay_toast): above the shader note when both show. */
static SRWLOCK s_toast_lock = SRWLOCK_INIT;
static wchar_t s_toast_text[96];
static int s_toast_on;
static ULONGLONG s_toast_t0;
void perf_overlay_toast(const wchar_t* text, int on)
{
    AcquireSRWLockExclusive(&s_toast_lock);
    wcsncpy(s_toast_text, text ? text : L"", 95); s_toast_text[95] = 0;
    s_toast_on = on; s_toast_t0 = GetTickCount64();
    ReleaseSRWLockExclusive(&s_toast_lock);
}
static void toast_note(uint32_t bw, uint32_t bh, int up, ULONGLONG now)
{
    enum { FADE_IN = 150, HOLD = 1800, FADE = 500 };
    static TextMask m_label;
    wchar_t text[96]; int on; ULONGLONG t0;
    AcquireSRWLockShared(&s_toast_lock);
    wcscpy(text, s_toast_text); on = s_toast_on; t0 = s_toast_t0;
    ReleaseSRWLockShared(&s_toast_lock);
    if (!t0 || !text[0] || now - t0 >= FADE_IN + HOLD + FADE) return;
    const ULONGLONG t = now - t0;
    float alpha = fminf(1.0f, (float)t / FADE_IN);
    if (t > FADE_IN + HOLD) alpha = 1.0f - (float)(t - FADE_IN - HOLD) / FADE;
    draw_pill(&s_note, bw, bh, up, on ? ICON_ON : ICON_OFF, &m_label, text, NULL, NULL, alpha, now);
}

static const rsx_ui_quad* note_quads(uint32_t bw, uint32_t bh, uint32_t* count)
{
    const ULONGLONG now = GetTickCount64();
    s_note.n = 0;
    const int up = shader_note(bw, bh, now);
    toast_note(bw, bh, up, now);
    *count = s_note.n;
    return s_note.n ? s_note.q : NULL;
}

const rsx_ui_quad* perf_overlay_quads(uint32_t w, uint32_t h, uint32_t* count)
{
    static unsigned last_items;
    static uint32_t last_w, last_h;
    static ULONGLONG last_build;
    static HANDLE gpu;
    *count = 0;
    uint32_t nn = 0;
    const rsx_ui_quad* nq = note_quads(w, h, &nn);
    const unsigned items = overlay_items();
    if (!items || !s_qpf.QuadPart) { last_items = 0; *count = nn; return nq; }
    if ((items & OV_GPU) && !gpu) gpu = CreateThread(NULL, 0, gpu_thread, NULL, 0, NULL);
    const ULONGLONG now = GetTickCount64();
    if (items != last_items || w != last_w || h != last_h || now - last_build >= 500) {
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        const double secs = (double)(t.QuadPart - s_win_start.QuadPart) / (double)s_qpf.QuadPart;
        static double fps, ms;
        if (s_win_frames && secs > 0.2) {
            fps = s_win_frames / secs; ms = secs * 1000.0 / s_win_frames;
            s_win_frames = 0; s_win_start = t;
        }
        static int cpu = -1;
        const int c = cpu_pct();
        if (c >= 0) cpu = c;
        Seg segs[6]; int n = 0;
        memset(segs, 0, sizeof segs);
        if (items & OV_FPS) { swprintf(segs[n++].value, 24, L"%.0f FPS", fps); }
        if (items & OV_MS)  { swprintf(segs[n++].value, 24, L"%.1f ms", ms); }
        if (items & OV_CPU) { wcscpy(segs[n].label, L"CPU"); if (cpu >= 0) swprintf(segs[n].value, 24, L"%d%%", cpu); else wcscpy(segs[n].value, L"-"); n++; }
        if (items & OV_GPU) { const LONG g = s_gpu_pct; wcscpy(segs[n].label, L"GPU");
                              if (g >= 0) swprintf(segs[n].value, 24, L"%ld%%", g); else wcscpy(segs[n].value, L"-"); n++; }
        if (items & OV_RAM) { PROCESS_MEMORY_COUNTERS pmc; wcscpy(segs[n].label, L"RAM");
                              if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc))
                                  swprintf(segs[n].value, 24, L"%.2f GB", pmc.WorkingSetSize / 1073741824.0);
                              else wcscpy(segs[n].value, L"-");
                              n++; }
        if (items & OV_VRAM) { wcscpy(segs[n].label, L"VRAM"); swprintf(segs[n].value, 24, L"%.2f GB", vram_used() / 1073741824.0); n++; }
        build(w, h, items, segs, n);
        last_items = items; last_w = w; last_h = h; last_build = now;
    }
    if (!nn) { *count = s_panel.n; return s_panel.q; }
    /* both: the panel, then the note */
    s_both.n = 0;
    for (uint32_t i = 0; i < s_panel.n; i++) { const rsx_ui_quad* q = &s_panel.q[i]; put_quad(&s_both, q->x, q->y, q->w, q->h, q->argb); }
    for (uint32_t i = 0; i < nn; i++) { const rsx_ui_quad* q = &nq[i]; put_quad(&s_both, q->x, q->y, q->w, q->h, q->argb); }
    *count = s_both.n;
    return s_both.q;
}
