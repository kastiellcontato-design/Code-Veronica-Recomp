#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional application controls. Default 100 leaves decoder output unchanged. */
void ps3_audio_gain_set(int streamed,int effects);
int ps3_audio_gain_percent(int effects);
void ps3_audio_gain_s16be(uint8_t* pcm,unsigned samples,int percent);
static inline float ps3_audio_gain_float(float sample,int percent) {return percent==100?sample:sample*(percent/100.f);}
#ifdef __cplusplus
}
#endif
