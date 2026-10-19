/* Optional, sampled D3D12 timing. Caller supplies an EXISTING frame fence;
 * this module never submits, signals, waits, resets or changes draw state.
 * Include after d3d12.h (C COM interface). */
#ifndef RSX_FRAME_GPU_PROBE_H
#define RSX_FRAME_GPU_PROBE_H
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
#define RSX_FRAME_GPU_IID(x) (x)
#else
#define RSX_FRAME_GPU_IID(x) (&(x))
#endif
#define RSX_FRAME_GPU_SLOTS 4u
#define RSX_FRAME_GPU_PARTS 512u
#define RSX_FRAME_GPU_TAGS 8u
#define RSX_FRAME_GPU_SURFACES 64u
typedef struct {uint32_t tag,offset,width,height;} RsxFrameGpuSurfaceKey;
typedef struct {RsxFrameGpuSurfaceKey key;double ms;uint32_t ranges;} RsxFrameGpuSurface;
typedef struct { double lists_ms,span_ms,gaps_ms; } RsxFrameGpuSample;
static inline int rsx_frame_gpu_sample(const uint64_t* t,uint32_t parts,uint64_t frequency,RsxFrameGpuSample* out) {
    if(!t||!out||!frequency||!parts||parts>RSX_FRAME_GPU_PARTS)return 0;
    double sum=0;
    for(uint32_t i=0;i<parts;i++) {
        uint64_t a=t[i*2],b=t[i*2+1];
        if(b<a||(i&&a<t[i*2-1]))return 0;
        sum+=(double)(b-a);
    }
    double scale=1000.0/(double)frequency;
    out->lists_ms=sum*scale;out->span_ms=(double)(t[parts*2-1]-t[0])*scale;
    out->gaps_ms=out->span_ms-out->lists_ms;if(out->gaps_ms<0)out->gaps_ms=0;
    return 1;
}
typedef struct {
    ID3D12QueryHeap* heap;ID3D12Resource* readback;const uint64_t* mapped;
    uint64_t frequency,required[RSX_FRAME_GPU_SLOTS];
    uint32_t pending[RSX_FRAME_GPU_SLOTS],discard[RSX_FRAME_GPU_SLOTS];
    uint32_t slots,slot,parts,active,open;
    uint32_t samples,invalid,overflow;
    uint32_t tag;
    uint8_t tags[RSX_FRAME_GPU_SLOTS][RSX_FRAME_GPU_PARTS];
    uint32_t width[RSX_FRAME_GPU_SLOTS],height[RSX_FRAME_GPU_SLOTS],scale_bits[RSX_FRAME_GPU_SLOTS];
    uint32_t result_width,result_height,result_scale_bits,context_mixed;
    double tag_ms[RSX_FRAME_GPU_TAGS];
    RsxFrameGpuSurfaceKey surface_key,pending_surfaces[RSX_FRAME_GPU_SLOTS][RSX_FRAME_GPU_PARTS];
    RsxFrameGpuSurface surfaces[RSX_FRAME_GPU_SURFACES];
    uint32_t surface_count,surface_overflow;double surface_other_ms;
    double lists_ms,span_ms,gaps_ms;
} RsxFrameGpuProbe;
static inline void rsx_frame_gpu_add_surface(RsxFrameGpuProbe* p,const RsxFrameGpuSurfaceKey* key,double ms) {
    if(!key->width||!key->height){p->surface_other_ms+=ms;return;}
    uint32_t i=0;
    for(;i<p->surface_count;i++)if(p->surfaces[i].key.tag==key->tag&&p->surfaces[i].key.offset==key->offset&&p->surfaces[i].key.width==key->width&&p->surfaces[i].key.height==key->height)break;
    if(i==p->surface_count) {
        if(i==RSX_FRAME_GPU_SURFACES){p->surface_overflow++;p->surface_other_ms+=ms;return;}
        p->surfaces[i].key=*key;p->surfaces[i].ms=0;p->surfaces[i].ranges=0;p->surface_count++;
    }
    p->surfaces[i].ms+=ms;p->surfaces[i].ranges++;
}
static inline void rsx_frame_gpu_destroy(RsxFrameGpuProbe* p) {
    if(p->mapped&&p->readback){D3D12_RANGE written={0,0};p->readback->lpVtbl->Unmap(p->readback,0,&written);}
    if(p->readback)p->readback->lpVtbl->Release(p->readback);
    if(p->heap)p->heap->lpVtbl->Release(p->heap);
    memset(p,0,sizeof *p);
}
static inline int rsx_frame_gpu_init(RsxFrameGpuProbe* p,ID3D12Device* device,ID3D12CommandQueue* queue,uint32_t slots) {
    memset(p,0,sizeof *p);
    if(!device||!queue||!slots||slots>RSX_FRAME_GPU_SLOTS)return 0;
    if(FAILED(queue->lpVtbl->GetTimestampFrequency(queue,&p->frequency))||!p->frequency)return 0;
    D3D12_QUERY_HEAP_DESC q;memset(&q,0,sizeof q);q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=slots*RSX_FRAME_GPU_PARTS*2;
    D3D12_HEAP_PROPERTIES hp;memset(&hp,0,sizeof hp);hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd;memset(&rd,0,sizeof rd);rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=(uint64_t)q.Count*8;
    rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device->lpVtbl->CreateQueryHeap(device,&q,RSX_FRAME_GPU_IID(IID_ID3D12QueryHeap),(void**)&p->heap))||
       FAILED(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,
           NULL,RSX_FRAME_GPU_IID(IID_ID3D12Resource),(void**)&p->readback))) {rsx_frame_gpu_destroy(p);return 0;}
    D3D12_RANGE range={0,(SIZE_T)rd.Width};
    if(FAILED(p->readback->lpVtbl->Map(p->readback,0,&range,(void**)&p->mapped))) {rsx_frame_gpu_destroy(p);return 0;}
    p->slots=slots;return 1;
}
static inline void rsx_frame_gpu_collect(RsxFrameGpuProbe* p,uint64_t completed_fence) {
    if(!p->mapped)return;
    for(uint32_t slot=0;slot<p->slots;slot++) {
        if(!p->pending[slot]||completed_fence<p->required[slot])continue;
        if(!p->discard[slot]) {
            RsxFrameGpuSample sample;
            if(rsx_frame_gpu_sample(p->mapped+(size_t)slot*RSX_FRAME_GPU_PARTS*2,p->pending[slot],p->frequency,&sample)) {
                if(!p->samples){p->result_width=p->width[slot];p->result_height=p->height[slot];p->result_scale_bits=p->scale_bits[slot];}
                else if(p->result_width!=p->width[slot]||p->result_height!=p->height[slot]||p->result_scale_bits!=p->scale_bits[slot])p->context_mixed=1;
                const uint64_t* t=p->mapped+(size_t)slot*RSX_FRAME_GPU_PARTS*2;
                for(uint32_t i=0;i<p->pending[slot];i++) {
                    uint32_t tag=p->tags[slot][i];if(tag>=RSX_FRAME_GPU_TAGS)tag=0;
                    double ms=(double)(t[i*2+1]-t[i*2])*1000.0/(double)p->frequency;
                    p->tag_ms[tag]+=ms;rsx_frame_gpu_add_surface(p,&p->pending_surfaces[slot][i],ms);
                }
                p->lists_ms+=sample.lists_ms;p->span_ms+=sample.span_ms;p->gaps_ms+=sample.gaps_ms;p->samples++;
            }else p->invalid++;
        }
        p->pending[slot]=0;
    }
}
static inline void rsx_frame_gpu_begin_frame(RsxFrameGpuProbe* p,uint32_t slot,int enabled) {
    p->active=enabled&&p->mapped&&slot<p->slots&&!p->pending[slot];p->open=0;p->parts=0;p->slot=slot;
}
static inline void rsx_frame_gpu_begin_list(RsxFrameGpuProbe* p,ID3D12GraphicsCommandList* list) {
    if(!p->active)return;
    if(p->open||p->parts>=RSX_FRAME_GPU_PARTS){p->overflow++;p->active=0;p->open=0;return;}
    list->lpVtbl->EndQuery(list,p->heap,D3D12_QUERY_TYPE_TIMESTAMP,(p->slot*RSX_FRAME_GPU_PARTS+p->parts)*2);
    p->tag=0;memset(&p->surface_key,0,sizeof p->surface_key);p->open=1;
}
static inline void rsx_frame_gpu_end_list(RsxFrameGpuProbe* p,ID3D12GraphicsCommandList* list) {
    if(!p->active||!p->open)return;
    list->lpVtbl->EndQuery(list,p->heap,D3D12_QUERY_TYPE_TIMESTAMP,(p->slot*RSX_FRAME_GPU_PARTS+p->parts)*2+1);
    p->tags[p->slot][p->parts]=(uint8_t)p->tag;p->pending_surfaces[p->slot][p->parts]=p->surface_key;p->parts++;p->open=0;
}
/* Optional range boundaries: timestamps only, on the CURRENT command list.
 * Existing frame fence gates every read; no submission/wait/state change. */
