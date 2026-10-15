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
