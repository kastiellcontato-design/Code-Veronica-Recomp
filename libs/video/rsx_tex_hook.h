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

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_TEX_HOOK_H */
