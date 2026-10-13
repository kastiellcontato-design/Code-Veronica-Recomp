/*
 * ps3recomp - cellDmux HLE implementation
 *
 * MPEG program stream PES extraction with guest-resident ES payloads.
 * Retains split PES packets between calls. The video decoder parses picture
 * boundaries in the extracted elementary byte stream.
 */

#include "cellDmux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../runtime/platform/win32_compat.h"
#include "ps3emu/guest_call.h"
#include "../../runtime/memory/vm.h"
#include "../../runtime/ppu/ppu_memory.h"   /* vm_write*: guest EA -> host, byte-swapped */
#include "../guest_struct.h"   /* GUEST_EA, guest_struct_load/store */

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

typedef struct {
    int in_use;
    HANDLE event,worker;
    volatile LONG busy,stopping;
    b8 discontinuity;
    u32 cbFunc;
    u32 cbArg;
    u32 streamType;
    /* Stream data tracking for demux processing */
    u32 streamAddr;
    u32 streamSize;
    u64 userData;
    u8* pending;
    u32 pendingSize;
    u32 auSeqNo;        /* running AU sequence counter */
} DmuxSlot;

typedef struct {
    int in_use;
    u32 dmuxId;
    CellDmuxEsFilterId filterId;
    u32 esCbFunc;
    u32 esCbArg;
    CellDmuxAuInfo currentAu;
    volatile LONG hasAu;
    int acquired;
    u32 resource, resourceSize;
} DmuxEsSlot;

static DmuxSlot s_dmux[CELL_DMUX_MAX_HANDLES];
static DmuxEsSlot s_es[CELL_DMUX_MAX_ES];

static s32 dmux_process_stream(CellDmuxHandle handle,u32 streamAddr,u32 streamSize,b8 discontinuity,u64 userData);
static void dmux_notify(u32 h, u32 type);
static int dmux_has_unreleased_au(u32 h)
{
    for (int i = 0; i < CELL_DMUX_MAX_ES; ++i)
        if (s_es[i].in_use && s_es[i].dmuxId == h && s_es[i].hasAu)
            return 1;
    return 0;
}

static DWORD WINAPI dmux_worker(LPVOID arg)
{
    u32 h = (u32)(uintptr_t)arg;
    DmuxSlot* d = &s_dmux[h];
    for (;;) {
        WaitForSingleObject(d->event, INFINITE);
        if (d->stopping) break;
        if (!InterlockedCompareExchange(&d->busy, 0, 0)) continue;

        /* Keep the asynchronous input buffer pinned while prior ES AUs are
         * outstanding. A rejected short final SetStream can otherwise be lost. */
        while (!d->stopping && dmux_has_unreleased_au(h))
            WaitForSingleObject(d->event, INFINITE);
        if (d->stopping) break;
        if (!InterlockedCompareExchange(&d->busy, 0, 0)) continue;

        s32 ret = dmux_process_stream(h, d->streamAddr, d->streamSize,
                                      d->discontinuity, d->userData);
        if (ret != CELL_OK) {
            InterlockedExchange(&d->busy, 0);
            fprintf(stderr, "[cellDmux] PROCESS_ERROR=%08X\n", (u32)ret);
            dmux_notify(h, CELL_DMUX_MSG_TYPE_FATAL_ERR);
        }
    }
    return 0;
}
/* Read once: these traces sit on per-AU paths. */
static int dmux_trace(void){static int t=-1;if(t<0)t=getenv("CV_AV_TRACE")?1:0;return t;}
/* Callback messages reside in the guest address space; callbacks are guest OPDs. */
static void dmux_notify(u32 h, u32 type)
{
    if(dmux_trace())fprintf(stderr,"[cellDmux] NOTIFY_BEGIN h=%u type=%u user=%llu\n",h,type,(unsigned long long)s_dmux[h].userData);
    u32 ea = VM_HLE_INJECT_BASE + 0x60000 + h * 0x20;
    vm_write32(ea, type); vm_write32(ea+4, 0); vm_write64(ea+8,s_dmux[h].userData);
    if (s_dmux[h].cbFunc && g_ps3_guest_caller)
        g_ps3_guest_caller(s_dmux[h].cbFunc,h,ea,s_dmux[h].cbArg,0,0,0,0,0);
    if(dmux_trace())fprintf(stderr,"[cellDmux] NOTIFY_END h=%u type=%u\n",h,type);
}
static void dmux_es_notify(u32 h, u32 type)
{
    DmuxEsSlot* es=&s_es[h];
    u32 ea = VM_HLE_INJECT_BASE + 0x60200 + h * 0x20;
    vm_write32(ea,type); vm_write32(ea+4,0); vm_write64(ea+8,s_dmux[es->dmuxId].userData);
    if (es->esCbFunc && g_ps3_guest_caller)
        g_ps3_guest_caller(es->esCbFunc,es->dmuxId,h,ea,es->esCbArg,0,0,0,0);
}
s32 cellDmuxQueryAttr(const CellDmuxType* type, void* attr)
{
    if (!type || !attr || vm_read32(GUEST_EA(type)) != CELL_DMUX_TYPE_PAMF)
        return (s32)CELL_DMUX_ERROR_ARG;
    /* Host parser state; reserve space for guest message/context storage. */
    vm_write32(GUEST_EA(attr),0x10000);
    vm_write32(GUEST_EA(attr)+4,0x260000);
    vm_write32(GUEST_EA(attr)+8,0x280000);
    fprintf(stderr,"[cellDmux] QueryAttr -> memSize=65536\n");
    return CELL_OK;
}
s32 cellDmuxQueryEsAttr(const CellDmuxType* type, const CellDmuxEsFilterId* filter, const void* specific, void* attr)
{
    (void)specific;
    if (!type || !filter || !attr || vm_read32(GUEST_EA(type)) != CELL_DMUX_TYPE_PAMF)
        return (s32)CELL_DMUX_ERROR_ARG;
    vm_write32(GUEST_EA(attr),0x400000);
    return CELL_OK;
}
s32 cellDmuxOpenEx(const CellDmuxType* type, const void* res, const void* cb, CellDmuxHandle* handle)
{
    return cellDmuxOpen(type,(const CellDmuxResource*)res,cb,handle);
}

