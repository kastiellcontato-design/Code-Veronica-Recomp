#ifndef RSX_POSE_COMPUTE_D3D12_H
#define RSX_POSE_COMPUTE_D3D12_H
#include "rsx_pose_compute_plan.h"
#include "rsx_pose_compute_shader.h"
#include "rsx_dxc.h"
#ifdef __cplusplus
#define PC_IID(x) x
#else
#define PC_IID(x) &x
#endif
typedef struct {
    ID3D12RootSignature* root;ID3D12PipelineState* pso[6];
    ID3D12Resource *input,*input_default,*output,*capture;void* mapped;
    uint64_t input_capacity,output_capacity;uint32_t final_offset;
    int ready,active,input_in_srv;
} RsxPoseComputeD3D12;
static inline void pc_release(RsxPoseComputeD3D12* p){
    if(p->input)p->input->lpVtbl->Release(p->input);if(p->input_default)p->input_default->lpVtbl->Release(p->input_default);if(p->output)p->output->lpVtbl->Release(p->output);
    if(p->capture)p->capture->lpVtbl->Release(p->capture);for(int i=0;i<6;i++)if(p->pso[i])p->pso[i]->lpVtbl->Release(p->pso[i]);if(p->root)p->root->lpVtbl->Release(p->root);memset(p,0,sizeof *p);
}
static inline HRESULT pc_buffer(ID3D12Device* d,D3D12_HEAP_TYPE heap,uint64_t bytes,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state,ID3D12Resource** out){
    D3D12_HEAP_PROPERTIES hp;memset(&hp,0,sizeof hp);hp.Type=heap;D3D12_RESOURCE_DESC rd;memset(&rd,0,sizeof rd);rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=flags;
    return d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,state,NULL,PC_IID(IID_ID3D12Resource),(void**)out);
}
static inline int pc_init(RsxPoseComputeD3D12* p,ID3D12Device* device,uint32_t maxv,uint32_t maxd){
    if(p->ready)return 1;
    D3D12_ROOT_PARAMETER rp[3];memset(rp,0,sizeof rp);rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[0].Constants.Num32BitValues=1;
    rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rd;memset(&rd,0,sizeof rd);rd.NumParameters=3;rd.pParameters=rp;
    ID3DBlob *sig=NULL,*err=NULL;HRESULT hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err);
    if(FAILED(hr))goto failed;
    hr=device->lpVtbl->CreateRootSignature(device,0,sig->lpVtbl->GetBufferPointer(sig),sig->lpVtbl->GetBufferSize(sig),PC_IID(IID_ID3D12RootSignature),(void**)&p->root);
    sig->lpVtbl->Release(sig);sig=NULL;if(FAILED(hr))goto failed;
    for(uint32_t phase=0;phase<6;phase++) {
        size_t length=strlen(rsx_pose_compute_shader)+64;char* source=(char*)malloc(length);if(!source)goto failed;
        snprintf(source,length,"#define PC_PHASE %u\n%s",phase,rsx_pose_compute_shader);
        void* object=NULL;size_t size=0;char* error=NULL;int rc=rsx_dxc_compile(source,strlen(source),"cs_6_0",&object,&size,&error);free(source);
        if(rc||!size){fprintf(stderr,"[POSE-COMPUTE] DXC: %.800s\n",error?error:"failed");free(object);free(error);goto failed;}
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd;memset(&pd,0,sizeof pd);pd.pRootSignature=p->root;pd.CS.pShaderBytecode=object;pd.CS.BytecodeLength=size;
        hr=device->lpVtbl->CreateComputePipelineState(device,&pd,PC_IID(IID_ID3D12PipelineState),(void**)&p->pso[phase]);free(object);free(error);if(FAILED(hr))goto failed;
    }
    p->output_capacity=(uint64_t)maxv*48+(uint64_t)maxd*12+2048;
    hr=pc_buffer(device,D3D12_HEAP_TYPE_DEFAULT,p->output_capacity,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&p->output);
    if(FAILED(hr))goto failed;p->ready=1;if(err)err->lpVtbl->Release(err);return 1;
