#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include "rsx_texture_pack.h"
#include "rsx_texture_dds.h"
#include "../../../libs/codec/stb_image_write.h"
extern "C" ID3D12Resource* texpack_gpu_test_upload(ID3D12Device*,ID3D12GraphicsCommandList*,const texpack_image*,ID3D12Resource**);
#include <filesystem>
#include <vector>
#include <fstream>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>
namespace fs=std::filesystem;
static void ok(HRESULT h){assert(SUCCEEDED(h));}
static std::vector<uint8_t> dds(uint32_t format,uint32_t levels=1){
    std::vector<uint8_t> b(128);assert(texpack_dds_header(b.data(),4,4,levels,format));
    texpack_image im{};im.w=im.h=4;im.levels=levels;im.format=format;tdds_layout(&im);b.resize(128+im.bytes);return b;
}
static void save(const fs::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());assert(f.good());}
static std::vector<uint8_t> load(const fs::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);assert(f);size_t n=(size_t)f.tellg();std::vector<uint8_t>b(n);f.seekg(0);f.read((char*)b.data(),n);assert(f.good());return b;}
static void wait_file(const fs::path& p){for(int i=0;i<500&&!fs::exists(p);i++)Sleep(10);assert(fs::exists(p));}
static void gpu_sample(texpack_image* im,float alpha){
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    ok(CreateDXGIFactory2(0,IID_IDXGIFactory4,(void**)&factory));ok(factory->EnumWarpAdapter(IID_IDXGIAdapter,(void**)&adapter));
    ok(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)&device));
    ID3D12CommandQueue* queue=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ok(device->CreateCommandQueue(&qd,IID_ID3D12CommandQueue,(void**)&queue));
    ID3D12CommandAllocator* alloc=nullptr;ID3D12GraphicsCommandList* cmd=nullptr;
    ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&alloc));
    ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_ID3D12GraphicsCommandList,(void**)&cmd));
    ID3D12Resource* upload=nullptr;auto* texture=texpack_gpu_test_upload(device,cmd,im,&upload);assert(texture&&upload);
    D3D12_RESOURCE_DESC desc{};desc=texture->GetDesc();assert(desc.Format==im->format&&desc.MipLevels==im->levels);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *out=nullptr,*readback=nullptr;
    ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,(void**)&out));
    heap.Type=D3D12_HEAP_TYPE_READBACK;buffer.Flags=D3D12_RESOURCE_FLAG_NONE;
    ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&readback));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* dh=nullptr;ok(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)&dh));
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};cpu=dh->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=(DXGI_FORMAT)im->format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=texpack_alpha_mapping(D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,im->opaque);srv.Texture2D.MipLevels=im->levels;
    device->CreateShaderResourceView(texture,&srv,cpu);cpu.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=16;uav.Buffer.StructureByteStride=16;
    device->CreateUnorderedAccessView(out,nullptr,&uav,cpu);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    for(auto& range:ranges){range.NumDescriptors=1;range.OffsetInDescriptorsFromTableStart=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;}
    D3D12_ROOT_PARAMETER param{};param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;param.DescriptorTable.NumDescriptorRanges=2;param.DescriptorTable.pDescriptorRanges=ranges;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&param;ID3DBlob *signature=nullptr,*errors=nullptr,*shader=nullptr;
    ok(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors));ID3D12RootSignature* rs=nullptr;
    ok(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_ID3D12RootSignature,(void**)&rs));
    const char* hlsl="Texture2D<float4>T:register(t0); RWStructuredBuffer<float4>O:register(u0); [numthreads(4,4,1)] void main(uint3 p:SV_DispatchThreadID){O[p.y*4+p.x]=T.Load(int3(p.xy,0));}";
    ok(D3DCompile(hlsl,strlen(hlsl),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&shader,&errors));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=rs;pd.CS.pShaderBytecode=shader->GetBufferPointer();pd.CS.BytecodeLength=shader->GetBufferSize();
    ID3D12PipelineState* pso=nullptr;ok(device->CreateComputePipelineState(&pd,IID_ID3D12PipelineState,(void**)&pso));
    cmd->SetPipelineState(pso);cmd->SetComputeRootSignature(rs);cmd->SetDescriptorHeaps(1,&dh);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};gpu=dh->GetGPUDescriptorHandleForHeapStart();cmd->SetComputeRootDescriptorTable(0,gpu);cmd->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=out;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd->ResourceBarrier(1,&barrier);cmd->CopyResource(readback,out);ok(cmd->Close());
    auto* list=(ID3D12CommandList*)cmd;queue->ExecuteCommandLists(1,&list);ID3D12Fence* fence=nullptr;ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);ok(queue->Signal(fence,1));ok(fence->SetEventOnCompletion(1,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
    float* pixels=nullptr;D3D12_RANGE range{0,256};ok(readback->Map(0,&range,(void**)&pixels));
    for(int i=0;i<16;i++)assert(std::abs(pixels[i*4+3]-alpha)<.006f);
    readback->Unmap(0,nullptr);
    fence->Release();pso->Release();rs->Release();shader->Release();signature->Release();dh->Release();
    out->Release();readback->Release();texture->Release();upload->Release();cmd->Release();alloc->Release();queue->Release();device->Release();adapter->Release();factory->Release();
}
int main(){
    for(uint32_t f:{28u,71u,74u,77u}){
        auto b=dds(f,3);for(size_t i=128;i<b.size();i++)b[i]=(uint8_t)i;
        auto* im=texpack_dds_parse(b.data(),b.size());assert(im&&im->levels==3&&im->bytes==b.size()-128&&im->format==f);assert(!memcmp(im->data,b.data()+128,im->bytes));texpack_free(im);
        for(size_t n=0;n<b.size();n++)assert(!texpack_dds_parse(b.data(),n));
    }
    auto b=dds(28);for(int i=0;i<16;i++){b[128+i*4]=30;b[129+i*4]=50;b[130+i*4]=70;b[131+i*4]=128;}
    auto* im=texpack_dds_parse(b.data(),b.size());assert(im&&im->data[3]==128);gpu_sample(im,128.f/255);texpack_free(im);
    for(uint32_t f:{71u,74u,77u}){
        auto v=dds(f);uint8_t* p=v.data()+128;
        if(f==71){p[2]=p[3]=255;memset(p+4,255,4);} // transparent index 3
        else if(f==74){memset(p,0x88,8);p[9]=0xf8;}
        else {p[0]=128;p[1]=64;p[9]=0xf8;}
        im=texpack_dds_parse(v.data(),v.size());assert(im);gpu_sample(im,f==71?0:f==74?136.f/255:128.f/255);texpack_free(im);
    }
    // BC7 mode 6: two identical endpoints with soft alpha, zero indices.
    std::vector<uint8_t> dx(164);auto legacy=dds(77);memcpy(dx.data(),legacy.data(),128);tdds_put(dx.data()+84,0x30315844);tdds_put(dx.data()+128,98);tdds_put(dx.data()+132,3);tdds_put(dx.data()+140,1);tdds_put(dx.data()+144,1);
    unsigned bit=0;auto put=[&](unsigned value,unsigned count){for(unsigned i=0;i<count;i++,bit++)dx[148+bit/8]|=((value>>i)&1)<<(bit%8);};put(64,7);for(int i=0;i<6;i++)put(0,7);put(64,7);put(64,7);put(0,2);put(0,3);for(int i=0;i<15;i++)put(0,4);assert(bit==128);
    im=texpack_dds_parse(dx.data(),dx.size());assert(im&&im->format==98&&im->bytes==16);gpu_sample(im,128.f/255);texpack_free(im);
    for(auto offset:{132u,140u}){auto bad=dx;tdds_put(bad.data()+offset,2);assert(!texpack_dds_parse(bad.data(),bad.size()));}
    auto bad=dx;tdds_put(bad.data()+144,2);assert(!texpack_dds_parse(bad.data(),bad.size()));bad=dx;tdds_put(bad.data()+128,999);assert(!texpack_dds_parse(bad.data(),bad.size()));
    for(auto pair:{std::pair{16u,0u},std::pair{12u,0xffffffffu},std::pair{28u,17u},std::pair{112u,0x200u}}){auto v=dds(77);tdds_put(v.data()+pair.first,pair.second);assert(!texpack_dds_parse(v.data(),v.size()));}
    // Small BC1 falls back to RGBA without losing cutout alpha.
    auto tiny=dds(71);tdds_put(tiny.data()+12,1);tdds_put(tiny.data()+16,1);tiny[130]=tiny[131]=255;memset(tiny.data()+132,255,4);
    im=texpack_dds_parse(tiny.data(),tiny.size());assert(im&&im->format==28&&im->bytes==4&&im->data[3]==0);texpack_free(im);
    auto bgra=dds(28);tdds_put(bgra.data()+92,0xff0000);tdds_put(bgra.data()+100,0xff);bgra[128]=70;bgra[129]=50;bgra[130]=30;bgra[131]=128;
    im=texpack_dds_parse(bgra.data(),bgra.size());assert(im&&im->data[0]==30&&im->data[1]==50&&im->data[2]==70&&im->data[3]==128);texpack_free(im);
    auto srgb=dx;tdds_put(srgb.data()+128,99);im=texpack_dds_parse(srgb.data(),srgb.size());assert(im&&im->format==99);gpu_sample(im,128.f/255);texpack_free(im);
    auto opaque=dx;tdds_put(opaque.data()+144,3);im=texpack_dds_parse(opaque.data(),opaque.size());assert(im&&im->opaque);gpu_sample(im,1.f);texpack_free(im);
    // Match index defaults and writer output without a game process.
    const auto root=fs::current_path()/('d'+std::to_string(GetCurrentProcessId()));fs::create_directories(root/L"mods");fs::create_directories(root/L"dump");
    auto env=[](const char* key,const fs::path& p){auto s=p.u8string();assert(!_putenv_s(key,(const char*)s.c_str()));};env("TEXTURE_DIR",root/L"mods");env("TEXTURE_DUMP_DIR",root/L"dump");
    save(root/L"mods"/L"123456789ABCDEF0_A.dds",dds(77));save(root/L"mods"/L"123456789ABCDEF0_A.png",{0});
    uint8_t png_pixels[64];for(int p=0;p<16;p++){png_pixels[p*4]=17;png_pixels[p*4+1]=30;png_pixels[p*4+2]=80;png_pixels[p*4+3]=128;}
    std::vector<uint8_t> png_bytes;
    auto png_sink=[](void* context,void* data,int n){auto& bytes=*(std::vector<uint8_t>*)context;bytes.insert(bytes.end(),(uint8_t*)data,(uint8_t*)data+n);};
    assert(stbi_write_png_to_func(png_sink,&png_bytes,4,4,4,png_pixels,16));save(root/L"mods"/L"3456789ABCDEF012_RGBA.png",png_bytes);
    for(int tries=0;texpack_lookup(0x123456789abcdef0)<0;tries++){assert(tries<5000);Sleep(1);}texpack_request(0x123456789abcdef0);
    uint64_t hash=0;int result=0;for(int i=0;i<500&&(result=texpack_next(&hash,&im))==0;i++)Sleep(10);
    assert(result==1&&hash==0x123456789abcdef0&&im->format==77);texpack_free(im);
    assert(texpack_lookup(0x3456789abcdef012)==1);texpack_request(0x3456789abcdef012);result=0;
    for(int i=0;i<500&&(result=texpack_next(&hash,&im))==0;i++)Sleep(10);
    assert(result==1&&hash==0x3456789abcdef012&&im->format==28&&im->levels==3&&im->data[3]==128);gpu_sample(im,128.f/255);texpack_free(im);
    texpack_set_dump_format(1);assert(texpack_dump_wanted(0x23456789abcdef01));auto* rgba=(uint8_t*)calloc(1,64);rgba[3]=128;texpack_dump(0x23456789abcdef01,4,4,"RGBA",rgba);
    const auto file=root/L"dump"/L"23456789ABCDEF01_4x4_RGBA.dds";wait_file(file);auto saved=load(file);im=texpack_dds_parse(saved.data(),saved.size());assert(im&&im->data[3]==128);texpack_free(im);
    auto generation=texpack_dump_generation();texpack_set_dump_format(0);assert(generation!=texpack_dump_generation()&&texpack_dump_wanted(0x23456789abcdef01));
    std::puts("PASS DDS: native BC1/2/3/7 and RGBA GPU alpha on WARP, mip layout, truncation, invalid headers/shapes/alpha, duplicate precedence, async live DDS export and format switching.");
}
