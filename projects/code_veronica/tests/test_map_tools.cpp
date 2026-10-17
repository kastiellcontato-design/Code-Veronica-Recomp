#include "../cv_test_tools.cpp"
#include <cassert>
static std::array<unsigned char,16*1024*1024> memory{};
static int frames=0,warps=0,serializations=0;
static ppu_context* caller_context;
extern "C" {
uint8_t* vm_base=memory.data();
PPU_THREAD_LOCAL ppu_context* g_active_ctx=nullptr;
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
int g_pad_block=0;
uint8_t vm_read8(uint64_t a){assert(a<memory.size());return memory[a];}
uint16_t vm_read16(uint64_t a){return uint16_t(vm_read8(a))<<8|vm_read8(a+1);}
uint32_t vm_read32(uint64_t a){return uint32_t(vm_read16(a))<<16|vm_read16(a+2);}
void vm_write8(uint64_t a,uint8_t v){assert(a<memory.size());memory[a]=v;}
void vm_write16(uint64_t a,uint16_t v){vm_write8(a,uint8_t(v>>8));vm_write8(a+1,uint8_t(v));}
void vm_write32(uint64_t a,uint32_t v){for(int i=0;i<4;i++)vm_write8(a+i,uint8_t(v>>(24-i*8)));}
void vm_write64(uint64_t a,uint64_t v){vm_write32(a,uint32_t(v>>32));vm_write32(a+4,uint32_t(v));}
void* rsx_d3d12_window(){return nullptr;}
int pad_phys_poll(int,PadPhys*){return 0;}
}
void ORIGINAL_FRAME(ppu_context*){++frames;}
void ORIGINAL_SERIALIZE(ppu_context*){++serializations;}
static void tail(void* raw){assert(raw==g_active_ctx);static_cast<ppu_context*>(raw)->gpr[3]=999;}
void TEST_WARP(ppu_context* c){
 ++warps;assert(c==g_active_ctx&&c!=caller_context);assert(c->gpr[1]==caller_context->gpr[1]-0x400&&c->gpr[2]==CV_TOC);
 assert(c->gpr[4]==2&&c->gpr[5]==14&&c->gpr[6]==3&&c->gpr[7]==255);
 assert(vm_read8(state_base+0x97)==1);g_trampoline_fn=tail;
}
int main(){
 cv_test::Room parsed;
 for(auto name:{L"rm_0000.arc",L"rm_0021.arc",L"rm_2141.arc",L"rm_9999.arc"})assert(cv_test::parse_room(name,parsed));
 assert(parsed.stage==9&&parsed.number==99&&parsed.variant==9);
 for(auto name:{L"rm_00.arc",L"rm_0020.arc.bak",L"../rm_0020.arc",L"rm_0A20.arc",L"rm_0020.png"})assert(!cv_test::parse_room(name,parsed));
 assert(!cv_launcher_controls::chord_active(cv_launcher_controls::menu_chord,cv_launcher_controls::test_chord));
 assert(!cv_launcher_controls::chord_active(cv_launcher_controls::srt_chord,cv_launcher_controls::test_chord));
 ppu_context c{};caller_context=&c;c.gpr[1]=0x9000;c.gpr[2]=111;c.gpr[3]=222;g_active_ctx=&c;
 constexpr uint32_t packet=0x80000000u|(2<<24)|(14<<16)|(1<<8)|3;
 cv_test_tools_enable(true);
 request=packet;TEST_FRAME(&c);assert(warps==0&&result==4); // title/uninitialized state
 vm_write32(CV_SRT_RDX,0x3D650);vm_write8(CV_SRT_CHARACTER,0);vm_write32(CV_SRT_HEALTH,160);   // health is 32-bit
 vm_write32(state_base+0x64,0x18080);vm_write32(state_base+0x68,0x18000); // menus enabled but blocked
 assert(active_game());
 request=packet;TEST_FRAME(&c);assert(warps==1&&request==0&&result==2);
 assert(c.gpr[1]==0x9000&&c.gpr[2]==111&&c.gpr[3]==222&&g_active_ctx==&c&&g_trampoline_fn==nullptr);
 TEST_FRAME(&c);assert(warps==1); // consumed once
 vm_write32(state_base+0x68,0x10000);request=packet;TEST_FRAME(&c);assert(warps==1&&result==4); // save dialog active
 vm_write32(state_base+0x68,0x18000);vm_write32(CV_SRT_HEALTH,0);request=packet;TEST_FRAME(&c);assert(warps==1); // dead
 // "Save here": the typewriter's request bit, only on a live game; the serializer
 // then keeps the inventory (ribbon) and the save count.
 vm_write32(CV_SRT_HEALTH,160);save_request=true;TEST_FRAME(&c);
 assert((vm_read32(state_base+0x74)&0x00200000u)!=0&&tool_save&&result==6);
 const uint32_t inventory=CV_SRT_INVENTORY;vm_write32(inventory+4,0x001F0002u);vm_write32(state_base+0x90,7);
 TEST_SERIALIZE(&c);assert(serializations==1&&vm_read32(inventory+4)==0x001F0002u&&vm_read32(state_base+0x90)==7);
 cv_test_tools_enable(false);TEST_SERIALIZE(&c);assert(serializations==2);TEST_FRAME(&c);assert(warps==1&&frames==7);
 std::puts("[map-tools-test] PASS: catalog validation, separate chords, live-state gates, one-shot dispatch, guest stack/TLS preservation, save-here request and kept ribbon/count, original save path unchanged");
}
