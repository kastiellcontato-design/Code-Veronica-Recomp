/*
 * Button icons for Code Veronica: PlayStation, Xbox or keyboard.
 *
 * Xbox. The PS3 release still carries the Xbox 360 port's art beside its own:
 *   window/button_guide.arc   buifba (Cross) / buifma (Circle)   and buifma_x (A) / buifba_x (B)
 *   adv/screen_setting.arc    Sst_moji01_ps3 ("Use Triangle")    and Sst_moji01_360 ("Use Y")
 *   on_manual/on_manual.arc   On_Manual00p/01p/04p               and On_Manual00x/01x/04x
 * (Page 05 has a 360 version too, but it has no buttons on it and quotes the
 * 360 save size, 160 KB, so the PS3 page stays.) The map atlas has no Xbox
 * version; one is drawn from the user's own files (see "the map screen").
 *
 * Keyboard. No version exists in the game: keycaps are drawn here, light
 * neutral silver with a soft bevel and an open label, showing the key the
 * launcher maps to that PS3 button (PAD_KEYMAP), so they follow the player's
 * own key setup. The How to Play pages are redrawn the same way (see "How to
 * Play in keyboard mode").
 *
 * The 360 manual describes the 360 layout (aim with RT, Status on Start),
 * and the controls follow the icons: see pad_phys_360_layout and the
 * launcher's set_icon_env.
 *
 * Icons are paired by position, the way the pad mapping pairs buttons: Cross
 * and A are the bottom face button, Circle and B the right one. (The archive
 * names pair them by meaning instead, Japanese style, which would be wrong for
 * this European release.)
 *
 * How a screen gets its icons, two ways:
 *  - When an archive loads with Xbox icons wanted, the game is handed a copy
 *    in which each PlayStation entry and its Xbox entry swap names; the PS3
 *    code only ever names the PlayStation entries, so it shows the Xbox art.
 *  - While the game runs, every image is recognised by its bytes as it goes to
 *    the GPU (rsx_tex_hook.h) and the wanted version is written in its place;
 *    a watcher rewrites the ones already seen when the wanted set changes. This
 *    is what makes the switch live, and how the keyboard and map images (which
 *    are not in any archive) reach the screen.
 *
 * Archive layout: a 0x10-byte "SFH" header, then the data in 0x20000-byte
 * blocks of 0x1FFF0 data + a 0x10-byte checksum. The ARC table sits at the
 * start of the first block: "ARC\0", version, count, then 80 bytes per entry:
 * name[64], type, compressed size, size and flags, offset.
 *
 * The checksum is checked on every read (MtFileStream, 0x00792930; a mismatch
 * shows "The game's installation is corrupted."). It is a byte-wise sum of the
 * block in 16 lanes (vaddubm over 16-byte rows, from a seed, then vrlb), so it
 * only sees how many of each byte value sit in each lane (offset mod 16).
 * Entries are 80 bytes apart, a multiple of 16, so every entry's name covers
 * the same lanes: EXCHANGING two names leaves the checksum as it was.
 *
 * It is the names that swap, not the size/offset records: large archives are
 * streamed forwards, entry by entry, and the reader cannot go back. Swapped
 * records point an early entry past a later one's data and the load never
 * completes (How to Play stayed black). With swapped names every entry still
 * reads its own data in file order; the game finds the art by name. The file
 * keeps its size.
 *
 * CV_BUTTON_ICONS = auto (default), ps, xbox or keyboard. Auto follows the
 * device used last (pad_last_input): keyboard keys, an Xbox controller's icons,
 * or PlayStation's; before any input, the connected controller's (keyboard
 * when there is none).
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "../../libs/filesystem/vfs_overlay.h"
#include "../../libs/filesystem/edat.h"
#include "../../libs/input/pad_phys.h"
#include "../../libs/input/pad_map.h"
#include "cv_version.h"
#include "cv_keycap_style.h"
#include "../../libs/video/rsx_tex_hook.h"
#include "../../libs/video/rsx_texture_pack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <thread>
#include <chrono>

extern "C" uint8_t* vm_base;

namespace {

namespace fs = std::filesystem;

enum { SET_PS = 0, SET_XBOX = 1, SET_KB = 2 };

/* What the keyboard version of an image is built from. */
enum { KB_NONE, KB_ICON32, KB_SCREEN_TEXT, KB_MAP, KB_MANUAL };

struct Swap { const char* file; const char* ps; const char* xbox; int kb; int action; };

const Swap k_swaps[] = {
    { "/window/button_guide.arc",   "buifba_BM_NOMIP",       "buifma_x_BM_NOMIP",     KB_ICON32, PA_CROSS },    /* Cross  -> A */
    { "/window/button_guide.arc",   "buifma_BM_NOMIP",       "buifba_x_BM_NOMIP",     KB_ICON32, PA_CIRCLE },   /* Circle -> B */
    { "/adv/screen_setting.arc",    "Sst_moji01_ps3",        "Sst_moji01_360",        KB_SCREEN_TEXT, PA_TRIANGLE },
    /* manual pages: action 1 marks page 1 (the controller picture) */
    { "/on_manual/on_manual.arc",   "On_Manual00p_BM_NOMIP", "On_Manual00x_BM_NOMIP", KB_MANUAL, 1 },
    { "/on_manual/on_manual.arc",   "On_Manual01p_BM_NOMIP", "On_Manual01x_BM_NOMIP", KB_MANUAL, 0 },
    { "/on_manual/on_manual.arc",   "On_Manual04p_BM_NOMIP", "On_Manual04x_BM_NOMIP", KB_MANUAL, 0 },
};

/* The controller's own set: Xbox for an XInput pad, else PlayStation. */
int pad_set()
{
    const int last = pad_last_input();
    if (last == PAD_KIND_XINPUT) return SET_XBOX;
    if (last != PAD_KIND_NONE && last != PAD_INPUT_KEYBOARD) return SET_PS;
    PadPhys p;
    return pad_phys_poll(0, &p) && p.kind == PAD_KIND_XINPUT ? SET_XBOX : SET_PS;
}

int wanted_set()
{
    const char* m = getenv("CV_BUTTON_ICONS");
    if (m && !strcmp(m, "xbox")) return SET_XBOX;
    if (m && !strcmp(m, "ps")) return SET_PS;
    if (m && !strcmp(m, "keyboard")) return SET_KB;
    const int last = pad_last_input();
    if (last == PAD_INPUT_KEYBOARD) return SET_KB;
    if (last == PAD_KIND_XINPUT) return SET_XBOX;
    if (last != PAD_KIND_NONE) return SET_PS;
    PadPhys p;
    if (!pad_phys_poll(0, &p)) return SET_KB;   /* no controller at all */
    return p.kind == PAD_KIND_XINPUT ? SET_XBOX : SET_PS;
}

bool ends_with(const std::string& s, const char* tail)
{
    size_t n = strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

/* ---- the Xbox archive copy ------------------------------------------- */

/* The ARC entry whose name's last path component is `leaf`, or -1. */
long find_entry(const std::vector<unsigned char>& d, const char* leaf)
{
    if (d.size() < 0x18 || memcmp(&d[0x11], "CRA", 3) != 0) return -1;
    const unsigned n = (unsigned)d[0x16] << 8 | d[0x17];
    for (unsigned i = 0; i < n; i++) {
        const size_t e = 0x18 + (size_t)i * 80;
        if (e + 80 > d.size() || e + 80 > 0x10 + 0x1FFF0) return -1;
        std::string name((const char*)&d[e], strnlen((const char*)&d[e], 64));
        size_t cut = name.find_last_of('\\');
        if (name.compare(cut == std::string::npos ? 0 : cut + 1, std::string::npos, leaf) == 0) return (long)e;
    }
    return -1;
}

std::vector<unsigned char> read_file(const std::string& path)
{
    std::vector<unsigned char> d;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size > 0) { d.resize((size_t)size); if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
    return d;
}

/* Write the Xbox copy of `src` to `dst`. False if the archive is not the
 * expected one (then the original is opened). */
bool build_copy(const std::string& src, const std::string& dst, const std::string& key)
{
    std::vector<unsigned char> d = read_file(src);
    if (d.empty()) return false;
    int done = 0;
    for (const Swap& s : k_swaps) {
        if (!ends_with(key, s.file)) continue;
        long ps = find_entry(d, s.ps), xb = find_entry(d, s.xbox);
        if (ps < 0 || xb < 0) { fprintf(stderr, "[icons] %s: no %s/%s entry, keeping the original\n", src.c_str(), s.ps, s.xbox); return false; }
        /* Exchange the names: see the notes at the top. */
        unsigned char t[64];
        memcpy(t, &d[(size_t)ps], 64);
        memcpy(&d[(size_t)ps], &d[(size_t)xb], 64);
        memcpy(&d[(size_t)xb], t, 64);
        done++;
    }
    if (!done) return false;
    std::error_code ec;
    fs::create_directories(fs::path(dst).parent_path(), ec);
    const std::string tmp = dst + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = (fclose(f) == 0) && ok;
    if (ok) { fs::remove(dst, ec); fs::rename(tmp, dst, ec); ok = !ec; }
    if (!ok) fs::remove(tmp, ec);
    return ok;
}

/* An archive's data without the SFH header and per-block checksums. */
std::vector<unsigned char> unblock(const std::vector<unsigned char>& d)
{
    std::vector<unsigned char> out;
    for (size_t k = 0x10; k < d.size(); k += 0x20000) {
        const size_t n = std::min<size_t>(0x1FFF0, d.size() - k);
        out.insert(out.end(), d.begin() + (long)k, d.begin() + (long)(k + n));
    }
    return out;
}

/* Level-0 image bytes of the texture entry `leaf` (the .tex past its 0x14
 * header), or empty. `s` is an unblocked archive. */
std::vector<unsigned char> tex_bytes(const std::vector<unsigned char>& s, const char* leaf)
{
    if (s.size() < 8 || memcmp(&s[1], "CRA", 3) != 0) return {};
    const unsigned n = (unsigned)s[6] << 8 | s[7];
    for (unsigned i = 0; i < n; i++) {
        const size_t e = 8 + (size_t)i * 80;
        if (e + 80 > s.size()) return {};
        std::string name((const char*)&s[e], strnlen((const char*)&s[e], 64));
        const size_t cut = name.find_last_of('\\');
        if (name.compare(cut == std::string::npos ? 0 : cut + 1, std::string::npos, leaf) != 0) continue;
        auto be = [&](size_t o) { return (uint32_t)s[o] << 24 | (uint32_t)s[o + 1] << 16 | (uint32_t)s[o + 2] << 8 | s[o + 3]; };
        const uint32_t cs = be(e + 68), size = be(e + 72) >> 3, off = be(e + 76);
        if (size < 0x14 || (size_t)off + cs > s.size()) return {};
        std::vector<unsigned char> t(size);
        if (edat_inflate(&s[off], cs, t.data(), t.size()) != (int)size) return {};
        return std::vector<unsigned char>(t.begin() + 0x14, t.end());
    }
    return {};
}

/* ---- pixels -------------------------------------------------------------- */

struct Rgba { float r, g, b, a; };

void unpack565(unsigned v, float c[3])
{
    c[0] = (float)((v >> 11) & 31) * 255.0f / 31.0f;
    c[1] = (float)((v >> 5) & 63) * 255.0f / 63.0f;
    c[2] = (float)(v & 31) * 255.0f / 31.0f;
}

/* DXT5 (BC3), the PC byte order: what the game's textures use. */
std::vector<Rgba> dxt5_decode(const unsigned char* d, int w, int h)
{
    std::vector<Rgba> px((size_t)w * h);
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++) {
            const unsigned char* b = d + ((size_t)by * (w / 4) + bx) * 16;
            float al[8] = { (float)b[0], (float)b[1] };
            for (int i = 2; i < 8; i++)
                al[i] = b[0] > b[1] ? ((8 - i) * al[0] + (i - 1) * al[1]) / 7.0f
                      : i < 6 ? ((6 - i) * al[0] + (i - 1) * al[1]) / 5.0f : (i == 6 ? 0.0f : 255.0f);
            uint64_t ai = 0;
            for (int i = 0; i < 6; i++) ai |= (uint64_t)b[2 + i] << (8 * i);
            float c[4][3];
            unpack565(b[8] | b[9] << 8, c[0]); unpack565(b[10] | b[11] << 8, c[1]);
            for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
            const uint32_t ci = (uint32_t)b[12] | b[13] << 8 | b[14] << 16 | (uint32_t)b[15] << 24;
            for (int i = 0; i < 16; i++) {
                const float* cc = c[(ci >> (2 * i)) & 3];
                px[(size_t)(by * 4 + i / 4) * w + bx * 4 + i % 4] = { cc[0], cc[1], cc[2], al[(ai >> (3 * i)) & 7] };
            }
        }
    return px;
}

unsigned pack565(const float c[3])
{
    auto q = [](float v, int bits) { int m = (1 << bits) - 1; int r = (int)(v * m / 255.0f + 0.5f); return (unsigned)(r < 0 ? 0 : r > m ? m : r); };
    return q(c[0], 5) << 11 | q(c[1], 6) << 5 | q(c[2], 5);
}

/* The colour half of a DXT block (four-colour mode) for the texels marked in
 * `use` (all when none is): endpoints along the texels' principal axis, then
 * refined by least squares on the chosen indices; the bounding box is tried
 * too and the smaller error wins. Small text on a keycap needs this: the box
 * alone smears dark strokes into the light face. */
