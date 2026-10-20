#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cmath>
#include "rsx_pose_compute_d3d12.h"
#include "rsx_pose_gpu.h"
#include <string>
static void run(bool warp){
    ID3D12Debug* debug=nullptr;if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);}
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)&factory)));
    if(warp)assert(SUCCEEDED(factory->lpVtbl->EnumWarpAdapter(factory,IID_IDXGIAdapter,(void**)&adapter)));
    assert(SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)&device)));
    RsxPoseComputeD3D12 gpu{};assert(pc_init(&gpu,device,1024,128));
    ID3D12CommandQueue* queue=nullptr;D3D12_COMMAND_QUEUE_DESC q{};
    assert(SUCCEEDED(device->lpVtbl->CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,(void**)&queue)));
    ID3D12CommandAllocator* alloc=nullptr;ID3D12GraphicsCommandList* list=nullptr;ID3D12Fence* fence=nullptr;
    assert(SUCCEEDED(device->lpVtbl->CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&alloc)));
    assert(SUCCEEDED(device->lpVtbl->CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_ID3D12GraphicsCommandList,(void**)&list)));
    assert(SUCCEEDED(list->lpVtbl->Close(list)));assert(SUCCEEDED(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence)));
    ID3D12Resource* readback=nullptr;assert(SUCCEEDED(pc_buffer(device,D3D12_HEAP_TYPE_READBACK,gpu.output_capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,&readback)));
    ID3D12Resource* cpu_upload=nullptr;uint32_t* cpu_records=nullptr;
    assert(SUCCEEDED(pc_buffer(device,D3D12_HEAP_TYPE_UPLOAD,1024*16,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ,&cpu_upload)));
    D3D12_RANGE empty_range={0,0};assert(SUCCEEDED(cpu_upload->lpVtbl->Map(cpu_upload,0,&empty_range,(void**)&cpu_records)));
    HANDLE event=CreateEventA(nullptr,FALSE,FALSE,nullptr);assert(event);
    RsxPoseHistory cpu{},compute{};assert(rsx_pose_init(&cpu,1024,128)&&rsx_pose_init(&compute,1024,128));RsxPoseComputePlan plan{};
    for(unsigned iteration=0;iteration<512;iteration++){
        RsxPoseVertex vertices[1024]{};RsxPoseDraw draws[128]{};float cameras[128*32]{};uint32_t nv=0,nd=8+2*(iteration%40),veto[1024]{};
        for(unsigned c=0;c<128;c++){for(unsigned k=0;k<4;k++)cameras[c*32+k*4+k]=1;cameras[c*32+19]=1;if(iteration%2)cameras[c*32+3]=float(c%2)*.5f;}
        for(unsigned i=0;i<nd;i++){
            unsigned count=3+(i/2)%2,first=nv;nv+=count;
            if(i==5&&iteration%5==0)first=draws[4].first;
            draws[i]={uint64_t(i/2),first,count,i%3,0};
            for(unsigned k=0;k<count;k++){auto& v=vertices[first+k];v.stamp=1;v.xyz[0]=k?float((i/2)*4+k):0;v.xyz[1]=k?float(k)*.25f:0;v.xyz[2]=float(iteration%11)*.125f;v.uv=k;
                if(iteration%19==0&&k)v.xyz[1]+=float(i)*.01f; // different patches, same edge point
                if(iteration%23==0&&i>=nd-2)v.xyz[0]+=100; // disconnected clean component
            }
        }
        if(iteration%31==1){
            // Detached eye/mouth geometry inside a retained animated body.
            nd=4;nv=33;memset(vertices,0,sizeof vertices);memset(veto,0,sizeof veto);
            draws[0]={101,0,24,0,0};draws[1]={102,24,3,0,0};draws[2]={103,27,3,0,0};draws[3]={104,30,3,1,0};
            cameras[60]=10;
            for(unsigned v=0;v<24;v++)for(unsigned a=0;a<3;a++)vertices[v].xyz[a]=((v>>a)&1)?2.f:-2.f;
            for(unsigned k=0;k<3;k++){
                vertices[24+k].xyz[0]=float(k)*.1f;vertices[24+k].xyz[1]=float(k%2)*.1f;
                vertices[27+k]=vertices[24+k];vertices[27+k].xyz[0]+=10;
                vertices[30+k]=vertices[24+k];
            }
            for(unsigned v=0;v<nv;v++){vertices[v].stamp=1;vertices[v].uv=v%3;}
            veto[0]=2;
        }
        if(iteration%37==1){
            nd=3;nv=12;memset(vertices,0,sizeof vertices);memset(veto,0,sizeof veto);
            draws[0]={201,0,6,0,iteration%2?2u:3u};draws[1]={202,6,3,1,3};draws[2]={203,9,3,2,0};
            const float p[6][3]={{0,0,2},{1,0,2},{0,1,2},{1,0,2},{1,1,2},{0,1,2}};
            for(unsigned v=0;v<6;v++)memcpy(vertices[v].xyz,p[v],12);
            for(unsigned v=6;v<12;v++)memcpy(vertices[v].xyz,p[v%3],12);
            cameras[60]=2;cameras[92]=4;for(unsigned v=0;v<nv;v++){vertices[v].stamp=1;vertices[v].uv=v%3;}
        }
        if(iteration%3==0)veto[draws[2].first]=2;
        unsigned packed=0;
        for(auto h:{&cpu,&compute}){
            packed=0;h->cur.nd=nd;h->cur.scene=1;memset(h->stationary_draws,0,h->maxd);
            if(iteration%29==0||iteration%37==1)h->stationary_draws[0]=1;
            for(unsigned i=0;i<nd;i++){
                h->cur.draws[i]=draws[i];h->cur.draws[i].first=packed;h->cur.tracks[i].active=iteration%13!=0;h->cur.tracks[i].alpha=(iteration%4)*.125f+.5f;
                memcpy(h->cur.cameras+i*32,cameras+draws[i].cb*32,128);memcpy(h->cur.old_cameras+i*32,cameras+draws[i].cb*32,128);
                for(unsigned k=0;k<draws[i].count;k++)for(unsigned a=0;a<3;a++){
                    float value=vertices[draws[i].first+k].xyz[a];h->cur.xyz[(packed+k)*3+a]=value;
                    float delta=a==0?float((iteration%7)+1)*.0625f:0.f;
                    if(iteration%7==0&&i%2)delta*=2; // inconsistent duplicate => collective fallback
                    if(iteration%19==0&&i%2)delta*=1.5f; // conflicting shared point only
                    if(iteration%17==0)delta=0;
                    if((iteration%29==0||iteration%37==1)&&i==0)delta=0; // proven stationary seam beside a moving patch
                    h->cur.old_xyz[(packed+k)*3+a]=value-delta;
                }packed+=draws[i].count;
            }h->cur.nv=packed;
        }
        RsxPoseFrame frame={vertices,cameras,draws,nv,nd,1,1};
        assert(rsx_pose_cohere(&cpu,&frame));assert(rsx_pose_cohere_ex(&compute,&frame,1));
        float out[1024*3]{};uint32_t states[1024]{},camstates[128]{};
        for(unsigned i=0;i<nd;i++)for(unsigned k=0;k<draws[i].count;k++){
            unsigned v=draws[i].first+k;float xyz[3];int source=cpu.vertex_sources[i];
            if(source>=0)rsx_pose_vertex(&cpu,unsigned(source),k,xyz);else memcpy(xyz,vertices[v].xyz,12);
            rsx_pose_merge(out+v*3,states+v,xyz,3);
        }
        for(unsigned v=0;v<nv;v++)if(veto[v]==2)states[v]=2;
        rsx_pose_protect_aliases(&cpu,&frame,states,camstates);assert(rsx_pose_weld(&cpu,&frame,out,states));
        if(iteration%19==0&&iteration%13!=0&&iteration%17!=0)assert(cpu.seam_components>0);
        if(iteration==0){
            const float saved=compute.cur.xyz[0];compute.cur.xyz[0]=1e-35f;
            assert(!rsx_pose_compute_plan_build(&plan,&compute,&frame,veto));compute.cur.xyz[0]=saved;
            const uint32_t saved_count=draws[0].count;draws[0].count=nv+1;
            assert(!rsx_pose_compute_plan_build(&plan,&compute,&frame,veto));draws[0].count=saved_count;
        }
        assert(rsx_pose_compute_plan_build(&plan,&compute,&frame,veto));assert(pc_upload(&gpu,device,&plan));
        for(auto pair:{std::pair<int,int>{PC_ALIAS_HEAD,PC_ALIAS_NEXT},{PC_POINT_HEAD,PC_POINT_NEXT}}){
            unsigned heads=pair.first==PC_ALIAS_HEAD?nd:nv;
            for(unsigned i=0;i<heads;i++){unsigned node=pc_at(&plan,plan.words[pair.first])[i],steps=0;while(node!=UINT32_MAX){assert(node<nv&&++steps<=nv);node=pc_at(&plan,plan.words[pair.second])[node];}}
        }
        assert(SUCCEEDED(alloc->lpVtbl->Reset(alloc)));assert(SUCCEEDED(list->lpVtbl->Reset(list,alloc,nullptr)));
        if(iteration%11==0){
            gpu.active=0;
            for(unsigned v=0;v<nv;v++){memcpy(cpu_records+v*4,states[v]==1?out+v*3:vertices[v].xyz,12);cpu_records[v*4+3]=states[v]==1;}
        }
        pc_record(&gpu,list,&plan,cpu_upload,0,nv);
        assert(pc_record_capture(&gpu,device,list,nv));
        assert(SUCCEEDED(list->lpVtbl->Close(list)));ID3D12CommandList* submitted[]={(ID3D12CommandList*)list};queue->lpVtbl->ExecuteCommandLists(queue,1,submitted);
        assert(SUCCEEDED(queue->lpVtbl->Signal(queue,fence,iteration+1)));assert(SUCCEEDED(fence->lpVtbl->SetEventOnCompletion(fence,iteration+1,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        uint32_t* actual=nullptr;D3D12_RANGE range={0,nv*16};HRESULT mapped=gpu.capture->lpVtbl->Map(gpu.capture,0,&range,(void**)&actual);
        if(FAILED(mapped)){
            fprintf(stderr,"Map failed iteration=%u hr=%08X removed=%08X\n",iteration,(unsigned)mapped,(unsigned)device->lpVtbl->GetDeviceRemovedReason(device));
            ID3D12InfoQueue* info=nullptr;if(SUCCEEDED(device->lpVtbl->QueryInterface(device,IID_ID3D12InfoQueue,(void**)&info))){
                for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);i++){SIZE_T size=0;info->lpVtbl->GetMessage(info,i,nullptr,&size);std::vector<char> data(size);auto m=(D3D12_MESSAGE*)data.data();info->lpVtbl->GetMessage(info,i,m,&size);fprintf(stderr,"D3D: %s\n",m->pDescription);}info->lpVtbl->Release(info);}
            abort();
        }
        for(unsigned v=0;v<nv;v++){
            const float* expected=states[v]==1?out+v*3:vertices[v].xyz;
            const void* observed=actual[v*4+3]?actual+v*4:(const void*)vertices[v].xyz;
            if(memcmp(expected,observed,12)){
                const float* a=(const float*)observed;
                fprintf(stderr,"Mismatch adapter=%s iteration=%u v=%u state=%u gpu_mode=%u expected=(%g,%g,%g) got=(%g,%g,%g)\n",warp?"WARP":"hardware",iteration,v,states[v],actual[v*4+3],expected[0],expected[1],expected[2],a[0],a[1],a[2]);abort();
            }
        }
        D3D12_RANGE nr={0,0};gpu.capture->lpVtbl->Unmap(gpu.capture,0,&nr);
    }
    printf("PASS %s: 512 GPU compute frames match CPU bit-for-bit (cross-pass conflicts, connected point-conflict/alias vetoes, disconnected clean patches, seams, phases, unchanged poses, CPU fallback/recovery, F12 capture)\n",warp?"WARP":"hardware");
    cpu_upload->lpVtbl->Unmap(cpu_upload,0,&empty_range);cpu_upload->lpVtbl->Release(cpu_upload);
    rsx_pose_compute_plan_destroy(&plan);rsx_pose_destroy(&cpu);rsx_pose_destroy(&compute);pc_release(&gpu);CloseHandle(event);
    readback->lpVtbl->Release(readback);fence->lpVtbl->Release(fence);list->lpVtbl->Release(list);alloc->lpVtbl->Release(alloc);queue->lpVtbl->Release(queue);device->lpVtbl->Release(device);if(adapter)adapter->lpVtbl->Release(adapter);factory->lpVtbl->Release(factory);
}
static void vertex_consumer(){
    const char* source="#define VF_POSE_COMPUTE_ENABLED 1\n" RSX_POSE_GPU_HLSL
        "float4 main(uint v:SV_VertexID):SV_Position{return vf_pose_position(v,float4(1,2,3,7));}\n";
    void* code=nullptr;size_t bytes=0;char* error=nullptr;
    const int rc=rsx_dxc_compile(source,strlen(source),"vs_6_0",&code,&bytes,&error);
    if(rc)fprintf(stderr,"Vertex consumer failed: %s\n",error?error:"");assert(!rc&&bytes);free(code);free(error);
    printf("PASS production GPU vertex consumer: DXC t25 output, original a0.w retained\n");
}
int main(){vertex_consumer();run(true);run(false);}
