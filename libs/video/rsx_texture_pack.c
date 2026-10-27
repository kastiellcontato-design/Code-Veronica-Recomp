/* rsx_texture_pack.c -- see rsx_texture_pack.h.
 *
 * Three background jobs, all off the render thread: listing the mods folder
 * (one thread, once), decoding replacement PNGs with their mip chains (two
 * threads, on demand), and writing dumped textures as PNG (one thread). The
 * render thread only hashes, looks up and uploads (rsx_d3d12_backend.c).
 */
#include "rsx_texture_pack.h"
#include "rsx_texture_dds.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../codec/stb_image.h"
#include "../codec/stb_image_write.h"

#define PATH_CAP 1024
#define READY_CAP_BYTES  (768ull << 20)   /* decoded, waiting for the render thread */
#define DUMP_CAP_BYTES   (256ull << 20)   /* waiting to be written */

/* ---- switches and folders ------------------------------------------------ */
static volatile LONG s_rep_on, s_dump_on, s_user_rep_on;
static volatile ULONGLONG s_flags_at;
static void flags_refresh(void)
{
    const ULONGLONG now = GetTickCount64();
    if (s_flags_at && now - s_flags_at < 500) return;
    s_flags_at = now;
    const char* r = getenv("TEXTURE_REPLACE");
    const char* d = getenv("TEXTURE_DUMP");
    const char* translation=getenv("CV_PTBR_TEXTURE_DIR");
    s_user_rep_on = r && r[0] == '1';
    s_rep_on = s_user_rep_on || (translation && *translation);
    s_dump_on = d && d[0] == '1';
}
int texpack_replace_on(void) { flags_refresh(); return s_rep_on; }
void texpack_flags_changed(void) { s_flags_at = 0; }
int texpack_dump_on(void) { flags_refresh(); return s_dump_on; }

static wchar_t s_dir[PATH_CAP], s_root[PATH_CAP], s_dump_dir[PATH_CAP], s_ptbr_dir[PATH_CAP];
/* Replacement paths are immutable for one game session. The dump override
 * can change live, protected by the same lock as the dump queue and hash set. */
static wchar_t s_dump_override[PATH_CAP];
static SRWLOCK s_dlock = SRWLOCK_INIT;
static volatile LONG s_dump_generation = 1;
static INIT_ONCE s_paths_once = INIT_ONCE_STATIC_INIT;
static int is_dir(const wchar_t* p)
{
    const DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
/* A folder from the environment: UTF-8 as the launcher sets it in this
 * process, or, set from outside (a shortcut, a script), the wide value --
 * the narrow one is then in the ANSI code page and loses any accent. */
static int env_path(const char* name, const wchar_t* wname, wchar_t* out)
{
    const char* v = getenv(name);
    if (v && *v && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v, -1, out, PATH_CAP)) return 1;
    const wchar_t* w = _wgetenv(wname);
    if (!w || !*w) return 0;
    wcsncpy(out, w, PATH_CAP - 1); out[PATH_CAP - 1] = 0;
    return 1;
}
static BOOL CALLBACK paths_init(PINIT_ONCE once, PVOID arg, PVOID* ctx)
{
    (void)once; (void)arg; (void)ctx;
    if (!env_path("TEXTURE_DIR", L"TEXTURE_DIR", s_dir)) {
        GetModuleFileNameW(NULL, s_dir, PATH_CAP);
        wchar_t* sl = wcsrchr(s_dir, L'\\');
        if (sl) sl[1] = 0;
        wcsncat(s_dir, L"textures", PATH_CAP - wcslen(s_dir) - 1);
    }
    for (size_t n = wcslen(s_dir); n > 3 && (s_dir[n - 1] == L'\\' || s_dir[n - 1] == L'/'); n--) s_dir[n - 1] = 0;
    env_path("CV_PTBR_TEXTURE_DIR",L"CV_PTBR_TEXTURE_DIR",s_ptbr_dir);
    wchar_t code[32] = L"";
    const char* g = getenv("TEXTURE_GAME");
    if (g && *g) MultiByteToWideChar(CP_UTF8, 0, g, -1, code, 32);
    /* One folder can hold a pack per version: <dir>\<code> when it exists. */
    swprintf(s_root, PATH_CAP, L"%ls\\%ls", s_dir, code);
    if (!code[0] || !is_dir(s_root)) wcscpy(s_root, s_dir);
    /* Dumps: TEXTURE_DUMP_DIR when chosen, else <dir>\<code>\dump. */
    if (!env_path("TEXTURE_DUMP_DIR", L"TEXTURE_DUMP_DIR", s_dump_dir)) {
        if (code[0]) swprintf(s_dump_dir, PATH_CAP, L"%ls\\%ls\\dump", s_dir, code);
        else swprintf(s_dump_dir, PATH_CAP, L"%ls\\dump", s_dir);
    }
    for (size_t n = wcslen(s_dump_dir); n > 3 && (s_dump_dir[n - 1] == L'\\' || s_dump_dir[n - 1] == L'/'); n--) s_dump_dir[n - 1] = 0;
    return TRUE;
}
static void paths(void) { InitOnceExecuteOnce(&s_paths_once, paths_init, NULL, NULL); }
static void mkdirs(const wchar_t* path)
{
    wchar_t p[PATH_CAP];
    wcsncpy(p, path, PATH_CAP - 1); p[PATH_CAP - 1] = 0;
    for (wchar_t* c = p + 3; *c; c++)
        if (*c == L'\\' || *c == L'/') { const wchar_t k = *c; *c = 0; CreateDirectoryW(p, NULL); *c = k; }
    CreateDirectoryW(p, NULL);
}