void encode_colour(const Rgba p[16], const bool use_in[16], unsigned char* out)
{
    bool use[16]; int n = 0;
    for (int i = 0; i < 16; i++) n += (use[i] = use_in[i]) ? 1 : 0;
    if (!n) for (int i = 0; i < 16; i++) { use[i] = true; n++; }
    auto v = [&](int i, int k) { return k == 0 ? p[i].r : k == 1 ? p[i].g : p[i].b; };

    struct Cand { unsigned c0, c1; uint32_t ci; float err; };
    auto evaluate = [&](const float e0[3], const float e1[3]) {
        Cand c{ pack565(e0), pack565(e1), 0, 0 };
        if (c.c0 < c.c1) std::swap(c.c0, c.c1);
        float pal[4][3];
        unpack565(c.c0, pal[0]); unpack565(c.c1, pal[1]);
        for (int k = 0; k < 3; k++) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
        for (int i = 0; i < 16; i++) {
            int best = 0; float bd = 1e30f;
            for (int k = 0; k < (c.c0 == c.c1 ? 1 : 4); k++) {
                float dd = 0;
                for (int ch = 0; ch < 3; ch++) { const float d = pal[k][ch] - v(i, ch); dd += d * d; }
                if (dd < bd) { bd = dd; best = k; }
            }
            c.ci |= (uint32_t)best << (2 * i);
            if (use[i]) c.err += bd;
        }
        return c;
    };

    float mean[3] = { 0, 0, 0 }, lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
    for (int i = 0; i < 16; i++)
        if (use[i])
            for (int k = 0; k < 3; k++) { mean[k] += v(i, k) / n; lo[k] = std::min(lo[k], v(i, k)); hi[k] = std::max(hi[k], v(i, k)); }
    Cand best = evaluate(hi, lo);

    float cov[3][3] = {};
    for (int i = 0; i < 16; i++)
        if (use[i])
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) cov[a][b] += (v(i, a) - mean[a]) * (v(i, b) - mean[b]);
    float axis[3] = { hi[0] - lo[0] + 1e-3f, hi[1] - lo[1] + 1e-3f, hi[2] - lo[2] + 1e-3f };
    for (int it = 0; it < 8; it++) {
        float t[3] = { 0, 0, 0 };
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) t[a] += cov[a][b] * axis[b];
        const float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (len < 1e-6f) break;
        for (int a = 0; a < 3; a++) axis[a] = t[a] / len;
    }
    {
        const float len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        for (int a = 0; a < 3; a++) axis[a] /= len;
    }
    float tmin = 1e30f, tmax = -1e30f;
    for (int i = 0; i < 16; i++)
        if (use[i]) {
            float t = 0;
            for (int k = 0; k < 3; k++) t += (v(i, k) - mean[k]) * axis[k];
            tmin = std::min(tmin, t); tmax = std::max(tmax, t);
        }
    float e0[3], e1[3];
    for (int k = 0; k < 3; k++) { e0[k] = std::clamp(mean[k] + axis[k] * tmax, 0.0f, 255.0f); e1[k] = std::clamp(mean[k] + axis[k] * tmin, 0.0f, 255.0f); }
    for (int it = 0; it < 4; it++) {
        const Cand c = evaluate(e0, e1);
        if (c.err < best.err) best = c;
        /* least squares for the two endpoints given the indices */
        static const float wa[4] = { 1, 0, 2.0f / 3, 1.0f / 3 };
        float aa = 0, ab = 0, bb = 0, ax[3] = { 0, 0, 0 }, bx[3] = { 0, 0, 0 };
        for (int i = 0; i < 16; i++) {
            if (!use[i]) continue;
            const int k = (c.ci >> (2 * i)) & 3;
            const float a = wa[k], b = 1 - a;
            aa += a * a; ab += a * b; bb += b * b;
            for (int ch = 0; ch < 3; ch++) { ax[ch] += a * v(i, ch); bx[ch] += b * v(i, ch); }
        }
        const float det = aa * bb - ab * ab;
        if (std::fabs(det) < 1e-6f) break;
        float c0f[3], c1f[3];
        for (int ch = 0; ch < 3; ch++) {
            c0f[ch] = std::clamp((ax[ch] * bb - bx[ch] * ab) / det, 0.0f, 255.0f);
            c1f[ch] = std::clamp((bx[ch] * aa - ax[ch] * ab) / det, 0.0f, 255.0f);
        }
        for (int ch = 0; ch < 3; ch++) { e0[ch] = c0f[ch]; e1[ch] = c1f[ch]; }
    }
    out[0] = (unsigned char)best.c0; out[1] = (unsigned char)(best.c0 >> 8);
    out[2] = (unsigned char)best.c1; out[3] = (unsigned char)(best.c1 >> 8);
    for (int i = 0; i < 4; i++) out[4 + i] = (unsigned char)(best.ci >> (8 * i));
}

/* DXT5 encoder: a bounding-box alpha half and the colour half above. */
void dxt5_encode_block(const Rgba p[16], unsigned char* b)
{
    float amin = 255, amax = 0;
    for (int i = 0; i < 16; i++) { amin = std::min(amin, p[i].a); amax = std::max(amax, p[i].a); }
    b[0] = (unsigned char)(amax + 0.5f); b[1] = (unsigned char)(amin + 0.5f);
    float al[8] = { (float)b[0], (float)b[1] };
    for (int i = 2; i < 8; i++) al[i] = ((8 - i) * al[0] + (i - 1) * al[1]) / 7.0f;
    uint64_t ai = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0;
        for (int k = 1; k < 8; k++) if (std::fabs(al[k] - p[i].a) < std::fabs(al[best] - p[i].a)) best = k;
        if (b[0] == b[1]) best = 0;
        ai |= (uint64_t)best << (3 * i);
    }
    for (int i = 0; i < 6; i++) b[2 + i] = (unsigned char)(ai >> (8 * i));
    /* colour: fitted to the visible texels */
    bool vis[16];
    for (int i = 0; i < 16; i++) vis[i] = p[i].a >= 16;
    encode_colour(p, vis, b + 8);
}

/* Re-encode the blocks of `img` that differ from `orig`; the rest keep the
 * bytes of `dxt` (all of them when orig is empty). */
std::vector<unsigned char> dxt5_reencode(const std::vector<unsigned char>& dxt, const std::vector<Rgba>& orig,
                                         const std::vector<Rgba>& img, int w, int h)
{
    std::vector<unsigned char> out = dxt;
    out.resize((size_t)w * h);
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++) {
            Rgba blk[16]; bool changed = orig.empty();
            for (int i = 0; i < 16; i++) {
                const size_t k = (size_t)(by * 4 + i / 4) * w + bx * 4 + i % 4;
                blk[i] = img[k];
                changed = changed || memcmp(&img[k], &orig[k], sizeof(Rgba)) != 0;
            }
            if (changed) dxt5_encode_block(blk, &out[((size_t)by * (w / 4) + bx) * 16]);
        }
    return out;
}

/* ---- keycaps --------------------------------------------------------------
 * Shared silver keycaps, optically centered labels and 4x antialiasing. */
void gdiplus_once()
{
    static std::once_flag once;
    std::call_once(once, [] {
        static ULONG_PTR token;
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token, &in, nullptr);
    });
}

const wchar_t* key_font() { return cv_keycap::font(); }

/* The label of a Windows virtual key, short enough for a 28-px cap. */
std::wstring key_label(int vk)
{
    if (vk <= 0) return L"-";   /* an action with no key */
    switch (vk) {
    case 0x20: return L"Space";  case 0x08: return L"BkSp"; case 0x0D: return L"Enter";
    case 0x09: return L"Tab";    case 0x1B: return L"Esc";  case 0x14: return L"Caps";
    case 0x10: case 0xA0: case 0xA1: return L"Shift";
    case 0x11: case 0xA2: case 0xA3: return L"Ctrl";
    case 0x12: case 0xA4: case 0xA5: return L"Alt";
    case 0x26: return L"↑"; case 0x28: return L"↓"; case 0x25: return L"←"; case 0x27: return L"→";
    case 0x2D: return L"Ins";    case 0x2E: return L"Del";  case 0x24: return L"Home"; case 0x23: return L"End";
    case 0x21: return L"PgUp";   case 0x22: return L"PgDn";
    case 0x6A: return L"Num*";   case 0x6B: return L"Num+"; case 0x6D: return L"Num-";
    case 0x6E: return L"Num.";   case 0x6F: return L"Num/";
    case 0x01: return L"LMB";    case 0x02: return L"RMB";  case 0x04: return L"MMB";
    case 0x5B: case 0x5C: return L"Win";  case 0x5D: return L"Menu";
    default: break;
    }
    if (vk >= 0x70 && vk <= 0x87) return L"F" + std::to_wstring(vk - 0x6F);
    if (vk >= 0x60 && vk <= 0x69) return L"Num" + std::to_wstring(vk - 0x60);
    const UINT ch = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_CHAR) & 0x7FFF;
    if (ch) return std::wstring(1, (wchar_t)towupper((wint_t)ch));
    return L"?";
}

/* The keys PAD_KEYMAP gives PS3 action `a` (pad_map.h ids; up to
 * PAD_KEY_SLOTS joined by '+', 0 = empty), else the default. */
std::vector<int> keys_for(int a)
{
    std::vector<int> keys{ k_pad_default_key[a] };
    const char* e = getenv("PAD_KEYMAP");
    const size_t idn = strlen(k_pad_action_id[a]);
    for (const char* p = e; p && *p; ) {
        if (!strncmp(p, k_pad_action_id[a], idn) && p[idn] == '=') {
            keys.clear();
            for (const char* v = p + idn + 1; ; ) {
                char* end;
                const int vk = (int)strtol(v, &end, 0);
                if (vk > 0) keys.push_back(vk);
                if (*end != '+') break;
                v = end + 1;
            }
        }
        p = strchr(p, ',');
        if (p) p++;
    }
    return keys;
}

/* The key the icons show for action `a`: its first one (0: none mapped). */
int key_for(int a)
{
    const std::vector<int> k = keys_for(a);
    return k.empty() ? 0 : k[0];
}

/* Draw `t` in `box`, shrinking the font until it fits. */
void draw_fit(Gdiplus::Graphics& g, const std::wstring& t, const Gdiplus::RectF& box, Gdiplus::Color c,
              float px, bool bold, Gdiplus::StringAlignment align, const wchar_t* family = nullptr)
{
    Gdiplus::FontFamily ff(family ? family : key_font());
    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
    sf.SetAlignment(align);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    for (;; px -= 0.5f) {
        Gdiplus::Font f(&ff, px, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF m;
        g.MeasureString(t.c_str(), -1, &f, Gdiplus::PointF(0, 0), &sf, &m);
        if (m.Width <= box.Width || px <= 6.0f) {
            Gdiplus::SolidBrush b(c);
            g.DrawString(t.c_str(), -1, &f, box, &sf, &b);
            return;
        }
    }
}

/* A w x h RGBA image drawn by `draw` on a transparent canvas. */
template <class F> std::vector<Rgba> render(int w, int h, F draw)
{
    gdiplus_once();
    std::vector<Rgba> out((size_t)w * h, Rgba{ 0, 0, 0, 0 });
    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        draw(g);
    }
    Gdiplus::BitmapData bd;
    Gdiplus::Rect r(0, 0, w, h);
    if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return out;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* p = (const unsigned char*)bd.Scan0 + (size_t)y * bd.Stride + (size_t)x * 4;
            out[(size_t)y * w + x] = { (float)p[2], (float)p[1], (float)p[0], (float)p[3] };
        }
    bmp.UnlockBits(&bd);
    return out;
}

/* render(), drawn at 4x and averaged down: cleaner edges and small text. */
template <class F> std::vector<Rgba> render_ss(int w, int h, F draw)
{
    const int S = 4;
    const std::vector<Rgba> big = render(w * S, h * S, [&](Gdiplus::Graphics& g) {
        g.ScaleTransform((Gdiplus::REAL)S, (Gdiplus::REAL)S);
        draw(g);
    });
    std::vector<Rgba> out((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float r = 0, gg = 0, b = 0, a = 0;
            for (int j = 0; j < S; j++)
                for (int i = 0; i < S; i++) {
                    const Rgba& c = big[(size_t)(y * S + j) * (w * S) + x * S + i];
                    r += c.r * c.a; gg += c.g * c.a; b += c.b * c.a; a += c.a;
                }
            out[(size_t)y * w + x] = a > 0 ? Rgba{ r / a, gg / a, b / a, a / (S * S) } : Rgba{ 0, 0, 0, 0 };
        }
    return out;
}

/* A keycap of cw x ch centred in a w x h image. */
std::vector<Rgba> keycap(const std::wstring& label, int w, int h, float cw, float ch)
{
    return render_ss(w, h, [&](Gdiplus::Graphics& g) {
        cv_keycap::paint(g, label, (w-cw)/2, (h-ch)/2, cw, ch, ch*0.60f);
    });
}

/* ---- the map screen -----------------------------------------------------
 * The map's atlas (mry/mp_dat0..4.arc, one per playable character) has the
 * PlayStation icons and "L1 ZOOM R1" painted into it, and no Xbox version in
 * the game. One is drawn here from the user's own files the first time a map
 * atlas opens: Circle becomes B, Square becomes X (the Cross icon itself, a
 * blue X), Cross becomes A, all on the map's grey disc; the B and A letters
 * come from the button guide's Xbox icons. The zoom labels become LT / RT,
 * where the 360 layout puts zoom. The keyboard version puts keycaps in the
 * three cells and the zoom keys' names in the labels. Geometry checked for
 * every language and character (only DXT noise differs). */
const int MAP_W = 192, MAP_H = 192;
const int CIRCLE_X = 159, CIRCLE_Y = 26, SQUARE_X = 159, SQUARE_Y = 62, CROSS_X = 159, CROSS_Y = 98;

float sat(const Rgba& c) { return std::max({ c.r, c.g, c.b }) - std::min({ c.r, c.g, c.b }); }
float radius(int x, int y) { return std::hypot((float)x - 15.5f, (float)y - 15.5f); }

/* The bright letter of a 32x32 Xbox icon, as a 0..1 mask inside its disc. */
std::vector<float> letter_mask(const std::vector<Rgba>& icon)
{
    std::vector<float> lum;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            const Rgba& c = icon[(size_t)y * 32 + x];
            if (c.a > 200 && radius(x, y) < 10.5f) lum.push_back((c.r + c.g + c.b) / 3);
        }
    std::vector<float> m(32 * 32, 0.0f);
    if (lum.empty()) return m;
    std::nth_element(lum.begin(), lum.begin() + (long)(lum.size() * 55 / 100), lum.end());
    const float base = lum[lum.size() * 55 / 100];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            const Rgba& c = icon[(size_t)y * 32 + x];
            if (c.a > 200 && radius(x, y) < 10.5f)
                m[(size_t)y * 32 + x] = std::clamp(((c.r + c.g + c.b) / 3 - base) / 40.0f, 0.0f, 1.0f);
        }
    return m;
}

