/* Original native SRT layout. Item art is read from the selected game copy;
 * it is never bundled with the port. */
#include "cv_srt.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <tuple>
#include <cstdlib>
#include <cstdio>
#include "cv_srt_text.h"
#include "cv_srt_icons.h"
#include "cv_srt_i18n.h"
#pragma comment(lib,"gdiplus.lib")
using namespace Gdiplus;
namespace {
struct Config {
    bool enabled=false,name=true,health=true,time=true,stats=true,inventory=false,inventory_below=false,window=false;
    cv_srt_i18n::Language lang=cv_srt_i18n::EN;
    int enemies=3,scale=100,corner=0,opacity=70;
    std::wstring credit=L"by Kastiel",game_root;
    std::array<std::wstring,17> words={L"Health",L"Game time",L"Saves",L"Retries",L"Enemies",L"Inventory",L"Equipped",L"Fine",L"Caution",L"Danger",L"Poison",L"Gas",L"Dead",L"Waiting for a game",L"Empty",L"Boss",L"Phase"};
};
SRWLOCK config_lock=SRWLOCK_INIT;Config config;uint64_t generation=0;
int env_int(const char* key,int fallback) {const char* e=std::getenv(key);return e&&*e?std::atoi(e):fallback;}
std::wstring utf8(const char* s) {
    if(!s)return L"";const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,nullptr,0);
    if(n<=1)return L"";std::wstring w(size_t(n),0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w.data(),n);w.pop_back();return w;
}
const wchar_t* character_name(int id) {
    static const wchar_t* names[]={L"CLAIRE REDFIELD",L"CHRIS REDFIELD",L"STEVE BURNSIDE",L"ALBERT WESKER"};
    return id>=0&&id<4?names[id]:L"—";
}
std::wstring number(int n) {return std::to_wstring(n);}
using cv_srt_text::upper;
struct Tile {int index,slot,wide;};
// Entry 0 is the fixed slot (lighter, lockpick...): drawn on its own above
// the equipped weapon, never in the grid.
std::vector<Tile> tiles(const cv_srt_state& state) {
    std::vector<Tile> out;int last=1;for(int i=1;i<CV_SRT_INVENTORY_SIZE;i++)if(state.inventory[i].id)last=i;
    int slot=0;
    for(int i=1;i<CV_SRT_INVENTORY_SIZE && slot<12;i++) {
        if(i>last&&slot>=10)break;
        const int wide=state.inventory[i].wide?2:1;if(wide==2&&(slot&1))slot++;
        if(slot+wide>12)break;out.push_back({i,slot,wide});slot+=wide;
    }
    return out;
}
struct ItemImages {
    cv_srt_icons::Library library;
    std::wstring root;
    std::map<int,std::unique_ptr<Bitmap>> images;
    void load(const std::wstring& selected) {
        if(root!=selected) {root=selected;images.clear();}
        library.load(selected);
    }
    Bitmap* get(int id) {
        auto cached=images.find(id);if(cached!=images.end())return cached->second.get();
        const auto* icon=library.get(id);if(!icon)return nullptr;
        auto image=std::make_unique<Bitmap>(icon->width,icon->height,PixelFormat32bppARGB);
        Rect rect(0,0,icon->width,icon->height);BitmapData data{};
        if(image->LockBits(&rect,ImageLockModeWrite,PixelFormat32bppARGB,&data)!=Ok)return nullptr;
        for(int y=0;y<icon->height;y++)std::memcpy(static_cast<uint8_t*>(data.Scan0)+y*data.Stride,icon->argb.data()+y*icon->width,icon->width*4);
        image->UnlockBits(&data);
        return images.emplace(id,std::move(image)).first->second.get();
    }
} item_images;
// A square photo embedded from launcher_art/ (RCDATA named after the file).
// Missing or unreadable: nullptr, remembered, and the layout goes without.
Bitmap* art(const wchar_t* name) {
    static std::map<std::wstring,std::unique_ptr<Bitmap>> cache;
    auto found=cache.find(name);if(found!=cache.end())return found->second.get();
    auto& image=cache[name];
    const HMODULE module=GetModuleHandleW(nullptr);const HRSRC resource=FindResourceW(module,name,MAKEINTRESOURCEW(10));
    if(!resource)return nullptr;const DWORD size=SizeofResource(module,resource);
    const void* data=LockResource(LoadResource(module,resource));if(!data||!size||size>1024*1024)return nullptr;
    HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,size);if(!block)return nullptr;
    void* bytes=GlobalLock(block);if(!bytes) {GlobalFree(block);return nullptr;}
    std::memcpy(bytes,data,size);GlobalUnlock(block);
    IStream* stream=nullptr;if(CreateStreamOnHGlobal(block,TRUE,&stream)!=S_OK) {GlobalFree(block);return nullptr;}
    {
        Bitmap source(stream);const UINT w=source.GetWidth(),h=source.GetHeight();
        if(source.GetLastStatus()==Ok&&w&&w==h&&w<=512)image.reset(source.Clone(0,0,int(w),int(h),PixelFormat32bppARGB));
    }
    stream->Release();
    return image.get();
}
Bitmap* portrait(int character) {
    const wchar_t* names[]={L"SRT_CLAIRE",L"SRT_CHRIS",L"SRT_STEVE",L"SRT_WESKER"};
    return character>=0&&character<4?art(names[character]):nullptr;
}
// Bosses, by enemy type: their photo (launcher_art/srt_boss_<name>.png) and,
// for Alexia, the phase. 21 is the small Albinoid, 22 the boss.
struct Boss {int type;const wchar_t* photo;int phase;};
const Boss* boss(int type) {
    static const Boss bosses[]={{2,L"SRT_BOSS_GULP_WORM",0},{12,L"SRT_BOSS_ALEXIA",1},{13,L"SRT_BOSS_ALEXIA",2},
        {14,L"SRT_BOSS_ALEXIA",3},{15,L"SRT_BOSS_NOSFERATU",0},{17,L"SRT_BOSS_STEVE",0},{19,L"SRT_BOSS_TYRANT",0},
        {22,L"SRT_BOSS_ALBINOID",0},{23,L"SRT_BOSS_GIANT_SPIDER",0}};
    for(const auto& b:bosses)if(b.type==type)return &b;
    return nullptr;
}
// The small photo beside an ordinary enemy (launcher_art/srt_enemy_<name>.png).
Bitmap* enemy_photo(int type) {
    switch(type) {
    case 1:return art(L"SRT_ENEMY_ZOMBIE");case 3:return art(L"SRT_ENEMY_SPIDER");case 4:return art(L"SRT_ENEMY_DOG");
    case 5:return art(L"SRT_ENEMY_HUNTER");case 6:return art(L"SRT_ENEMY_MOTH");case 7:return art(L"SRT_ENEMY_BAT");
    case 9:return art(L"SRT_ENEMY_BANDERSNATCH");case 21:return art(L"SRT_ENEMY_ALBINOID");case 26:return art(L"SRT_ENEMY_ANATOMIST");
    case 29:return art(L"SRT_ENEMY_TENTACLE");case 30:return art(L"SRT_ENEMY_ALEXIA_BABY");default:return nullptr;
    }
}
Bitmap* boss_photo(const Boss& b) {
    if(b.phase) {wchar_t name[48];swprintf(name,48,L"%s_%d",b.photo,b.phase);if(Bitmap* own=art(name))return own;}
    return art(b.photo);
}
struct Canvas {
    cv_srt_i18n::Language lang;
    float scale,opacity;Bitmap bitmap;Graphics g;Color cream{255,216,207,192},dim{255,149,142,134},red{255,163,51,44},gold{255,201,162,74};
    Canvas(int w,int h,float scale,int background_opacity,cv_srt_i18n::Language language):lang(language),scale(scale),opacity(background_opacity/100.f),bitmap(int(std::ceil(w*scale)),int(std::ceil(h*scale)),PixelFormat32bppARGB),g(&bitmap) {g.Clear(Color(0,0,0,0));g.ScaleTransform(scale,scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);}
    void box(float x,float y,float w,float h,Color color) {SolidBrush b(color);g.FillRectangle(&b,x,y,w,h);}
    void background(float x,float y,float w,float h,Color color) {box(x,y,w,h,Color(BYTE(std::lround(color.GetA()*opacity)),color.GetR(),color.GetG(),color.GetB()));}
    void line(float x,float y,float x2,float y2,Color c,float size=1) {Pen p(c,size);g.DrawLine(&p,x,y,x2,y2);}
    void text(const std::wstring& value,float x,float y,float w,float size,Color c,bool bold=false,bool right=false,bool serif=false,float h=30,bool wrap=false,bool ellipsis=true) {
        // Resolve text at 2x into a cached, native-pixel tile. Only the text is
        // supersampled: the panel, game and per-frame ECG keep their own cost.
        using Key=std::tuple<std::wstring,std::wstring,int,int,int,int,uint32_t>;
        static std::map<Key,std::unique_ptr<Bitmap>> cache;static size_t bytes=0;
        const int pw=std::max(1,int(std::ceil(w*scale))),ph=std::max(1,int(std::ceil(h*scale)));
        const bool rtl=cv_srt_i18n::rtl_text(value);
        const std::wstring face=serif?L"Georgia":cv_srt_i18n::font_face(lang,bold);
        const int flags=(bold?1:0)|(right?2:0)|(serif?4:0)|(wrap?8:0)|(ellipsis?16:0)|(rtl?32:0);
        Key key{value,face,pw,ph,int(std::lround(size*scale*64)),flags,c.GetValue()};
        auto found=cache.find(key);
        if(found==cache.end()) {
            const size_t cost=size_t(pw)*size_t(ph)*4;
            if(cache.size()>=96||bytes+cost>12*1024*1024) {cache.clear();bytes=0;}
            Bitmap high(pw*2,ph*2,PixelFormat32bppPARGB);Graphics hg(&high);
            hg.Clear(Color(0,0,0,0));hg.ScaleTransform(2,2);
            hg.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            FontFamily family(face.c_str());
            Font font(&family,size*scale,FontStyleRegular,UnitPixel);
            StringFormat format;format.SetLineAlignment(StringAlignmentCenter);
            // Under RTL, Near is the right edge: swap so Arabic keeps the same layout slots.
            format.SetAlignment(right!=rtl?StringAlignmentFar:StringAlignmentNear);
            format.SetTrimming(ellipsis?(wrap?StringTrimmingEllipsisWord:StringTrimmingEllipsisCharacter):StringTrimmingNone);format.SetFormatFlags((wrap?StringFormatFlagsLineLimit:StringFormatFlagsNoWrap)|(rtl?StringFormatFlagsDirectionRightToLeft:0));
            SolidBrush halo(Color(105,0,0,0)),shadow(Color(210,0,0,0)),ink(c);
            const float offsets[][2]={{-.65f,0},{.65f,0},{0,-.65f},{0,.65f}};
            for(const auto& d:offsets)hg.DrawString(value.c_str(),-1,&font,RectF(d[0],d[1],float(pw),float(ph)),&format,&halo);
            hg.DrawString(value.c_str(),-1,&font,RectF(1,1,float(pw),float(ph)),&format,&shadow);
            hg.DrawString(value.c_str(),-1,&font,RectF(0,0,float(pw),float(ph)),&format,&ink);
            hg.Flush(FlushIntentionSync);
            auto tile=std::make_unique<Bitmap>(pw,ph,PixelFormat32bppPARGB);
            {
                Graphics resolve(tile.get());resolve.SetCompositingMode(CompositingModeSourceCopy);
                resolve.SetInterpolationMode(InterpolationModeHighQualityBicubic);resolve.SetPixelOffsetMode(PixelOffsetModeHalf);
                resolve.DrawImage(&high,Rect(0,0,pw,ph),0,0,pw*2,ph*2,UnitPixel);
            }
            bytes+=cost;found=cache.emplace(std::move(key),std::move(tile)).first;
        }
        // Snap to physical pixels, including the inventory column transform.
        Matrix transform;g.GetTransform(&transform);PointF position(x,y);transform.TransformPoints(&position,1);
        const GraphicsState saved=g.Save();g.ResetTransform();
        g.DrawImage(found->second.get(),int(std::lround(position.X)),int(std::lround(position.Y)),pw,ph);
        g.Restore(saved);
    }
    bool character(int id,float x,float y) {
        Bitmap* image=portrait(id);if(!image)return false;
        photo(image,x,y,24,Color(200,0,0,0));
        return true;
    }
    // Every distinct pixel of a photo becomes a quad, so above ~44 screen
    // pixels the photo is resolved at 44x44 and enlarged in blocks: its cost
    // stays near 2000 quads at 4K and 200%, instead of tens of thousands.
    void photo(Bitmap* image,float x,float y,float size,Color frame) {
        box(x-1,y-1,size+2,size+2,frame);
        const int detail=44;const GraphicsState saved=g.Save();g.SetPixelOffsetMode(PixelOffsetModeHalf);
        if(size*scale<=detail) {
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(image,RectF(x,y,size,size),0,0,REAL(image->GetWidth()),REAL(image->GetHeight()),UnitPixel);
        } else {
            Bitmap reduced(detail,detail,PixelFormat32bppARGB);
            {Graphics sg(&reduced);sg.SetInterpolationMode(InterpolationModeHighQualityBicubic);sg.SetPixelOffsetMode(PixelOffsetModeHalf);
             sg.DrawImage(image,Rect(0,0,detail,detail),0,0,int(image->GetWidth()),int(image->GetHeight()),UnitPixel);}
            g.SetInterpolationMode(InterpolationModeNearestNeighbor);
            g.DrawImage(&reduced,RectF(x,y,size,size),0,0,REAL(detail),REAL(detail),UnitPixel);
        }
        g.Restore(saved);
    }
    void diamond(float cx,float cy,float r,Color c,bool filled) {
        const PointF p[]={PointF(cx,cy-r),PointF(cx+r,cy),PointF(cx,cy+r),PointF(cx-r,cy)};
        if(filled) {SolidBrush b(c);g.FillPolygon(&b,p,4);} else {Pen pen(c,1);g.DrawPolygon(&pen,p,4);}
    }
    bool icon(int id,float x,float y,float w,float h) {
        Bitmap* image=item_images.get(id);if(!image)return false;
        const float fit=std::min(w/image->GetWidth(),h/image->GetHeight());
        const float iw=image->GetWidth()*fit,ih=image->GetHeight()*fit;
        const RectF target(x+(w-iw)*.5f,y+(h-ih)*.5f,iw,ih);
        const GraphicsState saved=g.Save();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHalf);
        // Shadow follows the icon's alpha, keeping holes and the keyed background clear.
        ColorMatrix matrix={};matrix.m[3][3]=.7f;matrix.m[4][4]=1;
        ImageAttributes shadow;shadow.SetColorMatrix(&matrix);
        g.DrawImage(image,RectF(target.X+1,target.Y+1,iw,ih),0,0,REAL(image->GetWidth()),REAL(image->GetHeight()),UnitPixel,&shadow);
        g.DrawImage(image,target,0,0,REAL(image->GetWidth()),REAL(image->GetHeight()),UnitPixel);
        g.Restore(saved);return true;
    }
    void item_text(const std::vector<std::wstring>& lines,float x,float y,float w,float size,Color ink,float line_height) {
        for(const auto& line:lines) {text(line,x,y,w,size,ink,false,false,false,line_height,false,false);y+=line_height;}
    }
    void heading(const std::wstring& label,float y) {
        text(upper(label),18,y,268,14.5f,red,true,false,false,22);line(18,y+23,286,y+23,Color(255,67,39,35));
    }
    void bar(float x,float y,float w,float h,float ratio,Color c) {
        box(x+1,y+1,w+2,h+2,Color(155,0,0,0));
        box(x-1,y-1,w+2,h+2,Color(205,0,0,0));
        box(x,y,w,h,Color(255,40,35,34));box(x,y,w*std::clamp(ratio,0.f,1.f),h,c);
    }
};
std::vector<rsx_ui_quad> quads;
size_t base_count=0;uint64_t base_gen=0;
struct PulseArea {float x=0,y=0,w=0,scale=1;uint32_t color=0;bool dead=false;} pulse;
// Boss blocks, in logical units while the layout is drawn, then in screen
// pixels: the bar's filled part and the frame, animated over the cache.
struct BossFx {float x,y,w,h,fill,fx,fy,fw,fh;};
std::vector<BossFx> boss_fx;float fx_scale=1;
void animate_bosses() {
    if(boss_fx.empty())return;
    const double t=double(GetTickCount64()%1000000)/1000.0;
    // The bar breathes once every 1.4 s; the frame brightens with it.
    const float bright=float(.5+.5*std::sin(t*4.5));
    const float line=std::max(1.f,std::round(fx_scale));
    for(const auto& b:boss_fx) {
        const uint32_t frame=uint32_t(std::lround(200*bright))<<24|0xf3d27a;
        quads.push_back({b.fx,b.fy,b.fw,line,frame,0});quads.push_back({b.fx,b.fy+b.fh-line,b.fw,line,frame,0});
        quads.push_back({b.fx,b.fy,line,b.fh,frame,0});quads.push_back({b.fx+b.fw-line,b.fy,line,b.fh,frame,0});
        if(b.fill<1)continue;
        quads.push_back({b.x,b.y,b.fill,b.h,uint32_t(std::lround(110*bright))<<24|0xff6e5a,0});
        // A glow that crosses only the life left, slowly, then rests.
        const float band=40*fx_scale,speed=55*fx_scale,run=(b.fill+band)/speed,period=run+1.2f;
        const float centre=float(std::fmod(t,double(period)))*speed-band*.5f;
        for(int x=int(std::floor(centre-band*.5f));x<int(std::ceil(centre+band*.5f));x++) {
            if(x<0||x>=int(b.fill))continue;
            const float d=(x+.5f-centre)/(band*.28f);
            const uint32_t alpha=uint32_t(std::lround(150*std::exp(-d*d)));
            if(alpha)quads.push_back({b.x+x,b.y,1,b.h,alpha<<24|0xffebc8,0});
        }
    }
}
void animate_pulse() {
    quads.resize(base_count);animate_bosses();if(pulse.w<=0)return;
    const float points[][2]={{0,0},{.08f,0},{.12f,-2},{.17f,0},{.28f,0},{.31f,3},{.34f,-11},{.37f,7},{.42f,0},{.55f,-3},{.66f,0},{1,0}};
    // A full cycle now takes ~2.2 seconds, instead of ~0.9 seconds.
    const double phase=double(GetTickCount64()%100000)/1000.0*.45;
    auto sample=[&](float x) {
        if(pulse.dead)return 0.f;
        const float t=float(std::fmod(phase+x*.85,1.0));
        for(size_t i=1;i<sizeof points/sizeof points[0];i++)if(t<=points[i][0]) {
            const float r=(t-points[i-1][0])/(points[i][0]-points[i-1][0]);
            return (points[i-1][1]+r*(points[i][1]-points[i-1][1]))*pulse.scale;
        }
        return 0.f;
    };
    float prior=sample(0);
    const float half_width=std::max(.65f,.7f*pulse.scale);
    for(int pass=0;pass<2;pass++) {
      prior=sample(0);
      for(int x=0;x<int(pulse.w);x++) {
        const float ratio=float(x)/std::max(1.f,pulse.w-1),value=sample(ratio);
        const float top=std::min(value,prior)-half_width,bottom=std::max(value,prior)+half_width;
        const float core_top=std::ceil(top),core_bottom=std::floor(bottom);
        const float coverage[]={core_top-top,1.f,bottom-core_bottom};
        const float y[]={std::floor(top),core_top,core_bottom};
        const float h[]={1.f,std::max(0.f,core_bottom-core_top),1.f};
        for(int edge=0;edge<3;edge++) {
            const uint32_t alpha=uint32_t(std::lround((90+165*ratio)*coverage[edge]));
            const uint32_t color=pass==0?((alpha*3/4)<<24):(pulse.color&0xffffff)|(alpha<<24);
            quads.push_back({pulse.x+x+(pass==0?1.f:0.f),pulse.y+y[edge]+(pass==0?1.f:0.f),1.f,h[edge],color,0});
        }
        prior=value;
      }
    }
}

