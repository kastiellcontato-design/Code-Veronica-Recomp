#include "cv_items.h"
#include "cv_version.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
extern "C" {
extern uint8_t* vm_base;
uint8_t vm_read8(uint64_t);
uint16_t vm_read16(uint64_t);
uint32_t vm_read32(uint64_t);
void vm_write32(uint64_t, uint32_t);
}
namespace cv_items {
namespace {
// Weapons hold their loaded rounds; the grenade launcher keeps the round type
// in the top two bits of its count (0x00 normal), so it stays under 64.
const std::vector<Info> k_catalog = {
    {9,WEAPONS,true,15,250},{0x83,WEAPONS,true,15,250},{0x0A,WEAPONS,true,15,250},{5,WEAPONS,true,18,250},
    {0x21,WEAPONS,true,20,250},{4,WEAPONS,true,7,250},{0x20,WEAPONS,true,6,250},{6,WEAPONS,true,6,50},
    {7,WEAPONS,true,30,250},{3,WEAPONS,true,7,250},{2,WEAPONS,true,100,100,true},{0x22,WEAPONS,true,100,100,true},
    {0x8E,WEAPONS,true,100,100,true},{1,WEAPONS,true,4,250},{0x0B,WEAPONS,true,5,250},{8,WEAPONS,false,0,0},
    {0x0C,AMMO,true,30,250},{0x0D,AMMO,true,6,250},{0x1E,AMMO,true,6,250},{0x0E,AMMO,true,7,250},
    {0x0F,AMMO,true,6,250},{0x10,AMMO,true,6,250},{0x11,AMMO,true,6,250},{0x25,AMMO,true,6,250},
    {0x12,AMMO,true,30,250},{0x24,AMMO,true,10,250},{0x23,AMMO,true,1,250},{0x82,AMMO,true,1,250},
    {0x26,AMMO,true,100,250},{0x28,AMMO,true,10,250},{0x2A,AMMO,true,100,250},{0x8F,AMMO,true,100,250},
    {0x14,HEALING,false,1,1},{0x15,HEALING,false,1,1},{0x16,HEALING,false,1,1},{0x17,HEALING,false,1,1},
    {0x18,HEALING,false,1,1},{0x19,HEALING,false,1,1},{0x1A,HEALING,false,1,1},{0x1B,HEALING,false,1,1},
    {0x1C,HEALING,false,1,1},{0x1D,HEALING,false,1,1},
    {0x1F,OTHER,true,3,99},{0x39,OTHER,false,1,1},{0x4A,OTHER,false,1,1},{0x3A,OTHER,false,1,1},
    {0x2F,OTHER,false,1,1},{0x30,OTHER,false,1,1},{0x31,OTHER,false,1,1},{0x66,OTHER,false,1,1},
    {0x6A,OTHER,false,1,1},{0x7D,OTHER,false,1,1},{0x84,OTHER,false,1,1},{0x85,OTHER,false,1,1},
    {0x86,OTHER,false,1,1},{0x87,OTHER,false,1,1},{0x8B,OTHER,false,1,1},{0x91,OTHER,false,1,1},
    {0x3F,OTHER,false,1,1},{0x80,OTHER,false,1,1},{0x81,OTHER,false,1,1},
};
std::vector<Info> build() {
    std::vector<Info> all=k_catalog;
    // Every other named object is a key item: the story's keys, emblems,
    // jewels and parts. 0x8D/0x92/0x93/0x94/0x97 are unknown and left out.
    for(int id=0x13;id<=0x99;id++) {
        if(id>0x13&&id<0x27)continue;
        if(id==0x8D||id==0x92||id==0x93||id==0x94||id==0x97)continue;
        if(std::any_of(all.begin(),all.end(),[&](const Info& i){return i.id==id;}))continue;
        const bool counted=id==0x78||id==0x98||id==0x99;
        all.push_back({id,KEY_ITEMS,counted,1,counted?9:1});
    }
    return all;
}
bool mapped(uint32_t address,uint32_t size) {
    if(!vm_base||address>0x0FFFFFFF||size>0x10000000-address)return false;
#ifdef _WIN32
    const auto* p=vm_base+address;const auto* end=p+size;
    while(p<end) {MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(p,&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        const auto* next=static_cast<const uint8_t*>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;}
#endif
    return true;
}
std::atomic<bool> g_enabled{false};
struct Request { int character,id,quantity,entry;uint32_t expected;bool remove; };
std::mutex g_lock;   // the queue and the outcomes: the menu's thread and the game's
std::vector<Request> g_queue;
std::vector<Outcome> g_outcomes;unsigned g_serial=0;   // g_outcomes[i] has serial g_serial-size+i+1
void publish(Result r,const Request& q) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_outcomes.push_back({r,q.entry,q.id,q.remove});g_serial++;
    if(g_outcomes.size()>32)g_outcomes.erase(g_outcomes.begin());
}
}
const std::vector<Info>& catalog() {static const std::vector<Info> all=build();return all;}
const Info* info(int id) {
    for(const auto& i:catalog())if(i.id==id)return &i;
    return nullptr;
}
std::vector<int> items(Category category) {
    std::vector<int> out;for(const auto& i:catalog())if(i.category==category)out.push_back(i.id);
    return out;
}
int Inventory::first_empty() const {
    for(int i=1;i<=capacity;i++)if(empty(i))return i;
    return -1;
}
int Inventory::used() const {
    int n=0;for(int i=1;i<=capacity;i++)n+=!empty(i);
    return n;
}
uint32_t state_base() {return CV_SRT_CHARACTER-0x94;}
Inventory read() {
    Inventory out;
    const uint32_t state=state_base();
    if(!mapped(state,0x300)||!mapped(CV_SRT_RDX,4)||!mapped(CV_SRT_HEALTH,4))return out;
    static constexpr uint32_t rdx[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};
    const unsigned character=vm_read8(CV_SRT_CHARACTER);
    if(character>3||vm_read32(CV_SRT_RDX)!=rdx[character])return out;
    const uint32_t base=CV_SRT_INVENTORY+character*0x40;
    if(!mapped(base,4+kEntries*4))return out;
    out.valid=true;out.character=int(character);
    out.capacity=(vm_read32(state+0x6C)&0x08000000u)?10:8;
    const int equipped=int32_t(vm_read32(base));
    out.equipped=equipped>=1&&equipped<=kEntries?equipped:0;
    for(int i=0;i<kEntries;i++)out.entry[i]=vm_read32(base+4+i*4);
    // As cv_test_tools' active_game(): the game's task word runs the room
    // (0x80) and none of the inventory/map/file/option screens, the save
    // screen, a door, an event or a fade (0x1803F, +70/+74 flags).
    const uint32_t tasks=vm_read32(state+0x64)&~vm_read32(state+0x68);
    out.playing=int32_t(vm_read32(CV_SRT_HEALTH))>0&&(tasks&0x80)&&!(tasks&0x1803F)&&
        !(vm_read32(state+0x74)&7)&&!(vm_read32(state+0x70)&0x1C040008u);
    return out;
}
int clamp_quantity(int id,int quantity) {
    const Info* item=info(id);if(!item)return 0;
    if(!item->counted)return item->quantity;
    return std::clamp(quantity,1,item->maximum);
}
uint32_t encode(int id,int quantity) {
    return uint32_t(id&255)<<16|uint32_t(clamp_quantity(id,quantity)&0xffff);
}
int place(const Inventory& inventory,int id,int target,Result& why) {
    why=R_NONE;
    if(!info(id)||target<0||target>inventory.capacity) {why=R_BAD;return -1;}
    if(!inventory.valid||!inventory.playing) {why=R_NOT_PLAYING;return -1;}
    // An empty entry chosen past the list's end still goes to its end: the
    // list stays packed, as the game's own removal keeps it.
    if(target>0&&!inventory.empty(target)) {
        if(inventory.is_equipped(target)) {why=R_EQUIPPED;return -1;}
        return target;
    }
    const int free=inventory.first_empty();
    if(free<0)why=R_FULL;
    return free;
}
void set_enabled(bool on) {
    g_enabled=on;
    if(!on){std::lock_guard<std::mutex> hold(g_lock);g_queue.clear();}
}
bool enabled() {return g_enabled.load();}
Result request(int character,int id,int quantity,int entry,uint32_t expected) {
    if(!g_enabled)return R_DISABLED;
    if(character<0||character>3||!info(id)||entry<1||entry>10)return R_BAD;
    std::lock_guard<std::mutex> hold(g_lock);
    if(int(g_queue.size())>=kQueue)return R_BUSY;
    g_queue.push_back({character,id,clamp_quantity(id,quantity),entry,expected,false});
    return R_QUEUED;
}
Result request_remove(int character,int entry,uint32_t expected) {
    if(!g_enabled)return R_DISABLED;
    const int id=int((expected>>16)&255);
    if(character<0||character>3||!id||entry<1||entry>10)return R_BAD;
    std::lock_guard<std::mutex> hold(g_lock);
    if(int(g_queue.size())>=kQueue)return R_BUSY;
    g_queue.push_back({character,id,0,entry,expected,true});
    return R_QUEUED;
}
int pending() {std::lock_guard<std::mutex> hold(g_lock);return int(g_queue.size());}
namespace {
// One queued change on an inventory: the same rules for what the menu shows
// and for what the game frame writes. Unchanged unless R_DONE.
Result change(Inventory& inv,const Request& r) {
    if(inv.character!=r.character)return R_CHANGED;
    if(r.entry>inv.capacity)return R_BAD;
    if(inv.is_equipped(r.entry))return R_EQUIPPED;
    // The entry still holds what the menu saw (an empty one is still the
    // list's first empty), or the game changed it: change nothing unseen.
    if(inv.entry[r.entry]!=r.expected||(inv.empty(r.entry)&&inv.first_empty()!=r.entry))return R_CHANGED;
    if(!r.remove) {inv.entry[r.entry]=encode(r.id,r.quantity);return R_DONE;}
    if(inv.empty(r.entry))return R_CHANGED;
    // As the game takes a used-up item out (EU func_0012C224): the entries
    // below move up one, and an equipped entry below follows its weapon.
    for(int i=r.entry;i<kEntries-1;i++)inv.entry[i]=inv.entry[i+1];
    inv.entry[kEntries-1]=0;
    if(inv.equipped>r.entry+1)inv.equipped--;
    return R_DONE;
}
}
void apply_pending(Inventory& inventory) {
    std::lock_guard<std::mutex> hold(g_lock);
    for(const auto& r:g_queue)change(inventory,r);
}
std::vector<Outcome> outcomes(unsigned& serial) {
    std::lock_guard<std::mutex> hold(g_lock);
    const unsigned first=g_serial-unsigned(g_outcomes.size());
    std::vector<Outcome> out;
    for(size_t i=0;i<g_outcomes.size();i++)if(first+unsigned(i)+1>serial)out.push_back(g_outcomes[i]);
    serial=g_serial;return out;
}
unsigned outcome_serial() {std::lock_guard<std::mutex> hold(g_lock);return g_serial;}
void frame() {
    std::vector<Request> work;
    {std::lock_guard<std::mutex> hold(g_lock);if(g_queue.empty())return;work.swap(g_queue);}
    for(const auto& r:work) {
        if(!g_enabled) {publish(R_DISABLED,r);continue;}
        const Inventory now=read();
        if(!now.valid||!now.playing) {publish(R_NOT_PLAYING,r);continue;}
        Inventory next=now;const Result result=change(next,r);
        if(result!=R_DONE) {publish(result,r);continue;}
        const uint32_t base=CV_SRT_INVENTORY+uint32_t(r.character)*0x40;
        for(int i=1;i<kEntries;i++)if(next.entry[i]!=now.entry[i]) {
            vm_write32(base+4+uint32_t(i)*4,next.entry[i]);
            std::fprintf(stderr,"[ITEMS %s] character %d entry %d: %08X -> %08X\n",CV_TITLE_ID,r.character,i,now.entry[i],next.entry[i]);
        }
        if(next.equipped!=now.equipped)vm_write32(base,uint32_t(next.equipped));
        publish(R_DONE,r);
    }
}
}  // namespace cv_items
