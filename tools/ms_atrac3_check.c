/*
 * Offline check of libs/codec/ms_atrac3.c against Sony's plug-in.
 *
 * Input: local-store dumps around consecutive calls of the real plug-in's
 * decodeATRACPackets (atr_<k>_before.ls / atr_<k>_after.ls). They come from a
 * debugging hook added by hand to the lifted plug-in's entry and return while
 * a title runs with a real dev_flash; it logged each call as
 * "[atr#K] enter img=.. r3=.. r4=.. r5=.. r6=.. r7=.. r8=.. r9=.. r10=..".
 * Each call is replayed on the "before" store and the window it writes is
 * compared with the "after" store. Replays must start at a call whose reset
 * flag is set (the start of a sound), or the first frames lack history.
 *
 * Result for Code Veronica (2026-10-15): every window position matches; the
 * samples agree within a few LSB, and within about 2% on frames that use
 * ATRAC3 gain control (Sony's gain interpolation is not FFmpeg's).
 *
 * Build (from the repository root, MSVC environment):
 *   clang-cl /O2 /EHsc /Fe:ms_atrac3_check.exe tools\ms_atrac3_check.c
 *       libs\codec\ms_atrac3.c third_party\at3_standalone\*.cpp
 * Run:  ms_atrac3_check <dump dir> <first call> <last call> [run log | r3..r10 in hex]
 * With the run log, each call's registers come from its "[atr#K] enter" line.
 */
#include "../libs/codec/ms_atrac3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LS_SIZE 0x40000

static int load(const char* dir, int k, const char* which, uint8_t* ls)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/atr_%d_%s.ls", dir, k, which);
    FILE* f = fopen(p, "rb");
    if (!f) return 0;
    size_t n = fread(ls, 1, LS_SIZE, f);
    fclose(f);
    return n == LS_SIZE;
}

static int s16(const uint8_t* p) { return (int16_t)((uint16_t)p[0] << 8 | p[1]); }

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s dir first last [r3..r10]\n", argv[0]); return 2; }
    uint32_t r[8] = { 0x13A00, 0x1BA00, 0x23A00, 0x400, 0x2FB80, 0x3C900, 0x2FA80, 0 };
    const char* log = argc == 5 && strstr(argv[4], ".txt") ? argv[4] : NULL;
    if (!log) for (int i = 0; i < 8 && 4 + i < argc; i++) r[i] = (uint32_t)strtoul(argv[4 + i], NULL, 16);
    static uint8_t ls[LS_SIZE], want[LS_SIZE];
    int first = atoi(argv[2]), last = atoi(argv[3]), worst_all = 0;
    for (int k = first; k <= last; k++) {
        if (log) {                        /* this call's registers */
            FILE* f = fopen(log, "rb"); char line[512]; int found = 0;
            char tag[32]; snprintf(tag, sizeof tag, "[atr#%d] enter", k);
            while (f && !found && fgets(line, sizeof line, f)) {
                const char* q = strstr(line, tag);
                if (q) found = sscanf(strstr(q, "r3="), "r3=%x r4=%x r5=%x r6=%x r7=%x r8=%x r9=%x r10=%x",
                                      &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7]) == 8;
            }
            if (f) fclose(f);
            if (!found) continue;
        }
        if (!load(argv[1], k, "before", ls) || !load(argv[1], k, "after", want)) {
            printf("call %d: dumps missing\n", k);
            continue;
        }
        const int channels = ls[r[4] + 33], n = (int)r[3];
        uint32_t got = ms_atrac3_decode(ls, r);
        uint32_t ret = (uint32_t)want[r[5] + 0x8D0] << 24 | want[r[5] + 0x8D1] << 16 |
                       want[r[5] + 0x8D2] << 8 | want[r[5] + 0x8D3];
        int worst = 0, at = -1, diff = 0;
        for (int c = 0; c < channels; c++)
            for (int i = 0; i < n + 4; i++) {
                uint32_t o = r[1] + (uint32_t)c * 0x1000 + 2u * (uint32_t)i;
                int d = abs(s16(ls + o) - s16(want + o));
                if (d) diff++;
                if (d > worst) { worst = d; at = i; }
            }
        if (getenv("MS_DUMP")) {          /* hop of channel 0: ours, then the plug-in's */
            FILE* f = fopen(getenv("MS_DUMP"), k == first ? "wb" : "ab");
            if (f) { fwrite(ls + r[1], 1, (size_t)n, f); fwrite(want + r[1], 1, (size_t)n, f); fclose(f); }
        }
        if (worst > worst_all) worst_all = worst;
        printf("call %3d: P %4u (want %4u)  samples differing %4d  max diff %5d at %d\n",
               k, got, ret, diff, worst, at);
    }
    printf("worst over all calls: %d\n", worst_all);
    return 0;
}
