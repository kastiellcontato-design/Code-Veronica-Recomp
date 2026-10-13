/* Shared-library API from FFmpeg 7.1. No guest addresses cross this interface. */
#include "cv_ffmpeg.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#define API(ret,name,args) static ret (*f_##name) args
API(unsigned,avcodec_version,(void));
API(const AVCodec*,avcodec_find_decoder,(enum AVCodecID));
API(AVCodecContext*,avcodec_alloc_context3,(const AVCodec*));
API(int,avcodec_open2,(AVCodecContext*, const AVCodec*, AVDictionary**));
API(void,avcodec_free_context,(AVCodecContext**));
API(AVCodecParserContext*,av_parser_init,(int));
API(void,av_parser_close,(AVCodecParserContext*));
API(int,av_parser_parse2,(AVCodecParserContext*,AVCodecContext*,uint8_t**,int*,const uint8_t*,int,int64_t,int64_t,int64_t));
API(AVPacket*,av_packet_alloc,(void));
API(void,av_packet_free,(AVPacket**));
API(void,av_packet_unref,(AVPacket*));
API(int,av_new_packet,(AVPacket*,int));
API(int,avcodec_send_packet,(AVCodecContext*,const AVPacket*));
API(int,avcodec_receive_frame,(AVCodecContext*,AVFrame*));
API(AVFrame*,av_frame_alloc,(void));
API(void,av_frame_free,(AVFrame**));
API(void,av_frame_unref,(AVFrame*));
API(struct SwsContext*,sws_getContext,(int,int,enum AVPixelFormat,int,int,enum AVPixelFormat,int,SwsFilter*,SwsFilter*,const double*));
API(void,sws_freeContext,(struct SwsContext*));
API(int,sws_scale,(struct SwsContext*,const uint8_t*const[],const int[],int,int,uint8_t*const[],const int[]));
static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static int loaded;
static BOOL CALLBACK load_ffmpeg(PINIT_ONCE init,PVOID param,PVOID* out)
{
 (void)init;(void)param;(void)out;
 /* DLLs must match the headers; reject a different major version. */
 HMODULE a=LoadLibraryW(L"avutil-59.dll"),c=LoadLibraryW(L"avcodec-61.dll"),s=LoadLibraryW(L"swscale-8.dll");
 if(!a||!c||!s){fprintf(stderr,"[cellVdec] FFmpeg DLL load failed: %lu\n",GetLastError());return TRUE;}
#define LOAD(lib,name) f_##name=(void*)GetProcAddress(lib,#name); if(!f_##name)return TRUE
 LOAD(c,avcodec_version);LOAD(c,avcodec_find_decoder);LOAD(c,avcodec_alloc_context3);LOAD(c,avcodec_open2);LOAD(c,avcodec_free_context);
 LOAD(c,av_parser_init);LOAD(c,av_parser_close);LOAD(c,av_parser_parse2);LOAD(c,av_packet_alloc);LOAD(c,av_packet_free);LOAD(c,av_packet_unref);LOAD(c,av_new_packet);LOAD(c,avcodec_send_packet);LOAD(c,avcodec_receive_frame);
 LOAD(a,av_frame_alloc);LOAD(a,av_frame_free);LOAD(a,av_frame_unref);
 LOAD(s,sws_getContext);LOAD(s,sws_freeContext);LOAD(s,sws_scale);
#undef LOAD
 loaded=(f_avcodec_version()>>16)==61;return TRUE;
}
#define Q 64
/* Read once: getenv walks the environment, and these sit on per-picture paths. */
static int env_flag(const char* name,int* cache){if(*cache<0)*cache=getenv(name)?1:0;return *cache;}
static int s_av_trace=-1,s_pace_trace=-1,s_capture=-1;
struct CvVideoDecoder {AVCodecContext* ctx;AVCodecParserContext* parser;AVFrame* frames[Q];u64 pts[Q],user[Q];u8 itemSeen[Q];unsigned read,write;u64 nextPts;int continuous,timelineStarted;volatile LONG stopping;SRWLOCK lock;CONDITION_VARIABLE space;struct SwsContext* scaler;int scaler_w,scaler_h,scaler_fmt;u32 scaler_out;u64 shown,firstPts;ULONGLONG started,lastLog;};
CvVideoDecoder* cv_video_open(u32 codec)
{
 InitOnceExecuteOnce(&once,load_ffmpeg,NULL,NULL);if(!loaded)return NULL;
 enum AVCodecID id=codec==0?AV_CODEC_ID_MPEG2VIDEO:codec==1?AV_CODEC_ID_H264:AV_CODEC_ID_NONE;
 const AVCodec* c=f_avcodec_find_decoder(id);if(!c)return NULL;
 CvVideoDecoder* d=calloc(1,sizeof(*d));if(!d)return NULL;
 d->continuous=getenv("CV_AV_CONTINUOUS_PTS")&&codec==0;
 InitializeSRWLock(&d->lock);InitializeConditionVariable(&d->space);d->ctx=f_avcodec_alloc_context3(c);d->parser=f_av_parser_init(id);
 /* Slice threads split one picture across cores without adding output delay. */
 if(d->ctx){d->ctx->thread_count=4;d->ctx->thread_type=FF_THREAD_SLICE;}
 if(!d->ctx||!d->parser||f_avcodec_open2(d->ctx,c,NULL)<0){cv_video_close(d);return NULL;}
 fprintf(stderr,"[cellVdec] FFmpeg decoder opened codec=%u\n",codec);return d;
}
void cv_video_close(CvVideoDecoder* d)
{if(!d)return;for(unsigned i=0;i<Q;i++)if(d->frames[i])f_av_frame_free(&d->frames[i]);if(d->scaler)f_sws_freeContext(d->scaler);if(d->parser)f_av_parser_close(d->parser);if(d->ctx)f_avcodec_free_context(&d->ctx);free(d);}
void cv_video_stop(CvVideoDecoder* d)
{if(!d)return;InterlockedExchange(&d->stopping,1);AcquireSRWLockExclusive(&d->lock);WakeAllConditionVariable(&d->space);ReleaseSRWLockExclusive(&d->lock);}
void cv_video_reset_pacing(CvVideoDecoder* d)
{if(!d)return;AcquireSRWLockExclusive(&d->lock);d->shown=0;d->started=0;d->timelineStarted=0;d->nextPts=0;d->lastLog=0;ReleaseSRWLockExclusive(&d->lock);}
static int receive(CvVideoDecoder* d,u64 pts,u64 user,void (*notify)(void*),void* opaque)
{
 int count=0;
 for(;;){AVFrame* f=f_av_frame_alloc();if(!f)return -1;int ret=f_avcodec_receive_frame(d->ctx,f);
 if(ret<0){f_av_frame_free(&f);return count;}
 AcquireSRWLockExclusive(&d->lock);
 while(d->write-d->read>=Q&&!InterlockedCompareExchange(&d->stopping,0,0))SleepConditionVariableSRW(&d->space,&d->lock,INFINITE,0);
 if(InterlockedCompareExchange(&d->stopping,0,0)){ReleaseSRWLockExclusive(&d->lock);f_av_frame_free(&f);return -1;}
 unsigned slot=d->write++%Q;d->frames[slot]=f;d->itemSeen[slot]=0;d->pts[slot]=d->continuous||f->best_effort_timestamp==AV_NOPTS_VALUE?d->nextPts:(u64)f->best_effort_timestamp;
 d->nextPts=d->pts[slot]+(d->ctx->framerate.num ? (90000ull*d->ctx->framerate.den/d->ctx->framerate.num):3003);d->user[slot]=user;
 ReleaseSRWLockExclusive(&d->lock);count++;
 if(env_flag("CV_AV_TRACE",&s_av_trace))fprintf(stderr,"[cellVdec] REAL_PICTURE %dx%d pts=%llu\n",f->width,f->height,(unsigned long long)d->pts[slot]);
 if(notify)notify(opaque);
 }
}
int cv_video_decode(CvVideoDecoder* d,const u8* data,u32 size,u64 pts,u64 user,void (*notify)(void*),void* opaque)
{
 /* Parser input needs AV_INPUT_BUFFER_PADDING_SIZE zero bytes beyond its end. */
 u8* copy=calloc(1,(size_t)size+AV_INPUT_BUFFER_PADDING_SIZE);if(!copy)return -1;memcpy(copy,data,size);
 if((!d->continuous||!d->timelineStarted)&&pts!=(u64)AV_NOPTS_VALUE&&pts!=~(u64)0){d->nextPts=pts;d->timelineStarted=1;}
 int pictures=0;const u8* at=copy;int remaining=(int)size;
 while(remaining>0){u8* packet_data=NULL;int packet_size=0;
 int used=f_av_parser_parse2(d->parser,d->ctx,&packet_data,&packet_size,at,remaining,pts,AV_NOPTS_VALUE,0);
 if(used<0){free(copy);return -1;}
 at+=used;remaining-=used;
 if(packet_size){AVPacket* pkt=f_av_packet_alloc();if(!pkt){free(copy);return -1;}
 if(f_av_new_packet(pkt,packet_size)<0){f_av_packet_free(&pkt);free(copy);return -1;}
 memcpy(pkt->data,packet_data,packet_size);pkt->pts=d->parser->pts;pkt->dts=d->parser->dts;
 int ret=f_avcodec_send_packet(d->ctx,pkt);f_av_packet_free(&pkt);if(ret<0){free(copy);return -1;}
 ret=receive(d,pts,user,notify,opaque);if(ret<0){free(copy);return -1;}pictures+=ret;}
 if(!used&&!packet_size)break;
 pts=AV_NOPTS_VALUE;
 }
 free(copy);return pictures;
}
/* Metadata is consumed independently of the picture queue. A player may
 * fetch several items before requesting any output pixels. */
