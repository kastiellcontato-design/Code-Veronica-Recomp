#pragma once
#include "ppu_recomp.h"
extern "C" int cv_manual_reload_get();
extern "C" void cv_manual_reload_set(int);
extern "C" void cv_manual_reload_bindings_set(int key,int button);
extern "C" int cv_manual_reload_key_get();
extern "C" int cv_manual_reload_button_get();
extern "C" int cv_manual_reload_key_consumed(int);
extern "C" unsigned cv_manual_reload_filter_buttons(unsigned);
void cv_manual_reload_frame(ppu_context*);