/* ---- the hash ------------------------------------------------------------ */
static uint64_t rotl64(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }
uint64_t texpack_hash(const void* p, size_t n, uint32_t w, uint32_t h, uint32_t fmt)
{
    const uint8_t* b = (const uint8_t*)p;
    uint64_t x = 0x9E3779B97F4A7C15ull ^ ((uint64_t)n * 0xC2B2AE3D27D4EB4Full)
               ^ (((uint64_t)w << 40) | ((uint64_t)h << 16) | fmt);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v; memcpy(&v, b + i, 8);
        x ^= rotl64(v * 0x87C37B91114253D5ull, 31) * 0x4CF5AD432745937Full;
        x = rotl64(x, 27) * 5 + 0x52DCE729;
    }
    for (; i < n; i++) { x ^= b[i] * 0x9E3779B97F4A7C15ull; x = rotl64(x, 11) * 0x87C37B91114253D5ull; }
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull; x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull; x ^= x >> 33;
    return x ? x : 1;   /* 0 marks an empty table slot */
}

/* ---- a set of hashes (open addressing, 0 empty) --------------------------- */
typedef struct { uint64_t* k; uint32_t cap, n; } HashSet;
static uint32_t slot_of(uint64_t h, uint32_t cap) { return (uint32_t)(h >> 20) & (cap - 1); }
static int set_has(const HashSet* s, uint64_t h)
{
    if (!s->cap) return 0;
    for (uint32_t i = slot_of(h, s->cap);; i = (i + 1) & (s->cap - 1)) {
        if (!s->k[i]) return 0;
        if (s->k[i] == h) return 1;
    }
}
static int set_add(HashSet* s, uint64_t h)   /* 1 if new */
{
    if ((s->n + 1) * 2 > s->cap) {
        const uint32_t cap = s->cap ? s->cap * 2 : 4096;
        uint64_t* k = (uint64_t*)calloc(cap, sizeof *k);
        if (!k) return 0;
        for (uint32_t i = 0; i < s->cap; i++)
            if (s->k[i]) { uint32_t j = slot_of(s->k[i], cap); while (k[j]) j = (j + 1) & (cap - 1); k[j] = s->k[i]; }
        free(s->k); s->k = k; s->cap = cap;
    }
    uint32_t i = slot_of(h, s->cap);
    while (s->k[i]) { if (s->k[i] == h) return 0; i = (i + 1) & (s->cap - 1); }
    s->k[i] = h; s->n++;
    return 1;
}