failed:
    if(sig)sig->lpVtbl->Release(sig);if(err)err->lpVtbl->Release(err);pc_release(p);return 0;
}
/* Caller has fenced the previous frame before replacing any input storage. */
static inline int pc_upload(RsxPoseComputeD3D12* p,ID3D12Device* d,const RsxPoseComputePlan* plan){
    p->active=0;if(!p->ready||plan->output_bytes>p->output_capacity)return 0;
    if(plan->bytes>p->input_capacity){
        ID3D12Resource* input=NULL;uint64_t capacity=((uint64_t)plan->bytes+65535u)&~65535ull;
        if(FAILED(pc_buffer(d,D3D12_HEAP_TYPE_UPLOAD,capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ,&input)))return 0;
        void* map=NULL;D3D12_RANGE nr={0,0};if(FAILED(input->lpVtbl->Map(input,0,&nr,&map))){input->lpVtbl->Release(input);return 0;}
        ID3D12Resource* local=NULL;
        if(FAILED(pc_buffer(d,D3D12_HEAP_TYPE_DEFAULT,capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,&local))){input->lpVtbl->Release(input);return 0;}
        if(p->input)p->input->lpVtbl->Release(p->input);if(p->input_default)p->input_default->lpVtbl->Release(p->input_default);
        p->input=input;p->input_default=local;p->input_in_srv=0;p->mapped=map;p->input_capacity=capacity;
    }
    memcpy(p->mapped,plan->words,plan->bytes);
    MemoryBarrier(); /* finish writes to write-combined upload memory before GPU submission */
    p->final_offset=plan->final_offset;p->active=1;return 1;
}
static inline void pc_barrier(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b;memset(&b,0,sizeof b);b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.StateBefore=before;b.Transition.StateAfter=after;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;cl->lpVtbl->ResourceBarrier(cl,1,&b);
}
static inline void pc_record(RsxPoseComputeD3D12* p,ID3D12GraphicsCommandList* cl,const RsxPoseComputePlan* plan,ID3D12Resource* cpu_source,uint64_t cpu_offset,uint32_t nv){
    if(p->active){
        if(p->input_in_srv)pc_barrier(cl,p->input_default,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        cl->lpVtbl->CopyBufferRegion(cl,p->input_default,0,p->input,0,plan->bytes);
        pc_barrier(cl,p->input_default,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);p->input_in_srv=1;
        pc_barrier(cl,p->output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cl->lpVtbl->SetComputeRootSignature(cl,p->root);cl->lpVtbl->SetPipelineState(cl,p->pso[0]);
        cl->lpVtbl->SetComputeRootShaderResourceView(cl,1,p->input_default->lpVtbl->GetGPUVirtualAddress(p->input_default));
        cl->lpVtbl->SetComputeRootUnorderedAccessView(cl,2,p->output->lpVtbl->GetGPUVirtualAddress(p->output));
        for(uint32_t phase=0;phase<6;phase++){
            cl->lpVtbl->SetComputeRootSignature(cl,p->root);
            cl->lpVtbl->SetPipelineState(cl,p->pso[phase]);
            cl->lpVtbl->SetComputeRootShaderResourceView(cl,1,p->input_default->lpVtbl->GetGPUVirtualAddress(p->input_default));
            cl->lpVtbl->SetComputeRootUnorderedAccessView(cl,2,p->output->lpVtbl->GetGPUVirtualAddress(p->output));
            cl->lpVtbl->SetComputeRoot32BitConstant(cl,0,phase,0);
            uint32_t count=(phase==0||phase==2||phase==4)?plan->nd:plan->nv;
            cl->lpVtbl->Dispatch(cl,(count+63)/64,1,1);
            D3D12_RESOURCE_BARRIER b;memset(&b,0,sizeof b);b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=p->output;cl->lpVtbl->ResourceBarrier(cl,1,&b);
        }
        pc_barrier(cl,p->output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }else{
        p->final_offset=0;if(!nv)return;
        pc_barrier(cl,p->output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        cl->lpVtbl->CopyBufferRegion(cl,p->output,0,cpu_source,cpu_offset,(uint64_t)nv*16);
        pc_barrier(cl,p->output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
/* Diagnostic only: normal frames never read interpolation back to CPU. */
static inline int pc_record_capture(RsxPoseComputeD3D12* p,ID3D12Device* d,ID3D12GraphicsCommandList* cl,uint32_t nv){
    if(!p->capture&&FAILED(pc_buffer(d,D3D12_HEAP_TYPE_READBACK,p->output_capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,&p->capture)))return 0;
    pc_barrier(cl,p->output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->lpVtbl->CopyBufferRegion(cl,p->capture,0,p->output,p->final_offset,(uint64_t)nv*16);
    pc_barrier(cl,p->output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);return 1;
}
#endif
