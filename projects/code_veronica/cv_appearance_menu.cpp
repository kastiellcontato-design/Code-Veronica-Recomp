/* Code Veronica (EU/JP/US 1.00; addresses in cv_version.h): extend the guest's OPTION menu using its own 2D queue.
 * Only the build-local lift is wrapped. EBOOT and original generated files stay intact.
 * New labels use serif glyphs rasterized by Windows at the menu's own scale (one
 * texel per UI unit, like the game's lettering) and drawn magnified the way the
 * game's text is, bilinearly, submitted as one GPU batch in the game
 * framebuffer (no additional window).
 */
#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_appearance_menu_preview.h"
#include "cv_appearance_runtime.h"
#include "../../libs/video/rsx_ui_batch.h"
#include "../../libs/video/rsx_ui_cadence.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

void CV_OG_MENU_RESET(ppu_context*);
void CV_OG_MENU_INPUT(ppu_context*);
void CV_OG_MENU_COLOR(ppu_context*);
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" uint8_t* vm_base;
extern "C" int cv_pc_settings_read(void);
extern "C" int cv_pc_settings_saturation_read(void);
extern "C" int cv_pc_settings_effects_read(void);
extern "C" void cv_pc_settings_change(int which, int value);
extern "C" uint32_t cellGcm_flip_submit_count(void);

