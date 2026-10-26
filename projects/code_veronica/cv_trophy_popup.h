#pragma once
#include "cv_trophies.h"
#include "../../libs/video/rsx_ui_batch.h"
namespace cv_trophy_popup {
void initialize();
void enabled(bool show);
// 0 top left, 1 top right, 2 bottom left, 3 bottom right. Live preference.
void corner(int position);
void enqueue(const cv_trophies::Trophy& trophy,int language);
// Deterministic clock and silent sound observer for the isolated renderer test.
const rsx_ui_sprite* frame_at(uint32_t w,uint32_t h,uint64_t now,const uint32_t** rgba,uint32_t* aw,uint32_t* ah,uint64_t* generation);
void sound_observer_for_test(void (*observer)());
}
