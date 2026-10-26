#pragma once
#include <windows.h>
#include <filesystem>
#include <vector>
#include <cstdlib>
namespace cv_user_data {
namespace fs=std::filesystem;
inline fs::path executable_directory(){wchar_t p[32768]{};DWORD n=GetModuleFileNameW(nullptr,p,32768);return n&&n<32768?fs::path(p).parent_path():fs::current_path();}
inline fs::path installation(const fs::path& exe){std::error_code ec;return exe.filename()==L"bin"&&fs::is_regular_file(exe.parent_path()/L"Code Veronica.exe",ec)?exe.parent_path():exe;}
inline fs::path installation(){return installation(executable_directory());}
inline fs::path choose(const fs::path& base,const fs::path& relative){std::error_code ec;auto primary=base/relative,legacy=base/L"bin"/relative;return !fs::exists(primary,ec)&&fs::exists(legacy,ec)?legacy:primary;}
inline fs::path default_saves(){return choose(installation(),L"gamedata/dev_hdd0/home/00000001/savedata");}
inline fs::path trophies(){return choose(installation(),L"gamedata/trophies");}
inline std::vector<fs::path> save_roots(){
 std::vector<fs::path> out;const char* override=std::getenv("PS3_SAVE_ROOT");
 auto add=[&](fs::path p){p=fs::absolute(p).lexically_normal();for(const auto& old:out)if(_wcsicmp(old.c_str(),p.c_str())==0)return;out.push_back(p);};
 const auto base=installation();const fs::path suffix=L"gamedata/dev_hdd0/home/00000001/savedata";
 if(override&&*override){auto p=fs::absolute(fs::u8path(override)).lexically_normal();add(p);
  if(_wcsicmp(p.c_str(),(base/suffix).lexically_normal().c_str())&&_wcsicmp(p.c_str(),(base/L"bin"/suffix).lexically_normal().c_str()))return out;
 }else add(default_saves());
 add(base/suffix);add(base/L"bin"/suffix);return out;
}
inline std::vector<fs::path> f7_indices(){auto base=installation();return {base/L"cv_f7_saves.txt",base/L"bin/cv_f7_saves.txt"};}
}
