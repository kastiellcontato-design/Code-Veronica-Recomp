// Optional host-side test window. Guest calls are consumed only by the game dispatcher.
#include "cv_test_tools.h"
#include "cv_version.h"
#include "ppu_recomp.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>
#include <set>
#include <cstdio>
#include <cwctype>
#include "cv_launcher_controls.h"
#include "cv_test_tools_i18n.h"
#ifdef _WIN32
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib,"uxtheme.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"dwmapi.lib")
#endif
#ifdef _WIN32
extern "C" uint8_t* vm_base;
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" int g_pad_block;
extern "C" void* rsx_d3d12_window(void);
#if defined(CV_REGION_JP)
#define TEST_FRAME func_00032F80
#define ORIGINAL_FRAME cv_original_00032F80
#define TEST_WARP func_0018ED00
#define TEST_SERIALIZE func_001D76D0
#define ORIGINAL_SERIALIZE cv_original_001D76D0
#define SAVE_OBJECT 0u   /* not mapped for this version yet */
#define SAVE_MANAGER 0u
#elif defined(CV_REGION_US)
#define TEST_FRAME func_00032FA0
#define ORIGINAL_FRAME cv_original_00032FA0
#define TEST_WARP func_0018ED68
#define TEST_SERIALIZE func_001D7550
#define ORIGINAL_SERIALIZE cv_original_001D7550
#define SAVE_OBJECT 0u   /* not mapped for this version yet */
#define SAVE_MANAGER 0u
#else
#define TEST_FRAME func_000333A0
#define ORIGINAL_FRAME cv_original_000333A0
#define TEST_WARP func_00190700
#define TEST_SERIALIZE func_001D9370
#define ORIGINAL_SERIALIZE cv_original_001D9370
#define SAVE_OBJECT 0x00ABF0D4u    /* the save screen's object (func_001D952C) */
#define SAVE_MANAGER 0x00AF97C8u   /* the savedata thread's manager */
#endif
void ORIGINAL_FRAME(ppu_context*);
void ORIGINAL_SERIALIZE(ppu_context*);
namespace {
namespace fs=std::filesystem;
constexpr uint32_t state_base=CV_SRT_CHARACTER-0x94;
std::atomic<bool> enabled{false},ready{false};
// Zero means no request; bit 31 is set on every published request, including rm_0000.
std::atomic<uint32_t> request{0};
std::atomic<int> result{0}; // 0 idle, 1 queued, 2 native request issued, 3 busy, 4 wrong state, 5 timeout
std::atomic<uint32_t> room_now{0};
// "Save here": the game's own save screen, opened as the typewriter opens it.
std::atomic<bool> save_request{false};
bool tool_save=false;   // game thread: the open save screen came from this window
HWND window=nullptr,filter=nullptr,list=nullptr,entry=nullptr,travel=nullptr,save_button=nullptr;
std::vector<cv_test::Room> rooms;
std::vector<size_t> visible_rooms;
std::wstring game_root;
uint32_t previous_pad[PAD_PHYS_MAX]={};
ULONGLONG repeat_at[PAD_PHYS_MAX]={};
int language=1;
const wchar_t* tx(cv_test_i18n::Id id) {return cv_test_i18n::k_text[language][id];}
bool mapped(uint32_t address,size_t size) {
    if(!vm_base||size>UINT32_MAX-address)return false;
    uintptr_t at=reinterpret_cast<uintptr_t>(vm_base)+address,end=at+size;
    while(at<end) {MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        uintptr_t next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=at)return false;at=next;}
    return true;
}
bool active_game() {
    if(!mapped(state_base,0x2ACA4)||!mapped(CV_SRT_HEALTH,4)||!mapped(CV_SRT_RDX,4))return false;
    // The room header identifies a live character; stale title/game-over data is insufficient.
    static constexpr uint32_t rdx[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};
    unsigned character=vm_read8(CV_SRT_CHARACTER);
    if(character>=4||vm_read32(CV_SRT_RDX)!=rdx[character]||int32_t(vm_read32(CV_SRT_HEALTH))<=0)return false;   // 32-bit, as the SRT reads it
    uint32_t tasks=vm_read32(state_base+0x64),blocked=vm_read32(state_base+0x68);
    return ((tasks&~blocked)&0x80)&&!((tasks&~blocked)&0x1803F)&&!(vm_read32(state_base+0x74)&7)&&!(vm_read32(state_base+0x70)&0x1C040008);
}
void call_warp(ppu_context* caller,uint32_t packed) {
    ppu_context work=*caller;
    const auto old_active=g_active_ctx;const auto old_tail=g_trampoline_fn;
    work.gpr[1]-=0x400;vm_write64(work.gpr[1],caller->gpr[1]);work.gpr[2]=CV_TOC;
    work.gpr[3]=0;work.gpr[4]=(packed>>24)&15;work.gpr[5]=(packed>>16)&255;
    work.gpr[6]=packed&255;work.gpr[7]=255; // no door animation
    g_active_ctx=&work;g_trampoline_fn=nullptr;
    vm_write8(state_base+0x97,(packed>>8)&15);
    TEST_WARP(&work);
    while(g_trampoline_fn) {auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&work);}
    g_active_ctx=old_active;g_trampoline_fn=old_tail;
    std::printf("[CVTest] warp requested: rm_%u%02u%u entry=%u, on game thread\n",(packed>>24)&15,(packed>>16)&255,(packed>>8)&15,packed&255);std::fflush(stdout);
}
std::wstring lower(std::wstring s) {for(auto& c:s)c=wchar_t(towlower(c));return s;}
void refresh_list() {
    wchar_t value[128]={};GetWindowTextW(filter,value,128);std::wstring query=lower(value);
    SendMessageW(list,WM_SETREDRAW,FALSE,0);SendMessageW(list,LB_RESETCONTENT,0,0);visible_rooms.clear();
    for(size_t i=0;i<rooms.size();i++) {auto label=rooms[i].code();
        if(!query.empty()&&lower(label).find(query)==std::wstring::npos)continue;
        SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));visible_rooms.push_back(i);}
    if(!visible_rooms.empty())SendMessageW(list,LB_SETCURSEL,0,0);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
}
void catalog(const std::wstring& game) {
    rooms.clear();std::set<unsigned> seen;std::error_code ec;
    fs::path root=fs::path(game)/L"USRDIR"/L"BHCV"/L"nativePS3"/L"biocv_disc";
    // Prefer English, then add any region-specific rooms absent there. No recursive scan.
    std::vector<fs::path> dirs{root/L"eng"/L"rdx_lnk"};
    for(const auto& d:fs::directory_iterator(root,ec))if(d.is_directory(ec)&&d.path().filename()!=L"eng")dirs.push_back(d.path()/L"rdx_lnk");
    for(const auto& dir:dirs) {ec.clear();for(const auto& item:fs::directory_iterator(dir,ec)) {
        cv_test::Room room;if(!item.is_regular_file(ec)||!cv_test::parse_room(item.path().filename().wstring(),room)||!seen.insert(room.key()).second)continue;
        rooms.push_back(room);}}
    std::sort(rooms.begin(),rooms.end(),[](auto& a,auto& b){return a.key()<b.key();});
    std::printf("[CVTest] map catalog: %zu rooms, %s\n",rooms.size(),CV_TITLE_ID);
}
// ---- The window, in the launcher's colours -------------------------------
// Everything but the two text fields and the list is painted here; the list
// and the buttons are owner-drawn.
constexpr int W=680,H=640,MARGIN=24;
constexpr COLORREF C_BG=RGB(21,17,17),C_FIELD=RGB(26,22,22),C_FIELD_BD=RGB(70,60,58),C_LINE=RGB(70,28,28),
    C_TEXT=RGB(225,218,210),C_DIM=RGB(150,140,132),C_RED=RGB(196,28,34),C_RED_HI=RGB(230,50,50),C_OK=RGB(120,190,110),C_HOVER=RGB(38,30,30);
