/*
 * ps3recomp - switchable instruction patches
 *
 * ppu_lifter.py --patches lifts each patched site twice, the original
 * instruction and the replacement word, and the lifted code picks one by a flag
 * named after the patch -- what an emulator patch (RPCS3 patch.yml, "be32 at
 * this address") does, but switchable while the game runs.
 *
 * A flag starts on when its name is listed in PS3_PATCHES (comma-separated) and
 * can be changed later with ps3_patch_set, from a launcher's options menu.
 * Flags are created on first use, by the lifted site or by ps3_patch_set,
 * whichever comes first; each lifted site caches the pointer it gets back.
 */
#include <stdlib.h>
#include <string.h>

#define PS3_PATCH_MAX  64
#define PS3_PATCH_NAME 48

static struct {
    char name[PS3_PATCH_NAME];
    volatile unsigned char on;
} s_patch[PS3_PATCH_MAX];
static int s_patch_n;
static volatile long s_patch_lock;
static volatile unsigned char s_patch_overflow;   /* past PS3_PATCH_MAX: always off */

static int patch_env_lists(const char* name)
{
    const char* e = getenv("PS3_PATCHES");
    const size_t n = strlen(name);
    while (e && *e) {
        const char* c = strchr(e, ',');
        const size_t len = c ? (size_t)(c - e) : strlen(e);
        if (len == n && strncmp(e, name, n) == 0) return 1;
        e = c ? c + 1 : NULL;
    }
    return 0;
}

volatile unsigned char* ps3_patch_flag(const char* name)
{
    volatile unsigned char* r = &s_patch_overflow;
    while (__sync_lock_test_and_set(&s_patch_lock, 1)) { }
    int i;
    for (i = 0; i < s_patch_n; i++)
        if (strcmp(s_patch[i].name, name) == 0) break;
    if (i == s_patch_n && s_patch_n < PS3_PATCH_MAX && strlen(name) < PS3_PATCH_NAME) {
        strcpy(s_patch[i].name, name);
        s_patch[i].on = (unsigned char)patch_env_lists(name);
        s_patch_n++;
    }
    if (i < s_patch_n) r = &s_patch[i].on;
    __sync_lock_release(&s_patch_lock);
    return r;
}

void ps3_patch_set(const char* name, int on)
{
    volatile unsigned char* f = ps3_patch_flag(name);
    if (f != &s_patch_overflow) *f = on ? 1 : 0;
}

/*
 * Patches read from patch files (a launcher parses RPCS3's patch.yml): each is
 * a list of byte writes. A 4-byte write that a lifted switchable site stands
 * for (ps3_patch_site) turns that site's flag on; any other write goes to
 * guest memory, which only means something outside the code -- a launcher
 * offers code writes only when a lifted site covers them. Turning a patch off
 * puts back the bytes it replaced.
 *
 * spec: "addr:hexbytes,addr:hexbytes,..." (addr in hex, bytes in memory
 * order). PS3_PATCH_WRITES: "id=spec;id=spec" for the ones on at start.
 */
#include <stdint.h>
#include <stdio.h>

extern uint8_t* vm_base;

#define PS3_SITE_MAX   64
#define PS3_WRITES_MAX 64

static struct { uint32_t addr, word; char flag[PS3_PATCH_NAME]; } s_site[PS3_SITE_MAX];
static int s_site_n;

void ps3_patch_site(uint32_t addr, uint32_t word, const char* flag)
{
    if (s_site_n >= PS3_SITE_MAX || strlen(flag) >= PS3_PATCH_NAME) return;
    s_site[s_site_n].addr = addr; s_site[s_site_n].word = word;
    strcpy(s_site[s_site_n].flag, flag);
    s_site_n++;
}

/* The flag of the lifted site for that write, or NULL. */
const char* ps3_patch_site_flag(uint32_t addr, uint32_t word)
{
    for (int i = 0; i < s_site_n; i++) if (s_site[i].addr == addr && s_site[i].word == word) return s_site[i].flag;
    return NULL;
}

/* The replaced bytes of each patch that is on, to put them back. */
static struct { char id[64]; unsigned char* undo; size_t n; } s_on[PS3_WRITES_MAX];

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

