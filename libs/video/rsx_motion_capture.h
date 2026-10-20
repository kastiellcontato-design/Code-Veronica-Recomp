/* F9: bounded asynchronous real-frame capture. No Present/GPU waits.
 * Included by the renderer, and by the isolated D3D12 integration test. */
#ifndef RSX_MOTION_CAPTURE_H
#define RSX_MOTION_CAPTURE_H
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
enum { MC_SLOTS=4, MC_FRAMES=36 };
typedef struct {
    ID3D12Resource* rb; HANDLE writer;
    volatile LONG state; /* 0 free, 1 GPU pending, 2 writer, 3 recorded */
    UINT64 capacity, fence;
    unsigned w,h,pitch,index; void* pose; size_t pose_bytes;
    char dir[MAX_PATH],json[4096];
} MotionSlot;
static struct {
    volatile LONG request; ID3D12Fence* fence; UINT64 serial;
    MotionSlot slots[MC_SLOTS]; unsigned remaining,index,dropped;
    char dir[MAX_PATH];
} mc;
static void mc_request(void){InterlockedExchange(&mc.request,1);}
static int mc_write(const char* path,const void* data,size_t bytes){
    FILE* f=fopen(path,"wb");if(!f)return 0;
    int ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f))ok=0;return ok;
}
static DWORD WINAPI mc_writer(LPVOID arg){
    MotionSlot* s=(MotionSlot*)arg;char path[MAX_PATH+80];int ok=1;
    void* mapped=NULL;D3D12_RANGE r={0,(SIZE_T)s->pitch*s->h};
    HRESULT hr=s->rb->lpVtbl->Map(s->rb,0,&r,&mapped);
    if(SUCCEEDED(hr)&&mapped){
        snprintf(path,sizeof path,"%s/frame_%03u.bmp",s->dir,s->index);
        FILE* f=fopen(path,"wb");unsigned char* scan=(unsigned char*)malloc((size_t)s->w*4);
        if(f&&scan){
            BITMAPFILEHEADER head={0};BITMAPINFOHEADER info={0};
            head.bfType=0x4d42;head.bfOffBits=sizeof head+sizeof info;head.bfSize=head.bfOffBits+s->w*s->h*4;
            info.biSize=sizeof info;info.biWidth=s->w;info.biHeight=-(LONG)s->h;info.biPlanes=1;info.biBitCount=32;
            ok=fwrite(&head,sizeof head,1,f)==1&&fwrite(&info,sizeof info,1,f)==1;
            for(unsigned y=0;y<s->h&&ok;y++){
                const unsigned char* src=(const unsigned char*)mapped+(size_t)y*s->pitch;
                for(unsigned x=0;x<s->w;x++){scan[x*4]=src[x*4+2];scan[x*4+1]=src[x*4+1];scan[x*4+2]=src[x*4];scan[x*4+3]=255;}
                ok=fwrite(scan,4,s->w,f)==s->w;
            }
        }else ok=0;
        if(f&&fclose(f))ok=0;free(scan);
        D3D12_RANGE written={0,0};s->rb->lpVtbl->Unmap(s->rb,0,&written);
    }else ok=0;
    snprintf(path,sizeof path,"%s/frame_%03u.json",s->dir,s->index);ok=mc_write(path,s->json,strlen(s->json))&&ok;
    if(s->pose_bytes){snprintf(path,sizeof path,"%s/frame_%03u.pose",s->dir,s->index);ok=mc_write(path,s->pose,s->pose_bytes)&&ok;}
    free(s->pose);s->pose=NULL;s->pose_bytes=0;
    fprintf(stderr,"[MOTION-CAPTURE] frame=%u %s\n",s->index,ok?"saved":"WRITE FAILED");
    InterlockedExchange(&s->state,0);return 0;
}
static void mc_poll(void){
    if(!mc.fence)return;
    int pending=0;for(unsigned i=0;i<MC_SLOTS;i++)pending|=InterlockedCompareExchange(&mc.slots[i].state,0,0)==1;
    if(!pending)return;
    UINT64 done=mc.fence->lpVtbl->GetCompletedValue(mc.fence);
    /* UINT64_MAX means device removal; never map unfinished readbacks. */
    if(done==UINT64_MAX)return;
    for(unsigned i=0;i<MC_SLOTS;i++){
        MotionSlot* s=mc.slots+i;
        if(InterlockedCompareExchange(&s->state,0,0)==1&&s->fence<=done){
            InterlockedExchange(&s->state,2);s->writer=CreateThread(NULL,0,mc_writer,s,0,NULL);
            if(!s->writer){free(s->pose);s->pose=NULL;s->pose_bytes=0;InterlockedExchange(&s->state,0);fprintf(stderr,"[MOTION-CAPTURE] writer unavailable\n");}
        }
    }
}
static int mc_wants(void){
    mc_poll();
    if(InterlockedExchange(&mc.request,0)&&!mc.remaining){
        CreateDirectoryA("motion-captures",NULL);
        char relative[100];snprintf(relative,sizeof relative,"motion-captures/session_%lu_%llu",GetCurrentProcessId(),(unsigned long long)GetTickCount64());
        DWORD full=GetFullPathNameA(relative,MAX_PATH,mc.dir,NULL);
        if(!CreateDirectoryA(relative,NULL)||!full||full>=MAX_PATH){fprintf(stderr,"[MOTION-CAPTURE] cannot create directory\n");return 0;}
        mc.remaining=MC_FRAMES;mc.index=mc.dropped=0;
        fprintf(stderr,"[MOTION-CAPTURE] start %s; %u samples; real frames before overlays; no extra GPU wait\n",mc.dir,MC_FRAMES);
    }
    if(!mc.remaining)return 0;
    for(unsigned i=0;i<MC_SLOTS;i++)if(!InterlockedCompareExchange(&mc.slots[i].state,0,0))return 1;
    mc.dropped++;return 0;
}
/* Consumes pose on success only. Resource is RGBA8 in RENDER_TARGET state. */
static int mc_record(ID3D12Device* device,ID3D12GraphicsCommandList* cl,ID3D12Resource* color,
                     unsigned w,unsigned h,const char* json,void* pose,size_t bytes){
    MotionSlot* s=NULL;
    for(unsigned i=0;i<MC_SLOTS;i++)if(!InterlockedCompareExchange(&mc.slots[i].state,0,0)){s=mc.slots+i;break;}
    if(!s||!mc.remaining||!color||!w||!h)return 0;
    if(s->writer){WaitForSingleObject(s->writer,INFINITE);CloseHandle(s->writer);s->writer=NULL;}
    unsigned pitch=(w*4+255)&~255u;UINT64 size=(UINT64)pitch*h;
    if(size>64ull*1024*1024){mc.remaining=0;fprintf(stderr,"[MOTION-CAPTURE] resolution exceeds 64 MB per slot\n");return 0;}
    if(!mc.fence&&FAILED(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&mc.fence))){mc.remaining=0;return 0;}
    if(!s->rb||s->capacity<size){
        if(s->rb)s->rb->lpVtbl->Release(s->rb);s->rb=NULL;
        D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC d={0};
        d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&s->rb))){mc.remaining=0;return 0;}s->capacity=size;
    }
    s->w=w;s->h=h;s->pitch=pitch;s->pose=pose;s->pose_bytes=bytes;s->index=mc.index++;
    snprintf(s->dir,sizeof s->dir,"%s",mc.dir);snprintf(s->json,sizeof s->json,"%s",json);
    D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=color;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;cl->lpVtbl->ResourceBarrier(cl,1,&b);
    D3D12_TEXTURE_COPY_LOCATION a={0},z={0};a.pResource=s->rb;a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    a.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;a.PlacedFootprint.Footprint.Width=w;a.PlacedFootprint.Footprint.Height=h;a.PlacedFootprint.Footprint.Depth=1;a.PlacedFootprint.Footprint.RowPitch=pitch;
    z.pResource=color;z.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;cl->lpVtbl->CopyTextureRegion(cl,&a,0,0,0,&z,NULL);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;cl->lpVtbl->ResourceBarrier(cl,1,&b);
    InterlockedExchange(&s->state,3);
    if(!--mc.remaining)fprintf(stderr,"[MOTION-CAPTURE] recorded %u samples; skipped %u busy frames; writes pending\n",mc.index,mc.dropped);
    return 1;
}
static void mc_submit(ID3D12CommandQueue* queue){
    for(unsigned i=0;i<MC_SLOTS;i++){MotionSlot* s=mc.slots+i;
        if(InterlockedCompareExchange(&s->state,0,0)==3){s->fence=++mc.serial;
            if(SUCCEEDED(queue->lpVtbl->Signal(queue,mc.fence,s->fence)))InterlockedExchange(&s->state,1);
            else fprintf(stderr,"[MOTION-CAPTURE] fence signal FAILED\n");
        }
    }
}
/* Only after the renderer's normal shutdown GPU drain. */
static void mc_shutdown(void){
    mc_poll();
    for(unsigned i=0;i<MC_SLOTS;i++){MotionSlot* s=mc.slots+i;
        if(s->writer){WaitForSingleObject(s->writer,INFINITE);CloseHandle(s->writer);s->writer=NULL;}
        if(s->rb)s->rb->lpVtbl->Release(s->rb);s->rb=NULL;free(s->pose);s->pose=NULL;
    }
    if(mc.fence)mc.fence->lpVtbl->Release(mc.fence);mc.fence=NULL;
}
#endif
