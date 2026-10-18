/* The F6 item menu: choose an item, its quantity and a space; the game frame
 * puts it in the live inventory (cv_items.cpp). Item art is read from the
 * selected game copy, as the SRT does; it is never bundled. */
#include "cv_item_menu.h"
#ifdef _WIN32
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <tuple>
#include <dwmapi.h>
#include "cv_items_i18n.h"
#include "cv_srt_i18n.h"
#include "cv_srt_icons.h"
#include "cv_launcher_controls.h"
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"dwmapi.lib")
using namespace Gdiplus;
extern "C" int g_pad_block;
extern "C" void* rsx_d3d12_window(void);
extern "C" void cv_focus_pause_hold(int on);                       // cv_focus_pause.cpp
extern "C" void perf_overlay_toast(const wchar_t* text,int on);    // the notice in the game's corner

namespace cv_item_menu {
namespace {
namespace tx=cv_items_i18n;
const Color C_BG(255,21,17,17),C_PANEL(255,30,24,24),C_FIELD(255,26,22,22),C_CELL(255,36,29,28),C_BORDER(255,70,60,58),
    C_LINE(255,70,28,28),C_TEXT(255,225,218,210),C_DIM(255,150,140,132),C_RED(255,196,28,34),C_RED_HI(255,230,50,50),
    C_OK(255,120,190,110),C_AMBER(255,226,172,82),C_SELECT(255,62,24,26);
const wchar_t* t(const View& v,tx::Id id) {return tx::k_text[std::clamp(v.language,0,12)][id];}
cv_srt_i18n::Language lang(const View& v) {return cv_srt_i18n::Language(std::clamp(v.language,0,12));}
std::wstring name(const View& v,int id) {return cv_srt_i18n::item(id,lang(v));}
std::wstring format(const wchar_t* pattern,int n,const std::wstring& s=L"") {
    wchar_t out[256];swprintf(out,256,pattern,n,s.c_str());return out;
}
const wchar_t* character_name(int id) {
    static const wchar_t* names[]={L"CLAIRE REDFIELD",L"CHRIS REDFIELD",L"STEVE BURNSIDE",L"ALBERT WESKER"};
    return id>=0&&id<4?names[id]:L"—";
}

// ---- Images ---------------------------------------------------------------
struct Images {
    cv_srt_icons::Library library;std::map<int,std::unique_ptr<Bitmap>> cache;std::wstring root;
    Bitmap* get(const std::wstring& game,int id) {
        if(root!=game) {root=game;cache.clear();}
        library.load(game);
        auto found=cache.find(id);if(found!=cache.end())return found->second.get();
        auto& slot=cache[id];
        const auto* icon=library.get(id);if(!icon)return nullptr;
        auto image=std::make_unique<Bitmap>(icon->width,icon->height,PixelFormat32bppARGB);
        Rect r(0,0,icon->width,icon->height);BitmapData d{};
        if(image->LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&d)!=Ok)return nullptr;
        for(int y=0;y<icon->height;y++)std::memcpy(static_cast<uint8_t*>(d.Scan0)+y*d.Stride,icon->argb.data()+size_t(y)*icon->width,size_t(icon->width)*4);
        image->UnlockBits(&d);slot=std::move(image);return slot.get();
    }
} images;
// The square portraits the SRT uses (launcher_art/srt_<name>.png, RCDATA).
std::map<int,std::unique_ptr<Bitmap>> portraits;
Bitmap* portrait(int character) {
    auto& cache=portraits;
    if(character<0||character>3)return nullptr;
    auto found=cache.find(character);if(found!=cache.end())return found->second.get();
    auto& image=cache[character];
    const wchar_t* names[]={L"SRT_CLAIRE",L"SRT_CHRIS",L"SRT_STEVE",L"SRT_WESKER"};
    const HMODULE module=GetModuleHandleW(nullptr);const HRSRC res=FindResourceW(module,names[character],MAKEINTRESOURCEW(10));
    if(!res)return nullptr;const DWORD size=SizeofResource(module,res);
    const void* data=LockResource(LoadResource(module,res));if(!data||!size||size>4*1024*1024)return nullptr;
    HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,size);if(!block)return nullptr;
    void* bytes=GlobalLock(block);if(!bytes){GlobalFree(block);return nullptr;}
    std::memcpy(bytes,data,size);GlobalUnlock(block);
    IStream* stream=nullptr;if(CreateStreamOnHGlobal(block,TRUE,&stream)!=S_OK){GlobalFree(block);return nullptr;}
    {Bitmap source(stream);if(source.GetLastStatus()==Ok&&source.GetWidth()&&source.GetWidth()<=1024)
        image.reset(source.Clone(0,0,int(source.GetWidth()),int(source.GetHeight()),PixelFormat32bppARGB));}
    stream->Release();return image.get();
}

