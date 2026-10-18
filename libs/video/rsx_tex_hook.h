/* rsx_tex_hook.h -- let a title rewrite a texture's guest bytes before upload.
 *
 * The D3D12 backend reads every texture straight from guest memory and
 * re-uploads one whenever those bytes change. A title can therefore swap one
 * image for another (a mod, or the other platform's button icons) by
 * rewriting the bytes in place: from this hook, just before a texture is
 * uploaded, or at any later time (the change is seen on the next bind).
 */
#ifndef PS3RECOMP_RSX_TEX_HOOK_H
#define PS3RECOMP_RSX_TEX_HOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called with the texture's level-0 bytes in guest memory (`nbytes` of them,
 * at guest offset `off`) before they are uploaded. May modify them. */
typedef void (*rsx_tex_source_fn)(uint8_t* data, uint32_t nbytes, uint32_t off);

extern rsx_tex_source_fn g_rsx_tex_source_hook;

/* Optional native UI artwork. Called only on a texture upload, after the
 * source hook. Return a stable nonzero identity; when it differs from previous,
 * fill *image with a malloc-owned RGBA8 texpack_image (one mip). Backend frees
 * it. Return 0 for unrelated textures. Guest dimensions/UVs are unchanged.
 * A repeated identity may leave *image NULL to reuse its uploaded resource. */
struct texpack_image;
typedef uint64_t (*rsx_tex_ui_fn)(const uint8_t* data, uint32_t nbytes,
    uint32_t w, uint32_t h, float scale, uint64_t previous, struct texpack_image** image);
extern rsx_tex_ui_fn g_rsx_tex_ui_hook;


/* The backend notices a changed texture by a sparse checksum, which can miss a
 * small edit (one icon in a large page). A title that rewrites a texture's
 * bytes outside the hook calls this with its guest offset (as the hook was
 * given it) so the next bind uploads it again. */
void rsx_tex_mark_dirty(uint32_t off);

/* Backend side: 1 (and forgets it) if `off` was marked since. Cheap when
 * nothing is marked. */
int rsx_tex_take_dirty(uint32_t off);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_TEX_HOOK_H */
