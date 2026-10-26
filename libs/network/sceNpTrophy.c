/*
 * ps3recomp - sceNpTrophy HLE implementation
 *
 * Trophy management with persistent storage.  Unlocked trophies are saved
 * to a JSON file at gamedata/trophies/{commId}.json.
 *
 * JSON format (hand-written, no external dependency):
 * { "trophies": [ { "id": N, "timestamp": T }, ... ] }
 */

#include "sceNpTrophy.h"
#include "sceNpTrophyHost.h"
const SceNpTrophyHostBackend* g_sce_np_trophy_host = 0;
#include "../system/cellSysutil.h"
#include "ps3emu/guest_call.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "../../runtime/ppu/ppu_memory.h"   /* vm_base (guest mem) */
#include "ps3emu/endian.h"                   /* ps3_bswap64 -- guest is big-endian */
#include "../guest_struct.h"   /* GUEST_EA, guest_struct_load/store */
/* HLE args arrive as guest effective addresses; translate before deref. */
#define GUEST_PTR(p, T) ((T)((p) ? (void*)(vm_base + (uint32_t)(uintptr_t)(p)) : (void*)0))

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir_p(path) _mkdir(path)
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_p(path) mkdir(path, 0755)
#define PATH_SEP '/'
#endif

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

static int s_trophy_initialized = 0;
static char s_storage_path[512] = "gamedata/trophies";

/* Per-context data */
typedef struct {
    int                      in_use;
    int                      registered;
    SceNpCommunicationId     commId;
    u8                       unlocked[SCE_NP_TROPHY_MAX_NUM_TROPHIES];
    u64                      unlock_time[SCE_NP_TROPHY_MAX_NUM_TROPHIES];
    u32                      total_trophies;
} TrophyContext;

typedef struct {
    int in_use;
} TrophyHandle;

/* ID zero is the firmware invalid sentinel; retain all four usable slots. */
static TrophyContext s_contexts[SCE_NP_TROPHY_MAX_CONTEXTS + 1];
static TrophyHandle  s_handles[SCE_NP_TROPHY_MAX_HANDLES + 1];

/* Host callbacks never expose guest pointers. The runner binds one catalog
 * before starting guest threads. Generic toolkit users retain the old store. */