// ---- Drawing helpers --------------------------------------------------------
struct Painter {
    Graphics& g;const View& v;
    GraphicsPath* round(RectF r,float radius) {
        auto* p=new GraphicsPath();const float d=radius*2;
        p->AddArc(r.X,r.Y,d,d,180,90);p->AddArc(r.X+r.Width-d,r.Y,d,d,270,90);
        p->AddArc(r.X+r.Width-d,r.Y+r.Height-d,d,d,0,90);p->AddArc(r.X,r.Y+r.Height-d,d,d,90,90);p->CloseFigure();return p;
    }
    void panel(RectF r,Color fill,Color border,float radius=6,float width=1) {
        std::unique_ptr<GraphicsPath> p(round(r,radius));SolidBrush b(fill);g.FillPath(&b,p.get());
        if(border.GetA()){Pen pen(border,width);g.DrawPath(&pen,p.get());}
    }
    void box(RectF r,Color c) {SolidBrush b(c);g.FillRectangle(&b,r);}
    void text(const std::wstring& s,RectF r,float size,Color c,bool bold=false,StringAlignment h=StringAlignmentNear,
              StringAlignment vert=StringAlignmentNear,bool wrap=false,bool serif=false) {
        const bool rtl=cv_srt_i18n::rtl_text(s);
        FontFamily family(serif?L"Georgia":cv_srt_i18n::font_face(lang(v),bold));
        Font font(&family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
        StringFormat f;f.SetAlignment(rtl&&h==StringAlignmentNear?StringAlignmentFar:h);f.SetLineAlignment(vert);
        f.SetTrimming(StringTrimmingEllipsisCharacter);
        INT flags=StringFormatFlagsLineLimit;if(!wrap)flags|=StringFormatFlagsNoWrap;if(rtl)flags|=StringFormatFlagsDirectionRightToLeft;
        f.SetFormatFlags(flags);
        SolidBrush b(c);g.DrawString(s.c_str(),-1,&font,r,&f,&b);
    }
    // An item's art, fitted inside `r` keeping its proportions; its name when
    // the art is missing (unidentified cells keep their name, as in the SRT).
    void icon(const std::wstring& game,int id,RectF r,float alpha=1) {
        Bitmap* image=images.get(game,id);
        if(!image){text(name(v,id),r,12,C_DIM,false,StringAlignmentCenter,StringAlignmentCenter,true);return;}
        const float iw=float(image->GetWidth()),ih=float(image->GetHeight());
        const float s=std::min(r.Width/iw,r.Height/ih),w=iw*s,h=ih*s;
        const RectF to(r.X+(r.Width-w)/2,r.Y+(r.Height-h)/2,w,h);
        ColorMatrix m={1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,alpha,0, 0,0,0,0,1};
        ImageAttributes a;a.SetColorMatrix(&m);
        g.DrawImage(image,to,0,0,iw,ih,UnitPixel,&a);
    }
    void hit(RectF r,int kind,int value) {const_cast<View&>(v).hits.push_back({r,kind,value});}
};
constexpr float LX=32,LW=660,RX=716,RW=372;
constexpr int COLS=4,ROWS=4;constexpr float CW=160,CH=100,GAP=6.67f,GRID_Y=146;
constexpr float SW=183,SH=56,SLOT_Y=188;
RectF slot_rect(int entry) {   // 1..10: two columns, as the game's own screen
    const int i=entry-1;return RectF(RX+(i%2)*(SW+6),SLOT_Y+(i/2)*(SH+6),SW,SH);
}
}  // namespace

