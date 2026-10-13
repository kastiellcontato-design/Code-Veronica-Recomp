/*
 * ps3recomp - cellAdec HLE
 *
 * Audio decoder: AAC, ATRAC3plus, MP3, CELP audio decoding.
 * Receives AU from cellDmux and outputs decoded PCM audio.
 */

#ifndef PS3RECOMP_CELL_ADEC_H
#define PS3RECOMP_CELL_ADEC_H

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Error codes
 * -----------------------------------------------------------------------*/
/* These were all wrong -- every one of them. The real values are
 * FATAL/SEQ/ARG/BUSY/EMPTY = 0x80610001..5 (RPCS3 Modules/cellAdec.h:9), and
 * the guest proves it: ps1_netemu's audio thread drains the decoder with
 *
 *     000EE220  bl 0x15f14c        ; cellAdecEndSeq
 *     000EE230  bl 0x15f0ec        ; cellAdecGetPcm(handle, 0)
 *     000EE238  cmpw cr7, r3, r31  ; r31 = 0x80610005
 *
 * and 0x80610005 is the ONLY cellAdec code it ever compares against (8 sites).
 * Returning 0x80610204 for EMPTY meant that loop never saw its terminator, so
 * the title re-ran the whole format-change path: 4760 EndSeq calls against a
 * single StartSeq.
 *
 * There is no AU or PCM code in the real list; both are mapped to the nearest
 * real one rather than left as invented values that no title can recognise. */
#define CELL_ADEC_ERROR_FATAL          0x80610001
#define CELL_ADEC_ERROR_SEQ            0x80610002
#define CELL_ADEC_ERROR_ARG            0x80610003
#define CELL_ADEC_ERROR_BUSY           0x80610004
#define CELL_ADEC_ERROR_EMPTY          0x80610005
#define CELL_ADEC_ERROR_AU             CELL_ADEC_ERROR_ARG
#define CELL_ADEC_ERROR_PCM            CELL_ADEC_ERROR_EMPTY

/* ---------------------------------------------------------------------------
 * Codec types
 * -----------------------------------------------------------------------*/
#define CELL_ADEC_TYPE_LPCM            1
#define CELL_ADEC_TYPE_AC3             2
#define CELL_ADEC_TYPE_ATRACX          3
#define CELL_ADEC_TYPE_MP3             4
#define CELL_ADEC_TYPE_ATRAC3           5
#define CELL_ADEC_TYPE_AAC             7
#define CELL_ADEC_TYPE_CELP8           11

/* Callback message types */
#define CELL_ADEC_MSG_TYPE_AUDONE      0
#define CELL_ADEC_MSG_TYPE_PCMOUT      1
/* ERROR is 2 and SEQDONE is 3, not the other way round. These were swapped,
 * so cellAdecEndSeq's SEQDONE reached the guest as ERROR -- ps1_netemu's
 * callback (func_000ED918, OPD 0x1B5F00) then restarted the stream, giving
 * 4977 EndSeq calls against a single StartSeq. Order confirmed twice: RPCS3
 * Modules/cellAdec.h:304, and the guest callback itself, which acts only on
 * msgType == 1 (PCMOUT) and ignores everything else. */
#define CELL_ADEC_MSG_TYPE_ERROR       2
#define CELL_ADEC_MSG_TYPE_SEQDONE     3

/* ---------------------------------------------------------------------------
 * Types
 * -----------------------------------------------------------------------*/
typedef u32 CellAdecHandle;

typedef struct CellAdecType {
    u32 audioCodecType;
} CellAdecType;

typedef struct CellAdecResource {
    u32 memAddr;
    u32 memSize;
    u32 ppuThreadPrio;
    u32 ppuThreadStackSize;
    u32 spuThreadPrio;
    u32 numOfSpus;
} CellAdecResource;

typedef struct CellAdecAuInfo {
    u32 startAddr;
    u32 size;
    u64 pts;
    u64 userData;
} CellAdecAuInfo;

typedef struct CellAdecPcmItem {
    u32 pcmAddr;
    u32 pcmSize;
    u32 status;
    u64 pts;
    u32 channelNumber;
    u32 samplingRate;
    u32 bitsPerSample;
    u64 userData;
} CellAdecPcmItem;

/* NOT a host function pointer. The guest's cbFunc is a guest EA naming an OPD,
 * so it must be dispatched through g_ps3_guest_caller -- calling it as a host
 * pointer jumps to the guest address as if it were host code, which is exactly
 * how this crashed (rip = 0x1B5F00, the guest OPD). Kept as a comment rather
 * than a typedef so nobody can accidentally call one again.
 *
 *   cbFunc(handle, msgType, msgData, cbArg)  -- RPCS3 Modules/cellAdec.h:312
 */

/* ---------------------------------------------------------------------------
 * Functions
 * -----------------------------------------------------------------------*/

/* CellAdecCb is a GUEST STRUCT passed by pointer: { cbFunc, cbArg }, not two
 * separate register arguments. Splitting it into two shifted `handle` from r6 to
 * r7, so the handle out-pointer read as garbage and every Open failed ARG. */
typedef struct CellAdecCb {
    u32 cbFunc;   /* guest EA of a CellAdecCbMsg */
    u32 cbArg;    /* guest EA passed back to it  */
} CellAdecCb;

s32 cellAdecQueryAttr(const CellAdecType* type, void* attr);
s32 cellAdecOpen(const CellAdecType* type, const CellAdecResource* res,
                 const CellAdecCb* cb, CellAdecHandle* handle);
s32 cellAdecOpenEx(const CellAdecType* type, const CellAdecResource* res, const CellAdecCb* cb, CellAdecHandle* handle);
s32 cellAdecClose(CellAdecHandle handle);

s32 cellAdecStartSeq(CellAdecHandle handle, void* param);
s32 cellAdecEndSeq(CellAdecHandle handle);

s32 cellAdecDecodeAu(CellAdecHandle handle, const CellAdecAuInfo* auInfo);

s32 cellAdecGetPcm(CellAdecHandle handle, void* outBuffer);
s32 cellAdecGetPcmItem(CellAdecHandle handle, const CellAdecPcmItem** pcmItem);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_CELL_ADEC_H */