static inline void rsx_frame_gpu_scope(RsxFrameGpuProbe* p,ID3D12GraphicsCommandList* list,uint32_t tag,uint32_t target,uint32_t width,uint32_t height) {
    if(!p->active||!p->open||tag>=RSX_FRAME_GPU_TAGS)return;
    if(tag==p->tag&&p->surface_key.offset==target&&p->surface_key.width==width&&p->surface_key.height==height)return;
    rsx_frame_gpu_end_list(p,list);rsx_frame_gpu_begin_list(p,list);
    if(p->active){p->tag=tag;p->surface_key.tag=tag;p->surface_key.offset=target;p->surface_key.width=width;p->surface_key.height=height;}
}
static inline void rsx_frame_gpu_tag(RsxFrameGpuProbe* p,ID3D12GraphicsCommandList* list,uint32_t tag) {
    rsx_frame_gpu_scope(p,list,tag,0,0,0);
}
static inline void rsx_frame_gpu_context(RsxFrameGpuProbe* p,uint32_t width,uint32_t height,float scale) {
    if(!p->active)return;p->width[p->slot]=width;p->height[p->slot]=height;
    memcpy(&p->scale_bits[p->slot],&scale,4);
}
static inline void rsx_frame_gpu_end_frame(RsxFrameGpuProbe* p,ID3D12GraphicsCommandList* list,uint64_t required_fence,int discard) {
    if(!p->active||p->open||!p->parts)return;
    if(!required_fence){p->active=0;p->invalid++;return;}
    uint32_t slot=p->slot,first=slot*RSX_FRAME_GPU_PARTS*2;
    list->lpVtbl->ResolveQueryData(list,p->heap,D3D12_QUERY_TYPE_TIMESTAMP,first,p->parts*2,p->readback,(uint64_t)first*8);
    p->pending[slot]=p->parts;p->required[slot]=required_fence;p->discard[slot]=discard!=0;p->active=0;
}
#undef RSX_FRAME_GPU_IID
#endif
