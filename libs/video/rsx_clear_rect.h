#pragma once
#include <stdint.h>
#include <math.h>
/* Clamp in guest pixels, then scale both edges exactly like rs_dim. Empty
 * scissors remain empty; 4096 default scissors naturally cover the target. */
typedef struct { int32_t left,top,right,bottom; } RsxClearRect;
static inline int rsx_clear_rect(RsxClearRect* r,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t tw,uint32_t th,float scale) {
    if(!tw||!th||!w||!h||x>=tw||y>=th||!isfinite(scale)||scale<=0||scale>4)return 0;
    uint64_t right=(uint64_t)x+w,bottom=(uint64_t)y+h;
    if(right>tw)right=tw;if(bottom>th)bottom=th;
    r->left=(int32_t)((float)x*scale+0.5f);r->top=(int32_t)((float)y*scale+0.5f);
    r->right=(int32_t)((float)right*scale+0.5f);r->bottom=(int32_t)((float)bottom*scale+0.5f);
    return r->right>r->left&&r->bottom>r->top;
}
