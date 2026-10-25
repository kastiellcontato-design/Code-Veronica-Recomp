#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cwctype>

namespace cv_launcher_cache {
namespace fs=std::filesystem;
// Normal development runners work from the port root. A distributed build
// keeps its cache beside the executable. Never use an unrelated inherited CWD.
inline fs::path port_directory(const fs::path& executable)
{
    const auto bin=executable.parent_path(),build=bin.parent_path(),port=build.parent_path();
    const auto name=build.filename().wstring();std::error_code ec;
    if(bin.filename()==L"bin"&&(name==L"build-release"||name==L"build-release-jp"||name==L"build-release-us")&&
       fs::is_regular_file(port/L"launcher.cpp",ec)&&
       fs::is_regular_file(port/L"executar-code-veronica-otimizado.cmd",ec))return port;
    return bin;
}
inline bool is_cache_file(const fs::path& p)
{
    auto name=p.filename().wstring(),ext=p.extension().wstring();
    for(auto& c:name)c=wchar_t(towlower(c));for(auto& c:ext)c=wchar_t(towlower(c));
    return ext==L".cso"||name==L"pipelines.bin"||name==L"pipelines.bin.tmp"||
           name==L"pipelines.list"||name==L"pipelines.list.tmp";
}
// Follow compiler-specific subfolders, but never junctions or symbolic links.
inline std::vector<fs::path> files(const fs::path& root)
{
    std::vector<fs::path> found;std::error_code ec;
    const auto attributes=GetFileAttributesW(root.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return found;
    auto it=fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied,ec);
    for(;!ec&&it!=fs::recursive_directory_iterator();it.increment(ec)){
        const auto attrs=GetFileAttributesW(it->path().c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)){
            it.disable_recursion_pending();continue;
        }
        if(it->is_regular_file(ec)&&is_cache_file(it->path()))found.push_back(it->path());
    }
    return found;
}
inline void stats(const fs::path& root,int* count,double* mb)
{
    *count=0;*mb=0;
    for(const auto& p:files(root)){
        std::error_code ec;const auto size=fs::file_size(p,ec);
        if(!ec){++*count;*mb+=double(size)/(1024.0*1024.0);}
    }
}
inline int clear(const fs::path& root)
{
    int count=0;
    for(const auto& p:files(root)){
        std::error_code ec;if(fs::remove(p,ec))++count;
    }
    return count;
}
}
