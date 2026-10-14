/*
 * ps3recomp - PPU <-> SPU lock-line coherence
 *
 * See spu_coherency.h for what this is for. The state here is deliberately
 * plain: a bit per 128-byte line, a small table of the SPU contexts that have
 * ever reserved one, and the lock-line spinlock. Everything that WRITES it runs
 * with the lock-line lock held (the SPU's GETLLAR, the PPU's coherent store),
 * so the read-modify-write on a shared bitmap byte is serialized. Only
 * spu_coh_is_reserved reads without the lock, which is the entire point: it is
 * on every PPU store.
 */

#include <stdlib.h>
#include "spu_coherency.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * The lock-line lock
 *
 * Moved here from spu_channels.c, where it was a file-static and so could
 * serialize SPU against SPU but not SPU against PPU -- which left the PPU free
 * to write a line in the middle of a PUTLLC's compare-and-commit. Same
 * spinlock, same semantics, now shared. _InterlockedExchange is a clang-cl/MSVC
 * intrinsic (no runtime library symbol needed); elsewhere use the C11
 * equivalent, which lowers to the same LL/SC or lock-xchg on every target.
 * ===========================================================================*/
#if defined(_MSC_VER)
#include <intrin.h>
#include <windows.h>   /* SwitchToThread */
static volatile long g_lockline = 0;
/* Test-and-test-and-set: spin on a plain read (no cache-line ping-pong) with a
 * pause, and yield after a long wait. The bare exchange loop let GH3's four
 * idle job-policy pollers saturate the line and delay every other SPU atomic
 * -- FMOD's mixer task among them, 25-40 ms at a time. */
/* Contention counters for the [PERF] coherence line (spu_coh_stats_report). */
static volatile long s_ll_contended, s_ll_yields;
/* Time spent waiting in the contended path, in QPC ticks (PS3_SPU_STAT). */
volatile long long g_ll_wait_ticks = 0;
void spu_lockline_lock(void)
{
    unsigned spins = 0;
    if (!_InterlockedExchange(&g_lockline, 1)) return;
    _InterlockedIncrement(&s_ll_contended);
    LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
    do {
        do {
            _mm_pause();
            if (++spins >= 4096) { spins = 0; _InterlockedIncrement(&s_ll_yields); SwitchToThread(); }
        } while (g_lockline);
    } while (_InterlockedExchange(&g_lockline, 1));
    LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
    _InterlockedExchangeAdd64(&g_ll_wait_ticks, t1.QuadPart - t0.QuadPart);
}
void spu_lockline_unlock(void) { _InterlockedExchange(&g_lockline, 0); }
#else
#include <stdatomic.h>
static atomic_flag g_lockline = ATOMIC_FLAG_INIT;
void spu_lockline_lock(void)   { while (atomic_flag_test_and_set_explicit(&g_lockline, memory_order_acquire)) { } }
void spu_lockline_unlock(void) { atomic_flag_clear_explicit(&g_lockline, memory_order_release); }
#endif

/* ===========================================================================
 * The reserved-line bitmap
 *
 * One bit per 128-byte line over the whole 32-bit guest address space: 4 MiB of
 * zero-initialized BSS, of which only the pages around addresses an SPU has
 * actually reserved are ever touched. The reference implementation scopes its
 * bitmap to the one window a single title's SPURS structures live in; this
 * runtime hands out guest memory from several disjoint regions (the ELF image,
 * the sys_memory window at 0x40000000, the raw-SPU local-store windows), and a
 * line outside a hardcoded window would be silently non-coherent, which is the
 * failure this whole file exists to remove.
 * ===========================================================================*/
#define SPU_COH_LINE_SHIFT  7                                  /* 128-byte lines */
#define SPU_COH_LINES       (1u << (32 - SPU_COH_LINE_SHIFT))   /* 2^25 lines */
#define SPU_COH_BITMAP_SZ   (SPU_COH_LINES / 8)                 /* 4 MiB */

/* Not static: the PPU store path inlines the reserved-line test. */
unsigned char s_coh_bitmap[SPU_COH_BITMAP_SZ];

/* Zero until the first GETLLAR anywhere. Keeps the PPU fast path off the
 * bitmap entirely for a title that runs no SPU code, and keeps every existing
 * test that stores through vm_write* paying one compare against a hot global.
 * Written once, under the lock; read unlocked, where a stale zero costs at most
 * one missed event on the very first reservation of the run. */
int s_coh_armed;

