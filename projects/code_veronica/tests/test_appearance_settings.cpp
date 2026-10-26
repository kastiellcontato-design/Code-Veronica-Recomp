/* Drive the real native menu with guest input, without a game or GPU. */
#define NOMINMAX
#include <windows.h>
static bool keyboard_keys[256]={},keyboard_focused=true;
static SHORT menu_key_state(int vk){return vk>=0&&vk<256&&keyboard_keys[vk]?static_cast<SHORT>(0x8000):0;}
static HWND menu_foreground(){return keyboard_focused?reinterpret_cast<HWND>(1):nullptr;}
#define GetAsyncKeyState menu_key_state
#define GetForegroundWindow menu_foreground
#include "../cv_appearance_menu.cpp"
#undef GetForegroundWindow
#undef GetAsyncKeyState
#include <cassert>
#include <utility>

static std::array<unsigned char, 16 * 1024 * 1024> memory;
static std::array<int,8> settings={4,2,1,160,0,0,70,4};
static std::array<int,4> enhanced={100,100,0,1};
static int language=1,device=100;
static PadPhys test_pad{};
static int test_360_layout=-1;
static std::vector<std::pair<int,int>> changes;
static unsigned largest_batch,last_batch,flip_counter,flip_step=1;
static std::vector<rsx_ui_quad> preview_quads;
extern "C" {
int cv_enhanced_get(int which){return enhanced[which-8];}
int cv_enhanced_language(){return language;}
int pad_last_input(){return device;}
int pad_phys_360_layout(int kind){return test_360_layout==1||(test_360_layout==-1&&kind==PAD_KIND_XINPUT);}
int pad_phys_poll(int index,PadPhys* out){
 *out=index==0?test_pad:PadPhys{};
 if(pad_phys_360_layout(out->kind)){
  const unsigned b=out->buttons,mask=(1u<<PB_L1)|(1u<<PB_R1)|(1u<<PB_L2)|(1u<<PB_R2);
  out->buttons=(b&~mask)|(((b>>PB_L1)&1u)<<PB_L2)|(((b>>PB_R1)&1u)<<PB_R2)|(((b>>PB_L2)&1u)<<PB_L1)|(((b>>PB_R2)&1u)<<PB_R1);
 }
 return out->connected;
}
void* rsx_d3d12_window(){return reinterpret_cast<void*>(1);}
uint8_t* vm_base=memory.data();
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
PPU_THREAD_LOCAL ppu_context* g_active_ctx=nullptr;
uint32_t vm_read32(uint64_t a) {
    assert(a+4<=memory.size());
    return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3];
}
void vm_write32(uint64_t a,uint32_t v) {
    assert(a+4<=memory.size());
    for(int i=0;i<4;i++)memory[a+i]=uint8_t(v>>(24-8*i));
}
void vm_write64(uint64_t a,uint64_t v) { vm_write32(a,uint32_t(v>>32));vm_write32(a+4,uint32_t(v)); }
int cv_pc_settings_read() {
    return settings[2]|settings[0]<<4|settings[1]<<8|settings[3]<<10|settings[4]<<18;
}
int cv_pc_settings_saturation_read() { return settings[5]; }
int cv_pc_settings_effects_read() { return settings[6]|settings[7]<<8; }
void cv_pc_settings_change(int which,int value) { if(which<8)settings[which]=value;else enhanced[which-8]=value;changes.emplace_back(which,value); }
uint32_t cellGcm_flip_submit_count() { flip_counter+=flip_step;return flip_counter; }   // one frame per menu call
void rsx_d3d12_set_ui_quads(const rsx_ui_quad* q,uint32_t count) {
    largest_batch=std::max(largest_batch,count);last_batch=count;
    if(count)preview_quads.assign(q,q+count);else preview_quads.clear();
    assert(count<131072);
    for(uint32_t i=0;i<count;i++) {
        assert(q[i].w>0&&q[i].h>0);
        assert(q[i].x>=0&&q[i].x+q[i].w<640);
        assert(q[i].y>=0&&q[i].y+q[i].h<480);
    }
}
}
void CV_OG_MENU_RESET(ppu_context* ctx) {ctx->gpr[3]=0;}
void CV_OG_MENU_INPUT(ppu_context* ctx) {ctx->gpr[3]=0;}
void CV_OG_MENU_COLOR(ppu_context*) {}
void CV_FN_MENU_FRAME(ppu_context*) {}
void CV_FN_MENU_SOUND(ppu_context*) {}


extern "C" void rsx_d3d12_set_ui_retained(int){}
namespace cv_appearance {
bool AppearancePreview::open(const std::filesystem::path&){return false;}
void AppearancePreview::select(unsigned index){selected_=index;}
void AppearancePreview::append(std::vector<rsx_ui_quad>&,float,float,float,float){}
bool AppearancePreview::load(unsigned){return false;}
bool request(ppu_context*,unsigned,const PlayerAsset*){return false;}
void frame(ppu_context*,bool){}
bool busy(){return false;}
const char* status(){return "";}
std::filesystem::path appearance_system_directory(const std::filesystem::path& p){return p;}
}
int main(){
 ppu_context ctx{};ctx.gpr[1]=0x8000;CV_FN_MENU_RESET(&ctx);pc_open=true;pc_tab=0;pc_row=9;read_settings();
 assert(kSettingCount==9&&values[8]==1);
 auto press=[&](unsigned input,unsigned repeat){ctx.gpr[3]=0;ctx.gpr[4]=1;vm_write32(kInput,input);vm_write32(kRepeat,repeat);CV_FN_MENU_INPUT(&ctx);};
 press(0,kRight);assert(enhanced[3]==0&&changes.back()==std::make_pair(11,0));
 press(0,kLeft);assert(enhanced[3]==1&&changes.back()==std::make_pair(11,1));
 auto before=settings;reset_colors();assert(settings[6]==before[6]&&settings[7]==before[7]&&enhanced[3]==1);
 for(language=0;language<13;language++){assert(tr(cv_enhanced_text::FilmFilter)=="Film grain");assert(tr(cv_enhanced_text::RestartApply)=="Restart the game to apply.");pc_row=9;draw_settings(&ctx);assert(last_batch>0);}
 puts("PASS appearance menu: film row on/off, bridge ID 11, localized restart notice, reset isolation, 13 languages and 640x480 bounds");
}
