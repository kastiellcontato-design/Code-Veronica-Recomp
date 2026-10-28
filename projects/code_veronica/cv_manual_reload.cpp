// Independent PS3 implementation. Idea studied in aikatore/recvx-enhanced-port
// pc/glue/reload_glue.c (MIT): native ammo combination followed by mode 70.
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <algorithm>
#include "cv_manual_reload.h"
#include "cv_items.h"
#include "cv_version.h"
extern "C" {
extern uint8_t* vm_base;
uint32_t vm_read32(uint64_t);uint8_t vm_read8(uint64_t);void vm_write32(uint64_t,uint32_t);void vm_write8(uint64_t,uint8_t);void vm_write64(uint64_t,uint64_t);
uint32_t cv_combat_hud_pad();extern int g_pad_block;void* rsx_d3d12_window();
uint32_t cv_manual_reload_physical_buttons();
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
}
#if defined(CV_REGION_JP)
constexpr uint32_t state_ptr=0xA79C34,player_ptr=0xA79C3C,inventory_ptr=0xCA89A0,capacity_table=0x877860;
#define CHECK_AMMO func_001B2318
#define SEARCH_AMMO func_001AACF8
#elif defined(CV_REGION_US)
constexpr uint32_t state_ptr=0xA79BD0,player_ptr=0xA79BD8,inventory_ptr=0xCA8920,capacity_table=0x876F68;
#define CHECK_AMMO func_001B2270
#define SEARCH_AMMO func_001AAC50
#else
constexpr uint32_t state_ptr=0xA89BD0,player_ptr=0xA89BD8,inventory_ptr=0xCB8C20,capacity_table=0x879010;
#define CHECK_AMMO func_001B3CF0
#define SEARCH_AMMO func_001AC6A8
#endif
// Declarations use the selected region's lifted functions at link time.
void CHECK_AMMO(ppu_context*);void SEARCH_AMMO(ppu_context*);
namespace {
std::atomic<int> enabled{0},reload_key{'R'},reload_button{
#if defined(CV_REGION_JP)
8
#else
1
#endif
};
std::atomic<uint64_t> filter_until{0};std::atomic<unsigned> binding_generation{0};
bool was=false;uint64_t requested=0;
bool readable(uint32_t a,size_t n){
 if(!vm_base||a>0x0fffffff||n>0x10000000-a)return false;
 const auto* p=vm_base+a;const auto* end=p+n;
 while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(p,&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;const auto* next=(const uint8_t*)m.BaseAddress+m.RegionSize;if(next<=p)return false;p=next;}return true;
}
int invoke(ppu_context* caller,void(*fn)(ppu_context*)){
 ppu_context work=*caller;work.gpr[1]-=0x400;work.gpr[2]=CV_TOC;
 vm_write64(work.gpr[1],caller->gpr[1]);
 auto* saved=g_active_ctx;auto tail=g_trampoline_fn;g_active_ctx=&work;g_trampoline_fn=nullptr;
 fn(&work);while(g_trampoline_fn){auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&work);}
 g_active_ctx=saved;g_trampoline_fn=tail;return int32_t(work.gpr[3]);
}
bool supported(int id){switch(id){case 2:case 4:case 5:case 9:case 10:case 0x20:case 0x21:case 0x22:case 0x83:case 0x8e:return true;default:return false;}}
}
extern "C" int cv_manual_reload_get(){return enabled.load(std::memory_order_relaxed);}
extern "C" void cv_manual_reload_set(int value){if(enabled.exchange(value!=0)!=(value!=0)){filter_until=0;binding_generation++;std::fprintf(stderr,"[MANUAL-RELOAD] %s; configurable aim + button; native reload\n",value?"on":"off");}}
extern "C" void cv_manual_reload_bindings_set(int key,int button){key=std::clamp(key,0,255);button=std::clamp(button,-1,17);if(reload_key.exchange(key)!=key||reload_button.load()!=button){reload_button=button;binding_generation++;filter_until=0;}}
extern "C" int cv_manual_reload_key_get(){return reload_key.load();}
extern "C" int cv_manual_reload_button_get(){return reload_button.load();}
extern "C" int cv_manual_reload_key_consumed(int key){return enabled&&key>0&&key==reload_key&&GetTickCount64()<filter_until&&!g_pad_block;}
extern "C" unsigned cv_manual_reload_filter_buttons(unsigned buttons){const int b=reload_button;return enabled&&b>=0&&GetTickCount64()<filter_until&&!g_pad_block?buttons&~(1u<<b):buttons;}
void cv_manual_reload_frame(ppu_context* ctx){
 static unsigned seen=~0u;if(seen!=binding_generation){seen=binding_generation;was=false;requested=0;}
 if(!cv_manual_reload_get()){was=false;requested=0;filter_until=0;return;}
 const bool focused=GetForegroundWindow()==(HWND)rsx_d3d12_window();
 const auto inv=cv_items::read();
 bool aiming=false;
 if(focused&&!g_pad_block&&inv.valid&&inv.playing&&readable(player_ptr,4)){
  const auto player=vm_read32(player_ptr);
  if(readable(player,0x400))aiming=vm_read8(player+0xc)==1&&vm_read8(player+0xd)==1&&((vm_read8(player+0xe)>=64&&vm_read8(player+0xe)<=67)||vm_read8(player+0xe)==164);
 }
 filter_until=aiming?GetTickCount64()+100:0;
 const int key=reload_key,button=reload_button;const uint32_t raw=cv_manual_reload_physical_buttons();
 const bool shortcut_chord=(raw&0xf0u)==0xf0u;
 const bool down=!g_pad_block&&focused&&!shortcut_chord&&((key>0&&(GetAsyncKeyState(key)&0x8000))||(button>=0&&(raw&(1u<<button))));
 const bool pressed=down&&!was;was=down;
 const uint64_t now=GetTickCount64();if(pressed)requested=now;
 if(!requested||now-requested>500||g_pad_block||!focused){requested=0;return;}
 if(!aiming||!inv.valid||!inv.playing||inv.equipped<1||inv.equipped>cv_items::kEntries){requested=0;return;}
 const uint32_t word=inv.entry[inv.equipped-1];const int id=(word>>16)&255;
 if(!supported(id)||(word&0x08000000u)){requested=0;return;}
 if(!readable(state_ptr,4)||!readable(player_ptr,4)||!readable(inventory_ptr,4)){requested=0;return;}
 const uint32_t state=vm_read32(state_ptr),player=vm_read32(player_ptr),iv=vm_read32(inventory_ptr);
 const uint32_t expected_inventory=CV_SRT_INVENTORY+inv.character*0x40;
 if(state!=cv_items::state_base()||iv!=expected_inventory||!readable(player,0x400)||!readable(iv,48)){requested=0;return;}
 const int mode1=vm_read8(player+0xd),mode2=vm_read8(player+0xe),mode3=vm_read8(player+0xf);
 const uint32_t attack=vm_read32(player+0x3ec);
 // Wait briefly for an aim pose initiated simultaneously with the shortcut.
 if(mode1==1&&(mode2==64||mode2==164||((mode2>=65&&mode2<=67)&&!(attack&0x10))))return;
 requested=0;
 if(vm_read8(player+0xc)!=1||mode1!=1||mode2<65||mode2>67||mode3!=1||!(attack&0x10)||(attack&0x40000000u))return;
 const unsigned difficulty=vm_read32(state+0x9c);if(difficulty>3)return;
 const uint32_t capacity=capacity_table+id*16+difficulty*4;if(!readable(capacity,4))return;
 const int maximum=int32_t(vm_read32(capacity)),loaded=word&65535;
 if(maximum<=0||maximum>1000||loaded>=maximum)return;
 if(invoke(ctx,CHECK_AMMO)<0)return; // Native infinite-ammo and mode checks.
 if(!invoke(ctx,SEARCH_AMMO)){std::fprintf(stderr,"[MANUAL-RELOAD] no compatible reserve weapon=%02X loaded=%d/%d\n",id,loaded,maximum);return;}
 // SEARCH_AMMO performs native combine/removal/compaction. Never invent ammo.
 // The original empty-trigger paths set exactly these three fields.
 vm_write32(state+0x6c,vm_read32(state+0x6c)&~0x00040000u);
 vm_write8(player+0xe,70);vm_write8(player+0xf,0);
 std::fprintf(stderr,"[MANUAL-RELOAD] native animation weapon=%02X before=%d max=%d\n",id,loaded,maximum);
}
