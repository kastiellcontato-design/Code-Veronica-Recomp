/* Drive the real native menu with guest input, without a game or GPU. */
#include "../native_pc_menu.cpp"
#include <cassert>
#include <utility>

static std::array<unsigned char, 16 * 1024 * 1024> memory;
static std::array<int,6> settings={4,2,1,160,0,0};
static std::vector<std::pair<int,int>> changes;
static unsigned largest_batch;
extern "C" {
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
void cv_pc_settings_change(int which,int value) { settings[which]=value;changes.emplace_back(which,value); }
uint32_t cellGcm_flip_submit_count() { static uint32_t frame;return ++frame; }   // one frame per menu call
void rsx_d3d12_set_ui_quads(const rsx_ui_quad* q,uint32_t count) {
    largest_batch=std::max(largest_batch,count);
    assert(count<131072);
    for(uint32_t i=0;i<count;i++) {
        assert(q[i].w>0&&q[i].h>0);
        assert(q[i].x>=0&&q[i].x+q[i].w<640);
        assert(q[i].y>=0&&q[i].y+q[i].h<448);
    }
}
}
void CV_OG_MENU_RESET(ppu_context* ctx) {ctx->gpr[3]=0;}
void CV_OG_MENU_INPUT(ppu_context* ctx) {ctx->gpr[3]=0;}
void CV_OG_MENU_COLOR(ppu_context*) {}
void CV_FN_MENU_FRAME(ppu_context*) {}
void CV_FN_MENU_SOUND(ppu_context*) {}

int main() {
    assert(cv_native_pc_menu_selftest()==0);
    ppu_context ctx{};ctx.gpr[1]=0x8000;
    CV_FN_MENU_RESET(&ctx);
    vm_write32(kCursor,4);vm_write32(CV_MENU_PULSE,255);
    auto press=[&](uint32_t input,uint32_t repeat) {
        ctx.gpr[3]=0;ctx.gpr[4]=1;
        vm_write32(kInput,input);vm_write32(kRepeat,repeat);
        CV_FN_MENU_INPUT(&ctx);
    };
    press(0,kDown);assert(pc_selected);
    press(0x4000,0);assert(pc_open&&pc_row==0);
    for(int i=0;i<6;i++)press(0,kDown);
    assert(pc_row==kResetColorsRow&&changes.empty());
    press(0,kLeft);press(0,kRight);assert(changes.empty());
    press(0x4000,0);
    assert(pc_open&&pc_row==kResetColorsRow);
    assert((settings==std::array<int,6>{4,2,1,80,50,100}));
    assert((changes==std::vector<std::pair<int,int>>{{3,80},{4,50},{5,100}}));
    press(0,kDown);assert(pc_row==kBackRow);
    press(0x4000,0);assert(!pc_open&&pc_selected);
    press(0x4000,0);assert(pc_open&&pc_row==0);
    press(0,kUp);assert(pc_row==kBackRow);
    press(0x4000,0);assert(!pc_open);
    press(0x4000,0);
    for(int i=0;i<6;i++)press(0,kDown);
    changes.clear();settings[3]=150;settings[4]=120;settings[5]=190;read_settings();
    press(kCancel|0x4000,0);
    assert(!pc_open&&changes.empty()&&settings[3]==150&&settings[4]==120&&settings[5]==190);
    press(0x4000,0);
    for(int i=0;i<3;i++)press(0,kDown);
    press(0,kLeft);
    assert(settings[3]==149&&settings[0]==4&&settings[1]==2&&settings[2]==1);
    assert(changes.size()==1&&changes[0].first==3);
    std::printf("[pc-menu-test] PASS: confirm-only color reset, display unchanged, cancel/back/wrap, one-point adjustment; max batch=%u\n",largest_batch);
}
