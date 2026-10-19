/* Bounded UI reuse for render frames between native menu updates. */
#ifndef RSX_UI_CADENCE_H
#define RSX_UI_CADENCE_H
#include <stdint.h>
/* Select the newest published batch that belongs to this frame or up to
 * max_age earlier flips. Empty batches are tombstones, never bypassed.
 * Unsigned age handles counter wrap and rejects batches from the future. */
static inline int rsx_ui_select_batch(uint32_t frame,const uint32_t* stamps,
    const uint8_t* valid,uint32_t slots,uint32_t max_age) {
    int best=-1;uint32_t age_best=UINT32_MAX;
    for(uint32_t i=0;i<slots;i++) {
        if(!valid[i])continue;
        uint32_t age=frame-stamps[i];
        if(age<=max_age&&(best<0||age<age_best)){best=(int)i;age_best=age;}
    }
    return best;
}
static inline int rsx_ui_menu_contiguous(uint32_t frame,uint32_t previous,int interpolated) {
    uint32_t gap=frame-previous;
    return gap>=1&&gap<=(interpolated?4u:1u);
}
#endif
