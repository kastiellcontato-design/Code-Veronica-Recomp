/*
 * ps3recomp - RSX guest vertex fetch (see rsx_vertex_fetch.h)
 */
#include "rsx_vertex_fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

float rsx_rd_bef(const u8* p)
{
    u32 w; memcpy(&w, p, 4);
    w = ((w >> 24) & 0xFFu) | ((w >> 8) & 0xFF00u) |
        ((w << 8) & 0xFF0000u) | ((w << 24) & 0xFF000000u);
    float f; memcpy(&f, &w, 4); return f;
}

float rsx_rd_half_be(const u8* p)
{
    u16 h = (u16)((p[0] << 8) | p[1]);
    u32 sgn = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, man = h & 0x3FFu, f;
    if (exp == 0)        f = sgn << 31;
    else if (exp == 31)  f = (sgn << 31) | 0x7F800000u | (man << 13);
    else                 f = (sgn << 31) | ((exp - 15u + 127u) << 23) | (man << 13);
    float o; memcpy(&o, &f, 4); return o;
}

void rsx_fetch_attrib(const rsx_state* st, int idx, u32 vi, float out[4])
{
    /* A disabled attribute array feeds the CONSTANT vertex attribute register
     * (NV4097_SET_VERTEX_DATA4F_M), not zero -- same rule as glColor4f with
     * the colour array off. Defaulting these to black multiplied Rubber
     * Ducky's duck texture away in the fragment program. Components the array
     * does not supply (size < 4) keep the register's value too. */
    out[0] = st->vertex_data4f[idx][0];
    out[1] = st->vertex_data4f[idx][1];
    out[2] = st->vertex_data4f[idx][2];
    out[3] = st->vertex_data4f[idx][3];

    const rsx_vertex_attrib* a = &st->vertex_attribs[idx];
    if (!a->enabled || a->stride == 0 || !vm_base) return;

    /* Vertex frequency divisor (instancing). freq 0/1 = per-vertex. For
     * freq > 1 the element index is either vertex/freq (DIVIDE: per-instance
     * data advances once per freq verts) or vertex%freq (MODULO: a mesh
     * repeats every freq verts). NV4097_SET_FREQUENCY_DIVIDER_OPERATION's
     * per-attribute bit selects MODULO. DeferredShading's cube rings need
     * this: the shared cube mesh is MODULO, the per-instance transform in
     * attrib9 is DIVIDE -- without it attrib9 advanced every vertex and the
     * instanced cubes drew with garbage transforms (only the baseplate,
     * which isn't instanced, survived). */
    u32 ei = vi;
    if (a->frequency > 1) {
        static int nofreq = -1;                 /* VP_NOFREQ: instancing kill-switch */
        if (nofreq < 0) nofreq = getenv("VP_NOFREQ") ? 1 : 0;
        if (!nofreq)
            ei = (st->frequency_divider_op & (1u << idx))
                     ? (vi % a->frequency)      /* MODULO: repeat mesh */
                     : (vi / a->frequency);     /* DIVIDE: per-instance */
    }

    /* Bit 31 of the vertex-array OFFSET selects the context DMA: 0 = LOCAL
     * (VRAM), 1 = MAIN (IO-mapped system memory). Resolve LOCAL as local --
     * routing it through cellGcmResolveOffset lets the IO table win for any
     * offset whose page the IO region also covers. One title's vertex arrays
     * sit at offsets like 0x4480, shadowed by its 1MB IO window at 0x11100000:
     * the array was uploaded to VRAM 0xC0004480 but resolved to main
     * 0x11104480, which is empty -- so every INDEXED mesh fetched zeros and
     * collapsed to the origin, while non-indexed geometry whose arrays lie
     * outside the shadowed pages drew fine. */
    u32 off = (a->offset & 0x7FFFFFFFu) + ei * a->stride;
    const u8* p = vm_base + ((a->offset & 0x80000000u)
        ? cellGcmResolveLocated(0, off)   /* MAIN:  IO offset table          */
        : cellGcmResolveLocated(1, off)); /* LOCAL: VRAM, never the IO table */

    u32 n = a->size ? a->size : 4; if (n > 4) n = 4;
    /* VTX_TYPEDBG=1: each distinct (attribute, type, size, stride) once. */
    { static int t = -1; if (t < 0) t = getenv("VTX_TYPEDBG") ? 1 : 0;
      if (t) { static u32 seen[64]; static int ns = 0;
        u32 key = ((u32)idx << 24) | ((a->type & 0xFFu) << 16) | ((a->size & 0xFFu) << 8) | (a->stride & 0xFFu);
        int f = 0; for (int i = 0; i < ns; i++) if (seen[i] == key) f = 1;
        if (!f && ns < 64) { seen[ns++] = key;
            fprintf(stderr, "[VTXFMT] attr=%d type=%u size=%u stride=%u%c", idx, a->type, a->size, a->stride, 10); } } }
    switch (a->type) {
    case 2: /* CELL_GCM_VERTEX_F: float32 BE */
        for (u32 k = 0; k < n; k++) out[k] = rsx_rd_bef(p + k * 4);
        break;
    case 3: /* SF: half float BE */
        for (u32 k = 0; k < n; k++) out[k] = rsx_rd_half_be(p + k * 2);
        break;
    case 4: /* UB: u8 normalized [0,1] */
        for (u32 k = 0; k < n; k++) out[k] = p[k] / 255.0f;
        break;
    case 1: /* S1: s16 normalized [-1,1] */
        for (u32 k = 0; k < n; k++) {
            s16 s = (s16)((p[k*2] << 8) | p[k*2+1]);
            out[k] = (float)s / 32767.0f;
        }
        break;
    case 5: /* S32K: s16 integer */
        for (u32 k = 0; k < n; k++)
            out[k] = (float)(s16)((p[k*2] << 8) | p[k*2+1]);
        break;
    case 7: /* UB256: u8 unnormalized */
        for (u32 k = 0; k < n; k++) out[k] = (float)p[k];
        break;
    case 6: { /* CMP: one BE word, signed normalized x:11 y:11 z:10 (low bits first) */
        u32 v = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        s32 x = (s32)(v << 21) >> 21, y = (s32)(v << 10) >> 21, z = (s32)v >> 22;
        out[0] = (float)x / 1023.0f;
        out[1] = (float)y / 1023.0f;
        out[2] = (float)z / 511.0f;
        out[3] = 1.0f;
        break; }
    default:
        /* Always reported, once per type: reading an unknown format as floats
         * turns a whole mesh into garbage positions that never rasterise. */
        { static u32 seen = 0;
          if (!(seen & (1u << (a->type & 31)))) { seen |= 1u << (a->type & 31);
              fprintf(stderr, "[VTXTYPE] UNHANDLED type=%u attr=%d size=%u stride=%u\n",
                      a->type, idx, a->size, a->stride); } }
        for (u32 k = 0; k < n; k++) out[k] = rsx_rd_bef(p + k * 4);
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Planned fetch (see rsx_fetch_plan in the header)
 * -------------------------------------------------------------------------*/
extern u32 cellGcmResolveLocated(int local, u32 offset);

void rsx_fetch_plan_build(const rsx_state* st, rsx_fetch_plan* plan)
{
    static int nofreq = -1;
    if (nofreq < 0) nofreq = getenv("VP_NOFREQ") ? 1 : 0;
    for (int i = 0; i < 16; i++) {
        rsx_attr_plan* p = &plan->a[i];
        const rsx_vertex_attrib* a = &st->vertex_attribs[i];
        memcpy(p->cst, st->vertex_data4f[i], sizeof p->cst);
        p->active = a->enabled && a->stride != 0 && vm_base != NULL;
        p->type = a->type;
        p->n = a->size ? a->size : 4; if (p->n > 4) p->n = 4;
        p->stride = a->stride;
        p->freq = (a->frequency > 1 && !nofreq) ? a->frequency : 0;
        p->modulo = (st->frequency_divider_op & (1u << i)) != 0;
        p->main_mem = (a->offset & 0x80000000u) != 0;
        p->off0 = a->offset & 0x7FFFFFFFu;
        p->page = ~0u; p->page_base = NULL;
    }
    /* RSX_VA_HIST=1: which 1 MB regions of guest memory the draws' vertex
     * arrays come from, every 5 s (draw count per region). */
    { static int on = -1; if (on < 0) on = getenv("RSX_VA_HIST") ? 1 : 0;
      if (on && vm_base) {
          static unsigned cnt[4096]; static unsigned long long t0; static unsigned draws;
          draws++;
          for (int i = 0; i < 16; i++) {
              const rsx_attr_plan* p = &plan->a[i];
              if (!p->active) continue;
              u32 ea = cellGcmResolveLocated(p->main_mem ? 0 : 1, p->off0);
              cnt[(ea >> 20) & 4095]++;
          }
          unsigned long long now = (unsigned long long)clock() * 1000ull / CLOCKS_PER_SEC; if (!t0) t0 = now;
          if (now - t0 >= 5000) { t0 = now;
              fprintf(stderr, "[va-hist] %u draws/5s, attribute arrays by EA MB:", draws);
              for (int k = 0; k < 4096; k++) if (cnt[k]) { fprintf(stderr, " %03X=%u", k, cnt[k]); cnt[k] = 0; }
              fprintf(stderr, "\n"); draws = 0; } } }
}

static inline u32 rd_be32(const u8* p)
{
    u32 w; memcpy(&w, p, 4);
#if defined(_MSC_VER) || defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(w);
#else
    return ((w >> 24) & 0xFFu) | ((w >> 8) & 0xFF00u) | ((w << 8) & 0xFF0000u) | (w << 24);
#endif
}

void rsx_fetch_vertex_planned(rsx_fetch_plan* plan, u32 vi, float out[16][4])
{
    for (int i = 0; i < 16; i++) {
        rsx_attr_plan* a = &plan->a[i];
        float* o = out[i];
        o[0] = a->cst[0]; o[1] = a->cst[1]; o[2] = a->cst[2]; o[3] = a->cst[3];
        if (!a->active) continue;
        u32 ei = vi;
        if (a->freq) ei = a->modulo ? (vi % a->freq) : (vi / a->freq);
        u32 off = a->off0 + ei * a->stride;
        const u8* p;
        if (a->main_mem) {
            u32 pg = off >> 20;
            if (pg != a->page) {   /* IO pages need not be contiguous */
                a->page = pg;
                a->page_base = vm_base + cellGcmResolveLocated(0, pg << 20);
            }
            p = a->page_base + (off & 0xFFFFFu);
        } else {
            p = vm_base + cellGcmResolveLocated(1, off);   /* local: linear */
        }
        const u32 n = a->n;
        switch (a->type) {
        case 2: /* float32 BE */
            for (u32 k = 0; k < n; k++) { u32 w = rd_be32(p + k * 4); memcpy(&o[k], &w, 4); }
            break;
        case 3: for (u32 k = 0; k < n; k++) o[k] = rsx_rd_half_be(p + k * 2); break;
        case 4: for (u32 k = 0; k < n; k++) o[k] = p[k] / 255.0f; break;
        case 1:
            for (u32 k = 0; k < n; k++) {
                s16 v = (s16)((p[k*2] << 8) | p[k*2+1]);
                o[k] = (float)v / 32767.0f;
            }
            break;
        case 5: for (u32 k = 0; k < n; k++) o[k] = (float)(s16)((p[k*2] << 8) | p[k*2+1]); break;
        case 7: for (u32 k = 0; k < n; k++) o[k] = (float)p[k]; break;
        case 6: {
            u32 v = rd_be32(p);
            s32 x = (s32)(v << 21) >> 21, y = (s32)(v << 10) >> 21, z = (s32)v >> 22;
            o[0] = (float)x / 1023.0f; o[1] = (float)y / 1023.0f;
            o[2] = (float)z / 511.0f;  o[3] = 1.0f;
            break; }
        default:   /* unknown: same as rsx_fetch_attrib (which reports it) */
            { static u32 seen = 0;
              if (!(seen & (1u << (a->type & 31)))) { seen |= 1u << (a->type & 31);
                  fprintf(stderr, "[VTXTYPE] UNHANDLED type=%u attr=%d size=%u stride=%u%c",
                          a->type, i, n, a->stride, 10); } }
            for (u32 k = 0; k < n; k++) o[k] = rsx_rd_bef(p + k * 4);
            break;
        }
    }
}
