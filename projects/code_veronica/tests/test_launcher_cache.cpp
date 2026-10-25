#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cv_launcher_cache.h"
#include <cassert>
#include <fstream>
#include <cstdio>
namespace fs=std::filesystem;
static void put(const fs::path& p,const char* s){fs::create_directories(p.parent_path());std::ofstream(p)<<s;}
int main()
{
    const auto base=fs::temp_directory_path()/(L"cv-launcher-cache-test-"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(base);
    const auto previous=fs::current_path();
    const auto dev=base/L"port",bin=dev/L"build-release/bin",foreign=base/L"foreign";
    fs::create_directories(bin);fs::create_directories(foreign);
    put(dev/L"launcher.cpp","source");put(dev/L"executar-code-veronica-otimizado.cmd","runner");
    fs::current_path(foreign);
    assert(cv_launcher_cache::port_directory(bin/L"CodeVeronicaPort_JP.exe")==dev);
    assert(cv_launcher_cache::port_directory(base/L"portable/CodeVeronicaPort.exe")==base/L"portable");
    assert(cv_launcher_cache::port_directory(base/L"other/build-release/bin/CodeVeronicaPort.exe")==base/L"other/build-release/bin");
    const auto cache=dev/L"shader_cache";
    put(cache/L"pipelines.bin","123");put(cache/L"dxc/compiler/one.cso","1234");
    put(cache/L"dxc/two.CSO","12");put(cache/L"dxc/pipelines.list.tmp","1");
    put(cache/L"keep.png","keep");put(cache/L"dxc/notes.txt","keep");
    put(base/L"outside/secret.cso","external");
    const bool linked=CreateSymbolicLinkW((cache/L"external").c_str(),(base/L"outside").c_str(),
        SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=0;
    int count=0;double mb=0;cv_launcher_cache::stats(cache,&count,&mb);
    assert(count==4&&mb==10.0/(1024.0*1024.0));
    assert(cv_launcher_cache::clear(cache)==4);
    assert(fs::exists(cache/L"keep.png")&&fs::exists(cache/L"dxc/notes.txt"));
    assert(fs::exists(base/L"outside/secret.cso"));
    cv_launcher_cache::stats(cache,&count,&mb);assert(count==0&&mb==0);
    if(linked)fs::remove(cache/L"external");
    fs::current_path(previous);fs::remove_all(base);
    std::printf("launcher cache tests: ok (foreign CWD, dev/portable roots, recursive stats/clear, unrelated files%s)\n",linked?", symlink exclusion":"");
}