/* The disc with `mask` painted on it in `col`, with a soft dark edge. */
std::vector<Rgba> paint_letter(std::vector<Rgba> disc, const std::vector<float>& m, const float col[3])
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            float e = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < 32 && yy >= 0 && yy < 32) e = std::max(e, m[(size_t)yy * 32 + xx]);
                }
            const float k = m[(size_t)y * 32 + x];
            e = std::max(0.0f, e - k) * 0.6f;
            Rgba& c = disc[(size_t)y * 32 + x];
            c.r = (c.r * (1 - e) + 40 * e) * (1 - k) + col[0] * k;
            c.g = (c.g * (1 - e) + 40 * e) * (1 - k) + col[1] * k;
            c.b = (c.b * (1 - e) + 40 * e) * (1 - k) + col[2] * k;
        }
    return disc;
}

struct MapImage {
    std::vector<Rgba> px;
    Rgba& at(int x, int y) { return px[(size_t)y * MAP_W + x]; }
    std::vector<Rgba> cell(int x0, int y0) const
    {
        std::vector<Rgba> c(32 * 32);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) c[(size_t)y * 32 + x] = px[(size_t)(y0 + y) * MAP_W + x0 + x];
        return c;
    }
    void put_cell(int x0, int y0, const std::vector<Rgba>& c)
    {
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) at(x0 + x, y0 + y) = c[(size_t)y * 32 + x];
    }
};

/* The Xbox version of a map atlas (DXT5, MAP_W x MAP_H), or empty. */
std::vector<unsigned char> xbox_map_atlas(const std::vector<unsigned char>& dxt,
                                          const std::vector<unsigned char>& a_icon,
                                          const std::vector<unsigned char>& b_icon)
{
    if (dxt.size() != (size_t)MAP_W * MAP_H || a_icon.size() != 1024 || b_icon.size() != 1024) return {};
    MapImage img{ dxt5_decode(dxt.data(), MAP_W, MAP_H) };
    const std::vector<Rgba> orig = img.px;
    const std::vector<Rgba> circle = img.cell(CIRCLE_X, CIRCLE_Y), square = img.cell(SQUARE_X, SQUARE_Y),
                            cross = img.cell(CROSS_X, CROSS_Y);
    /* A clean disc: per texel the greyest of the three icons; texels still
     * coloured take the mean grey at their radius. */
    std::vector<Rgba> disc(32 * 32);
    for (size_t i = 0; i < disc.size(); i++) {
        const Rgba* c3[3] = { &circle[i], &square[i], &cross[i] };
        disc[i] = **std::min_element(c3, c3 + 3, [](const Rgba* a, const Rgba* b) { return sat(*a) < sat(*b); });
    }
    for (int r = 0; r <= 23; r++) {
        float sum[4] = { 0, 0, 0, 0 }; int n = 0;
        for (int i = 0; i < 32 * 32; i++)
            if ((int)std::lround(radius(i % 32, i / 32)) == r && sat(disc[i]) < 18) {
                sum[0] += disc[i].r; sum[1] += disc[i].g; sum[2] += disc[i].b; sum[3] += disc[i].a; n++;
            }
        if (!n) continue;
        for (int i = 0; i < 32 * 32; i++)
            if ((int)std::lround(radius(i % 32, i / 32)) == r && sat(disc[i]) >= 18)
                disc[i] = { sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n };
    }
    static const float A_COL[3] = { 110, 220, 60 }, B_COL[3] = { 240, 80, 90 };
    img.put_cell(SQUARE_X, SQUARE_Y, cross);                                                       /* Square -> X */
    img.put_cell(CROSS_X, CROSS_Y, paint_letter(disc, letter_mask(dxt5_decode(a_icon.data(), 32, 32)), A_COL));
    img.put_cell(CIRCLE_X, CIRCLE_Y, paint_letter(disc, letter_mask(dxt5_decode(b_icon.data(), 32, 32)), B_COL));
    /* "L1" / "R1" -> "LT" / "RT": erase the 1 and draw a T in the label grey,
     * 2-px strokes like the font's, with a soft third pixel. */
    const Rgba grey = img.at(12, 106);
    if (grey.a > 200) {
        auto put = [&](int x, int y, float a) { Rgba& c = img.at(x, y); c = { grey.r, grey.g, grey.b, std::max(c.a, a) }; };
        auto one_to_t = [&](int x0, int x1, int stem, bool keep_foot) {
            for (int y = 99; y <= 116; y++)
                for (int x = x0; x <= x1; x++)
                    if (!(keep_foot && x == x0 && y >= 114)) img.at(x, y).a = 0;
            for (int x = x0; x <= x1 + 2; x++) { put(x, 99, 255); put(x, 100, 255); put(x, 101, 110); }
            for (int y = 99; y <= 116; y++) { put(stem, y, 255); put(stem + 1, y, 255); put(stem + 2, y, 110); }
        };
        one_to_t(9, 14, 12, true);      /* L1: the L's foot reaches x 9 on rows 114-116 */
        one_to_t(118, 124, 121, false); /* R1 */
    }
    return dxt5_reencode(dxt, orig, img.px, MAP_W, MAP_H);
}

/* The keyboard version of a map atlas: keycaps in the three cells, the zoom
 * keys' names in place of "L1" / "R1". */
std::vector<unsigned char> keyboard_map_atlas(const std::vector<unsigned char>& dxt)
{
    if (dxt.size() != (size_t)MAP_W * MAP_H) return {};
    MapImage img{ dxt5_decode(dxt.data(), MAP_W, MAP_H) };
    const std::vector<Rgba> orig = img.px;
    img.put_cell(CIRCLE_X, CIRCLE_Y, keycap(key_label(key_for(PA_CIRCLE)), 32, 32, 28, 28));
    img.put_cell(SQUARE_X, SQUARE_Y, keycap(key_label(key_for(PA_SQUARE)), 32, 32, 28, 28));
    img.put_cell(CROSS_X, CROSS_Y, keycap(key_label(key_for(PA_CROSS)), 32, 32, 28, 28));
    const Rgba grey = img.at(12, 106);
    if (grey.a > 200) {
        const Gdiplus::Color gc(255, (BYTE)grey.r, (BYTE)grey.g, (BYTE)grey.b);
        /* "L1" spans x 0..14 and "R1" x 107..124, rows 99..116 */
        for (int y = 99; y <= 116; y++) {
            for (int x = 0; x <= 14; x++) img.at(x, y).a = 0;
            for (int x = 107; x <= 124; x++) img.at(x, y).a = 0;
        }
        /* Drawn on a tall canvas, then placed so the glyphs' top sits on row
         * 99 like the label they replace (and their bottom by row 116). */
        auto label = [&](int a, int x_at, int width, Gdiplus::StringAlignment align) {
            const int CW = 20, CH = 40;
            const std::vector<Rgba> t = render(CW, CH, [&](Gdiplus::Graphics& g) {
                draw_fit(g, key_label(key_for(a)), Gdiplus::RectF(0, 0, (float)width, (float)CH), gc, 24, true, align); });
            int top = CH, bottom = -1;
            for (int y = 0; y < CH; y++)
                for (int x = 0; x < CW; x++)
                    if (t[(size_t)y * CW + x].a > 60) { top = std::min(top, y); bottom = std::max(bottom, y); }
            if (bottom < top) return;
            for (int y = top; y <= bottom && 99 + y - top <= 116; y++)
                for (int x = 0; x < width; x++) {
                    const Rgba& c = t[(size_t)y * CW + x];
                    if (c.a > 0) img.at(x_at + x, 99 + y - top) = c;
                }
        };
        label(PA_L1, 0, 16, Gdiplus::StringAlignmentNear);    /* x 0..15 */
        label(PA_R1, 107, 18, Gdiplus::StringAlignmentFar);   /* x 107..124 */
    }
    return dxt5_reencode(dxt, orig, img.px, MAP_W, MAP_H);
}

/* ---- How to Play in keyboard mode -----------------------------------------
 * The game has no keyboard pages, so they are drawn here from the PlayStation
 * page (DXT1, 1280 x 720) and its 360 twin, for any language:
 *  - every PlayStation icon becomes a keycap of the key mapped to it. Which
 *    button an icon is comes from the 360 page, which has a coloured icon at
 *    the same place (X blue = Square, A green = Cross, B red = Circle,
 *    Y yellow = Triangle);
 *  - the L1 / R1 badges become keycaps (two in a row: L1 then R1; one: R1);
 *  - the white up / down arrows in the panels become the Up / Down keys;
 * and on page 1 the controller and its orange leader lines are painted out
 * and a keyboard is drawn in their place with the mapped keys lit, and the
 * Move panel's four arrows get the movement keys. */
const int MAN_W = 1280, MAN_H = 720;

std::vector<Rgba> dxt1_decode(const unsigned char* d, int w, int h)
{
    std::vector<Rgba> px((size_t)w * h);
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++) {
            const unsigned char* b = d + ((size_t)by * (w / 4) + bx) * 8;
            const unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
            float c[4][3];
            unpack565(c0, c[0]); unpack565(c1, c[1]);
            for (int k = 0; k < 3; k++) {
                if (c0 > c1) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
                else         { c[2][k] = (c[0][k] + c[1][k]) / 2;     c[3][k] = 0; }
            }
            const uint32_t ci = (uint32_t)b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
            for (int i = 0; i < 16; i++) {
                const int k = (ci >> (2 * i)) & 3;
                const float* cc = c[k];
                px[(size_t)(by * 4 + i / 4) * w + bx * 4 + i % 4] = { cc[0], cc[1], cc[2], (c0 <= c1 && k == 3) ? 0.0f : 255.0f };
            }
        }
    return px;
}

/* Opaque four-colour DXT1 block. */
void dxt1_encode_block(const Rgba p[16], unsigned char* b)
{
    bool all[16];
    for (bool& u : all) u = true;
    encode_colour(p, all, b);
}

std::vector<unsigned char> dxt1_reencode(const std::vector<unsigned char>& dxt, const std::vector<Rgba>& orig,
                                         const std::vector<Rgba>& img, int w, int h)
{
    std::vector<unsigned char> out = dxt;
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++) {
            Rgba blk[16]; bool changed = false;
            for (int i = 0; i < 16; i++) {
                const size_t k = (size_t)(by * 4 + i / 4) * w + bx * 4 + i % 4;
                blk[i] = img[k];
                changed = changed || memcmp(&img[k], &orig[k], sizeof(Rgba)) != 0;
            }
            if (changed) dxt1_encode_block(blk, &out[((size_t)by * (w / 4) + bx) * 8]);
        }
    return out;
}

struct Comp { int x0, y0, x1, y1, n; float cy; };   /* cy: centre of mass, y */

/* 4-connected components of `mask` (w x h). */
std::vector<Comp> components(const std::vector<unsigned char>& mask, int w, int h)
{
    std::vector<Comp> out;
    std::vector<unsigned char> seen(mask.size(), 0);
    std::vector<int> stack;
    for (int s = 0; s < w * h; s++) {
        if (!mask[(size_t)s] || seen[(size_t)s]) continue;
        Comp c{ w, h, -1, -1, 0, 0 };
        float sy = 0;
        stack.assign(1, s); seen[(size_t)s] = 1;
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back();
            const int x = i % w, y = i / w;
            c.x0 = std::min(c.x0, x); c.x1 = std::max(c.x1, x); c.y0 = std::min(c.y0, y); c.y1 = std::max(c.y1, y);
            c.n++; sy += (float)y;
            const int nb[4] = { x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1 };
            for (int j : nb)
                if (j >= 0 && mask[(size_t)j] && !seen[(size_t)j]) { seen[(size_t)j] = 1; stack.push_back(j); }
        }
        c.cy = sy / (float)c.n;
        out.push_back(c);
    }
    return out;
}

/* Blend a w x h RGBA image onto `img` (MAN_W wide) at (x, y). */
void blend(std::vector<Rgba>& img, const std::vector<Rgba>& src, int w, int h, int x, int y)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const int X = x + i, Y = y + j;
            if (X < 0 || Y < 0 || X >= MAN_W || Y >= MAN_H) continue;
            const Rgba& s = src[(size_t)j * w + i];
            const float a = s.a / 255.0f;
            if (a <= 0) continue;
            Rgba& d = img[(size_t)Y * MAN_W + X];
            d.r = s.r * a + d.r * (1 - a); d.g = s.g * a + d.g * (1 - a); d.b = s.b * a + d.b * (1 - a); d.a = 255;
        }
}

/* The keys' lettering on the How to Play pages: a round, open sans, so O is
 * not read as 0. */
const wchar_t* const LABEL_FONT = L"Segoe UI";

void round_rect(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r)
{
    r = std::min(r, std::min(w, h) / 2);
    p.AddArc(x, y, 2 * r, 2 * r, 180, 90);
    p.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
    p.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
    p.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
    p.CloseFigure();
}

/* A raised key at (x, y), cw x ch: a dark rim, a light face lit from above and
 * a soft shadow below. `lit`: an amber rim (the keyboard's mapped keys). */
void draw_key3d(Gdiplus::Graphics& g, const std::wstring& label, float x, float y, float cw, float ch,
                float px, bool lit)
{
    cv_keycap::paint(g, label, x, y, cw, ch, px, lit);
}

