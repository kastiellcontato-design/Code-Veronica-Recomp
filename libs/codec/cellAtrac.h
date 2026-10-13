/*
 * ps3recomp - cellAtrac HLE
 *
 * libatrac3plus: decodes an ATRAC3plus RIFF (.at3) file to float PCM. On the
 * console the decode runs on an SPU; here it runs FFmpeg's decoder
 * (third_party/at3_standalone) on the calling thread.
 *
 * Pointer parameters are guest EAs.
 */

#ifndef PS3RECOMP_CELL_ATRAC_H
#define PS3RECOMP_CELL_ATRAC_H

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CELL_ATRAC_ERROR_API_FAIL              0x80610301
#define CELL_ATRAC_ERROR_READSIZE_OVER_BUFFER  0x80610311
#define CELL_ATRAC_ERROR_UNKNOWN_FORMAT        0x80610312
#define CELL_ATRAC_ERROR_READSIZE_IS_TOO_SMALL 0x80610313
#define CELL_ATRAC_ERROR_ILLEGAL_DATA          0x80610315
#define CELL_ATRAC_ERROR_NO_DECODER            0x80610321
#define CELL_ATRAC_ERROR_UNSET_DATA            0x80610322
#define CELL_ATRAC_ERROR_DECODER_WAS_CREATED   0x80610323
#define CELL_ATRAC_ERROR_ALLDATA_WAS_DECODED   0x80610331
#define CELL_ATRAC_ERROR_ALLDATA_IS_ONMEMORY   0x80610341
#define CELL_ATRAC_ERROR_UNSET_LOOP_NUM        0x80610361
#define CELL_ATRAC_ERROR_ILLEGAL_SAMPLE        0x80610371

#define CELL_ATRAC_ALLDATA_IS_ON_MEMORY             (-1)
#define CELL_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY (-2)
#define CELL_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY    (-3)

s32 cellAtracSetDataAndGetMemSize(u32 pHandle, u32 pucBufferAddr, u32 uiReadByte,
                                  u32 uiBufferByte, u32 puiWorkMemByte);
s32 cellAtracCreateDecoder(u32 pHandle, u32 pucWorkMem, u32 uiPpuThreadPriority,
                           u32 uiSpuThreadPriority);
s32 cellAtracCreateDecoderExt(u32 pHandle, u32 pucWorkMem, u32 uiPpuThreadPriority,
                              u32 pExtRes);
s32 cellAtracDeleteDecoder(u32 pHandle);
s32 cellAtracDecode(u32 pHandle, u32 pfOutAddr, u32 puiSamples, u32 puiFinishflag,
                    u32 piRemainFrame);
s32 cellAtracGetRemainFrame(u32 pHandle, u32 piRemainFrame);
s32 cellAtracGetChannel(u32 pHandle, u32 puiChannel);
s32 cellAtracGetMaxSample(u32 pHandle, u32 puiMaxSample);
s32 cellAtracGetNextSample(u32 pHandle, u32 puiNextSample);
s32 cellAtracGetSoundInfo(u32 pHandle, u32 piEndSample, u32 piLoopStartSample,
                          u32 piLoopEndSample);
s32 cellAtracGetNextDecodePosition(u32 pHandle, u32 puiSamplePosition);
s32 cellAtracGetBitrate(u32 pHandle, u32 puiBitrate);
s32 cellAtracGetLoopInfo(u32 pHandle, u32 piLoopNum, u32 puiLoopStatus);
s32 cellAtracSetLoopNum(u32 pHandle, s32 iLoopNum);
s32 cellAtracResetPlayPosition(u32 pHandle, u32 uiSample, u32 uiWriteByte);
s32 cellAtracGetStreamDataInfo(u32 pHandle, u32 ppucWritePointer, u32 puiWritableByte,
                               u32 puiReadPosition);
s32 cellAtracAddStreamData(u32 pHandle, u32 uiAddByte);
s32 cellAtracGetVacantSize(u32 pHandle, u32 puiVacantSize);
s32 cellAtracIsSecondBufferNeeded(u32 pHandle);
s32 cellAtracGetSecondBufferInfo(u32 pHandle, u32 puiReadPosition, u32 puiDataByte);
s32 cellAtracSetSecondBuffer(u32 pHandle, u32 pucSecondBufferAddr, u32 uiSecondBufferByte);
s32 cellAtracGetBufferInfoForResetting(u32 pHandle, u32 uiSample, u32 pBufferInfo);
s32 cellAtracGetInternalErrorInfo(u32 pHandle, u32 piResult);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_CELL_ATRAC_H */