void release() {images.cache.clear();portraits.clear();}
int View::item() const {
    const auto list=cv_items::items(cv_items::Category(category));
    return selected>=0&&selected<int(list.size())?list[size_t(selected)]:0;
}
std::vector<int> targets(const View& view) {
    std::vector<int> out;const auto& inv=view.inventory;
    if(inv.first_empty()>0)out.push_back(0);
    for(int i=1;i<=inv.capacity;i++)if(!inv.empty(i)&&!inv.is_equipped(i))out.push_back(i);
    return out;
}
namespace {
void fix_target(View& v) {
    const auto list=targets(v);
    if(list.empty()){v.target=0;return;}
    if(std::find(list.begin(),list.end(),v.target)==list.end())v.target=list.front();
}
void open_panel(View& v) {
    const auto* info=cv_items::info(v.item());if(!info)return;
    v.focus=1;v.quantity=info->quantity;v.row=info->counted?0:1;
    v.target=v.inventory.first_empty()>0?0:-1;fix_target(v);
}
}
void refresh(View& view,const cv_items::Inventory& live) {
    const bool changed=live.character!=view.inventory.character;
    view.inventory=live;cv_items::apply_pending(view.inventory);   // as it will be once the menu closes
    if(changed&&view.focus!=0)view.focus=0;
    fix_target(view);
}
namespace {
// Removing: the occupied entries, but not the equipped weapon.
bool removable(const cv_items::Inventory& inv,int e) {return e>=1&&e<=inv.capacity&&!inv.empty(e)&&!inv.is_equipped(e);}
void open_remove(View& v) {
    v.focus=2;v.status=t(v,tx::REMOVE_PICK);v.tone=NEUTRAL;
    if(removable(v.inventory,v.slot))return;
    for(int e=1;e<=v.inventory.capacity;e++)if(removable(v.inventory,e)){v.slot=e;return;}
    v.slot=1;
}
}
bool apply(View& v,Action a) {
    const auto list=cv_items::items(cv_items::Category(v.category));
    const int n=int(list.size());
    if(a==REMOVE) {if(v.focus==2)v.focus=0;else open_remove(v);return false;}
    if(v.focus==2) {   // a space of the inventory, two to a row
        const int cap=std::max(1,v.inventory.capacity);
        switch(a) {
        case LEFT:if(!(v.slot&1))v.slot--;break;
        case RIGHT:if((v.slot&1)&&v.slot<cap)v.slot++;break;
        case UP:if(v.slot>2)v.slot-=2;break;
        case DOWN:if(v.slot+2<=cap)v.slot+=2;break;
        case CONFIRM:submit(v);break;
        case BACK:v.focus=0;v.status.clear();break;
        default:break;
        }
        return false;
    }
    if(v.focus==0) {
        switch(a) {
        case LEFT:if(v.selected%COLS)v.selected--;break;
        case RIGHT:if(v.selected%COLS<COLS-1&&v.selected+1<n)v.selected++;break;
        case UP:if(v.selected>=COLS)v.selected-=COLS;break;
        case DOWN:if(v.selected+COLS<n)v.selected+=COLS;else if(v.selected/COLS<(n-1)/COLS)v.selected=n-1;break;
        case PREV:case NEXT:
            v.category=(v.category+(a==NEXT?1:cv_items::CATEGORY_COUNT-1))%cv_items::CATEGORY_COUNT;
            v.selected=0;v.scroll=0;break;
        case CONFIRM:open_panel(v);break;
        case BACK:return true;
        default:break;
        }
        const int row=v.selected/COLS;
        if(row<v.scroll)v.scroll=row;if(row>=v.scroll+ROWS)v.scroll=row-ROWS+1;
        return false;
    }
    const auto* info=cv_items::info(v.item());
    if(!info){v.focus=0;return false;}
    const int first_row=info->counted?0:1;
    switch(a) {
    case UP:v.row=std::max(first_row,v.row-1);break;
    case DOWN:v.row=std::min(2,v.row+1);break;
    case LEFT:case RIGHT:case PREV:case NEXT: {
        const int sign=(a==LEFT||a==PREV)?-1:1;
        if(v.row==0)v.quantity=cv_items::clamp_quantity(info->id,v.quantity+sign*((a==PREV||a==NEXT)?10:1));
        else if(v.row==1) {
            const auto list=targets(v);if(list.empty())break;
            auto at=std::find(list.begin(),list.end(),v.target);
            const int i=at==list.end()?0:int(at-list.begin());
            v.target=list[size_t((i+sign+int(list.size()))%int(list.size()))];
        }
        break;}
    case CONFIRM:submit(v);break;
    case BACK:v.focus=0;break;
    default:break;
    }
    return false;
}
cv_items::Result submit(View& v) {
    if(v.focus==2) {
        const auto& inv=v.inventory;
        if(!inv.valid||!inv.playing){v.status=t(v,tx::NOT_PLAYING);v.tone=BAD;return cv_items::R_NOT_PLAYING;}
        if(inv.is_equipped(v.slot)){v.status=t(v,tx::NO_REMOVE_EQUIPPED);v.tone=BAD;return cv_items::R_EQUIPPED;}
        if(!removable(inv,v.slot)){v.status=t(v,tx::REMOVE_PICK);v.tone=WARN;return cv_items::R_BAD;}
        const int gone=inv.id(v.slot);
        const auto r=cv_items::request_remove(inv.character,v.slot,inv.entry[v.slot]);
        if(r==cv_items::R_QUEUED) {
            const std::wstring said=format(t(v,tx::REMOVE_QUEUED_FMT),v.slot,name(v,gone));
            refresh(v,cv_items::read());   // the entries below move up at once
            if(!removable(v.inventory,v.slot))open_remove(v);   // the list's end: another item, if any
            v.status=said;v.tone=GOOD;
        } else if(r==cv_items::R_BUSY){v.status=t(v,tx::QUEUE_FULL);v.tone=WARN;}
        else {v.status=t(v,tx::NOT_PLAYING);v.tone=BAD;}
        return r;
    }
    const int id=v.item();const auto& inv=v.inventory;
    cv_items::Result why;
    const int entry=v.target<0?-1:cv_items::place(inv,id,v.target,why);
    if(entry<0) {
        if(v.target<0)why=cv_items::R_FULL;
        v.status=t(v,why==cv_items::R_FULL?tx::FULL:why==cv_items::R_EQUIPPED?tx::NO_EQUIPPED:tx::NOT_PLAYING);
        v.tone=BAD;return why;
    }
    const auto r=cv_items::request(inv.character,id,v.quantity,entry,inv.entry[entry]);
    if(r==cv_items::R_QUEUED) {
        v.status=format(t(v,tx::QUEUED_FMT),entry,name(v,id));v.tone=GOOD;v.focus=0;
        v.inventory.entry[entry]=cv_items::encode(id,v.quantity);fix_target(v);
    } else if(r==cv_items::R_BUSY){v.status=t(v,tx::QUEUE_FULL);v.tone=WARN;}
    else {v.status=t(v,tx::NOT_PLAYING);v.tone=BAD;}
    return r;
}
std::wstring summary(int language,const std::vector<cv_items::Outcome>& done,bool& ok) {
    View v;v.language=language;ok=true;int placed=0;const cv_items::Outcome* last=nullptr;
    for(const auto& o:done) {
        if(o.result==cv_items::R_DONE){placed++;last=&o;continue;}
        ok=false;
    }
    if(!ok) {
        for(const auto& o:done)if(o.result==cv_items::R_CHANGED||o.result==cv_items::R_EQUIPPED)return t(v,tx::CHANGED);
        return t(v,tx::NOT_PLAYING);
    }
    if(placed==1)return format(t(v,last->removed?tx::REMOVED_FMT:tx::DONE_FMT),last->entry,name(v,last->id));
    for(const auto& o:done)if(o.removed)return format(t(v,tx::CHANGES_N_FMT),placed);
    return format(t(v,tx::ADDED_N_FMT),placed);
}
void poll_result(View& v) {
    // Outcomes come while the game runs: the menu shows them if still open.
    const auto done=cv_items::outcomes(v.serial);
    if(done.empty())return;
    bool ok;v.status=summary(v.language,done,ok);v.tone=ok?GOOD:WARN;
}

