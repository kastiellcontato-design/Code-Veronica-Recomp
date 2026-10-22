// Native progress library: independent files, never modifies DATA0.DAT.
#include "cv_save_library.h"
#ifdef _WIN32
#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_save_rooms.h"
#include "cv_launcher_controls.h"
#include "cv_test_tools.h"
#include "cv_test_tools_i18n.h"
#include "cv_archives_i18n.h"
#include "cv_srt_i18n.h"
#include <cwctype>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <cmath>
#include <dwmapi.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cstdlib>
extern "C" uint8_t* vm_base;
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" int g_pad_block;
extern "C" void* rsx_d3d12_window(void);
extern "C" void cv_focus_pause_hold(int);
extern "C" void perf_overlay_toast(const wchar_t*,int);
extern "C" int rsx_d3d12_request_thumbnail(void (*)(void*,const uint32_t*,unsigned,unsigned),void*);

#if defined(CV_REGION_JP)
#define SAVE_DATA_PTR 0x00AB2AD4u
#define PLAYER_PTR 0x00A79C3Cu
#define STOP_BANK func_00259C4C
#define LOAD_BEGIN func_001CE9E0
#define SOUND_PTR 0x00AB5F64u
#elif defined(CV_REGION_US)
#define SAVE_DATA_PTR 0x00AB2A18u
#define PLAYER_PTR 0x00A79BD8u
#define STOP_BANK func_0025935C
#define LOAD_BEGIN func_001CE860
#define SOUND_PTR 0x00AB5EA8u
#else
#define SAVE_DATA_PTR 0x00AC2D64u
#define PLAYER_PTR 0x00A89BD8u
#define STOP_BANK func_0025B32C
#define LOAD_BEGIN func_001D0680
#define SOUND_PTR 0x00AC61F4u
#endif
void STOP_BANK(ppu_context*);void LOAD_BEGIN(ppu_context*);
namespace {
namespace fs=std::filesystem;
constexpr uint32_t state=CV_SRT_CHARACTER-0x94;
constexpr size_t slot_size=0x838,common_size=0x34,head_size=128;
constexpr unsigned tw=320,th=180;

constexpr UINT WM_DONE=WM_APP+432;
struct Record {fs::path path;uint64_t time=0;unsigned stage=0,room=0,variant=0,character=0,seconds=0;int legacy=-1;};
struct Data {std::array<uint8_t,slot_size> slot{};std::array<uint8_t,common_size> common{};uint32_t option=0;};
struct Pending {bool save=false;fs::path replace;Data data;};
std::mutex queue_lock,photo_lock;std::condition_variable photo_ready;Pending queued;bool have_request=false;
std::atomic<bool> busy{false},available{false};std::atomic<HWND> notification{nullptr};
std::atomic<uintptr_t> sequence{0};uintptr_t photo_id=0;std::vector<uint32_t> photo;
HWND window=nullptr;bool saving=false;std::atomic<int> language{1};int selected=0,scroll=0;
std::vector<Record> entries;std::map<std::wstring,std::vector<uint32_t>> thumbnails;
std::wstring status;HFONT title_font=nullptr,body_font=nullptr,small_font=nullptr;
uint32_t previous_pad[PAD_PHYS_MAX]={};ULONGLONG repeat_at[PAD_PHYS_MAX]={};

uint32_t be(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
void putbe(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=uint8_t(v>>(24-i*8));}
uint32_t le(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put(std::vector<uint8_t>& b,size_t p,uint32_t v){for(unsigned i=0;i<4;i++)b[p+i]=uint8_t(v>>(i*8));}
uint64_t time_value(){FILETIME t;GetSystemTimeAsFileTime(&t);return uint64_t(t.dwLowDateTime)|(uint64_t(t.dwHighDateTime)<<32);}
uint32_t crc(const uint8_t* p,size_t n){uint32_t h=~0u;for(size_t i=0;i<n;i++){h^=p[i];for(unsigned j=0;j<8;j++)h=(h>>1)^(0xedb88320u&uint32_t(-int(h&1)));}return ~h;}
bool mapped(uint32_t a,size_t n){if(!vm_base||!a||n>UINT32_MAX-a)return false;const uint8_t* p=vm_base+a,*end=p+n;
 while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(p,&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 const auto* next=static_cast<const uint8_t*>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;}return true;}
fs::path root(){const char* env=std::getenv("PS3_SAVE_ROOT");return env&&*env?fs::u8path(env):fs::path(L"./gamedata/dev_hdd0/home/00000001/savedata");}
fs::path library(){return root()/L"CodeVeronicaLibrary"/CV_TITLE_ID;}
bool valid_slot(const Data& d){
 if(be(d.slot.data())!=10||d.slot[8]>=4||d.slot[9]>=10||d.slot[10]>=100||d.slot[11]>=10)return false;
 for(unsigned axis=0;axis<3;axis++){uint32_t bits=be(d.slot.data()+0x804+axis*4);float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f)||std::abs(f)>1000000)return false;}
 return true;
}
Record metadata(const Data& d){Record r;r.time=time_value();r.character=d.slot[8];r.stage=d.slot[9];r.room=d.slot[10];r.variant=d.slot[11];r.seconds=be(d.slot.data()+CV_SRT_TIME-state-0x8c)/60;return r;}
bool header(const uint8_t* h){return !std::memcmp(h,"CVSAVE1",8)&&le(h+8)==1&&le(h+12)==head_size&&!std::memcmp(h+16,CV_TITLE_ID,std::strlen(CV_TITLE_ID))&&le(h+40)==slot_size+common_size+4&&le(h+44)==tw*th*4;}
bool read(const Record& r,Data& d,std::vector<uint32_t>* image=nullptr){
 std::ifstream f(r.path,std::ios::binary|std::ios::ate);if(!f)return false;const auto n=f.tellg();
 if(r.legacy>=0){if(n!=0x7c84)return false;f.seekg(8+size_t(r.legacy)*slot_size);f.read((char*)d.slot.data(),slot_size);f.seekg(0x7b50);f.read((char*)d.common.data(),common_size);f.seekg(0x7b84);uint8_t opt[4]{};f.read((char*)opt,4);d.option=be(opt);return bool(f)&&valid_slot(d);}
 const size_t expected=head_size+slot_size+common_size+4+tw*th*4;if(n!=std::streamoff(expected))return false;
 std::vector<uint8_t> b(expected);f.seekg(0);f.read((char*)b.data(),b.size());if(!f||!header(b.data()))return false;
 const uint32_t check=le(b.data()+48);std::fill(b.begin()+48,b.begin()+52,0);if(crc(b.data(),b.size())!=check)return false;
 std::memcpy(d.slot.data(),b.data()+head_size,slot_size);std::memcpy(d.common.data(),b.data()+head_size+slot_size,common_size);d.option=le(b.data()+head_size+slot_size+common_size);
 if(image){image->resize(tw*th);std::memcpy(image->data(),b.data()+head_size+slot_size+common_size+4,tw*th*4);}return valid_slot(d);
}
void refresh(){
 entries.clear();thumbnails.clear();std::error_code ec;
 for(fs::directory_iterator it(library(),ec),end;!ec&&it!=end;it.increment(ec)){
  if(!it->is_regular_file(ec)||it->path().extension()!=L".cvsav")continue;
  std::ifstream f(it->path(),std::ios::binary);std::array<uint8_t,head_size> h{};f.read((char*)h.data(),h.size());if(!f||!header(h.data()))continue;
  Record r;r.path=it->path();r.time=uint64_t(le(h.data()+28))|(uint64_t(le(h.data()+32))<<32);r.character=h[52];r.stage=h[53];r.room=h[54];r.variant=h[55];r.seconds=le(h.data()+56);entries.push_back(r);
 }
 // Original slots are listed read-only. Never copy over, delete, or switch DATA0.DAT.
 ec.clear();for(fs::directory_iterator it(root(),ec),end;!ec&&it!=end;it.increment(ec)){
  if(!it->is_directory(ec)||it->path().filename().wstring().find(fs::path(CV_TITLE_ID).wstring())!=0)continue;
  const auto path=it->path()/L"DATA0.DAT";std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=0x7c84)continue;
  WIN32_FILE_ATTRIBUTE_DATA info{};uint64_t written=0;if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info))written=uint64_t(info.ftLastWriteTime.dwLowDateTime)|(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32);
  for(int i=0;i<15;i++){Data d;f.seekg(8+i*slot_size);f.read((char*)d.slot.data(),slot_size);if(!f||!valid_slot(d))continue;auto r=metadata(d);r.path=path;r.time=written;r.legacy=i;entries.push_back(r);}
 }
 std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.time>b.time;});
 selected=std::clamp(selected,0,std::max(0,int(entries.size())-(saving?0:1)));selected=std::max(0,selected);scroll=0;
}
std::wstring date(uint64_t value){FILETIME ft{DWORD(value),DWORD(value>>32)},local{};SYSTEMTIME t{};FileTimeToLocalFileTime(&ft,&local);FileTimeToSystemTime(&local,&t);wchar_t out[64];swprintf(out,64,L"%04u-%02u-%02u  %02u:%02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return out;}
void thumbnail(void* user,const uint32_t* rgba,unsigned w,unsigned h){
 if(!rgba||!w||!h)return;std::lock_guard<std::mutex> lock(photo_lock);if(uintptr_t(user)!=sequence.load())return;
 photo.assign(tw*th,0xff111111);const double scale=std::min(double(tw)/w,double(th)/h);unsigned dw=std::max(1u,unsigned(w*scale)),dh=std::max(1u,unsigned(h*scale));
 for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){uint32_t c=rgba[size_t(std::min(h-1,unsigned(y/scale)))*w+std::min(w-1,unsigned(x/scale))];photo[size_t(y+(th-dh)/2)*tw+x+(tw-dw)/2]=0xff000000u|((c&255)<<16)|(c&0xff00)|((c>>16)&255);}
 photo_id=uintptr_t(user);photo_ready.notify_all();
}
void publish(const wchar_t* msg,bool ok){perf_overlay_toast(msg,ok);if(HWND h=notification.load())PostMessageW(h,WM_DONE,ok,0);}
bool write_save(const Data& d,const fs::path& replace,uintptr_t id){
 std::vector<uint32_t> pixels(tw*th,0xff191616);
 {std::unique_lock<std::mutex> lock(photo_lock);photo_ready.wait_for(lock,std::chrono::seconds(3),[&]{return photo_id==id;});if(photo_id==id)pixels=photo;}
 auto r=metadata(d);std::vector<uint8_t> b(head_size+slot_size+common_size+4+pixels.size()*4);
 std::memcpy(b.data(),"CVSAVE1",8);put(b,8,1);put(b,12,head_size);std::memcpy(b.data()+16,CV_TITLE_ID,std::strlen(CV_TITLE_ID));put(b,28,uint32_t(r.time));put(b,32,uint32_t(r.time>>32));
 put(b,40,slot_size+common_size+4);put(b,44,tw*th*4);b[52]=uint8_t(r.character);b[53]=uint8_t(r.stage);b[54]=uint8_t(r.room);b[55]=uint8_t(r.variant);put(b,56,r.seconds);
 std::memcpy(b.data()+head_size,d.slot.data(),slot_size);std::memcpy(b.data()+head_size+slot_size,d.common.data(),common_size);put(b,head_size+slot_size+common_size,d.option);
 std::memcpy(b.data()+head_size+slot_size+common_size+4,pixels.data(),pixels.size()*4);put(b,48,crc(b.data(),b.size()));
 std::error_code ec;fs::create_directories(library(),ec);if(ec)return false;
 wchar_t name[96];swprintf(name,96,L"%020llu-%u-%llu.cvsav",(unsigned long long)r.time,GetCurrentProcessId(),(unsigned long long)id);
 const auto target=replace.empty()?library()/name:replace;const auto temp=library()/(std::wstring(name)+L".pending");
 HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 DWORD done=0;bool ok=WriteFile(file,b.data(),DWORD(b.size()),&done,nullptr)&&done==b.size()&&FlushFileBuffers(file);CloseHandle(file);
 if(ok)ok=MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(replace.empty()?0:MOVEFILE_REPLACE_EXISTING))!=FALSE;
 if(!ok)DeleteFileW(temp.c_str());return ok;
}
struct Call {ppu_context c;ppu_context* previous;void(*tail)(void*);Call(ppu_context* p):c(*p),previous(g_active_ctx),tail(g_trampoline_fn){c.gpr[1]-=0x2000;vm_write64(c.gpr[1],p->gpr[1]);c.gpr[2]=CV_TOC;g_active_ctx=&c;g_trampoline_fn=nullptr;}
 ~Call(){g_active_ctx=previous;g_trampoline_fn=tail;}void run(void(*fn)(ppu_context*),uint32_t a=0,uint32_t b=0){c.gpr[3]=a;c.gpr[4]=b;fn(&c);while(g_trampoline_fn){auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&c);}}};
