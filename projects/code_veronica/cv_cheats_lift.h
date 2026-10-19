#pragma once
// Included AFTER the lift's memory helpers/macros, before any guest function.
// Disabled: keep the original inline coherent store, with one unlikely branch.
#include "cv_cheats.h"
#include "cv_version.h"
static inline uint32_t cv_cheats_store_value(uint64_t a,uint32_t v,unsigned width) {
    const unsigned flags=cv_cheats_flags.load(std::memory_order_relaxed);
    if(__builtin_expect(flags!=0,0)) {
        const bool hp=(flags&CV_CHEAT_HEALTH)&&a>=CV_SRT_HEALTH&&a<CV_SRT_HEALTH+4;
        const bool ammo=(flags&CV_CHEAT_AMMO)&&a>=CV_SRT_INVENTORY+4&&a<CV_SRT_INVENTORY+4*0x40;
        if(hp||ammo)return cv_cheats_filter(a,v,width,flags);
    }
    return v;
}
static inline void cv_cheats_store8(uint64_t a,uint8_t v) {vm_write8(a,uint8_t(cv_cheats_store_value(a,v,1)));}
static inline void cv_cheats_store16(uint64_t a,uint16_t v) {vm_write16(a,uint16_t(cv_cheats_store_value(a,v,2)));}
static inline void cv_cheats_store32(uint64_t a,uint32_t v) {vm_write32(a,cv_cheats_store_value(a,v,4));}
#undef vm_write8
#undef vm_write16
#undef vm_write32
#define vm_write8(a,v) cv_cheats_store8((a),(v))
#define vm_write16(a,v) cv_cheats_store16((a),(v))
#define vm_write32(a,v) cv_cheats_store32((a),(v))
