/* The SSE forms of the quadword byte rotates/shifts and cntb against their
 * byte-loop references (_ref in spu_helpers.h), bit for bit: every immediate
 * 0..255 (and negatives), shift words with high and sign bits set, random
 * data. Build with SSE4.1 or later (the SPU code is built /arch:AVX2). */
#include "spu_helpers.h"
#include <stdio.h>
#include <stdint.h>

#if !defined(__SSE4_1__)
#error "build with SSE4.1 (e.g. /arch:AVX2): this test checks the SSE forms"
#endif

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17; return (uint32_t)(s_rng >> 16); }
static u128 rnd128(void) { u128 r; for (int i = 0; i < 4; i++) r._u32[i] = rnd(); return r; }
static int same(u128 a, u128 b) { return memcmp(&a, &b, 16) == 0; }

static unsigned long long s_checks, s_fails;
static void check(const char* op, u128 got, u128 want, uint32_t sh)
{
    s_checks++;
    if (same(got, want)) return;
    if (s_fails++ < 10)
        printf("FAIL %s sh=0x%08X got %08X%08X%08X%08X want %08X%08X%08X%08X\n", op, sh,
               got._u32[0], got._u32[1], got._u32[2], got._u32[3],
               want._u32[0], want._u32[1], want._u32[2], want._u32[3]);
}

int main(void)
{
    for (int iter = 0; iter < 20000; iter++) {
        const u128 a = rnd128();
        /* Immediates: every value an encoding can carry, plus negatives. */
        for (int i = -128; i < 256; i++) {
            check("shlqbyi", spu_shlqbyi(a, i), spu_shlqbyi_ref(a, i), (uint32_t)i);
            check("rotqbyi", spu_rotqbyi(a, i), spu_rotqbyi_ref(a, i), (uint32_t)i);
            check("rotqmbyi", spu_rotqmbyi(a, i), spu_rotqmbyi_ref(a, i), (uint32_t)i);
        }
        /* Register shifts: the low byte swept, the rest random or sign-set. */
        for (int k = 0; k < 64; k++) {
            u128 b = rnd128();
            if (k < 32) b._u32[0] = (rnd() & ~0xFFu) | (uint32_t)(k * 8 + (rnd() & 7));
            if (k & 1) b._u32[0] |= 0x80000000u;
            const uint32_t sh = b._u32[0];
            check("shlqby", spu_shlqby(a, b), spu_shlqby_ref(a, b), sh);
            check("rotqby", spu_rotqby(a, b), spu_rotqby_ref(a, b), sh);
            check("shlqbybi", spu_shlqbybi(a, b), spu_shlqbybi_ref(a, b), sh);
            check("rotqbybi", spu_rotqbybi(a, b), spu_rotqbybi_ref(a, b), sh);
            check("rotqmby", spu_rotqmby(a, b), spu_rotqmby_ref(a, b), sh);
            check("rotqmbybi", spu_rotqmbybi(a, b), spu_rotqmbybi_ref(a, b), sh);
        }
        check("cntb", spu_cntb(a), spu_cntb_ref(a), 0);
    }
    /* cntb: every byte value in every lane. */
    for (int v = 0; v < 256; v++) {
        u128 a; memset(&a, v, 16);
        check("cntb-all", spu_cntb(a), spu_cntb_ref(a), (uint32_t)v);
    }
    printf("%llu checks, %llu failed\n", s_checks, s_fails);
    return s_fails ? 1 : 0;
}
