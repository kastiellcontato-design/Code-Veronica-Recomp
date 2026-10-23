/* Opt-in pacing experiment. Keep the guest's real-time delta calculation.
 * The first vblank-only experiment did not unlock this title's 30 FPS cap.
 * Addresses matched independently in each region's lifted frame routine.
 */
#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_fps_clock.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <atomic>
void CV_OG_FRAME_PACE(ppu_context*);
// These methods are invoked virtually through the guest function registry.
// There are no direct call sites in any of the three lifted images.
#if defined(CV_REGION_JP)
#define CV_BIO_UPDATE func_0020E9C0
static constexpr uint32_t bio_update_address=0x0020E9C0;
#elif defined(CV_REGION_US)
#define CV_BIO_UPDATE func_0020E0D0
static constexpr uint32_t bio_update_address=0x0020E0D0;
#else
#define CV_BIO_UPDATE func_00210070
static constexpr uint32_t bio_update_address=0x00210070;
#endif
void CV_BIO_UPDATE(ppu_context*);
extern "C" void ppu_register_function(uint64_t,void (*)(ppu_context*));
extern "C" void rsx_d3d12_pose_scene(uint32_t);
extern "C" int rsx_d3d12_pose_replay_enabled(void);
namespace {
// Publish pacing to a possible engine worker as one consistent snapshot.
std::atomic<uint64_t> timing{0};
float read_float(uint32_t a) { uint32_t b=vm_read32(a); float f; std::memcpy(&f,&b,4); return f; }
void write_float(uint32_t a,float f) { uint32_t b; std::memcpy(&b,&f,4);vm_write32(a,b); }
bool profile_enabled() {
    static const bool on=[] {const char* e=std::getenv("CV_FRAME_PROFILE");return e&&std::strcmp(e,"1")==0;}();
    return on;
}
bool enabled() {
    static const bool on=[] {const char* e=std::getenv("CV_FPS_TEST_60");return e&&std::strcmp(e,"1")==0;}();
    return on;
}
// CV_FPS_UNLOCKED=1 (benchmark shortcut only): the engine cap goes to 1000
// instead of 60, so the frame rate shows how far the machine can go.
float frame_cap() {
    static const float cap=[] {const char* e=std::getenv("CV_FPS_UNLOCKED");return e&&std::strcmp(e,"1")==0?1000.0f:60.0f;}();
    return cap;
}
// The renderer presents every native 30 Hz frame twice (interpolated, then
// recorded): the guest keeps its own pacing, one update and one draw a tick.
bool replay() {
    static const bool on=rsx_d3d12_pose_replay_enabled()!=0;
    return on;
}
}
void CV_FN_FRAME_PACE(ppu_context* ctx) {
    if(!enabled()) { CV_OG_FRAME_PACE(ctx);return; }
    const uint32_t object=static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t previous=vm_read32(object+0x64);
    const uint8_t mode=vm_read8(object+0x68);
    const float base=read_float(object+0x60),cap=read_float(object+0x64);
    // Game init writes 30 into +60. Fixed-step mode (+68 != 0) waits
    // for one full logical tick. Select the original variable-step path,
    // with a 60 FPS cap: target delta is 30/60 = 0.5 logical ticks.
    // Keep the base at 30 so elapsed game time is not doubled.
    const bool change=base==30.0f && cap>=30.0f && cap<=3000.0f && mode<=1 && !replay();
    if(change) {write_float(object+0x64,frame_cap());vm_write8(object+0x68,0);}
    static bool announced=false;
    if(!announced) {
        std::fprintf(stderr,"[FPS60 %s] engine object=%08X base=%g cap=%g mode=%u override=%u\n",CV_TITLE_ID,object,base,cap,unsigned(mode),unsigned(change));
        announced=true;
    }
    CV_OG_FRAME_PACE(ctx);
    timing.store(change?(0x100000000ull|vm_read32(object+0x90)):0,std::memory_order_release);
    // No lasting guest setting. Leave a change made by the original routine alone.
    if(change) {
        if(read_float(object+0x64)==frame_cap())vm_write32(object+0x64,previous);
        if(vm_read8(object+0x68)==0)vm_write8(object+0x68,mode);
    }
    if(!profile_enabled())return;
    using Clock=std::chrono::steady_clock;
    static auto start=Clock::now();static unsigned frames=0;static double steps=0;
    frames++;steps+=read_float(object+0x90);
    const auto now=Clock::now();const double elapsed=std::chrono::duration<double>(now-start).count();
    if(elapsed>=5.0) {
        std::fprintf(stderr,"[FPS60 %s] pacing_calls_per_second=%.2f delta_ticks_per_second=%.2f last_step=%.4f override=%u\n",CV_TITLE_ID,frames/elapsed,steps/elapsed,read_float(object+0x90),unsigned(change));
        start=now;frames=0;steps=0;
    }
}

namespace {
void bio_update_30hz(ppu_context* ctx) {
    static thread_local cv_fps::LogicClock clock;
    const uint64_t sample=timing.load(std::memory_order_acquire);
    unsigned count=1;float elapsed_ticks=1;
    if(!(sample>>32)) {
        // Native pacing (replay or unrecognised object): one update a frame.
        clock.reset();CV_BIO_UPDATE(ctx);
        if(!replay())return;
    }else {
        uint32_t bits=static_cast<uint32_t>(sample);std::memcpy(&elapsed_ticks,&bits,4);
        count=clock.advance(elapsed_ticks);
        // The virtual method is void. Skipped updates leave the incoming guest
        // context alone; each executed update gets the same incoming arguments.
        const ppu_context incoming=*ctx;
        for(unsigned i=0;i<count;i++) {*ctx=incoming;CV_BIO_UPDATE(ctx);}
    }
    static const bool interpolate=[] {const char* e=std::getenv("CV_RENDER_INTERPOLATION");return e&&std::strcmp(e,"1")==0;}();
    if(interpolate) {
        static const uint32_t markers[]={0x3D650,0x3C4FC,0x3A4D0,0x3E814};
        uint32_t c=vm_read8(CV_SRT_CHARACTER),scene=0;
        if(c<4&&vm_read32(CV_SRT_RDX)==markers[c])scene=0x80000000u|(uint32_t(vm_read8(CV_SRT_ROOM))<<16)|(uint32_t(vm_read8(CV_SRT_ROOM+1))<<8)|c;
        rsx_d3d12_pose_scene(scene);
    }
    if(!profile_enabled())return;
    using Clock=std::chrono::steady_clock;
    static thread_local auto start=Clock::now();
    static thread_local unsigned draws=0,updates=0;
    draws++;updates+=count;
    const auto now=Clock::now();double seconds=std::chrono::duration<double>(now-start).count();
    if(seconds>=5.0) {
        std::fprintf(stderr,"[FPS60-LOGIC %s] render_updates_per_second=%.2f game_updates_per_second=%.2f ticks=%.4f\n",CV_TITLE_ID,draws/seconds,updates/seconds,elapsed_ticks);
        draws=0;updates=0;start=now;
    }
}
}
extern "C" void cv_fps_register_game_update() {
    const char* raw=std::getenv("CV_FPS_BENCHMARK_RAW");
    if(!enabled())return;
    if(raw&&std::strcmp(raw,"1")==0) {
        std::fprintf(stderr,"[FPS60] RAW benchmark: game logic still runs once per rendered frame\n");return;
    }
    ppu_register_function(bio_update_address,bio_update_30hz);
    std::fprintf(stderr,"[FPS60] 30 Hz game update experiment registered at %08X; rendering kept separate\n",bio_update_address);
}