/* A file name that starts with 16 hex digits names that hash. */
static int name_hash(const wchar_t* name, uint64_t* out)
{
    if (wcslen(name) < 16) return 0;
    uint64_t h = 0;
    for (int i = 0; i < 16; i++) {
        const wchar_t c = name[i];
        const int v = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10
                    : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
        if (v < 0) return 0;
        h = h << 4 | (uint64_t)v;
    }
    const wchar_t e = name[16];
    if ((e >= L'0' && e <= L'9') || (e >= L'a' && e <= L'f') || (e >= L'A' && e <= L'F')) return 0;
    *out = h ? h : 1;
    return 1;
}
static int is_dds(const wchar_t* name) { const size_t n=wcslen(name); return n>4&&!_wcsicmp(name+n-4,L".dds"); }
static int is_png(const wchar_t* name)
{
    const size_t n = wcslen(name);
    return n > 4 && !_wcsicmp(name + n - 4, L".png");
}

/* ---- the list of replacements ---------------------------------------------- */
typedef struct { uint64_t hash; wchar_t* path; } IdxEnt;
static IdxEnt* s_idx; static uint32_t s_idx_cap, s_idx_n;
static volatile LONG s_idx_state;   /* 0 not started, 1 listing, 2 ready */
static const IdxEnt* idx_find(uint64_t h)
{
    if (!s_idx_cap) return NULL;
    for (uint32_t i = slot_of(h, s_idx_cap);; i = (i + 1) & (s_idx_cap - 1)) {
        if (!s_idx[i].hash) return NULL;
        if (s_idx[i].hash == h) return &s_idx[i];
    }
}
static int translation_path(const wchar_t* path)
{
    const size_t n=wcslen(s_ptbr_dir);
    return n && !_wcsnicmp(path,s_ptbr_dir,n) && (path[n]==L'\\'||path[n]==L'/');
}
static void idx_add(uint64_t h, const wchar_t* path)
{
    if ((s_idx_n + 1) * 2 > s_idx_cap) {
        const uint32_t cap = s_idx_cap ? s_idx_cap * 2 : 4096;
        IdxEnt* t = (IdxEnt*)calloc(cap, sizeof *t);
        if (!t) return;
        for (uint32_t i = 0; i < s_idx_cap; i++)
            if (s_idx[i].hash) { uint32_t j = slot_of(s_idx[i].hash, cap); while (t[j].hash) j = (j + 1) & (cap - 1); t[j] = s_idx[i]; }
        free(s_idx); s_idx = t; s_idx_cap = cap;
    }
    uint32_t i = slot_of(h, s_idx_cap);
    while (s_idx[i].hash) {
        if (s_idx[i].hash == h) {
            /* DDS wins over PNG; equal types use a stable lexical path. */
            const int newer=is_dds(path), older=is_dds(s_idx[i].path);
            const int ptbr=translation_path(path), old_ptbr=translation_path(s_idx[i].path);
            if(ptbr>old_ptbr||(ptbr==old_ptbr&&(newer>older||(newer==older&&_wcsicmp(path,s_idx[i].path)<0)))) {
                wchar_t* copy=_wcsdup(path);if(copy){free(s_idx[i].path);s_idx[i].path=copy;}
            }
            return;
        }
        i = (i + 1) & (s_idx_cap - 1);
    }
    s_idx[i].hash = h; s_idx[i].path = _wcsdup(path);
    if (s_idx[i].path) s_idx_n++; else s_idx[i].hash = 0;
}
static int is_dump_directory(const wchar_t* directory)
{
    AcquireSRWLockShared(&s_dlock);
    const int excluded = !_wcsicmp(directory, s_dump_dir) ||
        (s_dump_override[0] && !_wcsicmp(directory, s_dump_override));
    ReleaseSRWLockShared(&s_dlock);
    return excluded;
}
static void scan_dir(const wchar_t* dir, int depth)
{
    if (is_dump_directory(dir)) return;
    wchar_t pat[PATH_CAP];
    if (depth > 16 || swprintf(pat, PATH_CAP, L"%ls\\*", dir) < 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const wchar_t* n = fd.cFileName;
        if (!wcscmp(n, L".") || !wcscmp(n, L"..")) continue;
        wchar_t full[PATH_CAP];
        if (swprintf(full, PATH_CAP, L"%ls\\%ls", dir, n) < 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (_wcsicmp(n, L"dump") && !is_dump_directory(full))   /* never the textures being dumped */
                scan_dir(full, depth + 1);
        } else {
            uint64_t hh;
            if ((is_png(n)||is_dds(n)) && name_hash(n, &hh)) idx_add(hh, full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static DWORD WINAPI idx_thread(LPVOID arg)
{
    (void)arg;
    const ULONGLONG t0 = GetTickCount64();
    scan_dir(s_root, 0);
    if(s_ptbr_dir[0] && _wcsicmp(s_ptbr_dir,s_root))scan_dir(s_ptbr_dir,0);
    InterlockedExchange(&s_idx_state, 2);
    fprintf(stderr, "[TEXPACK] %u replacement textures listed in %ls (%.1f s)%c",
            s_idx_n, s_root, (GetTickCount64() - t0) / 1000.0, 10);
    return 0;
}
int texpack_lookup(uint64_t hash)
{
    if (s_idx_state == 0 && InterlockedCompareExchange(&s_idx_state, 1, 0) == 0) {
        paths();
        HANDLE th = CreateThread(NULL, 0, idx_thread, NULL, 0, NULL);
        if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); }
        else InterlockedExchange(&s_idx_state, 2);
    }
    if (s_idx_state != 2) return -1;
    const IdxEnt* found=idx_find(hash);
    return found && (s_user_rep_on || translation_path(found->path)) ? 1 : 0;
}

/* ---- decoding replacements ------------------------------------------------- */
typedef struct Job { struct Job* next; uint64_t hash; texpack_image* img; int failed; } Job;
static SRWLOCK s_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_cv = CONDITION_VARIABLE_INIT;
static Job *s_q_head, *s_q_tail, *s_ready;
static volatile LONG64 s_ready_bytes;
static volatile LONG s_workers;

static uint8_t* read_file(const wchar_t* path, size_t* n)
{
    FILE* f = _wfopen(path, L"rb");
    if (!f) return NULL;
    uint8_t* b = NULL;
    if (!_fseeki64(f, 0, SEEK_END)) {
        const long long sz = _ftelli64(f);
        _fseeki64(f, 0, SEEK_SET);
        if (sz > 0 && sz <= (512ll << 20) && (b = (uint8_t*)malloc((size_t)sz)) != NULL &&
            fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
        if (b) *n = (size_t)sz;
    }
    fclose(f);
    return b;
}
/* The PNG as RGBA8 with every mip level below it (2x2 box filter), so a
 * replacement larger than the original still looks right far away. */
static texpack_image* decode_png(const wchar_t* path)
{
    size_t n = 0;
    uint8_t* file = read_file(path, &n);
    if (!file) return NULL;
    int w = 0, h = 0, c = 0;
    uint8_t* px = stbi_load_from_memory(file, (int)n, &w, &h, &c, 4);
    free(file);
    if (!px) return NULL;
    texpack_image* im = NULL;
    if (w > 0 && h > 0 && w <= 16384 && h <= 16384 && (im = (texpack_image*)calloc(1, sizeof *im)) != NULL) {
        im->format=28;
        im->w = (uint32_t)w; im->h = (uint32_t)h;
        size_t total = 0;
        for (uint32_t lw = im->w, lh = im->h; im->levels < 16; ) {
            im->row_bytes[im->levels]=lw*4; im->rows[im->levels]=lh;
            im->level_off[im->levels++] = total;
            total += (size_t)lw * lh * 4;
            if (lw == 1 && lh == 1) break;
            lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
        }
        im->bytes = total;
        if ((im->data = (uint8_t*)malloc(total)) == NULL) { free(im); im = NULL; }
        else {
            memcpy(im->data, px, (size_t)w * h * 4);
            uint32_t pw = im->w, ph = im->h;
            for (uint32_t l = 1; l < im->levels; l++) {
                const uint8_t* s = im->data + im->level_off[l - 1];
                uint8_t* d = im->data + im->level_off[l];
                const uint32_t nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
                for (uint32_t y = 0; y < nh; y++)
                    for (uint32_t x = 0; x < nw; x++) {
                        const uint32_t x0 = x * 2, y0 = y * 2;
                        const uint32_t x1 = x0 + 1 < pw ? x0 + 1 : x0, y1 = y0 + 1 < ph ? y0 + 1 : y0;
                        for (int k = 0; k < 4; k++)
                            d[((size_t)y * nw + x) * 4 + k] = (uint8_t)((s[((size_t)y0 * pw + x0) * 4 + k] + s[((size_t)y0 * pw + x1) * 4 + k] +
                                                                        s[((size_t)y1 * pw + x0) * 4 + k] + s[((size_t)y1 * pw + x1) * 4 + k] + 2) / 4);
                    }
                pw = nw; ph = nh;
            }
        }
    }
    stbi_image_free(px);
    return im;
}
static DWORD WINAPI decode_thread(LPVOID arg)
{
    (void)arg;
    for (;;) {
        AcquireSRWLockExclusive(&s_lock);
        while (!s_q_head) SleepConditionVariableSRW(&s_cv, &s_lock, INFINITE, 0);
        Job* j = s_q_head;
        s_q_head = j->next; if (!s_q_head) s_q_tail = NULL;
        ReleaseSRWLockExclusive(&s_lock);
        /* The render thread takes a few a frame: hold back while plenty wait. */
        while (s_ready_bytes > (LONG64)READY_CAP_BYTES) Sleep(5);
        const IdxEnt* e = idx_find(j->hash);
        j->img=NULL;
        if(e) {
            if(is_dds(e->path)) { size_t n=0;uint8_t* file=read_file(e->path,&n);j->img=texpack_dds_parse(file,n);free(file); }
            else j->img=decode_png(e->path);
        }
        j->failed = !j->img;
        if (j->failed && e) fprintf(stderr, "[TEXPACK] could not read %ls%c", e->path, 10);
        AcquireSRWLockExclusive(&s_lock);
        if (j->img) s_ready_bytes += (LONG64)j->img->bytes;
        j->next = s_ready; s_ready = j;
        ReleaseSRWLockExclusive(&s_lock);
    }
}
void texpack_request(uint64_t hash)
{
    if (s_idx_state != 2) return;
    if (!s_workers && InterlockedCompareExchange(&s_workers, 1, 0) == 0)
        for (int i = 0; i < 2; i++) {
            HANDLE th = CreateThread(NULL, 0, decode_thread, NULL, 0, NULL);
            if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); }
        }
    Job* j = (Job*)calloc(1, sizeof *j);
    if (!j) return;
    j->hash = hash;
    AcquireSRWLockExclusive(&s_lock);
    if (s_q_tail) s_q_tail->next = j; else s_q_head = j;
    s_q_tail = j;
    ReleaseSRWLockExclusive(&s_lock);
    WakeConditionVariable(&s_cv);
}
int texpack_next(uint64_t* hash, texpack_image** img)
{
    if (!s_ready) return 0;
    AcquireSRWLockExclusive(&s_lock);
    Job* j = s_ready;
    if (j) { s_ready = j->next; if (j->img) s_ready_bytes -= (LONG64)j->img->bytes; }
    ReleaseSRWLockExclusive(&s_lock);
    if (!j) return 0;
    *hash = j->hash; *img = j->img;
    const int r = j->failed ? -1 : 1;
    free(j);
    return r;
}
void texpack_free(texpack_image* img)
{
    if (!img) return;
    free(img->data);
    free(img);
}

/* ---- dumping ----------------------------------------------------------------- */
typedef struct DumpJob { struct DumpJob* next; uint64_t hash; uint32_t w, h, format, dds; size_t bytes; char name[8]; uint8_t* rgba; wchar_t directory[PATH_CAP]; } DumpJob;
static CONDITION_VARIABLE s_dcv = CONDITION_VARIABLE_INIT;
static DumpJob *s_d_head, *s_d_tail;
static volatile LONG64 s_d_bytes;
static HashSet s_dumped;
static int s_dumped_listed;
static int s_dump_dds;
int texpack_dump_is_dds(void){AcquireSRWLockShared(&s_dlock);int d=s_dump_dds;ReleaseSRWLockShared(&s_dlock);return d;}
void texpack_set_dump_format(int dds){
    AcquireSRWLockExclusive(&s_dlock);
    if(s_dump_dds!=!!dds){s_dump_dds=!!dds;free(s_dumped.k);memset(&s_dumped,0,sizeof s_dumped);s_dumped_listed=0;InterlockedIncrement(&s_dump_generation);}
    ReleaseSRWLockExclusive(&s_dlock);
}
static int s_dump_thread;

int texpack_set_dump_directory(const wchar_t* directory)
{
    if (!directory || !*directory || wcslen(directory) >= PATH_CAP) return 0;
    wchar_t normalized[PATH_CAP];
    wcscpy(normalized, directory);
    for(wchar_t* c=normalized;*c;c++)if(*c==L'/')*c=L'\\';
    for (size_t n = wcslen(normalized); n > 3 && (normalized[n-1] == L'\\' || normalized[n-1] == L'/'); n--)
        normalized[n-1] = 0;
    AcquireSRWLockExclusive(&s_dlock);
    if (_wcsicmp(s_dump_override, normalized)) {
        wcscpy(s_dump_override, normalized);
        free(s_dumped.k); memset(&s_dumped, 0, sizeof s_dumped);
        s_dumped_listed = 0;
        InterlockedIncrement(&s_dump_generation);
    }
    ReleaseSRWLockExclusive(&s_dlock);
    /* Do not initialize replacement paths here: the launcher may still be
     * selecting its game or mod folder before the renderer first uses it. */
    return 1;
}
uint32_t texpack_dump_generation(void)
{
    return (uint32_t)InterlockedCompareExchange(&s_dump_generation, 0, 0);
}

static void png_write(void* ctx, void* data, int size) { fwrite(data, 1, (size_t)size, (FILE*)ctx); }
static DWORD WINAPI dump_thread(LPVOID arg)
{
    (void)arg;
    for (;;) {
        AcquireSRWLockExclusive(&s_dlock);
        while (!s_d_head) SleepConditionVariableSRW(&s_dcv, &s_dlock, INFINITE, 0);
        DumpJob* j = s_d_head;
        s_d_head = j->next; if (!s_d_head) s_d_tail = NULL;
        ReleaseSRWLockExclusive(&s_dlock);
        mkdirs(j->directory);
        wchar_t path[PATH_CAP], tmp[PATH_CAP + 8];
        swprintf(path, PATH_CAP, L"%ls\\%016llX_%ux%u_%hs.%ls", j->directory, (unsigned long long)j->hash, j->w, j->h, j->name,j->dds?L"dds":L"png");
        swprintf(tmp, PATH_CAP + 8, L"%ls.tmp", path);
        FILE* f = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? _wfopen(tmp, L"wb") : NULL;
        if (f) {
            /* written under a temporary name: a game closed mid-write leaves no half PNG */
            int ok=0;
            if(j->dds){uint8_t header[128];ok=texpack_dds_header(header,j->w,j->h,1,j->format)&&fwrite(header,1,128,f)==128&&fwrite(j->rgba,1,j->bytes,f)==j->bytes;}
            else ok=stbi_write_png_to_func(png_write, f, (int)j->w, (int)j->h, 4, j->rgba, (int)j->w * 4);
            if (fclose(f) || !ok || !MoveFileExW(tmp, path, 0)) DeleteFileW(tmp);
        }
        InterlockedAdd64(&s_d_bytes, -(LONG64)j->bytes);
        free(j->rgba);
        free(j);
    }
}
int texpack_dump_wanted(uint64_t hash)
{
    paths();
    AcquireSRWLockExclusive(&s_dlock);
    if (!s_dumped_listed) {   /* what earlier sessions wrote is not written again */
        s_dumped_listed = 1;
        wchar_t pat[PATH_CAP];
        swprintf(pat, PATH_CAP, L"%ls\\*.%ls", s_dump_override[0] ? s_dump_override : s_dump_dir,s_dump_dds?L"dds":L"png");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
        if (h != INVALID_HANDLE_VALUE) {
            do { uint64_t hh; if (name_hash(fd.cFileName, &hh)) set_add(&s_dumped, hh); } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    const int fresh = !set_has(&s_dumped, hash) && set_add(&s_dumped, hash);
    ReleaseSRWLockExclusive(&s_dlock);
    return fresh;
}
static void dump_enqueue(uint64_t hash,uint32_t w,uint32_t h,const char* fmt_name,uint8_t* rgba,size_t bytes,uint32_t format,int force_dds)
{
    if (!rgba) return;
    paths();
    if (s_d_bytes + (LONG64)bytes > (LONG64)DUMP_CAP_BYTES) { free(rgba); return; }   /* the disk is behind: skip */
    DumpJob* j = (DumpJob*)calloc(1, sizeof *j);
    if (!j) { free(rgba); return; }
    j->hash = hash; j->w = w; j->h = h; j->rgba = rgba;j->bytes=bytes;j->format=format;
    strncpy(j->name, fmt_name ? fmt_name : "RGBA", sizeof j->name - 1);
    InterlockedAdd64(&s_d_bytes, (LONG64)bytes);
    AcquireSRWLockExclusive(&s_dlock);
    j->dds=force_dds||s_dump_dds;
    wcscpy(j->directory, s_dump_override[0] ? s_dump_override : s_dump_dir);
    if (!s_dump_thread) {
        s_dump_thread = 1;
        HANDLE th = CreateThread(NULL, 0, dump_thread, NULL, 0, NULL);
        if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); }
    }
    if (s_d_tail) s_d_tail->next = j; else s_d_head = j;
    s_d_tail = j;
    ReleaseSRWLockExclusive(&s_dlock);
    WakeConditionVariable(&s_dcv);
}

void texpack_dump(uint64_t hash,uint32_t w,uint32_t h,const char* name,uint8_t* rgba){dump_enqueue(hash,w,h,name,rgba,(size_t)w*h*4,28,0);}
void texpack_dump_dds(uint64_t hash,uint32_t w,uint32_t h,const char* name,uint32_t format,uint8_t* data,size_t bytes){dump_enqueue(hash,w,h,name,data,bytes,format,1);}

/* ---- BC1/BC2/BC3 to RGBA8, for dumping compressed textures ------------------ */
static void rgb565(uint16_t c, uint8_t* o)
{
    const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    o[0] = (uint8_t)(r << 3 | r >> 2); o[1] = (uint8_t)(g << 2 | g >> 4); o[2] = (uint8_t)(b << 3 | b >> 2); o[3] = 255;
}
void texpack_decode_bc(int bc, const uint8_t* blocks, size_t pitch, uint32_t w, uint32_t h, uint8_t* rgba)
{
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bsize = bc == 1 ? 8u : 16u;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = blocks + (size_t)by * pitch + (size_t)bx * bsize;
            const uint8_t* cb = bc == 1 ? b : b + 8;
            uint8_t pal[4][4];
            const uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
            rgb565(c0, pal[0]); rgb565(c1, pal[1]);
            if (bc != 1 || c0 > c1)
                for (int k = 0; k < 3; k++) {
                    pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k] + 1) / 3);
                    pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
                }
            else
                for (int k = 0; k < 3; k++) { pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
            pal[2][3] = 255; pal[3][3] = (bc == 1 && c0 <= c1) ? 0 : 255;
            const uint32_t idx = (uint32_t)cb[4] | (uint32_t)cb[5] << 8 | (uint32_t)cb[6] << 16 | (uint32_t)cb[7] << 24;
            uint8_t alpha[16];
            if (bc == 2)
                for (int i = 0; i < 16; i++) { const uint32_t a = (b[i / 2] >> ((i & 1) * 4)) & 15; alpha[i] = (uint8_t)(a * 17); }
            else if (bc == 3) {
                uint32_t a[8]; a[0] = b[0]; a[1] = b[1];
                if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1] + 3) / 7;
                else { for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1] + 2) / 5; a[6] = 0; a[7] = 255; }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = (uint8_t)a[(bits >> (3 * i)) & 7];
            }
            for (int i = 0; i < 16; i++) {
                const uint32_t x = bx * 4 + (uint32_t)(i & 3), y = by * 4 + (uint32_t)(i >> 2);
                if (x >= w || y >= h) continue;
                uint8_t* o = rgba + ((size_t)y * w + x) * 4;
                memcpy(o, pal[(idx >> (2 * i)) & 3], 4);
                if (bc != 1) o[3] = alpha[i];
            }
        }
}
