#ifndef RSX_TEXTURE_UPLOAD_LAYOUT_H
#define RSX_TEXTURE_UPLOAD_LAYOUT_H
#include <stdint.h>

/* Host upload-buffer placement, independent of the guest cube-face stride.
 * D3D12 requires 512-byte subresource offsets on devices without unrestricted
 * buffer/texture copies. Row pitch alone (256 bytes) does not ensure that. */
static inline uint64_t rsx_texture_upload_face_stride(uint32_t pitch,
                                                     uint32_t rows, int cube)
{
    const uint64_t bytes = (uint64_t)pitch * rows;
    return cube ? (bytes + 511u) & ~(uint64_t)511u : bytes;
}
#endif
