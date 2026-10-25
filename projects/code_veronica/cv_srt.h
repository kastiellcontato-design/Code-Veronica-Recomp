#pragma once
#include <stdint.h>
#include "../../libs/video/rsx_ui_batch.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { CV_SRT_MAX_ENEMIES=64, CV_SRT_INVENTORY_SIZE=11 };
enum { SRT_FINE, SRT_CAUTION, SRT_WARNING, SRT_DANGER, SRT_POISON, SRT_GAS, SRT_DEAD };
typedef struct { int id, quantity, wide, counted, infinite; } cv_srt_item;
typedef struct { int type, hp, maximum, estimated; } cv_srt_enemy;
typedef struct {
    int valid, character, hp, maximum, condition, room;
    uint32_t seconds;
    int saves, retry, fas, equipped, enemy_count;
    cv_srt_item inventory[CV_SRT_INVENTORY_SIZE];
    cv_srt_enemy enemies[CV_SRT_MAX_ENEMIES];
} cv_srt_state;
/* No writes to guest memory. Called by the render/frame-clock thread only. */
int cv_srt_sample(cv_srt_state* state);
void cv_srt_configure_env(void);
/* Pointer coordinates are back-buffer pixels. Render thread only; a release
 * notifies the launcher thread, which takes and saves the normalized positions. */
void cv_srt_pointer(float x,float y,int left_down,int active);
void cv_srt_set_position_notify(void (*notify)(void));
int cv_srt_take_positions(int* main_x,int* main_y,int* inventory_x,int* inventory_y);
const rsx_ui_quad* cv_srt_quads(uint32_t width,uint32_t height,uint32_t* count);
/* Native layout, also used by the isolated design/regression test. */
const rsx_ui_quad* cv_srt_design(const cv_srt_state* state,uint32_t width,uint32_t height,uint32_t* count);
/* The images drawn with the quads just returned (the boss bar's art), and the
 * atlas they sample: RGBA8, straight alpha, `gen` changes when it does. */
const rsx_ui_sprite* cv_srt_sprites(uint32_t* count,const uint32_t** rgba,uint32_t* w,uint32_t* h,uint64_t* gen);
#ifdef __cplusplus
}
#endif
