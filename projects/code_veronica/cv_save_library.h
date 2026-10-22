#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "cv_test_tools.h"
struct ppu_context;
void cv_save_library_show(bool saving,int language);
void cv_save_library_close();
void cv_archives_show(int page,int language,int opening_input=0);
bool cv_archives_visible();
void cv_archives_maps(const std::vector<cv_test::Room>& rooms,unsigned current,bool (*travel)(unsigned,unsigned));
bool cv_save_library_message(MSG* message);
void cv_save_library_frame(ppu_context* ctx,bool available);
#endif