/* ---------------------------------------------------------------------------
 * Demuxer lifecycle
 * -----------------------------------------------------------------------*/

s32 cellDmuxOpen(const CellDmuxType* type, const CellDmuxResource* res,
                  const void* cb, CellDmuxHandle* handle)
{
    (void)res;

    printf("[cellDmux] Open(streamType=%u)\n",
           type ? vm_read32(GUEST_EA(type)) : 0);   /* streamType */

    if (!type || !handle)
        return (s32)CELL_DMUX_ERROR_ARG;

    for (int i = 0; i < CELL_DMUX_MAX_HANDLES; i++) {
        if (!s_dmux[i].in_use) {
            memset(&s_dmux[i],0,sizeof(s_dmux[i]));
            s_dmux[i].in_use = 1;
            s_dmux[i].event=CreateEventA(NULL,FALSE,FALSE,NULL);
            s_dmux[i].worker=CreateThread(NULL,0,dmux_worker,(LPVOID)(uintptr_t)i,0,NULL);
            if(!s_dmux[i].event||!s_dmux[i].worker)return (s32)CELL_DMUX_ERROR_FATAL;
            s_dmux[i].cbFunc = cb ? vm_read32(GUEST_EA(cb)) : 0;
            s_dmux[i].cbArg = cb ? vm_read32(GUEST_EA(cb)+4) : 0;
            s_dmux[i].streamType = vm_read32(GUEST_EA(type));
            s_dmux[i].streamAddr = 0;
            s_dmux[i].streamSize = 0;
            s_dmux[i].userData = 0;
            s_dmux[i].auSeqNo = 0;
            vm_write32((u32)(uintptr_t)handle, (u32)i);
            printf("[cellDmux] Open -> handle=%u\n", i);
            return CELL_OK;
        }
    }
    return (s32)CELL_DMUX_ERROR_BUSY;
}

