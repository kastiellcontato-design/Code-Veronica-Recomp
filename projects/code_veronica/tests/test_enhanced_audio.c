/* Test the actual three delivery paths with deterministic decoder output.
 * No game assets, firmware, sound device or FFmpeg DLLs are needed. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
uint8_t* vm_base;
#include "../../../libs/codec/cellAtrac.c"
#define rd32 ms_rd32
#include "../../../libs/codec/ms_atrac3.c"
#undef rd32
#include "../../../libs/codec/cv_ffmpeg.c"
int g_resv_store_active;uint32_t g_ww_lo,g_ww_hi;
void ppu_resv_break_store(uint64_t ea){(void)ea;}
void ps3_ww_report_inline(uint32_t a,uint64_t v,int w){(void)a;(void)v;(void)w;}
int spu_coh_is_reserved(uint32_t a){(void)a;return 0;}
void spu_lockline_lock(void){}void spu_lockline_unlock(void){}void spu_coh_notify_write(uint32_t a){(void)a;}
void* at3p_open(int c,int* b){(void)c;(void)b;return (void*)1;}
void at3p_close(void* d){(void)d;}void at3p_flush(void* d){(void)d;}
int at3p_decode(void* d,float* l,float* r,int* n,const uint8_t* f,int b){(void)d;(void)f;for(int i=0;i<2048;i++){l[i]=.5f;r[i]=-.5f;}*n=2048;return b;}
void* at3_open(int c,int b){(void)c;(void)b;return (void*)1;}void at3_close(void* d){(void)d;}
int at3_decode(void* d,float* l,float* r,const uint8_t* f,int b){(void)d;(void)r;(void)f;for(int i=0;i<1024;i++)l[i]=.5f;return b;}
static float guest_float(uint32_t a){uint32_t b=vm_read32(a);float v;memcpy(&v,&b,4);return v;}
static float be_float(const uint8_t* p){uint32_t b=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];float v;memcpy(&v,&b,4);return v;}
int main(void){
 uint8_t memory[65536]={0};vm_base=memory;
 AtracSlot* slot=&s_slots[0];slot->handle_ea=0x100;slot->dec=(void*)1;slot->t.channels=2;slot->t.loop_start=-1;slot->t.end=16;slot->pcm_frame=0;
 for(int i=0;i<16;i++){slot->pcm[0][i]=.5f;slot->pcm[1][i]=-.5f;}
 for(int percent=100;percent>=0;percent-=50){
   slot->pos=0;ps3_audio_gain_set(percent,100);assert(cellAtracDecode(0x100,0x1000,0x200,0x204,0x208)==0);
   assert(guest_float(0x1000)==.5f*percent/100&&guest_float(0x1004)==-.5f*percent/100);assert(slot->pcm[0][0]==.5f);
 }
 slot->pos=0;ps3_audio_gain_set(100,0);cellAtracDecode(0x100,0x1000,0,0,0);assert(guest_float(0x1000)==.5f);
 uint8_t ls[0x40000]={0};uint32_t reg[8]={0x1000,0x2000,0,16,0x5000,0x7000,0x9000,0};
 wr32(ls+0x52d0,5);ls[0x5021]=1;wr32(ls+0x907c,0x100000);
 for(int i=0;i<5;i++){
   int percent=i==0?100:i==1?50:i==2?0:i==3?50:100;
   ps3_audio_gain_set(0,percent);wr32(ls+0x78d0,1024);ms_atrac3_decode(ls,reg);
   assert(rd16(ls+0x2000)==-16384*percent/100);assert(s_streams[0].cur[0]==-16384);
 }
 CvAudioDecoder decoder={0};InitializeSRWLock(&decoder.lock);InitializeConditionVariable(&decoder.space);
 float left[2]={.5f,-.5f},right[2]={-.25f,.25f};uint8_t* planes[2]={(uint8_t*)left,(uint8_t*)right};
 AVFrame frame={0};frame.nb_samples=2;frame.ch_layout.nb_channels=2;frame.extended_data=planes;
 decoder.frames[0].frame=&frame;decoder.write=1;uint8_t pcm[32];
 for(int percent=100;percent>=0;percent-=50){
   ps3_audio_gain_set(percent,100);assert(cv_audio_pcm(&decoder,0x84,pcm,0)==1);
   assert(be_float(pcm)==.5f*percent/100&&be_float(pcm+4)==-.25f*percent/100);
   assert(left[0]==.5f&&right[0]==-.25f&&decoder.read==0);
   assert(cv_audio_pcm(&decoder,2,pcm,0)==1);assert(rd16(pcm)==16384*percent/100);
 }
 ps3_audio_gain_set(100,0);cv_audio_pcm(&decoder,0x84,pcm,0);assert(be_float(pcm)==.5f);
 puts("[audio-path-test] PASS: cellAtrac, MultiStream and cellAdec; mute/half/default, independent channels, raw caches preserved and gain restored");
 return 0;
}
