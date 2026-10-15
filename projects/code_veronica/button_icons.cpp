/*
 * Xbox button icons for Code Veronica.
 *
 * The PS3 release still carries the Xbox 360 port's art beside its own:
 *   window/button_guide.arc   buifba (Cross) / buifma (Circle)   and buifma_x (A) / buifba_x (B)
 *   adv/screen_setting.arc    Sst_moji01_ps3 ("Use Triangle")    and Sst_moji01_360 ("Use Y")
 *   on_manual/on_manual.arc   On_Manual00p/01p/04p               and On_Manual00x/01x/04x
 * (Page 05 has a 360 version too, but it has no buttons on it and quotes the
 * 360 save size, 160 KB, so the PS3 page stays.)
 * The PS3 code only ever names the PlayStation entries. So, when Xbox icons
 * are wanted, the game is handed a copy of each of those archives in which
 * each PlayStation entry and its Xbox entry swap names. The game's own art is
 * shown and no game code changes.
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
 * CV_BUTTON_ICONS = auto (default: Xbox when the first controller is an Xbox
 * one), xbox, or ps. Read each time an archive is opened, which is when the
 * game loads that screen.
 */
#include "../../libs/filesystem/vfs_overlay.h"
#include "../../libs/filesystem/edat.h"
#include "../../libs/input/pad_phys.h"
#include "../../libs/video/rsx_tex_hook.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

struct Swap { const char* file; const char* ps; const char* xbox; };

const Swap k_swaps[] = {
    { "/window/button_guide.arc",   "buifba_BM_NOMIP",       "buifma_x_BM_NOMIP" },  /* Cross  -> A */
    { "/window/button_guide.arc",   "buifma_BM_NOMIP",       "buifba_x_BM_NOMIP" },  /* Circle -> B */
    { "/adv/screen_setting.arc",    "Sst_moji01_ps3",        "Sst_moji01_360" },
    { "/on_manual/on_manual.arc",   "On_Manual00p_BM_NOMIP", "On_Manual00x_BM_NOMIP" },
    { "/on_manual/on_manual.arc",   "On_Manual01p_BM_NOMIP", "On_Manual01x_BM_NOMIP" },
    { "/on_manual/on_manual.arc",   "On_Manual04p_BM_NOMIP", "On_Manual04x_BM_NOMIP" },
};

bool want_xbox()
{
    const char* m = getenv("CV_BUTTON_ICONS");
    if (m && !strcmp(m, "xbox")) return true;
    if (m && !strcmp(m, "ps")) return false;
    PadPhys p;
    return pad_phys_poll(0, &p) && p.kind == PAD_KIND_XINPUT;
}

