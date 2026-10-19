#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_fps_clock.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <thread>
static unsigned char mem[4096];static unsigned calls,bio_calls;
static uint32_t expected_cap,next_step=0x3f800000;static uint8_t expected_mode;static bool change_inside;
static bool interpolation;static unsigned scene_calls;
uint32_t vm_read32(uint64_t a) {if(interpolation&&a==CV_SRT_RDX)return 0x3D650;assert(a+4<=sizeof mem);return uint32_t(mem[a])<<24|uint32_t(mem[a+1])<<16|uint32_t(mem[a+2])<<8|mem[a+3];}
uint8_t vm_read8(uint64_t a) {if(interpolation){if(a==CV_SRT_CHARACTER)return 0;if(a==CV_SRT_ROOM)return 1;if(a==CV_SRT_ROOM+1)return 13;}assert(a<sizeof mem);return mem[a];}
void vm_write32(uint64_t a,uint32_t b) {assert(a+4<=sizeof mem);for(int j=0;j<4;j++)mem[a+j]=uint8_t(b>>(24-j*8));}
void vm_write8(uint64_t a,uint8_t b) {assert(a<sizeof mem);mem[a]=b;}
void CV_FN_FRAME_PACE(ppu_context*);
void CV_OG_FRAME_PACE(ppu_context* ctx) {
 calls++;assert(ctx->gpr[3]==256);assert(vm_read32(256+0x64)==expected_cap);assert(vm_read8(256+0x68)==expected_mode);
 ctx->gpr[3]=77;vm_write32(256+0x90,next_step);
 if(change_inside) {vm_write32(256+0x64,0x41700000);vm_write8(256+0x68,2);}
}
static void (*registered_update)(ppu_context*);
#if defined(CV_REGION_JP)
#define TEST_BIO func_0020E9C0
static constexpr uint32_t test_bio_address=0x0020E9C0;
#elif defined(CV_REGION_US)
#define TEST_BIO func_0020E0D0
static constexpr uint32_t test_bio_address=0x0020E0D0;
#else
#define TEST_BIO func_00210070
static constexpr uint32_t test_bio_address=0x00210070;
#endif
void TEST_BIO(ppu_context* ctx) {assert(ctx->gpr[3]==256);bio_calls++;ctx->gpr[3]=77;}
extern "C" void ppu_register_function(uint64_t a,void (*fn)(ppu_context*)) {assert(a==test_bio_address);registered_update=fn;}
extern "C" void cv_fps_register_game_update();
extern "C" void rsx_d3d12_pose_scene(uint32_t scene) {assert(interpolation&&scene==0x80010D00u);scene_calls++;}
static ppu_context input() {ppu_context c{};c.gpr[3]=256;c.gpr[20]=123;return c;}
int main(int argc,char** argv) {
 interpolation=argc>1&&std::strcmp(argv[1],"interp")==0;
 _putenv_s("CV_RENDER_INTERPOLATION",interpolation?"1":"");
 bool raw=argc>1&&std::strcmp(argv[1],"raw")==0;
 bool on=raw||interpolation||(argc>1&&std::strcmp(argv[1],"on")==0);
 _putenv_s("CV_FPS_TEST_60",on?"1":"");_putenv_s("CV_FPS_BENCHMARK_RAW",raw?"1":"");
 for(int j=0;j<5;j++) {
  auto ctx=input();const uint32_t base=j==2?0x42700000:0x41f00000,cap=j==1?0x41700000:0x453b8000;
  const uint8_t mode=j==4?0:1;
  vm_write32(256+0x60,base);vm_write32(256+0x64,cap);vm_write8(256+0x68,mode);change_inside=j==3;
  expected_cap=on&&j!=1&&j!=2?0x42700000:cap;expected_mode=on&&j!=1&&j!=2?0:mode;
  CV_FN_FRAME_PACE(&ctx);assert(calls==unsigned(j+1));assert(ctx.gpr[3]==77&&ctx.gpr[20]==123);
  assert(vm_read32(256+0x60)==base);assert(vm_read32(256+0x64)==(change_inside?0x41700000:cap));
  assert(vm_read8(256+0x68)==(change_inside?2:mode));assert(vm_read32(256+0x90)==next_step);
 }
 cv_fps_register_game_update();assert(bool(registered_update)==(on&&!raw));
 if(registered_update) {
  vm_write32(256+0x60,0x41f00000);vm_write32(256+0x64,0x453b8000);vm_write8(256+0x68,1);
  change_inside=false;expected_cap=0x42700000;expected_mode=0;next_step=0x3f000000;
  for(int i=0;i<4;i++) {
   auto ctx=input();CV_FN_FRAME_PACE(&ctx);ctx=input();auto before=ctx;registered_update(&ctx);
   if(i%2==0)assert(ctx.gpr[3]==77);else assert(std::memcmp(&ctx,&before,sizeof ctx)==0);
  }
  assert(bio_calls==2);assert(scene_calls==(interpolation?4u:0u));
  // Catch-up executes three updates with identical incoming arguments.
  next_step=0x40200000;auto ctx=input();CV_FN_FRAME_PACE(&ctx);ctx=input();registered_update(&ctx);assert(bio_calls==5&&ctx.gpr[3]==77);
  // Pacing may run on a different engine thread than this virtual method.
  next_step=0x3f000000;ctx=input();CV_FN_FRAME_PACE(&ctx);
  std::thread worker([] {for(int i=0;i<3;i++){auto c=input();registered_update(&c);}});worker.join();assert(bio_calls==7);
 }
 for(int hz: {30,35,40,45,60}) {
  cv_fps::LogicClock c;unsigned total=0;for(int i=0;i<hz*10;i++)total+=c.advance(30.0/hz);
  assert(total>=299&&total<=301);
 }
 cv_fps::LogicClock c;assert(c.advance(0.5)==1);assert(c.advance(0.5)==0);assert(c.advance(0.5)==1);
 assert(c.advance(100)==4);assert(c.advance(-1)==0);assert(c.advance(NAN)==0);c.reset();assert(c.advance(0.5)==1);
 std::puts(raw?"PASS RAW: unlocked pacing, no game-update override":interpolation?"PASS INTERP: 30 Hz logic preserved and render scene published":on?"PASS ON: pacing, 30 Hz callback, skipped context, catch-up, worker and variable FPS":"PASS OFF: original unchanged, no registry override");
}