/* The width of `t` drawn at px. */
float text_width(const std::wstring& t, float px)
{
    gdiplus_once();
    Gdiplus::Bitmap bmp(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    Gdiplus::FontFamily ff(LABEL_FONT);
    Gdiplus::Font f(&ff, px, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    Gdiplus::RectF m;
    g.MeasureString(t.c_str(), -1, &f, Gdiplus::PointF(0, 0), &sf, &m);
    return m.Width;
}

/* A key's width at height ch: square for one letter, wider for a name. */
float key_width(int vk, float ch, float max_w)
{
    return std::clamp(text_width(key_label(vk), ch * 0.6f) + ch * 0.6f, ch, std::max(ch, max_w));
}

struct HdKey { int vk; float x,y,w,h; };
struct HdKeyboard { float x=0,y=0,w=0,h=0; std::set<int> lit; };
static thread_local std::vector<HdKey>* s_hd_keys;
static thread_local HdKeyboard* s_hd_keyboard;

/* A key of cw x ch centred on (cx, cy). */
void put_keycap(std::vector<Rgba>& img, int vk, float cx, float cy, float cw, float ch)
{
    if (s_hd_keys) s_hd_keys->push_back({vk,cx-cw/2,cy-ch/2-1,cw,ch});
    const int w = (int)std::ceil(cw) + 6, h = (int)std::ceil(ch) + 6;
    blend(img, render_ss(w, h, [&](Gdiplus::Graphics& g) {
              draw_key3d(g, key_label(vk), (w - cw) / 2, (h - ch) / 2 - 1, cw, ch, ch * 0.6f, false); }),
          w, h, (int)std::lround(cx - w / 2.0f), (int)std::lround(cy - h / 2.0f));
}

/* Mean of the texels around each pixel of a w x h image, by summed areas:
 * `weight` 0 texels are left out. */
struct Sums {
    int w, h;
    std::vector<double> t;
    Sums(const std::vector<Rgba>& img, const std::vector<unsigned char>& weight, int w_, int h_)
        : w(w_), h(h_), t((size_t)(w_ + 1) * (h_ + 1) * 4, 0.0)
    {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const size_t i = (size_t)y * w + x;
                const double k = weight[i] ? 1.0 : 0.0, v[4] = { img[i].r * k, img[i].g * k, img[i].b * k, k };
                for (int c = 0; c < 4; c++) at(x + 1, y + 1, c) = v[c] + at(x, y + 1, c) + at(x + 1, y, c) - at(x, y, c);
            }
    }
    double& at(int x, int y, int c) { return t[((size_t)y * (w + 1) + x) * 4 + c]; }
    /* sums over the box of radius r around (x, y): r, g, b, count */
    void box(int x, int y, int r, double out[4])
    {
        const int x0 = std::max(0, x - r), y0 = std::max(0, y - r), x1 = std::min(w, x + r + 1), y1 = std::min(h, y + r + 1);
        for (int c = 0; c < 4; c++) out[c] = at(x1, y1, c) - at(x0, y1, c) - at(x1, y0, c) + at(x0, y0, c);
    }
};

/* Fill the unknown texels from the known ones around them, in a box grown
 * until it holds enough of them; then soften everything with radius `soft`. */
void fill_and_soften(std::vector<Rgba>& img, const std::vector<unsigned char>& known, int w, int h, int soft)
{
    {
        Sums s(img, known, w, h);
        std::vector<Rgba> out = img;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (known[(size_t)y * w + x]) continue;
                double b[4];
                for (int r = 8;; r = r * 3 / 2) {
                    s.box(x, y, r, b);
                    if (b[3] >= 0.25 * (2 * r + 1) * (2 * r + 1) || r > w + h) break;
                }
                if (b[3] > 0) out[(size_t)y * w + x] = { (float)(b[0] / b[3]), (float)(b[1] / b[3]), (float)(b[2] / b[3]), 255 };
            }
        img.swap(out);
    }
    const std::vector<unsigned char> all((size_t)w * h, 1);
    for (int pass = 0; pass < 2; pass++) {
        Sums s(img, all, w, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                double b[4]; s.box(x, y, soft, b);
                img[(size_t)y * w + x] = { (float)(b[0] / b[3]), (float)(b[1] / b[3]), (float)(b[2] / b[3]), 255 };
            }
    }
}

/* The keyboard drawn on page 1: 15 units wide, six rows, mapped keys lit.
 * Returns its height for width W. */
struct KbKey { int vk; float x, w; };
float keyboard_height(float W) { const float pad = W * 0.018f, u = (W - 2 * pad) / 15.0f; return 6 * u + 2 * pad + 2; }
void draw_keyboard(Gdiplus::Graphics& g, float X, float Y, float W, const std::set<int>& lit)
{
    static const std::vector<std::vector<KbKey>> rows = {
        { {0x1B,0,1}, {0x70,2,1}, {0x71,3,1}, {0x72,4,1}, {0x73,5,1}, {0x74,6.5f,1}, {0x75,7.5f,1}, {0x76,8.5f,1},
          {0x77,9.5f,1}, {0x78,11,1}, {0x79,12,1}, {0x7A,13,1}, {0x7B,14,1} },
        { {0xC0,0,1}, {'1',1,1}, {'2',2,1}, {'3',3,1}, {'4',4,1}, {'5',5,1}, {'6',6,1}, {'7',7,1}, {'8',8,1},
          {'9',9,1}, {'0',10,1}, {0xBD,11,1}, {0xBB,12,1}, {0x08,13,2} },
        { {0x09,0,1.5f}, {'Q',1.5f,1}, {'W',2.5f,1}, {'E',3.5f,1}, {'R',4.5f,1}, {'T',5.5f,1}, {'Y',6.5f,1},
          {'U',7.5f,1}, {'I',8.5f,1}, {'O',9.5f,1}, {'P',10.5f,1}, {0xDB,11.5f,1}, {0xDD,12.5f,1}, {0xDC,13.5f,1.5f} },
        { {0x14,0,1.75f}, {'A',1.75f,1}, {'S',2.75f,1}, {'D',3.75f,1}, {'F',4.75f,1}, {'G',5.75f,1}, {'H',6.75f,1},
          {'J',7.75f,1}, {'K',8.75f,1}, {'L',9.75f,1}, {0xBA,10.75f,1}, {0xDE,11.75f,1}, {0x0D,12.75f,2.25f} },
        { {0xA0,0,2.25f}, {'Z',2.25f,1}, {'X',3.25f,1}, {'C',4.25f,1}, {'V',5.25f,1}, {'B',6.25f,1}, {'N',7.25f,1},
          {'M',8.25f,1}, {0xBC,9.25f,1}, {0xBE,10.25f,1}, {0xBF,11.25f,1}, {0xA1,12.25f,2.75f} },
        { {0xA2,0,1.5f}, {0x5B,1.5f,1}, {0xA4,2.5f,1.5f}, {0x20,4,7}, {0xA5,11,1.5f}, {0x5C,12.5f,1}, {0xA3,13.5f,1.5f} },
    };
    const float pad = W * 0.018f, u = (W - 2 * pad) / 15.0f, H = 6 * u + 2 * pad;
    {   /* the case: dark, with a light rim on top and a shadow below */
        Gdiplus::GraphicsPath sh; round_rect(sh, X + 1, Y + 3, W - 2, H, 10);
        Gdiplus::SolidBrush sb(Gdiplus::Color(140, 0, 0, 0)); g.FillPath(&sb, &sh);
        Gdiplus::GraphicsPath c; round_rect(c, X, Y, W, H, 10);
        Gdiplus::LinearGradientBrush cb(Gdiplus::PointF(0, Y), Gdiplus::PointF(0, Y + H),
                                        Gdiplus::Color(255, 72, 75, 82), Gdiplus::Color(255, 34, 36, 40));
        g.FillPath(&cb, &c);
        Gdiplus::Pen outer(Gdiplus::Color(255, 12, 13, 16), 1.5f); g.DrawPath(&outer, &c);
        Gdiplus::GraphicsPath in; round_rect(in, X + 2, Y + 2, W - 4, H - 4, 8);
        Gdiplus::Pen rim(Gdiplus::Color(90, 200, 204, 212), 1.0f); g.DrawPath(&rim, &in);
    }
    auto lit_vk = [&](int vk) {
        if (lit.count(vk)) return true;
        if ((vk == 0xA0 || vk == 0xA1) && lit.count(0x10)) return true;
        if ((vk == 0xA2 || vk == 0xA3) && lit.count(0x11)) return true;
        if ((vk == 0xA4 || vk == 0xA5) && lit.count(0x12)) return true;
        return false;
    };
    for (size_t r = 0; r < rows.size(); r++)
        for (const KbKey& k : rows[r]) {
            const float gap = u * 0.08f;
            draw_key3d(g, key_label(k.vk), X + pad + k.x * u + gap / 2, Y + pad + r * u + gap / 2,
                       k.w * u - gap, u - gap - 1, u * 0.44f, lit_vk(k.vk));
        }
}

/* Which PS3 button the 360 page shows at (x0..x1, y0..y1), by its colour. */
int button_by_360_colour(const std::vector<Rgba>& xb, const std::vector<unsigned char>& diff,
                         int x0, int y0, int x1, int y1)
{
    float s[3] = { 0, 0, 0 }; int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const Rgba& c = xb[(size_t)y * MAN_W + x];
            if (diff[(size_t)y * MAN_W + x] && sat(c) > 60) { s[0] += c.r; s[1] += c.g; s[2] += c.b; n++; }
        }
    if (n < 20) return -1;
    const float r = s[0] / n, g = s[1] / n, b = s[2] / n;
    if (r > 150 && g > 120 && b < 110) return PA_TRIANGLE;              /* Y, yellow */
    if (b > r + 30 && b > g) return PA_SQUARE;                          /* X, blue   */
    if (g > r + 30 && g > b + 15) return PA_CROSS;                      /* A, green  */
    if (r > g + 50 && r > b + 30) return PA_CIRCLE;                     /* B, red    */
    return -1;
}

struct Item { int vk; float cx, cy, cw, ch; };

/* Page 1 has the controller in the middle with leader lines out to the
 * panels. The keyboard page is laid out again around a keyboard instead: the
 * frame is widened, its inside is cleared to the softened photo, and the
 * game's own panels (so their text stays in the page's language) are put back
 * in two columns, each with the key it stands for. Panels are cut from the
 * page as they are; one widens by repeating its plain inner column, which the
 * text never reaches. Rects measured on every language's page 1.
 *
 * Every move keeps a panel's text on the texture's 4 x 4 block grid (new tops
 * and lefts agree with the old ones mod 4, and runs put in before the text are
 * multiples of 4), so the blocks holding the game's lettering are copied as
 * they are instead of being compressed a second time. */
struct Panel {
    int x0, y0, x1, y1;   /* on the PlayStation page, border included */
    int col;              /* 0: left column, 1: right column (right aligned) */
    int y;                /* new top */
    int key;              /* the action a key is added for, before the text; -1: none */
    int group;            /* panels of a group end up the same width */
};
const Panel k_panels[] = {
    { 200, 148,  568, 187, 0, 148, -1,       0 },   /* L1 + R1 Change Targets */
    { 200, 296,  479, 375, 0, 204, PA_START, 1 },   /* Options / Skip Movies */
    { 200, 216,  479, 256, 0, 300, PA_SELECT, 1 },  /* Status */
    { 200, 392,  447, 635, 0, 356, -1,       2 },   /* Move */
    { 720, 144, 1079, 183, 1, 144, -1,       3 },   /* R1 + up / down Aim */
    { 720, 188, 1079, 227, 1, 188, -1,       3 },   /* R1 + Square Fire */
    { 720, 232, 1079, 271, 1, 232, PA_R1,    3 },   /* Ready Weapon */
    { 832, 293, 1079, 332, 1, 297, PA_TRIANGLE, 4 },/* Map */
    { 832, 360, 1079, 439, 1, 348, PA_SQUARE, 4 },  /* Examine / Open Doors */
    { 832, 475, 1079, 514, 1, 439, -1,       4 },   /* up + Cross Run */
    { 832, 520, 1079, 559, 1, 484, -1,       4 },   /* down + Cross Quick Turn */
    { 704, 592, 1079, 631, 1, 592, -1,       5 },   /* OK / Cancel */
};
const int FX0 = 28, FX1 = 1251, FY0 = 124, FY1 = 651;   /* the new frame, border included */
const int LX = 52, RX = 1229;                            /* the columns' outer edges */

