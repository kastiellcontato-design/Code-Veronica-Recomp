#include "../cv_enhanced_settings.h"
#include "../../../libs/codec/audio_gain.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
static uint8_t memory[16*1024*1024];
static uint32_t vm_read32(uint64_t a){assert(a+4<=sizeof memory);return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3];}
static void put(uint32_t a,uint32_t v){for(int i=0;i<4;i++)memory[a+i]=uint8_t(v>>(24-8*i));}
#include "../cv_unlock_lift.h"
int main(){
 assert(cv_enhanced_get(8)==100&&cv_enhanced_get(9)==100&&cv_enhanced_get(10)==0);
 cv_enhanced_set(8,25);assert(cv_enhanced_get(8)==25&&cv_enhanced_get(9)==100);
 cv_enhanced_set(9,-5);assert(cv_enhanced_get(9)==0&&cv_enhanced_get(8)==25);
 cv_enhanced_set(8,120);assert(cv_enhanced_get(8)==100);
 uint8_t pcm[]={0x7f,0xff,0x80,0,0xff,0xff,0,0,0x40,0};
 ps3_audio_gain_s16be(pcm,5,100);assert(pcm[0]==0x7f&&pcm[1]==0xff&&pcm[2]==0x80);
 ps3_audio_gain_s16be(pcm,5,50);assert(pcm[0]==0x3f&&pcm[1]==0xff&&pcm[2]==0xc0&&pcm[3]==0&&pcm[8]==0x20);
 ps3_audio_gain_s16be(pcm,5,0);for(auto b:pcm)assert(b==0);
 assert(ps3_audio_gain_float(.5f,100)==.5f&&ps3_audio_gain_float(.5f,50)==.25f&&ps3_audio_gain_float(-1,0)==0);
 put(CV_BATTLE_SYS_POINTER,0xBC3818);put(0xBC381C,0x80000000);
 assert(cv_battle_read32(0xBC381C,1)==0x80000000);
 cv_enhanced_set(10,1);assert(cv_battle_read32(0xBC381C,1)==0x80000001);
 assert(cv_battle_read32(0xBC381C,0x7c)==0x8000007c);
 assert(cv_battle_read32(0xBC3820,0x7c)==0);
 assert(vm_read32(0xBC381C)==0x80000000);
 cv_enhanced_set(10,0);assert(cv_battle_read32(0xBC381C,0x7c)==0x80000000);
 put(CV_BATTLE_SYS_POINTER,0);cv_enhanced_set(10,1);assert(cv_battle_read32(4,1)==0);
 puts("[enhanced-test] PASS: defaults, independent gain, mute, clamp, Battle read filters and unchanged guest memory");
}
