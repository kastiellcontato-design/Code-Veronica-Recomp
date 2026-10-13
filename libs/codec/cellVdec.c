/*
 * ps3recomp - cellVdec HLE implementation
 *
 * FFmpeg-backed video decoding. Only actual decoded pictures generate PICOUT.
 */

#include "cellVdec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cv_ffmpeg.h"
#include "../../runtime/platform/win32_compat.h"
#include "../guest_struct.h"   /* GUEST_EA, vm_read/vm_write: guest EA -> host */
#include "ps3emu/guest_call.h" /* g_ps3_guest_caller -- cbFunc is a GUEST OPD */
#include "../../runtime/memory/vm.h"     /* VM_HLE_INJECT_BASE */
#include "../../runtime/ps3_log.h"       /* ps3_log_verbose */

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/
#define MAX_VDEC 4

typedef struct { CellVdecAuInfo info; u8* data; } VdecCommand;

typedef struct {
    int in_use;
    u32 codecType;
    u32 cbFunc;         /* guest EA of the callback's OPD */
    u32 cbArg;          /* guest EA handed back to it     */
    int seqStarted;
    CellVdecPicItem lastPic;
    int hasPic;
    CvVideoDecoder* decoder;
    u32 itemBase, itemSlots, itemIndex, frameRate, pictureType;
    SRWLOCK commandLock;
    HANDLE commandEvent, worker;
    volatile LONG stopping;
    VdecCommand commands[4];
    unsigned commandRead, commandWrite;
    u32 auCount;        /* total AUs decoded */
    u16 width;          /* configured resolution (0 = use default) */
    u16 height;
} VdecSlot;

static VdecSlot s_vdec[MAX_VDEC];

/* Read once: getenv walks the environment, and these sit on per-picture paths. */
static int vdec_trace(void)
{
    static int t = -1;
    if (t < 0) t = getenv("CV_AV_TRACE") ? 1 : 0;
    return t;
}

/* Deliver a vdec message to the guest callback (handle, msgType, msgData,
 * cbArg). It is a guest OPD, so it goes through g_ps3_guest_caller; this used
 * to call it as a host function pointer, which crashes the first time a
 * title's callback is reached.
 * ponytail: synchronous on the caller's thread, like adec_notify; give it a
 * decoder thread if a title's callback ever blocks on the DecodeAu caller. */
static void vdec_notify(CellVdecHandle handle, u32 msg_type, s32 msg_data)
{
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use) return;
    const VdecSlot* v = &s_vdec[handle];
    if (!v->cbFunc || !g_ps3_guest_caller) return;
    if (vdec_trace())
        fprintf(stderr,"[cellVdec] CALLBACK_BEGIN type=%u cb=%08X flags=%08X pictures=%u\n",msg_type,v->cbFunc,v->cbArg?vm_read32(v->cbArg+0x204):0,v->cbArg?vm_read32(v->cbArg+0x1FC):0);
    g_ps3_guest_caller(v->cbFunc, (u64)handle, (u64)msg_type,
                       (u64)(s64)msg_data, (u64)v->cbArg, 0, 0, 0, 0);
    if (vdec_trace())
        fprintf(stderr,"[cellVdec] CALLBACK_END type=%u\n",msg_type);
}

/* Decode and notify on a decoder thread, as the asynchronous SDK requires.
 * A guest commonly holds its player mutex while submitting DecodeAu. Calling
 * its AUDONE callback from that same thread deadlocks trying to take the mutex. */