namespace {
constexpr uint32_t kCursor=CV_MENU_CURSOR, kInput=CV_MENU_INPUT, kRepeat=CV_MENU_REPEAT;
constexpr uint32_t kDialog=CV_MENU_DIALOG;
constexpr uint32_t kUp=0x10, kDown=0x40, kLeft=0x80, kRight=0x20;
constexpr uint32_t kConfirm=0xC000, kCancel=0x2001;
/* The screen the OPTION menu shows (0 its rows, 3 MONITOR TUNING, -1 closed;
 * it switches at the darkest frame) and the black layer the game draws over
 * everything while changing screens (0 clear, 1 black), at the same offsets
 * from the cursor in the three versions. The menu is still called through
 * its sub-screens and transitions. */
constexpr uint32_t kScreen=CV_MENU_CURSOR+4, kFade=CV_MENU_CURSOR+0x4C;
bool pc_selected=false, pc_open=false, drawing_main=false;
int pc_row=0;
cv_appearance::AppearancePreview appearance_preview;
bool appearance_assets_attempted=false;
unsigned appearance_choice=0;
constexpr int kSettingCount=8, kResetColorsRow=kSettingCount+1;
constexpr int kBackRow=kResetColorsRow+1, kPcRowCount=kBackRow+1;
std::array<int,kSettingCount> values={1,0,0,80,50,100,0,0}; // neutral brightness, contrast and saturation
int pc_adjust_direction=0;
ULONGLONG pc_adjust_started=0;

bool enabled() {
    static const bool on=[] { const char* value=std::getenv("CV_NATIVE_PC_MENU"); return !value || std::strcmp(value,"0")!=0; }();
    return on;
}
ppu_context call_context(const ppu_context* caller) {
    ppu_context work=*caller;
    work.gpr[1]-=0x400;
    vm_write64(work.gpr[1],caller->gpr[1]);
    work.gpr[2]=CV_TOC;
    return work;
}
void finish(ppu_context* work) {
    // The lifted code may schedule a tail call instead of making it immediately.
    while(g_trampoline_fn) { auto next=g_trampoline_fn; g_trampoline_fn=nullptr; next(work); }
}
// Atomics and HLE callbacks consult this TLS pointer, not just the function arg.
// A copied context must be active for the entire guest call and its tail calls.
struct GuestCallScope {
    ppu_context* previous=g_active_ctx;
    void (*previous_tail)(void*)=g_trampoline_fn;
    explicit GuestCallScope(ppu_context* work) { g_active_ctx=work;g_trampoline_fn=nullptr; }
    ~GuestCallScope() { g_active_ctx=previous;g_trampoline_fn=previous_tail; }
};
void invoke(ppu_context* work,void (*function)(ppu_context*)) {
    GuestCallScope scope(work);
    function(work);finish(work);
}
void context_test_tail(void* raw) {
    auto* work=static_cast<ppu_context*>(raw);
    work->gpr[3]=(g_active_ctx==work)?0xC0FFEE:0;
}
void context_test_call(ppu_context* work) {
    if(g_active_ctx==work)g_trampoline_fn=context_test_tail;
}
void sound(const ppu_context* caller,int id) {
    auto work=call_context(caller); work.gpr[3]=0; work.gpr[4]=id;
    invoke(&work,CV_FN_MENU_SOUND);
}
void read_settings() {
    const int packed=cv_pc_settings_read(),effects=cv_pc_settings_effects_read();
    values={ (packed>>4)&7, (packed>>8)&3, packed&3, (packed>>10)&255, (packed>>18)&255, cv_pc_settings_saturation_read(), effects&127, (effects>>8)&7 };
    values[0]=std::clamp(values[0],0,4); values[1]=std::clamp(values[1],0,2); values[2]=std::clamp(values[2],0,2);
    values[3]=std::clamp(values[3],0,160); values[4]=std::clamp(values[4],0,130); values[5]=std::clamp(values[5],0,200);
    values[6]=std::clamp(values[6],0,100)/10*10;values[7]=std::clamp(values[7],0,4);
}

// Values use the same offsets as read_settings and the existing settings bridge.
// Only color rows are written: display settings keep their current values.
void reset_colors() {
    values[3]=80; values[4]=50; values[5]=100;
    for(int row=3;row<=5;row++)cv_pc_settings_change(row,values[row]);
    pc_adjust_direction=0;pc_adjust_started=0;
}

/* One cell between four texel centres, with its corners' coverage (top-left,
 * bottom-left, top-right, bottom-right from the low byte); a run of identical
 * uniform cells is one wide cell. */
struct Cell { float x,y,w; uint32_t corners; };
struct Glyph { int advance=0; std::vector<Cell> cells; };
struct Font {
    HDC dc=CreateCompatibleDC(nullptr);
    HFONT font=CreateFontW(-27,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Times New Roman");
    HGDIOBJ old=nullptr;
    // 27 units to the em: capitals as tall as the menu's own (EXIT, DEFAULT),
    // measured on screen. The baseline sits 21 units below a row's y, which
    // centres RECOMP SETTINGS between DEFAULT and EXIT.
    const int ascent=21;
    std::map<unsigned,Glyph> glyphs;
    Font() { if(dc&&font) old=SelectObject(dc,font); }
    ~Font() { if(dc&&old)SelectObject(dc,old); if(font)DeleteObject(font); if(dc)DeleteDC(dc); }
    const Glyph& get(unsigned character) {
        auto found=glyphs.find(character); if(found!=glyphs.end())return found->second;
        Glyph glyph; glyph.advance=9;
        if(dc&&font) {
            MAT2 matrix{};
            matrix.eM11.value=matrix.eM22.value=1;
            GLYPHMETRICS metrics{};
            DWORD size=GetGlyphOutlineW(dc,character,GGO_GRAY8_BITMAP,&metrics,0,nullptr,&matrix);
            if(size!=GDI_ERROR) {
                glyph.advance=metrics.gmCellIncX;
                std::vector<unsigned char> bitmap(size);
                if(size && GetGlyphOutlineW(dc,character,GGO_GRAY8_BITMAP,&metrics,size,bitmap.data(),&matrix)!=GDI_ERROR) {
                    const int w=int(metrics.gmBlackBoxX),h=int(metrics.gmBlackBoxY),stride=(w+3)&~3;
                    auto cov=[&](int x,int y)->uint32_t {
                        if(x<0||y<0||x>=w||y>=h)return 0;
                        return (bitmap[size_t(y)*stride+x]*255u+32u)/64u;
                    };
                    auto corners=[&](int x,int y) { return cov(x,y)|cov(x,y+1)<<8|cov(x+1,y)<<16|cov(x+1,y+1)<<24; };
                    const float left=float(metrics.gmptGlyphOrigin.x),top=float(ascent-metrics.gmptGlyphOrigin.y);
                    for(int y=-1;y<h;y++)
                        for(int x=-1;x<w;) {
                            const uint32_t c=corners(x,y);
                            if(!c) { ++x; continue; }
                            int end=x+1;
                            if((c&255)*0x01010101u==c)
                                while(end<w && corners(end,y)==c)++end;
                            glyph.cells.push_back({left+x+.5f,top+y+.5f,float(end-x),c});
                            x=end;
                        }
                }
            }
        }
        return glyphs.emplace(character,std::move(glyph)).first->second;
    }
};
struct TextBatch {
    std::vector<rsx_ui_quad> quads;
    void text(const std::string& text,float x,float y,uint32_t color,float scale=1.f,bool centered=false) {
        static Font font;
        std::vector<unsigned> letters;
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
        std::wstring wide(size_t(length>0?length:0),0);
        if(length>0)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),wide.data(),length);
        for(wchar_t ch:wide)letters.push_back(unsigned(ch));
        if(centered) { int width=0; for(unsigned ch:letters)width+=font.get(ch).advance; x-=width*scale*.5f; }
        for(int pass=0;pass<2;pass++) {   // shadow, then face
            float pen=x;
            for(unsigned ch:letters) {
                const auto& glyph=font.get(ch);
                for(const auto& cell:glyph.cells)
                    quads.push_back({pen+(cell.x+(pass?0:2))*scale,y+(cell.y+(pass?0:2))*scale,cell.w*scale,scale,
                                     pass?color:(color&0xFF000000u),cell.corners});
                pen+=glyph.advance*scale;
            }
        }
    }
    void submit(const ppu_context*) {
        rsx_d3d12_set_ui_quads(quads.data(),uint32_t(quads.size()));
    }
};
uint32_t selected_color(bool selected) {
    if(!selected)return 0x7FAFAFAF;
    const uint32_t pulse=vm_read32(CV_MENU_PULSE)&255;
    return ((pulse?pulse:255)<<24)|0xFFFFFF;
}
/* Our label is drawn after the game's whole frame, so nothing the game draws
 * can cover it; it takes on itself what the game's layers do to its rows. */
