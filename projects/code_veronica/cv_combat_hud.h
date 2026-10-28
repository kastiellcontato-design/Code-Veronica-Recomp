#pragma once
#include "cv_srt.h"
extern "C" void cv_combat_hud_set(int enabled);
extern "C" int cv_combat_hud_get(void);
extern "C" const rsx_ui_quad* cv_combat_hud_quads(uint32_t width,uint32_t height,uint32_t* count);