s32 cellDmuxClose(CellDmuxHandle handle)
{
    printf("[cellDmux] Close(handle=%u)\n", handle);

    if (handle >= CELL_DMUX_MAX_HANDLES || !s_dmux[handle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;

    s_dmux[handle].stopping=1;SetEvent(s_dmux[handle].event);
    WaitForSingleObject(s_dmux[handle].worker,INFINITE);
    CloseHandle(s_dmux[handle].worker);CloseHandle(s_dmux[handle].event);
    /* Close associated ES handles */
    for (int i = 0; i < CELL_DMUX_MAX_ES; i++) {
        if (s_es[i].in_use && s_es[i].dmuxId == handle)
            s_es[i].in_use = 0;
    }

    free(s_dmux[handle].pending);s_dmux[handle].pending=NULL;s_dmux[handle].pendingSize=0;
    s_dmux[handle].in_use = 0;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * ES management
 * -----------------------------------------------------------------------*/

s32 cellDmuxEnableEs(CellDmuxHandle handle, const CellDmuxEsFilterId* esFilterId,
                      const CellDmuxEsResource* esRes,
                      const void* esCb, const void* esSpecificInfo,
                      CellDmuxEsHandle* esHandle)
{
    (void)esSpecificInfo;

    printf("[cellDmux] EnableEs(dmux=%u, major=%u, minor=%u)\n",
           handle,
           esFilterId ? vm_read32(GUEST_EA(esFilterId)
                        + (u32)offsetof(CellDmuxEsFilterId, filterIdMajor)) : 0,
           esFilterId ? vm_read32(GUEST_EA(esFilterId)
                        + (u32)offsetof(CellDmuxEsFilterId, filterIdMinor)) : 0);

    if (handle >= CELL_DMUX_MAX_HANDLES || !s_dmux[handle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;
    if (!esFilterId || !esHandle)
        return (s32)CELL_DMUX_ERROR_ARG;

    for (int i = 0; i < CELL_DMUX_MAX_ES; i++) {
        if (!s_es[i].in_use) {
            s_es[i].in_use = 1;
            s_es[i].dmuxId = handle;
            guest_struct_load(&s_es[i].filterId, GUEST_EA(esFilterId),
                              (u32)sizeof(s_es[i].filterId));
            s_es[i].esCbFunc = esCb ? vm_read32(GUEST_EA(esCb)) : 0;
            s_es[i].esCbArg = esCb ? vm_read32(GUEST_EA(esCb)+4) : 0;
            s_es[i].hasAu = 0;
            s_es[i].acquired = 0;
            s_es[i].resource = esRes ? vm_read32(GUEST_EA(esRes)) : 0;
            s_es[i].resourceSize = esRes ? vm_read32(GUEST_EA(esRes)+4) : 0;
            if (!s_es[i].resource || s_es[i].resourceSize < 0x100) { s_es[i].in_use=0; return (s32)CELL_DMUX_ERROR_ARG; }
            memset(&s_es[i].currentAu, 0, sizeof(CellDmuxAuInfo));
            vm_write32((u32)(uintptr_t)esHandle, (u32)i);
            return CELL_OK;
        }
    }
    return (s32)CELL_DMUX_ERROR_BUSY;
}

s32 cellDmuxDisableEs(CellDmuxEsHandle esHandle)
{
    printf("[cellDmux] DisableEs(es=%u)\n", esHandle);

    if (esHandle >= CELL_DMUX_MAX_ES || !s_es[esHandle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;

    s_es[esHandle].in_use = 0;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Data feeding
 * -----------------------------------------------------------------------*/

static s32 dmux_process_stream(CellDmuxHandle handle, u32 streamAddr, u32 streamSize,
                       b8 discontinuity, u64 userData)
{
    static unsigned stream_log;
    if (stream_log++ < 16) fprintf(stderr,"[cellDmux] SetStream(handle=%u, addr=0x%X, size=%u, discont=%d)\n", handle, streamAddr, streamSize, discontinuity);

    if (handle >= CELL_DMUX_MAX_HANDLES || !s_dmux[handle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;

    DmuxSlot* dmux = &s_dmux[handle];
    if(!streamAddr || !streamSize) return (s32)CELL_DMUX_ERROR_ARG;
    if(discontinuity){dmux->pendingSize=0;dmux->auSeqNo=0;}
    if(streamSize>0x1000000 || dmux->pendingSize>0x1000000-streamSize) return (s32)CELL_DMUX_ERROR_ARG;
    u32 total=dmux->pendingSize+streamSize;
    u8* data=realloc(dmux->pending,total);if(!data)return (s32)CELL_DMUX_ERROR_FATAL;
    dmux->pending=data;memcpy(data+dmux->pendingSize,vm_ptr8(streamAddr),streamSize);
    dmux->userData=userData;
    u32 output[CELL_DMUX_MAX_ES]={0};u64 pts[CELL_DMUX_MAX_ES],dts[CELL_DMUX_MAX_ES];
    for(int i=0;i<CELL_DMUX_MAX_ES;i++)pts[i]=dts[i]=~(u64)0;
    u32 at=0;
    int programEnd = 0;
    while(at+6<=total){
        if(data[at] || data[at+1] || data[at+2]!=1){at++;continue;}
        u8 id=data[at+3];
        if(id==0xba){if(at+14>total)break;u32 n=14+(data[at+13]&7);if(at+n>total)break;at+=n;continue;}
        if(id==0xb9){programEnd=1;at+=4;continue;}
        u32 n=((u32)data[at+4]<<8)|data[at+5];
        if(!n){fprintf(stderr,"[cellDmux] zero-length PES not supported\n");return (s32)CELL_DMUX_ERROR_FATAL;}
        if(at+6+n>total)break;
        if((id>=0xe0 && id<=0xef)||id==0xbd){
            if(n<3){at+=6+n;continue;}
            u32 offset=at+9+data[at+8],end=at+6+n;
            if(offset>end){at=end;continue;}
            u64 stamp=~(u64)0,decode=~(u64)0;
            if((data[at+7]&0x80)&&data[at+8]>=5){
                const u8* t=data+at+9;
                stamp=((u64)(t[0]&14)<<29)|((u64)t[1]<<22)|((u64)(t[2]&254)<<14)|((u64)t[3]<<7)|(t[4]>>1);
                decode=stamp;
                if((data[at+7]&0x40)&&data[at+8]>=10){t+=5;decode=((u64)(t[0]&14)<<29)|((u64)t[1]<<22)|((u64)(t[2]&254)<<14)|((u64)t[3]<<7)|(t[4]>>1);}
            }
            u32 minor=id==0xbd&&offset<end?data[offset]:0;
            if(id==0xbd){if(end-offset<4){at=end;continue;}offset+=4;}
            for(int i=0;i<CELL_DMUX_MAX_ES;i++){
                DmuxEsSlot* es=&s_es[i];
                if(!es->in_use||es->dmuxId!=handle||es->filterId.filterIdMajor!=id||es->filterId.filterIdMinor!=minor)continue;
                u32 bytes=end-offset;
                if(output[i]+bytes>es->resourceSize-0x100)return (s32)CELL_DMUX_ERROR_BUSY;
                memcpy(vm_ptr8(es->resource+0x100+output[i]),data+offset,bytes);output[i]+=bytes;
                if(pts[i]==~(u64)0){pts[i]=stamp;dts[i]=decode;}
            }
        }
        at+=6+n;
    }
    dmux->pendingSize=total-at;memmove(data,data+at,dmux->pendingSize);
    for(int i=0;i<CELL_DMUX_MAX_ES;i++)if(output[i]){
        DmuxEsSlot* es=&s_es[i];es->hasAu=1;es->acquired=0;
        es->currentAu.auAddr=es->resource+0x100;es->currentAu.auSize=output[i];
        es->currentAu.pts=pts[i];es->currentAu.dts=dts[i];es->currentAu.userData=userData;
        fprintf(stderr,"[cellDmux] ES_PAYLOAD es=%d bytes=%u pts=%llu\n",i,output[i],(unsigned long long)pts[i]);
        dmux_es_notify(i,CELL_DMUX_ES_MSG_TYPE_AU_FOUND);
    }
    dmux->auSeqNo++;
    InterlockedExchange(&dmux->busy, 0);
    dmux_notify(handle, CELL_DMUX_MSG_TYPE_DEMUX_DONE);
    if (programEnd)
        dmux_notify(handle, CELL_DMUX_MSG_TYPE_PROG_END_CODE);

    return CELL_OK;
}

s32 cellDmuxSetStream(CellDmuxHandle h,u32 addr,u32 size,b8 discontinuity,u64 user)
{
 if(h>=CELL_DMUX_MAX_HANDLES||!s_dmux[h].in_use||!addr||!size)return (s32)CELL_DMUX_ERROR_ARG;
 DmuxSlot* d=&s_dmux[h];
 if(InterlockedCompareExchange(&d->busy,1,0))return (s32)CELL_DMUX_ERROR_BUSY;
 d->streamAddr=addr;d->streamSize=size;d->discontinuity=discontinuity;d->userData=user;
 SetEvent(d->event);return CELL_OK;
}

s32 cellDmuxResetStream(CellDmuxHandle handle)
{
    printf("[cellDmux] ResetStream(handle=%u)\n", handle);

    if (handle >= CELL_DMUX_MAX_HANDLES || !s_dmux[handle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;

    /* Clear pending AUs from all ES handles and reset stream tracking */
    for (int i = 0; i < CELL_DMUX_MAX_ES; i++) {
        if (s_es[i].in_use && s_es[i].dmuxId == handle)
            s_es[i].hasAu = 0;
    }
    s_dmux[handle].pendingSize = 0;
    s_dmux[handle].streamAddr = 0;
    s_dmux[handle].streamSize = 0;
    s_dmux[handle].auSeqNo = 0;

    return CELL_OK;
}

s32 cellDmuxResetStreamAndWaitDone(CellDmuxHandle handle)
{
    return cellDmuxResetStream(handle);
}

/* ---------------------------------------------------------------------------
 * AU retrieval
 * -----------------------------------------------------------------------*/

/* SDK output is a guest pointer to the AU descriptor and a guest pointer to codec-specific info. */
static s32 dmux_get_au(u32 h, CellDmuxAuInfo** out, u32* specific, int acquire)
{
 if(h>=CELL_DMUX_MAX_ES||!s_es[h].in_use||!out||!specific)return (s32)CELL_DMUX_ERROR_ARG;
 if(dmux_trace() && s_es[h].hasAu && !s_es[h].acquired)fprintf(stderr,"[cellDmux] GET_AU h=%u acquire=%d has=%d acquired=%d\n",h,acquire,s_es[h].hasAu,s_es[h].acquired);
 DmuxEsSlot* es=&s_es[h];if(!es->hasAu||es->acquired)return (s32)CELL_DMUX_ERROR_EMPTY;
 u32 ea=es->resource;CellDmuxAuInfo* au=&es->currentAu;
 memset(vm_ptr8(ea),0,0x100);
 vm_write32(ea,au->auAddr);vm_write32(ea+4,au->auSize);vm_write32(ea+8,es->resourceSize-0x100);
 vm_write8(ea+12,0);vm_write64(ea+16,au->userData);vm_write64(ea+24,au->pts);vm_write64(ea+32,au->dts);
 vm_write32(GUEST_EA(out),ea);vm_write32(GUEST_EA(specific),ea+0x80);
 if(acquire)es->acquired=1;return CELL_OK;
}
s32 cellDmuxGetAu(CellDmuxEsHandle h, CellDmuxAuInfo** out, u32* specific)
{ return dmux_get_au(h,out,specific,1); }
s32 cellDmuxPeekAu(CellDmuxEsHandle h, CellDmuxAuInfo** out, u32* specific)
{ return dmux_get_au(h,out,specific,0); }
s32 cellDmuxGetAuEx(CellDmuxEsHandle h, CellDmuxAuInfo** out, u32* specific)
{ return dmux_get_au(h,out,specific,1); }
s32 cellDmuxPeekAuEx(CellDmuxEsHandle h, CellDmuxAuInfo** out, u32* specific)
{ return dmux_get_au(h,out,specific,0); }

s32 cellDmuxReleaseAu(CellDmuxEsHandle esHandle)
{
    if (esHandle >= CELL_DMUX_MAX_ES || !s_es[esHandle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;

    if(dmux_trace())fprintf(stderr,"[cellDmux] RELEASE es=%u\n",esHandle);
    u32 dmux = s_es[esHandle].dmuxId;
    s_es[esHandle].hasAu = 0;
    if (dmux < CELL_DMUX_MAX_HANDLES && s_dmux[dmux].in_use)
        SetEvent(s_dmux[dmux].event);
    return CELL_OK;
}

s32 cellDmuxFlushEs(CellDmuxEsHandle esHandle)
{
    printf("[cellDmux] FlushEs(es=%u)\n", esHandle);

    if (esHandle >= CELL_DMUX_MAX_ES || !s_es[esHandle].in_use)
        return (s32)CELL_DMUX_ERROR_ARG;

    s_es[esHandle].hasAu = 0;

    if (s_es[esHandle].esCbFunc)
        dmux_es_notify(esHandle, CELL_DMUX_ES_MSG_TYPE_FLUSH_DONE);

    return CELL_OK;
}