/* screen and fade_bits: what this frame's rows are drawn with, which is what
 * the menu found when it ran the frame before (see CV_FN_MENU_INPUT). The
 * backend draws our batch over this same frame of the game's. */
void draw_entry(const ppu_context* caller,uint32_t screen,uint32_t fade_bits) {
    if(screen!=0) { rsx_d3d12_set_ui_quads(nullptr,0); return; }
    TextBatch batch; batch.text("RECOMP SETTINGS",32,310,selected_color(pc_selected));
    /* The black layer: the rows darken under it as it thickens. */
    float fade; std::memcpy(&fade,&fade_bits,4);
    const float shade=1.f-std::clamp(fade,0.f,1.f);
    /* A confirmation box (RESET GAME) is open: the game draws it over its rows,
     * which show through faintly at their first letters and fade out under it. */
    const bool boxed=vm_read32(kDialog)!=0;
    if(shade<1.f || boxed)
        for(auto& q:batch.quads) {
            const float under=boxed?std::clamp((95.f-q.x)/55.f,0.f,1.f)*.5f:1.f;
            auto channel=[&](int s) { return uint32_t(float((q.argb>>s)&255)*shade)<<s; };
            q.argb=(uint32_t(float(q.argb>>24)*under)<<24)|channel(16)|channel(8)|channel(0);
        }
    batch.submit(caller);
}
void draw_settings(const ppu_context* caller) {
    static const char* resolution[]={"480P","720P (NATIVE)","1080P","1440P","4K"};
    static const char* aa[]={"OFF","FXAA","SMAA"};
    static const char* mode[]={"WINDOWED","BORDERLESS","FULLSCREEN"};
    static const char* aniso[]={"OFF","2X","4X","8X","16X"};
    rsx_d3d12_set_ui_retained(1);
    TextBatch batch;
    batch.text("RECOMP SETTINGS",320,102,0xDFFFFFFF,1.35f,true);
    const char* labels[]={"RESOLUTION","ANTI-ALIASING","DISPLAY MODE","BRIGHTNESS","CONTRAST","SATURATION","SHARPNESS (FSR RCAS)","ANISOTROPIC FILTER","RESET COLORS","BACK"};
    char brightness[24],contrast[24],saturation[24],sharpness[24];
    std::snprintf(brightness,sizeof brightness,"%+d%%",-80+values[3]);
    std::snprintf(contrast,sizeof contrast,"%d%%",50+values[4]);
    std::snprintf(saturation,sizeof saturation,"%d%%",values[5]);
    if(values[6])std::snprintf(sharpness,sizeof sharpness,"%d%%",values[6]);
    else std::snprintf(sharpness,sizeof sharpness,"OFF");
    const char* current[]={resolution[values[0]],aa[values[1]],mode[values[2]],brightness,contrast,saturation,sharpness,aniso[values[7]]};
    if(!appearance_assets_attempted) {
        appearance_assets_attempted=true;
        const char* root=std::getenv("PS3_VFS_ROOT");
        if(root) {
            const auto disc=std::filesystem::path(root)/"USRDIR/BHCV/nativePS3/biocv_disc";
            auto system=disc/"jpn/system";
            if(!std::filesystem::exists(system/"pl00.arc"))system=disc/"eng/system";
            if(!appearance_preview.open(system))
                std::fprintf(stderr,"[appearance] Preview unavailable: %s\n",appearance_preview.error().c_str());
        }
    }
    cv_appearance::frame(const_cast<ppu_context*>(caller),true);
    batch.text("APPEARANCE",32,137,selected_color(pc_row==0),.78f);
    batch.text(std::string("< ")+cv_appearance::catalog[appearance_choice].name+" >",32,157,selected_color(pc_row==0),.46f);
    for(int row=1;row<kPcRowCount;row++) {
        const int setting=row-1;const uint32_t color=selected_color(pc_row==row);
        const float y=187.f+setting*20.f;
        batch.text(labels[setting],32,y,color,.57f);
        if(setting<kSettingCount)batch.text(std::string("< ")+current[setting]+" >",255,y,color,.55f);
    }
    appearance_preview.select(appearance_choice);
    appearance_preview.append(batch.quads,384,125,240,280);
    batch.text(appearance_preview.error().empty()?"360° PREVIEW":"PREVIEW UNAVAILABLE",504,408,0xCFDCCEB1,.47f,true);
    batch.text(cv_appearance::status(),320,425,0xCFDCCEB1,.40f,true);
    batch.text(pc_row==0?"LEFT / RIGHT: CHANGE APPEARANCE":"LEFT / RIGHT TO CHANGE",320,443,0xBFCFCFCF,.53f,true);
    batch.submit(caller);
    auto work=call_context(caller);invoke(&work,CV_FN_MENU_FRAME);
}
bool move_extra_row(uint32_t& cursor, bool& selected, uint32_t repeat) {
    if(selected && (repeat&kDown)) { selected=false; return true; }
    if(selected && (repeat&kUp)) { selected=false;cursor=4;return true; }
    if(!selected && ((cursor==4&&(repeat&kDown)) || (cursor==5&&(repeat&kUp)))) {
        selected=true;cursor=5;return true;
    }
    return false;
}
void consume(uint32_t input,uint32_t repeat) {vm_write32(input,0);vm_write32(repeat,0);}
}

