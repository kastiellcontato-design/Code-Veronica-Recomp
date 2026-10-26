#include "../cv_film_filter.h"
#include "../../../libs/filesystem/vfs_overlay.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <filesystem>
static const char* overlay(const char* path,char*,size_t){return cv_film_filter::resolve(path);}
int main(){
 using namespace cv_film_filter;
 assert(requested&&applied);vfs_overlay_set(overlay);
 const char* names[]={"TVNoise.tex","TVNoise_LIN.tex","TVMask.tex","TVMask_LIN.tex","VolumeNoise.tex","VolumeNoise_LIN.tex"};
 const char* titles[]={"NPEB00553","NPUB30467","NPJB00135"};
 for(auto id:titles)for(auto name:names){
  std::string path=std::string("/dev_hdd0/game/")+id+"/USRDIR/BHCV/nativePS3/system/filter/"+name;
  assert(asset(path.c_str()));char buf[2048];applied=true;assert(std::strcmp(vfs_overlay_resolve(path.c_str(),buf,sizeof buf),path.c_str())==0);
  applied=false;assert(std::strcmp(vfs_overlay_resolve(path.c_str(),buf,sizeof buf),"")==0);
 }
 const char* actual="D:/game/USRDIR/BHCV/nativePS3/system/filter/TVNoise_LIN.tex";
 applied=true;requested=false;assert(resolve(actual)==actual);boot();assert(*resolve(actual)==0);
 requested=true;assert(*resolve(actual)==0);boot();assert(resolve(actual)==actual);
 for(auto x:{"/dev_hdd0/game/NPEB00553/USRDIR/BHCV/nativePS3/system/filter/TVNoise.tex.bak","/dev_hdd0/game/NPEB00553/USRDIR/BHCV/nativePS3/system/filter/sub/TVNoise.tex","/other/filter/TVNoise.tex","/save/TVMask.tex","/USRDIR/BHCV/nativePS3/system/other.tex"})assert(!asset(x));
 assert(!asset(nullptr));assert(!asset(""));assert(asset("D:/GAME/USRDIR/BHCV/NATIVEPS3/SYSTEM/FILTER/TVNOISE_LIN.TEX"));
 puts("PASS film filter: all six files, EU/US/JP paths, excluded unrelated assets, restart latch and restoration");
}
