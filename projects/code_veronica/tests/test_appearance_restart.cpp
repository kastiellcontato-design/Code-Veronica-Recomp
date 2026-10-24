#include "../cv_appearance_runtime.cpp"
#include <cassert>
#include <set>
#include <map>
extern "C" {
uint8_t* vm_base=nullptr;
PPU_THREAD_LOCAL ppu_context* g_active_ctx=nullptr;
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
uint8_t vm_read8(uint64_t a){return vm_base[a];}
uint16_t vm_read16(uint64_t a){return uint16_t(vm_base[a])<<8|vm_base[a+1];}
uint32_t vm_read32(uint64_t a){return uint32_t(vm_read16(a))<<16|vm_read16(a+2);}
uint64_t vm_read64(uint64_t a){return uint64_t(vm_read32(a))<<32|vm_read32(a+4);}
void vm_write8(uint64_t a,uint8_t v){vm_base[a]=v;}
void vm_write16(uint64_t a,uint16_t v){vm_base[a]=uint8_t(v>>8);vm_base[a+1]=uint8_t(v);}
void vm_write32(uint64_t a,uint32_t v){vm_write16(a,uint16_t(v>>16));vm_write16(a+2,uint16_t(v));}
void vm_write64(uint64_t a,uint64_t v){vm_write32(a,uint32_t(v>>32));vm_write32(a+4,uint32_t(v));}
int64_t sys_memory_allocate(ppu_context*){assert(false);return -1;}
}
namespace cv_items {Inventory read(){Inventory i;i.playing=true;return i;}}
static std::set<uint32_t> registered;
static uint32_t expected_archive=0,resource=0x3200000;
static int reject_bank=-1,registrations=0;
void func_001E2324(ppu_context* c){assert(vm_read32(kPlayerArchive)==expected_archive);c->gpr[3]=resource;}
void func_000121AC(ppu_context* c){
 assert(c->gpr[4]==resource&&c->gpr[5]==0&&c->gpr[6]==1);
 unsigned bank=uint32_t(c->gpr[3]);
 c->gpr[3]=registrations++==reject_bank?0:1;
 if(c->gpr[3])registered.insert(bank);
}
void cv_original_001DBCA0(ppu_context*){}
void func_001DD208(ppu_context*){assert(false);}
void func_001DC3A4(ppu_context*){assert(false);}
void func_00197FC8(ppu_context* c){c->gpr[3]=0;}
void func_00031E14(ppu_context*){assert(false);}
void func_00167350(ppu_context*){assert(false);}
void func_000322AC(ppu_context*){assert(false);}
void func_0003F7A4(ppu_context* c){c->gpr[3]=uint32_t(-1);}
void cv_original_0003ECC0(ppu_context*){}
void cv_original_0004A3F8(ppu_context*){}
static constexpr uint32_t actor=0x3000000,sys=0x1000000,model=0x3100000;
static void native_player(unsigned who){
 using namespace cv_appearance;
 memset(vm_base+actor,0,0x600);vm_write32(kSys,sys);vm_write32(kPlayer,actor);
 vm_write32(sys+0x64,0x80);vm_write32(sys+0x28a84,0);
 vm_write8(CV_SRT_CHARACTER,uint8_t(who));vm_write32(CV_SRT_HEALTH,200);
 constexpr uint32_t markers[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};vm_write32(CV_SRT_RDX,markers[who]);
 vm_write32(actor+0x90,4);vm_write32(actor+0x15c,22);vm_write32(actor+0x16c,model);
 vm_write32(actor+0x164,0x3300000);vm_write32(actor+0xd8,0x3300000);vm_write32(actor+0x118,0x3300100);
 vm_write32(kPlayerArchive,0x3400000);
}
static void cached(unsigned choice){
 using namespace cv_appearance;
 desired=choice;chosen=true;failed=false;loading=false;active=false;original={};
 auto& v=visual();v.ready=true;v.textures_bound=false;v.storage=0x3500000;v.variant_archive=expected_archive=0x3600000;
 v.variant={0x200,22,0,0x3700000,0x3701000,0x3702000};
 v.variant_expressions={0x3703000,0x3704000};v.variant_skin=0x3705000;
 v.joint_offset=0x131000;
}
int main(){
 using namespace cv_appearance;
 vm_base=(uint8_t*)VirtualAlloc(nullptr,0x4000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(vm_base);
 ppu_context ctx{};ctx.gpr[1]=0x3f00000;
 // Real frame/install/restore code with simulated engine registry resets.
 for(unsigned choice:{1u,3u,4u,5u,32u}){
  native_player(1);cached(choice);registered.clear();registrations=0;
  frame(&ctx,true);assert(active&&current==choice);
  for(auto bank:{visual().variant[4],visual().variant_expressions[0],visual().variant_expressions[1]})assert(registered.count(bank));
  assert(vm_read32(kPlayerArchive)==0x3400000);
  const int bound_once=registrations;frame(&ctx,true);assert(registrations==bound_once);
  before_player_load();assert(!active);native_player(0);registered.clear();registrations=0;
  frame(&ctx,true);assert(active&&character==0&&current==choice);
  assert(registered.size()==3);assert(registrations==3);
  assert(vm_read32(actor+0x118)==visual().variant[4]);
  before_player_load();assert(vm_read32(actor+0x118)==0x3300100);
 }
 native_player(0);cached(4);registered.clear();registrations=0;reject_bank=1;
 frame(&ctx,true);assert(!active&&failed);assert(vm_read32(actor+0x164)==0x3300000);assert(vm_read32(actor+0x118)==0x3300100);assert(vm_read32(kPlayerArchive)==0x3400000);
 reject_bank=-1;cached(4);resource=0;frame(&ctx,true);assert(!active&&failed&&vm_read32(kPlayerArchive)==0x3400000);
 puts("PASS: Chris -> title -> new Claire cached skins; base and both expression banks rebound; native archive preserved; failure keeps original body");
 VirtualFree(vm_base,0,MEM_RELEASE);
}
