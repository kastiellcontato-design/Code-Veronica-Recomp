#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "rsx_frame_gpu_probe.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static void reducer() {
    uint64_t stamps[]={100,120,170,180,200,240};RsxFrameGpuSample sample{};
    assert(rsx_frame_gpu_sample(stamps,3,1000,&sample));
    assert(sample.lists_ms==70&&sample.span_ms==140&&sample.gaps_ms==70);
    assert(!rsx_frame_gpu_sample(stamps,3,0,&sample));
    assert(!rsx_frame_gpu_sample(stamps,0,1000,&sample));
    assert(!rsx_frame_gpu_sample(stamps,RSX_FRAME_GPU_PARTS+1,1000,&sample));
    assert(!rsx_frame_gpu_sample(nullptr,3,1000,&sample));
    uint64_t reversed[]={200,199},overlap[]={100,120,119,130};
    assert(!rsx_frame_gpu_sample(reversed,1,1000,&sample));
    assert(!rsx_frame_gpu_sample(overlap,2,1000,&sample));
    uint64_t zero[]={100,100};assert(rsx_frame_gpu_sample(zero,1,1000,&sample)&&sample.lists_ms==0);
    uint64_t large[]={UINT64_MAX-140,UINT64_MAX-70,UINT64_MAX-60,UINT64_MAX};
    assert(rsx_frame_gpu_sample(large,2,1000,&sample)&&sample.lists_ms==130&&sample.span_ms==140&&sample.gaps_ms==10);
}
int main() {
    reducer();
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)&factory)));
    assert(SUCCEEDED(factory->lpVtbl->EnumWarpAdapter(factory,IID_IDXGIAdapter,(void**)&adapter)));
    assert(SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)&device)));
    ID3D12CommandQueue* queue=nullptr;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    assert(SUCCEEDED(device->lpVtbl->CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,(void**)&queue)));
    ID3D12CommandAllocator* alloc[2]{};ID3D12GraphicsCommandList* list=nullptr;ID3D12Fence* fence=nullptr;
    for(auto& a:alloc)assert(SUCCEEDED(device->lpVtbl->CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&a)));
    assert(SUCCEEDED(device->lpVtbl->CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc[0],nullptr,IID_ID3D12GraphicsCommandList,(void**)&list)));
    assert(SUCCEEDED(list->lpVtbl->Close(list)));
    assert(SUCCEEDED(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence)));
    HANDLE event=CreateEventA(nullptr,FALSE,FALSE,nullptr);assert(event);
    RsxFrameGpuProbe probe{};assert(rsx_frame_gpu_init(&probe,device,queue,2));
    // Production probe, multiple real D3D12 submissions, both banks reused.
    for(unsigned frame=0;frame<8;frame++) {
        unsigned slot=frame%2;bool enabled=frame!=6;bool discard=frame==7;
        for(auto a:alloc)assert(SUCCEEDED(a->lpVtbl->Reset(a)));
        assert(SUCCEEDED(list->lpVtbl->Reset(list,alloc[0],nullptr)));
        rsx_frame_gpu_begin_frame(&probe,slot,enabled);rsx_frame_gpu_context(&probe,frame<4?1280:1920,frame<4?720:1080,frame<4?1.f:1.5f);rsx_frame_gpu_begin_list(&probe,list);
        rsx_frame_gpu_scope(&probe,list,1,0x7d90000,768,512);uint32_t parts=probe.parts;
        rsx_frame_gpu_scope(&probe,list,1,0x7d90000,768,512);assert(probe.parts==parts); // same tag creates no extra query
        rsx_frame_gpu_tag(&probe,list,RSX_FRAME_GPU_TAGS);assert(probe.parts==parts);
        rsx_frame_gpu_tag(&probe,list,4);
        rsx_frame_gpu_end_list(&probe,list);
        assert(SUCCEEDED(list->lpVtbl->Close(list)));
        ID3D12CommandList* lists[]={(ID3D12CommandList*)list};queue->lpVtbl->ExecuteCommandLists(queue,1,lists);
        assert(SUCCEEDED(list->lpVtbl->Reset(list,alloc[1],nullptr)));
        rsx_frame_gpu_begin_list(&probe,list);rsx_frame_gpu_tag(&probe,list,5);rsx_frame_gpu_end_list(&probe,list);
        rsx_frame_gpu_end_frame(&probe,list,frame+1,discard);
        assert(SUCCEEDED(list->lpVtbl->Close(list)));queue->lpVtbl->ExecuteCommandLists(queue,1,lists);
        unsigned samples=probe.samples;
        rsx_frame_gpu_collect(&probe,frame);assert(probe.samples==samples); // fence NOT complete: no read
        if(enabled) {
            assert(probe.pending[slot]==5);
            rsx_frame_gpu_begin_frame(&probe,slot,1);assert(!probe.active); // cannot overwrite pending queries
        }else assert(!probe.pending[slot]);
        assert(SUCCEEDED(queue->lpVtbl->Signal(queue,fence,frame+1)));
        assert(SUCCEEDED(fence->lpVtbl->SetEventOnCompletion(fence,frame+1,event)));
        assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        rsx_frame_gpu_collect(&probe,fence->lpVtbl->GetCompletedValue(fence));
        assert(probe.samples==samples+(enabled&&!discard?1:0));assert(!probe.pending[slot]&&!probe.invalid);
    }
    assert(probe.samples==6&&std::isfinite(probe.lists_ms)&&probe.lists_ms>=0&&probe.span_ms>=probe.lists_ms);
    double tagged=0;for(double ms:probe.tag_ms){assert(ms>=0);tagged+=ms;}
    assert(fabs(tagged-probe.lists_ms)<1e-7&&probe.context_mixed);
    assert(probe.tag_ms[1]>=0&&probe.tag_ms[4]>=0&&probe.tag_ms[5]>=0);
    assert(probe.surface_count==1&&probe.surfaces[0].key.offset==0x7d90000&&probe.surfaces[0].key.width==768);
    assert(fabs(probe.surfaces[0].ms+probe.surface_other_ms-probe.lists_ms)<1e-7);
    {RsxFrameGpuProbe surface_probe{};double total=0;
     for(unsigned i=0;i<RSX_FRAME_GPU_SURFACES+8;i++){RsxFrameGpuSurfaceKey key{2,0x1000,16+i,32};rsx_frame_gpu_add_surface(&surface_probe,&key,1);total++;}
     RsxFrameGpuSurfaceKey repeat{2,0x1000,16,32};rsx_frame_gpu_add_surface(&surface_probe,&repeat,2);total+=2;
     assert(surface_probe.surface_count==RSX_FRAME_GPU_SURFACES&&surface_probe.surface_overflow==8&&surface_probe.surfaces[0].ms==3);
     double sum=surface_probe.surface_other_ms;for(unsigned i=0;i<surface_probe.surface_count;i++)sum+=surface_probe.surfaces[i].ms;assert(sum==total);}
    // Part capacity, invalid slot and missing fence fail closed without issuing
    // out-of-range queries. No pending sample may be read in these cases.
    rsx_frame_gpu_begin_frame(&probe,2,1);assert(!probe.active);
    rsx_frame_gpu_begin_frame(&probe,0,1);probe.parts=RSX_FRAME_GPU_PARTS;
    rsx_frame_gpu_begin_list(&probe,list);assert(!probe.active&&probe.overflow);
    rsx_frame_gpu_begin_frame(&probe,0,1);probe.parts=1;
    rsx_frame_gpu_end_frame(&probe,list,0,0);assert(!probe.pending[0]&&!probe.active);
    rsx_frame_gpu_destroy(&probe);assert(!probe.heap&&!probe.readback&&!probe.mapped);
    CloseHandle(event);fence->lpVtbl->Release(fence);list->lpVtbl->Release(list);
    for(auto a:alloc)a->lpVtbl->Release(a);
    queue->lpVtbl->Release(queue);device->lpVtbl->Release(device);adapter->lpVtbl->Release(adapter);factory->lpVtbl->Release(factory);
    std::puts("PASS frame GPU probe: real WARP split lists, both slots, existing-fence gating, slot reuse protection, disabled/discarded samples, tagged ranges, exact target keys/aggregation, mixed resolution, bounded overflow and timestamp reduction");
}
