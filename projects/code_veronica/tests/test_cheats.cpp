#include "../cv_cheats.h"
#include "../cv_version.h"
#include "../launcher_i18n.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
static std::vector<uint8_t> memory(16*1024*1024);
extern "C" {
uint8_t* vm_base=memory.data();
uint8_t vm_read8(uint64_t a) {assert(a<memory.size());return memory[a];}
uint16_t vm_read16(uint64_t a) {return uint16_t(vm_read8(a))<<8|vm_read8(a+1);}
uint32_t vm_read32(uint64_t a) {return uint32_t(vm_read16(a))<<16|vm_read16(a+2);}
void vm_write8(uint64_t a,uint8_t v) {assert(a<memory.size());memory[a]=v;}
void vm_write16(uint64_t a,uint16_t v) {vm_write8(a,uint8_t(v>>8));vm_write8(a+1,uint8_t(v));}
void vm_write32(uint64_t a,uint32_t v) {vm_write16(a,uint16_t(v>>16));vm_write16(a+2,uint16_t(v));}
}
// Exercise the exact inline wrappers that the build-local lift includes.
#include "../cv_cheats_lift.h"
static void begin(unsigned character=0) {
    std::fill(memory.begin(),memory.end(),0);
    static const uint32_t markers[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};
    cv_cheats_set(false,false);
    vm_write8(CV_SRT_CHARACTER,uint8_t(character));vm_write32(CV_SRT_RDX,markers[character]);
    vm_write32(CV_SRT_HEALTH,88);
}
int main() {
    begin();
    assert(cv_cheats_filter(0xffffffffull,123,4,3)==123);
    assert(cv_cheats_filter(0x100000000ull,123,4,3)==123);
    assert(cv_cheats_filter(CV_SRT_HEALTH,123,0,3)==123);
    const auto original=memory;cv_cheats_frame();assert(memory==original);
    cv_cheats_set(true,false);cv_cheats_frame();
#ifdef CV_REGION_JP
    const unsigned normal=200,easy=400;
#else
    const unsigned normal=160,easy=320;
#endif
    assert(vm_read32(CV_SRT_HEALTH)==normal);
    vm_write32(CV_SRT_HEALTH,normal-50);assert(vm_read32(CV_SRT_HEALTH)==normal);
    vm_write32(CV_SRT_HEALTH,0xffffffffu);assert(vm_read32(CV_SRT_HEALTH)==normal);
    vm_write16(CV_SRT_HEALTH+2,0);assert(vm_read32(CV_SRT_HEALTH)==normal);
    vm_write32(CV_SRT_ENEMIES+0x41c,10);assert(vm_read32(CV_SRT_ENEMIES+0x41c)==10);
    vm_write32(CV_SRT_ENEMIES+0x41c,0);assert(vm_read32(CV_SRT_ENEMIES+0x41c)==0);
    vm_write8(CV_SRT_DIFFICULTY,2);cv_cheats_frame();assert(vm_read32(CV_SRT_HEALTH)==easy);
    cv_cheats_set(false,false);vm_write32(CV_SRT_HEALTH,25);assert(vm_read32(CV_SRT_HEALTH)==25);
    vm_write32(CV_SRT_HEALTH,0xffffffffu);cv_cheats_set(true,false);cv_cheats_frame();
    assert(vm_read32(CV_SRT_HEALTH)==0xffffffffu); // no game-over resurrection
    for(unsigned character=0;character<4;++character) {
        begin(character);
        const uint32_t slot=CV_SRT_INVENTORY+character*0x40+4;
        for(unsigned id:{1u,2u,3u,4u,5u,6u,7u,9u,10u,11u,0x20u,0x21u,0x22u,0x83u,0x8eu}) {
            cv_cheats_set(false,false);vm_write32(slot,(id<<16)|0);
            cv_cheats_set(false,true);cv_cheats_frame();
            const unsigned quantity=id==2||id==0x22||id==0x8e?100:99;
            assert(vm_read32(slot)==(id<<16|quantity)); // no permanent infinite bit
            vm_write16(slot+2,uint16_t(quantity-1));assert(vm_read16(slot+2)==quantity);
            vm_write8(slot+3,uint8_t(quantity-1));assert(vm_read16(slot+2)==quantity);
            vm_write32(slot,(id<<16)|(quantity-1));assert(vm_read16(slot+2)==quantity);
            cv_cheats_set(false,false);vm_write16(slot+2,uint16_t(quantity-1));
            assert(vm_read16(slot+2)==quantity-1);
        }
        for(unsigned id:{12u,13u,14u,15u,16u,17u,18u,0x1eu,0x24u,0x25u,0x26u,0x28u,0x2au,0x8fu}) {
            cv_cheats_set(false,false);vm_write32(slot,(id<<16)|3);
            cv_cheats_set(false,true);cv_cheats_frame();assert(vm_read16(slot+2)==99);
            vm_write16(slot+2,84);assert(vm_read16(slot+2)==99); // reloading preserves reserve
            cv_cheats_set(false,false);vm_write16(slot+2,84);assert(vm_read16(slot+2)==84);
        }
        cv_cheats_set(false,false);vm_write32(slot,0x00140002); // first aid spray
        vm_write32(slot+4,0x003e0001); // key item
        vm_write32(slot+8,0x08090005); // naturally infinite handgun
        cv_cheats_set(false,true);const auto before=memory;cv_cheats_frame();
        // Native flag is preserved; only weapon quantity may be replenished.
        assert(vm_read32(slot)==0x00140002&&vm_read32(slot+4)==0x003e0001);
        assert((vm_read32(slot+8)&0xffff0000u)==0x08090000);
        vm_write16(slot+2,1);assert(vm_read16(slot+2)==1); // consumables still consumed
        const uint32_t moved=vm_read32(slot+8);
        vm_write32(slot+12,moved);vm_write32(slot+8,0);
        assert(vm_read32(slot+8)==0&&vm_read32(slot+12)==moved);
        cv_cheats_set(false,false);vm_write16(slot+14,98);assert(vm_read16(slot+14)==98);
        cv_cheats_set(true,true);
        vm_write32(CV_SRT_RDX,0);const auto loading=memory;cv_cheats_frame();assert(memory==loading);
        vm_write32(CV_SRT_HEALTH,1);assert(vm_read32(CV_SRT_HEALTH)==1); // outside gameplay
    }
    for(int lang=0;lang<LANG_COUNT;++lang)
        for(int id=S_CHEAT_HP_T;id<=S_CHEAT_AMMO_D;++id)assert(k_str[lang][id]&&*k_str[lang][id]);
    std::printf("[CHEAT-TEST %s] PASS: off/live toggles, HP/death, enemies untouched, all weapons/reserves/percent/4 characters, byte-half-word stores, items/moves/loading/native flags, 13 locales\n",CV_TITLE_ID);
}