enum {ID_FILTER=10,ID_ENTRY,ID_LIST,ID_LOAD,ID_BACK,ID_SAVE};
const RECT filter_box{MARGIN,122,MARGIN+400,154},entry_box{MARGIN+424,122,W-MARGIN,154};
const RECT list_box{MARGIN,234,W-MARGIN,488},status_box{MARGIN,546,W-MARGIN,570};
HFONT title_font=nullptr,body_font=nullptr,small_font=nullptr,code_font=nullptr;
HBRUSH field_brush=nullptr;
std::wstring status_text;COLORREF status_color=C_DIM;
int hover_button=0;
HFONT font(int px,int weight,const wchar_t* face) {
    return CreateFontW(-px,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
}
void fill(HDC dc,RECT r,COLORREF c) {HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void frame(HDC dc,RECT r,COLORREF c) {HBRUSH b=CreateSolidBrush(c);FrameRect(dc,&r,b);DeleteObject(b);}
void write(HDC dc,const std::wstring& s,RECT r,HFONT f,COLORREF c,UINT format=DT_LEFT|DT_TOP|DT_WORDBREAK) {
    SelectObject(dc,f);SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,s.c_str(),-1,&r,format|DT_NOPREFIX);
}
void set_status(const std::wstring& s,COLORREF c) {
    if(s==status_text&&c==status_color)return;
    status_text=s;status_color=c;if(window)InvalidateRect(window,&status_box,FALSE);
}
void send_selected() {
    if(!enabled||!ready||request.load())return;
    auto i=SendMessageW(list,LB_GETCURSEL,0,0);if(i==LB_ERR||size_t(i)>=visible_rooms.size())return;
    wchar_t v[12];GetWindowTextW(entry,v,12);wchar_t* end=nullptr;long spawn=wcstol(v,&end,10);
    if(!*v||*end||spawn<0||spawn>255) {set_status(tx(cv_test_i18n::BAD_ENTRY),C_RED_HI);return;}
    const auto& room=rooms[visible_rooms[size_t(i)]];
    uint32_t packed=0x80000000u|(room.stage<<24)|(room.number<<16)|(room.variant<<8)|unsigned(spawn);
    uint32_t empty=0;result=1;if(!request.compare_exchange_strong(empty,packed)){result=3;return;}
    ShowWindow(window,SW_HIDE);g_pad_block=0;auto game=static_cast<HWND>(rsx_d3d12_window());if(game)SetForegroundWindow(game);
}
void send_save() {
    if(!SAVE_OBJECT||!enabled||!ready||request.load()||save_request.load())return;
    result=1;save_request=true;
    ShowWindow(window,SW_HIDE);g_pad_block=0;auto game=static_cast<HWND>(rsx_d3d12_window());if(game)SetForegroundWindow(game);
}
HWND child(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,RECT r,int id,HFONT f) {
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(f),TRUE);return c;
}
// A text field sits inside a painted frame, vertically centred.
RECT inside(RECT r) {return RECT{r.left+10,r.top+6,r.right-10,r.bottom-5};}
void paint(HWND h) {
    PAINTSTRUCT ps;HDC screen=BeginPaint(h,&ps);
    RECT all{0,0,W,H};HDC dc=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,W,H);HGDIOBJ old=SelectObject(dc,bmp);
    fill(dc,all,C_BG);
    fill(dc,RECT{0,0,W,4},C_RED);
    write(dc,tx(cv_test_i18n::TITLE),RECT{MARGIN,20,W-MARGIN,56},title_font,C_RED_HI,DT_LEFT|DT_SINGLELINE);
    write(dc,tx(cv_test_i18n::SUBTITLE),RECT{MARGIN+138,34,W-MARGIN,56},small_font,C_DIM,DT_LEFT|DT_SINGLELINE);
    fill(dc,RECT{MARGIN,66,W-MARGIN,67},C_LINE);
    write(dc,tx(cv_test_i18n::FILTER),RECT{MARGIN,94,MARGIN+400,118},body_font,C_TEXT,DT_LEFT|DT_SINGLELINE);
    write(dc,tx(cv_test_i18n::ENTRY),RECT{entry_box.left,94,W-MARGIN,118},body_font,C_TEXT,DT_LEFT|DT_SINGLELINE);
    for(RECT r:{filter_box,entry_box}) {fill(dc,r,C_FIELD);frame(dc,r,GetFocus()==(r.left==filter_box.left?filter:entry)?C_RED:C_FIELD_BD);}
    write(dc,tx(cv_test_i18n::ENTRY_HELP),
          RECT{MARGIN,164,W-MARGIN,222},small_font,C_DIM);
    frame(dc,RECT{list_box.left-1,list_box.top-1,list_box.right+1,list_box.bottom+1},C_FIELD_BD);
    write(dc,tx(cv_test_i18n::NOTE),
          RECT{MARGIN,498,W-MARGIN,542},small_font,C_DIM);
    write(dc,status_text,status_box,body_font,status_color,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
    BitBlt(screen,0,0,W,H,dc,0,0,SRCCOPY);
    SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);EndPaint(h,&ps);
}
void draw_button(const DRAWITEMSTRUCT* d) {
    const bool primary=d->CtlID!=ID_BACK,disabled=(d->itemState&ODS_DISABLED)!=0,hot=hover_button==int(d->CtlID)&&!disabled;
    RECT r=d->rcItem;
    COLORREF bg=primary?(disabled?RGB(70,40,40):hot?C_RED_HI:C_RED):(hot?C_HOVER:C_BG);
    fill(d->hDC,r,bg);if(!primary)frame(d->hDC,r,C_FIELD_BD);
    if(d->itemState&ODS_FOCUS) {RECT f{r.left+3,r.top+3,r.right-3,r.bottom-3};frame(d->hDC,f,primary?RGB(255,200,200):C_RED);}
    write(d->hDC,tx(d->CtlID==ID_LOAD?cv_test_i18n::LOAD:d->CtlID==ID_SAVE?cv_test_i18n::SAVE:cv_test_i18n::BACK),r,body_font,
          disabled?RGB(150,120,120):RGB(245,240,235),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
void draw_room(const DRAWITEMSTRUCT* d) {
    if(d->itemID==UINT(-1))return;
    RECT r=d->rcItem;const bool selected=(d->itemState&ODS_SELECTED)!=0;
    fill(d->hDC,r,selected?RGB(70,24,26):(d->itemID&1)?RGB(24,20,20):C_FIELD);
    if(selected)fill(d->hDC,RECT{r.left,r.top,r.left+3,r.bottom},C_RED_HI);
    if(d->itemID>=visible_rooms.size())return;
    const auto& room=rooms[visible_rooms[d->itemID]];
    write(d->hDC,room.code(),RECT{r.left+14,r.top,r.left+130,r.bottom},code_font,C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t where[64];swprintf(where,64,tx(cv_test_i18n::WHERE),room.stage,room.number);
    write(d->hDC,where,RECT{r.left+140,r.top,r.left+330,r.bottom},small_font,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    if(room.variant) {
        wchar_t tag[64];swprintf(tag,64,tx(cv_test_i18n::EVENT),room.variant);
        write(d->hDC,tag,RECT{r.left+330,r.top,r.right-12,r.bottom},small_font,RGB(214,120,96),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
}
LRESULT CALLBACK button_proc(HWND b,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_MOUSEMOVE&&hover_button!=GetDlgCtrlID(b)) {
        hover_button=GetDlgCtrlID(b);InvalidateRect(b,nullptr,FALSE);
        TRACKMOUSEEVENT t{sizeof t,TME_LEAVE,b,0};TrackMouseEvent(&t);
    } else if(msg==WM_MOUSELEAVE) {hover_button=0;InvalidateRect(b,nullptr,FALSE);}
    return DefSubclassProc(b,msg,wp,lp);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        filter=child(h,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,inside(filter_box),ID_FILTER,body_font);
        entry=child(h,L"EDIT",L"0",WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,inside(entry_box),ID_ENTRY,body_font);
        SendMessageW(entry,EM_LIMITTEXT,3,0);
        list=child(h,L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,list_box,ID_LIST,body_font);
        HWND load=child(h,L"BUTTON",L"",WS_TABSTOP|BS_OWNERDRAW,RECT{MARGIN,584,MARGIN+200,624},ID_LOAD,body_font);
        save_button=child(h,L"BUTTON",L"",WS_TABSTOP|BS_OWNERDRAW,RECT{MARGIN+216,584,MARGIN+416,624},ID_SAVE,body_font);
        HWND back=child(h,L"BUTTON",L"",WS_TABSTOP|BS_OWNERDRAW,RECT{W-MARGIN-200,584,W-MARGIN,624},ID_BACK,body_font);
        travel=load;
        SetWindowTheme(list,L"DarkMode_Explorer",nullptr);   // a dark scroll bar
        for(HWND b:{load,save_button,back})SetWindowSubclass(b,button_proc,1,0);
        SetTimer(h,1,50,nullptr);return 0;}
    case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=32;return TRUE;
    case WM_DRAWITEM: {
        auto* d=reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if(d->CtlType==ODT_LISTBOX)draw_room(d);else draw_button(d);
        return TRUE;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint(h);return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==ID_FILTER&&HIWORD(wp)==EN_CHANGE)refresh_list();
        if((LOWORD(wp)==ID_FILTER||LOWORD(wp)==ID_ENTRY)&&(HIWORD(wp)==EN_SETFOCUS||HIWORD(wp)==EN_KILLFOCUS)) {
            RECT r=LOWORD(wp)==ID_FILTER?filter_box:entry_box;InflateRect(&r,1,1);InvalidateRect(h,&r,FALSE);}
        if(LOWORD(wp)==ID_LOAD||(LOWORD(wp)==ID_LIST&&HIWORD(wp)==LBN_DBLCLK)||LOWORD(wp)==IDOK)send_selected();
        if(LOWORD(wp)==ID_SAVE)send_save();
        if(LOWORD(wp)==ID_BACK||LOWORD(wp)==IDCANCEL)SendMessageW(h,WM_CLOSE,0,0);return 0;
    case WM_TIMER: {
        if(!IsWindowVisible(h))return 0;
        int r=result.load();auto code=room_now.load();wchar_t line[256];
        if(r==6)set_status(tx(cv_test_i18n::SAVING),C_OK);
        else if(r==2) {swprintf(line,256,tx(cv_test_i18n::DONE),(code>>8)&255,code&255);set_status(line,C_OK);}
        else if(r==3||r==4)set_status(tx(cv_test_i18n::REFUSED),C_RED_HI);
        else if(ready.load()) {swprintf(line,256,tx(cv_test_i18n::READY),visible_rooms.size());set_status(line,C_OK);}
        else set_status(tx(cv_test_i18n::WAIT),C_DIM);
        EnableWindow(travel,enabled&&ready&&!request.load()&&!visible_rooms.empty());
        EnableWindow(save_button,SAVE_OBJECT&&enabled&&ready&&!request.load()&&!save_request.load());   // only where the save screen can be released
        if(GetForegroundWindow()!=h)return 0;
        for(int i=0;i<PAD_PHYS_MAX;i++) {PadPhys p{};if(!pad_phys_poll(i,&p)){previous_pad[i]=0;continue;}
            uint32_t edge=p.buttons&~previous_pad[i];previous_pad[i]=p.buttons;
            if(edge&(1u<<PB_EAST)){SendMessageW(h,WM_CLOSE,0,0);break;}
            if(edge&(1u<<PB_SOUTH)){send_selected();break;}
            int delta=(p.buttons&(1u<<PB_DOWN))?1:(p.buttons&(1u<<PB_UP))?-1:0;
            if(delta&&((edge&((1u<<PB_DOWN)|(1u<<PB_UP)))||GetTickCount64()>=repeat_at[i])) {int n=int(SendMessageW(list,LB_GETCOUNT,0,0)),sel=int(SendMessageW(list,LB_GETCURSEL,0,0));if(n)SendMessageW(list,LB_SETCURSEL,std::clamp(sel+delta,0,n-1),0);repeat_at[i]=GetTickCount64()+160;}}
        return 0;}
    case WM_ACTIVATE:g_pad_block=LOWORD(wp)!=WA_INACTIVE;return 0;   // the game ignores the pad only while this window has the focus, as under the launcher
    case WM_KEYDOWN:if(wp==VK_F7||wp==VK_ESCAPE)SendMessageW(h,WM_CLOSE,0,0);else if(wp==VK_RETURN)send_selected();return 0;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);g_pad_block=0;{auto game=static_cast<HWND>(rsx_d3d12_window());if(game)SetForegroundWindow(game);}return 0;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wp),C_TEXT);SetBkColor(reinterpret_cast<HDC>(wp),C_FIELD);return reinterpret_cast<LRESULT>(field_brush);
    case WM_DESTROY:window=nullptr;KillTimer(h,1);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
}
bool cv_test_tools_message(MSG* m) {
    if(!cv_test_tools_visible() || (m->hwnd!=window&&!IsChild(window,m->hwnd)))return false;
    if(m->message==WM_KEYDOWN&&(m->wParam==VK_ESCAPE||m->wParam==VK_F7)){cv_test_tools_close();return true;}
    if(m->message==WM_KEYDOWN&&m->wParam==VK_RETURN){send_selected();return true;}
    return IsDialogMessageW(window,m)!=FALSE;
}
void cv_test_tools_enable(bool on) {enabled=on;if(!on){request=0;if(window)cv_test_tools_close();}}
bool cv_test_tools_visible() {return window&&IsWindowVisible(window);}
void cv_test_tools_close() {if(window)SendMessageW(window,WM_CLOSE,0,0);}
void cv_test_tools_show(HWND owner,const std::wstring& game,int lang) {
    if(!enabled)return;
    if(cv_test_tools_visible()){cv_test_tools_close();return;}
    (void)owner;
    lang=lang>=0&&lang<13?lang:1;
    if(window&&language!=lang)DestroyWindow(window);
    language=lang;
    if(!window) {
        if(!field_brush) {field_brush=CreateSolidBrush(C_FIELD);title_font=font(30,FW_BOLD,L"Georgia");body_font=font(17,FW_NORMAL,L"Segoe UI");
            small_font=font(14,FW_NORMAL,L"Segoe UI");code_font=font(17,FW_SEMIBOLD,L"Consolas");}
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"CVTestMaps";
        wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
        RECT r{0,0,W,H};AdjustWindowRectEx(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW);
        // No owner: showing an owned window brings back its minimized owner (the launcher), which blocks the pad.
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"CVTestMaps",tx(cv_test_i18n::WINDOW),
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        // A dark title bar, as the launcher's.
        BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof dark);
    }
    if(game_root!=game||rooms.empty()){game_root=game;catalog(game);}
    refresh_list();ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(list);g_pad_block=1;
    for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);previous_pad[i]=p.buttons;}
}
void TEST_FRAME(ppu_context* ctx) {
    if(enabled) {
        bool valid=active_game();ready=valid;
        if(mapped(state_base+0x95,3))room_now=(vm_read8(state_base+0x95)<<8)|vm_read8(state_base+0x96);
        uint32_t pending=request.exchange(0);
        if(pending){if(valid){call_warp(ctx,pending);result=2;ready=false;}else{result=4;std::printf("[CVTest] warp refused: no controllable live game\n");}}
        // The request the typewriter makes once its ribbon is accepted:
        // func_001AEED4 (EU) sees +74 bit 0x00200000 and opens the save screen.
        if(save_request.exchange(false)) {
            if(valid) {vm_write32(state_base+0x74,vm_read32(state_base+0x74)|0x00200000u);tool_save=true;result=6;ready=false;
                std::printf("[CVTest] save screen requested\n");std::fflush(stdout);}
            else {result=4;std::printf("[CVTest] save refused: no controllable live game\n");}
        }
        // Record natural save entry/exit only while these optional tools are enabled.
        if(mapped(state_base,0x84)) {static uint32_t last_tasks=0;uint32_t tasks=vm_read32(state_base+0x64),active=tasks&~vm_read32(state_base+0x68);
            if((last_tasks&0x8000)&&!(active&0x8000))tool_save=false;   // the save screen closed
            // A save from this window in a room with no typewriter: the room has no
            // location name, so the "Saving content" text leaves the slot list's
            // display limit (+1C) as garbage and the minimum display time
            // (func_001D8D70) never ends. Once the file is written (the manager idle,
            // the save object waiting at state 2/4), end it as that timeout does.
            if(tool_save&&(active&0x8000)&&SAVE_OBJECT) {
                const uint32_t o=vm_read32(SAVE_OBJECT),mgr=vm_read32(SAVE_MANAGER);
                if(o&&mapped(o,0xA4)&&mgr&&mapped(mgr,0x4F0)&&vm_read32(o+0x30)==2&&vm_read32(o+0x58)==4&&vm_read8(o+0x2B)==1&&vm_read32(mgr+0x4E8)==0) {
                    const uint32_t ui=vm_read32(o+0xA0),list=ui&&mapped(ui,0x38)?vm_read32(ui+0x34):0;
                    if(list&&mapped(list,0x28)&&vm_read32(list)&&int32_t(vm_read32(list+0x20))>=8) {
                        vm_write32(list+0x20,0);vm_write32(list,0);vm_write16(list+0x14,0xFFFA);
                        std::printf("[CVTest] tool save written; save screen released\n");std::fflush(stdout);
                    }
                }
            }
            if(((active^last_tasks)&0x18000)!=0)std::printf("[CVTest] native menu/save transition: tasks=%08X blocked=%08X flags=%08X/%08X/%08X saveMode=%u helper=%08X\n",tasks,vm_read32(state_base+0x68),vm_read32(state_base+0x50),vm_read32(state_base+0x70),vm_read32(state_base+0x74),vm_read16(state_base+0x5C),vm_read32(state_base+0x60));last_tasks=active;}
    }else ready=false;
    ORIGINAL_FRAME(ctx);
}
void TEST_SERIALIZE(ppu_context* ctx) {
    if(enabled)std::printf("[CVTest] original save serializer: object=%08X room=%04X flags=%08X/%08X/%08X\n",uint32_t(ctx->gpr[3]),vm_read16(CV_SRT_ROOM),vm_read32(state_base+0x50),vm_read32(state_base+0x70),vm_read32(state_base+0x74));
    // A save made from this window spends no ink ribbon and adds nothing to the
    // save count: func_001D90A8 does both after the data was copied, so the
    // file is the same; only the live inventory and counter are put back.
    const uint32_t inventory=CV_SRT_INVENTORY+vm_read8(CV_SRT_CHARACTER)*0x40;
    const bool restore=enabled&&tool_save&&mapped(inventory,48)&&mapped(state_base+0x90,4);
    uint32_t items[12]={},saves=0;
    if(restore) {for(int i=0;i<12;i++)items[i]=vm_read32(inventory+i*4);saves=vm_read32(state_base+0x90);}
    ORIGINAL_SERIALIZE(ctx);
    if(restore) {for(int i=0;i<12;i++)vm_write32(inventory+i*4,items[i]);vm_write32(state_base+0x90,saves);
        std::printf("[CVTest] tool save: ribbon and save count kept\n");std::fflush(stdout);}
}
#endif
