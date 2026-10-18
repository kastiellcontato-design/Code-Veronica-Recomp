/* rsx_tex_hook.c -- see rsx_tex_hook.h. */
#include "rsx_tex_hook.h"
#include <windows.h>

rsx_tex_source_fn g_rsx_tex_source_hook;
rsx_tex_ui_fn g_rsx_tex_ui_hook;

/* A few offsets waiting for their next bind; 0 = a free entry. */
#define DIRTY_N 32
static volatile LONG s_dirty[DIRTY_N];
static volatile LONG s_ndirty;

void rsx_tex_mark_dirty(uint32_t off)
{
    if (!off) return;
    for (int i = 0; i < DIRTY_N; i++)
        if ((uint32_t)s_dirty[i] == off) return;
    for (int i = 0; i < DIRTY_N; i++)
        if (InterlockedCompareExchange(&s_dirty[i], (LONG)off, 0) == 0) { InterlockedIncrement(&s_ndirty); return; }
    /* full (offsets never bound again): take over the oldest-placed entry */
    static volatile LONG next;
    s_dirty[(InterlockedIncrement(&next) - 1) % DIRTY_N] = (LONG)off;
}

int rsx_tex_take_dirty(uint32_t off)
{
    if (!s_ndirty) return 0;
    for (int i = 0; i < DIRTY_N; i++)
        if ((uint32_t)s_dirty[i] == off && InterlockedCompareExchange(&s_dirty[i], 0, (LONG)off) == (LONG)off) {
            InterlockedDecrement(&s_ndirty);
            return 1;
        }
    return 0;
}
