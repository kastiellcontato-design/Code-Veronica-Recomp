#pragma once
// Included after the lift's own load helpers. Only verified read sites use
// this function; saved progress and every original store remain untouched.
#include "cv_enhanced_settings.h"
#if defined(CV_REGION_JP)
static constexpr uint32_t CV_BATTLE_SYS_POINTER=0x00A79C34u;
#elif defined(CV_REGION_US)
static constexpr uint32_t CV_BATTLE_SYS_POINTER=0x00A79BD0u;
#else
static constexpr uint32_t CV_BATTLE_SYS_POINTER=0x00A89BD0u;
#endif
static inline uint32_t cv_battle_read32(uint64_t address,unsigned mask){
    const uint32_t value=vm_read32(address);
    if(!cv_battle_unlocked.load(std::memory_order_relaxed))return value;
    const uint32_t sys=vm_read32(CV_BATTLE_SYS_POINTER);
    return sys>=0x10000u&&sys<0x10000000u&&address==uint64_t(sys)+4?value|mask:value;
}