bool restore(ppu_context* ctx,const Data& d){
 Call call(ctx);
 // System options belong to the active profile. The native load helper that
 // updates DATA0's achievement flag requires a live mutex/header; no fake
 // savedata manager or scratch lock is constructed for a library entry.
 // Same native slot copy as the load screen; no UI object or virtual slot
 // is constructed. Then enter its confirmed restart path, without UI cleanup.
 std::memcpy(vm_base+state+0x8c,d.slot.data(),slot_size);
 if(uint32_t sound=vm_read32(SOUND_PTR))call.run(STOP_BANK,sound,9);
 call.run(LOAD_BEGIN);
 // Keep the native confirmed-load flag (0x200). Room startup uses it
 // to restore saved position/health rather than initialize a new game.
 vm_write32(state+0x50,(vm_read32(state+0x50)&~0x00030040u)|0x200u);
 vm_write32(state+0x70,vm_read32(state+0x70)&~0x08000000u);vm_write32(state+0x74,vm_read32(state+0x74)&~0x00200000u);
 vm_write32(state+0x68,0x80);vm_write32(state+0x64,0x00300050);vm_write32(state+0x58,0);vm_write16(state+0x5c,0);vm_write16(state+0x5e,0);
 vm_write32(state+0x1b18c,vm_read32(state+0x1b1d4));
 std::fprintf(stderr,"[SAVE-LIBRARY] native load requested room=%u/%u/%u character=%u\n",d.slot[9],d.slot[10],d.slot[11],d.slot[8]);return true;
}
bool capture(Data& out){
 // Mirrors the PS3's native snapshot (EU 00190BA8 / JP 0018F1A8 /
 // US 0018F210), but writes only our local copy. The snapshot refreshes
 // live actor values that the persistent state does not update every tick.
 const uint32_t actor=vm_read32(PLAYER_PTR);const unsigned character=vm_read8(state+0x94);
 if(character>=4||!mapped(actor,0x420))return false;
 std::memcpy(out.slot.data(),vm_base+state+0x8c,slot_size);
 auto put16=[&](size_t at,unsigned v){out.slot[at]=uint8_t(v>>8);out.slot[at+1]=uint8_t(v);};
 putbe(out.slot.data(),10);out.slot[0x9a-0x8c]=vm_read8(actor+8);
 putbe(out.slot.data()+0x880-0x8c+character*4,vm_read32(actor+0x88)&0x78280000u);
 for(unsigned axis=0;axis<3;axis++)putbe(out.slot.data()+0x890-0x8c+axis*4,vm_read32(actor+0x10+axis*4));
 putbe(out.slot.data()+0x89c-0x8c,vm_read32(actor+0x20));
 out.slot[0x8a0-0x8c+character]=uint8_t(vm_read32(actor+0x3e4));
 put16(0x8a4-0x8c+character*2,vm_read32(actor+0x41c));
 std::memcpy(out.common.data(),vm_base+state+0x34,common_size);
 return valid_slot(out);
}
// Unified host UI. Native save capture/restore remains in cv_save_library.cpp.
namespace ui=cv_archives_i18n;
const wchar_t* tr(ui::Id id){return ui::strings[language.load()][id];}
const wchar_t* mt(cv_test_i18n::Id id){return cv_test_i18n::k_text[language.load()][id];}
constexpr int W=1200,H=760,row_h=110,top=202,rows_visible=4;
int tab=0,spawn=0,input_kind=PAD_INPUT_KEYBOARD;
int painted_hint_kind=PAD_INPUT_KEYBOARD;POINT last_mouse{};bool stick_active[PAD_PHYS_MAX]={};
int tab_selected[3]={},tab_scroll[3]={};
bool confirming=false,confirm_yes=false;
HWND search=nullptr;
HFONT code_font=nullptr,edit_font=nullptr;
std::vector<cv_test::Room> maps;std::vector<size_t> filtered_maps;
unsigned current_room=~0u;
bool (*travel_to)(unsigned,unsigned)=nullptr;
ULONGLONG held_since[PAD_PHYS_MAX]={};
uint32_t held_direction[PAD_PHYS_MAX]={};
const RECT primary{836,643,1164,693},secondary{836,604,1164,637};
const RECT cancel_box{342,410,584,456},yes_box{614,410,856,456};
void repaint(){if(window)InvalidateRect(window,nullptr,FALSE);}
void close_window(){if(busy)return;confirming=false;if(window)ShowWindow(window,SW_HIDE);g_pad_block=0;cv_focus_pause_hold(0);if(auto game=(HWND)rsx_d3d12_window())SetForegroundWindow(game);}
int count(){return tab==0?int(filtered_maps.size()):int(entries.size())+(saving?1:0);}
void move(int delta){if(busy||confirming||!count())return;selected=std::clamp(selected+delta,0,count()-1);if(selected<scroll)scroll=selected;if(selected>=scroll+rows_visible)scroll=selected-rows_visible+1;repaint();}
void filter_maps(){wchar_t value[128]={};if(search)GetWindowTextW(search,value,128);std::wstring query=value;for(auto& c:query)c=wchar_t(towlower(c));filtered_maps.clear();for(size_t i=0;i<maps.size();i++)if(query.empty()||maps[i].code().find(query)!=std::wstring::npos)filtered_maps.push_back(i);selected=scroll=0;repaint();}
void search_layout(){if(!search)return;RECT c;GetClientRect(window,&c);MoveWindow(search,230*c.right/W,159*c.bottom/H,397*c.right/W,29*c.bottom/H,TRUE);if(body_font){LOGFONTW lf{};GetObjectW(body_font,sizeof(lf),&lf);lf.lfHeight=-std::max(10,int(20*c.bottom/H));HFONT next=CreateFontIndirectW(&lf);SendMessageW(search,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(edit_font)DeleteObject(edit_font);edit_font=next;}ShowWindow(search,tab==0?SW_SHOW:SW_HIDE);}
void switch_tab(int next){if(busy||confirming)return;tab_selected[tab]=selected;tab_scroll[tab]=scroll;tab=(next+3)%3;saving=tab==1;selected=tab_selected[tab];scroll=tab_scroll[tab];status.clear();search_layout();SetFocus(window);move(0);repaint();}
const Record* selection(){int i=selected-(saving?1:0);return tab!=0&&i>=0&&i<int(entries.size())?&entries[i]:nullptr;}
std::wstring room_label(unsigned stage,unsigned room,unsigned variant){
 // Descriptive English room names are optional metadata. Other languages
 // use the localized area/room label rather than leaking English UI strings.
 if(language==1)return cv_save_room_name(stage,room,variant);
 wchar_t out[128];swprintf(out,128,mt(cv_test_i18n::WHERE),stage,room);wchar_t code[32];swprintf(code,32,L"  ·  rm_%u%02u%u",stage,room,variant);return std::wstring(out)+code;
}
void activate(bool overwrite=false){
 if(busy||!available)return;
 if(tab==0){if(selected>=0&&selected<int(filtered_maps.size())&&travel_to&&travel_to(maps[filtered_maps[selected]].key(),unsigned(spawn)))close_window();else{status=mt(cv_test_i18n::REFUSED);repaint();}return;}
 Pending p;p.save=saving;const int index=selected-(saving?1:0);
 if(saving){if(index>=0&&!overwrite){confirming=true;confirm_yes=false;repaint();return;}
  if(index>=0){if(index>=int(entries.size())||entries[index].legacy>=0)return;p.replace=entries[index].path;}}
 else if(index<0||index>=int(entries.size())||!read(entries[index],p.data)){status=tr(ui::CORRUPT);repaint();return;}
 {std::lock_guard<std::mutex> lock(queue_lock);if(have_request)return;queued=std::move(p);have_request=true;busy=true;}
 status=tr(ui::WORKING);cv_focus_pause_hold(0);if(!saving){ShowWindow(window,SW_HIDE);g_pad_block=0;if(auto game=(HWND)rsx_d3d12_window())SetForegroundWindow(game);}repaint();
}
void request_overwrite(){const auto* e=selection();if(busy||!available||!saving||!e||e->legacy>=0)return;confirming=true;confirm_yes=false;repaint();}
void accept(){if(confirming){bool yes=confirm_yes;confirming=false;if(yes)activate(true);repaint();}else{if(saving)selected=scroll=0;activate();}}
void back(){if(confirming){confirming=false;repaint();}else close_window();}
void fill(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void border(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FrameRect(dc,&r,b);DeleteObject(b);}
void label(HDC dc,const std::wstring& s,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS){auto old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);if(cv_srt_i18n::rtl_text(s))flags|=DT_RTLREADING|DT_RIGHT;
 // The fonts draw the PlayStation square tiny: leave its room blank in the text and draw a box there.
 const size_t sq=s.find(L'□');
 if(sq!=std::wstring::npos&&(flags&DT_SINGLELINE)&&!(flags&DT_RTLREADING)){
  TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);const int side=std::max(6,int(tm.tmAscent*0.62f));
  std::wstring spaced=s;SIZE gap{};GetTextExtentPoint32W(dc,L" ",1,&gap);spaced[sq]=L' ';   // an em space holds the box's place
  SIZE all{},before{};GetTextExtentPoint32W(dc,spaced.c_str(),int(spaced.size()),&all);GetTextExtentPoint32W(dc,spaced.c_str(),int(sq),&before);
  DrawTextW(dc,spaced.c_str(),int(spaced.size()),&r,flags|DT_NOPREFIX);
  const int x0=(flags&DT_CENTER)?r.left+(r.right-r.left-all.cx)/2:(flags&DT_RIGHT)?r.right-all.cx:r.left;
  const int bx=x0+before.cx+(gap.cx-side)/2,by=(flags&DT_VCENTER)?(r.top+r.bottom-side)/2:r.top+(tm.tmHeight-side)/2;
  HPEN pen=CreatePen(PS_SOLID,1,color);auto old_pen=SelectObject(dc,pen);auto old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
  Rectangle(dc,bx,by,bx+side,by+side);SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
  SelectObject(dc,old);return;
 }
 DrawTextW(dc,s.c_str(),int(s.size()),&r,flags|DT_NOPREFIX);SelectObject(dc,old);}
