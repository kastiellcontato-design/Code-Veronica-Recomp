/* Exact consecutive-prepass lookup hint. Live PSO/VS guards are mandatory
 * in the caller; no raw pipeline pointer is kept here. */
#ifndef RSX_PREPARED_REPEAT_H
#define RSX_PREPARED_REPEAT_H
#include <stdint.h>
#include <string.h>
typedef struct {
    uint32_t vs_idx,blend,nrt,rtfmt,exp32,cmask,cull,cube_mask,bytes;
} RsxPreparedRepeatKey;
static inline int rsx_prepared_repeat_match(const RsxPreparedRepeatKey* a,const RsxPreparedRepeatKey* b,const void* ac,const void* bc) {
    return a&&b&&ac&&bc&&a->bytes&&a->bytes<=4096&&
        !memcmp(a,b,sizeof *a)&&!memcmp(ac,bc,a->bytes);
}
#endif
