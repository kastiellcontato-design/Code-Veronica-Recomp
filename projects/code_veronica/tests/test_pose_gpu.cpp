#include "rsx_vertex_fetch.h"
#include "rsx_pose_interpolation.h"
#include "rsx_pose_gpu.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <memory>
#include <chrono>
using Microsoft::WRL::ComPtr;
extern "C" {
u8* vm_base;
u32 cellGcmResolveLocated(int local,u32 offset) {
    static const u32 pages[4]={4,6,5,7};
    return local?offset:pages[(offset>>20)&3]*0x100000+(offset&0xfffff);
}
u32 cellGcmResolveOffset(u32 offset){return cellGcmResolveLocated(0,offset);}
}
static void be32(u8* p,u32 value){for(int k=0;k<4;k++)p[k]=(u8)(value>>(24-k*8));}
static void prepare(rsx_state& st,int type,int size,bool main,unsigned freq,bool modulo) {
    memset(&st,0,sizeof st);
    for(int i=0;i<16;i++) {
        auto& a=st.vertex_attribs[i];a.enabled=1;a.type=type;a.size=size;a.stride=16;a.frequency=freq;
        a.offset=(main?0x80000000u:0u)+(0x100000-512+i*4096);
        if(modulo)st.frequency_divider_op|=1u<<i;
        for(int k=0;k<4;k++)st.vertex_data4f[i][k]=float(i*4+k)*.125f;
        for(unsigned v=0;v<100;v++) {
            u32 off=(a.offset&0x7fffffffu)+v*16;u8* p=vm_base+cellGcmResolveLocated(main?0:1,off);
            for(unsigned k=0;k<16;k++)p[k]=(u8)(17+v+k+i);
            if(type==2)for(unsigned k=0;k<4;k++){float f=float(v*4+k+i)*.03125f;u32 b;memcpy(&b,&f,4);be32(p+k*4,b);}
            if(type==3)for(unsigned k=0;k<4;k++){u16 h=(u16)(0x3800+v+k);p[k*2]=(u8)(h>>8);p[k*2+1]=(u8)h;}
        }
    }
    st.vertex_attribs[7].enabled=0;st.vertex_attribs[11].stride=0;
}
static void selected_fetch() {
    auto st=std::make_unique<rsx_state>();unsigned cases=0;
    for(int type:{1,2,3,4,5,6,7})for(int size:{1,2,3,4})for(bool main:{false,true})
    for(unsigned freq:{0u,2u,3u})for(bool mod:{false,true}) {
        prepare(*st,type,size,main,freq,mod);rsx_fetch_plan full,part;
        rsx_fetch_plan_build(st.get(),&full);rsx_fetch_plan_build(st.get(),&part);
        for(unsigned vi:{0u,1u,2u,5u,31u,33u,80u}) {
            float a[16][4],b[16][4],reference[4];memset(b,0x5a,sizeof b);
            rsx_fetch_vertex_planned(&full,vi,a);rsx_fetch_vertex_selected(&part,vi,RSX_POSE_ATTRIBUTE_MASK,b);
            for(unsigned attr=0;attr<16;attr++) {
                rsx_fetch_attrib(st.get(),(int)attr,vi,reference);
                assert(!memcmp(a[attr],reference,sizeof reference));
                if(RSX_POSE_ATTRIBUTE_MASK&(1u<<attr))assert(!memcmp(a[attr],b[attr],sizeof reference));
                else for(unsigned char c:reinterpret_cast<unsigned char(&)[16]>(b[attr]))assert(c==0x5a);
            }
            RsxPoseVertex x{},y{};rsx_pose_record_vertex(&x,a,7);rsx_pose_record_vertex(&y,b,7);assert(!memcmp(&x,&y,sizeof x));cases++;
        }
    }
    std::printf("PASS selective fetch: %u cases, all formats, divisors, constants, non-contiguous IO pages and identical pose hashes\n",cases);
}
static bool ok(HRESULT h,const char* what){if(FAILED(h))std::printf("FAIL %s: %08lX\n",what,h);return SUCCEEDED(h);}
static bool gpu_override() {
    const char shader[]="ByteAddressBuffer vf_buf:register(t0);\n#define VF_POSE_OFF_U 64u\n"
        RSX_POSE_GPU_HLSL
        "RWStructuredBuffer<float4> dst:register(u0);\n"
        "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){uint index=(id.x+17u)%1536u;dst[id.x]=vf_pose_position(index,float4(index,-2,4,3.75));}\n";
    ComPtr<ID3DBlob> code,error;
    if(!ok(D3DCompile(shader,sizeof shader-1,"production pose override",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error),"compile")){
        if(error)std::puts((const char*)error->GetBufferPointer());return false;
    }
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
    if(!ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx),"WARP"))return false;
    ComPtr<ID3D11ComputeShader> cs;if(!ok(dev->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs),"CS"))return false;
    constexpr unsigned count=1536,bank=64+count*RSX_POSE_GPU_STRIDE;std::vector<u8> bytes(bank*2,0xa5);
    std::vector<float> expected(2*count*4);
    for(unsigned parity=0;parity<2;parity++)for(unsigned i=0;i<count;i++){
        void* r=bytes.data()+parity*bank+64+i*RSX_POSE_GPU_STRIDE;rsx_pose_gpu_reset(r);
        float* e=expected.data()+(parity*count+i)*4;
        e[0]=float(i);e[1]=-2;e[2]=4;e[3]=3.75f;
        if((i+parity)%4==0){float xyz[3]={float(i)+.5f,float(parity),-float(i)};rsx_pose_gpu_write(r,xyz);memcpy(e,xyz,12);}
        if((i+parity)%4==1) {
            float old_xyz[3],current_xyz[3];
            const float alphas[]={.5f,.5001f,.625f,.75f,.99999994f,1.f};float alpha=alphas[i%6];
            for(unsigned k=0;k<3;k++) {
                old_xyz[k]=(float(int((i*1664525u+k*1013904223u)%200001u)-100000))*.00390625f;
                current_xyz[k]=old_xyz[k]+float(int((i*1237u+k*1931u)%257u)-128)*.03125f;
                e[k]=old_xyz[k]+(current_xyz[k]-old_xyz[k])*alpha;
            }
            if(i<32)for(unsigned k=0;k<3;k++) {
                old_xyz[k]=(i&(1u<<k))?-0.f:0.f;current_xyz[k]=(i&(1u<<(k+1)))?-0.f:0.f;
                e[k]=old_xyz[k]+(current_xyz[k]-old_xyz[k])*alpha;
            }
            assert(rsx_pose_gpu_write_blend(r,old_xyz,current_xyz,alpha));
            float cpu[3];assert(rsx_pose_gpu_read(r,cpu)&&!memcmp(cpu,e,12));
        }
        if(i==count-1){float xyz[3]={1,2,3};rsx_pose_gpu_write(r,xyz);rsx_pose_gpu_reset(r);e[0]=float(i);e[1]=-2;e[2]=4;}

    }
    {
        uint8_t record[RSX_POSE_GPU_STRIDE],before[RSX_POSE_GPU_STRIDE];memset(record,0xa5,sizeof record);memcpy(before,record,sizeof record);
        float a[3]={1,2,3},b[3]={2,3,4};
        assert(!rsx_pose_gpu_write_blend(record,a,b,NAN));
        assert(!rsx_pose_gpu_write_blend(record,a,b,.49f));
        b[0]=1e-30f;assert(!rsx_pose_gpu_write_blend(record,a,b,.5f));
        b[0]=INFINITY;assert(!rsx_pose_gpu_write_blend(record,a,b,.5f));
        assert(!memcmp(record,before,sizeof record));
    }
    for(unsigned p=0;p<2;p++)for(unsigned k=0;k<64;k++)assert(bytes[p*bank+k]==0xa5); // metadata untouched
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=unsigned(bytes.size());bd.Usage=D3D11_USAGE_IMMUTABLE;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA init{bytes.data(),0,0};ComPtr<ID3D11Buffer> input;
    if(!ok(dev->CreateBuffer(&bd,&init,&input),"input"))return false;
    bd={};bd.ByteWidth=count*16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
    ComPtr<ID3D11Buffer> output,readback;if(!ok(dev->CreateBuffer(&bd,nullptr,&output),"output"))return false;
    ComPtr<ID3D11UnorderedAccessView> uav;if(!ok(dev->CreateUnorderedAccessView(output.Get(),nullptr,&uav),"UAV"))return false;
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(!ok(dev->CreateBuffer(&bd,nullptr,&readback),"readback"))return false;
    for(unsigned parity=0;parity<2;parity++){
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
        sd.BufferEx.FirstElement=parity*bank/4;sd.BufferEx.NumElements=bank/4;sd.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
        ComPtr<ID3D11ShaderResourceView> srv;if(!ok(dev->CreateShaderResourceView(input.Get(),&sd,&srv),"SRV"))return false;
        ID3D11ShaderResourceView* views[]={srv.Get()};ID3D11UnorderedAccessView* outs[]={uav.Get()};
        ctx->CSSetShader(cs.Get(),nullptr,0);ctx->CSSetShaderResources(0,1,views);ctx->CSSetUnorderedAccessViews(0,1,outs,nullptr);
        ctx->Dispatch(count,1,1);ctx->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        if(!ok(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map"))return false;
        for(unsigned thread=0;thread<count;thread++){
            unsigned i=(thread+17)%count;
            assert(!memcmp((const float*)mapped.pData+thread*4,expected.data()+(parity*count+i)*4,4*sizeof(float)));
        }
        ctx->Unmap(readback.Get(),0);
    }
    std::puts("PASS GPU/WARP: production CPU override/GPU precise interpolation bit-exact for 3072 positions; indexed draws, parity banks, reset and original a0.w/metadata");return true;
}
static void benchmark() {
    auto st=std::make_unique<rsx_state>();prepare(*st,2,3,false,0,false);
    for(int i=0;i<16;i++)if(i!=0&&i!=8&&i!=14&&i!=15)st->vertex_attribs[i].enabled=0;
    constexpr unsigned count=100000;std::vector<float> upload(count*64);std::vector<RsxPoseVertex> poses(count);
    double elapsed[2]{};volatile float checksum=0;
    for(int mode=0;mode<2;mode++) {
        rsx_fetch_plan plan;rsx_fetch_plan_build(st.get(),&plan);float attrs[16][4]{};
        auto begin=std::chrono::steady_clock::now();
        for(unsigned i=0;i<count;i++){
            if(!mode){rsx_fetch_vertex_planned(&plan,i%100,attrs);memcpy(upload.data()+i*64,attrs,sizeof attrs);}
            else rsx_fetch_vertex_selected(&plan,i%100,RSX_POSE_ATTRIBUTE_MASK,attrs);
            rsx_pose_record_vertex(&poses[i],attrs,1);
        }
        elapsed[mode]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        for(unsigned i=0;i<count;i++)checksum=checksum+poses[i].xyz[0];
        if(!mode)for(float f:upload)checksum=checksum+f;
    }
    std::printf("CPU synthetic conversion, 100000 vertices: full16+upload %.3f ms, selected3 %.3f ms (%.2fx); not whole-game FPS\n",elapsed[0],elapsed[1],elapsed[0]/elapsed[1]);
    assert(checksum!=0);
}
int main(){std::vector<u8> memory(8*0x100000);vm_base=memory.data();selected_fetch();if(!gpu_override())return 1;benchmark();}