/* The SPU contexts that have reserved a line. Sized to match the MFC engine
 * registry in spu_channels.c, which is the ceiling on live SPU contexts. It
 * was 8 while that registry grew to 64: Code Veronica runs ~40 tasks at once,
 * and a task left out of this table never got its lock-line-lost wakeup --
 * every such wait ran to the 10 ms channel timeout, which is the whole
 * budget of one audio mixer round. */
#define SPU_COH_MAX_CTX 64
static spu_context* s_coh_ctxs[SPU_COH_MAX_CTX];
/* One past the highest slot ever used; walkers scan only that far. Raised
 * under the lock-line lock, never lowered. */
static volatile int s_coh_n = 0;

unsigned long g_spu_lr_raise = 0;

void spu_coh_reserve(spu_context* ctx, uint32_t ea)
{
    uint32_t line = ea >> SPU_COH_LINE_SHIFT;
    s_coh_bitmap[line >> 3] |= (unsigned char)(1u << (line & 7));
    s_coh_armed = 1;

    if (!ctx) return;
    for (int i = 0; i < SPU_COH_MAX_CTX; i++) {
        if (s_coh_ctxs[i] == ctx) return;
        if (s_coh_ctxs[i] == NULL) {
            s_coh_ctxs[i] = ctx;
            if (i >= s_coh_n) s_coh_n = i + 1;
            return;
        }
    }
    /* More live SPU contexts than the registry holds. The ones already in it
     * still get their events; this one would silently never wake, so say so. */
    { static int warned = 0;
      if (!warned) { warned = 1;
          fprintf(stderr, "[spu-coh] more than %d reserving SPU contexts -- "
                          "spu=0x%X will not receive lock-line events\n",
                  SPU_COH_MAX_CTX, ctx->spu_id);
          fflush(stderr); } }
}

/* Drop a context from the reserving set.
 *
 * There was no way to do this, and the contexts that most need it are
 * TRANSIENT: spu_run_lifted_job_abi runs each job on a STACK-LOCAL
 * spu_context, which registered itself here on its first reservation and then
 * returned. The entry outlived the object, so spu_coh_notify_write walked a
 * dangling pointer -- reading resv_valid, writing event_status and calling
 * spu_ch_wake on reclaimed stack. Undefined behaviour, and observable: Guitar
 * Hero III showed THREE live-looking contexts for one dispatch, stealing each
 * other`s reservations, with every PUTLLC failing "no reservation". */
void spu_coh_unregister(spu_context* ctx)
{
    if (!ctx) return;
    /* Every run that ends here also gives back its MFC engine slot. SPURS
     * task runs never did: after 64 tasks the table was full for good, and
     * every DMA of every later task took the claim lock and shared one
     * fallback engine -- Code Veronica's lighter tasks spent ~70% of their
     * time waiting on that lock. */
    { extern void spu_mfc_release(spu_context*); spu_mfc_release(ctx); }
    /* Under the lock-line lock, which every walker of s_coh_ctxs holds: a
     * notifier that loaded this pointer just before the NULL store would
     * otherwise go on dereferencing it after the job returned and its stack
     * (or its whole async thread) was gone. GH3 at -O2 crashed that way in
     * spu_coh_notify_write, reading a freed thread stack. */
    spu_lockline_lock();
    for (int i = 0, n = s_coh_n; i < n; i++)
        if (s_coh_ctxs[i] == ctx) { s_coh_ctxs[i] = NULL; break; }
    spu_lockline_unlock();
}

/* Drop every registered context that lies in [lo, hi) -- a host thread's
 * stack, called as that thread exits. A stack-local context still registered
 * then has leaked past its unregister; the next walker would read an unmapped
 * stack (GH3 at the end of a Havok task: spu_coh_notify_write faulting).
 * ponytail: safety net that also names the leak; find the path that skips
 * spu_coh_unregister if it keeps firing. */
void spu_coh_forget_range(uintptr_t lo, uintptr_t hi)
{
    { extern void spu_mfc_forget_range(uintptr_t, uintptr_t); spu_mfc_forget_range(lo, hi); }
    spu_lockline_lock();
    for (int i = 0, n = s_coh_n; i < n; i++) {
        uintptr_t c = (uintptr_t)s_coh_ctxs[i];
        if (c >= lo && c < hi) {
            static int _n = 0;
            if (_n++ < 16)
                fprintf(stderr, "[spu-coh] leaked ctx %p (img=%d) dropped at thread exit\n",
                        (void*)c, s_coh_ctxs[i]->image_id);
            s_coh_ctxs[i] = NULL;
        }
    }
    spu_lockline_unlock();
}

