#pragma once
#include <atomic>
extern std::atomic<bool> cv_battle_unlocked;
extern "C" {
void cv_enhanced_configure_env(void);
void cv_enhanced_language_set(int language);
int cv_enhanced_language(void);
int cv_enhanced_get(int which); // settings bridge IDs 8 music/voice, 9 effects, 10 Battle Game
void cv_enhanced_set(int which,int value);
}
