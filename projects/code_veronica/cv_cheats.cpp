#include "cv_cheats.h"
#include "cv_version.h"
#include <algorithm>
#include <cstdio>
extern "C" {
extern uint8_t* vm_base;
uint8_t vm_read8(uint64_t);
uint16_t vm_read16(uint64_t);
uint32_t vm_read32(uint64_t);
void vm_write16(uint64_t,uint16_t);
void vm_write32(uint64_t,uint32_t);
}
std::atomic<unsigned> cv_cheats_flags{0};
extern "C" void cv_cheats_set(bool health,bool ammo) {
    const unsigned next=(health?CV_CHEAT_HEALTH:0)|(ammo?CV_CHEAT_AMMO:0);
    const unsigned before=cv_cheats_flags.exchange(next,std::memory_order_relaxed);
    if(before!=next)std::fprintf(stderr,"[CHEATS %s] health=%u ammo=%u\n",CV_TITLE_ID,unsigned(health),unsigned(ammo));
}
namespace {
int character() {
    if(!vm_base)return -1;
    const unsigned c=vm_read8(CV_SRT_CHARACTER);
    constexpr uint32_t markers[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};
    return c<4&&vm_read32(CV_SRT_RDX)==markers[c]?int(c):-1;
}
bool weapon(unsigned id) {
    return (id>=1&&id<=7)||(id>=9&&id<=11)||id==0x20||id==0x21||id==0x22||id==0x83||id==0x8e;
}
bool ammunition(unsigned id) {
    return weapon(id)||(id>=12&&id<=18)||id==0x1e||id==0x24||id==0x25||
        id==0x26||id==0x28||id==0x2a||id==0x8f;
}
bool percent(unsigned id) {return id==2||id==0x22||id==0x8e;}
uint32_t merge(uint32_t old,uint32_t value,unsigned offset,unsigned width) {
    const unsigned shift=(4-offset-width)*8;
    const uint32_t mask=width==4?0xffffffffu:((1u<<(width*8))-1u);
    return (old&~(mask<<shift))|((value&mask)<<shift);
}
uint32_t part(uint32_t word,unsigned offset,unsigned width) {
    const unsigned shift=(4-offset-width)*8;
    return width==4?word:(word>>shift)&((1u<<(width*8))-1u);
}
}
uint32_t cv_cheats_filter(uint64_t a64,uint32_t value,unsigned width,unsigned flags) {
    if(!flags||a64>0xffffffffull||!width||width>4)return value;
    const uint32_t a=uint32_t(a64);
    const bool health=(flags&CV_CHEAT_HEALTH)&&a>=CV_SRT_HEALTH&&a64+width<=uint64_t(CV_SRT_HEALTH)+4;
    const bool inventory=(flags&CV_CHEAT_AMMO)&&a>=CV_SRT_INVENTORY+4&&a<CV_SRT_INVENTORY+4*0x40;
    if(!health&&!inventory)return value;
    const int c=character();if(c<0)return value;
    if(health) {
        const int32_t old=int32_t(vm_read32(CV_SRT_HEALTH));
        const unsigned offset=a-CV_SRT_HEALTH;
        const int32_t next=int32_t(merge(uint32_t(old),value,offset,width));
        // Protect the live player only. Do not resurrect a game-over state,
        // or affect an enemy that happens to use the same actor HP offset.
        if(old>0&&old<=10000&&next<old)return part(uint32_t(old),offset,width);
    } else {
        const uint32_t first=CV_SRT_INVENTORY+uint32_t(c)*0x40+4;
        if(a<first||a64+width>uint64_t(first)+11*4)return value;
        const unsigned offset=(a-first)%4;if(offset+width>4)return value;
        const uint32_t base=a-offset,old=vm_read32(base),next=merge(old,value,offset,width);
        // Different IDs/flags and empty slots are normal moves/removals, not
        // ammo use. Preserve these operations and all non-ammunition consumables.
        if((old&0xffff0000u)==(next&0xffff0000u)&&ammunition((old>>16)&255)&&
           (next&0xffffu)<(old&0xffffu))return part(old,offset,width);
    }
    return value;
}
extern "C" void cv_cheats_frame() {
    const unsigned flags=cv_cheats_flags.load(std::memory_order_relaxed);
    if(!flags)return;
    const int c=character();if(c<0)return;
    if(flags&CV_CHEAT_HEALTH) {
#ifdef CV_REGION_JP
        const int max=vm_read8(CV_SRT_DIFFICULTY)==2?400:200;
#else
        const int max=vm_read8(CV_SRT_DIFFICULTY)==2?320:160;
#endif
        const int hp=int32_t(vm_read32(CV_SRT_HEALTH));
        if(hp>0&&hp<max)vm_write32(CV_SRT_HEALTH,uint32_t(max));
    }
    if(flags&CV_CHEAT_AMMO) {
        const uint32_t first=CV_SRT_INVENTORY+uint32_t(c)*0x40+4;
        for(unsigned i=0;i<11;i++) {
            const uint32_t a=first+i*4,word=vm_read32(a);const unsigned id=(word>>16)&255;
            if(!ammunition(id))continue;
            // Avoid an empty-magazine branch and percentage depletion before
            // its store. Never set a permanent native infinite-item flag.
            const unsigned minimum=percent(id)?100:99;
            if((word&0xffffu)<minimum)vm_write16(a+2,uint16_t(minimum));
        }
    }
}
