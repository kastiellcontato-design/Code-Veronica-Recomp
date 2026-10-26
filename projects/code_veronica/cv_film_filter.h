#pragma once
#include <atomic>
#include <string>
#include <algorithm>
// Requested preference is separate from the boot-latched state: texture caches
// must not become half-enabled when the user changes this setting in a menu.
namespace cv_film_filter {
inline std::atomic<bool> requested{true},applied{true};
inline bool asset(const char* path){
 if(!path)return false;
 std::string key(path);for(char& c:key){if(c=='\\')c='/';if(c>='A'&&c<='Z')c=char(c-'A'+'a');}
 const std::string dir="/usrdir/bhcv/nativeps3/system/filter/";
 const auto at=key.rfind(dir);if(at==std::string::npos)return false;
 const std::string name=key.substr(at+dir.size());
 return name=="tvmask.tex"||name=="tvmask_lin.tex"||name=="tvnoise.tex"||name=="tvnoise_lin.tex"||name=="volumenoise.tex"||name=="volumenoise_lin.tex";
}
inline const char* resolve(const char* path){
 // Empty path is a guaranteed failed lookup, not a file renamed/deleted on disk.
 return !applied.load(std::memory_order_relaxed)&&asset(path)?"":path;
}
inline void boot(){applied.store(requested.load(std::memory_order_relaxed),std::memory_order_relaxed);}
}
