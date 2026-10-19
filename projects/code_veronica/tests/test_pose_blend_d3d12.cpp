#include "rsx_pose_gpu.h"
#include "rsx_dxc.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
using Microsoft::WRL::ComPtr;
int main() {
    constexpr unsigned count=4096,bank=count*RSX_POSE_GPU_STRIDE;
    std::vector<uint8_t> records(bank*2,0xa5);std::vector<float> expected(count*2*4);
    for(unsigned parity=0;parity<2;parity++)for(unsigned i=0;i<count;i++) {
        auto* record=records.data()+parity*bank+i*RSX_POSE_GPU_STRIDE;
        float* e=expected.data()+(parity*count+i)*4;
        e[0]=float(i);e[1]=-2;e[2]=4;e[3]=3.75f;rsx_pose_gpu_reset(record);
        float a[3],b[3],t=.5f+float((i*31+parity)%1025)/2048.f;
        for(unsigned k=0;k<3;k++) {
            a[k]=float(int((i*1664525u+k*1013904223u+parity)%200001)-100000)*.00390625f;
            b[k]=a[k]+float(int((i*1237u+k*1931u)%257)-128)*.03125f;
            if(i<32){a[k]=(i&(1u<<k))?-0.f:0.f;b[k]=(i&(1u<<(k+1)))?-0.f:0.f;}
        }
        if(i%4==0){rsx_pose_gpu_write(record,b);memcpy(e,b,12);}
        if(i%4==1){assert(rsx_pose_gpu_write_blend(record,a,b,t));for(int k=0;k<3;k++)e[k]=a[k]+(b[k]-a[k])*t;}
        if(i==count-1){rsx_pose_gpu_write_blend(record,a,b,t);rsx_pose_gpu_reset(record);}
    }
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> dev;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev))));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
    assert(SUCCEEDED(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;
    ComPtr<ID3DBlob> sig,error;assert(SUCCEEDED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&error)));
    ComPtr<ID3D12RootSignature> root;assert(SUCCEEDED(dev->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&root))));
    auto buffer=[&](uint64_t size,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags) {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=heap;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
        ComPtr<ID3D12Resource> result;assert(SUCCEEDED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result))));return result;
    };
    auto input=buffer(records.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE);
    auto output=buffer(count*16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback=buffer(count*16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_NONE);
    void* mapped=nullptr;D3D12_RANGE none{};assert(SUCCEEDED(input->Map(0,&none,&mapped)));memcpy(mapped,records.data(),records.size());input->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))));
    assert(SUCCEEDED(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
    assert(SUCCEEDED(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventA(nullptr,FALSE,FALSE,nullptr);assert(event);uint64_t fence_value=0;
    const char source[]="ByteAddressBuffer vf_buf:register(t0);\n#define VF_POSE_OFF_U 0u\n" RSX_POSE_GPU_HLSL
        "RWStructuredBuffer<float4> dst:register(u0);\n[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID){uint i=(id.x+17u)%4096u;dst[id.x]=vf_pose_position(i,float4(i,-2,4,3.75));}\n";
    std::vector<uint8_t> cpu_records(count*2*RSX_POSE_GPU_CPU_STRIDE);
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<count;i++) {
        void* destination=cpu_records.data()+(p*count+i)*RSX_POSE_GPU_CPU_STRIDE;float xyz[3];rsx_pose_gpu_reset(destination);
        if(rsx_pose_gpu_read(records.data()+(p*count+i)*RSX_POSE_GPU_STRIDE,xyz))rsx_pose_gpu_write(destination,xyz);
    }
    for(unsigned compiler=0;compiler<4;compiler++) {
        unsigned stride=compiler<2?RSX_POSE_GPU_STRIDE:RSX_POSE_GPU_CPU_STRIDE,active_bank=count*stride;
        auto& active=compiler<2?records:cpu_records;
        assert(SUCCEEDED(input->Map(0,&none,&mapped)));memcpy(mapped,active.data(),active.size());input->Unmap(0,nullptr);
        std::string shader="#define VF_POSE_STRIDE_U "+std::to_string(stride)+"u\n"+source;
        ComPtr<ID3DBlob> fxc;void* dxil=nullptr;size_t size=0;char* diagnostic=nullptr;
        D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root.Get();
        if(!(compiler&1)){assert(SUCCEEDED(D3DCompile(shader.data(),shader.size(),"pose blend",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&fxc,&error)));ps.CS={fxc->GetBufferPointer(),fxc->GetBufferSize()};}
        else {int result=rsx_dxc_compile(shader.data(),shader.size(),"cs_6_0",&dxil,&size,&diagnostic);if(result)std::printf("DXC: %s\n",diagnostic?diagnostic:"unavailable");assert(result==0);ps.CS={dxil,size};}
        ComPtr<ID3D12PipelineState> pipeline;assert(SUCCEEDED(dev->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline))));free(dxil);free(diagnostic);
        for(unsigned parity=0;parity<2;parity++) {
            assert(SUCCEEDED(alloc->Reset()));assert(SUCCEEDED(list->Reset(alloc.Get(),pipeline.Get())));
            list->SetComputeRootSignature(root.Get());list->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress()+parity*active_bank);list->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());list->Dispatch(count/64,1,1);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=output.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&barrier);
            list->CopyResource(readback.Get(),output.Get());barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;list->ResourceBarrier(1,&barrier);
            assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);assert(SUCCEEDED(queue->Signal(fence.Get(),++fence_value)));assert(SUCCEEDED(fence->SetEventOnCompletion(fence_value,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
            assert(SUCCEEDED(readback->Map(0,nullptr,&mapped)));
            for(unsigned thread=0;thread<count;thread++){unsigned i=(thread+17)%count;assert(!memcmp((float*)mapped+thread*4,expected.data()+(parity*count+i)*4,16));}
            readback->Unmap(0,&none);
        }
    }
    CloseHandle(event);puts("PASS D3D12 WARP: FXC and production DXC, 32768 bit-exact outputs, CPU 16-byte / GPU 32-byte records, both parity banks, GPU blend/CPU fallback/inactive reset, signed zero and original w");
}