void draw(Graphics& g,View& v,const std::wstring& game) {
    v.hits.clear();
    Painter p{g,v};
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHalf);
    p.box(RectF(0,0,W,H),C_BG);
    p.box(RectF(0,0,W,4),C_RED);
    const auto& inv=v.inventory;
    // Header: the title, and the character whose inventory this is.
    p.text(t(v,tx::TITLE),RectF(LX,18,420,40),30,C_RED_HI,true,StringAlignmentNear,StringAlignmentNear,false,v.language!=3&&v.language<10);
    p.text(std::wstring(t(v,tx::SUBTITLE))+L"  ·  "+t(v,tx::PAUSED),RectF(LX,54,520,20),14,C_DIM);
    if(inv.valid) {
        if(Bitmap* face=portrait(inv.character)) {
            RectF r(W-32-24-6-50,18,50,50);p.panel(r,C_FIELD,C_RED,4,1.5f);
            g.DrawImage(face,RectF(r.X+2,r.Y+2,r.Width-4,r.Height-4));
        }
        p.text(character_name(inv.character),RectF(W-32-88-300,20,300,24),18,C_TEXT,true,StringAlignmentFar,StringAlignmentNear,false,true);
        wchar_t spaces[64];swprintf(spaces,64,t(v,tx::SPACES_FMT),inv.used(),inv.capacity);
        p.text(spaces,RectF(W-32-88-300,46,300,20),13,C_DIM,false,StringAlignmentFar);
    } else p.text(t(v,tx::WAITING),RectF(W-32-400,30,400,24),15,C_DIM,false,StringAlignmentFar);
    {
        const RectF r(W-32-24,31,24,24);
        p.panel(r,C_SELECT,C_RED_HI,12,1.5f);
        p.text(L"×",r,19,C_TEXT,true,StringAlignmentCenter,StringAlignmentCenter);
        p.hit(r,HIT_CLOSE,0);
    }
    p.box(RectF(LX,80,W-2*LX,1),C_LINE);

    // Categories.
    const tx::Id cats[]={tx::WEAPONS,tx::AMMO,tx::HEALING,tx::KEY_ITEMS,tx::OTHER};
    const float tab=(LW-4*6)/5;
    for(int i=0;i<5;i++) {
        RectF r(LX+i*(tab+6),94,tab,38);const bool on=v.category==i;
        p.panel(r,on?C_SELECT:C_FIELD,on?C_RED:C_BORDER,5);
        if(on)p.box(RectF(r.X+8,r.Y+r.Height-3,r.Width-16,3),C_RED_HI);
        p.text(t(v,cats[i]),RectF(r.X+4,r.Y,r.Width-8,r.Height),14,on?C_TEXT:C_DIM,on,StringAlignmentCenter,StringAlignmentCenter);
        p.hit(r,HIT_TAB,i);
    }
    // The items of the category, four to a row; the rows scroll with the selection.
    const auto list=cv_items::items(cv_items::Category(v.category));
    const int rows=(int(list.size())+COLS-1)/COLS;
    for(int i=v.scroll*COLS;i<int(list.size())&&i<(v.scroll+ROWS)*COLS;i++) {
        const int c=i%COLS,r=i/COLS-v.scroll;const RectF cell(LX+c*(CW+GAP),GRID_Y+r*(CH+GAP),CW,CH);
        const bool on=i==v.selected;
        p.panel(cell,on?C_SELECT:C_CELL,on?(v.focus==0?C_RED_HI:C_RED):C_BORDER,6,on?2.f:1.f);
        p.icon(game,list[size_t(i)],RectF(cell.X+10,cell.Y+8,cell.Width-20,52));
        p.text(name(v,list[size_t(i)]),RectF(cell.X+6,cell.Y+62,cell.Width-12,34),12.5f,on?C_TEXT:C_DIM,false,
               StringAlignmentCenter,StringAlignmentNear,true);
        p.hit(cell,HIT_ITEM,i);
    }
    if(rows>ROWS) {   // where the visible rows are in the list
        const float top=GRID_Y,track=ROWS*(CH+GAP)-GAP,bar=track*ROWS/rows;
        p.box(RectF(LX+LW+4,top,4,track),C_FIELD);
        p.box(RectF(LX+LW+4,top+(track-bar)*v.scroll/float(rows-ROWS),4,bar),C_RED);
    }
    // The selected item, its warnings, and the last request's outcome.
    const int id=v.item();const auto* info=cv_items::info(id);
    float y=GRID_Y+ROWS*(CH+GAP)+6;
    if(info) {
        p.text(name(v,id),RectF(LX,y,LW,26),19,C_TEXT,true);y+=28;
        if(info->category==cv_items::KEY_ITEMS){p.text(L"⚠  "+std::wstring(t(v,tx::WARN_KEY)),RectF(LX,y,LW,20),13,C_AMBER);y+=21;}
        p.text(t(v,tx::NOTE_SAVE),RectF(LX,y,LW,20),13,C_DIM);y+=21;
    }
    const Color tones[]={C_DIM,C_OK,C_AMBER,C_RED_HI};
    if(!v.status.empty())p.text(v.status,RectF(LX,y,LW,20),13.5f,tones[v.tone],true);
    else if(inv.valid&&!inv.playing)p.text(t(v,tx::NOT_PLAYING),RectF(LX,y,LW,20),13.5f,C_AMBER,true);
    else if(inv.valid&&inv.first_empty()<0)p.text(t(v,tx::FULL),RectF(LX,y,LW,20),13.5f,C_AMBER,true);

    // The current inventory: the fixed slot, then the entries in two columns.
    p.text(t(v,tx::CURRENT),RectF(RX,94,RW,22),15,C_RED_HI,true,StringAlignmentNear,StringAlignmentNear,false,true);
    const bool panel_open=v.focus==1&&info;
    const int ghost=panel_open?(v.target>0?v.target:inv.first_empty()):-1;
    {
        RectF r(RX,122,RW,SH);p.panel(r,C_FIELD,C_BORDER,6);
        p.text(t(v,tx::FIXED),RectF(r.X+10,r.Y+4,120,16),11.5f,C_DIM);
        if(inv.valid&&!inv.empty(0)) {
            p.icon(game,inv.id(0),RectF(r.X+10,r.Y+18,60,34));
            p.text(name(v,inv.id(0)),RectF(r.X+78,r.Y,r.Width-88,r.Height),13,C_TEXT,false,StringAlignmentNear,StringAlignmentCenter);
        }
    }
    for(int e=1;e<=10;e++) {
        const RectF r=slot_rect(e);
        if(!inv.valid||e>inv.capacity) {   // the two spaces the Side Pack adds
            p.panel(r,Color(255,24,20,20),Color(255,48,42,40),6);
            HatchBrush hatch(HatchStyleWideUpwardDiagonal,Color(40,150,140,132),Color(0,0,0,0));g.FillRectangle(&hatch,r);
            if(inv.valid)p.text(t(v,tx::LOCKED),r,11.5f,Color(255,110,102,96),false,StringAlignmentCenter,StringAlignmentCenter);
            continue;
        }
        const bool target=e==ghost,equipped=inv.is_equipped(e),picked=v.focus==2&&e==v.slot;
        p.panel(r,target||picked?C_SELECT:C_CELL,target||picked?C_RED_HI:equipped?C_RED:C_BORDER,6,target||picked?2.f:1.f);
        p.text(std::to_wstring(e),RectF(r.X+6,r.Y+3,20,14),10.5f,C_DIM);
        if(target) {   // what will be there
            p.icon(game,id,RectF(r.X+8,r.Y+14,58,36),inv.empty(e)?0.55f:0.9f);
            p.text(name(v,id),RectF(r.X+70,r.Y+3,r.Width-76,r.Height-6),12,C_TEXT,true,StringAlignmentNear,StringAlignmentCenter,true);
            p.hit(r,HIT_SLOT,e);continue;
        }
        if(inv.empty(e)){p.text(t(v,tx::EMPTY),r,12,Color(255,96,88,84),false,StringAlignmentCenter,StringAlignmentCenter);p.hit(r,HIT_SLOT,e);continue;}
        p.icon(game,inv.id(e),RectF(r.X+8,r.Y+14,58,36));
        p.text(name(v,inv.id(e)),RectF(r.X+70,r.Y+3,r.Width-76,r.Height-20),12,C_TEXT,false,StringAlignmentNear,StringAlignmentCenter,true);
        const auto* held=cv_items::info(inv.id(e));
        if(held&&held->counted)p.text(std::to_wstring(inv.quantity(e))+(held->percent?L"%":L""),RectF(r.X+70,r.Y+r.Height-19,r.Width-78,16),12,C_AMBER,true,StringAlignmentFar);
        if(equipped)p.text(t(v,tx::EQUIPPED),RectF(r.X+70,r.Y+r.Height-19,r.Width-78,16),10.5f,C_RED_HI,true);
        if(picked)p.text(L"✕",RectF(r.X+r.Width-22,r.Y+2,18,18),13,C_RED_HI,true,StringAlignmentCenter);   // to be removed
        p.hit(r,HIT_SLOT,e);
    }
    // The placement: quantity, space and the button.
    const float py=SLOT_Y+5*(SH+6)+8;
    const bool removing=v.focus==2;   // quantity and space do not apply
    const bool counted=info&&info->counted&&!removing;
    auto row=[&](int index,float ry,const wchar_t* label,const std::wstring& value,bool enabled,int minus,int plus) {
        const bool on=panel_open&&v.row==index;RectF r(RX,ry,RW,36);
        p.panel(r,on?C_SELECT:C_FIELD,on?C_RED_HI:C_BORDER,5,on?1.5f:1.f);
        p.text(label,RectF(r.X+12,r.Y,130,r.Height),13,enabled?C_DIM:Color(255,90,84,80),false,StringAlignmentNear,StringAlignmentCenter);
        const Color arrow=enabled?(on?C_RED_HI:C_TEXT):Color(255,80,74,70);
        p.text(L"◀",RectF(r.X+140,r.Y,24,r.Height),13,arrow,false,StringAlignmentCenter,StringAlignmentCenter);
        p.text(value,RectF(r.X+164,r.Y,r.Width-164-34,r.Height),14,enabled?C_TEXT:Color(255,100,92,88),true,StringAlignmentCenter,StringAlignmentCenter);
        p.text(L"▶",RectF(r.X+r.Width-32,r.Y,24,r.Height),13,arrow,false,StringAlignmentCenter,StringAlignmentCenter);
        if(enabled){p.hit(RectF(r.X+130,r.Y,40,r.Height),minus,0);p.hit(RectF(r.X+r.Width-44,r.Y,44,r.Height),plus,0);}
    };
    row(0,py,t(v,tx::QUANTITY),counted?std::to_wstring(v.quantity)+(info->percent?L"%":L""):L"—",counted,HIT_QTY_MINUS,HIT_QTY_PLUS);
    // Closed, the panel shows where the item would go by default.
    const int shown=panel_open?v.target:inv.first_empty()>0?0:targets(v).empty()?-1:targets(v).front();
    std::wstring where=removing?L"—":shown==0?t(v,tx::FIRST_FREE):shown>0?format(t(v,tx::REPLACE_FMT),shown):L"—";
    row(1,py+42,t(v,tx::SLOT),where,info&&!removing&&!targets(v).empty(),HIT_TARGET_PREV,HIT_TARGET_NEXT);
    {
        // Removing turns the button into the removal's, on the chosen space.
        const bool ready=removing?inv.valid&&inv.playing&&removable(inv,v.slot):info&&inv.valid&&inv.playing&&v.target>=0&&!targets(v).empty();
        const bool on=removing||(panel_open&&v.row==2);RectF r(RX,py+86,RW,40);
        const Color fill=removing?Color(255,120,20,24):on?C_RED_HI:C_RED;
        p.panel(r,ready?fill:Color(255,70,40,40),on&&ready?Color(255,255,200,200):Color(0,0,0,0),5,1.5f);
        p.text(t(v,removing?tx::REMOVE:tx::PLACE),r,14.5f,ready?Color(255,248,242,236):Color(255,150,120,120),true,StringAlignmentCenter,StringAlignmentCenter);
        if(ready)p.hit(r,HIT_PLACE,0);
    }
    // The controls.
    p.box(RectF(LX,H-46,W-2*LX,1),C_LINE);
    p.text(t(v,tx::HELP_KEYS),RectF(LX,H-40,W-2*LX,17),12.5f,C_DIM);
    p.text(t(v,tx::HELP_PAD),RectF(LX,H-22,W-2*LX,17),12.5f,C_DIM);
}
}  // namespace cv_item_menu