int ps3_patch_writes(const char* id, const char* spec, int on)
{
    int slot = -1, free_slot = -1;
    for (int i = 0; i < PS3_WRITES_MAX; i++) {
        if (s_on[i].id[0] && !strcmp(s_on[i].id, id)) slot = i;
        else if (!s_on[i].id[0] && free_slot < 0) free_slot = i;
    }
    if (!on) {
        if (slot < 0) return 0;
        /* undo: [addr(4) len(2) bytes]..., or [addr(4) FFFF len(1) flag] for a site */
        for (size_t p = 0; p + 6 <= s_on[slot].n; ) {
            const unsigned char* u = s_on[slot].undo + p;
            const uint32_t a = (uint32_t)u[0] << 24 | (uint32_t)u[1] << 16 | (uint32_t)u[2] << 8 | u[3];
            const size_t len = (size_t)u[4] << 8 | u[5];
            if (len == 0xFFFF) {   /* a site flag */
                const size_t fl = u[6];
                char flag[PS3_PATCH_NAME] = {0};
                memcpy(flag, u + 7, fl < PS3_PATCH_NAME - 1 ? fl : PS3_PATCH_NAME - 1);
                ps3_patch_set(flag, 0);
                p += 7 + fl;
                continue;
            }
            if (vm_base) memcpy(vm_base + a, u + 6, len);
            p += 6 + len;
        }
        free(s_on[slot].undo);
        memset(&s_on[slot], 0, sizeof s_on[slot]);
        return 0;
    }
    if (slot >= 0) return 0;               /* already on */
    if (free_slot < 0 || strlen(id) >= sizeof s_on[0].id) return -1;
    slot = free_slot;
    size_t cap = 256, n = 0;
    unsigned char* undo = (unsigned char*)malloc(cap);
    if (!undo) return -1;
    for (const char* p = spec; p && *p; ) {
        char* end;
        const uint32_t a = (uint32_t)strtoul(p, &end, 16);
        if (*end != ':') break;
        unsigned char bytes[512]; size_t len = 0;
        for (p = end + 1; hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && len < sizeof bytes; p += 2)
            bytes[len++] = (unsigned char)(hexval(p[0]) << 4 | hexval(p[1]));
        if (*p == ',') p++;
        if (n + 8 + PS3_PATCH_NAME + len > cap) {
            while (n + 8 + PS3_PATCH_NAME + len > cap) cap *= 2;
            unsigned char* g = (unsigned char*)realloc(undo, cap);
            if (!g) break;
            undo = g;
        }
        int site = -1;
        if (len == 4) {
            const uint32_t w = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
            for (int i = 0; i < s_site_n; i++) if (s_site[i].addr == a && s_site[i].word == w) site = i;
        }
        undo[n++] = (unsigned char)(a >> 24); undo[n++] = (unsigned char)(a >> 16);
        undo[n++] = (unsigned char)(a >> 8);  undo[n++] = (unsigned char)a;
        if (site >= 0) {
            const size_t fl = strlen(s_site[site].flag);
            undo[n++] = 0xFF; undo[n++] = 0xFF; undo[n++] = (unsigned char)fl;
            memcpy(undo + n, s_site[site].flag, fl); n += fl;
            ps3_patch_set(s_site[site].flag, 1);
        } else {
            undo[n++] = (unsigned char)(len >> 8); undo[n++] = (unsigned char)len;
            if (vm_base) { memcpy(undo + n, vm_base + a, len); memcpy(vm_base + a, bytes, len); }
            else memset(undo + n, 0, len);
            n += len;
        }
    }
    strcpy(s_on[slot].id, id);
    s_on[slot].undo = undo; s_on[slot].n = n;
    return 0;
}

void ps3_patch_writes_from_env(void)
{
    const char* e = getenv("PS3_PATCH_WRITES");
    while (e && *e) {
        const char* eq = strchr(e, '=');
        const char* semi = strchr(e, ';');
        if (!eq || (semi && semi < eq)) break;
        char id[64] = {0};
        memcpy(id, e, (size_t)(eq - e) < sizeof id - 1 ? (size_t)(eq - e) : sizeof id - 1);
        const size_t sl = semi ? (size_t)(semi - eq - 1) : strlen(eq + 1);
        char* spec = (char*)malloc(sl + 1);
        if (spec) {
            memcpy(spec, eq + 1, sl); spec[sl] = 0;
            ps3_patch_writes(id, spec, 1);
            fprintf(stderr, "[patch] %s on (from a patch file)\n", id);
            free(spec);
        }
        e = semi ? semi + 1 : NULL;
    }
}
