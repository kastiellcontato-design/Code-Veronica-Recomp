#define NOMINMAX
#include "cv_trophy_popup.h"
#include "cv_trophy_i18n.h"
#include <windows.h>
#include <mmsystem.h>
#include <gdiplus.h>
#include <objidl.h>
#include <deque>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"winmm.lib")
extern "C" const rsx_ui_sprite* (*g_rsx_trophy_hook)(uint32_t,uint32_t,const uint32_t**,uint32_t*,uint32_t*,uint64_t*);
namespace cv_trophy_popup {
namespace {
using namespace Gdiplus;
constexpr unsigned card_w=448,card_h=140;
constexpr uint64_t enter_ms=280,hold_ms=5000,exit_ms=300,total_ms=enter_ms+hold_ms+exit_ms;
struct Card {std::vector<uint32_t> rgba;uint64_t generation=0;};
struct State {
 std::mutex lock;std::deque<std::shared_ptr<Card>> queue;std::shared_ptr<Card> active;
 std::atomic<int> position{1};std::atomic<bool> show{true},work{false};std::atomic<uint64_t> canceled{0};uint64_t observed_cancel=0,start=0,next_generation=1;
 ULONG_PTR gdip=0;HANDLE sound_event=nullptr;void(*sound_observer)()=nullptr;rsx_ui_sprite sprite{};
};
// The renderer and optional sound thread live for the lifetime of the process.
State& state(){static State* instance=new State;return *instance;}
DWORD WINAPI sound_thread(void* parameter){
 auto& s=*static_cast<State*>(parameter);
 while(WaitForSingleObject(s.sound_event,INFINITE)==WAIT_OBJECT_0)
  if(s.show.load())PlaySoundW(L"TROPHY_SOUND",GetModuleHandleW(nullptr),SND_RESOURCE|SND_ASYNC|SND_NODEFAULT);
 return 0;
}
const wchar_t* font(int language){return language==3?L"Yu Gothic UI":language==10?L"Microsoft YaHei UI":language==11?L"Malgun Gothic":L"Segoe UI";}
void draw_text(Graphics& g,const wchar_t* value,Font& font,const RectF& rect,Color color,bool rtl){
 StringFormat format;format.SetTrimming(StringTrimmingEllipsisWord);format.SetLineAlignment(StringAlignmentNear);
 if(rtl)format.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
 SolidBrush brush(color);g.DrawString(value,-1,&font,rect,&format,&brush);
}
std::shared_ptr<Card> prepare(const cv_trophies::Trophy& trophy,int language){
 auto& s=state();if(!s.gdip)return {};
 Bitmap bitmap(card_w,card_h,PixelFormat32bppARGB);Graphics g(&bitmap);
 g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(Color(0,0,0,0));
 GraphicsPath box;box.AddArc(0.f,0.f,18.f,18.f,180.f,90.f);box.AddArc(float(card_w-19),0.f,18.f,18.f,270.f,90.f);box.AddArc(float(card_w-19),float(card_h-19),18.f,18.f,0.f,90.f);box.AddArc(0.f,float(card_h-19),18.f,18.f,90.f,90.f);box.CloseFigure();
 SolidBrush bg(Color(248,24,23,24));Pen border(Color(255,132,105,65),1.f);g.FillPath(&bg,&box);g.DrawPath(&border,&box);
 if(!trophy.icon.empty()){
  HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,trophy.icon.size());if(block){void* p=GlobalLock(block);
   if(p){memcpy(p,trophy.icon.data(),trophy.icon.size());GlobalUnlock(block);IStream* input=nullptr;
    if(SUCCEEDED(CreateStreamOnHGlobal(block,TRUE,&input))){Bitmap icon(input);if(icon.GetLastStatus()==Ok){g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.DrawImage(&icon,RectF(14,28,82,82));}input->Release();}else GlobalFree(block);
   }else GlobalFree(block);
  }
 }
 FontFamily face(font(language));Font heading(&face,11,FontStyleBold,UnitPixel),title(&face,15,FontStyleBold,UnitPixel),description(&face,12,FontStyleRegular,UnitPixel),grade(&face,11,FontStyleRegular,UnitPixel);
 const bool rtl=language==9;const float x=110,w=card_w-x-16;
 draw_text(g,cv_trophy_text::get(language,cv_trophy_text::Toast),heading,RectF(x,10,w,18),Color(255,213,177,106),rtl);
 draw_text(g,trophy.name.c_str(),title,RectF(x,32,w,40),Color(255,239,230,210),rtl);
 draw_text(g,trophy.description.c_str(),description,RectF(x,75,w,32),Color(255,185,166,137),rtl);
 auto label=trophy.grade==1?cv_trophy_text::Platinum:trophy.grade==2?cv_trophy_text::Gold:trophy.grade==3?cv_trophy_text::Silver:cv_trophy_text::Bronze;
 Color tier=trophy.grade==1?Color(255,158,213,229):trophy.grade==2?Color(255,230,191,97):trophy.grade==3?Color(255,199,205,214):Color(255,203,150,104);
 SolidBrush dot(tier);g.FillEllipse(&dot,x,119.f,6.f,6.f);
 draw_text(g,cv_trophy_text::get(language,label),grade,RectF(x+14,113,w-14,22),tier,rtl);
 g.Flush(FlushIntentionSync);BitmapData pixels{};Rect rect(0,0,card_w,card_h);if(bitmap.LockBits(&rect,ImageLockModeRead,PixelFormat32bppARGB,&pixels)!=Ok)return {};
 auto card=std::make_shared<Card>();card->rgba.resize(card_w*card_h);
 for(unsigned y=0;y<card_h;y++){auto* src=reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(pixels.Scan0)+ptrdiff_t(y)*pixels.Stride);
  for(unsigned x=0;x<card_w;x++){uint32_t c=src[x];card->rgba[y*card_w+x]=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);}}
 bitmap.UnlockBits(&pixels);return card;
}
const rsx_ui_sprite* frame(uint32_t w,uint32_t h,const uint32_t** rgba,uint32_t* aw,uint32_t* ah,uint64_t* generation){return frame_at(w,h,GetTickCount64(),rgba,aw,ah,generation);}
float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
}
void initialize(){
 static std::once_flag initialized;std::call_once(initialized,[]{auto& s=state();GdiplusStartupInput input;GdiplusStartup(&s.gdip,&input,nullptr);
  s.sound_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(s.sound_event){HANDLE thread=CreateThread(nullptr,0,sound_thread,&s,0,nullptr);if(thread)CloseHandle(thread);else{CloseHandle(s.sound_event);s.sound_event=nullptr;}}
  g_rsx_trophy_hook=frame;
 });
}
void corner(int position){state().position=std::clamp(position,0,3);}
void enabled(bool show){auto& s=state();s.show=show;if(!show){std::lock_guard<std::mutex> guard(s.lock);s.queue.clear();s.canceled.fetch_add(1);s.work=false;}}
void enqueue(const cv_trophies::Trophy& trophy,int language){
 initialize();auto& s=state();if(!s.show.load())return;auto card=prepare(trophy,language);if(!card)return;
 std::lock_guard<std::mutex> guard(s.lock);if(!s.show.load()||s.queue.size()>=128)return;card->generation=s.next_generation++;s.queue.push_back(std::move(card));s.work=true;
}
const rsx_ui_sprite* frame_at(uint32_t w,uint32_t h,uint64_t now,const uint32_t** rgba,uint32_t* aw,uint32_t* ah,uint64_t* generation){
 *rgba=nullptr;*aw=*ah=0;*generation=0;auto& s=state();if(!s.show.load()||!s.work.load()||!w||!h)return nullptr;
 std::lock_guard<std::mutex> guard(s.lock);
 if(s.observed_cancel!=s.canceled.load()){s.active.reset();s.observed_cancel=s.canceled.load();}
 if(s.active&&now-s.start>=total_ms)s.active.reset();
 if(!s.active){if(s.queue.empty()){s.work=false;return nullptr;}s.active=s.queue.front();s.queue.pop_front();s.start=now;
  if(s.sound_observer)s.sound_observer();else if(s.sound_event)SetEvent(s.sound_event);
 }
 const float scale=std::min(std::clamp(float(h)/1080,.65f,1.5f),float(w-std::min(w,24u))/card_w);
 const uint64_t age=now-s.start;float alpha=age<enter_ms?smooth(float(age)/enter_ms):age>enter_ms+hold_ms?1-smooth(float(age-enter_ms-hold_ms)/exit_ms):1;
 const float margin=20*scale;
 const int position=s.position.load();const bool right=(position&1)!=0,bottom=(position&2)!=0;
 const float x=right?float(w)-card_w*scale-margin:margin;const float y=bottom?float(h)-card_h*scale-margin:margin;
 s.sprite={x+(right?1.f:-1.f)*(1-alpha)*78*scale,y,card_w*scale,card_h*scale,0,0,1,1,(uint32_t(std::lround(alpha*255))<<24)|0xffffff,0};
 *rgba=s.active->rgba.data();*aw=card_w;*ah=card_h;*generation=s.active->generation;return &s.sprite;
}
void sound_observer_for_test(void(*observer)()){auto& s=state();std::lock_guard<std::mutex> guard(s.lock);s.sound_observer=observer;}
}
