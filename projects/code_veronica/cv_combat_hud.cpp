#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <tuple>
#include <filesystem>
#include "cv_combat_hud.h"
#include "cv_combat_hud_model.h"
#include "cv_srt_icons.h"
#include "cv_items.h"
#include "cv_version.h"
#include "cv_srt_i18n.h"
extern "C" int cv_hud_language(void);
#pragma comment(lib,"gdiplus.lib")
extern "C" uint32_t cv_combat_hud_pad(void);
extern "C" uint32_t vm_read32(uint64_t);
extern "C" uint16_t vm_read16(uint64_t);
extern "C" uint8_t vm_read8(uint64_t);
extern "C" int g_pad_block;
extern "C" int cv_pc_settings_read(void);
using namespace Gdiplus;
namespace cv_combat_hud {
std::atomic<int> enabled{0};std::atomic<unsigned> settings_generation{0};
cv_srt_icons::Library icons;
std::map<int,std::unique_ptr<Bitmap>> weapons;
void init(){static ULONG_PTR token=0;if(!token){GdiplusStartupInput input;GdiplusStartup(&token,&input,nullptr);}}
Bitmap* icon(int id){
 auto it=weapons.find(id);if(it!=weapons.end())return it->second.get();
 // HUD-only art. Inventory and SRT continue using the original game atlas.
 std::filesystem::path directory;
 wchar_t custom[MAX_PATH]{};
 if(GetEnvironmentVariableW(L"CV_HUD_ICON_ROOT",custom,MAX_PATH)>0)directory=custom;
 else {wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);directory=std::filesystem::path(executable).parent_path()/L"assets"/L"hud-weapons";if(!std::filesystem::exists(directory))directory=std::filesystem::path(executable).parent_path().parent_path()/L"assets"/L"hud-weapons";}
 const auto file=directory/(std::to_wstring(id)+L".png");
 if(std::filesystem::exists(file)){
  Bitmap art(file.c_str());
  if(art.GetLastStatus()==Ok&&art.GetWidth()>0&&art.GetHeight()>0&&art.GetWidth()<=4096&&art.GetHeight()<=4096){
   Rect r(0,0,art.GetWidth(),art.GetHeight());BitmapData data{};
   if(art.LockBits(&r,ImageLockModeRead,PixelFormat32bppARGB,&data)==Ok){
    int left=r.Width,top=r.Height,right=0,bottom=0;
    for(int y=0;y<r.Height;y++){const auto* row=(const uint32_t*)((const uint8_t*)data.Scan0+y*data.Stride);for(int x=0;x<r.Width;x++)if((row[x]>>24)>8){left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);}}
    art.UnlockBits(&data);
    if(right>left&&bottom>top){
     // Resample once when this weapon is first used, not on each health change.
     const float fit=std::min(512.f/(right-left),384.f/(bottom-top));
     auto cropped=std::make_unique<Bitmap>(std::max(1,int(std::ceil((right-left)*fit))),std::max(1,int(std::ceil((bottom-top)*fit))),PixelFormat32bppARGB);
     {Graphics copy(cropped.get());copy.Clear(Color(0,0,0,0));copy.SetInterpolationMode(InterpolationModeHighQualityBicubic);copy.DrawImage(&art,Rect(0,0,cropped->GetWidth(),cropped->GetHeight()),left,top,right-left,bottom-top,UnitPixel);}
     if(cropped->GetLastStatus()==Ok)return weapons.emplace(id,std::move(cropped)).first->second.get();
    }
   }
  }
 }
 const auto* source=icons.get(id);if(!source)return nullptr;
 auto out=std::make_unique<Bitmap>(source->width,source->height,PixelFormat32bppARGB);
 Rect r(0,0,source->width,source->height);BitmapData pixels{};
 if(out->LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&pixels)!=Ok)return nullptr;
 for(int y=0;y<source->height;y++){
  auto* row=(uint32_t*)((uint8_t*)pixels.Scan0+y*pixels.Stride);
  for(int x=0;x<source->width;x++){
   const auto c=source->argb[size_t(y)*source->width+x];const unsigned luminance=(((c>>16)&255)*54+((c>>8)&255)*183+(c&255)*19)/256;
   const unsigned gray=72+luminance*183/255;row[x]=(c&0xff000000)|(gray<<16)|(gray<<8)|gray;
  }
 }
 out->UnlockBits(&pixels);return weapons.emplace(id,std::move(out)).first->second.get();
}
Color life_color(int hp,int maximum,int condition){
 switch(health_condition(maximum>0?hp:0,condition)){
 case SRT_FINE:return Color(255,147,203,158);case SRT_CAUTION:return Color(255,222,190,104);case SRT_WARNING:return Color(255,220,145,78);
 case SRT_POISON:return Color(255,139,164,238);case SRT_GAS:return Color(255,176,165,191);default:return Color(255,224,90,82);
 }
}
int health_word(int hp,int maximum,int condition){
 switch(health_condition(maximum>0?hp:0,condition)){
 case SRT_FINE:return 7;case SRT_CAUTION:case SRT_WARNING:return 8;case SRT_DANGER:return 9;case SRT_POISON:return 10;case SRT_GAS:return 11;default:return 12;
 }
}
void health_label(Graphics& g,int hp,int maximum,int condition,int language,Color color){
 const auto lang=cv_srt_i18n::Language(std::clamp(language,0,12));const int word=health_word(hp,maximum,condition);const auto label=lang==cv_srt_i18n::PT&&word==8?std::wstring(L"Cuidado"):cv_srt_i18n::labels(lang)[word];
 FontFamily family(cv_srt_i18n::font_face(lang,true));StringFormat format(StringFormat::GenericTypographic());
 format.SetFormatFlags(StringFormatFlagsNoWrap|(cv_srt_i18n::rtl_text(label)?StringFormatFlagsDirectionRightToLeft:0));
 format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);
 Font measure_font(&family,14,FontStyleRegular,UnitPixel);RectF measured;g.MeasureString(label.c_str(),-1,&measure_font,PointF(0,0),&format,&measured);
 const float fit=std::min(1.f,std::min(82.f/std::max(1.f,measured.Width),20.f/std::max(1.f,measured.Height)));
 Font font(&family,14*fit,FontStyleRegular,UnitPixel);SolidBrush shadow(Color(220,0,0,0)),ink(color);
 const auto saved=g.Save();g.SetClip(RectF(43,69,86,30),CombineModeIntersect);
 g.DrawString(label.c_str(),-1,&font,RectF(46,73,82,26),&format,&shadow);
 g.DrawString(label.c_str(),-1,&font,RectF(45,72,82,26),&format,&ink);g.Restore(saved);
}
std::unique_ptr<Bitmap> render(int hp,int maximum,const Weapon& w,float scale=1,int layer=0,int condition=SRT_FINE,int language=0){
 init();auto image=std::make_unique<Bitmap>(int(std::ceil(240*scale)),int(std::ceil(190*scale)),PixelFormat32bppARGB);
 Graphics g(image.get());g.Clear(Color(0,0,0,0));g.ScaleTransform(scale,scale);
 g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHalf);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
 const float ratio=maximum>0?std::clamp(float(hp)/maximum,0.f,1.f):0;
 Color health=life_color(hp,maximum,condition);
 if(layer!=2){
 auto arc=[&](Color c,float thickness,float radius,float start,float sweep){Pen p(c,thickness);p.SetStartCap(LineCapFlat);p.SetEndCap(LineCapFlat);g.DrawArc(&p,86-radius,91-radius,radius*2,radius*2,start,sweep);};
 if(layer!=3){
 SolidBrush shade(Color(48,0,0,0));g.FillEllipse(&shade,17.f,20.f,144.f,144.f);
 arc(Color(170,0,0,0),5,72,56,298);
 arc(Color(190,168,175,171),1.1f,72,55,299);
 arc(Color(80,172,185,177),.7f,67,67,288);
 arc(Color(190,20,32,27),7,53,90,270);
 arc(Color(130,62,87,71),1,57,90,270);
 }
 if(layer!=1&&ratio>0){arc(Color(38,health.GetR(),health.GetG(),health.GetB()),11,53,90,270*ratio);arc(health,5,53,90,270*ratio);arc(Color(100,223,247,225),1,51,90,270*ratio);}
 if(layer!=3){
 // Short, subtle ring ticks; no melee icon or auxiliary lower bar.
 for(int i=0;i<4;i++){float a=(106+i*63)*3.14159265f/180.f;Pen tick(Color(110,123,138,130),.7f);g.DrawLine(&tick,86+std::cos(a)*65,91+std::sin(a)*65,86+std::cos(a)*70,91+std::sin(a)*70);}
 if(auto* weapon_image=icon(w.id)){
  const float factor=std::min(101.f/weapon_image->GetWidth(),75.f/weapon_image->GetHeight());
  const float iw=weapon_image->GetWidth()*factor,ih=weapon_image->GetHeight()*factor;
  const RectF box(111+(101-iw)*.5f,91+(75-ih)*.5f,iw,ih);
  g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.DrawImage(weapon_image,box);
 }
 else if(w.id){Pen fallback(Color(190,210,216,211),1);g.DrawLine(&fallback,133,116,186,116);}
 }
 }
 if(layer==0||layer==3)health_label(g,hp,maximum,condition,language,health);
 if((layer==0||layer==2)&&w.id&&w.id!=8&&w.id!=0x37){
  const std::wstring loaded=w.infinite?L"∞":std::to_wstring(std::max(0,w.loaded))+(w.percent?L"%":L"");
  const std::wstring reserve=(w.id==1||w.id==11||w.id==0x21)?L"—":std::to_wstring(w.reserve)+(w.percent?L"%":L"");
  FontFamily family(L"Bahnschrift");Font large(&family,25,FontStyleRegular,UnitPixel),reserve_font(&family,15,FontStyleRegular,UnitPixel);
  StringFormat right;right.SetAlignment(StringAlignmentFar);right.SetFormatFlags(StringFormatFlagsNoWrap);
  SolidBrush shadow(Color(220,0,0,0)),front(w.loaded==0&&!w.infinite?Color(255,224,110,96):Color(255,195,229,196)),rear(Color(230,159,170,162));
  g.DrawString(loaded.c_str(),-1,&large,RectF(138,40,80,34),&right,&shadow);
  g.DrawString(loaded.c_str(),-1,&large,RectF(137,39,80,34),&right,&front);
  g.DrawString(reserve.c_str(),-1,&reserve_font,RectF(168,72,49,24),&right,&rear);
 }
 return image;
}
struct DamagePixel {rsx_ui_quad quad;float ratio;};
std::vector<DamagePixel> damage_ring(float scale){
 init();Bitmap image(int(std::ceil(240*scale)),int(std::ceil(190*scale)),PixelFormat32bppARGB);
 {Graphics g(&image);g.Clear(Color(0,0,0,0));g.ScaleTransform(scale,scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHalf);Pen p(Color(255,235,87,78),5);p.SetStartCap(LineCapFlat);p.SetEndCap(LineCapFlat);g.DrawArc(&p,33.f,38.f,106.f,106.f,90.f,270.f);}
 Rect r(0,0,image.GetWidth(),image.GetHeight());BitmapData data{};std::vector<DamagePixel> result;
 if(image.LockBits(&r,ImageLockModeRead,PixelFormat32bppARGB,&data)!=Ok)return result;
 for(int y=0;y<r.Height;y++){const auto* row=(const uint32_t*)((const uint8_t*)data.Scan0+y*data.Stride);for(int x=0;x<r.Width;x++)if(row[x]>>24){float a=std::atan2((y+.5f)/scale-91,(x+.5f)/scale-86)*180/3.14159265f-90;if(a<0)a+=360;result.push_back({{float(x),float(y),1,1,row[x],0},a/270});}}
 image.UnlockBits(&data);return result;
}
void raster(Bitmap& image,std::vector<rsx_ui_quad>& output,size_t budget=10000){
 Rect r(0,0,image.GetWidth(),image.GetHeight());BitmapData data{};output.clear();
 if(image.LockBits(&r,ImageLockModeRead,PixelFormat32bppARGB,&data)!=Ok)return;
 struct Run{int x,end;uint32_t color;size_t index;};std::vector<Run> old,row;
 for(unsigned step=4;step<=256;step*=2){
  old.clear();output.clear();
  auto color=[&](uint32_t c){if(!(c>>24))return uint32_t(0);uint32_t out=0;for(int shift=0;shift<32;shift+=8){unsigned v=(c>>shift)&255;v=std::min(255u,(v+step/2)/step*step);out|=v<<shift;}return out;};
  for(int y=0;y<r.Height;y++){
   const auto* p=(const uint32_t*)((const uint8_t*)data.Scan0+y*data.Stride);row.clear();size_t search=0;
   for(int x=0;x<r.Width;){auto c=color(p[x]);int end=x+1;while(end<r.Width&&color(p[end])==c)end++;
    if(c>>24){while(search<old.size()&&old[search].x<x)search++;size_t index=output.size();
     if(search<old.size()&&old[search].x==x&&old[search].end==end&&old[search].color==c){index=old[search].index;output[index].h++;}
     else output.push_back({float(x),float(y),float(end-x),1,c,0});row.push_back({x,end,c,index});}x=end;}
   old.swap(row);
  }
  if(output.size()<=budget)break;
 }
 image.UnlockBits(&data);
}
cv_srt_state sample(){
 cv_srt_state s{};const auto inventory=cv_items::read();if(!inventory.valid||!inventory.playing)return s;
 s.valid=1;s.character=inventory.character;s.equipped=inventory.equipped;
 s.hp=int32_t(vm_read32(CV_SRT_HEALTH));
 if(s.hp<=0||s.hp>10000)return {};
 const bool easy=vm_read8(CV_SRT_DIFFICULTY)==2;
#ifdef CV_REGION_JP
 s.maximum=easy?400:200;
#else
 s.maximum=easy?320:160;
#endif
 s.room=vm_read16(CV_SRT_ROOM);
 const unsigned status=vm_read8(CV_SRT_STATUS);s.condition=(status&0x20)?SRT_GAS:(status&8)?SRT_POISON:SRT_FINE;
 for(int i=0;i<CV_SRT_INVENTORY_SIZE;i++){const uint32_t v=inventory.entry[i];s.inventory[i]={int((v>>16)&255),int(v&65535),0,1,(v&0x08000000u)!=0};}
 if(vm_read8(CV_SRT_CHARACTER)!=s.character)return {};
 return s;
}
}
extern "C" void cv_combat_hud_set(int value){const int next=value!=0;if(cv_combat_hud::enabled.exchange(next)!=next){cv_combat_hud::settings_generation++;fprintf(stderr,"[COMBAT-HUD] %s; hide after 5s idle + 0.6s fade; no guest writes\n",next?"on":"off");}}
extern "C" int cv_combat_hud_get(){return cv_combat_hud::enabled.load();}
static const rsx_ui_quad* cv_combat_hud_base_quads(uint32_t width,uint32_t height,uint32_t* count){
 uint32_t n=0;const auto* normal=cv_srt_quads(width,height,&n);*count=n;
 using namespace cv_combat_hud;
 static Visibility visibility;static HealthTrail trail;static cv_srt_state state{};static uint64_t sampled=0;static unsigned config=~0u;
 static std::vector<rsx_ui_quad> cached,life,numbers,combined;static Weapon previous;static int hp=-1,maximum=-1;static float previous_scale=0;
 using NumberKey=std::tuple<int,int,int,bool,bool,int>;
 static std::map<NumberKey,std::vector<rsx_ui_quad>> number_cache;
 static std::vector<DamagePixel> damage;static int condition=-1,previous_language=-1,observed_condition=-1;
 static float previous_draw_scale=0;static int resolution=2;static uint64_t settings_read=0;
 if(!enabled){visibility.reset();trail.reset();return normal;}
 if(settings_generation.load()!=config){visibility.reset();trail.reset();}
 const uint64_t now=GetTickCount64();
 if(now-sampled>=33||!sampled){state=sample();sampled=now;}
 const auto gun=weapon(state);const auto bits=cv_combat_hud_pad();
 if(state.valid&&observed_condition!=state.condition){visibility.reset();observed_condition=state.condition;}
 const float opacity=visibility.update(true,state.valid&&!g_pad_block,(bits&0x0a00)!=0,state.character,state.room,gun,state.hp,now);
 const float damage_end=trail.update(state.valid&&!g_pad_block,state.character,state.room,state.hp,state.maximum,now);
 if(opacity<=0||width<320||height<240)return normal;
 if(!settings_read||now-settings_read>=250){resolution=(cv_pc_settings_read()>>4)&15;settings_read=now;}
 const auto placement=layout(width,height);const float scale=placement.scale,draw_scale=std::min(scale,resolution_scale(resolution));
 const bool size_changed=scale!=previous_scale||draw_scale!=previous_draw_scale;
 // Render at the actual target resolution; increase detail budget with area.
 const size_t budget=size_t(std::clamp(10000.f*draw_scale*draw_scale,10000.f,48000.f));
 const unsigned gen=settings_generation.load();const int language=std::clamp(cv_hud_language(),0,12);
 const bool base_changed=gen!=config||gun.id!=previous.id||size_changed;
 if(base_changed){
  if(const wchar_t* root=_wgetenv(L"PS3_VFS_ROOT")){icons.load(root);}
  else if(const char* root=getenv("PS3_VFS_ROOT")){int len=MultiByteToWideChar(CP_UTF8,0,root,-1,nullptr,0);std::wstring w(len,0);MultiByteToWideChar(CP_UTF8,0,root,-1,w.data(),len);if(len)w.pop_back();icons.load(w);}
  if(damage.empty()||size_changed)damage=damage_ring(draw_scale);
  const auto started=std::chrono::steady_clock::now();auto image=render(state.hp,state.maximum,gun,draw_scale,1,state.condition);raster(*image,cached,budget);
  if(getenv("CV_HUD_PROFILE"))fprintf(stderr,"[HUD-BUILD] layer=life-weapon %.3fms quads=%zu hp=%d/%d weapon=%d\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),cached.size(),state.hp,state.maximum,gun.id);
 }
 if(gen!=config||hp!=state.hp||maximum!=state.maximum||size_changed||condition!=state.condition||language!=previous_language){auto image=render(state.hp,state.maximum,gun,draw_scale,3,state.condition,language);raster(*image,life,budget);}
 if(gen!=config||gun!=previous||size_changed){
  const NumberKey key{gun.id,gun.infinite?0:gun.loaded,gun.reserve,gun.infinite,gun.percent,int(draw_scale*1000)};
  const auto started=std::chrono::steady_clock::now();auto found=number_cache.find(key);
  if(found==number_cache.end()){
   if(number_cache.size()>=64)number_cache.clear();
   auto image=render(state.hp,state.maximum,gun,draw_scale,2);std::vector<rsx_ui_quad> glyphs;raster(*image,glyphs);
   found=number_cache.emplace(key,std::move(glyphs)).first;
  }
  numbers=found->second;
  if(getenv("CV_HUD_PROFILE"))fprintf(stderr,"[HUD-BUILD] layer=ammo %.3fms quads=%zu loaded=%d reserve=%d\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),numbers.size(),gun.loaded,gun.reserve);
 }
 previous=gun;hp=state.hp;maximum=state.maximum;condition=state.condition;previous_language=language;previous_scale=scale;previous_draw_scale=draw_scale;config=gen;
 combined.clear();if(normal&&n)combined.insert(combined.end(),normal,normal+n);
 const float x=placement.x,y=placement.y;
 for(const auto* layer:{&cached,&life,&numbers})for(auto q:*layer){const float factor=scale/draw_scale;q.x=x+q.x*factor;q.y=y+q.y*factor;q.w*=factor;q.h*=factor;q.argb=(q.argb&0xffffffu)|(uint32_t(std::lround((q.argb>>24)*opacity))<<24);combined.push_back(q);}
 const float health_ratio=state.maximum>0?std::clamp(float(state.hp)/state.maximum,0.f,1.f):0;
 // Bright moving core along the remaining life. Cached pixel coverage
 // keeps the sweep animated without rebuilding the ring each frame.
 for(const auto& pixel:damage)if(pixel.ratio<=health_ratio){const float shine=life_sweep(pixel.ratio,health_ratio,now);if(shine<.01f)continue;auto q=pixel.quad;const float factor=scale/draw_scale;q.x=x+q.x*factor;q.y=y+q.y*factor;q.w*=factor;q.h*=factor;q.argb=(uint32_t(std::lround((q.argb>>24)*opacity*shine*245/255.f))<<24)|0xfffff2;combined.push_back(q);}
 if(damage_end>health_ratio+.001f)for(const auto& pixel:damage)if(pixel.ratio>health_ratio&&pixel.ratio<=damage_end){auto q=pixel.quad;const float factor=scale/draw_scale;q.x=x+q.x*factor;q.y=y+q.y*factor;q.w*=factor;q.h*=factor;q.argb=(q.argb&0xffffffu)|(uint32_t(std::lround((q.argb>>24)*opacity))<<24);combined.push_back(q);}
 // Respect the backend's shared UI capacity, retaining original SRT first.
 if(combined.size()>131072)combined.resize(131072);
 *count=uint32_t(combined.size());return combined.data();
}

// Four-slot cross, original vector/GDI+ design using our HUD-only weapon art.
#include "cv_quick_select.h"
#include "quick_edit_text.h"
namespace cv_combat_hud {
#include "quick_edit_render.cpp.inc"
std::unique_ptr<Bitmap> render_quick(const cv_quick::View& menu,float scale,int language=-1){
 language=std::clamp(language<0?cv_hud_language():language,0,12);
 init();auto image=std::make_unique<Bitmap>(int(std::ceil(362*scale)),int(std::ceil(254*scale)),PixelFormat32bppARGB);
 Graphics g(image.get());g.Clear(Color(0,0,0,0));g.ScaleTransform(scale,scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
 if(menu.editing){draw_quick_picker(g,menu,language);quick_edit_tip(g,menu,language);return image;}
 const float xs[]={125,248,125,2},ys[]={2,84,166,84};
 FontFamily family(L"Bahnschrift");Font font(&family,16,FontStyleRegular,UnitPixel),hint_font(&family,11,FontStyleRegular,UnitPixel);StringFormat right_align;right_align.SetAlignment(StringAlignmentFar);StringFormat center;center.SetAlignment(StringAlignmentCenter);center.SetLineAlignment(StringAlignmentCenter);
 cv_srt_state state{};for(int i=0;i<cv_items::kEntries;i++)state.inventory[i]={menu.inventory.id(i),menu.inventory.quantity(i),0,1,(menu.inventory.entry[i]&0x08000000)!=0};
 for(int d=0;d<4;d++){
  const int slot=menu.slots[d];const bool active=menu.selected==d,available=slot>=0;const float x=xs[d],y=ys[d];RectF box(x,y,112,66);
  if(active)for(int k=8;k>=1;k--){Pen glow(Color(BYTE(4+(8-k)*2),218,236,225),float(k));g.DrawRectangle(&glow,box);}
  LinearGradientBrush background(PointF(x,y),PointF(x,y+66),active?Color(205,109,119,113):Color(155,29,32,31),Color(active?205:150,8,11,10));g.FillRectangle(&background,box);
  Pen border(available?(active?Color(245,220,236,224):Color(120,116,127,120)):Color(65,102,110,104),active?1.3f:.8f);g.DrawRectangle(&border,box);
  int id=available?menu.inventory.id(slot):menu.wanted[d];
  if(auto* art=icon(id)){
   const bool icon_only=id==8||id==55;
   const float fit=std::min(88.f/art->GetWidth(),(icon_only?40.f:30.f)/art->GetHeight());
   const float iw=art->GetWidth()*fit,ih=art->GetHeight()*fit;
   RectF area(x+(112-iw)/2,y+33.f-ih/2,iw,ih);
   if(available)g.DrawImage(art,area);else{ColorMatrix matrix={1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,.18f,0,0,0,0,0,1};ImageAttributes attrs;attrs.SetColorMatrix(&matrix);g.DrawImage(art,area,0,0,float(art->GetWidth()),float(art->GetHeight()),UnitPixel,&attrs);Pen slash(Color(100,130,137,131),1);g.DrawLine(&slash,x+35,y+52,x+76,y+11);}
  }
  if(available&&id!=8&&id!=55){state.equipped=slot+1;const auto gun=weapon(state);SolidBrush ammo(active?Color(255,192,229,192):Color(225,181,198,183));const auto amount=(gun.infinite?std::wstring(L"∞"):std::to_wstring(gun.loaded)) + L"/" + std::to_wstring(gun.reserve);g.DrawString(amount.c_str(),-1,&font,RectF(x+8,y+48,96,18),&right_align,&ammo);}
  SolidBrush direction(Color(155,183,192,185));const wchar_t* arrows[]={L"↑",L"→",L"↓",L"←"};g.DrawString(arrows[d],-1,&hint_font,PointF(x+5,y+3),&direction);
 }
 // Right-stick hint; keyboard users see their configured four keys.
 Pen ring(Color(200,166,183,170),1.2f);g.DrawEllipse(&ring,166.f,102.f,30.f,30.f);SolidBrush hint(Color(225,206,217,208));
 // Hand-drawn R: continuous strokes, independent of variable-font outlines.
 Pen letter_ink(Color(240,206,217,208),1.6f);letter_ink.SetStartCap(LineCapRound);letter_ink.SetEndCap(LineCapRound);letter_ink.SetLineJoin(LineJoinRound);
 g.DrawLine(&letter_ink,177.5f,111.f,177.5f,123.f);
 GraphicsPath bowl;bowl.AddLine(177.5f,111.f,181.f,111.f);bowl.AddBezier(181.f,111.f,185.6f,111.f,185.6f,117.f,181.f,117.f);bowl.AddLine(181.f,117.f,177.5f,117.f);g.DrawPath(&letter_ink,&bowl);
 g.DrawLine(&letter_ink,180.8f,117.f,184.5f,123.f);
 const float key_x[]={181,220,181,142},key_y[]={85,117,149,117};
 for(int d=0;d<4;d++){
  int k=menu.config.keys[d];wchar_t name[48]{};LONG scan=LONG(MapVirtualKeyW(k,MAPVK_VK_TO_VSC)<<16);if(k>=VK_PRIOR&&k<=VK_DOWN)scan|=1<<24;
  std::wstring label=k&&GetKeyNameTextW(scan,name,48)?name:L"—";
  GraphicsPath glyph;glyph.AddString(label.c_str(),-1,&family,FontStyleRegular,11,PointF(0,0),StringFormat::GenericTypographic());RectF visible;glyph.GetBounds(&visible);
  if(visible.Width>0&&visible.Height>0){const float fit=std::min(1.f,std::min(44.f/visible.Width,16.f/visible.Height));Matrix transform(fit,0,0,fit,key_x[d]-(visible.X+visible.Width/2)*fit,key_y[d]-(visible.Y+visible.Height/2)*fit);glyph.Transform(&transform);g.FillPath(&hint,&glyph);}
 }
 if(menu.selected>=0)quick_edit_tip(g,menu,language);
 return image;
}
}
extern "C" const rsx_ui_quad* cv_combat_hud_quads(uint32_t width,uint32_t height,uint32_t* count){
 uint32_t n=0;const auto* base=cv_combat_hud_base_quads(width,height,&n);*count=n;
 const auto menu=cv_quick::view();const uint64_t now=GetTickCount64();
 if(g_pad_block||width<320||height<240||(!menu.visible&&(!menu.closed||now-menu.closed>=220)))return base;
 float alpha=menu.visible?std::min(1.f,float(now-menu.opened)/90.f):std::max(0.f,1.f-float(now-menu.closed)/220.f);alpha=alpha*alpha*(3-2*alpha);
 using namespace cv_combat_hud;const auto place=layout(width,height);const float scale=place.scale;
 static int resolution=2;static uint64_t settings_read=0;if(!settings_read||now-settings_read>=250){resolution=(cv_pc_settings_read()>>4)&15;settings_read=now;}
 const float draw_scale=std::min(scale,resolution_scale(resolution));
 static std::vector<rsx_ui_quad> cached,combined;static std::vector<int> previous;
 std::vector<int> key{menu.selected,int(draw_scale*1000),menu.inventory.equipped,menu.editing,menu.choice,cv_hud_language(),menu.config.edit_key,menu.config.edit_button,menu.config.confirm_key,menu.config.confirm_button};for(int i=0;i<cv_items::kEntries;i++)key.push_back(menu.inventory.entry[i]);for(int d=0;d<4;d++){key.push_back(menu.slots[d]);key.push_back(menu.wanted[d]);key.push_back(menu.config.keys[d]);}
 if(key!=previous){auto bitmap=render_quick(menu,draw_scale);raster(*bitmap,cached,size_t(std::clamp(16000.f*draw_scale*draw_scale,16000.f,60000.f)));previous=key;}
 combined.clear();if(n&&base)combined.insert(combined.end(),base,base+n);const float factor=scale/draw_scale;
 const float x=std::max(12.f,float(width)-670*scale),y=float(height)-357*scale;
 for(auto q:cached){q.x=x+q.x*factor;q.y=y+q.y*factor;q.w*=factor;q.h*=factor;q.argb=(q.argb&0xffffff)|(uint32_t(std::lround((q.argb>>24)*alpha))<<24);if(combined.size()<131072)combined.push_back(q);}
 *count=uint32_t(combined.size());return combined.data();
}
