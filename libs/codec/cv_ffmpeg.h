#ifndef CV_FFMPEG_H
#define CV_FFMPEG_H
#include "ps3emu/ps3types.h"
typedef struct CvVideoDecoder CvVideoDecoder;
CvVideoDecoder* cv_video_open(u32 codec);
void cv_video_close(CvVideoDecoder* d);
void cv_video_stop(CvVideoDecoder* d);
void cv_video_reset_pacing(CvVideoDecoder* d);
int cv_video_decode(CvVideoDecoder* d, const u8* data, u32 size, u64 pts, u64 user, void (*notify)(void*), void* opaque);
int cv_video_peek(CvVideoDecoder* d,u16* w,u16* h,u64* pts,u64* user,u32* frameRate,u32* pictureType);
int cv_video_picture(CvVideoDecoder* d,u32 format,u8 alpha,u8* dst);
typedef struct CvAudioDecoder CvAudioDecoder;
typedef struct {u32 samples,channels,rate,blockBytes;u64 pts,user;} CvPcmInfo;
CvAudioDecoder* cv_audio_open(void);
void cv_audio_close(CvAudioDecoder* d);
int cv_audio_decode(CvAudioDecoder* d,const u8* data,u32 size,u64 pts,u64 user,void (*notify)(void*),void* arg);
int cv_audio_peek(CvAudioDecoder* d,CvPcmInfo* info);
int cv_audio_pcm(CvAudioDecoder* d,u32 bw,u8* dst,int consume);
#endif