int spu_coh_is_reserved(uint32_t addr)
{
    if (!s_coh_armed) return 0;
    uint32_t line = addr >> SPU_COH_LINE_SHIFT;
    return (s_coh_bitmap[line >> 3] >> (line & 7)) & 1u;
}

/* A DMA PUT is issued BY an SPU, and hardware does not take that SPU's own
 * reservation away for its own MFC write -- the MFC is part of the same SPE.
 * Our notify had no way to say "everyone but me", so an SPU that reserved a
 * line and then DMA'd a buffer overlapping it killed its own reservation and
 * its next PUTLLC could never succeed. Guitar Hero III's job policy module
 * livelocks exactly there: 16 million atomic ops on one line, every PUTLLC
 * failing for "no reservation", 600k of them self-inflicted.
 *
 * The PUTLLC path already guards this case by dropping its own reservation
 * before notifying; this gives the DMA path the same ability. */
void spu_coh_notify_write_except(uint32_t ea, const void* self)
{
    uint32_t line = ea & ~127u;
    for (int i = 0, n = s_coh_n; i < n; i++) {
        spu_context* c = s_coh_ctxs[i];
        if (!c || (const void*)c == self) continue;
        if (c->resv_valid && (c->resv_ea & ~127u) == line) {
            c->event_status |= SPU_EVENT_LR;
            c->resv_valid = 0;
            g_spu_lr_raise++;
            spu_ch_wake(c);
        }
    }
}

/* Locked PPU stores, and the lines they hit most (racy counters, diagnostics). */
static volatile long s_coh_stores, s_coh_wakes;
#define SPU_COH_HOT 16
static uint32_t s_coh_hot_line[SPU_COH_HOT];
static unsigned s_coh_hot_ct[SPU_COH_HOT];

void spu_coh_stats_report(void* fp)
{
    FILE* f = (FILE*)fp;
#if defined(_MSC_VER)
    long c = _InterlockedExchange(&s_ll_contended, 0), y = _InterlockedExchange(&s_ll_yields, 0);
#else
    long c = 0, y = 0;
#endif
    fprintf(f, "[PERF-COH] locked PPU stores %ld, SPU wakes %ld, lock contended %ld, yields %ld | hot lines:",
            s_coh_stores, s_coh_wakes, c, y);
    s_coh_stores = 0; s_coh_wakes = 0;
    for (int r = 0; r < 6; r++) {
        int b = -1;
        for (int i = 0; i < SPU_COH_HOT; i++)
            if (s_coh_hot_ct[i] && (b < 0 || s_coh_hot_ct[i] > s_coh_hot_ct[b])) b = i;
        if (b < 0) break;
        fprintf(f, " %08X x%u", s_coh_hot_line[b], s_coh_hot_ct[b]);
        s_coh_hot_ct[b] = 0;
    }
    for (int i = 0; i < SPU_COH_HOT; i++) { s_coh_hot_ct[i] = 0; s_coh_hot_line[i] = 0; }
    fprintf(f, "\n");
}

void spu_coh_notify_write(uint32_t ea)
{
    uint32_t line = ea & ~127u;
    s_coh_stores++;
    { int i = (int)((line >> 7) * 2654435761u >> 28) & (SPU_COH_HOT - 1);
      if (s_coh_hot_line[i] == line || !s_coh_hot_ct[i]) { s_coh_hot_line[i] = line; s_coh_hot_ct[i]++; } }
    for (int i = 0, n = s_coh_n; i < n; i++) {
        spu_context* c = s_coh_ctxs[i];
        if (!c) continue;
        if (c->resv_valid && (c->resv_ea & ~127u) == line) {
            c->event_status |= SPU_EVENT_LR;
            c->resv_valid = 0;          /* reservation lost, PUTLLC must fail */
            /* SPU_PUTLLC_WHY=1: name the agent that killed it. A PUTLLC that
             * always fails for "no reservation" is useless without knowing who
             * took it away -- a peer SPU, or the PPU committing to the line. */
            { static int s_w = -1;
              if (s_w < 0) s_w = getenv("SPU_PUTLLC_WHY") ? 1 : 0;
              if (s_w) { static unsigned long long n;
                  if ((++n % 200000) == 1)
                      fprintf(stderr, "[resv-lost] %llu: line 0x%08X taken from img=%d ctx=%p\n",
                              n, line, c->image_id, (void*)c); } }
            g_spu_lr_raise++;
            s_coh_wakes++;
            /* The event_status store has to be visible before the wake. The
             * waiter re-polls its predicate on a timeout as well, so a
             * straggling store costs latency and not the wakeup itself. */
            spu_ch_wake(c);
        }
    }
}

#ifdef __cplusplus
}
#endif
