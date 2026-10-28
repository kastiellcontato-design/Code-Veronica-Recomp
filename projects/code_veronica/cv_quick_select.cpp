// Independent PS3 implementation, inspired by recvx-enhanced-port knife_glue.c.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstdio>
#include "cv_quick_select.h"
#include "ppu_recomp.h"
#include "cv_version.h"
extern "C" {
extern uint8_t* vm_base;extern int g_pad_block;void* rsx_d3d12_window();
uint8_t vm_read8(uint64_t);uint16_t vm_read16(uint64_t);uint32_t vm_read32(uint64_t);void vm_write64(uint64_t,uint64_t);
uint32_t cv_manual_reload_physical_buttons();uint32_t cv_quick_select_stick();
void cv_quick_pins_save(uint32_t packed);
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;extern PPU_THREAD_LOCAL void(*g_trampoline_fn)(void*);
}
#if defined(CV_REGION_JP)
constexpr uint32_t quick_player_ptr=0xA79C3C,quick_swork=0xCB0000-30432;
#define QUICK_EQUIP func_001A4CD0
#elif defined(CV_REGION_US)
constexpr uint32_t quick_player_ptr=0xA79BD8,quick_swork=0xCB0000-30560;
#define QUICK_EQUIP func_001A4C28
#else
constexpr uint32_t quick_player_ptr=0xA89BD8,quick_swork=0xCC0000-29792;
#define QUICK_EQUIP func_001A6680
#endif
void QUICK_EQUIP(ppu_context*);
namespace cv_quick {
namespace {
std::mutex guard;Config settings;View published;
std::atomic<unsigned> generation{0};std::atomic<uint64_t> input_until{0};
bool readable(uint32_t a,size_t n){if(!vm_base||a>0xfffffff||n>0x10000000-a)return false;const auto* p=vm_base+a;const auto* end=p+n;while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(p,&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;auto* next=(const uint8_t*)m.BaseAddress+m.RegionSize;if(next<=p)return false;p=next;}return true;}
bool key(int k){return k>0&&(GetAsyncKeyState(k)&0x8000);}
bool held(const Config& c,unsigned buttons){return key(c.key)||(c.button>=0&&(buttons&(1u<<c.button)));}
bool live(const cv_items::Inventory& inv){return inv.valid&&inv.playing&&!g_pad_block&&GetForegroundWindow()==(HWND)rsx_d3d12_window();}
bool rest(uint32_t p){return readable(p,0x400)&&vm_read8(p+0xc)==1&&vm_read8(p+0xd)==0&&vm_read8(p+0xe)<=11&&!(vm_read32(p)&0x10000);}
bool equip(ppu_context* caller,const View& menu){
 if(menu.selected<0||menu.selected>3)return false;const int index=menu.slots[menu.selected];
 const auto inv=cv_items::read();if(!live(inv)||inv.character!=menu.inventory.character||index<0||index>inv.capacity)return false;
 // The selected item must still be in the same entry. Never inject an item.
 if(inv.entry[index]!=menu.inventory.entry[index]||!equipable(inv.id(index)))return false;
 if(inv.equipped==index+1)return true; // Native WeaponSet toggles off; avoid that.
 if(!readable(quick_player_ptr,4)||!readable(quick_swork,0x84)||!readable(cv_items::state_base()+0x1b104,4))return false;
 const auto player=vm_read32(quick_player_ptr),iv=CV_SRT_INVENTORY+inv.character*0x40;
 if(!rest(player)||vm_read32(quick_player_ptr-8)!=cv_items::state_base()||vm_read32(quick_swork+0x80)!=iv)return false;
 ppu_context work=*caller;work.gpr[1]-=0x400;work.gpr[2]=CV_TOC;work.gpr[3]=index+1;work.gpr[4]=index+1;vm_write64(work.gpr[1],caller->gpr[1]);
 auto* saved=g_active_ctx;auto tail=g_trampoline_fn;g_active_ctx=&work;g_trampoline_fn=nullptr;
 QUICK_EQUIP(&work);while(g_trampoline_fn){auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&work);}g_active_ctx=saved;g_trampoline_fn=tail;
 std::fprintf(stderr,"[QUICK-EQUIP] native region=%s item=%02X slot=%d character=%d\n",CV_TITLE_ID,inv.id(index),index+1,inv.character);return true;
}
}
Config config(){std::lock_guard<std::mutex> lock(guard);return settings;}
void configure(const Config& c){std::lock_guard<std::mutex> lock(guard);settings=c;settings.on=c.on!=0;settings.key=std::clamp(c.key,0,255);settings.button=std::clamp(c.button,-1,17);settings.edit_key=std::clamp(c.edit_key,0,255);settings.edit_button=std::clamp(c.edit_button,-1,17);settings.confirm_key=std::clamp(c.confirm_key,0,255);settings.confirm_button=std::clamp(c.confirm_button,-1,17);for(int i=0;i<4;i++){settings.keys[i]=std::clamp(c.keys[i],0,255);settings.items[i]=(c.items[i]==0||c.items[i]==-1||equipable(c.items[i]))?c.items[i]:0;}input_until=0;generation++;}
View view(){std::lock_guard<std::mutex> lock(guard);return published;}
bool equipable(int id){switch(id){case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 10:case 11:case 32:case 33:case 34:case 55:case 131:case 142:return true;default:return false;}}
void resolve(View& v){
 bool used[cv_items::kEntries]={};std::fill_n(v.slots,4,-1);
 auto find=[&](int id){for(int i=0;i<=v.inventory.capacity;i++)if(!used[i]&&v.inventory.id(i)==id&&equipable(id))return i;return -1;};
 for(int d=0;d<4;d++)if(v.config.items[d]>0){v.wanted[d]=v.config.items[d];v.slots[d]=find(v.wanted[d]);if(v.slots[d]>=0)used[v.slots[d]]=true;}
 for(int d=0;d<4;d++)if(v.config.items[d]==0){int i=d==0?find(55):d==2?find(8):-1;
  if(i<0)for(int j=0;j<=v.inventory.capacity;j++)if(!used[j]&&equipable(v.inventory.id(j))&&!(d!=2&&v.config.items[2]==0&&v.inventory.id(j)==8)){i=j;break;}
  v.slots[d]=i;v.wanted[d]=i>=0?v.inventory.id(i):0;if(i>=0)used[i]=true;
 }else if(v.config.items[d]<0)v.wanted[d]=0;
}
bool consumes_stick(){if(GetTickCount64()>=input_until||g_pad_block)return false;const auto c=config();const auto buttons=cv_manual_reload_physical_buttons();return c.on&&(buttons&0xf0u)!=0xf0u&&held(c,buttons);}
int key_consumed(int vk){if(!consumes_stick())return false;const auto c=config();if(vk==c.key||(c.edit_key>0&&vk==c.edit_key)||(c.confirm_key>0&&vk==c.confirm_key))return true;for(int k:c.keys)if(k>0&&vk==k)return true;return false;}
unsigned filter_buttons(unsigned buttons){const auto c=config();if(!consumes_stick())return buttons;if(c.button>=0)buttons&=~(1u<<c.button);if(c.edit_button>=0)buttons&=~(1u<<c.edit_button);if(c.confirm_button>=0)buttons&=~(1u<<c.confirm_button);return buttons;}
void edit_choices(View& v){
 v.choice_count=1;v.choices[0]=-1;v.choice=0;
 for(int i=0;i<=v.inventory.capacity;i++)if(equipable(v.inventory.id(i))){bool duplicate=false;for(int j=1;j<v.choice_count;j++)if(v.inventory.id(v.choices[j])==v.inventory.id(i))duplicate=true;if(!duplicate&&v.choice_count<12){v.choices[v.choice_count]=i;if(v.selected>=0&&v.slots[v.selected]==i)v.choice=v.choice_count;v.choice_count++;}}
}
bool assign_choice(View& v){
 if(v.selected<0||v.selected>3||v.choice<0||v.choice>=v.choice_count)return false;
 const int entry=v.choices[v.choice],id=entry<0?-1:v.inventory.id(entry);if(entry>=0&&!equipable(id))return false;
 // Resolve Automatic before editing, so a moved weapon never refills its old slot.
 int pins[4];for(int d=0;d<4;d++)pins[d]=v.config.items[d]>0?v.config.items[d]:v.slots[d]>=0?v.inventory.id(v.slots[d]):-1;
 int origin=-1;if(id>0)for(int d=0;d<4;d++)if(d!=v.selected&&pins[d]==id)origin=d;
 if(origin>=0)pins[origin]=pins[v.selected];pins[v.selected]=id;
 uint32_t packed=0;
 {std::lock_guard<std::mutex> lock(guard);for(int d=0;d<4;d++){settings.items[d]=pins[d];v.config.items[d]=pins[d];packed|=uint32_t(pins[d]+1)<<(d*8);}}
 resolve(v);cv_quick_pins_save(packed);return true;
}
void frame(ppu_context* caller){
 static View menu;static bool was=false,require_release=false,from_pad=false,edit_was=false,confirm_was=false,wait_neutral=false;static unsigned seen=~0u;static int nav_previous=-1;static uint64_t repeat_at=0;
 const auto c=config();const uint64_t now=GetTickCount64();
 if(!c.on){input_until=0;was=false;menu={};std::lock_guard<std::mutex> lock(guard);published={};return;}
 const auto inv=cv_items::read();const auto buttons=cv_manual_reload_physical_buttons();
 const bool chord=(buttons&0xf0u)==0xf0u;const bool down=held(c,buttons);
 const bool edit_down=(c.edit_key!=c.key&&key(c.edit_key))||(c.edit_button>=0&&c.edit_button!=c.button&&(buttons&(1u<<c.edit_button)));const bool edit_pressed=edit_down&&!edit_was;edit_was=edit_down;
 const bool confirm_down=(c.confirm_key!=c.key&&key(c.confirm_key))||(c.confirm_button>=0&&c.confirm_button!=c.button&&(buttons&(1u<<c.confirm_button)));const bool confirm_pressed=confirm_down&&!confirm_was;confirm_was=confirm_down;
 if(seen!=generation){seen=generation;menu={};was=false;require_release=down;edit_was=edit_down;confirm_was=confirm_down;wait_neutral=false;}
 if(!down)require_release=false;
 const bool valid=c.on&&live(inv)&&!chord;
 if(!valid||(menu.visible&&(menu.inventory.character!=inv.character||menu.room!=vm_read16(CV_SRT_ROOM)||(from_pad&&!key(c.key)&&!(cv_quick_select_stick()&0x10000))))){menu={};input_until=0;was=down;require_release=down;}
 else {
  if(down&&!was&&!require_release&&readable(quick_player_ptr,4)&&rest(vm_read32(quick_player_ptr))){menu={};menu.config=c;menu.inventory=inv;menu.room=vm_read16(CV_SRT_ROOM);from_pad=c.button>=0&&(buttons&(1u<<c.button));resolve(menu);menu.visible=true;menu.opened=now;}
  if(menu.visible){
   input_until=now+100;
   // Keep quantities current, but cancel if the selected inventory layout changed.
   bool changed=false;for(int i=0;i<cv_items::kEntries;i++)if((menu.inventory.entry[i]&0xffff0000)!=(inv.entry[i]&0xffff0000))changed=true;
   if(changed){menu.visible=false;menu.closed=now;menu.selected=-1;require_release=down;}
   else {
    menu.inventory=inv;int direction=-1;const auto stick=cv_quick_select_stick();const int x=int(stick&255)-128,y=int((stick>>8)&255)-128;
    if(std::max(abs(x),abs(y))>58)direction=abs(x)>abs(y)?(x>0?1:3):(y>0?2:0);
    for(int d=0;d<4;d++)if(c.keys[d]!=c.key&&c.keys[d]!=c.edit_key&&c.keys[d]!=c.confirm_key&&key(c.keys[d]))direction=d;
    if(menu.editing){
     if(!down&&was){menu.editing=false;menu.visible=false;menu.closed=now;input_until=0;}
     else if(confirm_pressed){if(assign_choice(menu)){menu.editing=false;wait_neutral=true;}}
     else if(direction>=0&&(direction!=nav_previous||now>=repeat_at)){menu.choice=(menu.choice+(direction==0||direction==3?-1:1)+menu.choice_count)%menu.choice_count;repeat_at=now+(direction!=nav_previous?400:160);}
     nav_previous=direction;
    }else{
     if(direction<0)wait_neutral=false;
     if(direction>=0&&!wait_neutral)menu.selected=direction;
     if(edit_pressed&&down&&menu.selected>=0){menu.editing=true;edit_choices(menu);nav_previous=direction;repeat_at=now+400;}
     else if(!down&&was){if(menu.selected>=0&&!equip(caller,menu))std::fprintf(stderr,"[QUICK-EQUIP] selection refused: animation/state changed\n");menu.visible=false;menu.closed=now;input_until=0;}
    }
   }
  }else input_until=readable(quick_player_ptr,4)&&rest(vm_read32(quick_player_ptr))?now+100:0;
  was=down;
 }
 if(!c.on||!live(inv)){menu={};input_until=0;}
 {std::lock_guard<std::mutex> lock(guard);published=menu;}
}
}
extern "C" int cv_quick_key_consumed(int key){return cv_quick::key_consumed(key);}
extern "C" unsigned cv_quick_filter_buttons(unsigned buttons){return cv_quick::filter_buttons(buttons);}
extern "C" int cv_quick_consumes_stick(){return cv_quick::consumes_stick();}