void CV_FN_MENU_RESET(ppu_context* ctx) {
    rsx_d3d12_set_ui_retained(0);
    pc_selected=pc_open=drawing_main=false; pc_row=0;
    rsx_d3d12_set_ui_quads(nullptr,0);
    CV_OG_MENU_RESET(ctx);
}
void CV_FN_MENU_COLOR(ppu_context* ctx) {
    if(drawing_main && pc_selected && uint32_t(ctx->gpr[5])==CV_MENU_ROW_TEXT)ctx->gpr[4]=0x7FAFAFAF;
    CV_OG_MENU_COLOR(ctx);
}
void CV_FN_MENU_INPUT(ppu_context* ctx) {
    if(!enabled()) {rsx_d3d12_set_ui_retained(0);CV_OG_MENU_INPUT(ctx);return;}
    const uint32_t port=uint32_t(ctx->gpr[3]);
    if(port>6) {CV_OG_MENU_INPUT(ctx);return;}
    const bool interactive=ctx->gpr[4]!=0 && vm_read32(kDialog)==0;
    const uint32_t input_address=kInput+port*60,repeat_address=kRepeat+port*60;
    const uint32_t input=interactive?vm_read32(input_address):0;
    const uint32_t repeat=interactive?vm_read32(repeat_address):0;
    uint32_t cursor=vm_read32(kCursor);
    if(pc_selected && cursor!=5)pc_selected=false;
    if(pc_open) {
        if(!cv_appearance::busy() && (input&kCancel || ((input&kConfirm)&&pc_row==kBackRow))) {
            rsx_d3d12_set_ui_retained(0);
            rsx_d3d12_set_ui_quads(nullptr,0);
            pc_open=false;pc_selected=true;vm_write32(kCursor,5);sound(ctx,0);
            consume(input_address,repeat_address);ctx->gpr[4]=0;
        } else {
            if(repeat&kUp) {pc_row=(pc_row+kPcRowCount-1)%kPcRowCount;pc_adjust_direction=0;sound(ctx,2);}
            else if(repeat&kDown) {pc_row=(pc_row+1)%kPcRowCount;pc_adjust_direction=0;sound(ctx,2);}
            else if(pc_row==kResetColorsRow && (input&kConfirm)) { reset_colors();sound(ctx,3); }
            else if(pc_row==0 && !cv_appearance::busy() && (repeat&(kLeft|kRight))) {
                appearance_choice=(appearance_choice+((repeat&kRight)?1:cv_appearance::appearance_count-1))%cv_appearance::appearance_count;
                const bool ok=cv_appearance::request(ctx,appearance_choice,appearance_preview.asset(appearance_choice));
                sound(ctx,ok?3:0);
            } else if(pc_row==0 && !cv_appearance::busy() && (input&kConfirm)) {
                const bool ok=cv_appearance::request(ctx,appearance_choice,appearance_preview.asset(appearance_choice));
                sound(ctx,ok?3:0);
            }
            else if(pc_row>0 && pc_row<=kSettingCount && ((repeat&(kLeft|kRight)) || (input&kConfirm))) {
                const int setting=pc_row-1;
                if(setting<3||setting>=6) {
                    const int limit=setting==0||setting==7?5:setting==6?11:3;
                    const int step=(repeat&kLeft)?-1:1,unit=setting==6?10:1;
                    values[setting]=((values[setting]/unit+step+limit)%limit)*unit;
                    pc_adjust_direction=0;pc_adjust_started=0;
                } else {
                    const int direction=(repeat&kLeft)?-1:1;
                    const ULONGLONG now=GetTickCount64();
                    if(pc_adjust_direction!=direction) {pc_adjust_direction=direction;pc_adjust_started=now;}
                    const ULONGLONG held=now-pc_adjust_started;
                    const int step=held>=1200?8:held>=500?3:1;
                    const int maximum=setting==3?160:setting==4?130:200;
                    values[setting]=std::clamp(values[setting]+direction*step,0,maximum);
                }
                cv_pc_settings_change(setting,values[setting]);sound(ctx,2);
            } else pc_adjust_direction=0;
            consume(input_address,repeat_address);draw_settings(ctx);ctx->gpr[3]=0;return;
        }
    } else if(interactive && !(input&(kCancel|kConfirm))) {
        if(move_extra_row(cursor,pc_selected,repeat)) {
            vm_write32(kCursor,cursor);consume(input_address,repeat_address);sound(ctx,2);
        }
    } else if(interactive && pc_selected && (input&kConfirm) && !(input&kCancel)) {
        pc_open=true;pc_selected=false;pc_row=0;pc_adjust_direction=0;read_settings();sound(ctx,3);
        consume(input_address,repeat_address);draw_settings(ctx);ctx->gpr[3]=0;return;
    }
    /* The rows lag the menu's state by a frame: keep it a frame, and when the
     * menu did not run the frame before (it was on a sub-screen), draw nothing
     * yet. */
    static uint32_t last_screen=~0u,last_fade=0,last_frame=0;
    const uint32_t frame=cellGcm_flip_submit_count();
    static const bool interpolated=[] {
        const char* fps=std::getenv("CV_FPS_TEST_60"),*pose=std::getenv("CV_RENDER_INTERPOLATION");
        return fps&&std::strcmp(fps,"1")==0&&pose&&std::strcmp(pose,"1")==0;
    }();
    const uint32_t screen=rsx_ui_menu_contiguous(frame,last_frame,interpolated)?last_screen:~0u,fade=last_fade;
    last_screen=vm_read32(kScreen);last_fade=vm_read32(kFade);last_frame=frame;
    drawing_main=true;CV_OG_MENU_INPUT(ctx);finish(ctx);drawing_main=false;
    draw_entry(ctx,screen,fade);
}
extern "C" int cv_native_pc_menu_selftest(void) {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) {++failures;std::fprintf(stderr,"[pc-menu-test] FAIL: %s\n",message);} };
    ppu_context original{},scratch{};
    auto* previous=g_active_ctx;
    auto previous_tail=g_trampoline_fn;
    g_active_ctx=&original;g_trampoline_fn=context_test_tail;
    invoke(&scratch,context_test_call);
    check(scratch.gpr[3]==0xC0FFEE,"copied context active during guest and tail calls");
    check(g_active_ctx==&original && g_trampoline_fn==context_test_tail,"caller context and pending tail restored");
    g_active_ctx=previous;g_trampoline_fn=previous_tail;
    uint32_t cursor=4;bool selected=false;
    check(move_extra_row(cursor,selected,kDown)&&selected&&cursor==5,"DEFAULT -> RECOMP SETTINGS");
    check(move_extra_row(cursor,selected,kDown)&&!selected&&cursor==5,"RECOMP SETTINGS -> EXIT");
    check(move_extra_row(cursor,selected,kUp)&&selected&&cursor==5,"EXIT -> RECOMP SETTINGS");
    check(move_extra_row(cursor,selected,kUp)&&!selected&&cursor==4,"RECOMP SETTINGS -> DEFAULT");
    cursor=1;selected=false;
    check(!move_extra_row(cursor,selected,kDown)&&cursor==1&&!selected,"existing rows keep native navigation");
    cursor=5;
    check(!move_extra_row(cursor,selected,kDown)&&cursor==5&&!selected,"EXIT wrap remains native");
    TextBatch text;text.text("RECOMP SETTINGS",32,310,0xFFFFFFFF);
    check(!text.quads.empty(),"serif glyphs available");
    check(text.quads.size()<131072,"entry fits single GPU batch");
    for(const auto& q:text.quads) {
        check(q.x>=0&&q.x+q.w<640&&q.y>=0&&q.y+q.h<384,"entry fits original menu panel");
        check(q.w>0&&q.h>0,"glyph cells retain positive area");
    }
    std::printf("[pc-menu-test] %s: navigation and %zu text cells\n",failures?"FAILED":"PASS",text.quads.size());
    return failures?1:0;
}
#endif