static void vdec_picture_ready(void* argument)
{
    u32 h=(u32)(uintptr_t)argument;
    if(h<MAX_VDEC&&!InterlockedCompareExchange(&s_vdec[h].stopping,0,0))
        vdec_notify(h,CELL_VDEC_MSG_TYPE_PICOUT,CELL_OK);
}
static DWORD WINAPI vdec_worker(LPVOID argument)
{
    u32 h=(u32)(uintptr_t)argument;VdecSlot* v=&s_vdec[h];
    for(;;){
        WaitForSingleObject(v->commandEvent,INFINITE);
        for(;;){
            VdecCommand command = {0};
            AcquireSRWLockExclusive(&v->commandLock);
            if(v->commandRead!=v->commandWrite)command=v->commands[v->commandRead++%4];
            ReleaseSRWLockExclusive(&v->commandLock);
            if(!command.data)break;
            CellVdecAuInfo au = command.info;
            int pictures=cv_video_decode(v->decoder,command.data,au.size,au.pts,au.userData,vdec_picture_ready,(void*)(uintptr_t)h);
            free(command.data);
            if (InterlockedCompareExchange(&v->stopping,0,0)) break;
            vdec_notify(h,CELL_VDEC_MSG_TYPE_AUDONE,CELL_OK);
            if(pictures<0){vdec_notify(h,CELL_VDEC_MSG_TYPE_ERROR,(s32)CELL_VDEC_ERROR_AU);continue;}
            v->auCount++;

        }
        if(v->stopping)break;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

/* cellVdecQueryAttr: how much work memory the decoder wants, before Open.
 * CellVdecAttr (guest, big-endian): +0 u32 memSize, +4 u8 cmdDepth,
 * +8 u32 decoderVerUpper, +0xC u32 decoderVerLower.
 * Decoding is host-side, so the guest's buffer is never touched; memSize only
 * has to be something the title can allocate. cmdDepth sizes the title's own
 * AU queue, so it must not be 0. Unimplemented, the call left the struct
 * unfilled and a movie player (Resistance: Fall of Man) gave up and retried
 * forever. */
s32 cellVdecQueryAttr(const CellVdecType* type, void* attr)
{
    u32 ea = (u32)(uintptr_t)attr;
    if (!type || !ea)
        return (s32)CELL_VDEC_ERROR_ARG;
    vm_write32(ea + 0x0, 0x100000);   /* memSize: 1 MB */
    vm_write8 (ea + 0x4, 4);          /* cmdDepth */
    vm_write32(ea + 0x8, 0x00010000); /* decoderVerUpper */
    vm_write32(ea + 0xC, 0);          /* decoderVerLower */
    return CELL_OK;
}

/* cellVdecOpen(type, res, cb, handle) -- FOUR arguments, like cellAdecOpen.
 * cb is a guest pointer to { u32 cbFunc; u32 cbArg; }. Declared with five,
 * `handle` came from r7 instead of r6: the handle was written somewhere
 * random and the title's own handle stayed 0 (Resistance: Fall of Man opened
 * handle 1, then called StartSeq(0)); four Opens later every call was BUSY. */
s32 cellVdecOpen(const CellVdecType* type, const CellVdecResource* res,
                  const CellVdecCb* cb, CellVdecHandle* handle)
{
    (void)res;
    u32 cb_ea = (u32)(uintptr_t)cb;

    u32 codec_type = type ? vm_read32(GUEST_EA(type)) : 0;   /* codecType */
    printf("[cellVdec] Open(codecType=%u)\n", codec_type);

    if (!type || !handle)
        return (s32)CELL_VDEC_ERROR_ARG;

    for (int i = 0; i < MAX_VDEC; i++) {
        if (!s_vdec[i].in_use) {
            memset(&s_vdec[i], 0, sizeof(VdecSlot));
            s_vdec[i].decoder = cv_video_open(codec_type);
            if (!s_vdec[i].decoder) return (s32)CELL_VDEC_ERROR_FATAL;
            /* Use the guest-owned decoder workspace for stable picture items. */
            u32 res_ea=GUEST_EA(res);
            u32 work=res_ea?vm_read32(res_ea):0;
            u32 size=res_ea?vm_read32(res_ea+4):0;
            s_vdec[i].itemBase=work&&size>=0x10000?work:VM_HLE_INJECT_BASE+0x50000u+(u32)i*0x4000u;
            s_vdec[i].itemSlots=work&&size>=0x10000?64:16;
            s_vdec[i].in_use = 1;
            s_vdec[i].codecType = codec_type;
            s_vdec[i].cbFunc = cb_ea ? vm_read32(cb_ea + 0) : 0;
            s_vdec[i].cbArg  = cb_ea ? vm_read32(cb_ea + 4) : 0;
            InitializeSRWLock(&s_vdec[i].commandLock);
            s_vdec[i].commandEvent=CreateEventA(NULL,FALSE,FALSE,NULL);
            s_vdec[i].worker=CreateThread(NULL,0,vdec_worker,(LPVOID)(uintptr_t)i,0,NULL);
            if(!s_vdec[i].commandEvent||!s_vdec[i].worker){s_vdec[i].in_use=0;return (s32)CELL_VDEC_ERROR_FATAL;}
            vm_write32((u32)(uintptr_t)handle, (u32)i);
            printf("[cellVdec] Open -> handle=%u\n", i);
            return CELL_OK;
        }
    }
    return (s32)CELL_VDEC_ERROR_BUSY;
}

s32 cellVdecClose(CellVdecHandle handle)
{
    printf("[cellVdec] Close(handle=%u)\n", handle);

    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;

    VdecSlot* v=&s_vdec[handle];
    InterlockedExchange(&v->stopping,1);
    AcquireSRWLockExclusive(&v->commandLock);
    while(v->commandRead!=v->commandWrite){
        VdecCommand* command=&v->commands[v->commandRead++%4];
        free(command->data);command->data=NULL;
    }
    ReleaseSRWLockExclusive(&v->commandLock);
    cv_video_stop(v->decoder);SetEvent(v->commandEvent);
    WaitForSingleObject(s_vdec[handle].worker,INFINITE);
    CloseHandle(s_vdec[handle].worker);CloseHandle(s_vdec[handle].commandEvent);
    cv_video_close(s_vdec[handle].decoder);
    s_vdec[handle].in_use = 0;
    return CELL_OK;
}

s32 cellVdecStartSeq(CellVdecHandle handle)
{
    printf("[cellVdec] StartSeq(handle=%u)\n", handle);

    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;

    cv_video_reset_pacing(s_vdec[handle].decoder);
    s_vdec[handle].seqStarted = 1;
    return CELL_OK;
}

s32 cellVdecEndSeq(CellVdecHandle handle)
{
    printf("[cellVdec] EndSeq(handle=%u)\n", handle);

    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;

    s_vdec[handle].seqStarted = 0;

    /* Notify sequence done */
    vdec_notify(handle, CELL_VDEC_MSG_TYPE_SEQDONE, CELL_OK);

    return CELL_OK;
}

s32 cellVdecDecodeAu(CellVdecHandle handle, s32 mode, const CellVdecAuInfo* auInfo)
{
    (void)mode;

    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    if (!auInfo)
        return (s32)CELL_VDEC_ERROR_ARG;

    VdecSlot* v = &s_vdec[handle];

    /* CellVdecAuInfo mixes u32 and u64, so it comes across field by field: a
     * big-endian u64 is high word first, and a word-at-a-time copy would put
     * the two halves the wrong way round on a little-endian host. */
    u32 au_ea = GUEST_EA(auInfo);
    CellVdecAuInfo au;
    au.startAddr = vm_read32(au_ea + (u32)offsetof(CellVdecAuInfo, startAddr));
    au.size      = vm_read32(au_ea + (u32)offsetof(CellVdecAuInfo, size));
    au.pts       = vm_read64(au_ea + (u32)offsetof(CellVdecAuInfo, pts));
    au.dts       = vm_read64(au_ea + (u32)offsetof(CellVdecAuInfo, dts));
    au.userData  = vm_read64(au_ea + (u32)offsetof(CellVdecAuInfo, userData));

    if (ps3_log_verbose())
        printf("[cellVdec] DecodeAu(handle=%u, addr=0x%X, size=%u, pts=%llu)\n",
           handle, au.startAddr, au.size,
           (unsigned long long)au.pts);

    if (!au.startAddr || !au.size || au.size > 0x1000000)
        return (s32)CELL_VDEC_ERROR_ARG;
    u8* owned = (u8*)malloc(au.size);
    if (!owned) return (s32)CELL_VDEC_ERROR_FATAL;
    memcpy(owned, vm_ptr8(au.startAddr), au.size);
    AcquireSRWLockExclusive(&v->commandLock);
    if(v->commandWrite-v->commandRead>=4){ReleaseSRWLockExclusive(&v->commandLock);free(owned);return (s32)CELL_VDEC_ERROR_BUSY;}
    VdecCommand* command = &v->commands[v->commandWrite++%4];
    command->info = au;
    command->data = owned;
    ReleaseSRWLockExclusive(&v->commandLock);
    SetEvent(v->commandEvent);

    return CELL_OK;
}

/* Guest-resident pic items. cellVdecGetPicItem hands the title a POINTER to a
 * CellVdecPicItem, so it has to live in guest memory, big-endian; writing
 * &host_struct through the guest's out-pointer crashed Resistance: Fall of Man
 * inside its PICOUT callback. One 0x100-byte block per decoder after cellAdec's
 * scratch (VM_HLE_INJECT_BASE + 0x40000..0x50000): the item at +0, the codec
 * info it points to at +0x80.
 *
 * Guest CellVdecPicItem: +0x00 codecType, +0x04 startAddr, +0x08 size,
 * +0x0C u8 auNum, +0x10 auPts[2] {upper,lower}, +0x20 auDts[2],
 * +0x30 u64 auUserData[2], +0x40 status, +0x44 attr (0 normal, 1 skipped),
 * +0x48 picInfo. CellVdecAvcInfo and CellVdecMpeg2Info both begin with
 * u16 horizontalSize, u16 verticalSize; the rest of the info block is zero. */
#define VDEC_ITEM_EA(h)  (s_vdec[h].itemBase + (s_vdec[h].itemIndex % s_vdec[h].itemSlots) * 0x400u)
#define VDEC_INFO_EA(h)  (VDEC_ITEM_EA(h) + 0x80u)

static void vdec_write_item(CellVdecHandle handle)
{
    const VdecSlot* v = &s_vdec[handle];
    const CellVdecPicItem* p = &v->lastPic;
    u32 it = VDEC_ITEM_EA(handle), info = VDEC_INFO_EA(handle);
    for (u32 o = 0; o < 0x400; o += 4) vm_write32(it + o, 0);
    vm_write32(it + 0x00, v->codecType);
    vm_write32(it + 0x04, p->startAddr);
    vm_write32(it + 0x08, p->size);
    vm_write8 (it + 0x0C, 1);                          /* one AU per picture */
    vm_write32(it + 0x10, (u32)(p->pts >> 32));        /* auPts[0] */
    vm_write32(it + 0x14, (u32)p->pts);
    vm_write32(it + 0x18, 0xFFFFFFFFu);                /* auPts[1]: unused */
    vm_write32(it + 0x1C, 0xFFFFFFFFu);
    vm_write32(it + 0x20, (u32)(p->dts >> 32));        /* auDts[0] */
    vm_write32(it + 0x24, (u32)p->dts);
    vm_write32(it + 0x28, 0xFFFFFFFFu);
    vm_write32(it + 0x2C, 0xFFFFFFFFu);
    vm_write64(it + 0x30, p->userData);                /* auUserData[0] */
    vm_write32(it + 0x40, 0);                          /* status: OK */
    vm_write32(it + 0x44, 0);                          /* attr: normal */
    vm_write32(it + 0x48, info);
    vm_write16(info + 0, p->width);
    vm_write16(info + 2, p->height);
    if(v->codecType==CELL_VDEC_CODEC_TYPE_MPEG2){
        vm_write8(info+4,1); /* square pixels */
        vm_write8(info+5,(u8)v->frameRate);
        vm_write8(info+6,1);vm_write8(info+7,1);
        vm_write8(info+8,5); /* unspecified video format */
        vm_write8(info+10,1);vm_write8(info+11,1);vm_write8(info+12,1);
        vm_write8(info+18,(u8)v->pictureType);
        vm_write8(info+20,3);vm_write8(info+21,3); /* full frames */
        vm_write8(info+24,1);
    }
}

/* GetPicItem advances the metadata cursor; GetPicture consumes pixels. */
s32 cellVdecGetPicItem(CellVdecHandle handle, const CellVdecPicItem** picItem)
{
    u32 out = (u32)(uintptr_t)picItem;
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use || !out)
        return (s32)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[handle];
    if (vdec_trace()) fprintf(stderr,"[cellVdec] GetPicItem handle=%u\n",handle);
    if (!cv_video_peek(v->decoder,&v->lastPic.width,&v->lastPic.height,&v->lastPic.pts,&v->lastPic.userData,&v->frameRate,&v->pictureType))
        return (s32)CELL_VDEC_ERROR_EMPTY;
    v->lastPic.dts=v->lastPic.pts;
    v->lastPic.size=v->lastPic.width*v->lastPic.height*3/2;
    vdec_write_item(handle);
    vm_write32(out, VDEC_ITEM_EA(handle));
    v->itemIndex++;
    return CELL_OK;
}

/* cellVdecGetPicture(handle, format, outBuff): copy the next picture out in
 * the requested format and consume it. format is a guest CellVdecPicFormat
 * { u32 formatType; u32 colorMatrixType; u8 alpha; }; formatType 0 ARGB32,
 * 1 RGBA32, 2 UYVY422, 3 YUV420 planar.
 * ponytail: there is no H.264/MPEG-2 decoder behind this, so the picture is
 * black at the item's size (1280x720 unless set). Real frames need a decoder;
 * titles that only pace playback on PICOUT/GetPicture run as they should. */
s32 cellVdecGetPicture(CellVdecHandle handle, const void* format, void* outBuff)
{
    u32 fmt_ea = (u32)(uintptr_t)format, dst = (u32)(uintptr_t)outBuff;
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use || !fmt_ea)
        return (s32)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[handle];
    if (vdec_trace()) fprintf(stderr,"[cellVdec] GetPicture handle=%u format=%u dst=%08X\n",handle,vm_read32(fmt_ea),dst);
    int ret = cv_video_picture(v->decoder, vm_read32(fmt_ea), vm_read8(fmt_ea+8), dst ? vm_ptr8(dst):NULL);
    if (!ret) return (s32)CELL_VDEC_ERROR_EMPTY;
    if (ret < 0) return (s32)CELL_VDEC_ERROR_ARG;
    return CELL_OK;
}

s32 cellVdecSetFrameRate(CellVdecHandle handle, u32 frameRateCode)
{
    printf("[cellVdec] SetFrameRate(handle=%u, code=%u)\n",
           handle, frameRateCode);

    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;

    return CELL_OK;
}

s32 cellVdecQueryAttrEx(const CellVdecType* type, void* attr)
{ return cellVdecQueryAttr(type,attr); }
s32 cellVdecOpenEx(const CellVdecType* type, const CellVdecResource* res, const CellVdecCb* cb, CellVdecHandle* handle)
{ return cellVdecOpen(type,res,cb,handle); }