void main_page_layout(std::vector<Rgba>& img, const std::vector<Rgba>& orig, const std::vector<Item>& items,
                      std::vector<int>& src, bool japanese = false)
{
    std::vector<Panel> panels(std::begin(::k_panels),std::end(::k_panels));
    if(japanese){
        const int rects[][4]={{200,130,527,169},{200,262,479,341},{200,199,479,238},{200,366,447,606},
            {752,126,1079,165},{752,170,1079,209},{752,214,1079,253},{832,276,1079,315},
            {832,343,1079,422},{832,448,1079,487},{832,493,1079,532},{704,564,1079,603}};
        for(size_t i=0;i<panels.size();++i){panels[i].x0=rects[i][0];panels[i].y0=rects[i][1];panels[i].x1=rects[i][2];panels[i].y1=rects[i][3];}
        panels[11].y=560; // leave the JP vibration note readable below the legend
    }
    const auto& k_panels=panels;
    const int NP = (int)k_panels.size();
    auto in_panel = [](const Panel& p, float x, float y) { return x >= p.x0 && x <= p.x1 && y >= p.y0 && y <= p.y1; };
    auto light = [&](int x, int y) { const Rgba& c = orig[(size_t)y * MAN_W + x]; return (c.r + c.g + c.b) / 3 > 150 && sat(c) < 60; };

    /* Each panel is rebuilt column by column from the page's own columns: an
     * icon's columns become a run of the plain column beside it, as wide as its
     * key needs (a long key name moves the text on instead of shrinking), an
     * added key gets room before the text, and the panel is padded at its right
     * bevel to its group's width. */
    const float KH = 30, IH = 28;   /* added keys; keys in place of icons */
    struct Key { float x, cy, w, h; int vk; };   /* x: centre, in new columns */
    struct Plan { std::vector<int> cols; std::vector<Key> keys; int width = 0; };
    std::vector<Plan> plan((size_t)NP);
    auto plain_col = [&](const Panel& p, int x) {   /* the nearest column at or left of x with no text */
        for (int c = x; c > p.x0 + 4; c--) {
            bool clear = true;
            for (int y = p.y0 + 4; y <= p.y1 - 4 && clear; y++) clear = !light(c, y);
            if (clear) return c;
        }
        return p.x0 + 4;
    };
    for (int i = 0; i < NP; i++) {
        const Panel& p = k_panels[i];
        Plan& q = plan[(size_t)i];
        struct Slot { int x0, x1; const Item* it; };
        std::vector<Slot> slots;
        for (const Item& it : items)
            if (in_panel(p, it.cx, it.cy))
                slots.push_back({ (int)std::floor(it.cx - it.cw / 2) - 2, (int)std::ceil(it.cx + it.cw / 2) + 1, &it });
        int text_x0 = p.x1 + 1;   /* the text's left edge, for an added key */
        if (p.key >= 0)
            for (int y = p.y0 + 5; y <= p.y1 - 5; y++)
                for (int x = p.x0 + 5; x < text_x0; x++)
                    if (light(x, y)) { text_x0 = x; break; }
        /* room for an added key goes in before the text's shadow (~6 px) */
        const int ins = std::max(p.x0 + 6, text_x0 - 8);
        for (int x = p.x0; x <= p.x1; x++) {
            if (x == ins && p.key >= 0) {
                const int vk = key_for(p.key);
                const float w = key_width(vk, KH, 72);
                const int need = (int)std::ceil(w) + 26, room = text_x0 - (p.x0 + 4);
                const int add = (std::max(0, need - room) + 3) / 4 * 4;
                for (int k = 0; k < add; k++) q.cols.push_back(p.x0 + 6);   /* past the bevel */
                q.keys.push_back({ (float)q.cols.size() + (text_x0 - ins) - 13 - w / 2, (p.y1 - p.y0) / 2.0f, w, KH, vk });
            }
            const Slot* sl = nullptr;
            for (const Slot& t : slots) if (x == t.x0) sl = &t;
            if (sl) {
                const float w = key_width(sl->it->vk, IH, 96);
                const int slot_w = sl->x1 - sl->x0 + 1, want = (int)std::ceil(w) + 4;
                const int n = slot_w + (int)std::ceil((want - slot_w) / 4.0) * 4, src = plain_col(p, sl->x0 - 1);
                q.keys.push_back({ (float)q.cols.size() + n / 2.0f, sl->it->cy - p.y0, w, IH, sl->it->vk });
                for (int k = 0; k < n; k++) q.cols.push_back(src);
                x = sl->x1;
                continue;
            }
            q.cols.push_back(x);
        }
        q.width = (int)q.cols.size();
    }
    for (int gi = 0; gi < 6; gi++) {   /* a group takes its widest panel's width */
        int w = 0;
        for (int i = 0; i < NP; i++) if (k_panels[i].group == gi) w = std::max(w, plan[(size_t)i].width);
        for (int i = 0; i < NP; i++) {
            Plan& q = plan[(size_t)i];
            if (k_panels[i].group != gi || q.width >= w) continue;
            q.cols.insert(q.cols.end() - 4, (size_t)(w - q.width), k_panels[i].x1 - 4);
            q.width = w;
        }
    }
    for (int i = 0; i < NP; i++) {   /* the right column: a block-aligned left, padded out to RX */
        Plan& q = plan[(size_t)i];
        if (k_panels[i].col != 1) continue;
        const int x = RX + 1 - q.width, aligned = x - (((x - k_panels[i].x0) % 4) + 4) % 4;
        q.cols.insert(q.cols.end() - 4, (size_t)(x - aligned), k_panels[i].x1 - 4);
        q.width += x - aligned;
    }
    auto new_x = [&](int i) { return k_panels[i].col == 0 ? LX : RX - plan[(size_t)i].width + 1; };

    /* The frame's new inside: the photo as the frame shows it (outside the old
     * frame: 0.44 x + 27, the old frame's tint), with what stood on it filled
     * in from around and everything softened alike. */
    const int bw = FX1 - FX0 + 1, bh = FY1 - FY0 + 1;
    std::vector<Rgba> bg((size_t)bw * bh);
    std::vector<unsigned char> known((size_t)bw * bh, 0);
    std::vector<unsigned char> fg((size_t)MAN_W * MAN_H, 0);
    auto mark = [&](int x0, int y0, int x1, int y1) {
        for (int y = std::max(0, y0); y <= std::min(MAN_H - 1, y1); y++)
            for (int x = std::max(0, x0); x <= std::min(MAN_W - 1, x1); x++) fg[(size_t)y * MAN_W + x] = 1;
    };
    for (const Panel& p : k_panels) mark(p.x0 - 3, p.y0 - 3, p.x1 + 3, p.y1 + 3);
    if(japanese)mark(360,616,930,644);
    mark(444, 250, 840, 592);                           /* the controller */
    for (int y = 126; y < 650; y++)                      /* the orange leader lines */
        for (int x = 166; x < 1114; x++) {
            const Rgba& c = orig[(size_t)y * MAN_W + x];
            if (c.r > 150 && c.g > 90 && c.g < 215 && c.b < 110 && c.r - c.b > 80) mark(x - 5, y - 5, x + 5, y + 5);
        }
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            const int X = FX0 + x, Y = FY0 + y;
            const Rgba& c = orig[(size_t)Y * MAN_W + X];
            const size_t i = (size_t)y * bw + x;
            if (X >= 166 && X <= 1113 && Y >= 126 && Y <= 649) { bg[i] = c; known[i] = !fg[(size_t)Y * MAN_W + X]; }
            else if (X < 164 || X > 1115 || Y < 124 || Y > 651) {
                bg[i] = { c.r * 0.44f + 27, c.g * 0.44f + 27, c.b * 0.44f + 27, 255 }; known[i] = 1;
            }
        }
    fill_and_soften(bg, known, bw, bh, 4);
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            const bool border = x < 2 || y < 2 || x >= bw - 2 || y >= bh - 2;
            img[(size_t)(FY0 + y) * MAN_W + FX0 + x] = border ? Rgba{ 50, 49, 49, 255 } : bg[(size_t)y * bw + x];
        }

    if(japanese)for(int y=616;y<=644;++y)for(int x=360;x<=930;++x)
        img[(size_t)y*MAN_W+x]=orig[(size_t)y*MAN_W+x];

    /* The panels and their keys. */
    for (int i = 0; i < NP; i++) {
        const Panel& p = k_panels[i];
        const Plan& q = plan[(size_t)i];
        const int nx = new_x(i);
        for (int j = 0; j < q.width; j++)
            for (int y = p.y0; y <= p.y1; y++) {
                const size_t d = (size_t)(p.y + y - p.y0) * MAN_W + nx + j, o = (size_t)y * MAN_W + q.cols[(size_t)j];
                img[d] = orig[o];
                src[d] = (int)o;
            }
        for (const Key& k : q.keys) put_keycap(img, k.vk, nx + k.x, p.y + k.cy, k.w, k.h);
    }
    /* The Move panel: the four grey arrows in the middle of its dial are
     * painted out (the two diagonals through (325.5, 535) are kept) and the
     * movement keys go there as an inverted T, clear of the direction words in
     * every language. */
    {
        const Panel& m = k_panels[3];
        const int ox = new_x(3) - m.x0, oy = m.y - m.y0;
        const int dy = japanese ? -28 : 0;
        const float CX = 325.5f, CY = 535.0f + dy;
        std::vector<unsigned char> arrow((size_t)MAN_W * MAN_H, 0);
        for (int y = 503 + dy; y <= 568 + dy; y++)
            for (int x = 292; x <= 359; x++) {
                const Rgba& c = orig[(size_t)y * MAN_W + x];
                const float l = (c.r + c.g + c.b) / 3, dx = x - CX, dy = y - CY;
                bool near_text = false;   /* the words' white strokes and their edges stay */
                for (int j = -2; j <= 2 && !near_text; j++)
                    for (int i = -2; i <= 2 && !near_text; i++) {
                        const Rgba& t = orig[(size_t)(y + j) * MAN_W + x + i];
                        near_text = (t.r + t.g + t.b) / 3 > 165;
                    }
                if (!near_text && l > 60 && l < 150 && sat(c) < 40 && std::fabs(dx - dy) > 2.5f && std::fabs(dx + dy) > 2.5f)
                    for (int j = -1; j <= 1; j++)
                        for (int i = -1; i <= 1; i++) {
                            const float ex = dx + i, ey = dy + j;
                            if (std::fabs(ex - ey) > 2.5f && std::fabs(ex + ey) > 2.5f)
                                arrow[(size_t)(y + j) * MAN_W + x + i] = 1;
                        }
            }
        for (bool any = true; any; ) {   /* fill from the unmasked neighbours, edge in */
            any = false;
            std::vector<std::pair<size_t, Rgba>> next;
            for (int y = 500 + dy; y <= 571 + dy; y++)
                for (int x = 289; x <= 362; x++) {
                    const size_t i = (size_t)y * MAN_W + x;
                    if (!arrow[i]) continue;
                    float s3[3] = { 0, 0, 0 }; int n = 0;
                    for (int j = -1; j <= 1; j++)
                        for (int k = -1; k <= 1; k++) {
                            const size_t o = (size_t)(y + j) * MAN_W + x + k;
                            if (arrow[o]) continue;
                            const Rgba& c = img[(size_t)(y + j + oy) * MAN_W + x + k + ox];
                            s3[0] += c.r; s3[1] += c.g; s3[2] += c.b; n++;
                        }
                    if (n >= 2) next.push_back({ i, Rgba{ s3[0] / n, s3[1] / n, s3[2] / n, 255 } });
                }
            for (auto& e : next) {
                const int x = (int)(e.first % MAN_W), y = (int)(e.first / MAN_W);
                img[(size_t)(y + oy) * MAN_W + x + ox] = e.second;
                arrow[e.first] = 0;
                any = true;
            }
        }
        const float K = 20, G = 21;
        put_keycap(img, key_for(PA_UP), CX + ox, 523 + dy + oy, K, K);
        put_keycap(img, key_for(PA_LEFT), CX - G + ox, 546 + dy + oy, K, K);
        put_keycap(img, key_for(PA_DOWN), CX + ox, 546 + dy + oy, K, K);
        put_keycap(img, key_for(PA_RIGHT), CX + G + ox, 546 + dy + oy, K, K);
    }

    /* The keyboard between the columns, lit with the mapped keys. */
    int left_edge = 0, right_edge = MAN_W;
    for (int i = 0; i < NP; i++) {
        const Panel& p = k_panels[i];
        if (p.col == 0 && p.y > 200) left_edge = std::max(left_edge, new_x(i) + plan[(size_t)i].width);
        if (p.col == 1 && p.group == 4) right_edge = std::min(right_edge, new_x(i));
    }
    const int KX = left_edge + 18, KW = right_edge - 18 - KX, KHT = (int)std::ceil(keyboard_height((float)KW)) + 4;
    const int KY = std::max(300, (296 + 524) / 2 - KHT / 2);
    std::set<int> lit;
    for (int a : { PA_CROSS, PA_CIRCLE, PA_SQUARE, PA_TRIANGLE, PA_L1, PA_R1, PA_SELECT, PA_START,
                   PA_UP, PA_DOWN, PA_LEFT, PA_RIGHT })
        for (int vk : keys_for(a)) lit.insert(vk);   /* every key that does something here */
    if (s_hd_keyboard) *s_hd_keyboard = {(float)KX,(float)KY,(float)KW,(float)KHT,lit};
    blend(img, render_ss(KW, KHT, [&](Gdiplus::Graphics& g) { draw_keyboard(g, 0, 0, (float)KW, lit); }), KW, KHT, KX, KY);
}

std::vector<unsigned char> japanese_keyboard_manual(const std::vector<unsigned char>& ps_dxt, int page);

