/* Drive the real native menu with guest input, without a game or GPU. */
#define NOMINMAX
#include <windows.h>
static bool keyboard_keys[256]={},keyboard_focused=true;
static SHORT menu_key_state(int vk){return vk>=0&&vk<256&&keyboard_keys[vk]?static_cast<SHORT>(0x8000):0;}
static HWND menu_foreground(){return keyboard_focused?reinterpret_cast<HWND>(1):nullptr;}
#define GetAsyncKeyState menu_key_state
#define GetForegroundWindow menu_foreground
#include "../native_pc_menu.cpp"
#undef GetForegroundWindow
#undef GetAsyncKeyState
#include <cassert>
#include <utility>

static std::array<unsigned char, 16 * 1024 * 1024> memory;
static std::array<int,8> settings={4,2,1,160,0,0,70,4};
static std::array<int,3> enhanced={100,100,0};
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
        assert(q[i].y>=0&&q[i].y+q[i].h<448);
    }
}
}
void CV_OG_MENU_RESET(ppu_context* ctx) {ctx->gpr[3]=0;}
void CV_OG_MENU_INPUT(ppu_context* ctx) {ctx->gpr[3]=0;}
void CV_OG_MENU_COLOR(ppu_context*) {}
void CV_FN_MENU_FRAME(ppu_context*) {}
void CV_FN_MENU_SOUND(ppu_context*) {}

