#pragma once
#include <stdint.h>
/* Linear cube faces follow the byte/pitch layout without the 128-byte
 * padding required by swizzled faces (RPCS3 TextureUtils.cpp). */
static inline uint32_t rsx_cube_stride(uint32_t face_bytes,int swizzled) {
    return swizzled?(face_bytes+127u)&~127u:face_bytes;
}

/* CONTROL3 is a byte stride, shared by the linear mip rows. */
static inline uint32_t rsx_cube_row_bytes(uint32_t packed,uint32_t pitch,int swizzled) {
    return !swizzled && pitch>packed ? pitch : packed;
}
