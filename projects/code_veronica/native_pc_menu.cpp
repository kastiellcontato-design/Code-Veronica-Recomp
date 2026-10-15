/* Code Veronica EU 1.00: extend the guest's OPTION menu using its own 2D queue.
 * Only the build-local lift is wrapped. EBOOT and original generated files stay intact.
 * New labels use cached serif glyph outlines rasterized by Windows, submitted as
 * one GPU batch in the game framebuffer (no additional window).
 */
#include "ppu_recomp.h"
#include "../../libs/video/rsx_ui_batch.h"
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

void cv_original_0002A468(ppu_context*);
void cv_original_0002F3D0(ppu_context*);
void cv_original_001BD060(ppu_context*);
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" uint8_t* vm_base;
extern "C" int cv_pc_settings_read(void);
extern "C" int cv_pc_settings_saturation_read(void);
extern "C" void cv_pc_settings_change(int which, int value);

namespace {
constexpr uint32_t kCursor=0x00BBE910, kInput=0x00BC3370, kRepeat=0x00BC3374;
constexpr uint32_t kDialog=0x00AD33DC;
constexpr uint32_t kUp=0x10, kDown=0x40, kLeft=0x80, kRight=0x20;
constexpr uint32_t kConfirm=0xC000, kCancel=0x2001;
bool pc_selected=false, pc_open=false, drawing_main=false;
int pc_row=0;
std::array<int,6> values={1,0,0,80,50,100}; // neutral brightness, contrast and saturation
int pc_adjust_direction=0;
ULONGLONG pc_adjust_started=0;
constexpr float kGlyphRasterScale=1.5f;

bool enabled() {
    static const bool on=[] { const char* value=std::getenv("CV_NATIVE_PC_MENU"); return !value || std::strcmp(value,"0")!=0; }();
    return on;
}
ppu_context call_context(const ppu_context* caller) {
    ppu_context work=*caller;
    work.gpr[1]-=0x400;
    vm_write64(work.gpr[1],caller->gpr[1]);
    work.gpr[2]=0x00A8AE70;
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
    invoke(&work,func_00197308);
}
void read_settings() {
    const int packed=cv_pc_settings_read();
    values={ (packed>>4)&7, (packed>>8)&3, packed&3, (packed>>10)&255, (packed>>18)&255, cv_pc_settings_saturation_read() };
    values[0]=std::clamp(values[0],0,4); values[1]=std::clamp(values[1],0,2); values[2]=std::clamp(values[2],0,2);
    values[3]=std::clamp(values[3],0,160); values[4]=std::clamp(values[4],0,130); values[5]=std::clamp(values[5],0,200);
}

struct Span { float x,y,w; unsigned char coverage; float h=1.0f/kGlyphRasterScale; };
struct Glyph { int advance=0; std::vector<Span> spans; };
struct Font {
    HDC dc=CreateCompatibleDC(nullptr);
    HFONT font=CreateFontW(-28,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Times New Roman");
    HGDIOBJ old=nullptr;
    int ascent=24;
    std::map<unsigned,Glyph> glyphs;
    Font() { if(dc&&font) { old=SelectObject(dc,font); TEXTMETRICW metrics{}; if(GetTextMetricsW(dc,&metrics))ascent=metrics.tmAscent; } }
    ~Font() { if(dc&&old)SelectObject(dc,old); if(font)DeleteObject(font); if(dc)DeleteDC(dc); }
    const Glyph& get(unsigned character) {
        auto found=glyphs.find(character); if(found!=glyphs.end())return found->second;
        Glyph glyph; glyph.advance=14;
        if(dc&&font) {
            MAT2 matrix{};
            matrix.eM11.value=matrix.eM22.value=1;
            matrix.eM11.fract=matrix.eM22.fract=0x8000;
            GLYPHMETRICS metrics{};
            DWORD size=GetGlyphOutlineW(dc,character,GGO_GRAY8_BITMAP,&metrics,0,nullptr,&matrix);
            if(size!=GDI_ERROR) {
                glyph.advance=static_cast<int>(std::lround(metrics.gmCellIncX/kGlyphRasterScale));
                std::vector<unsigned char> bitmap(size);
                if(size && GetGlyphOutlineW(dc,character,GGO_GRAY8_BITMAP,&metrics,size,bitmap.data(),&matrix)!=GDI_ERROR) {
                    const unsigned stride=(metrics.gmBlackBoxX+3)&~3u;
                    for(unsigned y=0;y<metrics.gmBlackBoxY;y++) {
                        for(unsigned x=0;x<metrics.gmBlackBoxX;) {
                            const unsigned alpha=(bitmap[y*stride+x]*255u+32u)/64u;
                            if(!alpha) { ++x; continue; }
                            unsigned end=x+1;
                            while(end<metrics.gmBlackBoxX && (bitmap[y*stride+end]*255u+32u)/64u==alpha)++end;
                            const float px=(float(x)+metrics.gmptGlyphOrigin.x)/kGlyphRasterScale;
                            const float py=float(y)/kGlyphRasterScale+ascent-float(metrics.gmptGlyphOrigin.y)/kGlyphRasterScale;
                            glyph.spans.push_back({px,py,float(end-x)/kGlyphRasterScale,static_cast<unsigned char>(alpha)});
                            x=end;
                        }
                    }
                }
            }
        }
        // Merge vertically adjacent identical runs into solid rectangles.
        // This preserves every covered pixel while reducing native quad draws.
        std::vector<Span> merged;
        for(const auto& span:glyph.spans) {
            bool joined=false;
            for(auto& rect:merged) {
                if(rect.x==span.x && rect.w==span.w && rect.coverage==span.coverage && std::fabs(rect.y+rect.h-span.y)<0.001f) {
                    rect.h+=span.h;joined=true;break;
                }
            }
            if(!joined)merged.push_back(span);
        }
        glyph.spans=std::move(merged);
        return glyphs.emplace(character,std::move(glyph)).first->second;
    }
};
struct Vertex { float x,y,z; uint32_t color; };
struct TextBatch {
    std::vector<Vertex> vertices;
    void rect(float x,float y,float w,float h,uint32_t color) {
        const Vertex first{x,y,0.05f,color};
        if(!vertices.empty()) { vertices.push_back(vertices.back()); vertices.push_back(first); }
        vertices.push_back(first); vertices.push_back({x,y+h,0.05f,color});
        vertices.push_back({x+w,y,0.05f,color}); vertices.push_back({x+w,y+h,0.05f,color});
    }
    void text(const std::string& text,float x,float y,uint32_t color,float scale=1.f,bool centered=false) {
        static Font font;
        if(centered) { int width=0; for(unsigned char ch:text)width+=font.get(ch).advance; x-=width*scale*.5f; }
        const unsigned alpha=color>>24;
        for(unsigned char ch:text) {
            const auto& glyph=font.get(ch);
            // Shadow, then face. Coverage lives in the vertex color's alpha.
            for(int pass=0;pass<2;pass++)for(const auto& span:glyph.spans) {
                const unsigned a=alpha*span.coverage/255;
                rect(x+(span.x+(pass?0:2))*scale,y+(span.y+(pass?0:2))*scale,span.w*scale,span.h*scale,
                     (a<<24)|(pass?(color&0xffffff):0));
            }
            x+=glyph.advance*scale;
        }
    }
    void submit(const ppu_context*) {
        // Reuse exactly the approved glyph rectangles, now submitted in one
        // render batch instead of thousands of guest heap/queue/draw calls.
        std::vector<rsx_ui_quad> quads;
        quads.reserve((vertices.size()+2)/6);
        for(size_t first=0;first+3<vertices.size();first+=6) {
            const auto& a=vertices[first]; const auto& b=vertices[first+3];
            quads.push_back({a.x,a.y,b.x-a.x,b.y-a.y,a.color});
        }
        rsx_d3d12_set_ui_quads(quads.data(),uint32_t(quads.size()));
    }
};
uint32_t selected_color(bool selected) {
    if(!selected)return 0x7FAFAFAF;
    const uint32_t pulse=vm_read32(0x00BBE92C)&255;
    return ((pulse?pulse:255)<<24)|0xFFFFFF;
}
void draw_entry(const ppu_context* caller) {
    TextBatch batch; batch.text("PC SETTINGS",32,310,selected_color(pc_selected)); batch.submit(caller);
}
void draw_settings(const ppu_context* caller) {
    static const char* resolution[]={"480P","720P (NATIVE)","1080P","1440P","4K"};
    static const char* aa[]={"OFF","FXAA","SMAA"};
    static const char* mode[]={"WINDOWED","BORDERLESS","FULLSCREEN"};
    TextBatch batch;
    batch.text("PC SETTINGS",320,102,0xDFFFFFFF,1.35f,true);
    const char* labels[]={"RESOLUTION","ANTI-ALIASING","DISPLAY MODE","BRIGHTNESS","CONTRAST","SATURATION","BACK"};
    char brightness[24],contrast[24],saturation[24];
    std::snprintf(brightness,sizeof brightness,"%+d%%",-80+values[3]);
    std::snprintf(contrast,sizeof contrast,"%d%%",50+values[4]);
    std::snprintf(saturation,sizeof saturation,"%d%%",values[5]);
    const char* current[]={resolution[values[0]],aa[values[1]],mode[values[2]],brightness,contrast,saturation};
    const float rows[]={146,182,218,254,290,326,362};
    for(int row=0;row<7;row++) {
        const uint32_t color=selected_color(pc_row==row);
        batch.text(labels[row],32,rows[row],color,.9f);
        if(row<6)batch.text(std::string("< ")+current[row]+" >",330,rows[row],color,.8f);
    }
    batch.text("LEFT / RIGHT TO CHANGE",320,410,0xBFCFCFCF,.6f,true);
    batch.submit(caller);
    auto work=call_context(caller);invoke(&work,func_0002B548);
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

void func_0002A468(ppu_context* ctx) {
    pc_selected=pc_open=drawing_main=false; pc_row=0;
    rsx_d3d12_set_ui_quads(nullptr,0);
    cv_original_0002A468(ctx);
}
void func_001BD060(ppu_context* ctx) {
    if(drawing_main && pc_selected && uint32_t(ctx->gpr[5])==0x00BBEE18)ctx->gpr[4]=0x7FAFAFAF;
    cv_original_001BD060(ctx);
}
void func_0002F3D0(ppu_context* ctx) {
    if(!enabled()) {cv_original_0002F3D0(ctx);return;}
    const uint32_t port=uint32_t(ctx->gpr[3]);
    if(port>6) {cv_original_0002F3D0(ctx);return;}
    const bool interactive=ctx->gpr[4]!=0 && vm_read32(kDialog)==0;
    const uint32_t input_address=kInput+port*60,repeat_address=kRepeat+port*60;
    const uint32_t input=interactive?vm_read32(input_address):0;
    const uint32_t repeat=interactive?vm_read32(repeat_address):0;
    uint32_t cursor=vm_read32(kCursor);
    if(pc_selected && cursor!=5)pc_selected=false;
    if(pc_open) {
        if(input&kCancel || ((input&kConfirm)&&pc_row==6)) {
            pc_open=false;pc_selected=true;vm_write32(kCursor,5);sound(ctx,0);
            consume(input_address,repeat_address);ctx->gpr[4]=0;
        } else {
            if(repeat&kUp) {pc_row=(pc_row+6)%7;pc_adjust_direction=0;sound(ctx,2);}
            else if(repeat&kDown) {pc_row=(pc_row+1)%7;pc_adjust_direction=0;sound(ctx,2);}
            else if(pc_row<6 && ((repeat&(kLeft|kRight)) || (input&kConfirm))) {
                if(pc_row<3) {
                    const int limit=pc_row==0?5:3;
                    const int step=(repeat&kLeft)?-1:1;
                    values[pc_row]=(values[pc_row]+step+limit)%limit;
                } else {
                    const int direction=(repeat&kLeft)?-1:1;
                    const ULONGLONG now=GetTickCount64();
                    if(pc_adjust_direction!=direction) {pc_adjust_direction=direction;pc_adjust_started=now;}
                    const ULONGLONG held=now-pc_adjust_started;
                    const int step=held>=1200?8:held>=500?3:1;
                    const int maximum=pc_row==3?160:pc_row==4?130:200;
                    values[pc_row]=std::clamp(values[pc_row]+direction*step,0,maximum);
                }
                cv_pc_settings_change(pc_row,values[pc_row]);sound(ctx,2);
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
    drawing_main=true;cv_original_0002F3D0(ctx);finish(ctx);drawing_main=false;
    if((ctx->gpr[3]&255)==255)rsx_d3d12_set_ui_quads(nullptr,0);
    else draw_entry(ctx);
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
    check(move_extra_row(cursor,selected,kDown)&&selected&&cursor==5,"DEFAULT -> PC SETTINGS");
    check(move_extra_row(cursor,selected,kDown)&&!selected&&cursor==5,"PC SETTINGS -> EXIT");
    check(move_extra_row(cursor,selected,kUp)&&selected&&cursor==5,"EXIT -> PC SETTINGS");
    check(move_extra_row(cursor,selected,kUp)&&!selected&&cursor==4,"PC SETTINGS -> DEFAULT");
    cursor=1;selected=false;
    check(!move_extra_row(cursor,selected,kDown)&&cursor==1&&!selected,"existing rows keep native navigation");
    cursor=5;
    check(!move_extra_row(cursor,selected,kDown)&&cursor==5&&!selected,"EXIT wrap remains native");
    TextBatch text;text.text("PC SETTINGS",32,310,0xFFFFFFFF);
    check(!text.vertices.empty(),"serif glyphs available");
    check(text.vertices.size()%6==4,"separate glyph spans form one strip");
    check((text.vertices.size()+2)/6<131072,"entry geometry stays within the expanded UI batch");
    for(const auto& vertex:text.vertices)check(vertex.x>=0&&vertex.x<640&&vertex.y>=0&&vertex.y<384,"entry fits original menu panel");
    size_t quad_count=0;
    for(size_t first=0;first+3<text.vertices.size();first+=6) {
        const auto& a=text.vertices[first];const auto& b=text.vertices[first+3];
        check(b.x>a.x && b.y>a.y,"batched glyph rectangles retain positive area");
        ++quad_count;
    }
    check(quad_count<131072,"entry fits single GPU batch");
    std::printf("[pc-menu-test] %s: navigation and %zu text vertices\n",failures?"FAILED":"PASS",text.vertices.size());
    return failures?1:0;
}
#endif