/* The keyboard version of a How to Play page, or empty. `main`: page 1. */
std::vector<unsigned char> keyboard_manual_page(const std::vector<unsigned char>& ps_dxt,
                                                const std::vector<unsigned char>& xb_dxt, bool main, bool japanese = false, int page = -1)
{
    if (ps_dxt.size() != (size_t)MAN_W * MAN_H / 2 || xb_dxt.size() != ps_dxt.size()) return {};
    if(japanese)return japanese_keyboard_manual(ps_dxt,page);
    std::vector<Rgba> img = dxt1_decode(ps_dxt.data(), MAN_W, MAN_H);
    const std::vector<Rgba> orig = img, xb = dxt1_decode(xb_dxt.data(), MAN_W, MAN_H);
    const size_t N = (size_t)MAN_W * MAN_H;
    auto lum = [](const Rgba& c) { return (c.r + c.g + c.b) / 3; };

    /* Light texels (the white badges, icons and arrows) and the texels that
     * differ from the 360 page (the coloured icons of pages 2 and 5). */
    std::vector<unsigned char> light(N), diff(N);
    for (size_t i = 0; i < N; i++) {
        light[i] = lum(orig[i]) > 150 && sat(orig[i]) < 60;
        const Rgba& a = orig[i]; const Rgba& b = xb[i];
        diff[i] = std::max({ std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b) }) > 40;
    }
    std::vector<Item> items;
    std::vector<Comp> badges;
    auto square_icon = [](const Comp& c) {
        const int w = c.x1 - c.x0 + 1, h = c.y1 - c.y0 + 1;
        return w >= 22 && w <= 36 && h >= 22 && h <= 36 && std::abs(w - h) <= 6;
    };
    auto add_icon = [&](const Comp& c) {
        const int a = button_by_360_colour(xb, diff, c.x0, c.y0, c.x1, c.y1);
        if (a < 0) return;
        const float cx = (c.x0 + c.x1) / 2.0f, cy = (c.y0 + c.y1) / 2.0f;
        for (const Item& it : items) if (std::fabs(it.cx - cx) < 8 && std::fabs(it.cy - cy) < 8) return;
        const float side = (float)std::min(c.x1 - c.x0 + 1, c.y1 - c.y0 + 1) - 2;
        items.push_back({ key_for(a), cx, cy, side, side });
    };
    for (const Comp& c : components(light, MAN_W, MAN_H)) {
        const int w = c.x1 - c.x0 + 1, h = c.y1 - c.y0 + 1;
        if (c.n < 150 && !(h >= 24 && h <= 36)) continue;
        if (w >= 48 && w <= 60 && h >= 22 && h <= 30) badges.push_back(c);              /* L1 / R1 */
        else if (c.x0 >= 780 && w >= 12 && w <= 26 && h >= 24 && h <= 36 && h > w + 4) { /* up / down arrows */
            /* the up arrow's head is narrow at the very top and its body is
             * heavier below, so its centre of mass sits low */
            const bool up = c.cy > (c.y0 + c.y1) / 2.0f;
            items.push_back({ key_for(up ? PA_UP : PA_DOWN), (c.x0 + c.x1) / 2.0f, (c.y0 + c.y1) / 2.0f, 24, 24 });
        }
        else if (square_icon(c)) add_icon(c);                                             /* white icons */
    }
    for (const Comp& c : components(diff, MAN_W, MAN_H))
        if (square_icon(c)) add_icon(c);                                                  /* coloured icons */
    for (const Comp& b : badges) {
        bool left = false, right = false;
        for (const Comp& o : badges)
            if (&o != &b && std::abs(o.y0 - b.y0) < 6) { left = left || o.x0 < b.x0; right = right || o.x0 > b.x0; }
        const int a = (right && !left) ? PA_L1 : PA_R1;   /* "L1 + R1": the left one is L1 */
        items.push_back({ key_for(a), (b.x0 + b.x1) / 2.0f, (b.y0 + b.y1) / 2.0f, (float)(b.x1 - b.x0 + 1), (float)(b.y1 - b.y0 + 1) });
    }

    if (main) {
        std::vector<int> src(N, -1);
        main_page_layout(img, orig, items, src);
        std::vector<unsigned char> out = dxt1_reencode(ps_dxt, orig, img, MAN_W, MAN_H);
        /* blocks moved whole and untouched: the page's own bytes */
        for (int by = 0; by < MAN_H / 4; by++)
            for (int bx = 0; bx < MAN_W / 4; bx++) {
                const size_t d0 = (size_t)by * 4 * MAN_W + bx * 4;
                const int s0 = src[d0];
                if (s0 < 0 || (s0 % MAN_W) % 4 || (s0 / MAN_W) % 4) continue;
                bool same = true;
                for (int i = 0; i < 16 && same; i++) {
                    const size_t d = d0 + (size_t)(i / 4) * MAN_W + i % 4;
                    const int o = s0 + (i / 4) * MAN_W + i % 4;
                    same = src[d] == o && !memcmp(&img[d], &orig[(size_t)o], sizeof(Rgba));
                }
                if (same) {
                    const size_t sb = ((size_t)(s0 / MAN_W / 4) * (MAN_W / 4) + (s0 % MAN_W) / 4) * 8;
                    memcpy(&out[((size_t)by * (MAN_W / 4) + bx) * 8], &ps_dxt[sb], 8);
                }
            }
        return out;
    }
    /* In running text: over the whole icon, and wider into the spaces either
     * side when the key's name needs it. */
    for (const Item& it : items) {
        const float ch = it.ch + 3;
        put_keycap(img, it.vk, it.cx, it.cy, std::clamp(key_width(it.vk, ch, 96), it.cw + 3, it.cw + 15), ch);
    }
    return dxt1_reencode(ps_dxt, orig, img, MAN_W, MAN_H);
}

/* ---- live switching ------------------------------------------------------ */
struct Pair {
    std::vector<unsigned char> img[3];   /* SET_PS, SET_XBOX, SET_KB (may be empty) */
    int kb = KB_NONE, action = -1, manual_page = -1;
    bool jp_manual = false;
    uint64_t hd_revision = 1;
    std::vector<HdKey> hd_keys;
    HdKeyboard hd_keyboard;
    int box[4] = { 0, 0, 0, 0 };         /* KB_SCREEN_TEXT: the icon's box in the image */
    /* KB_MANUAL: the keyboard pages of earlier key maps. The game keeps each
     * page in its own memory, so one drawn with old keys can come back to
     * the screen later and is still recognised and redrawn. */
    std::vector<std::vector<unsigned char>> stale;
};
struct Seen { uint32_t off; size_t pair; };

std::mutex s_mu;
std::vector<Pair> s_pairs;
std::set<std::string> s_loaded;    /* archives whose pairs are registered */
std::vector<Seen> s_seen;          /* guest offsets showing one of the pairs */
int s_shown = -1;                  /* the set s_seen hold */

/* The image a pair shows for set `want`: the How to Play pages have no
 * keyboard version and show the controller's. */
int target(const Pair& p, int want)
{
    if (want == SET_KB && p.img[SET_KB].empty()) return pad_set();
    return want;
}

/* Build (or rebuild, for a new key map) a pair's keyboard image. */
void make_keyboard(Pair& p)
{
    ++p.hd_revision; p.hd_keys.clear(); p.hd_keyboard = {};
    struct Recording {
        Recording(Pair& p) { s_hd_keys=&p.hd_keys; s_hd_keyboard=&p.hd_keyboard; }
        ~Recording() { s_hd_keys=nullptr; s_hd_keyboard=nullptr; }
    } recording(p);
    if (p.kb == KB_ICON32) {
        p.img[SET_KB] = dxt5_reencode({}, {}, keycap(key_label(key_for(p.action)), 32, 32, 28, 28), 32, 32);
    } else if (p.kb == KB_MAP) {
        p.img[SET_KB] = keyboard_map_atlas(p.img[SET_PS]);
    } else if (p.kb == KB_MANUAL) {
        p.img[SET_KB] = keyboard_manual_page(p.img[SET_PS], p.img[SET_XBOX], p.action == 1, p.jp_manual, p.manual_page);
    } else if (p.kb == KB_SCREEN_TEXT) {
        /* 648 x 128 ARGB text ("Use [icon] to return to default."): the icon's
         * box gets a keycap, the rest of the text stays. */
        const int W = 648, x0 = p.box[0], y0 = p.box[1], bw = p.box[2] - x0 + 1, bh = p.box[3] - y0 + 1;
        if (p.img[SET_PS].size() != (size_t)W * 128 * 4 || bw <= 0 || bh <= 0) return;
        const float side = (float)std::min(bw, bh) - 1;
        const std::vector<Rgba> cap = keycap(key_label(key_for(p.action)), bw, bh, side, side);
        std::vector<unsigned char> out = p.img[SET_PS];
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                const Rgba& c = cap[(size_t)y * bw + x];
                unsigned char* d = &out[((size_t)(y0 + y) * W + x0 + x) * 4];   /* A R G B */
                d[0] = (unsigned char)c.a; d[1] = (unsigned char)c.r; d[2] = (unsigned char)c.g; d[3] = (unsigned char)c.b;
            }
        p.img[SET_KB] = std::move(out);
    }
}

/* KB_SCREEN_TEXT: the box where the PlayStation and Xbox images differ. */
void find_icon_box(Pair& p)
{
    const std::vector<unsigned char>& a = p.img[SET_PS];
    const std::vector<unsigned char>& b = p.img[SET_XBOX];
    const int W = 648, H = 128;
    if (a.size() != (size_t)W * H * 4 || b.size() != a.size()) return;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const size_t i = ((size_t)y * W + x) * 4;
            int d = 0;
            for (int k = 0; k < 4; k++) d = std::max(d, std::abs((int)a[i + k] - (int)b[i + k]));
            if (d > 40) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
        }
    if (x1 >= x0 && x1 - x0 < 48 && y1 - y0 < 48) { p.box[0] = x0; p.box[1] = y0; p.box[2] = x1; p.box[3] = y1; }
}

void load_pairs(const std::string& path, const std::string& key)
{
    {
        std::lock_guard<std::mutex> l(s_mu);
        if (!s_loaded.insert(path).second) return;
    }
    const std::vector<unsigned char> s = unblock(read_file(path));
    int added = 0;
    for (const Swap& sw : k_swaps) {
        if (!ends_with(key, sw.file)) continue;
        Pair p;
        p.img[SET_PS] = tex_bytes(s, sw.ps);
        p.img[SET_XBOX] = tex_bytes(s, sw.xbox);
        if (p.img[SET_PS].empty() || p.img[SET_PS].size() != p.img[SET_XBOX].size() || p.img[SET_PS] == p.img[SET_XBOX]) continue;
        p.kb = sw.kb; p.action = sw.action;
        p.jp_manual = key.find("/jpn/") != std::string::npos;
        if (p.kb==KB_MANUAL) p.manual_page = strstr(sw.ps,"Manual00") ? 0 : strstr(sw.ps,"Manual01") ? 1 : 4;
        if (p.kb == KB_SCREEN_TEXT) find_icon_box(p);
        if (p.kb != KB_MANUAL) make_keyboard(p);   /* pages: when first wanted (watcher) */
        std::lock_guard<std::mutex> l(s_mu);
        s_pairs.push_back(std::move(p));
        added++;
    }
    fprintf(stderr, "[icons] %d image pair(s) from %s\n", added, path.c_str());
}

/* A map atlas was opened: register its PlayStation/Xbox/keyboard images. */
void load_map_pair(const std::string& path, const std::string& key)
{
    {
        std::lock_guard<std::mutex> l(s_mu);
        if (!s_loaded.insert(path).second) return;
    }
    const size_t m = key.rfind("/mry/");
    if (m == std::string::npos) return;
    const std::string guide = path.substr(0, m) + "/window/button_guide.arc";
    const std::vector<unsigned char> g = unblock(read_file(guide)), s = unblock(read_file(path));
    Pair p;
    p.img[SET_PS] = tex_bytes(s, "tex0000_BM");
    p.img[SET_XBOX] = xbox_map_atlas(p.img[SET_PS], tex_bytes(g, "buifma_x_BM_NOMIP"), tex_bytes(g, "buifba_x_BM_NOMIP"));
    if (p.img[SET_XBOX].empty()) { fprintf(stderr, "[icons] %s: not the expected map atlas\n", path.c_str()); return; }
    p.kb = KB_MAP;
    make_keyboard(p);
    if (const char* dump = getenv("CV_ICONS_DUMP")) {   /* the generated atlases, raw DXT5, for checking */
        if (FILE* f = fopen(dump, "wb")) {
            fwrite(p.img[SET_XBOX].data(), 1, p.img[SET_XBOX].size(), f);
            fwrite(p.img[SET_KB].data(), 1, p.img[SET_KB].size(), f);
            fclose(f);
        }
    }
    std::lock_guard<std::mutex> l(s_mu);
    s_pairs.push_back(std::move(p));
    fprintf(stderr, "[icons] map atlas images from %s\n", path.c_str());
}

/* If the image at `data` is one of a pair's versions, make it the one `want`
 * asks for. Returns the pair's index, or -1. */
long fix_image(unsigned char* data, uint32_t nbytes, int want, bool* wrote = nullptr)
{
    for (size_t i = 0; i < s_pairs.size(); i++) {
        const Pair& p = s_pairs[i];
        int cur = -1;
        for (int k = 0; k < 3 && cur < 0; k++)
            if (p.img[k].size() == nbytes && !memcmp(data, p.img[k].data(), nbytes)) cur = k;
        for (const auto& old : p.stale)
            if (cur < 0 && old.size() == nbytes && !memcmp(data, old.data(), nbytes)) cur = 3;
        if (cur < 0) continue;
        const int t = target(p, want);
        if (t != cur && !p.img[t].empty()) {
            memcpy(data, p.img[t].data(), nbytes);
            if (wrote) *wrote = true;
        }
        return (long)i;
    }
    return -1;
}


/* Production sprite sheet, embedded in every regional executable. Its native
 * circles are ~344px across, sufficient for 32px prompts through 4K and beyond. */
Gdiplus::Bitmap* hd_pad_sheet() {
    static std::unique_ptr<Gdiplus::Bitmap> sheet;
    static IStream* stream=nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        gdiplus_once();
        HRSRC r=FindResourceW(nullptr,L"CONTROLLER_BUTTONS_HD",MAKEINTRESOURCEW(10));
        if(!r)return;
        HGLOBAL data=LoadResource(nullptr,r); const DWORD n=SizeofResource(nullptr,r);
        HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,n);
        if(!copy || !data) { if(copy)GlobalFree(copy); return; }
        void* bytes=GlobalLock(copy);
        if(!bytes) {GlobalFree(copy);return;}
        memcpy(bytes,LockResource(data),n); GlobalUnlock(copy);
        if(CreateStreamOnHGlobal(copy,TRUE,&stream)!=S_OK){GlobalFree(copy);return;}
        sheet.reset(Gdiplus::Bitmap::FromStream(stream));
        if(!sheet || sheet->GetLastStatus()!=Gdiplus::Ok)sheet.reset();
    });
    return sheet.get();
}
void hd_pad(Gdiplus::Graphics& g,int set,int action,float x,float y,float w,float h) {
    auto* sheet=hd_pad_sheet(); if(!sheet)return;
    int col=action==PA_CROSS?0:action==PA_CIRCLE?1:action==PA_SQUARE?2:3;
    // Tight artwork rectangles, excluding the sheet's transparent margins.
    static const int rect[8][4]={{53,69,349,345},{492,71,349,345},{930,70,350,346},{1370,69,350,346},
        {49,464,347,349},{489,464,352,350},{925,464,354,350},{1375,464,350,350}};
    const int i=(set==SET_XBOX?4:0)+col; const auto& r=rect[i];
    auto state=g.Save(); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::ImageAttributes attr; attr.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    g.DrawImage(sheet,Gdiplus::RectF(x,y,w,h),(float)r[0],(float)r[1],(float)r[2],(float)r[3],Gdiplus::UnitPixel,&attr);
    g.Restore(state);
}


