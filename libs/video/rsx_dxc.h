/* Optional DXC adapter. No static DLL dependency: FXC works without DXC. */
#ifndef RSX_DXC_H
#define RSX_DXC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int rsx_dxc_available(void);
const char* rsx_dxc_cache_tag(void);
/* Success: 0, malloc-owned DXIL object. Failure: negative, optional malloc-
 * owned diagnostic. All outputs are initialized; caller frees both buffers. */
int rsx_dxc_compile(const char* src, size_t len, const char* profile,
                    void** out, size_t* out_size, char** error);
#ifdef __cplusplus
}
#endif
#endif