bool ends_with(const std::string& s, const char* tail)
{
    size_t n = strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

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

/* Write the Xbox copy of `src` to `dst`. False if the archive is not the
 * expected one (then the original is opened). */
bool build_copy(const std::string& src, const std::string& dst, const std::string& key)
{
    FILE* f = fopen(src.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size > 0) { d.resize((size_t)size); if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
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
    f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = (fclose(f) == 0) && ok;
    if (ok) { fs::remove(dst, ec); fs::rename(tmp, dst, ec); ok = !ec; }
    if (!ok) fs::remove(tmp, ec);
    return ok;
}

/* ---- live switching --------------------------------------------------
 * The archive copy above decides what a screen shows when it loads. To
 * switch while the game runs, the same images are swapped where the GPU reads
 * them: each PlayStation/Xbox image pair is read from the original archive,
 * the renderer's upload hook (rsx_tex_hook.h) recognises either image by its
 * bytes and writes the wanted one in its place, and a watcher rewrites the
 * ones already seen whenever the wanted set changes (the launcher option, or
 * the controller in auto mode). The renderer notices the changed bytes and
 * uploads them again. */
struct Pair { std::vector<unsigned char> ps, xbox; };
struct Seen { uint32_t off; size_t pair; };

std::mutex s_mu;
std::vector<Pair> s_pairs;
std::set<std::string> s_loaded;    /* archives whose pairs are registered */
std::vector<Seen> s_seen;          /* guest offsets showing one of the pairs */
int s_shown = -1;                  /* 1 Xbox, 0 PlayStation: what s_seen hold */

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

void load_pairs(const std::string& path, const std::string& key)
{
    {
        std::lock_guard<std::mutex> l(s_mu);
        if (!s_loaded.insert(path).second) return;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    std::vector<unsigned char> d;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size > 0) { d.resize((size_t)size); if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
    const std::vector<unsigned char> s = unblock(d);
    int added = 0;
    for (const Swap& sw : k_swaps) {
        if (!ends_with(key, sw.file)) continue;
        Pair p{ tex_bytes(s, sw.ps), tex_bytes(s, sw.xbox) };
        if (p.ps.empty() || p.ps.size() != p.xbox.size() || p.ps == p.xbox) continue;
        std::lock_guard<std::mutex> l(s_mu);
        s_pairs.push_back(std::move(p));
        added++;
    }
    fprintf(stderr, "[icons] %d image pair(s) from %s\n", added, path.c_str());
}

/* Make the image at `data` the wanted one if it is either half of a pair. */
void fix_image(unsigned char* data, uint32_t nbytes, uint32_t off, bool xbox, bool remember)
{
    for (size_t i = 0; i < s_pairs.size(); i++) {
        const Pair& p = s_pairs[i];
        if (p.ps.size() != nbytes) continue;
        const bool is_ps = !memcmp(data, p.ps.data(), nbytes);
        if (!is_ps && memcmp(data, p.xbox.data(), nbytes) != 0) continue;
        if (is_ps == xbox) memcpy(data, xbox ? p.xbox.data() : p.ps.data(), nbytes);
        if (remember) {
            bool known = false;
            for (const Seen& v : s_seen) known = known || v.off == off;
            if (!known) {
                s_seen.push_back({ off, i });
                fprintf(stderr, "[icons] image %zu on screen at 0x%08X (%u bytes)\n", i, off, nbytes);
            }
        }
        return;
    }
}

void on_upload(uint8_t* data, uint32_t nbytes, uint32_t off)
{
    std::lock_guard<std::mutex> l(s_mu);
    if (s_pairs.empty() || s_shown < 0) return;
    fix_image(data, nbytes, off, s_shown == 1, true);
}

/* Follows the wanted icons and rewrites the images already on screen. */
void watcher()
{
    for (;;) {
        const int want = want_xbox() ? 1 : 0;
        {
            std::lock_guard<std::mutex> l(s_mu);
            if (want != s_shown) {
                s_shown = want;
                for (const Seen& v : s_seen)
                    if (vm_base) fix_image(vm_base + v.off, (uint32_t)s_pairs[v.pair].ps.size(), v.off, want == 1, false);
                if (!s_seen.empty())
                    fprintf(stderr, "[icons] switched to %s icons\n", want ? "Xbox" : "PlayStation");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

/* ---- the map screen -----------------------------------------------------
 * The map's atlas (mry/mp_dat0..4.arc, one per playable character) has the
 * PlayStation icons and "L1 ZOOM R1" painted into it, and unlike the screens
 * above it has no Xbox version in the game. One is drawn here, from the
 * user's own files, the first time a map atlas is opened: Circle becomes B,
 * Square becomes X (the Cross icon itself, a blue X), Cross becomes A, all on
 * the map's grey disc; the B and A letters come from the button guide's Xbox
 * icons. The zoom labels become LT / RT, where the 360 layout puts zoom.
 * Geometry checked for every language and character (only DXT noise differs). */
struct Rgba { float r, g, b, a; };
const int MAP_W = 192, MAP_H = 192;
const int CIRCLE_X = 159, CIRCLE_Y = 26, SQUARE_X = 159, SQUARE_Y = 62, CROSS_X = 159, CROSS_Y = 98;

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

/* Bounding-box DXT5 encoder: plenty for a few icon blocks. */
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
    /* colour: the box of the visible texels (all of them if none is) */
    float lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
    bool any = false;
    for (int pass = 0; pass < 2 && !any; pass++)
        for (int i = 0; i < 16; i++) {
            if (pass == 0 && p[i].a < 16) continue;
            const float v[3] = { p[i].r, p[i].g, p[i].b };
            for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
            any = true;
        }
    unsigned c0 = pack565(hi), c1 = pack565(lo);
    if (c0 < c1) std::swap(c0, c1);
    float c[4][3];
    unpack565(c0, c[0]); unpack565(c1, c[1]);
    for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
    uint32_t ci = 0;
    if (c0 != c1)
        for (int i = 0; i < 16; i++) {
            int best = 0; float bd = 1e30f;
            for (int k = 0; k < 4; k++) {
                const float dr = c[k][0] - p[i].r, dg = c[k][1] - p[i].g, db = c[k][2] - p[i].b;
                const float dd = dr * dr + dg * dg + db * db;
                if (dd < bd) { bd = dd; best = k; }
            }
            ci |= (uint32_t)best << (2 * i);
        }
    b[8] = (unsigned char)c0; b[9] = (unsigned char)(c0 >> 8);
    b[10] = (unsigned char)c1; b[11] = (unsigned char)(c1 >> 8);
    for (int i = 0; i < 4; i++) b[12 + i] = (unsigned char)(ci >> (8 * i));
}

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

/* The Xbox version of a map atlas (DXT5, MAP_W x MAP_H), or empty. */
std::vector<unsigned char> xbox_map_atlas(const std::vector<unsigned char>& dxt,
                                          const std::vector<unsigned char>& a_icon,
                                          const std::vector<unsigned char>& b_icon)
{
    if (dxt.size() != (size_t)MAP_W * MAP_H || a_icon.size() != 1024 || b_icon.size() != 1024) return {};
    std::vector<Rgba> img = dxt5_decode(dxt.data(), MAP_W, MAP_H);
    const std::vector<Rgba> orig = img;
    auto at = [&](int x, int y) -> Rgba& { return img[(size_t)y * MAP_W + x]; };
    auto cell = [&](int x0, int y0) {
        std::vector<Rgba> c(32 * 32);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) c[(size_t)y * 32 + x] = orig[(size_t)(y0 + y) * MAP_W + x0 + x];
        return c;
    };
    const std::vector<Rgba> circle = cell(CIRCLE_X, CIRCLE_Y), square = cell(SQUARE_X, SQUARE_Y), cross = cell(CROSS_X, CROSS_Y);
    /* A clean disc: per texel the greyest of the three icons; texels still
     * coloured take the mean grey at their radius. */
    std::vector<Rgba> disc(32 * 32);
    for (size_t i = 0; i < disc.size(); i++) {
        const Rgba* c3[3] = { &circle[i], &square[i], &cross[i] };
        disc[i] = *std::min_element(c3, c3 + 3, [](const Rgba* a, const Rgba* b) { return sat(*a) < sat(*b); })[0];
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
    const std::vector<Rgba> a = paint_letter(disc, letter_mask(dxt5_decode(a_icon.data(), 32, 32)), A_COL);
    const std::vector<Rgba> b = paint_letter(disc, letter_mask(dxt5_decode(b_icon.data(), 32, 32)), B_COL);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            at(SQUARE_X + x, SQUARE_Y + y) = cross[(size_t)y * 32 + x];   /* Square -> X */
            at(CROSS_X + x, CROSS_Y + y) = a[(size_t)y * 32 + x];         /* Cross  -> A */
            at(CIRCLE_X + x, CIRCLE_Y + y) = b[(size_t)y * 32 + x];       /* Circle -> B */
        }
    /* "L1" / "R1" -> "LT" / "RT": erase the 1 and draw a T in the label grey,
     * 2-px strokes like the font's, with a soft third pixel. */
    const Rgba grey = at(12, 106);
    if (grey.a > 200) {
        auto put = [&](int x, int y, float a) { Rgba& c = at(x, y); c = { grey.r, grey.g, grey.b, std::max(c.a, a) }; };
        auto one_to_t = [&](int x0, int x1, int stem, bool keep_foot) {
            for (int y = 99; y <= 116; y++)
                for (int x = x0; x <= x1; x++)
                    if (!(keep_foot && x == x0 && y >= 114)) at(x, y).a = 0;
            for (int x = x0; x <= x1 + 2; x++) { put(x, 99, 255); put(x, 100, 255); put(x, 101, 110); }
            for (int y = 99; y <= 116; y++) { put(stem, y, 255); put(stem + 1, y, 255); put(stem + 2, y, 110); }
        };
        one_to_t(9, 14, 12, true);      /* L1: the L's foot reaches x 9 on rows 114-116 */
        one_to_t(118, 124, 121, false); /* R1 */
    }
    /* Re-encode only the blocks that changed; the rest keep their bytes. */
    std::vector<unsigned char> out = dxt;
    for (int by = 0; by < MAP_H / 4; by++)
        for (int bx = 0; bx < MAP_W / 4; bx++) {
            Rgba blk[16]; bool changed = false;
            for (int i = 0; i < 16; i++) {
                const size_t k = (size_t)(by * 4 + i / 4) * MAP_W + bx * 4 + i % 4;
                blk[i] = img[k];
                changed = changed || memcmp(&img[k], &orig[k], sizeof(Rgba)) != 0;
            }
            if (changed) dxt5_encode_block(blk, &out[((size_t)by * (MAP_W / 4) + bx) * 16]);
        }
    return out;
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

/* A map atlas was opened: register its PlayStation/Xbox pair. */
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
    Pair p{ tex_bytes(s, "tex0000_BM"), {} };
    p.xbox = xbox_map_atlas(p.ps, tex_bytes(g, "buifma_x_BM_NOMIP"), tex_bytes(g, "buifba_x_BM_NOMIP"));
    if (p.xbox.empty()) { fprintf(stderr, "[icons] %s: not the expected map atlas\n", path.c_str()); return; }
    if (const char* dump = getenv("CV_ICONS_DUMP")) {   /* the generated atlas, raw DXT5, for checking */
        if (FILE* f = fopen(dump, "wb")) { fwrite(p.xbox.data(), 1, p.xbox.size(), f); fclose(f); }
    }
    std::lock_guard<std::mutex> l(s_mu);
    s_pairs.push_back(std::move(p));
    fprintf(stderr, "[icons] map atlas pair from %s\n", path.c_str());
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
    if (!want_xbox()) return host_path;

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
    std::thread(watcher).detach();
    /* CV_ICONS_TEST_MAP=<a map .arc> builds its Xbox atlas at once (with
     * CV_ICONS_DUMP=<file> to write it out), so it can be checked without
     * playing up to the map. */
    if (const char* t = getenv("CV_ICONS_TEST_MAP")) {
        std::string key(t);
        for (char& c : key) if (c == '\\') c = '/';
        load_map_pair(t, key);
    }
}
