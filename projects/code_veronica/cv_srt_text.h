#pragma once
#include <string>
#include <vector>
#include <cwctype>
#include <windows.h>
namespace cv_srt_text {
// Wrap at words, splitting an oversized word only when it cannot fit on an
// empty line. The caller supplies the same font metrics used for drawing.
template<class Measure>
std::vector<std::wstring> wrap(const std::wstring& text,float width,Measure measure) {
    std::vector<std::wstring> lines;std::wstring line;
    for(size_t at=0;at<text.size();) {
        while(at<text.size()&&std::iswspace(text[at]))at++;
        if(at==text.size())break;
        size_t end=at;while(end<text.size()&&!std::iswspace(text[end]))end++;
        std::wstring word=text.substr(at,end-at);at=end;
        const auto joined=line.empty()?word:line+L" "+word;
        if(measure(joined)<=width) {line=joined;continue;}
        if(!line.empty()) {lines.push_back(std::move(line));line.clear();}
        while(!word.empty()&&measure(word)>width) {
            size_t fit=0;
            for(size_t n=1;n<=word.size();n++) {
                // Do not cut a UTF-16 surrogate pair in half.
                if(n<word.size()&&word[n-1]>=0xd800&&word[n-1]<=0xdbff&&word[n]>=0xdc00&&word[n]<=0xdfff)continue;
                if(measure(word.substr(0,n))>width&&fit)break;
                fit=n;
            }
            if(fit==word.size())break;
            lines.push_back(word.substr(0,fit));word.erase(0,fit);
        }
        line=std::move(word);
    }
    if(!line.empty())lines.push_back(std::move(line));
    if(lines.empty())lines.emplace_back();
    return lines;
}
// Unicode casing, independent of the process C locale (which leaves ç/ã lowercase).
inline std::wstring upper(const std::wstring& text) {
    if(text.empty())return text;
    const int n=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,text.data(),int(text.size()),nullptr,0,nullptr,nullptr,0);
    if(n<=0)return text;
    std::wstring result(size_t(n),0);
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,text.data(),int(text.size()),result.data(),n,nullptr,nullptr,0))return text;
    return result;
}
}