struct ManualIcon { float cx,cy,side; int action; bool white; };
static bool manual_badge_area(const Pair& p,float x,float y) {
    if(p.manual_page==0)return !(x>430&&x<835&&y>260&&y<575) && y>125&&y<640;
    if(p.manual_page==1)return x>240&&x<850&&y>520&&y<660;
    if(p.manual_page==4)return x>240&&x<1050&&y>150&&y<280;
    return false;
}
static int white_manual_symbol(const std::vector<Rgba>& image,int W,float cx,float cy,float side) {
    int action=-1;float best=0;
    const int actions[]={PA_CROSS,PA_CIRCLE,PA_SQUARE,PA_TRIANGLE};
    auto segment=[](float x,float y,float ax,float ay,float bx,float by){
        float dx=bx-ax,dy=by-ay,t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.0f,1.0f);
        return std::hypot(x-ax-t*dx,y-ay-t*dy);
    };
    for(int a:actions)for(float radius:{.20f,.22f,.24f,.26f,.28f,.30f})for(float stroke:{.025f,.04f,.055f,.07f}){
        int intersection=0,uni=0;
        for(int yy=-11;yy<=11;++yy)for(int xx=-11;xx<=11;++xx){
            if(xx*xx+yy*yy>132.25f)continue;
            const float x=xx/30.0f,y=yy/30.0f;
            const auto& c=image[(size_t)std::lround(cy+yy*side/30)*W+(size_t)std::lround(cx+xx*side/30)];
            bool dark=(c.r+c.g+c.b)/3<110,pred=false;
            if(a==PA_CROSS)pred=std::min(std::fabs(x-y),std::fabs(x+y))*.70710678f<stroke&&std::max(std::fabs(x),std::fabs(y))<radius+stroke;
            else if(a==PA_CIRCLE)pred=std::fabs(std::hypot(x,y)-radius)<stroke;
            else if(a==PA_SQUARE)pred=std::fabs(std::max(std::fabs(x),std::fabs(y))-radius)<stroke;
            else pred=std::min({segment(x,y,0,-radius,radius,radius),segment(x,y,radius,radius,-radius,radius),segment(x,y,-radius,radius,0,-radius)})<stroke;
            intersection+=dark&&pred;uni+=dark||pred;
        }
        const float score=uni?(float)intersection/uni:0;
        if(score>best){best=score;action=a;}
    }
    return best>.42f?action:-1;
}
/* Locate badges in THIS page. No coordinates or button identities are borrowed
 * from the other console's manual: JP has extra Circle badges and shifted rows. */
static std::vector<ManualIcon> manual_icons(const Pair& p,int set,const std::vector<Rgba>& image,int W,int H) {
    std::vector<ManualIcon> out;std::vector<unsigned char> mask((size_t)W*H);
    auto add=[&](float cx,float cy,float side,int action,bool white){
        if(action<0||!manual_badge_area(p,cx,cy))return;
        for(const auto& old:out)if(std::fabs(old.cx-cx)<14&&std::fabs(old.cy-cy)<14)return;
        out.push_back({cx,cy,side,action,white});
    };
    if(set==SET_PS){
        for(size_t i=0;i<mask.size();++i)mask[i]=(image[i].r+image[i].g+image[i].b)/3>150&&sat(image[i])<60;
        for(const Comp& c:components(mask,W,H)){
            int w=c.x1-c.x0+1,h=c.y1-c.y0+1;
            if(w<22||w>36||h<22||h>36||std::abs(w-h)>6||c.n<150)continue;
            const float cx=(c.x0+c.x1)*.5f,cy=(c.y0+c.y1)*.5f,side=(float)std::max(w,h);
            if(!manual_badge_area(p,cx,cy))continue;
            int ring=0;
            for(int j=0;j<32;++j){float a=j*6.28318530718f/32;
                const auto& q=image[(size_t)std::lround(cy+std::sin(a)*side*.39f)*W+(size_t)std::lround(cx+std::cos(a)*side*.39f)];
                ring+=(q.r+q.g+q.b)/3>145&&sat(q)<70;}
            if(ring<24)continue;
            add(cx,cy,side+2,white_manual_symbol(image,W,cx,cy,side),true);
        }
    }
    for(size_t i=0;i<mask.size();++i)mask[i]=sat(image[i])>90&&std::max({image[i].r,image[i].g,image[i].b})>95;
    for(const Comp& c:components(mask,W,H)){
        const int w=c.x1-c.x0+1,h=c.y1-c.y0+1;
        if(set==SET_XBOX){if(w<18||w>36||h<18||h>36||std::abs(w-h)>6||c.n<150)continue;}
        else if(w<6||w>26||h<6||h>26||std::abs(w-h)>8||c.n<15)continue;
        const float cx=(c.x0+c.x1)*.5f,cy=(c.y0+c.y1)*.5f;
        if(!manual_badge_area(p,cx,cy))continue;
        float r=0,g=0,b=0;int count=0;
        for(int y=c.y0;y<=c.y1;++y)for(int x=c.x0;x<=c.x1;++x){const auto& q=image[(size_t)y*W+x];if(sat(q)<60)continue;r+=q.r;g+=q.g;b+=q.b;++count;}
        if(!count)continue;r/=count;g/=count;b/=count;
        int action=-1;
        if(set==SET_XBOX)action=g>r+12&&g>b+25?PA_CROSS:r>g+45&&r>b+30?PA_CIRCLE:b>r+25&&b>g+8?PA_SQUARE:r>b+40&&g>b+40?PA_TRIANGLE:-1;
        else action=r>g+40&&b>g+40?PA_SQUARE:b>r+25&&b>g+8?PA_CROSS:g>r+25&&g>b+25?PA_TRIANGLE:r>g+40&&r>b+30?PA_CIRCLE:-1;
        add(cx,cy,32,action,false);
    }
    return out;
}
/* Restore the panel behind the entire old badge, including its antialiased rim.
 * Median samples of nearby low-luminance panel pixels reject neighbouring text.
 * Changes stay inside the original circular footprint. */
static void clear_manual_icons(std::vector<Rgba>& image,int W,int H,const std::vector<ManualIcon>& marks) {
    const auto original=image;
    for(const auto& m:marks){
        const float radius=m.side*.5f+1.0f;
        int x0=std::max(0,(int)std::floor(m.cx-radius)),x1=std::min(W-1,(int)std::ceil(m.cx+radius));
        int y0=std::max(0,(int)std::floor(m.cy-radius)),y1=std::min(H-1,(int)std::ceil(m.cy+radius));
        for(int y=y0;y<=y1;++y){
            std::vector<float> red,green,blue,alpha;
            for(int side:{-1,1})for(int d:{19,20,21,22,23}){
                int x=(int)std::lround(m.cx)+side*d;if(x<0||x>=W)continue;
                const auto& c=original[(size_t)y*W+x];if((c.r+c.g+c.b)/3>135)continue;
                red.push_back(c.r);green.push_back(c.g);blue.push_back(c.b);alpha.push_back(c.a);
            }
            if(red.empty())continue;
            auto median=[](std::vector<float>& a){std::sort(a.begin(),a.end());return a[a.size()/2];};
            const Rgba background={median(red),median(green),median(blue),median(alpha)};
            for(int x=x0;x<=x1;++x)if((x-m.cx)*(x-m.cx)+(y-m.cy)*(y-m.cy)<=radius*radius)image[(size_t)y*W+x]=background;
        }
    }
}

/* Japanese manual controls measured in the original 1280x720 pages. Rectangles
 * include the old antialiased edge. No coordinates are shared between consoles. */
struct ManualControl { float x,y,w,h; int action; };
static std::vector<ManualControl> jp_manual_controls(const Pair& p,int set) {
    if(!p.jp_manual||p.manual_page!=0)return {};
    if(set==SET_XBOX)return {
        {275,152,32,32,PA_L1},{353,152,32,32,PA_R1},
        {766,148,32,32,PA_R1},{766,192,32,32,PA_R1},
        {846,148,26,32,PA_UP},{908,148,26,32,PA_DOWN},
        {843,479,26,32,PA_UP},{843,524,26,32,PA_DOWN}};
    return {{232,135,58,30,PA_L1},{329,135,58,30,PA_R1},
        {763,131,58,30,PA_R1},{763,175,58,30,PA_R1},
        {866,130,26,32,PA_UP},{928,130,26,32,PA_DOWN},
        {843,451,26,32,PA_UP},{843,496,26,32,PA_DOWN}};
}
static void clear_manual_rects(std::vector<Rgba>& image,int W,int H,const std::vector<ManualControl>& controls) {
    const auto orig=image;
    for(const auto& c:controls){
        const int x0=std::max(0,(int)c.x),x1=std::min(W-1,(int)(c.x+c.w-1));
        for(int y=std::max(0,(int)c.y);y<std::min(H,(int)(c.y+c.h));++y){
            const auto left=orig[(size_t)y*W+std::max(0,x0-2)],right=orig[(size_t)y*W+std::min(W-1,x1+2)];
            for(int x=x0;x<=x1;++x){float t=(float)(x-x0)/(x1-x0);
                image[(size_t)y*W+x]={left.r+(right.r-left.r)*t,left.g+(right.g-left.g)*t,left.b+(right.b-left.b)*t,255};}
        }
    }
}
static void manual_arrow(Gdiplus::Graphics& g,float x,float y,float w,float h,int action) {
    using namespace Gdiplus;
    PointF pts[]={{w*.5f,0},{w,h*.45f},{w*.71f,h*.45f},{w*.71f,h},{w*.29f,h},{w*.29f,h*.45f},{0,h*.45f}};
    auto state=g.Save();g.TranslateTransform(x+w*.5f,y+h*.5f);
    if(action==PA_DOWN)g.RotateTransform(180);else if(action==PA_LEFT)g.RotateTransform(-90);else if(action==PA_RIGHT)g.RotateTransform(90);
    g.TranslateTransform(-w*.5f,-h*.5f);
    SolidBrush shadow(Color(170,0,0,0));PointF sh[7];for(int i=0;i<7;++i)sh[i]={pts[i].X+1,pts[i].Y+1};g.FillPolygon(&shadow,sh,7);
    SolidBrush face(Color(255,239,242,238));Pen edge(Color(255,75,82,77),.8f);
    g.FillPolygon(&face,pts,7);g.DrawPolygon(&edge,pts,7);g.Restore(state);
}
static void manual_shoulder(Gdiplus::Graphics& g,int set,int action,float x,float y,float w,float h) {
    using namespace Gdiplus;GraphicsPath path;cv_keycap::rounded(path,x+.6f,y+.6f,w-1.2f,h-1.2f,3.2f);
    LinearGradientBrush face(PointF(x,y),PointF(x,y+h),Color(255,250,251,248),Color(255,192,199,190));
    Pen edge(Color(255,70,81,70),1.0f);g.FillPath(&face,&path);g.DrawPath(&edge,&path);
    const wchar_t* label=action==PA_L1?(set==SET_PS?L"L1":L"LT"):(set==SET_PS?L"R1":L"RT");
    FontFamily ff(L"Segoe UI");GraphicsPath glyph;StringFormat fmt(StringFormat::GenericTypographic());
    glyph.AddString(label,-1,&ff,FontStyleBold,h*.70f,PointF(0,0),&fmt);RectF b;glyph.GetBounds(&b);
    Matrix m;m.Translate(x+(w-b.Width)*.5f-b.X,y+(h-b.Height)*.5f-b.Y);glyph.Transform(&m);
    SolidBrush ink(Color(255,35,41,36));g.FillPath(&ink,&glyph);
}
std::vector<unsigned char> japanese_keyboard_manual(const std::vector<unsigned char>& ps_dxt,int page_number) {
    const bool main=page_number==0;
    Pair page;page.jp_manual=true;page.manual_page=page_number;
    auto orig=dxt1_decode(ps_dxt.data(),MAN_W,MAN_H),img=orig;
    auto marks=manual_icons(page,SET_PS,orig,MAN_W,MAN_H);
    std::vector<Item> items;
    for(const auto& m:marks)items.push_back({key_for(m.action),m.cx,m.cy,m.side,m.side});
    const auto controls=jp_manual_controls(page,SET_PS);
    for(const auto& c:controls)items.push_back({key_for(c.action),c.x+c.w*.5f,c.y+c.h*.5f,c.w,c.h});
    if(main){std::vector<int> src((size_t)MAN_W*MAN_H,-1);main_page_layout(img,orig,items,src,true);}
    else {clear_manual_icons(img,MAN_W,MAN_H,marks);for(const auto& it:items)
        put_keycap(img,it.vk,it.cx,it.cy,30,30);}
    return dxt1_reencode(ps_dxt,orig,img,MAN_W,MAN_H);
}

/* Native-size source pages remain the authority for layout and translations.
 * Only the button overlays are rerasterized at the render resolution. */
