#pragma once
#include <atomic>
#include <cstdint>
extern std::atomic<unsigned> cv_cheats_flags;
enum { CV_CHEAT_HEALTH=1, CV_CHEAT_AMMO=2 };
extern "C" void cv_cheats_set(bool health,bool ammo);
extern "C" void cv_cheats_frame();
uint32_t cv_cheats_filter(uint64_t address,uint32_t value,unsigned width,unsigned flags);
