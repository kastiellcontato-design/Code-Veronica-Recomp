#ifndef CV_NP_TROPHY_HOST_H
#define CV_NP_TROPHY_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SceNpTrophyHostGame {uint32_t count,grades[5];char title[128],description[1024];} SceNpTrophyHostGame;
typedef struct SceNpTrophyHostItem {uint32_t grade;uint8_t hidden;uint64_t timestamp;char name[128],description[1024];} SceNpTrophyHostItem;
typedef struct SceNpTrophyHostBackend {
 int (*query)(const char* commid,int id,SceNpTrophyHostGame* game,SceNpTrophyHostItem* item);
 // 0=committed, 1=already unlocked, 2=disabled, 3=invalid, 4=storage error.
 int (*unlock)(const char* commid,int id,uint64_t* timestamp);
} SceNpTrophyHostBackend;
extern const SceNpTrophyHostBackend* g_sce_np_trophy_host;
#ifdef __cplusplus
}
#endif
#endif
