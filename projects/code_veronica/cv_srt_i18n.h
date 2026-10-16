#pragma once
#include <array>
#include <string>
#include "cv_srt.h"
namespace cv_srt_i18n {
enum Language { PT,EN,ES,JA,FR,DE,IT,PL,TR,AR,ZH,KO,RU,COUNT };
Language language(const char* code);
Language selection(int saved,int launcher_language);
const wchar_t* font_face(Language lang,bool bold=false);
bool rtl_text(const std::wstring& text);
std::wstring item(int id,Language lang);
std::wstring enemy(int id,Language lang);
std::array<std::wstring,15> labels(Language lang);
std::wstring credit(Language lang);
// The selected inventory entry contains the ammunition loaded in the weapon,
// independently from reserve-ammunition entries in the other slots.
std::wstring quantity(const cv_srt_item& item);
}
