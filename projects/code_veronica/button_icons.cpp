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

const char* overlay(const char* host_path, char* buf, size_t cap)
{
    std::string key(host_path);
    for (char& c : key) if (c == '\\') c = '/';
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
}
