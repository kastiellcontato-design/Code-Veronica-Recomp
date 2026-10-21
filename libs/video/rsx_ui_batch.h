/* Host UI geometry for the existing game framebuffer; no window/input layer. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* corner_a: the alpha of each corner, one byte each (top-left, bottom-left,
 * top-right, bottom-right from the low byte up), multiplied into argb's and
 * interpolated across the quad; 0 means opaque corners. A grid of such
 * quads between texel centres draws an image magnified the way a texture's
 * bilinear filter does it. */
typedef struct rsx_ui_quad { float x,y,w,h; uint32_t argb; uint32_t corner_a; } rsx_ui_quad;
/* Logical 640x480, centered at the game's 4:3 UI scale. Copy on submit.
 * Empty batches clear the UI. Data expires if the producer stops updating. */
void rsx_d3d12_set_ui_quads(const rsx_ui_quad* quads,uint32_t count);
/* Retain the latest menu batch between native updates. Explicitly release on
 * close/reset. Never bypasses a newer empty batch; lease expires after 500 ms. */
void rsx_d3d12_set_ui_retained(int active);
/* An image drawn by the port's own overlay (the SRT): a rectangle of the
 * overlay's atlas (u/v normalised), tinted by argb, drawn after the overlay's
 * first `after` quads. The atlas is RGBA8, straight alpha, row-major; it is
 * uploaded again only when its generation changes. */
typedef struct rsx_ui_sprite { float x,y,w,h,u0,v0,u1,v1; uint32_t argb; uint32_t after; } rsx_ui_sprite;
#ifdef __cplusplus
}
#endif