std::vector<Rgba> hd_image(const Pair& p,int set,int scale,int W,int H) {
    std::vector<Rgba> base;
    if(p.kb==KB_SCREEN_TEXT) {
        base.resize((size_t)W*H);
        const auto& b=p.img[set];
        for(size_t i=0;i<base.size();++i)base[i]={(float)b[i*4+1],(float)b[i*4+2],(float)b[i*4+3],(float)b[i*4]};
    } else if(p.kb==KB_MANUAL)base=dxt1_decode(p.img[set].data(),W,H);
    else base=dxt5_decode(p.img[set].data(),W,H);
    std::vector<ManualIcon> manual;
    std::vector<ManualControl> controls;
    if(p.kb==KB_MANUAL && set!=SET_KB){controls=jp_manual_controls(p,set);clear_manual_rects(base,W,H,controls);}
    if(p.kb==KB_MANUAL && set!=SET_KB){manual=manual_icons(p,set,base,W,H);clear_manual_icons(base,W,H,manual);}
    // GDI+ straight alpha input; DrawImage performs the scale in premultiplied space.
    Gdiplus::Bitmap original(W,H,PixelFormat32bppARGB);
    Gdiplus::BitmapData bits; Gdiplus::Rect rect(0,0,W,H);
    if(original.LockBits(&rect,Gdiplus::ImageLockModeWrite,PixelFormat32bppARGB,&bits)!=Gdiplus::Ok)return {};
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){
        auto* d=(unsigned char*)bits.Scan0+(ptrdiff_t)y*bits.Stride+x*4;const Rgba& c=base[(size_t)y*W+x];
        d[0]=(unsigned char)c.b;d[1]=(unsigned char)c.g;d[2]=(unsigned char)c.r;d[3]=(unsigned char)c.a;
    }
    original.UnlockBits(&bits);
    return render(W*scale,H*scale,[&](Gdiplus::Graphics& g){
        g.ScaleTransform((float)scale,(float)scale);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        Gdiplus::ImageAttributes attrs;attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        g.DrawImage(&original,Gdiplus::RectF(0,0,(float)W,(float)H),0,0,(float)W,(float)H,Gdiplus::UnitPixel,&attrs);
        auto button=[&](int a,float x,float y,float w,float h){
            if(set==SET_KB)cv_keycap::paint(g,key_label(key_for(a)),x,y,w,h,h*0.60f);
            else hd_pad(g,set,a,x,y,w,h);
        };
        auto erase=[&](float x,float y,float w,float h){
            auto state=g.Save();g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            Gdiplus::SolidBrush clear(Gdiplus::Color(0,0,0,0));g.FillRectangle(&clear,x,y,w,h);g.Restore(state);
        };
        if(p.kb==KB_ICON32) {erase(0,0,32,32);button(p.action,2,2,28,28);}
        else if(p.kb==KB_SCREEN_TEXT){
            float x=(float)p.box[0],y=(float)p.box[1],w=(float)(p.box[2]-p.box[0]+1),h=(float)(p.box[3]-p.box[1]+1);
            erase(x,y,w,h);const float side=std::min(w,h)-1;button(p.action,x+(w-side)/2,y+(h-side)/2,side,side);
        }else if(p.kb==KB_MAP){
            for(auto a:{PA_CIRCLE,PA_SQUARE,PA_CROSS}){
                const int Y=a==PA_CIRCLE?CIRCLE_Y:a==PA_SQUARE?SQUARE_Y:CROSS_Y;
                erase(CIRCLE_X-1.0f,Y-1.0f,34,34);button(a,CIRCLE_X+2.0f,Y+2.0f,28,28);
            }
        }else if(p.kb==KB_MANUAL && set==SET_KB){
            for(const HdKey& k:p.hd_keys)cv_keycap::paint(g,key_label(k.vk),k.x,k.y,k.w,k.h,k.h*0.60f);
            if(p.hd_keyboard.w>0)draw_keyboard(g,p.hd_keyboard.x,p.hd_keyboard.y,p.hd_keyboard.w,p.hd_keyboard.lit);
        }else if(p.kb==KB_MANUAL){
            for(const auto& c:controls){
                if(c.action==PA_L1||c.action==PA_R1)manual_shoulder(g,set,c.action,c.x,c.y,c.w,c.h);
                else manual_arrow(g,c.x,c.y,c.w,c.h,c.action);
            }
            for(const ManualIcon& m:manual){
                const float cx=m.cx,cy=m.cy,side=m.side;const int a=m.action;
                if(set==SET_PS&&m.white){
                    Gdiplus::SolidBrush face(Gdiplus::Color(255,211,213,210));
                    Gdiplus::Pen ink(Gdiplus::Color(255,35,36,37),1.8f);
                    g.FillEllipse(&face,cx-side/2,cy-side/2,side,side);g.DrawEllipse(&ink,cx-side/2,cy-side/2,side,side);
                    const float r=side*.24f;
                    if(a==PA_CROSS){g.DrawLine(&ink,cx-r,cy-r,cx+r,cy+r);g.DrawLine(&ink,cx-r,cy+r,cx+r,cy-r);}
                    else if(a==PA_CIRCLE)g.DrawEllipse(&ink,cx-r,cy-r,2*r,2*r);
                    else if(a==PA_SQUARE)g.DrawRectangle(&ink,cx-r,cy-r,2*r,2*r);
                    else {Gdiplus::PointF pts[]={{cx,cy-r},{cx+r,cy+r},{cx-r,cy+r},{cx,cy-r}};g.DrawLines(&ink,pts,4);}
                }else button(a,cx-side/2,cy-side/2,side,side);
            }
        }
    });
}

uint64_t on_hd_image(const uint8_t* data,uint32_t nbytes,uint32_t W,uint32_t H,
                     float render_scale,uint64_t previous,texpack_image** image) {
    *image=nullptr;
    if(const char* off=getenv("CV_HD_BUTTONS");off&&off[0]=='0')return 0;
    // Reject world textures before taking a lock or comparing their contents.
    if(!((W==32&&H==32)||(W==MAP_W&&H==MAP_H)||(W==648&&H==128)||(W==MAN_W&&H==MAN_H)))return 0;
    std::lock_guard<std::mutex> lock(s_mu);
    for(size_t i=0;i<s_pairs.size();++i){
        const Pair& p=s_pairs[i];const int set=target(p,s_shown<0?wanted_set():s_shown);
        if(p.img[set].size()!=nbytes || memcmp(data,p.img[set].data(),nbytes))continue;
        if((p.kb==KB_ICON32&&(W!=32||H!=32))||(p.kb==KB_MAP&&(W!=MAP_W||H!=MAP_H))||
           (p.kb==KB_SCREEN_TEXT&&(W!=648||H!=128))||(p.kb==KB_MANUAL&&(W!=MAN_W||H!=MAN_H)))continue;
        const int scale=std::clamp((int)std::ceil(render_scale),p.kb==KB_ICON32?2:1,4);
        const uint64_t id=((uint64_t)(i+1)<<40)|(p.hd_revision<<8)|(uint64_t)(set*8+scale);
        if(id==previous)return id;
        const auto rgba=hd_image(p,set,scale,(int)W,(int)H);
        if(rgba.size()!=(size_t)W*H*scale*scale)return 0;
        auto* out=(texpack_image*)calloc(1,sizeof(texpack_image));if(!out)return 0;
        out->w=W*scale;out->h=H*scale;out->levels=1;out->bytes=rgba.size()*4;
        out->data=(uint8_t*)malloc(out->bytes);if(!out->data){free(out);return 0;}
        for(size_t j=0;j<rgba.size();++j){out->data[j*4]=(uint8_t)rgba[j].r;out->data[j*4+1]=(uint8_t)rgba[j].g;
            out->data[j*4+2]=(uint8_t)rgba[j].b;out->data[j*4+3]=(uint8_t)rgba[j].a;}
        *image=out;return id;
    }
    return 0;
}

void on_upload(uint8_t* data, uint32_t nbytes, uint32_t off)
{
    std::lock_guard<std::mutex> l(s_mu);
    if (s_pairs.empty() || s_shown < 0) return;
    const long i = fix_image(data, nbytes, s_shown);
    if (i < 0) return;
    for (const Seen& v : s_seen) if (v.off == off) return;
    s_seen.push_back({ off, (size_t)i });
    fprintf(stderr, "[icons] image %ld on screen at 0x%08X (%u bytes)\n", i, off, nbytes);
}

/* The keyboard How to Play pages take a moment to draw, so they are drawn the
 * first time the keyboard set is wanted, off the lock. */
void build_keyboard_pages()
{
    for (size_t i = 0;; i++) {
        Pair work;
        {
            std::lock_guard<std::mutex> l(s_mu);
            while (i < s_pairs.size() && !(s_pairs[i].kb == KB_MANUAL && s_pairs[i].img[SET_KB].empty())) i++;
            if (i >= s_pairs.size()) return;
            work.img[SET_PS] = s_pairs[i].img[SET_PS];
            work.img[SET_XBOX] = s_pairs[i].img[SET_XBOX];
            work.kb = KB_MANUAL; work.action = s_pairs[i].action; work.manual_page = s_pairs[i].manual_page; work.jp_manual = s_pairs[i].jp_manual;
        }
        make_keyboard(work);
        if (work.img[SET_KB].empty()) work.img[SET_KB] = work.img[SET_PS];   /* not a page we can redo: keep PS */
        std::lock_guard<std::mutex> l(s_mu);
        s_pairs[i].img[SET_KB] = std::move(work.img[SET_KB]);
        s_pairs[i].hd_keys = std::move(work.hd_keys);
        s_pairs[i].hd_keyboard = std::move(work.hd_keyboard);
        ++s_pairs[i].hd_revision;
        for (const Seen& v : s_seen)   /* a page already on screen gets it now */
            if (v.pair == i && vm_base && s_shown == SET_KB) {
                bool wrote = false;   /* a small edit can slip past the GPU's change check */
                fix_image(vm_base + v.off, (uint32_t)s_pairs[i].img[SET_PS].size(), SET_KB, &wrote);
                if (wrote) rsx_tex_mark_dirty(v.off);
            }
        fprintf(stderr, "[icons] keyboard How to Play page %zu drawn\n", i);
    }
}

/* Follows the wanted icons and the key map, and rewrites the images already
 * on screen. */
void watcher()
{
    std::string keymap = "\x01";   /* never a real value: build on the first pass */
    for (;;) {
        const int want = wanted_set();
        const char* km = getenv("PAD_KEYMAP");
        if (want == SET_KB) build_keyboard_pages();
        {
            std::lock_guard<std::mutex> l(s_mu);
            if ((km ? km : "") != keymap) {
                const bool first = keymap == "\x01";
                keymap = km ? km : "";
                if (!first)
                    for (size_t i = 0; i < s_pairs.size(); i++) {
                        Pair& p = s_pairs[i];
                        if (p.kb == KB_NONE) continue;
                        if (p.kb == KB_MANUAL) {   /* redrawn off the lock by build_keyboard_pages */
                            if (!p.img[SET_KB].empty()) {
                                if (p.stale.size() >= 4) p.stale.erase(p.stale.begin());
                                p.stale.push_back(std::move(p.img[SET_KB]));
                            }
                            p.img[SET_KB].clear();
                            continue;
                        }
                        const std::vector<unsigned char> old = p.img[SET_KB];
                        make_keyboard(p);
                        for (const Seen& v : s_seen)   /* keycaps on screen get the new keys */
                            if (v.pair == i && vm_base && old.size() == p.img[SET_KB].size() &&
                                !memcmp(vm_base + v.off, old.data(), old.size()))
                            { memcpy(vm_base + v.off, p.img[SET_KB].data(), old.size()); rsx_tex_mark_dirty(v.off); }
                    }
            }
            if (want != s_shown) {
                s_shown = want;
                for (const Seen& v : s_seen)
                    if (vm_base) {
                        bool wrote = false;
                        fix_image(vm_base + v.off, (uint32_t)s_pairs[v.pair].img[SET_PS].size(), want, &wrote);
                        if (wrote) rsx_tex_mark_dirty(v.off);
                    }
                if (!s_seen.empty())
                    fprintf(stderr, "[icons] switched to %s icons\n",
                            want == SET_KB ? "keyboard" : want == SET_XBOX ? "Xbox" : "PlayStation");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

const char* overlay(const char* host_path, char* buf, size_t cap)
{
    std::string key(host_path);
    for (char& c : key) if (c == '\\') c = '/';
    if (key.find("/mry/mp_dat") != std::string::npos && ends_with(key, ".arc")) {
        load_map_pair(host_path, key);   /* texture swap only: the file is served as it is */
        return host_path;
    }
    bool ours = false;
    for (const Swap& s : k_swaps) ours = ours || ends_with(key, s.file);
    if (!ours) return host_path;
    load_pairs(host_path, key);
    if (wanted_set() != SET_XBOX) return host_path;

    /* <TEMP>/ps3recomp-mods/cv_xbox_icons/<language>/<area>/<file>: the
     * language folder (eng, fra, ...) keeps the copies apart. */
    size_t at = key.find("/biocv_disc/");
    if (at == std::string::npos) return host_path;
    const char* tmp = getenv("TEMP");
    std::string dst = std::string(tmp && *tmp ? tmp : ".") + "/ps3recomp-mods/cv_xbox_icons/" + key.substr(at + 12);

    /* Rebuilt on every open: these archives are small and opened rarely, and
     * a stale copy from an older build of this file can never be served. */
    if (!build_copy(host_path, dst, key)) return host_path;
    if (dst.size() + 1 > cap) return host_path;
    memcpy(buf, dst.c_str(), dst.size() + 1);
    fprintf(stderr, "[icons] Xbox icons: %s\n", key.substr(at + 12).c_str());
    return buf;
}

}  // namespace

extern "C" void cv_button_icons_init(void)
{
    vfs_overlay_set(overlay);
    g_rsx_tex_source_hook = on_upload;
    g_rsx_tex_ui_hook = on_hd_image;
    std::thread(watcher).detach();
    /* CV_ICONS_TEST_MAP=<a map .arc> builds its atlases at once (with
     * CV_ICONS_DUMP=<file> to write the Xbox and keyboard ones out), and
     * CV_ICONS_TEST_ARC=<an icon .arc> registers that archive's images, so
     * both can be checked without playing up to them. */
    if (const char* t = getenv("CV_ICONS_TEST_MAP")) {
        std::string key(t);
        for (char& c : key) if (c == '\\') c = '/';
        load_map_pair(t, key);
    }
    if (const char* t = getenv("CV_ICONS_TEST_ARC")) {
        std::string key(t);
        for (char& c : key) if (c == '\\') c = '/';
        load_pairs(t, key);
        build_keyboard_pages();
        if (const char* dump = getenv("CV_ICONS_DUMP_KB")) {
            std::lock_guard<std::mutex> l(s_mu);
            if (FILE* f = fopen(dump, "wb")) {
                for (const Pair& p : s_pairs) fwrite(p.img[SET_KB].data(), 1, p.img[SET_KB].size(), f);
                fclose(f);
            }
        }
    }
}

/* The launcher draws the same keycaps beside its key settings. */
std::wstring cv_key_cap_label(int vk) { return key_label(vk); }
const wchar_t* cv_key_cap_font(void) { gdiplus_once(); return key_font(); }
