#include "cv_enhanced_settings.h"
#include "../../libs/codec/audio_gain.h"
#include <algorithm>
#include "cv_film_filter.h"
#include <cstdlib>
#include <cstring>
#include "cv_version.h"
extern "C" int cv_dynamic_shadows_get(void);
extern "C" void cv_dynamic_shadows_set(int);
std::atomic<bool> cv_battle_unlocked{false};
static std::atomic<int> language{1};
extern "C" void cv_enhanced_language_set(int value){language.store(std::clamp(value,0,12),std::memory_order_relaxed);}
extern "C" int cv_enhanced_language(void){if(std::strcmp(CV_TITLE_ID,"NPEB00553")&&std::strcmp(CV_TITLE_ID,"NPUB30467"))return language.load(std::memory_order_relaxed);const char* root=std::getenv("CV_PTBR_ASSET_ROOT");return root&&*root?0:1;}
static int env(const char* key,int fallback){auto* v=std::getenv(key);return v&&*v?std::atoi(v):fallback;}
extern "C" int cv_enhanced_get(int which){return which==8?ps3_audio_gain_percent(0):which==9?ps3_audio_gain_percent(1):which==10?int(cv_battle_unlocked.load(std::memory_order_relaxed)):which==11?int(cv_film_filter::requested.load(std::memory_order_relaxed)) :which==12?cv_dynamic_shadows_get():0;}
extern "C" void cv_enhanced_set(int which,int value){
    if(which==12)cv_dynamic_shadows_set(value);
    if(which==8)ps3_audio_gain_set(std::clamp(value,0,100),cv_enhanced_get(9));
    if(which==9)ps3_audio_gain_set(cv_enhanced_get(8),std::clamp(value,0,100));
    if(which==11)cv_film_filter::requested.store(value!=0,std::memory_order_relaxed);
    if(which==10)cv_battle_unlocked.store(value!=0,std::memory_order_relaxed);
}
extern "C" void cv_enhanced_configure_env(void){ps3_audio_gain_set(env("CV_MUSIC_VOLUME",100),env("CV_SFX_VOLUME",100));cv_battle_unlocked.store(env("CV_BATTLE_UNLOCKED",0)!=0,std::memory_order_relaxed);}

extern "C" void cv_enhanced_apply_startup(void){cv_film_filter::boot();}
