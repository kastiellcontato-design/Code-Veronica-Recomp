#pragma once
#include <cwchar>
#include <string>
#include <string_view>
namespace cv_test {
struct Room {
    int stage=0, number=0, variant=0;
    unsigned key() const { return (stage<<16)|(number<<8)|variant; }
    std::wstring code() const { wchar_t out[24];swprintf(out,24,L"rm_%d%02d%d",stage,number,variant);return out; }
};
inline bool parse_room(std::wstring_view file,Room& out) {
    if(file.size()!=11 || file.substr(0,3)!=L"rm_" || file.substr(7)!=L".arc")return false;
    for(int i=3;i<7;i++)if(file[i]<L'0'||file[i]>L'9')return false;
    out={int(file[3]-L'0'),int(file[4]-L'0')*10+int(file[5]-L'0'),int(file[6]-L'0')};return true;
}
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
void cv_test_tools_enable(bool enabled);
void cv_test_tools_show(HWND owner,const std::wstring& game,int language);   // launcher_i18n.h Lang
bool cv_test_tools_visible();
void cv_test_tools_close();
bool cv_test_tools_message(MSG* message);
#endif
