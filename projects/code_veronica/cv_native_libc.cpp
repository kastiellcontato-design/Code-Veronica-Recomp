/* The guest libc memset and memmove, done natively.
 *
 * Lifted, they store 8 bytes at a time through vm_write64, a call with its
 * checks per store: in Code Veronica's busy rooms the two were ~14% of the
 * main PPU thread, which is what holds the frame rate there. The runtime's
 * ppu_vm_fill / ppu_vm_move keep the stores' rules (SPU reservations, the
 * memory diagnostics). Same registers as the originals on return: r3 is the
 * destination (memmove zero-extends it, as the lifted body does).
 * CV_NATIVE_LIBC=0 runs the lifted bodies instead. */
#include "ppu_recomp.h"
#include "cv_version.h"
#include <cstdio>
#include <cstdlib>

extern "C" void ppu_vm_fill(uint32_t a, uint8_t c, uint32_t n);
extern "C" void ppu_vm_move(uint32_t d, uint32_t s, uint32_t n);
void CV_OG_MEMSET(ppu_context* ctx);
void CV_OG_MEMMOVE(ppu_context* ctx);

static bool native_libc()
{
    static const bool on = [] {
        const char* e = std::getenv("CV_NATIVE_LIBC");
        const bool v = !(e && *e == '0');
        std::fprintf(stderr, "[LIBC] guest memset/memmove: %s\n", v ? "native" : "lifted");
        return v;
    }();
    return on;
}

void CV_FN_MEMSET(ppu_context* ctx)
{
    if (!native_libc()) { CV_OG_MEMSET(ctx); return; }
    ppu_vm_fill((uint32_t)ctx->gpr[3], (uint8_t)ctx->gpr[4], (uint32_t)ctx->gpr[5]);
}

void CV_FN_MEMMOVE(ppu_context* ctx)
{
    if (!native_libc()) { CV_OG_MEMMOVE(ctx); return; }
    ctx->gpr[3] = (uint32_t)ctx->gpr[3];
    ppu_vm_move((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5]);
}
