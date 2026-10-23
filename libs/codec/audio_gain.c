#include "audio_gain.h"
#include <stdatomic.h>
static atomic_int streamed_volume=100,effects_volume=100;
static int clamp(int v){return v<0?0:v>100?100:v;}
void ps3_audio_gain_set(int streamed,int effects){atomic_store_explicit(&streamed_volume,clamp(streamed),memory_order_relaxed);atomic_store_explicit(&effects_volume,clamp(effects),memory_order_relaxed);}
int ps3_audio_gain_percent(int effects){return atomic_load_explicit(effects?&effects_volume:&streamed_volume,memory_order_relaxed);}
void ps3_audio_gain_s16be(uint8_t* pcm,unsigned samples,int percent){
    percent=clamp(percent);if(percent==100)return;
    for(unsigned i=0;i<samples;i++){int v=(int16_t)((unsigned)pcm[i*2]<<8|pcm[i*2+1]);v=v*percent/100;pcm[i*2]=(uint8_t)((uint16_t)v>>8);pcm[i*2+1]=(uint8_t)v;}
}