int cv_video_peek(CvVideoDecoder* d,u16* w,u16* h,u64* pts,u64* user,u32* frameRate,u32* pictureType)
{
 AcquireSRWLockExclusive(&d->lock);
 unsigned n=d->read;
 while(n!=d->write&&d->itemSeen[n%Q])n++;
 if(n==d->write){ReleaseSRWLockExclusive(&d->lock);return 0;}
 unsigned i=n%Q;d->itemSeen[i]=1;
 *w=(u16)d->frames[i]->width;*h=(u16)d->frames[i]->height;*pts=d->pts[i];*user=d->user[i];
 *pictureType=(u32)d->frames[i]->pict_type;
 int num=d->ctx->framerate.num,den=d->ctx->framerate.den;
 double fps=num&&den?(double)num/den:29.97002997;
 *frameRate=fps<23.99?1:fps<24.5?2:fps<27?3:fps<29.99?4:fps<40?5:fps<55?6:fps<59.99?7:8;
 ReleaseSRWLockExclusive(&d->lock);return 1;
}
int cv_video_picture(CvVideoDecoder* d,u32 format,u8 alpha,u8* dst)
{
 if(format>3)return -1;
 AcquireSRWLockExclusive(&d->lock);if(d->read==d->write){ReleaseSRWLockExclusive(&d->lock);return 0;}
 unsigned i=d->read++%Q;
 if(env_flag("CV_VIDEO_PACE_TRACE",&s_pace_trace)){
   ULONGLONG now=GetTickCount64();if(!d->started){d->started=now;d->firstPts=d->pts[i];}
   d->shown++;
   if(now-d->lastLog>=1000){fprintf(stderr,"[cellVdec] delivery wall_ms=%llu movie_ms=%llu frames=%llu queued=%u\n",(unsigned long long)(now-d->started),(unsigned long long)((d->pts[i]-d->firstPts)/90),(unsigned long long)d->shown,d->write-d->read);d->lastLog=now;}
 }
 AVFrame* f=d->frames[i];d->frames[i]=NULL;WakeConditionVariable(&d->space);ReleaseSRWLockExclusive(&d->lock);
 if(dst){enum AVPixelFormat fmt[]={AV_PIX_FMT_ARGB,AV_PIX_FMT_RGBA,AV_PIX_FMT_UYVY422,AV_PIX_FMT_YUV420P};
 int w=f->width,h=f->height;uint8_t* planes[4]={dst,NULL,NULL,NULL};int stride[4]={w*4,0,0,0};
 if(format==2)stride[0]=w*2;
 if(format==3){stride[0]=w;stride[1]=stride[2]=(w+1)/2;planes[1]=dst+w*h;planes[2]=planes[1]+stride[1]*((h+1)/2);}
 if(!d->scaler || d->scaler_w!=w || d->scaler_h!=h || d->scaler_fmt!=f->format || d->scaler_out!=format){
   if(d->scaler)f_sws_freeContext(d->scaler);
   d->scaler=f_sws_getContext(w,h,f->format,w,h,fmt[format],SWS_BILINEAR,NULL,NULL,NULL);
   d->scaler_w=w;d->scaler_h=h;d->scaler_fmt=f->format;d->scaler_out=format;
 }
 if(!d->scaler){f_av_frame_free(&f);return -1;}
 f_sws_scale(d->scaler,(const uint8_t*const*)f->data,f->linesize,0,h,planes,stride);
 if(format==3 && env_flag("CV_VIDEO_CAPTURE",&s_capture)) { static unsigned n=0; unsigned seq=n++;
   if(seq%10==0) { char path[100];snprintf(path,sizeof path,"decoded_%03u.pgm",seq);
     FILE* pf=fopen(path,"wb");if(pf){fprintf(pf,"P5\n%d %d\n255\n",w,h);fwrite(dst,1,(size_t)w*h,pf);fclose(pf);} }
 }

 if(format<2)for(int p=0;p<w*h;p++)dst[p*4+(format==0?0:3)]=alpha;
 }
 f_av_frame_free(&f);return 1;
}

