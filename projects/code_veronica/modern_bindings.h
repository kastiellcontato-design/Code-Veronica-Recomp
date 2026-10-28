#pragma once
#if defined(CV_REGION_JP)
#define EXP_PLAYER func_00173258
constexpr uint32_t EXP_PLAYER_PTR=0xA79C3C,EXP_CAMERA=0xBAE108;
#elif defined(CV_REGION_US)
#define EXP_PLAYER func_00173290
constexpr uint32_t EXP_PLAYER_PTR=0xA79BD8,EXP_CAMERA=0xBAE088;
#else
#define EXP_PLAYER func_00174C28
constexpr uint32_t EXP_PLAYER_PTR=0xA89BD8,EXP_CAMERA=0xBBE388;
#endif
