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
    bool enabled=false,name=true,health=true,time=true,stats=true,inventory=false;
    cv_srt_i18n::Language lang=cv_srt_i18n::EN;
    int enemies=3,scale=100,corner=0,opacity=70;
    std::wstring credit=L"by Kastiel",game_root;
    std::array<std::wstring,15> words={L"Health",L"Game time",L"Saves",L"Retries",L"Enemies",L"Inventory",L"Equipped",L"Fine",L"Caution",L"Danger",L"Poison",L"Gas",L"Dead",L"Waiting for a game",L"Empty"};
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
std::vector<Tile> tiles(const cv_srt_state& state) {
    std::vector<Tile> out;int last=0;for(int i=0;i<CV_SRT_INVENTORY_SIZE;i++)if(state.inventory[i].id)last=i;
    int slot=0;
    for(int i=0;i<CV_SRT_INVENTORY_SIZE && slot<12;i++) {
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
Bitmap* portrait(int character) {
    static bool attempted=false;static std::array<std::unique_ptr<Bitmap>,4> images;
    if(!attempted) {
        attempted=true;const wchar_t* names[]={L"SRT_CLAIRE",L"SRT_CHRIS",L"SRT_STEVE",L"SRT_WESKER"};
        for(int i=0;i<4;i++) {
            const HMODULE module=GetModuleHandleW(nullptr);const HRSRC resource=FindResourceW(module,names[i],MAKEINTRESOURCEW(10));
            if(!resource)continue;const DWORD size=SizeofResource(module,resource);
            const void* data=LockResource(LoadResource(module,resource));if(!data||!size||size>1024*1024)continue;
            HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,size);if(!block)continue;
            void* bytes=GlobalLock(block);if(!bytes) {GlobalFree(block);continue;}
            std::memcpy(bytes,data,size);GlobalUnlock(block);
            IStream* stream=nullptr;if(CreateStreamOnHGlobal(block,TRUE,&stream)!=S_OK) {GlobalFree(block);continue;}
            {
                Bitmap source(stream);
                if(source.GetLastStatus()==Ok&&source.GetWidth()==96&&source.GetHeight()==96)
                    images[i].reset(source.Clone(0,0,96,96,PixelFormat32bppARGB));
            }
            stream->Release();
        }
    }
    return character>=0&&character<4?images[character].get():nullptr;
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
        box(x-1,y-1,26,26,Color(200,0,0,0));
        const GraphicsState saved=g.Save();g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(image,RectF(x,y,24,24),0,0,96,96,UnitPixel);g.Restore(saved);
        return true;
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
size_t base_count=0;
struct PulseArea {float x=0,y=0,w=0,scale=1;uint32_t color=0;bool dead=false;} pulse;
void animate_pulse() {
    quads.resize(base_count);if(pulse.w<=0)return;
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
    static ULONG_PTR token=0;if(!token) {GdiplusStartupInput input;GdiplusStartup(&token,&input,nullptr);}
    Bitmap measure_bitmap(1,1,PixelFormat32bppARGB);Graphics measure_graphics(&measure_bitmap);
    Font credit_font(L"Segoe UI",12,FontStyleRegular,UnitPixel);RectF credit_bounds;
    measure_graphics.MeasureString(cfg.credit.c_str(),-1,&credit_font,PointF(0,0),&credit_bounds);
    const bool credit_wrap=credit_bounds.Width>104;
    const auto grid=tiles(st);int gridrows=0;for(const auto& t:grid)gridrows=std::max(gridrows,t.slot/2+1);
    const int count=cfg.enemies==99?st.enemy_count:std::min(st.enemy_count,cfg.enemies);
    int h=16+(cfg.name?28:0)+(cfg.health?104:0)+(cfg.time?40:0)+(cfg.stats?45:0);
    if(cfg.enemies)h+=28+std::max(1,count)*39;
    h+=credit_wrap?42:26;
    const bool side=st.valid&&cfg.inventory;
    if(side)item_images.load(cfg.game_root);
    const bool right=cfg.corner==1||cfg.corner==3;
    const float main_x=side&&right?316.f:0.f,inventory_x=right?0.f:316.f;
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
    if(side) {
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
    const int inventory_height=int(std::ceil(12+30+equipped_height+8+grid_height+13));
    const int logical_width=!st.valid?260:side?620:304;
    if(!st.valid)h=44;else if(side)h=std::max(h,inventory_height);
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
                const auto& e=st.enemies[i];c.text(cv_srt_i18n::enemy(e.type,cfg.lang),18,y,151,14,c.cream,false,false,false,22);
                const std::wstring hp=number(e.hp)+L" / "+number(e.maximum);
                c.text(hp,171,y,115,14,Color(255,222,116,107),false,true,false,22);
                c.bar(18,y+22,268,11,e.maximum?float(e.hp)/e.maximum:0,Color(255,163,51,44));y+=39;
            }
        }
        if(cfg.inventory) {
            y=12;c.g.TranslateTransform(inventory_x-main_x,0);
            c.heading(cfg.words[5],y);y+=30;
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
            c.g.TranslateTransform(main_x-inventory_x,0);
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
    base_count=quads.size();
    pulse={};
    if(ecg_y>=0) {
        pulse={std::round(x0+(main_x+18)*scale),std::round(y0+ecg_y*scale),std::floor(166*scale),scale,ecg_color,st.condition==SRT_DEAD};
        quads.reserve(base_count+size_t(pulse.w)*6+1);
    }
}
}
extern "C" void cv_srt_configure_env() {
    Config c;c.game_root=utf8(std::getenv("PS3_VFS_ROOT"));c.enabled=env_int("SRT",0)!=0;c.name=env_int("SRT_NAME",1)!=0;c.health=env_int("SRT_HEALTH",1)!=0;
    c.time=env_int("SRT_TIME",1)!=0;c.stats=env_int("SRT_STATS",1)!=0;c.inventory=env_int("SRT_INVENTORY",0)!=0;
    c.enemies=std::clamp(env_int("SRT_ENEMIES",3),0,99);if(c.enemies>5&&c.enemies!=99)c.enemies=5;
    c.opacity=std::clamp(env_int("SRT_OPACITY",70),0,100);
    c.scale=std::clamp(env_int("SRT_SCALE",100),50,200);
    const char* corner=std::getenv("SRT_CORNER");if(corner)c.corner=!strcmp(corner,"tr")?1:!strcmp(corner,"bl")?2:!strcmp(corner,"br")?3:0;
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
    if(!enabled)return nullptr;
    static cv_srt_state state{};static ULONGLONG last=0;
    const ULONGLONG now=GetTickCount64();if(!last||now-last>=33) {cv_srt_sample(&state);last=now;}
    return cv_srt_design(&state,width,height,count);
}
#else
extern "C" void cv_srt_configure_env() {}
extern "C" const rsx_ui_quad* cv_srt_quads(uint32_t,uint32_t,uint32_t* n) {*n=0;return nullptr;}
extern "C" const rsx_ui_quad* cv_srt_design(const cv_srt_state*,uint32_t,uint32_t,uint32_t* n) {*n=0;return nullptr;}
#endif
