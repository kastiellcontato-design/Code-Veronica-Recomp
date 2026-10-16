/* rsx_perf_overlay.h -- the performance overlay in the corner of the game.
 *
 * PERF_OVERLAY lists what it shows, in this order, joined by commas:
 * fps, ms (frame time), cpu (whole system), gpu (3D engine, whole system),
 * ram (this process's working set) and vram (this process's video memory).
 * Unset or empty: no overlay. PERF_OVERLAY_CORNER (tl, tr, bl, br; tr by
 * default) places it, and PERF_OVERLAY_LAYOUT (row or column) puts the
 * numbers side by side or one under the other. Read again every refresh, so
 * a launcher can change them while the game runs.
 */
#pragma once
#include <stdint.h>
#include <wchar.h>
#include "rsx_ui_batch.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Once per presented frame. */
void perf_overlay_frame(void);
/* The overlay's quads in back-buffer pixels (w x h), in its corner;
 * NULL / 0 when it is off. Valid until the next call. */
const rsx_ui_quad* perf_overlay_quads(uint32_t w, uint32_t h, uint32_t* count);
/* A short notice in the bottom-right corner (about two seconds), drawn
 * whether or not the overlay is on: a green dot when on is set, a grey ring
 * otherwise. Any thread. */
void perf_overlay_toast(const wchar_t* text, int on);
#ifdef __cplusplus
}
#endif
