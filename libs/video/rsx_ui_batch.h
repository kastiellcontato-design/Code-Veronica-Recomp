/* Host UI geometry for the existing game framebuffer; no window/input layer. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rsx_ui_quad { float x,y,w,h; uint32_t argb; } rsx_ui_quad;
/* Logical 640x480, centered at the game's 4:3 UI scale. Copy on submit.
 * Empty batches clear the UI. Data expires if the producer stops updating. */
void rsx_d3d12_set_ui_quads(const rsx_ui_quad* quads,uint32_t count);
#ifdef __cplusplus
}
#endif
