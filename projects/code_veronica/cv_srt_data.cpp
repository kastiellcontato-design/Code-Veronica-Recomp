/* Native SRT reader, written for this port. No injected code or guest writes.
 * Address/type facts and boss state rules studied in re-cvx-srt-provider,
 * Kapdap, MIT: https://github.com/kapdap/re-cvx-srt-provider .
 * This is an independent implementation; the external scanner is not linked.
 */
#include "cv_srt.h"
#include "cv_srt_enemy_hold.h"
#include <chrono>
#include "cv_version.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
extern "C" {
extern uint8_t* vm_base;
uint8_t vm_read8(uint64_t);
uint16_t vm_read16(uint64_t);
uint32_t vm_read32(uint64_t);
}
namespace {
bool readable(uint32_t address,size_t size) {
    if(!vm_base || address>0x0FFFFFFF || size>0x10000000-address)return false;
#ifdef _WIN32
    const auto* p=vm_base+address;const auto* end=p+size;
    while(p<end) { MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(p,&info,sizeof info)||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        const auto* next=static_cast<const uint8_t*>(info.BaseAddress)+info.RegionSize;
        if(next<=p)return false;p=next;
    }
#endif
    return true;
}
int maximum(int type,int room) {
    switch(type) {
    case 2:return 300;case 12:return 300;case 13:return 700;case 14:return 400;
    case 15:return 600;case 17:case 22:case 23:return 250;
    case 19:return room==0x0501?700:500;case 26:return 200;case 29:return 160;
    default:return 0;
    }
}
bool known_type(int n) {
    switch(n) {case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 9:case 12:case 13:case 14:case 15:case 17:case 19:case 21:case 22:case 23:case 26:case 29:case 30:return true;default:return false;}
}
bool has_quantity(int id) {
    return (id>=1&&id<=7)||(id>=9&&id<=0x12)||id==0x1e||id==0x1f||
        (id>=0x20&&id<=0x26)||id==0x28||id==0x2a||id==0x78||id==0x82||id==0x83||id==0x8e||id==0x8f||id==0x98||id==0x99;
}
struct Seen { int type=0,slot=0,hp=0,alive=0; };
Seen seen[CV_SRT_MAX_ENEMIES];int previous_room=-1,previous_character=-1;
cv_srt_enemy_hold::Cache enemy_display;
}
static int sample_current(cv_srt_state* out) {
    *out={};
    if(!readable(CV_SRT_CHARACTER,0x900)||!readable(CV_SRT_RDX,0xa00))return 0;
    const int character=vm_read8(CV_SRT_CHARACTER);
    if(character>3)return 0;
    static const uint32_t rdx[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};
    const int room=vm_read16(CV_SRT_ROOM);
    if(vm_read32(CV_SRT_RDX)!=rdx[character]) {previous_room=-1;return 0;}
    const int difficulty=vm_read8(CV_SRT_DIFFICULTY);
#ifdef CV_REGION_JP
    const int maxhp=difficulty==2?400:200;
#else
    const int maxhp=difficulty==2?320:160;
#endif
    const int hp=int32_t(vm_read32(CV_SRT_HEALTH));
    if(hp < -10000 || hp>10000)return 0;
    out->valid=1;out->character=character;out->room=room;out->maximum=maxhp;
    out->hp=std::max(0,hp);out->seconds=vm_read32(CV_SRT_TIME)/60;
    out->saves=std::max(0,int32_t(vm_read32(CV_SRT_SAVES)));
    out->retry=std::max(0,int(int16_t(vm_read16(CV_SRT_RETRY))));
    out->fas=std::max(0,int(int16_t(vm_read16(CV_SRT_FAS))));
    const int status=vm_read8(CV_SRT_STATUS);
    out->condition=hp<0?SRT_DEAD:(status&0x20)?SRT_GAS:(status&8)?SRT_POISON:
        hp*100>=maxhp*60?SRT_FINE:hp*100>=maxhp*30?SRT_CAUTION:hp*100>=maxhp*15?SRT_WARNING:SRT_DANGER;
    const uint32_t inventory=CV_SRT_INVENTORY+character*0x40;
    if(readable(inventory,48)) {
        const int equipped=int32_t(vm_read32(inventory));
        out->equipped=equipped>=1&&equipped<=CV_SRT_INVENTORY_SIZE?equipped:0;
        for(int i=0;i<CV_SRT_INVENTORY_SIZE;i++) {
            const uint32_t value=vm_read32(inventory+4+i*4);
            cv_srt_item& item=out->inventory[i];item.id=(value>>16)&255;
            if(item.id>0x99) {item={};continue;}
            item.quantity=int(value&0xffff);item.infinite=((value>>24)&8)!=0;
            item.wide=item.id==1||item.id==2||item.id==3||item.id==0x21||item.id==0x22||item.id==0x8e;
            item.counted=has_quantity(item.id);
        }
    }
    if(previous_room!=room || previous_character!=character) {
        std::memset(seen,0,sizeof seen);previous_room=room;previous_character=character;
    }
    std::array<cv_srt_enemy_hold::Observation,CV_SRT_MAX_ENEMIES> observed{};
    const int count=std::clamp(int32_t(vm_read32(CV_SRT_ENEMY_COUNT)),0,int(CV_SRT_MAX_ENEMIES));
    bool leader_alive=false,leader_seen=false;
    for(int i=0;i<count;i++) {
        const uint32_t address=CV_SRT_ENEMIES+i*0x578;
        if(!readable(address,0x420))break;
        const int type=int16_t(vm_read16(address+4));
        if(!known_type(type)) {seen[i]={};continue;}
        const int enemy_hp=int32_t(vm_read32(address+0x41c));
        const int action=vm_read8(address+0xc),status=vm_read8(address+0xf),model=vm_read8(address+0x88);
        const int slot=int32_t(vm_read32(address+0x39c));
        auto& observation=observed[i];
        observation.identified=true;observation.type=type;observation.slot=slot;
        observation.remove=enemy_hp<=0;
        const bool active=action>0&&action<4;
        bool alive=active&&status>0;
        switch(type) {
        case 5:case 15:case 17:case 22:alive=active&&model==0;break;
        case 19:alive=active&&enemy_hp>=0;break;
        case 12:alive=active&&room!=0x091e;break;
        case 13:alive=active;break;case 14:alive=!leader_alive;break;
        case 29:alive=active&&enemy_hp>=0&&model==0&&room!=0x091e;break;
        case 30:alive=leader_alive;break;
        }
        alive=alive&&enemy_hp>=0&&enemy_hp<=100000;
        // These existing rules explicitly exclude another boss form/room,
        // rather than indicating a briefly missing actor. Do not retain it.
        observation.remove=observation.remove || (type==14&&!alive) ||
            (type==30&&!alive) || ((type==12||type==29)&&room==0x091e);
        if(!leader_seen) {leader_alive=alive;leader_seen=true;}
        Seen& history=seen[i];
        if(history.type!=type||history.slot!=slot||(!history.alive&&alive))history={type,slot,std::max(1,enemy_hp),0};
        history.alive=alive;history.hp=std::max(history.hp,enemy_hp);
        if(!alive)continue;
        auto& enemy=out->enemies[out->enemy_count++];enemy.type=type;enemy.hp=enemy_hp;
        const int fixed=maximum(type,room);enemy.estimated=!fixed;enemy.maximum=fixed?fixed:std::max(1,history.hp);
        observation.shown=true;observation.enemy=enemy;
    }
    if(vm_read8(CV_SRT_CHARACTER)!=character || vm_read16(CV_SRT_ROOM)!=room ||
       vm_read32(CV_SRT_RDX)!=rdx[character]) {*out={};previous_room=-1;return 0;}
    const uint64_t now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    enemy_display.apply(*out,observed,now);
    if(const char* log=std::getenv("SRT_LOG");log&&*log&&*log!='0') {
        static unsigned n; if(++n%30==1)std::fprintf(stderr,"[SRT %s] char=%d hp=%d/%d state=%d time=%u room=%04X enemies=%d equip=%d difficulty=%d\n",CV_TITLE_ID,character,hp,maxhp,out->condition,out->seconds,room,out->enemy_count,out->equipped,difficulty);
    }
    return 1;
}

extern "C" int cv_srt_sample(cv_srt_state* out) {
    const int valid=sample_current(out);
    if(!valid) {
        // Menus/loading/invalid reads must never retain enemies from a prior game.
        enemy_display.clear();previous_room=previous_character=-1;
    }
    return valid;
}
