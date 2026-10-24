#include "pkg_core.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
 if(argc<3)return 2;
 if(std::wstring(argv[1])==L"inspect"){auto i=veronica_pkg::inspect(argv[2]);std::cout<<i.title_id<<" "<<i.version<<" "<<i.files<<" "<<i.bytes;return 0;}
 if(argc<4)return 2;std::atomic<bool> cancel=false;uint64_t previous=0;auto p=veronica_pkg::extract(argv[2],argv[3],cancel,[&](auto progress){if(progress.done<previous||progress.done>progress.total)throw std::runtime_error("Invalid progress");previous=progress.done;if(std::wstring(argv[1])==L"cancel")cancel=true;});std::wcout<<p.wstring();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
