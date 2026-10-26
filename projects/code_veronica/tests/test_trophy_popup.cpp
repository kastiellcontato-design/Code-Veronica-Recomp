#include "../cv_trophy_popup.cpp"
#include "../cv_trophies.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
extern "C" {const rsx_ui_sprite* (*g_rsx_trophy_hook)(uint32_t,uint32_t,const uint32_t**,uint32_t*,uint32_t*,uint64_t*)=nullptr;}
static unsigned sounds=0;
static void heard(){sounds++;}
static void save_card(const uint32_t* rgba,unsigned w,unsigned h,const wchar_t* name){
 using namespace Gdiplus;Bitmap b(w,h,PixelFormat32bppARGB);BitmapData data{};Rect r(0,0,w,h);assert(b.LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&data)==Ok);
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){uint32_t c=rgba[y*w+x];reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(data.Scan0)+size_t(y)*data.Stride)[x]=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);}
 b.UnlockBits(&data);CLSID png;assert(CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}",&png)==S_OK);assert(b.Save(name,&png,nullptr)==Ok);
}
int main(int argc,char** argv){
 assert(argc==3);
 const std::filesystem::path game=std::filesystem::u8path(argv[1]);
 cv_trophies::storage_for_test(std::filesystem::u8path(argv[2]));
 using namespace cv_trophy_popup;initialize();sound_observer_for_test(heard);assert(g_rsx_trophy_hook);
 auto first=cv_trophies::view(game,0,true);assert(first.catalog&&first.catalog->items.size()==12);
 auto trophy=first.catalog->items[0];
 const uint32_t* pixels=nullptr;uint32_t w=0,h=0;uint64_t generation=0;
 assert(!frame_at(1920,1080,1000,&pixels,&w,&h,&generation));enqueue(trophy,0);
 auto* card=frame_at(1920,1080,1000,&pixels,&w,&h,&generation);assert(card&&w==448&&h==140&&sounds==1&&(card->argb>>24)==0);float start_x=card->x;
 card=frame_at(1920,1080,1140,&pixels,&w,&h,&generation);assert(card&&(card->argb>>24)>100&&(card->argb>>24)<160);assert(card->x<start_x);
 card=frame_at(1920,1080,1280,&pixels,&w,&h,&generation);assert(card&&(card->argb>>24)==255&&sounds==1);float held_x=card->x;
 save_card(pixels,w,h,L"popup-pt.png");
 card=frame_at(1920,1080,6280,&pixels,&w,&h,&generation);assert(card&&(card->argb>>24)==255&&card->x==held_x&&sounds==1);
 card=frame_at(1920,1080,6430,&pixels,&w,&h,&generation);assert(card&&(card->argb>>24)>100&&(card->argb>>24)<160&&card->x>held_x);
 assert(!frame_at(1920,1080,6580,&pixels,&w,&h,&generation));assert(!frame_at(1920,1080,9000,&pixels,&w,&h,&generation));
 enqueue(trophy,0);enqueue(trophy,0);frame_at(1280,720,10000,&pixels,&w,&h,&generation);assert(sounds==2);frame_at(1280,720,15580,&pixels,&w,&h,&generation);assert(sounds==3);
 enabled(false);assert(!frame_at(1280,720,16000,&pixels,&w,&h,&generation));enqueue(trophy,0);enabled(true);assert(!frame_at(1280,720,16001,&pixels,&w,&h,&generation));
 for(int lang=0;lang<13;lang++){
  auto snapshot=cv_trophies::view(game,lang,true);assert(snapshot.catalog&&snapshot.catalog->items.size()==12);
  for(const auto& localized:snapshot.catalog->items){
   enabled(false);enabled(true);enqueue(localized,lang);frame_at(1920,1080,20000+lang*10000,&pixels,&w,&h,&generation);
   card=frame_at(1920,1080,20280+lang*10000,&pixels,&w,&h,&generation);assert(card&&pixels&&generation);
   if(lang!=0&&localized.id==0)assert(localized.name!=trophy.name&&localized.description!=trophy.description);
   if(localized.id==0){auto filename=L"popup-lang-"+std::to_wstring(lang)+L".png";save_card(pixels,w,h,filename.c_str());if(lang==9)save_card(pixels,w,h,L"popup-ar.png");}
  }
 }
 HRSRC sound=FindResourceW(GetModuleHandleW(nullptr),L"TROPHY_SOUND",L"WAVE");assert(sound);
 assert(SizeofResource(GetModuleHandleW(nullptr),sound)>170000);
 auto* data=static_cast<const char*>(LockResource(LoadResource(GetModuleHandleW(nullptr),sound)));assert(data&&!memcmp(data,"RIFF",4)&&!memcmp(data+8,"WAVE",4));
 assert(PlaySoundW(L"TROPHY_SOUND",GetModuleHandleW(nullptr),SND_RESOURCE|SND_ASYNC|SND_NODEFAULT));Sleep(1100);
 puts("PASS popup: prepared RGBA/icon; 280ms entry; 5s hold; 300ms exit; sound once on display; sequential queue; disabled cancels; idle empty; all 12 trophies in all 13 launcher languages; embedded WAV playback accepted.");
 return 0;
}