static void trophy_commid(const TrophyContext* ctx,char out[32]) {
    snprintf(out,32,"%.*s_%02u",SCE_NP_COMMUNICATION_ID_MAX_LENGTH,ctx->commId.data,ctx->commId.num);
}
static int trophy_host_info(TrophyContext* ctx,int id,SceNpTrophyHostGame* game,SceNpTrophyHostItem* item) {
    if(!g_sce_np_trophy_host)return 0;
    char commid[32];trophy_commid(ctx,commid);
    return g_sce_np_trophy_host->query(commid,id,game,item);
}
static void trophy_host_sync(TrophyContext* ctx) {
    SceNpTrophyHostGame game;
    if(!trophy_host_info(ctx,-1,&game,NULL))return;
    ctx->total_trophies=game.count<=128?game.count:128;
    for(u32 i=0;i<ctx->total_trophies;i++) {
        SceNpTrophyHostItem item;
        if(trophy_host_info(ctx,(int)i,NULL,&item)) {
            ctx->unlock_time[i]=item.timestamp;ctx->unlocked[i]=item.timestamp!=0;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Persistent storage helpers
 * -----------------------------------------------------------------------*/

static void trophy_ensure_dir(void)
{
    char tmp[512];
    char* p;

    strncpy(tmp, s_storage_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    /* Create directories recursively */
    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            mkdir_p(tmp);
            *p = PATH_SEP;
        }
    }
    mkdir_p(tmp);
}

static void trophy_get_filepath(const SceNpCommunicationId* commId,
                                 char* buf, size_t bufsize)
{
    snprintf(buf, bufsize, "%s/%s.json", s_storage_path, commId->data);
}

static void trophy_save(TrophyContext* ctx)
{
    if(g_sce_np_trophy_host)return; /* committed atomically by the host backend */
    char filepath[1024];
    FILE* fp;
    int first = 1;

    trophy_ensure_dir();
    trophy_get_filepath(&ctx->commId, filepath, sizeof(filepath));

    fp = fopen(filepath, "w");
    if (!fp) {
        printf("[sceNpTrophy] WARNING: Could not save trophies to %s\n",
               filepath);
        return;
    }

    fprintf(fp, "{\n  \"trophies\": [");
    for (u32 i = 0; i < ctx->total_trophies; i++) {
        if (ctx->unlocked[i]) {
            if (!first) fprintf(fp, ",");
            fprintf(fp, "\n    { \"id\": %u, \"timestamp\": %llu }",
                    i, (unsigned long long)ctx->unlock_time[i]);
            first = 0;
        }
    }
    fprintf(fp, "\n  ]\n}\n");
    fclose(fp);
}

static void trophy_load(TrophyContext* ctx)
{
    if(g_sce_np_trophy_host){trophy_host_sync(ctx);return;}
    char filepath[1024];
    FILE* fp;
    char line[256];

    trophy_get_filepath(&ctx->commId, filepath, sizeof(filepath));

    fp = fopen(filepath, "r");
    if (!fp)
        return; /* no saved data, all locked */

    /*
     * Simple JSON parser -- look for "id": N and "timestamp": T patterns.
     * This avoids any external JSON library dependency.
     */
    while (fgets(line, sizeof(line), fp)) {
        const char* id_pos = strstr(line, "\"id\":");
        if (id_pos) {
            u32 tid = 0;
            u64 ts = 0;
            const char* ts_pos;

            if (sscanf(id_pos, "\"id\": %u", &tid) == 1 &&
                tid < SCE_NP_TROPHY_MAX_NUM_TROPHIES) {
                ts_pos = strstr(line, "\"timestamp\":");
                if (ts_pos)
                    sscanf(ts_pos, "\"timestamp\": %llu",
                           (unsigned long long*)&ts);
                ctx->unlocked[tid] = 1;
                ctx->unlock_time[tid] = ts;
            }
        }
    }
    fclose(fp);

    printf("[sceNpTrophy] Loaded trophy data from %s\n", filepath);
}

static u32 trophy_count_unlocked(TrophyContext* ctx)
{
    u32 count = 0;
    for (u32 i = 0; i < ctx->total_trophies; i++)
        if (ctx->unlocked[i]) count++;
    return count;
}

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

void sceNpTrophySetStoragePath(const char* path)
{
    path = GUEST_PTR(path, const char*);
    if (path) {
        strncpy(s_storage_path, path, sizeof(s_storage_path) - 1);
        s_storage_path[sizeof(s_storage_path) - 1] = '\0';
    }
}

s32 sceNpTrophyInit(void* poolPtr, u32 poolSize, u32 containerId, u64 options)
{
    (void)poolPtr; (void)poolSize; (void)containerId; (void)options;

    printf("[sceNpTrophy] Init()\n");

    if (s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_ALREADY_INITIALIZED;

    memset(s_contexts, 0, sizeof(s_contexts));
    memset(s_handles, 0, sizeof(s_handles));
    s_trophy_initialized = 1;
    return CELL_OK;
}

s32 sceNpTrophyTerm(void)
{
    printf("[sceNpTrophy] Term()\n");

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    /* Save all registered contexts */
    for (int i = 1; i <= SCE_NP_TROPHY_MAX_CONTEXTS; i++) {
        if (s_contexts[i].in_use && s_contexts[i].registered)
            trophy_save(&s_contexts[i]);
    }

    memset(s_contexts, 0, sizeof(s_contexts));
    memset(s_handles, 0, sizeof(s_handles));
    s_trophy_initialized = 0;
    return CELL_OK;
}

s32 sceNpTrophyCreateContext(SceNpTrophyContext* context,
                             const SceNpCommunicationId* commId,
                             const SceNpCommunicationSignature* commSign,
                             u64 options)
{
    (void)commSign; (void)options;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (!context || !commId)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    const SceNpCommunicationId* commId_h = GUEST_PTR(commId, const SceNpCommunicationId*);

    for (s32 i = 1; i <= SCE_NP_TROPHY_MAX_CONTEXTS; i++) {
        if (!s_contexts[i].in_use) {
            memset(&s_contexts[i], 0, sizeof(TrophyContext));
            s_contexts[i].in_use = 1;
            s_contexts[i].commId = *commId_h;
            s_contexts[i].total_trophies = SCE_NP_TROPHY_MAX_NUM_TROPHIES;
            vm_write32(GUEST_EA(context), (u32)i);
            printf("[sceNpTrophy] CreateContext(commId=\"%s\") -> ctx=%d\n",
                   commId_h->data, i);
            return CELL_OK;
        }
    }

    return SCE_NP_TROPHY_ERROR_OUT_OF_MEMORY;
}

s32 sceNpTrophyDestroyContext(SceNpTrophyContext context)
{
    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (s_contexts[context].registered)
        trophy_save(&s_contexts[context]);

    s_contexts[context].in_use = 0;
    printf("[sceNpTrophy] DestroyContext(ctx=%d)\n", context);
    return CELL_OK;
}

s32 sceNpTrophyCreateHandle(SceNpTrophyHandle* handle)
{
    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (!handle)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;

    for (s32 i = 1; i <= SCE_NP_TROPHY_MAX_HANDLES; i++) {
        if (!s_handles[i].in_use) {
            s_handles[i].in_use = 1;
            vm_write32(GUEST_EA(handle), (u32)i);
            printf("[sceNpTrophy] CreateHandle() -> handle=%d\n", i);
            return CELL_OK;
        }
    }

    return SCE_NP_TROPHY_ERROR_OUT_OF_MEMORY;
}

s32 sceNpTrophyDestroyHandle(SceNpTrophyHandle handle)
{
    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (handle <= 0 || handle > SCE_NP_TROPHY_MAX_HANDLES ||
        !s_handles[handle].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_HANDLE;

    s_handles[handle].in_use = 0;
    printf("[sceNpTrophy] DestroyHandle(handle=%d)\n", handle);
    return CELL_OK;
}

s32 sceNpTrophyRegisterContext(SceNpTrophyContext context,
                               SceNpTrophyHandle handle,
                               u32 statusCb,
                               u32 arg,
                               u64 options)
{
    (void)options;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (handle <= 0 || handle > SCE_NP_TROPHY_MAX_HANDLES ||
        !s_handles[handle].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_HANDLE;

    if (s_contexts[context].registered)
        return SCE_NP_TROPHY_ERROR_CONTEXT_ALREADY_REG;

    if (!statusCb) return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    if (!g_ps3_guest_caller) return SCE_NP_TROPHY_ERROR_UNKNOWN;

    /* Load any previously saved trophy data */
    trophy_load(&s_contexts[context]);
    s_contexts[context].registered = 1;

    printf("[sceNpTrophy] RegisterContext(ctx=%d, handle=%d, statusCb=0x%08X, "
           "arg=0x%08X) -> loaded saved data\n", context, handle, statusCb, arg);

    /* Use the runner's OPD bridge, not the optional code/TOC scaffold entry
     * point. Registration status is synchronous; its terminal notification is
     * delivered through sysutil polling, after the HLE storage load completes.
     * Intermediate firmware installation progress is not simulated here. */
    g_ps3_guest_caller(statusCb, (u32)context, SCE_NP_TROPHY_STATUS_INSTALLED,
                       0, 0, arg, 0, 0, 0);
    if (!s_trophy_initialized) return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;
    if (!s_contexts[context].in_use) return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;
    if (!s_handles[handle].in_use) return SCE_NP_TROPHY_ERROR_INVALID_HANDLE;
    const u64 completion[8] = {(u32)context, SCE_NP_TROPHY_STATUS_PROCESSING_COMPLETE,
                               0, 0, arg, 0, 0, 0};
    if (cellSysutilQueueGuestCallbackArgs(statusCb, completion) != CELL_OK) {
        s_contexts[context].registered = 0;
        return SCE_NP_TROPHY_ERROR_OUT_OF_MEMORY;
    }

    return CELL_OK;
}

s32 sceNpTrophyGetRequiredDiskSpace(SceNpTrophyContext context,
                                    SceNpTrophyHandle handle,
                                    u64* reqSpace, u64 options)
{
    (void)handle; (void)options;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (!reqSpace)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    reqSpace = GUEST_PTR(reqSpace, u64*);

    /* Typical trophy pack size. The guest is big-endian and reads this u64
     * back in BE order, so the value MUST be byte-swapped on write -- otherwise
     * a host-LE 0x100000 is read as 0x0010000000000000 (2^44). LBP feeds this
     * "required disk space" straight into its save-data free-space accounting
     * (sub_379EC0: errB = budget - usedKB - reqSpaceKB); a garbage 2^44 makes
     * errB underflow negative -> cellGame GameData check returns ERROR-B ->
     * boot bails. (Same LE-write class as the cellUserInfo fix.) */
    *reqSpace = ps3_bswap64((u64)(1024 * 1024)); /* 1 MB, big-endian */
    printf("[sceNpTrophy] GetRequiredDiskSpace(ctx=%d) -> 1MB\n", context);
    return CELL_OK;
}

s32 sceNpTrophyGetGameInfo(SceNpTrophyContext context,
                           SceNpTrophyHandle handle,
                           SceNpTrophyGameDetails* details,
                           SceNpTrophyGameData* data)
{
    (void)handle;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (!s_contexts[context].registered)
        return SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED;
    details = GUEST_PTR(details, SceNpTrophyGameDetails*);
    data = GUEST_PTR(data, SceNpTrophyGameData*);

    TrophyContext* ctx=&s_contexts[context];trophy_host_sync(ctx);
    SceNpTrophyHostGame info={0};int hosted=trophy_host_info(ctx,-1,&info,NULL);
    if(!details&&!data)return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    if (details) {
        memset(details,0,sizeof(*details));
        details->numTrophies=ps3_bswap32(hosted?info.count:ctx->total_trophies);
        details->numPlatinum=ps3_bswap32(hosted?info.grades[1]:0);
        details->numGold=ps3_bswap32(hosted?info.grades[2]:0);
        details->numSilver=ps3_bswap32(hosted?info.grades[3]:0);
        details->numBronze=ps3_bswap32(hosted?info.grades[4]:ctx->total_trophies);
        snprintf(details->title,sizeof details->title,"%s",hosted?info.title:"PS3 Game");
        snprintf(details->description,sizeof details->description,"%s",hosted?info.description:"Trophy set");
    }
    if(data) {
        u32 grades[5]={0};memset(data,0,sizeof(*data));
        for(u32 i=0;i<ctx->total_trophies;i++)if(ctx->unlocked[i]){
            SceNpTrophyHostItem item={0};u32 grade=trophy_host_info(ctx,(int)i,NULL,&item)?item.grade:4;
            if(grade>=1&&grade<=4)grades[grade]++;
        }
        data->unlockedTrophies=ps3_bswap32(trophy_count_unlocked(ctx));
        data->unlockedPlatinum=ps3_bswap32(grades[1]);data->unlockedGold=ps3_bswap32(grades[2]);
        data->unlockedSilver=ps3_bswap32(grades[3]);data->unlockedBronze=ps3_bswap32(grades[4]);
    }

    printf("[sceNpTrophy] GetGameInfo(ctx=%d)\n", context);
    return CELL_OK;
}

s32 sceNpTrophyGetTrophyInfo(SceNpTrophyContext context,
                             SceNpTrophyHandle handle,
                             SceNpTrophyId trophyId,
                             SceNpTrophyDetails* details,
                             SceNpTrophyData* data)
{
    (void)handle;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (!s_contexts[context].registered)
        return SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED;

    if (trophyId < 0 || (u32)trophyId >= s_contexts[context].total_trophies)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    details = GUEST_PTR(details, SceNpTrophyDetails*);
    data = GUEST_PTR(data, SceNpTrophyData*);

    TrophyContext* ctx=&s_contexts[context];trophy_host_sync(ctx);
    SceNpTrophyHostItem item={0};int hosted=trophy_host_info(ctx,trophyId,NULL,&item);
    if(g_sce_np_trophy_host&&!hosted)return SCE_NP_TROPHY_ERROR_NOT_FOUND;
    if(!details&&!data)return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    if(details){
        memset(details,0,sizeof(*details));details->trophyId=ps3_bswap32((u32)trophyId);
        details->trophyGrade=ps3_bswap32(hosted?item.grade:SCE_NP_TROPHY_GRADE_BRONZE);
        if(hosted){snprintf(details->name,sizeof details->name,"%s",item.name);snprintf(details->description,sizeof details->description,"%s",item.description);details->hidden=item.hidden;}
        else{snprintf(details->name,sizeof details->name,"Trophy %d",trophyId);snprintf(details->description,sizeof details->description,"Trophy #%d description",trophyId);}
    }
    if(data){
        memset(data,0,sizeof(*data));data->trophyId=ps3_bswap32((u32)trophyId);
        data->unlocked=ctx->unlocked[trophyId];
        data->timestamp=ps3_bswap64(ctx->unlocked[trophyId]?(ctx->unlock_time[trophyId]+62135596800ULL)*1000000ULL:0);
    }

    return CELL_OK;
}

s32 sceNpTrophyUnlockTrophy(SceNpTrophyContext context,
                            SceNpTrophyHandle handle,
                            SceNpTrophyId trophyId,
                            SceNpTrophyId* platinumId)
{
    (void)handle;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (!s_contexts[context].registered)
        return SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED;

    if (trophyId < 0 || (u32)trophyId >= s_contexts[context].total_trophies)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;

    if(g_sce_np_trophy_host){
        TrophyContext* ctx=&s_contexts[context];char commid[32];trophy_commid(ctx,commid);
        uint64_t timestamp=0;int result=g_sce_np_trophy_host->unlock(commid,trophyId,&timestamp);
        if(platinumId)vm_write32(GUEST_EA(platinumId),(u32)-1);
        if(result==2)return CELL_OK; /* recording explicitly disabled */
        if(result==3)return SCE_NP_TROPHY_ERROR_NOT_FOUND;
        if(result==4)return SCE_NP_TROPHY_ERROR_UNKNOWN;
        ctx->unlocked[trophyId]=1;ctx->unlock_time[trophyId]=timestamp;
        return result==1?SCE_NP_TROPHY_ERROR_ALREADY_UNLOCKED:CELL_OK;
    }
    if (s_contexts[context].unlocked[trophyId])
        return SCE_NP_TROPHY_ERROR_ALREADY_UNLOCKED;

    /* Unlock the trophy */
    s_contexts[context].unlocked[trophyId] = 1;
    s_contexts[context].unlock_time[trophyId] = (u64)time(NULL);

    printf("************************************************************\n");
    printf("*  TROPHY UNLOCKED!  Trophy #%d                            \n",
           trophyId);
    printf("*  Context: %d  CommId: %s                                 \n",
           context, s_contexts[context].commId.data);
    printf("************************************************************\n");

    /* Save immediately */
    trophy_save(&s_contexts[context]);

    /* Check if all non-platinum trophies are unlocked -> platinum */
    if (platinumId) {
        vm_write32((u32)(uintptr_t)platinumId, (u32)SCE_NP_TROPHY_INVALID_TROPHY_ID);
        /* Simplified: don't auto-award platinum without grade info */
    }

    return CELL_OK;
}

s32 sceNpTrophyGetTrophyUnlockState(SceNpTrophyContext context,
                                    SceNpTrophyHandle handle,
                                    SceNpTrophyFlagArray* flags,
                                    u32* count)
{
    (void)handle;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (!s_contexts[context].registered)
        return SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED;

    if (!flags || !count)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;

    trophy_host_sync(&s_contexts[context]);
    u32 flags_ea = (u32)(uintptr_t)flags;
    for (u32 o = 0; o < sizeof(SceNpTrophyFlagArray); o += 4)
        vm_write32(flags_ea + o, 0);

    for (u32 i = 0; i < s_contexts[context].total_trophies; i++) {
        if (s_contexts[context].unlocked[i])
            {
            u32 word_ea = flags_ea + (i / 32) * 4;
            vm_write32(word_ea, vm_read32(word_ea) | (1u << (i % 32)));
        }
    }

    vm_write32((u32)(uintptr_t)count, (u32)s_contexts[context].total_trophies);
    return CELL_OK;
}

s32 sceNpTrophyGetGameProgress(SceNpTrophyContext context,
                               SceNpTrophyHandle handle,
                               s32* percentage)
{
    (void)handle;

    if (!s_trophy_initialized)
        return SCE_NP_TROPHY_ERROR_NOT_INITIALIZED;

    if (context <= 0 || context > SCE_NP_TROPHY_MAX_CONTEXTS ||
        !s_contexts[context].in_use)
        return SCE_NP_TROPHY_ERROR_INVALID_CONTEXT;

    if (!s_contexts[context].registered)
        return SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED;

    if (!percentage)
        return SCE_NP_TROPHY_ERROR_INVALID_ARGUMENT;

    trophy_host_sync(&s_contexts[context]);
    u32 total = s_contexts[context].total_trophies;
    u32 unlocked = trophy_count_unlocked(&s_contexts[context]);

    s32 pct = total > 0 ? (s32)((unlocked * 100) / total) : 0;
    vm_write32(GUEST_EA(percentage), (u32)pct);

    printf("[sceNpTrophy] GetGameProgress(ctx=%d) -> %d%%\n",
           context, pct);
    return CELL_OK;
}