constexpr COLORREF ivory=RGB(234,223,203),dim=RGB(157,148,137),brass=RGB(193,161,104),red=RGB(146,32,40);
const wchar_t* chars[]={L"Claire Redfield",L"Chris Redfield",L"Steve Burnside",L"Albert Wesker"};
void picture(HDC dc,const Record* e,RECT box){fill(dc,box,RGB(13,14,15));bool shown=false;
 if(e&&e->legacy<0){auto key=e->path.wstring();if(!thumbnails.count(key)){if(thumbnails.size()>=12)thumbnails.clear();Data d;std::vector<uint32_t> px;if(read(*e,d,&px))thumbnails.emplace(key,std::move(px));}auto at=thumbnails.find(key);if(at!=thumbnails.end()){BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),tw,-int(th),1,32,BI_RGB};SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,box.left,box.top,box.right-box.left,box.bottom-box.top,0,0,tw,th,at->second.data(),&bi,DIB_RGB_COLORS,SRCCOPY);shown=true;}}
 if(!shown){RECT r=box;r.top+=(r.bottom-r.top)/2-12;label(dc,e&&e->legacy>=0?tr(ui::ORIGINAL):tr(ui::NO_PREVIEW),r,small_font,dim,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}border(dc,box,RGB(61,54,46));
}
// Match the launcher's explicit icon preference; Auto follows local menu input.
int hint_kind(){
 const char* mode=std::getenv("CV_BUTTON_ICONS");
 if(mode&&std::strcmp(mode,"keyboard")==0)return PAD_INPUT_KEYBOARD;
 if(mode&&std::strcmp(mode,"xbox")==0)return PAD_KIND_XINPUT;
 if(mode&&std::strcmp(mode,"ps")==0)return PAD_KIND_DS4;
 return input_kind==PAD_KIND_XINPUT?PAD_KIND_XINPUT:(input_kind==PAD_KIND_DS4||input_kind==PAD_KIND_DUALSENSE)?PAD_KIND_DS4:PAD_INPUT_KEYBOARD;
}
void note_input(int kind){
 if(kind!=PAD_KIND_XINPUT&&kind!=PAD_KIND_DS4&&kind!=PAD_KIND_DUALSENSE)kind=PAD_INPUT_KEYBOARD;
 if(input_kind!=kind){input_kind=kind;repaint();}
}
void menu_pad(PadPhys& p){
 // Undo gameplay's Xbox 360 shoulder remapping only for this host menu.
 if(pad_phys_360_layout(p.kind)){const auto bits=p.buttons;auto swap=[&](int a,int b){p.buttons=(p.buttons&~((1u<<a)|(1u<<b)))|((bits&(1u<<a))?(1u<<b):0)|((bits&(1u<<b))?(1u<<a):0);};swap(PB_L1,PB_L2);swap(PB_R1,PB_R2);}
 if(p.ly<64)p.buttons|=1u<<PB_UP;else if(p.ly>192)p.buttons|=1u<<PB_DOWN;
 if(p.lx<64)p.buttons|=1u<<PB_LEFT;else if(p.lx>192)p.buttons|=1u<<PB_RIGHT;
}
std::wstring button_hint(const wchar_t* key,const wchar_t* ps,const wchar_t* xbox){const int kind=hint_kind();return kind==PAD_INPUT_KEYBOARD?key:kind==PAD_KIND_XINPUT?xbox:ps;}
std::wstring action_name(){if(tab==0)return mt(cv_test_i18n::LOAD);return saving?tr(ui::NEW_SAVE):mt(cv_test_i18n::LOAD_SAVE);}
void paint(HWND h){painted_hint_kind=hint_kind();PAINTSTRUCT ps;HDC dc0=BeginPaint(h,&ps),dc=CreateCompatibleDC(dc0);HBITMAP bmp=CreateCompatibleBitmap(dc0,W,H);auto old=SelectObject(dc,bmp);
 fill(dc,{0,0,W,H},RGB(17,17,19));fill(dc,{0,0,W,3},red);border(dc,{0,0,W,H},RGB(67,58,46));
 label(dc,L"CODE: VERONICA",{32,21,800,44},small_font,dim);label(dc,tr(ui::ARCHIVES),{30,48,1020,96},title_font,ivory);label(dc,tr(ui::PAUSED),{940,35,1165,66},small_font,dim,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS);
 const wchar_t* names[]={mt(cv_test_i18n::TITLE),tr(ui::SAVE_TAB),tr(ui::LOAD_TAB)};
 for(int i=0;i<3;i++){RECT r{170+i*286,108,448+i*286,146};if(i==tab){fill(dc,r,RGB(35,26,28));fill(dc,{r.left,144,r.right,147},red);}label(dc,names[i],{r.left+8,r.top+6,r.right-8,r.bottom},body_font,i==tab?ivory:dim,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
 label(dc,button_hint(L"Q / Shift+Tab",L"L1",L"LB"),{24,116,165,143},small_font,brass,DT_CENTER|DT_SINGLELINE);label(dc,button_hint(L"E / Tab",L"R1",L"RB"),{1045,116,1180,143},small_font,brass,DT_CENTER|DT_SINGLELINE);
 if(tab==0){label(dc,mt(cv_test_i18n::FILTER),{32,165,223,191},small_font,dim);std::wstring now=tr(ui::CURRENT);if(current_room!=~0u){cv_test::Room r{int(current_room>>16),int((current_room>>8)&255),int(current_room&255)};now+=L"  "+r.code();}label(dc,now,{667,162,1164,192},small_font,brass);}
 else{label(dc,tr(ui::LIBRARY),{32,162,900,192},small_font,dim);label(dc,std::to_wstring(entries.size())+L" "+tr(ui::FILES),{960,162,1164,192},small_font,dim,DT_RIGHT|DT_SINGLELINE);}
 for(int row=0;row<rows_visible;row++){int item=scroll+row;if(item>=count())break;int y=top+row*row_h;RECT r{28,y,630,y+102};bool chosen=item==selected;fill(dc,r,chosen?RGB(46,30,31):RGB(24,23,25));border(dc,r,chosen?brass:RGB(49,44,42));
  if(tab==0){const auto& m=maps[filtered_maps[item]];label(dc,m.code(),{48,y+13,607,y+40},code_font,ivory);label(dc,room_label(m.stage,m.number,m.variant),{48,y+46,607,y+73},small_font,dim);if(m.variant){wchar_t v[100];swprintf(v,100,mt(cv_test_i18n::EVENT),m.variant);label(dc,v,{48,y+77,607,y+98},small_font,brass);}continue;}
  if(saving&&item==0){label(dc,L"+",{50,y+27,120,y+77},title_font,brass);label(dc,tr(ui::NEW_SAVE),{159,y+15,612,y+43},body_font,ivory);label(dc,tr(ui::NEW_HINT),{159,y+50,612,y+95},small_font,dim,DT_WORDBREAK);continue;}
  const auto& e=entries[item-(saving?1:0)];picture(dc,&e,{35,y+7,191,y+95});label(dc,chars[std::min(e.character,3u)],{210,y+11,615,y+38},body_font,ivory);label(dc,room_label(e.stage,e.room,e.variant),{210,y+42,615,y+65},small_font,dim);label(dc,date(e.time),{210,y+72,615,y+95},small_font,dim);
 }
 if(count()>rows_visible){int track=rows_visible*row_h-8,bar=std::max(18,track*rows_visible/count());int y=top+(track-bar)*scroll/std::max(1,count()-rows_visible);fill(dc,{640,top,644,top+track},RGB(49,44,42));fill(dc,{640,y,644,y+bar},brass);}
 if(!count())label(dc,tr(ui::EMPTY),{40,315,615,405},body_font,dim,DT_CENTER|DT_WORDBREAK);
 const Record* e=selection();unsigned stage=0,room=0,variant=0;bool has_room=false;
 if(tab==0&&selected<int(filtered_maps.size())&&!filtered_maps.empty()){const auto& m=maps[filtered_maps[selected]];stage=m.stage;room=m.number;variant=m.variant;has_room=true;for(const auto& record:entries)if(record.legacy<0&&record.stage==stage&&record.room==room&&record.variant==variant){e=&record;break;}}
 if(e&&tab!=0){stage=e->stage;room=e->room;variant=e->variant;has_room=true;}
 picture(dc,e,{672,202,1164,479});label(dc,has_room?room_label(stage,room,variant):tr(ui::NEW_SAVE),{672,492,1164,550},body_font,ivory,DT_WORDBREAK);
 if(tab==0){label(dc,tr(ui::MAP_NOTE),{672,548,1164,589},small_font,dim,DT_WORDBREAK);label(dc,mt(cv_test_i18n::ENTRY),{672,606,836,633},small_font,dim);fill(dc,{840,600,884,635},RGB(48,39,36));fill(dc,{1020,600,1064,635},RGB(48,39,36));label(dc,L"−",{840,606,884,633},body_font,brass,DT_CENTER|DT_SINGLELINE);label(dc,std::to_wstring(spawn),{886,606,1018,633},body_font,ivory,DT_CENTER|DT_SINGLELINE);label(dc,L"+",{1020,606,1064,633},body_font,brass,DT_CENTER|DT_SINGLELINE);label(dc,L"← →",{1080,607,1164,633},small_font,dim,DT_RIGHT|DT_SINGLELINE);}
 else if(e){label(dc,std::wstring(tr(ui::CHARACTER))+L"   "+chars[std::min(e->character,3u)],{672,551,1164,575},small_font,dim);wchar_t tm[32];swprintf(tm,32,L"%02u:%02u:%02u",e->seconds/3600,e->seconds/60%60,e->seconds%60);label(dc,std::wstring(tr(ui::TIME))+L"   "+tm,{672,578,1164,601},small_font,dim);}
 else label(dc,tr(ui::NEW_HINT),{672,551,1164,610},small_font,dim,DT_WORDBREAK);
 bool can_overwrite=saving&&selection()&&selection()->legacy<0;
 if(can_overwrite){fill(dc,secondary,RGB(48,39,36));label(dc,button_hint(L"O",L"□",L"X")+L"  "+tr(ui::OVERWRITE),{848,611,1152,634},small_font,ivory,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
 bool enabled=available&&!busy&&(tab==1||count()>0);fill(dc,primary,enabled?RGB(115,29,35):RGB(43,38,39));border(dc,primary,enabled?red:RGB(61,53,50));label(dc,button_hint(saving?L"Enter / N":L"Enter",L"✕",L"A")+L"   "+action_name(),{846,657,1155,687},body_font,enabled?ivory:dim,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 label(dc,!available?tr(ui::UNAVAILABLE):status,{30,650,810,696},small_font,brass,DT_WORDBREAK);
 fill(dc,{28,709,1172,710},RGB(62,52,44));
 label(dc,button_hint(L"↑ ↓  /  PgUp PgDn",L"↑ ↓  /  L2 R2",L"↑ ↓  /  LT RT")+L"  "+tr(ui::PAGES),{30,725,425,750},small_font,dim);
 label(dc,tr(ui::HOLD),{430,725,900,750},small_font,dim);label(dc,button_hint(L"Esc",L"○",L"B")+L"  "+tr(ui::BACK),{962,725,1170,750},small_font,ivory,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS);
 if(confirming){fill(dc,{300,286,900,480},RGB(29,25,26));border(dc,{300,286,900,480},brass);label(dc,tr(ui::QUESTION),{330,310,870,390},body_font,ivory,DT_WORDBREAK);fill(dc,cancel_box,!confirm_yes?red:RGB(48,40,39));fill(dc,yes_box,confirm_yes?red:RGB(48,40,39));label(dc,tr(ui::CANCEL),{350,422,576,450},body_font,ivory,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);label(dc,tr(ui::YES),{622,422,848,450},body_font,ivory,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
 RECT client{};GetClientRect(h,&client);SetStretchBltMode(dc0,HALFTONE);StretchBlt(dc0,0,0,client.right,client.bottom,dc,0,0,W,H,SRCCOPY);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);EndPaint(h,&ps);
}
bool hit(RECT r,int x,int y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
void keyboard(WPARAM key){note_input(PAD_INPUT_KEYBOARD);if(busy)return;
 if(key==VK_ESCAPE||key==VK_F7){back();return;}if(confirming){if(key==VK_LEFT||key==VK_RIGHT||key==VK_TAB){confirm_yes=!confirm_yes;repaint();}if(key==VK_RETURN)accept();return;}
 if(key==VK_TAB||key=='Q'||key=='E'){switch_tab(tab+((key=='Q'||(key==VK_TAB&&(GetKeyState(VK_SHIFT)&0x8000)))?-1:1));return;}
 if(key==VK_UP)move(-1);if(key==VK_DOWN)move(1);if(key==VK_PRIOR)move(-rows_visible);if(key==VK_NEXT)move(rows_visible);if(key==VK_HOME)move(-count());if(key==VK_END)move(count());
 if(tab==0&&(key==VK_LEFT||key==VK_RIGHT)){spawn=std::clamp(spawn+(key==VK_LEFT?-1:1),0,255);repaint();}
 if(key==VK_RETURN)accept();if(saving&&key=='O')request_overwrite();if(saving&&key=='N'){selected=scroll=0;activate();}
 if(tab==0&&key=='F'){SetFocus(search);SendMessageW(search,EM_SETSEL,0,-1);}
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
 case WM_PAINT:paint(h);return 0;case WM_ERASEBKGND:return 1;
 case WM_CLOSE:back();return 0;
 case WM_ACTIVATE:g_pad_block=LOWORD(wp)!=WA_INACTIVE;return 0;
 case WM_SIZE:search_layout();return 0;
 case WM_KEYDOWN:keyboard(wp);return 0;
 case WM_COMMAND:if(HIWORD(wp)==EN_CHANGE&&reinterpret_cast<HWND>(lp)==search)filter_maps();return 0;
 case WM_CTLCOLOREDIT:SetDCBrushColor(reinterpret_cast<HDC>(wp),RGB(30,28,30));SetTextColor(reinterpret_cast<HDC>(wp),ivory);SetBkColor(reinterpret_cast<HDC>(wp),RGB(30,28,30));return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
 case WM_MOUSEWHEEL:note_input(PAD_INPUT_KEYBOARD);move(GET_WHEEL_DELTA_WPARAM(wp)>0?-3:3);return 0;
 case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:{note_input(PAD_INPUT_KEYBOARD);if(busy)return 0;RECT c;GetClientRect(h,&c);int x=GET_X_LPARAM(lp)*W/std::max(1L,c.right),y=GET_Y_LPARAM(lp)*H/std::max(1L,c.bottom);
  if(confirming){if(hit(cancel_box,x,y)){confirm_yes=false;accept();}else if(hit(yes_box,x,y)){confirm_yes=true;accept();}return 0;}
  SetFocus(h);if(y>=108&&y<147){if(x<170)switch_tab(tab-1);else if(x>=1028)switch_tab(tab+1);else switch_tab((x-170)/286);return 0;}
  if(x>=28&&x<644&&y>=top&&y<top+rows_visible*row_h){int n=scroll+(y-top)/row_h;if(n<count()){selected=n;repaint();if(msg==WM_LBUTTONDBLCLK){if(saving&&selection())request_overwrite();else accept();}}return 0;}
  if(hit(primary,x,y)){if(saving)selected=scroll=0;activate();}else if(saving&&hit(secondary,x,y))request_overwrite();else if(tab==0&&y>=600&&y<635&&((x>=840&&x<884)||(x>=1020&&x<1064))){spawn=std::clamp(spawn+(x<900?-1:1),0,255);repaint();}else if(y>713&&x>950)back();return 0;}
 case WM_DONE:busy=false;if(IsWindowVisible(h)){cv_focus_pause_hold(1);refresh();status=wp?tr(ui::SAVED):tr(ui::FAILED);repaint();}return 0;
 case WM_TIMER:if(painted_hint_kind!=hint_kind())repaint();if(IsWindowVisible(h)&&(GetForegroundWindow()==h||IsChild(h,GetForegroundWindow())))for(int i=0;i<PAD_PHYS_MAX;i++){
  PadPhys p{};if(!pad_phys_poll(i,&p)){previous_pad[i]=held_direction[i]=0;stick_active[i]=false;continue;}
  menu_pad(p);
  const bool motion=p.rx<64||p.rx>192||p.ry<64||p.ry>192;
  const bool new_motion=motion&&!stick_active[i];stick_active[i]=motion;
  uint32_t edge=p.buttons&~previous_pad[i];previous_pad[i]=p.buttons;
  if(cv_launcher_controls::chord_held(p.buttons))continue;if(edge||new_motion){note_input(p.kind);SetFocus(h);}if(busy)continue;
  if(edge&(1u<<PB_EAST)){back();return 0;}
  if(confirming){if(edge&((1u<<PB_LEFT)|(1u<<PB_RIGHT))){confirm_yes=!confirm_yes;repaint();}if(edge&(1u<<PB_SOUTH))accept();continue;}
  if(edge&(1u<<PB_L1))switch_tab(tab-1);if(edge&(1u<<PB_R1))switch_tab(tab+1);
  if(edge&(1u<<PB_SOUTH))accept();if(saving&&(edge&(1u<<PB_WEST)))request_overwrite();if(saving&&(edge&(1u<<PB_NORTH))){selected=scroll=0;activate();}
  if(tab==0&&(edge&((1u<<PB_LEFT)|(1u<<PB_RIGHT)))){spawn=std::clamp(spawn+((edge&(1u<<PB_LEFT))?-1:1),0,255);repaint();}
  uint32_t dir=p.buttons&((1u<<PB_UP)|(1u<<PB_DOWN)|(1u<<PB_L2)|(1u<<PB_R2));auto now=GetTickCount64();if(!dir){held_direction[i]=0;continue;}if(dir!=held_direction[i]){held_direction[i]=dir;held_since[i]=now;repeat_at[i]=0;}
  if(now>=repeat_at[i]){note_input(p.kind);int delta=(dir&((1u<<PB_UP)|(1u<<PB_L2)))?-1:1;if(dir&((1u<<PB_L2)|(1u<<PB_R2)))delta*=rows_visible;move(delta);repeat_at[i]=now+((now==held_since[i])?340:(now-held_since[i]>1100?55:115));}
 }return 0;
 case WM_DESTROY:KillTimer(h,1);notification=nullptr;window=nullptr;search=nullptr;return 0;}
 return DefWindowProcW(h,msg,wp,lp);
}

}
void cv_save_library_frame(ppu_context* ctx,bool valid){available=valid;
 Pending p;{std::lock_guard<std::mutex> lock(queue_lock);if(!have_request)return;p=std::move(queued);have_request=false;}
 if(!valid){busy=false;publish(tr(ui::UNAVAILABLE),false);return;}
 if(!mapped(state+0x8c,slot_size)||!mapped(state+0x34,common_size)){busy=false;publish(tr(ui::UNAVAILABLE),false);return;}
 if(!p.save){bool ok=valid_slot(p.data)&&restore(ctx,p.data);busy=false;publish(ok?tr(ui::LOADING):tr(ui::CORRUPT),ok);return;}
 if(!capture(p.data)){busy=false;publish(tr(ui::UNAVAILABLE),false);return;}
 const uint32_t original=vm_read32(SAVE_DATA_PTR);if(mapped(original,0x7cc0))p.data.option=vm_read32(original+0x7bac);
 if(!valid_slot(p.data)){busy=false;publish(tr(ui::UNAVAILABLE),false);return;}
 const uintptr_t id=++sequence;{std::lock_guard<std::mutex> lock(photo_lock);photo_id=0;photo.clear();}
 rsx_d3d12_request_thumbnail(thumbnail,reinterpret_cast<void*>(id));
 try{std::thread([p=std::move(p),id](){bool ok=false;try{ok=write_save(p.data,p.replace,id);}catch(...){ok=false;}
  // The menu consumes completion before accepting another write, so an old
  // completion cannot pause a newer request between its snapshot and picture.
  publish(ok?tr(ui::SAVED):tr(ui::FAILED),ok);
 }).detach();}
 catch(...){busy=false;publish(tr(ui::FAILED),false);}
}
void cv_save_library_show(bool mode,int lang){cv_archives_show(mode?1:2,lang);}
void cv_archives_show(int page,int lang,int opening_input){if(busy)return;language=std::clamp(lang,0,12);tab=std::clamp(page,0,2);saving=tab==1;selected=scroll=0;confirming=false;status.clear();refresh();
 if(!window){WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"CVSaveLibrary";wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
 window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Code: Veronica",WS_POPUP,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);if(!window)return;
 search=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,100,25,window,reinterpret_cast<HMENU>(INT_PTR(400)),wc.hInstance,nullptr);
 }
 for(HFONT* f:{&title_font,&body_font,&small_font,&code_font})if(*f){DeleteObject(*f);*f=nullptr;}
 const auto face=cv_srt_i18n::font_face(cv_srt_i18n::Language(language.load()));
 title_font=CreateFontW(-38,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,language<3||language==4||language==5||language==6||language==7||language==8||language==12?L"Georgia":face);
 body_font=CreateFontW(-20,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,face);
 small_font=CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,face);
 code_font=CreateFontW(-23,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Consolas");
 SendMessageW(search,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
 SetWindowTextW(search,L"");SendMessageW(search,EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(tr(ui::SEARCH)));
 std::fill(std::begin(tab_selected),std::end(tab_selected),0);std::fill(std::begin(tab_scroll),std::end(tab_scroll),0);
 filter_maps();if(tab==0)for(size_t i=0;i<filtered_maps.size();i++)if(maps[filtered_maps[i]].key()==current_room){selected=int(i);scroll=std::max(0,selected-rows_visible+1);break;}
 HWND game=(HWND)rsx_d3d12_window();RECT r{};if(game)GetWindowRect(game,&r);else SystemParametersInfoW(SPI_GETWORKAREA,0,&r,0);
 MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&mi);
 double scale=std::min({1.0,double(mi.rcWork.right-mi.rcWork.left-24)/W,double(mi.rcWork.bottom-mi.rcWork.top-24)/H});
 int width=int(W*scale),height=int(H*scale);int x=std::clamp(int((r.left+r.right-width)/2),int(mi.rcWork.left),int(mi.rcWork.right)-width),y=std::clamp(int((r.top+r.bottom-height)/2),int(mi.rcWork.top),int(mi.rcWork.bottom)-height);
 search_layout();SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);notification=window;cv_focus_pause_hold(1);g_pad_block=1;ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);SetTimer(window,1,16,nullptr);note_input(opening_input==PAD_KIND_NONE?pad_last_input():opening_input);GetCursorPos(&last_mouse);
 for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);menu_pad(p);previous_pad[i]=p.buttons;held_direction[i]=0;stick_active[i]=p.rx<64||p.rx>192||p.ry<64||p.ry>192;}InvalidateRect(window,nullptr,FALSE);
}
void cv_save_library_close(){if(!busy)close_window();}
bool cv_save_library_message(MSG* msg){if(!window||!IsWindowVisible(window)||(msg->hwnd!=window&&!IsChild(window,msg->hwnd)))return false;
 // Input in child edits must update hints too. Stationary/synthetic mouse
 // messages do not steal the device from the controller.
 if(msg->message==WM_KEYDOWN||msg->message==WM_SYSKEYDOWN||msg->message==WM_CHAR||msg->message==WM_LBUTTONDOWN||msg->message==WM_RBUTTONDOWN||msg->message==WM_MOUSEWHEEL)note_input(PAD_INPUT_KEYBOARD);
 if(msg->message==WM_MOUSEMOVE){POINT point{};GetCursorPos(&point);if(point.x!=last_mouse.x||point.y!=last_mouse.y){last_mouse=point;note_input(PAD_INPUT_KEYBOARD);}}
 if(msg->hwnd==search&&msg->message==WM_KEYDOWN){
  if(msg->wParam==VK_TAB||msg->wParam==VK_UP||msg->wParam==VK_DOWN||msg->wParam==VK_RETURN||msg->wParam==VK_ESCAPE||msg->wParam==VK_F7||msg->wParam==VK_PRIOR||msg->wParam==VK_NEXT){SetFocus(window);keyboard(msg->wParam);return true;}
 }
 TranslateMessage(msg);DispatchMessageW(msg);return true;}
bool cv_archives_visible(){return window&&IsWindowVisible(window);}
void cv_archives_maps(const std::vector<cv_test::Room>& rooms,unsigned current,bool (*travel)(unsigned,unsigned)){maps=rooms;current_room=current;travel_to=travel;}

#endif
