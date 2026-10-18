#pragma once
#include <filesystem>
#include <string>
#include <cwchar>
namespace cv_texture_paths {
namespace fs=std::filesystem;
inline fs::path mod_folder(const fs::path& executable_directory,const std::wstring& chosen) {
    return chosen.empty()?executable_directory/L"mods"/L"textures":fs::path(chosen);
}
inline fs::path dump_folder(const fs::path& executable_directory,const std::wstring& chosen,const std::wstring& code) {
    return chosen.empty()?executable_directory/L"mods"/L"texture-dumps"/code:fs::path(chosen);
}
inline fs::path replacement_folder(const fs::path& mod,const std::wstring& code) {
    std::error_code ec;const auto regional=mod/code;
    return fs::is_directory(regional,ec)?regional:mod;
}
inline bool same_folder(const fs::path& a,const fs::path& b) {
    std::error_code ec1,ec2;auto aa=fs::weakly_canonical(a,ec1),bb=fs::weakly_canonical(b,ec2);
    if(ec1)aa=a.lexically_normal();if(ec2)bb=b.lexically_normal();
    return !_wcsicmp(aa.c_str(),bb.c_str());
}
}