int main(int argc,char**) {
    const bool interp=argc>1;
    _putenv_s("CV_FPS_TEST_60",interp?"1":"");
    _putenv_s("CV_RENDER_INTERPOLATION",interp?"1":"");
    flip_step=interp?4:1;
    uint32_t stamps[8]={};uint8_t valid[8]={};
    auto publish=[&](uint32_t f){stamps[f%8]=f;valid[f%8]=1;};
    publish(100);
    assert(rsx_ui_select_batch(100,stamps,valid,8,0)==4);
    assert(rsx_ui_select_batch(101,stamps,valid,8,0)==-1);
    for(uint32_t f=100;f<=103;f++)assert(rsx_ui_select_batch(f,stamps,valid,8,3)==4);
    assert(rsx_ui_select_batch(104,stamps,valid,8,3)==-1);
    publish(102); // A newer empty batch must win too; no fallback to old text.
    assert(rsx_ui_select_batch(102,stamps,valid,8,3)==6);
    assert(rsx_ui_select_batch(101,stamps,valid,8,3)==4); // no future batch
    memset(valid,0,sizeof valid);publish(UINT32_MAX);
    assert(rsx_ui_select_batch(0,stamps,valid,8,3)==7);
    assert(rsx_ui_menu_contiguous(0,UINT32_MAX,0));
    assert(!rsx_ui_menu_contiguous(14,10,0)&&rsx_ui_menu_contiguous(14,10,1));
    assert(!rsx_ui_menu_contiguous(15,10,1)&&!rsx_ui_menu_contiguous(10,10,1));
    assert(cv_native_pc_menu_selftest()==0);
    ppu_context ctx{};ctx.gpr[1]=0x8000;
    CV_FN_MENU_RESET(&ctx);
    vm_write32(kCursor,4);vm_write32(CV_MENU_PULSE,255);
    auto press=[&](uint32_t input,uint32_t repeat) {
        ctx.gpr[3]=0;ctx.gpr[4]=1;
        vm_write32(kInput,input);vm_write32(kRepeat,repeat);
        CV_FN_MENU_INPUT(&ctx);
    };
    auto shoulder=[&](int button,uint32_t guest=0){
        test_pad.connected=1;test_pad.kind=device;test_pad.buttons=1u<<button;
        press(guest,0);test_pad.buttons=0;press(0,0);
    };
    press(0,0);press(0,0);press(0,0);assert(last_batch>0);
    vm_write32(kScreen,3);press(0,0);press(0,0);assert(last_batch==0);
    vm_write32(kScreen,0);press(0,0);press(0,0);assert(last_batch>0);
    press(0,kDown);assert(pc_selected);
    press(0x4000,0);assert(pc_open&&pc_row==0);
    for(int i=0;i<kResetColorsRow;i++)press(0,kDown);
    assert(pc_row==kResetColorsRow&&changes.empty());
    press(0,kLeft);press(0,kRight);assert(changes.empty());
    press(0x4000,0);
    assert(pc_open&&pc_row==kResetColorsRow);
    assert((settings==std::array<int,8>{4,2,1,80,50,100,70,4}));
    assert((changes==std::vector<std::pair<int,int>>{{3,80},{4,50},{5,100}}));
    press(0,kDown);assert(pc_row==kBackRow);
    press(0x4000,0);assert(!pc_open&&pc_selected);
    press(0x4000,0);assert(pc_open&&pc_row==0);
    press(0,kUp);assert(pc_row==kBackRow);
    press(0x4000,0);assert(!pc_open);
    press(0x4000,0);
    for(int i=0;i<kResetColorsRow;i++)press(0,kDown);
    changes.clear();settings[3]=150;settings[4]=120;settings[5]=190;read_settings();
    press(kCancel|0x4000,0);
    assert(!pc_open&&changes.empty()&&settings[3]==150&&settings[4]==120&&settings[5]==190);
    press(0x4000,0);
    for(int i=0;i<3;i++)press(0,kDown);
    press(0,kLeft);
    assert(settings[3]==149&&settings[0]==4&&settings[1]==2&&settings[2]==1);
    assert(changes.size()==1&&changes[0].first==3);
    // RCAS changes use the same 10-percent steps as the launcher, with wrap.
    for(int i=0;i<3;i++)press(0,kDown);
    assert(pc_row==6&&values[6]==70);
    press(0,kRight);assert(settings[6]==80&&changes.back()==std::make_pair(6,80));
    values[6]=settings[6]=100;press(0,kRight);assert(settings[6]==0);
    press(0,kLeft);assert(settings[6]==100);
    // The native anisotropy row saves the same launcher indices (0/2/4/8/16x).
    press(0,kDown);assert(pc_row==7&&values[7]==4);
    press(0,kRight);assert(settings[7]==0&&changes.back()==std::make_pair(7,0));
    press(0,kLeft);assert(settings[7]==4);
    press(kCancel,0);assert(!pc_open);
    press(0x4000,0);assert(values[6]==100&&values[7]==4);
    press(0,kDown);assert(pc_row==1);
    press(0,kRight);assert(settings[1]==0);press(0,kLeft);assert(settings[1]==2);
    shoulder(PB_R1);assert(pc_tab==1&&pc_row==0);
    press(0,kLeft);assert(enhanced[0]==95&&enhanced[1]==100);
    for(int i=0;i<30;i++)press(0,kLeft);assert(enhanced[0]==0);
    press(0,kLeft);assert(enhanced[0]==0);
    press(0,kDown);press(0,kLeft);assert(enhanced[1]==95);
    press(0,kDown);press(0x4000,0);assert(enhanced[0]==100&&enhanced[1]==100);
    shoulder(PB_R1);assert(pc_tab==2&&pc_row==0&&enhanced[2]==0);
    press(0,kRight);assert(enhanced[2]==1);press(0,kLeft);assert(enhanced[2]==0);
    shoulder(PB_R1);assert(pc_tab==0&&pc_row==0);
    shoulder(PB_L1);assert(pc_tab==2);
    // Guest 0x400/0x800 caused the trigger/shoulder mismatch. They must
    // never select a tab without a physical shoulder-button edge.
    const int unchanged_tab=pc_tab;
    for(int kind:{PAD_KIND_XINPUT,PAD_KIND_DS4,PAD_KIND_DUALSENSE,PAD_KIND_SWITCH})
    for(int layout:{-1,0,1}){
      device=kind;test_360_layout=layout;
      shoulder(PB_L2,0x400);assert(pc_tab==unchanged_tab);
      shoulder(PB_R2,0x800);assert(pc_tab==unchanged_tab);
      press(0x400,0);press(0x800,0);assert(pc_tab==unchanged_tab);
      shoulder(PB_R1);assert(pc_tab==(unchanged_tab+1)%3);
      shoulder(PB_L1);assert(pc_tab==unchanged_tab);
    }
    pc_keyboard_tabs=false;device=PAD_KIND_DUALSENSE;
    _putenv_s("CV_BUTTON_ICONS","xbox");assert(tab_hint_kind()==PAD_KIND_XINPUT);
    _putenv_s("CV_BUTTON_ICONS","switch");assert(tab_hint_kind()==PAD_KIND_SWITCH);
    _putenv_s("CV_BUTTON_ICONS","ps");assert(tab_hint_kind()==PAD_KIND_DS4);
    _putenv_s("CV_BUTTON_ICONS","keyboard");assert(tab_hint_kind()==PAD_INPUT_KEYBOARD);
    _putenv_s("CV_BUTTON_ICONS","auto");assert(tab_hint_kind()==PAD_KIND_DUALSENSE);
    device=PAD_INPUT_KEYBOARD;assert(tab_hint_kind()==PAD_INPUT_KEYBOARD);
    _putenv_s("CV_BUTTON_ICONS","");device=PAD_KIND_SWITCH;
    test_360_layout=-1;
    test_pad.buttons=1u<<PB_R1;press(0,0);assert(pc_tab==(unchanged_tab+1)%3);
    for(int i=0;i<8;i++){press(0,0);assert(pc_tab==(unchanged_tab+1)%3);}
    test_pad.buttons=0;press(0,0);shoulder(PB_L1);assert(pc_tab==unchanged_tab);
    // A shoulder already held when opening the menu waits for release.
    press(kCancel,0);test_pad.buttons=1u<<PB_R1;press(0x4000,0);press(0,0);assert(pc_tab==0);
    test_pad.buttons=0;press(0,0);shoulder(PB_R1);assert(pc_tab==1);
    shoulder(PB_L1);assert(pc_tab==0);device=PAD_INPUT_KEYBOARD;
    std::puts("[pc-menu-test] PASS: physical L1/R1 across Xbox/DS4/DualSense/Switch with 360 layout auto/on/off; triggers ignored, hold/reopen guarded");
    assert(tab_direction(0,true,false)==-1);assert(tab_direction(0,true,false)==0);
    assert(tab_direction(0,false,false)==0);assert(tab_direction(0,false,true)==1);
    tab_direction(0,false,false);
    for(language=0;language<13;language++){
      for(int id=0;id<cv_enhanced_text::Count;id++){
        const auto key=cv_enhanced_text::Id(id);
        assert(*cv_enhanced_text::get(language,key));
        assert(!cv_menu_text::raster(cv_enhanced_text::utf8(language,key)).cells.empty());
      }
      for(pc_tab=0;pc_tab<3;pc_tab++){pc_row=0;draw_settings(&ctx);}
    }
    language=1;pc_tab=1;press(kCancel,0);assert(!pc_open);
    press(0x4000,0);assert(pc_tab==0&&pc_row==0);
    // E mapped to Cancel and Q mapped to Confirm reproduce the user's conflict.
    // The actual keyboard-to-pad filter must consume these keys before the
    // guest builds its menu input/repeat, while native tab polling still sees them.
    assert(pad_keyboard_reserved_keys()==('Q'|('E'<<16)));
    auto mapped_key=[&](unsigned key,uint32_t button){
        const bool down=pad_keyboard_key_down(pad_keyboard_reserved_keys(),key,keyboard_keys[key]);
        press(down?button:0,down?button:0);
    };
    auto original_settings=settings;
    keyboard_keys['E']=true;mapped_key('E',0x2000);assert(pc_open&&pc_tab==1&&pc_row==0);
    for(int i=0;i<8;i++){mapped_key('E',0x2000);assert(pc_open&&pc_tab==1);}
    keyboard_keys['E']=false;mapped_key('E',0x2000);
    keyboard_keys['Q']=true;mapped_key('Q',0x4000);assert(pc_open&&pc_tab==0&&settings==original_settings);
    keyboard_keys['Q']=false;mapped_key('Q',0x4000);
    keyboard_focused=false;keyboard_keys['E']=true;mapped_key('E',0x2000);assert(pc_tab==0);
    keyboard_keys['E']=false;keyboard_focused=true;press(0,0);
    // Physical shoulder/cancel buttons and all other keyboard mappings survive.
    shoulder(PB_R1);assert(pc_tab==1);shoulder(PB_L1);assert(pc_tab==0);
    assert(pad_keyboard_key_down(pad_keyboard_reserved_keys(),'R',1));
    assert(pad_keyboard_key_down(pad_keyboard_reserved_keys(),VK_BACK,1));
    keyboard_keys[VK_BACK]=true;mapped_key(VK_BACK,0x2000);
    assert(!pc_open&&pad_keyboard_reserved_keys()==0);
    assert(pad_keyboard_key_down(pad_keyboard_reserved_keys(),'Q',1));
    assert(pad_keyboard_key_down(pad_keyboard_reserved_keys(),'E',1));
    keyboard_keys[VK_BACK]=false;press(0x4000,0);assert(pc_open);
    keyboard_keys['E']=true;press(0x2000,0);assert(!pc_open&&pad_keyboard_reserved_keys()==0);
    keyboard_keys['E']=false;press(0x4000,0);assert(pc_open);
    CV_FN_MENU_RESET(&ctx);assert(pad_keyboard_reserved_keys()==0);
    pc_open=true;pc_tab=0;pc_row=0;
    std::puts("[pc-menu-test] PASS: Q/E exclusively switch tabs despite Cancel/Confirm mappings; hold/focus/other keys/physical pad/reset/close preserved");
    if(const char* folder=std::getenv("CV_MENU_TEST_PREVIEW")){
      CLSID encoder={0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
      for(int lang:{0,1,3,9})for(int tab=0;tab<3;tab++){
        language=lang;pc_tab=tab;pc_row=0;draw_settings(&ctx);
        Gdiplus::Bitmap image(1280,896,PixelFormat32bppARGB);
        std::vector<uint32_t> pixels(1280*896,0xff161413);
        for(const auto& q:preview_quads){
          int left=std::max(0,int(q.x*2)),right=std::min(1280,int(std::ceil((q.x+q.w)*2)));
          int top=std::max(0,int(q.y*2)),bottom=std::min(896,int(std::ceil((q.y+q.h)*2)));
          for(int y=top;y<bottom;y++)for(int x=left;x<right;x++){
            float u=std::clamp(((x+.5f)/2-q.x)/q.w,0.f,1.f),v=std::clamp(((y+.5f)/2-q.y)/q.h,0.f,1.f);
            float alpha=((q.corner_a&255)*(1-u)*(1-v)+((q.corner_a>>8)&255)*(1-u)*v+((q.corner_a>>16)&255)*u*(1-v)+(q.corner_a>>24)*u*v)/255.f*(q.argb>>24)/255.f;
            auto& dst=pixels[y*1280+x];uint32_t rgb=0xff000000;
            for(int sh:{0,8,16})rgb|=uint32_t(((q.argb>>sh)&255)*alpha+((dst>>sh)&255)*(1-alpha))<<sh;
            dst=rgb;
          }
        }
        Gdiplus::BitmapData data{};Gdiplus::Rect bounds(0,0,1280,896);image.LockBits(&bounds,Gdiplus::ImageLockModeWrite,PixelFormat32bppARGB,&data);
        for(int y=0;y<896;y++)std::memcpy(static_cast<uint8_t*>(data.Scan0)+y*data.Stride,pixels.data()+y*1280,1280*4);image.UnlockBits(&data);
        std::string file=std::string(folder)+"/menu-"+std::to_string(lang)+"-"+std::to_string(tab)+".bmp";std::wstring wide(file.begin(),file.end());assert(image.Save(wide.c_str(),&encoder,nullptr)==Gdiplus::Ok);
      }
    }
    std::printf("[pc-menu-test] PASS: color reset preserves display/effects, native AA/RCAS/anisotropy changes and reopen, cancel/back/wrap, one-point adjustment; max batch=%u\n",largest_batch);
}
