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
uint64_t vm_read64(uint64_t a){return uint64_t(vm_read32(a))<<32|vm_read32(a+4);}
void* rsx_d3d12_window(){return nullptr;}
int pad_phys_poll(int,PadPhys*){return 0;}
}
void ORIGINAL_FRAME(ppu_context*){++frames;}
void ORIGINAL_SERIALIZE(ppu_context*){++serializations;}
static int rows=0;static std::vector<uint16_t> row_text;
void ORIGINAL_ROW(ppu_context* c){   // reads the location as func_001D8090 does
 ++rows;const uint32_t row=uint32_t(c->gpr[3]),table=vm_read32(state_base+0x1B3F0);
 const int id=int8_t(vm_read8(row+0xD));row_text.clear();
 if(c->gpr[1]>=0x9000)return;   // a slot without the label: nothing borrowed
 assert(id!=0&&c->gpr[1]==0x9000-0x200&&vm_read64(c->gpr[1])==0x9000);
 for(uint32_t t=table+vm_read32(table+4+id*4);;t+=2){uint16_t v=vm_read16(t);row_text.push_back(v);if(v==0xFFFF)break;}
}
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
 // A room with no typewriter: once the file is written, the slot list's display wait is ended.
 const uint32_t object=0x200000,manager=0x210000,ui=0x220000,slots=0x230000;
 vm_write32(SAVE_OBJECT,object);vm_write32(SAVE_MANAGER,manager);vm_write32(manager+0x4E8,1);
 vm_write32(object+0x30,2);vm_write32(object+0x58,4);vm_write8(object+0x2B,1);vm_write32(object+0xA0,ui);vm_write32(ui+0x34,slots);
 vm_write32(slots,1);vm_write16(slots+0x14,3);vm_write32(slots+0x20,8);
 vm_write32(state_base+0x68,0x10000);TEST_FRAME(&c);assert(vm_read32(slots+0x20)==8&&vm_read32(slots)==1); // still writing
 vm_write32(manager+0x4E8,0);TEST_FRAME(&c);assert(vm_read32(slots+0x20)==0&&vm_read32(slots)==0&&vm_read16(slots+0x14)==0xFFFA);
 // The chosen slot, once the state is copied into it (step 5), is remembered;
 // its row then shows "F7: rm_XXXX" where the location goes.
 const uint32_t data=0x240000,slot=data+0x30+2*0x838,table=0x260000,row=0x270000;
 f7_file=(fs::temp_directory_path()/L"cv_f7_saves_test.txt").wstring();_wremove(f7_file.c_str());
 vm_write32(SAVE_DATA,data);vm_write32(slot,0xA);vm_write8(slot+9,2);vm_write8(slot+0xA,14);vm_write8(slot+0xB,0);
 vm_write32(state_base+0x1B3F0,table);vm_write32(table+8,0x40);
 vm_write32(row,3);vm_write8(row+0xD,0);c.gpr[3]=row;c.gpr[1]=0x9000;
 TEST_ROW(&c);assert(rows==1&&row_text.empty()); // not a tool save yet
 vm_write32(ui+0x10,2);vm_write32(object+0x54,5);TEST_FRAME(&c);assert(f7_saves.size()==1);
 TEST_ROW(&c);assert(rows==2&&c.gpr[1]==0x9000&&vm_read32(table+8)==0x40&&vm_read8(row+0xD)==0);
 const std::vector<uint16_t> label=f7_label(2,14,0);
 assert(row_text==label&&label.back()==0xFFFF);
#if !defined(CV_REGION_JP)
 {std::string shown;for(uint16_t v:label)if(v!=0xFFFF)shown+=v==0xFF01?' ':char(v+0x20);assert(shown=="F7: rm_2140");}
#endif
 f7_saves.clear();f7_loaded=false;TEST_ROW(&c);assert(rows==3&&!row_text.empty()); // read back from the file
 vm_write8(slot+0x20,1);TEST_ROW(&c);assert(rows==4&&row_text.empty()); // overwritten by a normal save
 _wremove(f7_file.c_str());
 vm_write32(object+0x54,0);vm_write32(state_base+0x68,0x18000);
 cv_test_tools_enable(false);TEST_SERIALIZE(&c);assert(serializations==2);TEST_FRAME(&c);assert(warps==1&&frames==10);
 std::puts("[map-tools-test] PASS: catalog validation, separate chords, live-state gates, one-shot dispatch, guest stack/TLS preservation, save-here request and kept ribbon/count, save screen released after the write, F7 slot label (" CV_TITLE_ID "), original save path unchanged");
}