// ---- The window ---------------------------------------------------------------
namespace {
using namespace cv_item_menu;
HWND window=nullptr;View view;std::wstring game_root;float scale=1;ULONGLONG closed_at=0;
uint32_t previous_pad[PAD_PHYS_MAX]={};ULONGLONG repeat_at[PAD_PHYS_MAX]={};
std::atomic<bool> allowed{false};
void paint(HWND h) {
    PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT c;GetClientRect(h,&c);
    const int w=std::max<int>(1,c.right),hh=std::max<int>(1,c.bottom);
    Bitmap back(w,hh,PixelFormat32bppARGB);
    {Graphics g(&back);g.ScaleTransform(scale,scale);draw(g,view,game_root);}
    Graphics out(dc);out.DrawImage(&back,0,0);
    EndPaint(h,&ps);
}
void redraw() {if(window)InvalidateRect(window,nullptr,FALSE);}
void act(Action a) {if(apply(view,a))cv_item_menu_close();else redraw();}
void click(int x,int y) {
    const float px=x/scale,py=y/scale;
    for(const auto& hit:view.hits) {
        if(!hit.rect.Contains(px,py))continue;
        switch(hit.kind) {
        case HIT_CLOSE:cv_item_menu_close();return;
        case HIT_TAB:view.category=hit.value;view.selected=0;view.scroll=0;view.focus=0;break;
        case HIT_ITEM:if(view.selected==hit.value&&view.focus==0)apply(view,CONFIRM);else{view.selected=hit.value;view.focus=0;}break;
        case HIT_SLOT:if(view.focus==1){const auto l=targets(view);if(std::find(l.begin(),l.end(),hit.value)!=l.end())view.target=hit.value;
                      else if(view.inventory.empty(hit.value)&&view.inventory.first_empty()>0)view.target=0;view.row=1;}
                      else if(!view.inventory.empty(hit.value)){if(view.focus!=2)apply(view,REMOVE);view.slot=hit.value;}   // the item to take out
                      break;
        case HIT_QTY_MINUS:case HIT_QTY_PLUS:case HIT_TARGET_PREV:case HIT_TARGET_NEXT:
            if(view.focus==0)apply(view,CONFIRM);
            view.row=hit.kind<=HIT_QTY_PLUS?0:1;apply(view,(hit.kind==HIT_QTY_MINUS||hit.kind==HIT_TARGET_PREV)?LEFT:RIGHT);break;
        case HIT_PLACE:if(view.focus==0)apply(view,CONFIRM);submit(view);break;
        default:break;
        }
        redraw();return;
    }
}
void place_window() {
    HWND game=static_cast<HWND>(rsx_d3d12_window());RECT area{};
    if(game&&GetClientRect(game,&area)&&area.right>0){POINT o{0,0};ClientToScreen(game,&o);OffsetRect(&area,o.x,o.y);}
    else SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
    const float aw=float(area.right-area.left),ah=float(area.bottom-area.top);
    scale=std::clamp(std::min(aw*0.94f/W,ah*0.94f/H),0.55f,2.5f);
    const int w=int(std::lround(W*scale)),h=int(std::lround(H*scale));
    SetWindowPos(window,HWND_TOPMOST,area.left+int(aw-w)/2,area.top+int(ah-h)/2,w,h,SWP_NOACTIVATE);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint(h);return 0;
    case WM_KEYDOWN:
        switch(wp) {
        case VK_UP:act(UP);break;case VK_DOWN:act(DOWN);break;case VK_LEFT:act(LEFT);break;case VK_RIGHT:act(RIGHT);break;
        case 'Q':case VK_PRIOR:act(PREV);break;case 'E':case VK_NEXT:case VK_TAB:act(NEXT);break;
        case VK_RETURN:case VK_SPACE:act(CONFIRM);break;case VK_ESCAPE:case VK_BACK:act(BACK);break;
        case 'R':case VK_DELETE:act(REMOVE);break;
        case VK_F6:cv_item_menu_close();break;
        }
        return 0;
    case WM_LBUTTONDOWN:click(short(LOWORD(lp)),short(HIWORD(lp)));return 0;
    case WM_MOUSEWHEEL:act(GET_WHEEL_DELTA_WPARAM(wp)>0?UP:DOWN);return 0;
    case WM_TIMER: {
        if(!IsWindowVisible(h)) {
            // Closed with items queued: the game, running again, puts them in
            // on its next frame; say so in its corner, then stop looking.
            const auto done=cv_items::outcomes(view.serial);
            if(!done.empty()){bool ok;perf_overlay_toast(summary(view.language,done,ok).c_str(),ok);}
            if(!cv_items::pending()||GetTickCount64()>=closed_at+5000)KillTimer(h,1);
            return 0;
        }
        const auto before=view.inventory;refresh(view,cv_items::read());
        const unsigned serial=view.serial;poll_result(view);
        bool dirty=serial!=view.serial||std::memcmp(&before,&view.inventory,sizeof before)!=0;
        if(GetForegroundWindow()==h) for(int i=0;i<PAD_PHYS_MAX;i++) {
            PadPhys pad{};if(!pad_phys_poll(i,&pad)){previous_pad[i]=0;continue;}
            const uint32_t edge=pad.buttons&~previous_pad[i];previous_pad[i]=pad.buttons;
            const auto pressed=[&](int b){return (edge>>b)&1u;};
            // The shortcut chords (R1+R2+L1+L2 + a button) belong to the
            // launcher: nothing here while both triggers are held.
            if(cv_launcher_controls::chord_held(pad.buttons))continue;
            if(pressed(PB_EAST)){act(BACK);return 0;}
            if(pressed(PB_SOUTH))act(CONFIRM);
            if(pressed(PB_WEST))act(REMOVE);
            if(pressed(PB_L1))act(PREV);if(pressed(PB_R1))act(NEXT);
            const int dirs[]={PB_UP,PB_DOWN,PB_LEFT,PB_RIGHT};const Action as[]={UP,DOWN,LEFT,RIGHT};
            for(int d=0;d<4;d++) if((pad.buttons>>dirs[d])&1u) {
                if(((edge>>dirs[d])&1u)||GetTickCount64()>=repeat_at[i]){act(as[d]);repeat_at[i]=GetTickCount64()+((edge>>dirs[d])&1u?350:110);}
                break;
            }
        }
        if(dirty)redraw();
        return 0;}
    case WM_ACTIVATE:g_pad_block=LOWORD(wp)!=WA_INACTIVE;return 0;   // the game ignores the pad under the menu
    case WM_CLOSE:cv_item_menu_close();return 0;
    case WM_DESTROY:window=nullptr;return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
}  // namespace

void cv_item_menu_enable(bool on) {
    allowed=on;cv_items::set_enabled(on);
    if(!on)cv_item_menu_close();
}
bool cv_item_menu_visible() {return window&&IsWindowVisible(window);}
void cv_item_menu_close() {
    if(!cv_item_menu_visible())return;
    ShowWindow(window,SW_HIDE);g_pad_block=0;closed_at=GetTickCount64();
    if(!cv_items::pending())KillTimer(window,1);   // else it stays to report them
    cv_focus_pause_hold(0);   // the game runs again; its next frame takes the queue
    if(auto game=static_cast<HWND>(rsx_d3d12_window()))SetForegroundWindow(game);
}
void cv_item_menu_toggle(const std::wstring& game,int language) {
    if(!allowed)return;
    if(cv_item_menu_visible()){cv_item_menu_close();return;}
    if(!window) {
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        wc.lpszClassName=L"CVItemMenu";wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
        // No owner: an owned window would bring back the minimized launcher.
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"CVItemMenu",L"Code: Veronica",WS_POPUP,
                               0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)return;
    }
    game_root=game;
    view.language=std::clamp(language,0,12);view.focus=0;view.status.clear();view.tone=NEUTRAL;
    view.serial=cv_items::outcome_serial();
    cv_focus_pause_hold(1);   // the game stands still while choosing
    refresh(view,cv_items::read());
    place_window();
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);g_pad_block=1;
    for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys pad{};pad_phys_poll(i,&pad);previous_pad[i]=pad.buttons;}
    SetTimer(window,1,50,nullptr);redraw();
}
#endif