/* ATRAC3plus ATS stream decoding; included by cv_ffmpeg.c. */
typedef struct { AVFrame* frame; CvPcmInfo info; } CvPcmFrame;
struct CvAudioDecoder {
 AVCodecContext* ctx;
 u8* pending; size_t size;
 u64 nextPts;int continuous,timelineStarted;
 CvPcmFrame frames[64]; unsigned read,write;
 SRWLOCK lock; CONDITION_VARIABLE space;
};
CvAudioDecoder* cv_audio_open(void)
{
 InitOnceExecuteOnce(&once,load_ffmpeg,NULL,NULL);if(!loaded)return NULL;
 CvAudioDecoder* d=calloc(1,sizeof(*d));if(!d)return NULL;d->continuous=getenv("CV_AV_CONTINUOUS_PTS")!=NULL;
 InitializeSRWLock(&d->lock);InitializeConditionVariable(&d->space);return d;
}
void cv_audio_close(CvAudioDecoder* d)
{
 if(!d)return;
 for(unsigned i=0;i<64;i++)if(d->frames[i].frame)f_av_frame_free(&d->frames[i].frame);
 if(d->ctx)f_avcodec_free_context(&d->ctx);free(d->pending);free(d);
}
int cv_audio_decode(CvAudioDecoder* d,const u8* data,u32 size,u64 pts,u64 user,void (*notify)(void*),void* arg)
{
 if(!d||size>0x1000000||d->size>0x1000000-size)return -1;
 if(!d->size&&(!d->continuous||!d->timelineStarted)&&pts!=~(u64)0&&pts!=(u64)AV_NOPTS_VALUE){d->nextPts=pts;d->timelineStarted=1;}
 u8* p=realloc(d->pending,d->size+size+AV_INPUT_BUFFER_PADDING_SIZE);if(!p)return -1;
 d->pending=p;memcpy(p+d->size,data,size);d->size+=size;
 memset(p+d->size,0,AV_INPUT_BUFFER_PADDING_SIZE);
 size_t at=0;int count=0;
 while(d->size-at>=8){
  const u8* h=p+at;
  if(h[0]!=0x0f||h[1]!=0xd0){fprintf(stderr,"[cellAdec] invalid ATS sync at=%zu bytes=%02X%02X\n",at,h[0],h[1]);return -1;}
  unsigned params=(h[2]<<8)|h[3],bytes=((params&1023)+1)*8,cfg=(params>>10)&7;
  unsigned rate=(params>>13)==1?44100:(params>>13)==2?48000:0;
  if(!rate||cfg<1||cfg>2)return -1;
  if(d->size-at<bytes+8)break;
  if(!d->ctx){
   const AVCodec* c=f_avcodec_find_decoder(AV_CODEC_ID_ATRAC3P);if(!c)return -1;
   d->ctx=f_avcodec_alloc_context3(c);if(!d->ctx)return -1;
   d->ctx->sample_rate=rate;d->ctx->ch_layout.nb_channels=cfg;d->ctx->block_align=bytes;
   if(f_avcodec_open2(d->ctx,c,NULL)<0)return -1;
   fprintf(stderr,"[cellAdec] FFmpeg ATRAC3plus rate=%u channels=%u block=%u\n",rate,cfg,bytes);
  }
  AVPacket* pkt=f_av_packet_alloc();if(!pkt)return -1;
  if(f_av_new_packet(pkt,bytes)<0){f_av_packet_free(&pkt);return -1;}
  memcpy(pkt->data,h+8,bytes);int ret=f_avcodec_send_packet(d->ctx,pkt);f_av_packet_free(&pkt);
  if(ret<0){fprintf(stderr,"[cellAdec] ATRAC send error=%d\n",ret);return -1;}
  for(;;){
   AVFrame* f=f_av_frame_alloc();if(!f)return -1;
   ret=f_avcodec_receive_frame(d->ctx,f);
   if(ret==AVERROR(EAGAIN)||ret==AVERROR_EOF){f_av_frame_free(&f);break;}
   if(ret<0){f_av_frame_free(&f);return -1;}
   if(f->format!=AV_SAMPLE_FMT_FLTP){f_av_frame_free(&f);return -1;}
   AcquireSRWLockExclusive(&d->lock);
   while(d->write-d->read>=64)SleepConditionVariableSRW(&d->space,&d->lock,INFINITE,0);
   unsigned i=d->write++%64;d->frames[i].frame=f;
   d->frames[i].info=(CvPcmInfo){f->nb_samples,f->ch_layout.nb_channels,rate,bytes,d->nextPts,user};
   d->nextPts+=(90000ull*f->nb_samples)/rate;
   ReleaseSRWLockExclusive(&d->lock);
   if(env_flag("CV_AV_TRACE",&s_av_trace))fprintf(stderr,"[cellAdec] REAL_PCM samples=%d rate=%u channels=%d pts=%llu\n",f->nb_samples,rate,f->ch_layout.nb_channels,(unsigned long long)d->frames[i].info.pts);
   count++;if(notify)notify(arg);
  }
  at+=bytes+8;
 }
 d->size-=at;memmove(p,p+at,d->size);return count;
}
int cv_audio_peek(CvAudioDecoder* d,CvPcmInfo* info)
{
 AcquireSRWLockShared(&d->lock);int has=d->read!=d->write;
 if(has)*info=d->frames[d->read%64].info;
 ReleaseSRWLockShared(&d->lock);return has;
}
int cv_audio_pcm(CvAudioDecoder* d,u32 bw,u8* dst,int consume)
{
 AcquireSRWLockExclusive(&d->lock);
 if(d->read==d->write){ReleaseSRWLockExclusive(&d->lock);return 0;}
 unsigned i=d->read%64;AVFrame* f=d->frames[i].frame;
 if(dst){
  unsigned word=bw&127;if(word!=2&&word!=3&&word!=4){ReleaseSRWLockExclusive(&d->lock);return -1;}
  for(int sample=0;sample<f->nb_samples;sample++)for(int ch=0;ch<f->ch_layout.nb_channels;ch++){
   float v=((float*)f->extended_data[ch])[sample];if(v>0.99999988f)v=0.99999988f;if(v< -1.f)v=-1.f;
   u32 bits;if(bw==0x84)memcpy(&bits,&v,4);else if(word==2)bits=(u16)(s16)(v*32768.f);else bits=(u32)(s32)(v*2147483648.0);
   for(unsigned b=0;b<word;b++)*dst++=(u8)(bits>>(8*(word-1-b)));
  }
 }
 if(consume){d->frames[i].frame=NULL;d->read++;WakeConditionVariable(&d->space);}
 ReleaseSRWLockExclusive(&d->lock);if(consume)f_av_frame_free(&f);return 1;
}