void build(const cv_srt_state& st,const Config& cfg,uint32_t width,uint32_t height) {
    boss_fx.clear();
    static ULONG_PTR token=0;if(!token) {GdiplusStartupInput input;GdiplusStartup(&token,&input,nullptr);}
    Bitmap measure_bitmap(1,1,PixelFormat32bppARGB);Graphics measure_graphics(&measure_bitmap);
    Font credit_font(L"Segoe UI",12,FontStyleRegular,UnitPixel);RectF credit_bounds;
    measure_graphics.MeasureString(cfg.credit.c_str(),-1,&credit_font,PointF(0,0),&credit_bounds);
    const bool credit_wrap=credit_bounds.Width>104;
    const auto grid=tiles(st);int gridrows=0;for(const auto& t:grid)gridrows=std::max(gridrows,t.slot/2+1);
    // Bosses first, so a limit of one or two enemies still shows them.
    std::vector<cv_srt_enemy> shown(st.enemies,st.enemies+std::clamp(st.enemy_count,0,int(CV_SRT_MAX_ENEMIES)));
    std::stable_partition(shown.begin(),shown.end(),[](const cv_srt_enemy& e){return boss(e.type)!=nullptr;});
    const int count=cfg.enemies==99?int(shown.size()):std::min(int(shown.size()),cfg.enemies);
    int bosses=0;for(int i=0;i<count;i++)if(boss(shown[i].type))bosses++;
    int h=16+(cfg.name?28:0)+(cfg.health?104:0)+(cfg.time?40:0)+(cfg.stats?45:0);
    if(cfg.enemies)h+=28+std::max(1,count)*39+bosses*45;
    h+=credit_wrap?42:26;
    // The inventory goes beside the main panel, or under the enemies.
    const bool inventory=st.valid&&cfg.inventory,below=inventory&&cfg.inventory_below,side=inventory&&!below;
    if(inventory)item_images.load(cfg.game_root);
    const bool right=cfg.corner==1||cfg.corner==3;
    const float main_x=side&&right?316.f:0.f,inventory_x=below?main_x:right?0.f:316.f;
    // Measure labels once per snapshot, at the original font size. Reserve a
    // separate quantity line, and let both cells in a row share its max height.
    Font item_font(cv_srt_i18n::font_face(cfg.lang),13,FontStyleRegular,UnitPixel),equipped_font(cv_srt_i18n::font_face(cfg.lang),14,FontStyleRegular,UnitPixel);
    const float item_line=std::max(18.f,float(std::ceil(item_font.GetHeight(&measure_graphics)))),equipped_line=std::max(19.f,float(std::ceil(equipped_font.GetHeight(&measure_graphics))));
    auto label=[&](const std::wstring& value,float available,const Font& font) {
        return cv_srt_text::wrap(value,available,[&](const std::wstring& part) {
            RectF bounds;measure_graphics.MeasureString(part.c_str(),-1,&font,PointF(0,0),&bounds);
            return bounds.Width+2; // Room for the shadow and native-pixel rounding.
        });
    };
    struct ItemLabel {std::vector<std::wstring> lines;float icon=0;};
    std::array<ItemLabel,CV_SRT_INVENTORY_SIZE> labels;
    std::vector<float> row_height(size_t(gridrows),33),row_top(size_t(gridrows),0);
    const cv_srt_item equipped=st.equipped>0&&st.equipped<=CV_SRT_INVENTORY_SIZE?st.inventory[st.equipped-1]:cv_srt_item{};
    float equipped_icon=0,equipped_height=44;
    std::vector<std::wstring> equipped_lines;
    const cv_srt_item fixed=st.inventory[0];
    ItemLabel fixed_label;float fixed_height=0;
    if(inventory) {
        if(fixed.id) {
            fixed_label.icon=item_images.get(fixed.id)?34.f:0.f;
            fixed_label.lines=label(cv_srt_i18n::item(fixed.id,cfg.lang),256-fixed_label.icon,item_font);
            fixed_height=std::max(33.f,4.f+fixed_label.lines.size()*item_line+(fixed.counted?15.f:0.f));
        }
        equipped_icon=item_images.get(equipped.id)?(equipped.wide?66.f:42.f):0.f;
        equipped_lines=label(equipped.id?cv_srt_i18n::item(equipped.id,cfg.lang):cfg.words[14],250-equipped_icon,equipped_font);
        equipped_height=std::max(44.f,23.f+equipped_lines.size()*equipped_line);
        for(const auto& tile:grid) {
            const auto& item=st.inventory[tile.index];auto& name=labels[tile.index];
            name.icon=item_images.get(item.id)?(tile.wide==2?62.f:34.f):0.f;
            name.lines=label(item.id?cv_srt_i18n::item(item.id,cfg.lang):cfg.words[14],(tile.wide==2?268.f:130.f)-12-name.icon,item_font);
            const float content=4.f+name.lines.size()*item_line+(item.id&&item.counted?15.f:0.f);
            row_height[tile.slot/2]=std::max(row_height[tile.slot/2],content);
        }
    }
    float grid_height=0;
    for(int row=0;row<gridrows;row++) {row_top[row]=grid_height;grid_height+=row_height[row]+3;}
    const int inventory_height=int(std::ceil(12+30+(fixed.id?fixed_height+6:0)+equipped_height+8+grid_height+13));
    const int logical_width=!st.valid?260:side?620:304;
    if(!st.valid)h=44;else if(side)h=std::max(h,inventory_height);else if(below)h+=inventory_height-8;
    float scale=float(height)/900.f*cfg.scale/100.f;
    scale=std::min(scale,float(height-32)/float(h));scale=std::min(scale,float(width-32)/float(logical_width));
    Canvas c(logical_width,h,scale,cfg.opacity,cfg.lang);
    auto panel=[&](float x,float ph,float pw=304) {
        c.background(x,0,pw,ph,Color(255,12,12,14));
        c.line(x,0,x+pw,0,Color(190,163,51,44));c.line(x,ph-1,x+pw,ph-1,Color(100,93,68,52));
        c.line(x,0,x,ph,Color(100,103,91,74));c.line(x+pw-1,0,x+pw-1,ph,Color(100,103,91,74));
    };
    panel(main_x,float(h),float(st.valid?304:logical_width));if(side)panel(inventory_x,float(inventory_height));
    c.g.TranslateTransform(main_x,0);
    float ecg_y=-1;uint32_t ecg_color=0;
    float y=12;
    if(!st.valid) {
        c.text(cfg.words[13],16,5,logical_width-32,14,c.cream,false,false,false,34);
    } else {
        if(cfg.name) {const float photo=c.character(st.character,18,y)?32.f:0.f;c.text(character_name(st.character),18+photo,y,268-photo,17,c.cream,true,false,false,24);c.line(18,y+25,286,y+25,Color(255,67,39,35));y+=28;}
        static const uint32_t colors[]={0xff79bb91,0xffd7bd66,0xffdc914e,0xffe36461,0xffbf8bea,0xffb0a5bf,0xffb65955};
        const int condition=std::clamp(st.condition,0,6);const Color state_color(colors[condition]);
        if(cfg.health) {
            const float ratio=st.maximum>0?float(st.hp)/st.maximum:0;
            const int pct=int(std::clamp(ratio,0.f,1.f)*100+.5f);
            c.text(upper(cfg.words[0]),18,y,62,13,c.dim,true,false,false,28);
            c.text(number(st.hp)+L" / "+number(st.maximum),80,y,137,24,c.cream,false,true,true,28);
            c.text(number(pct)+L"%",222,y,64,17,state_color,false,true,false,28);
            c.bar(18,y+32,268,11,ratio,state_color);
            const int word=condition==SRT_FINE?7:condition<=SRT_WARNING?8:condition==SRT_DANGER?9:condition==SRT_POISON?10:condition==SRT_GAS?11:12;
            c.text(upper(cfg.words[word]),196,y+57,90,14,state_color,true,false,false,34);
            ecg_y=y+72;ecg_color=state_color.GetValue();
            for(int row=-1;row<=1;row++)c.line(18,y+72+row*12.f,184,y+72+row*12.f,Color(35,110,120,110),.5f);
            for(int col=0;col<=10;col++)c.line(18+col*16.f,y+60,18+col*16.f,y+84,Color(25,110,120,110),.5f);
            y+=104;
        }
        if(cfg.time) {
            c.line(18,y,286,y,Color(255,67,39,35));
            c.text(upper(cfg.words[1]),18,y+3,125,13,c.dim,true,false,false,30);
            wchar_t time[40];swprintf(time,40,L"%02u:%02u:%02u",st.seconds/3600,(st.seconds/60)%60,st.seconds%60);
            c.text(time,143,y+3,143,23,c.cream,false,true,true,30);y+=40;
        }
        if(cfg.stats) {
            const int n[]={st.saves,st.retry,st.fas};const std::wstring names[]={cfg.words[2],cfg.words[3],L"F.A.S."};
            for(int i=0;i<3;i++) {float x=18+i*92.f;c.text(names[i],x,y,86,13,c.dim,false,false,false,19);c.text(number(n[i]),x,y+17,86,20,c.cream,false,false,true,25);}
            y+=45;
        }
        if(cfg.enemies) {
            c.heading(cfg.words[4],y);y+=28;
            if(!count) {c.text(L"—",18,y,268,14,c.dim,false,false,false,24);y+=39;}
            for(int i=0;i<count;i++) {
                const auto& e=shown[i];
                if(const Boss* b=boss(e.type)) {
                    // A framed block: photo, BOSS, the name large, a thicker bar
                    // in quarters, Alexia's phase as three diamonds.
                    const Color frame(255,201,162,74),hp_ink(255,222,116,107);
                    c.background(18,y,268,76,Color(255,28,20,12));
                    c.line(18,y,286,y,frame);c.line(18,y+75,286,y+75,frame);c.line(18,y,18,y+76,frame);c.line(285,y,285,y+76,frame);
                    Bitmap* image=boss_photo(*b);const float tx=image?86.f:26.f,right=278;
                    if(image)c.photo(image,26,y+12,52,frame);
                    const float ratio=e.maximum?std::clamp(float(e.hp)/e.maximum,0.f,1.f):0;
                    const std::wstring pct=number(int(ratio*100+.5f))+L"%";
                    c.text(upper(cfg.words[15]),tx,y+3,120,11,frame,true,false,false,16);
                    if(b->phase)for(int k=0;k<3;k++)c.diamond(right-28+k*12.f,y+11,4.5f,frame,k<b->phase);
                    else c.text(pct,right-60,y+3,60,12,frame,false,true,false,16);
                    c.text(upper(cv_srt_i18n::enemy(b->phase?12:e.type,cfg.lang)),tx,y+16,right-tx,18,c.cream,false,false,true,26);
                    c.bar(tx,y+44,right-tx,12,ratio,Color(255,184,50,42));
                    boss_fx.push_back({main_x+tx,y+44,right-tx,12,ratio,main_x+18,y,268,76});
                    for(int q=1;q<4;q++)c.line(tx+(right-tx)*q/4.f,y+44,tx+(right-tx)*q/4.f,y+56,Color(255,12,12,14));
                    if(b->phase) {
                        static const wchar_t* roman[]={L"I",L"II",L"III"};
                        c.text(upper(cfg.words[16])+L" "+roman[b->phase-1]+L" · "+pct,tx,y+57,140,12,frame,false,false,false,17);
                    }
                    c.text(number(e.hp)+L" / "+number(e.maximum),right-110,y+57,110,12,hp_ink,false,true,false,17);
                    y+=84;continue;
                }
                Bitmap* image=enemy_photo(e.type);const float ex=image?56.f:18.f;
                if(image)c.photo(image,18,y+1,32,Color(255,67,39,35));
                c.text(cv_srt_i18n::enemy(e.type,cfg.lang),ex,y,169-ex,14,c.cream,false,false,false,22);
                const std::wstring hp=number(e.hp)+L" / "+number(e.maximum);
                c.text(hp,171,y,115,14,Color(255,222,116,107),false,true,false,22);
                c.bar(ex,y+22,286-ex,11,e.maximum?float(e.hp)/e.maximum:0,Color(255,163,51,44));y+=39;
            }
        }
        if(inventory) {
            if(side) {y=12;c.g.TranslateTransform(inventory_x-main_x,0);} else y+=4;
            c.heading(cfg.words[5],y);y+=30;
            if(fixed.id) {
                c.background(18,y,268,fixed_height,Color(90,31,28,28));c.line(18,y,286,y,Color(150,66,52,45));
                if(fixed_label.icon)c.icon(fixed.id,23,y+(fixed_height-25)*.5f,fixed_label.icon-6,25);
                const float tx=24+fixed_label.icon,available=256-fixed_label.icon;
                c.item_text(fixed_label.lines,tx,y+1,available,13,c.cream,item_line);
                if(fixed.counted)c.text(cv_srt_i18n::quantity(fixed),tx,y+1+fixed_label.lines.size()*item_line,available,12,c.dim,false,true,false,15);
                y+=fixed_height+6;
            }
            c.background(18,y,268,equipped_height,Color(100,55,43,23));c.line(18,y,286,y,c.gold);
            if(equipped_icon)c.icon(equipped.id,25,y+(equipped_height-32)*.5f,equipped_icon-8,32);
            const auto loaded=cv_srt_i18n::quantity(equipped);
            c.text(upper(cfg.words[6]),25+equipped_icon,y+1,250-equipped_icon-(loaded.empty()?0:66),13,c.gold,false,false,false,20);
            if(!loaded.empty())c.text(loaded,217,y+1,58,14,equipped.quantity==0&&!equipped.infinite?Color(255,227,100,97):c.gold,false,true,false,20);
            c.item_text(equipped_lines,25+equipped_icon,y+20,250-equipped_icon,14,c.cream,equipped_line);
            y+=equipped_height+8;
            for(const auto& tile:grid) {
                const auto& item=st.inventory[tile.index];const auto& name=labels[tile.index];
                const float x=18+(tile.slot%2)*138.f,top=y+row_top[tile.slot/2],th=row_height[tile.slot/2];
                const float tw=tile.wide==2?268.f:130.f;const bool selected=st.equipped==tile.index+1;
                c.background(x,top,tw,th,Color(90,31,28,28));c.line(x,top,x+tw,top,selected?c.gold:Color(150,66,52,45));
                if(name.icon)c.icon(item.id,x+5,top+(th-25)*.5f,name.icon-6,25);
                const float tx=x+6+name.icon,available=tw-12-name.icon;
                c.item_text(name.lines,tx,top+1,available,13,item.id?c.cream:c.dim,item_line);
                if(item.id&&item.counted)c.text(cv_srt_i18n::quantity(item),tx,top+1+name.lines.size()*item_line,available,12,selected?c.gold:c.dim,false,true,false,15);
            }
            if(side)c.g.TranslateTransform(main_x-inventory_x,0);
        }
        c.text(L"SRT · CODE:VERONICA",18,float(h-(credit_wrap?41:25)),credit_wrap?268:156,12,c.dim,false,false,false,22);
        c.text(cfg.credit,credit_wrap?18:178,float(h-25),credit_wrap?268:108,12,c.cream,false,true,false,22);
    }
    // Rasterize at final back-buffer resolution: never point-scale the text.
    // Coalesce equal adjacent rows as well as horizontal runs. Cached geometry
    // remains one draw call and solid panel areas do not cost one quad/pixel.
    const int pw=int(c.bitmap.GetWidth()),ph=int(c.bitmap.GetHeight());
    BitmapData pixels{};Rect r(0,0,pw,ph);quads.clear();
    if(c.bitmap.LockBits(&r,ImageLockModeRead,PixelFormat32bppARGB,&pixels)!=Ok)return;
    const float x0=(cfg.corner==1||cfg.corner==3)?float(width)-16-pw:16.f;
    const float y0=cfg.corner>=2?float(height)-16-ph:16.f;
    struct Run {int x,end;uint32_t color;size_t quad;};
    // Extra antialias/shadow shades can create many runs at large HUD scales.
    // Coarsen only those shades when needed, instead of dropping the tail of
    // the panel at the backend's 131072-quad limit. Normal-size HUDs are exact.
    for(unsigned step=0;;step=step?step*2:4) {
      quads.clear();
      auto color_at=[&](uint32_t color) {
        if(!step || !(color>>24) || (color&0xffffff)==0x0c0c0e ||
           color==0xffa3332c || color==0xff282322 || color==0xff000000)return color;
        uint32_t out=0;
        for(unsigned shift=0;shift<32;shift+=8) {
            const unsigned channel=(color>>shift)&255;
            out|=std::min(255u,((channel+step/2)/step)*step)<<shift;
        }
        return out;
      };
      std::vector<Run> previous,current;
      for(int row=0;row<ph;row++) {
        const auto* px=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(pixels.Scan0)+row*pixels.Stride);
        current.clear();size_t search=0;
        for(int x=0;x<pw;) {
            const uint32_t color=color_at(px[x]);int end=x+1;while(end<pw&&color_at(px[end])==color)end++;
            if(color>>24) {
                while(search<previous.size()&&previous[search].x<x)search++;
                size_t index=quads.size();
                if(search<previous.size()&&previous[search].x==x&&previous[search].end==end&&previous[search].color==color) {
                    index=previous[search].quad;quads[index].h+=1.f;
                } else quads.push_back({x0+x,y0+row,float(end-x),1.f,color,0});
                current.push_back({x,end,color,index});
            }
            x=end;
        }
        previous.swap(current);
      }
      if(quads.size()+size_t(166*scale)*6<=120000 || step>=256)break;
    }
    c.bitmap.UnlockBits(&pixels);
    base_count=quads.size();base_gen++;
    fx_scale=scale;
    for(auto& b:boss_fx) {   // logical -> screen pixels, on the same pixel grid as the cache
        const float fill=b.w*b.fill;
        b={std::round(x0+b.x*scale),std::round(y0+b.y*scale),std::round(b.w*scale),std::round(b.h*scale),std::round(fill*scale),
           std::round(x0+b.fx*scale),std::round(y0+b.fy*scale),std::round(b.fw*scale),std::round(b.fh*scale)};
    }
    pulse={};
    if(ecg_y>=0) {
        pulse={std::round(x0+(main_x+18)*scale),std::round(y0+ecg_y*scale),std::floor(166*scale),scale,ecg_color,st.condition==SRT_DEAD};
        quads.reserve(base_count+size_t(pulse.w)*6+boss_fx.size()*size_t(8+48*scale)+1);
    }
}
}
// ---- The SRT in a window of its own (SRT_WINDOW=1) ------------------------
// The same quads, rasterised on the CPU into a premultiplied 32-bit image and
// shown by a borderless, always-on-top layered window that can be dragged
// anywhere. Its position is kept in cv_launcher.ini ([srt] janela_x/y).
// The render thread builds the image; the window's own thread shows it.
namespace srt_window {
constexpr UINT WM_SRT_SHOW=WM_APP+1,WM_SRT_HIDE=WM_APP+2;
SRWLOCK lock=SRWLOCK_INIT;
std::vector<uint32_t> shared;int shared_w=0,shared_h=0;   // under lock
HWND hwnd=nullptr;volatile LONG started=0;
std::vector<uint32_t> base,frame;int bw=0,bh=0;float bx=0,by=0;uint64_t cached_gen=0;

std::wstring ini() {
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring path(exe);const size_t slash=path.find_last_of(L"\\/");
    return (slash==std::wstring::npos?std::wstring():path.substr(0,slash+1))+L"cv_launcher.ini";
}
void save_position() {
    RECT r;if(!GetWindowRect(hwnd,&r))return;
    const std::wstring path=ini();
    WritePrivateProfileStringW(L"srt",L"janela_x",std::to_wstring(r.left).c_str(),path.c_str());
    WritePrivateProfileStringW(L"srt",L"janela_y",std::to_wstring(r.top).c_str(),path.c_str());
}
POINT initial_position() {
    const std::wstring path=ini();wchar_t bx_[32],by_[32];
    GetPrivateProfileStringW(L"srt",L"janela_x",L"",bx_,32,path.c_str());GetPrivateProfileStringW(L"srt",L"janela_y",L"",by_,32,path.c_str());
    if(bx_[0]&&by_[0]) {
        POINT p{_wtoi(bx_),_wtoi(by_)};
        if(MonitorFromPoint(POINT{p.x+20,p.y+20},MONITOR_DEFAULTTONULL))return p;   // still on a monitor
    }
    // First time: just right of the game window, or its top-left corner.
    RECT g;HWND game=FindWindowW(L"ps3recomp_d3d12",nullptr);
    if(game&&GetWindowRect(game,&g)) {
        HMONITOR m=MonitorFromWindow(game,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof mi};GetMonitorInfoW(m,&mi);
        if(g.right+340<=mi.rcWork.right)return POINT{g.right+8,g.top};
        return POINT{g.left+16,g.top+40};
    }
    return POINT{40,40};
}
void show() {
    std::vector<uint32_t> px;int w,h;
    AcquireSRWLockShared(&lock);px=shared;w=shared_w;h=shared_h;ReleaseSRWLockShared(&lock);
    if(!w||!h)return;
    BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
    void* bits=nullptr;HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);
    HBITMAP dib=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(dib) {
        std::memcpy(bits,px.data(),size_t(w)*h*4);
        HGDIOBJ old=SelectObject(mem,dib);
        SIZE size{w,h};POINT src{0,0};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd,screen,nullptr,&size,mem,&src,0,&blend,ULW_ALPHA);
        SelectObject(mem,old);DeleteObject(dib);
    }
    DeleteDC(mem);ReleaseDC(nullptr,screen);
    if(!IsWindowVisible(hwnd))ShowWindow(hwnd,SW_SHOWNOACTIVATE);
}
// Dragged by hand (button down anywhere, move, release): the system move
// loop would activate the window and take the keyboard from the game.
POINT grab{};bool dragging=false;
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;         // never take the game's focus
    case WM_LBUTTONDOWN: {
        POINT c;GetCursorPos(&c);RECT r;GetWindowRect(w,&r);
        grab={c.x-r.left,c.y-r.top};dragging=true;SetCapture(w);return 0;
    }
    case WM_MOUSEMOVE:
        if(dragging) {POINT c;GetCursorPos(&c);SetWindowPos(w,nullptr,c.x-grab.x,c.y-grab.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
        return 0;
    case WM_LBUTTONUP:case WM_CAPTURECHANGED:
        if(dragging) {dragging=false;if(msg==WM_LBUTTONUP)ReleaseCapture();save_position();}
        return 0;
    case WM_SRT_SHOW:show();return 0;
    case WM_SRT_HIDE:ShowWindow(w,SW_HIDE);return 0;
    case WM_CLOSE:return 0;                             // closes with the game
    }
    return DefWindowProcW(w,msg,wp,lp);
}
DWORD WINAPI thread(LPVOID) {
    WNDCLASSEXW wc{sizeof wc};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.hCursor=LoadCursor(nullptr,IDC_SIZEALL);wc.lpszClassName=L"cv_srt_window";
    wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));RegisterClassExW(&wc);   // the game's icon, if any
    const POINT p=initial_position();
    // A normal top-level window (in the taskbar), so OBS's window capture
    // lists it as "SRT · Code: Veronica".
    hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_APPWINDOW|WS_EX_NOACTIVATE,L"cv_srt_window",L"SRT · Code: Veronica",
                         WS_POPUP,p.x,p.y,16,16,nullptr,nullptr,wc.hInstance,nullptr);
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0) {TranslateMessage(&m);DispatchMessageW(&m);}
    return 0;
}
void hide() {if(hwnd&&IsWindowVisible(hwnd))PostMessageW(hwnd,WM_SRT_HIDE,0,0);}
// Over in premultiplied BGRA, one quad at a time, in order.
void fill(std::vector<uint32_t>& img,int w,int h,const rsx_ui_quad& q) {
    const uint32_t a=q.argb>>24;if(!a)return;
    const int x0=std::max(0,int(std::floor(q.x-bx))),y0=std::max(0,int(std::floor(q.y-by)));
    const int x1=std::min(w,int(std::ceil(q.x-bx+q.w))),y1=std::min(h,int(std::ceil(q.y-by+q.h)));
    const uint32_t r=(q.argb>>16&255)*a/255,g=(q.argb>>8&255)*a/255,b=(q.argb&255)*a/255,inv=255-a;
    for(int y=y0;y<y1;y++) {
        uint32_t* row=img.data()+size_t(y)*w;
        for(int x=x0;x<x1;x++) {
            const uint32_t d=row[x];
            row[x]=(a+((d>>24)*inv)/255)<<24|(r+((d>>16&255)*inv)/255)<<16|(g+((d>>8&255)*inv)/255)<<8|(b+((d&255)*inv)/255);
        }
    }
}
// The design's quads -> the window. The cached part is rasterised again only
// when the layout was rebuilt; the animated quads go over a copy each frame.
void present(const rsx_ui_quad* q,uint32_t count) {
    if(!InterlockedExchange(&started,1))CloseHandle(CreateThread(nullptr,0,thread,nullptr,0,nullptr));
    if(!hwnd||!count)return;
    if(cached_gen!=base_gen) {
        cached_gen=base_gen;
        float x0=1e9f,y0=1e9f,x1=0,y1=0;
        for(uint32_t i=0;i<count;i++) {x0=std::min(x0,q[i].x);y0=std::min(y0,q[i].y);x1=std::max(x1,q[i].x+q[i].w);y1=std::max(y1,q[i].y+q[i].h);}
        bx=std::floor(x0);by=std::floor(y0);bw=std::max(1,int(std::ceil(x1-bx)));bh=std::max(1,int(std::ceil(y1-by)));
        base.assign(size_t(bw)*bh,0x01000000u);   // alpha 1: invisible, but the whole rectangle takes the mouse
        for(size_t i=0;i<std::min<size_t>(base_count,count);i++)fill(base,bw,bh,q[i]);
    }
    frame=base;
    for(size_t i=base_count;i<count;i++)fill(frame,bw,bh,q[i]);
    AcquireSRWLockExclusive(&lock);shared.swap(frame);shared_w=bw;shared_h=bh;ReleaseSRWLockExclusive(&lock);
    PostMessageW(hwnd,WM_SRT_SHOW,0,0);
}
}
extern "C" void cv_srt_configure_env() {
    Config c;c.game_root=utf8(std::getenv("PS3_VFS_ROOT"));c.enabled=env_int("SRT",0)!=0;c.name=env_int("SRT_NAME",1)!=0;c.health=env_int("SRT_HEALTH",1)!=0;
    c.time=env_int("SRT_TIME",1)!=0;c.stats=env_int("SRT_STATS",1)!=0;c.inventory=env_int("SRT_INVENTORY",0)!=0;
    if(const char* where=std::getenv("SRT_INVENTORY_POS"))c.inventory_below=!strcmp(where,"below");
    c.enemies=std::clamp(env_int("SRT_ENEMIES",3),0,99);if(c.enemies>5&&c.enemies!=99)c.enemies=5;
    c.opacity=std::clamp(env_int("SRT_OPACITY",70),0,100);
    c.scale=std::clamp(env_int("SRT_SCALE",100),50,200);
    const char* corner=std::getenv("SRT_CORNER");if(corner)c.corner=!strcmp(corner,"tr")?1:!strcmp(corner,"bl")?2:!strcmp(corner,"br")?3:0;
    // In its own window the panel starts at the window's corner; the corner
    // choice only applies over the game.
    c.window=env_int("SRT_WINDOW",0)!=0;if(c.window)c.corner=0;
    c.lang=cv_srt_i18n::language(std::getenv("SRT_LANGUAGE"));c.words=cv_srt_i18n::labels(c.lang);
    c.credit=cv_srt_i18n::credit(c.lang);
    const auto credit=utf8(std::getenv("SRT_CREDIT"));if(!credit.empty())c.credit=credit;
    std::wstring words=utf8(std::getenv("SRT_TEXT"));size_t start=0;
    for(auto& word:c.words) {const auto end=words.find(L'|',start);if(start<words.size())word=words.substr(start,end==std::wstring::npos?end:end-start);if(end==std::wstring::npos)break;start=end+1;}
    AcquireSRWLockExclusive(&config_lock);config=std::move(c);generation++;ReleaseSRWLockExclusive(&config_lock);
}
extern "C" const rsx_ui_quad* cv_srt_design(const cv_srt_state* state,uint32_t width,uint32_t height,uint32_t* count) {
    *count=0;if(width<64||height<64)return nullptr;
    Config cfg;uint64_t gen;
    AcquireSRWLockShared(&config_lock);cfg=config;gen=generation;ReleaseSRWLockShared(&config_lock);
    if(!gen) {cv_srt_configure_env();return cv_srt_design(state,width,height,count);}
    if(!cfg.enabled||(!cfg.name&&!cfg.health&&!cfg.time&&!cfg.stats&&!cfg.enemies&&!cfg.inventory))return nullptr;
    static cv_srt_state previous{};static uint64_t built=0;static uint32_t w=0,h=0;
    if(built!=gen||w!=width||h!=height||std::memcmp(&previous,state,sizeof previous)) {
        build(*state,cfg,width,height);previous=*state;w=width;h=height;built=gen;
    }
    animate_pulse();*count=uint32_t(quads.size());return quads.data();
}
extern "C" const rsx_ui_quad* cv_srt_quads(uint32_t width,uint32_t height,uint32_t* count) {
    *count=0;
    AcquireSRWLockShared(&config_lock);const bool enabled=config.enabled;const bool ready=generation!=0;ReleaseSRWLockShared(&config_lock);
    if(!ready) {cv_srt_configure_env();return cv_srt_quads(width,height,count);}
    AcquireSRWLockShared(&config_lock);const bool own_window=config.window;ReleaseSRWLockShared(&config_lock);
    if(!enabled||!own_window)srt_window::hide();
    if(!enabled)return nullptr;
    static cv_srt_state state{};static ULONGLONG last=0;
    const ULONGLONG now=GetTickCount64();if(!last||now-last>=33) {cv_srt_sample(&state);last=now;}
    if(own_window) {
        // Laid out for the game window's size, so the panel keeps the size it
        // has over the game; nothing is drawn over the game itself.
        uint32_t n=0;const rsx_ui_quad* q=cv_srt_design(&state,width,height,&n);
        srt_window::present(q,n);return nullptr;
    }
    return cv_srt_design(&state,width,height,count);
}
#else
extern "C" void cv_srt_configure_env() {}
extern "C" const rsx_ui_quad* cv_srt_quads(uint32_t,uint32_t,uint32_t* n) {*n=0;return nullptr;}
extern "C" const rsx_ui_quad* cv_srt_design(const cv_srt_state*,uint32_t,uint32_t,uint32_t* n) {*n=0;return nullptr;}
#endif
