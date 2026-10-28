// Optional modern movement. Behavioral reference: aikatore/recvx-enhanced-port (MIT).
// Convert input before native edge/history generation; preserve native animation flow.
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_items.h"
#include "cv_quick_select.h"
#include "modern_math.h"
#include "modern_bindings.h"
extern "C" {
extern uint8_t* vm_base;extern int g_pad_block;void* rsx_d3d12_window();
uint32_t vm_read32(uint64_t);uint16_t vm_read16(uint64_t);uint8_t vm_read8(uint64_t);
void vm_write32(uint64_t,uint32_t);void vm_write16(uint64_t,uint16_t);
uint32_t cv_modern_axes();
}
namespace {
using namespace cv_modern_math;
std::atomic<int> modern{0};std::atomic<unsigned> generation{0};
unsigned seen=~0u;Hold held;uint32_t actor=0;bool steer=false;float wanted=0;
bool readable(uint32_t a,unsigned n){MEMORY_BASIC_INFORMATION m{};return a>=0x10000&&n&&uint64_t(a)+n<=0x100000000ull&&VirtualQuery(vm_base+a,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&uintptr_t(vm_base)+uint64_t(a)+n<=uintptr_t(m.BaseAddress)+m.RegionSize;}
float real(uint32_t a){uint32_t w=vm_read32(a);float f;memcpy(&f,&w,4);return f;}
bool focused(){return !g_pad_block&&GetForegroundWindow()==static_cast<HWND>(rsx_d3d12_window());}
void reset(){held.reset();steer=false;}
std::wstring ini(){wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);return (std::filesystem::path(path).parent_path()/L"cv_launcher.ini").wstring();}
}
extern "C" int cv_modern_get(){return modern;}
extern "C" void cv_modern_set(int value){modern=value!=0;generation++;}
extern "C" void cv_modern_change(int value){cv_modern_set(value);WritePrivateProfileStringW(L"jogabilidade",L"movimento_moderno",value?L"1":L"0",ini().c_str());}
extern "C" void cv_modern_boot(){cv_modern_set(GetPrivateProfileIntW(L"jogabilidade",L"movimento_moderno",0,ini().c_str())!=0);}
extern "C" uint32_t cv_controls_input(uint32_t pad){
 steer=false;if(seen!=generation){seen=generation;reset();}
 if(!modern||!focused()||cv_quick::consumes_stick()){reset();return pad;}
 const auto inv=cv_items::read();if(!inv.valid||!inv.playing||!readable(EXP_PLAYER_PTR,4)){reset();return pad;}
 actor=vm_read32(EXP_PLAYER_PTR);const auto state=cv_items::state_base();
 // 0x10000 is set by native movement animations. It suppresses a native
 // mode-selection pass, but does not suspend input conversion. Rejecting it
 // here alternates modern/tank inputs and resets the walk on every step.
 if(!readable(actor,0x400)||vm_read32(EXP_PLAYER_PTR-8)!=state||!readable(state+0x1b0c0,0x28)||vm_read8(actor+12)!=1||vm_read8(actor+13)!=0||(pad&0x10)){reset();return pad;}
 const auto axes=cv_modern_axes();if(!(axes&0x10000)){reset();return pad;}
 float x=(int(axes&255)-128)/127.f,y=(128-int((axes>>8)&255))/127.f;bool moving=stick(x,y);const bool analog=moving&&(axes&0x20000);
 if(!moving&&(pad&15)){x=float(bool(pad&8))-float(bool(pad&4));y=float(bool(pad&1))-float(bool(pad&2));moving=x!=0||y!=0;}
 if(!moving){reset();return pad;}
 float rx,rz;
 {
  if(!readable(EXP_CAMERA,0xa4)){reset();return pad;}
  const auto matrix=vm_read32(EXP_CAMERA+0x9c);if(!readable(matrix,64)){reset();return pad;}
  rx=real(matrix);rz=real(matrix+32);if(!std::isfinite(rx+rz)||std::hypot(rx,rz)<.1f||std::hypot(rx,rz)>2){reset();return pad;}
  const auto room=vm_read16(CV_SRT_ROOM);const auto cut=vm_read32(EXP_CAMERA+0x8c)^(vm_read32(EXP_CAMERA+4)<<16);
  const int phase=vm_read8(actor+14);if(phase>13||phase==12)held.reset();
  held.use(room,cut,x,y,rx,rz);
 }
 wanted=target(rx,rz,x,y);const auto d=wrap(wanted-int32_t(vm_read32(actor+32)));
 const auto phase=vm_read8(actor+14);const bool special=phase>13||phase==12;
 auto result=pad&~0x6fu;
 if(special){if(std::fabs(d)<16384)result|=1;}
 else{result|=std::fabs(d)>65536.f*100/360&&!(vm_read32(actor)&0x100000)?(d>0?4:8):1;if(analog&&x*x+y*y>=.49f)result|=0x400;steer=true;}
 const uint16_t amount=uint16_t(std::lround(127*(analog?std::min(1.f,std::hypot(x,y)):1.f)));
 vm_write16(state+0x1b0e0,amount);vm_write16(state+0x1b0e4,amount);
 return result;
}
extern "C" void cv_modern_turn(){
 if(!modern||!steer||vm_read32(EXP_PLAYER_PTR)!=actor||vm_read8(actor+12)!=1||vm_read8(actor+13)!=0||vm_read8(actor+14)>13||vm_read8(actor+14)==12||(vm_read32(actor)&0x100000))return;
 const int32_t yaw=int32_t(vm_read32(actor+32));vm_write32(actor+32,uint32_t(yaw+int(turn(float(yaw),wanted))));
}
