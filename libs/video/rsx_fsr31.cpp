#include "rsx_fsr31.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <string>
#include "rsx_dxc.h"
#include "../../third_party/amd_fsr31/api/include/dx12/ffx_api_dx12.h"
#include "../../third_party/amd_fsr31/framegeneration/include/ffx_framegeneration.h"
#include "../../third_party/amd_fsr31/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h"
using Microsoft::WRL::ComPtr;
#include "rsx_fsr31_capture.h"
namespace {
struct State {
 HMODULE dll{};PfnFfxCreateContext create{};PfnFfxDestroyContext destroy{};
 PfnFfxConfigure configure{};PfnFfxQuery query{};PfnFfxDispatch dispatch{};
 ffxContext swap{},fg{};ID3D12Device* device{};void* chain{};
 ComPtr<IDXGIFactory> factory;IDXGISwapChain4* swap_output{};
 ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> shaders[5];
 ComPtr<ID3D12DescriptorHeap> heap;
 ComPtr<ID3D12Resource> luma,previous,motion,depth,hudless,candidates,previous_color,present_color;
 unsigned width{},height{},qw{},qh{},lw{},lh{},inc{},scene{};uint64_t id{},real{},resets{};
 bool failed{},have_previous{},have_depth{},have_color{},reset{},ready{};
 double previous_time{},report_time{};std::atomic<uint64_t> generated{0},generated_total{0};std::atomic<unsigned> callback_error{0};
 /* SDK create-chain descriptions are retained for their documented lifetime. */
 ffxCreateBackendDX12Desc backend{};ffxOverrideVersion version{};
 ffxCreateContextDescFrameGenerationVersion api_version{};
 ffxCreateContextDescFrameGenerationHudless hud_format{};
 ffxCreateContextDescFrameGeneration create_fg{};
 ffxCreateContextDescFrameGenerationSwapChainVersionDX12 swap_version{};
 ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create_swap{};
 DXGI_SWAP_CHAIN_DESC1 swap_desc{};
} s;
double now(){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return double(t.QuadPart)/double(f.QuadPart);}
void barrier(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
 if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cl->ResourceBarrier(1,&x);
}
bool texture(ComPtr<ID3D12Resource>& r,unsigned w,unsigned h,DXGI_FORMAT format,bool uav){
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};
 d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;
 d.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
 return SUCCEEDED(s.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&r)));
}
D3D12_RESOURCE_STATES api_state(uint32_t state){
 unsigned r=0;
 if(state&FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)r|=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
 if(state&FFX_API_RESOURCE_STATE_COMPUTE_READ)r|=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
 if(state&FFX_API_RESOURCE_STATE_PIXEL_READ)r|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
 if(state&FFX_API_RESOURCE_STATE_COPY_SRC)r|=D3D12_RESOURCE_STATE_COPY_SOURCE;
 if(state&FFX_API_RESOURCE_STATE_COPY_DEST)r|=D3D12_RESOURCE_STATE_COPY_DEST;
 if(state&FFX_API_RESOURCE_STATE_RENDER_TARGET)r|=D3D12_RESOURCE_STATE_RENDER_TARGET;
 return (D3D12_RESOURCE_STATES)r;
}
D3D12_CPU_DESCRIPTOR_HANDLE cpu(unsigned i){auto h=s.heap->GetCPUDescriptorHandleForHeapStart();h.ptr+=size_t(i)*s.inc;return h;}
D3D12_GPU_DESCRIPTOR_HANDLE gpu(unsigned i){auto h=s.heap->GetGPUDescriptorHandleForHeapStart();h.ptr+=size_t(i)*s.inc;return h;}
void srv(unsigned i,ID3D12Resource* r,DXGI_FORMAT fmt){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=fmt;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;s.device->CreateShaderResourceView(r,&d,cpu(i));}
void uav(unsigned i,ID3D12Resource* r,DXGI_FORMAT fmt){D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=fmt;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;s.device->CreateUnorderedAccessView(r,nullptr,&d,cpu(i));}
/* Engine-independent backward image motion. The original game exposes no
 * velocity attachment. Estimate current->previous displacement from luma,
 * rather than supplying fabricated all-zero game motion. FSR's own optical
 * flow remains enabled. Only the search luma is quarter-resolution; submitted
 * motion/depth and the guarded SDK output use the full display resolution. */
const char* source=R"(
cbuffer Params:register(b0){uint2 size;uint2 sourceSize;uint reset;};
Texture2D<float4> color:register(t0);Texture2D<float> previous:register(t1);
Texture2D<float> rawDepth:register(t2);Texture2D<float> current:register(t3);
Texture2D<float4> candidates:register(t4);Texture2D<float4> previousColor:register(t5);
Texture2D<float> sceneDepth:register(t6);
Texture2D<float4> presentColor:register(t7);
RWTexture2D<float> luma:register(u0);RWTexture2D<float2> motion:register(u1);
RWTexture2D<float> depth:register(u2);RWTexture2D<float4> proposals:register(u3);
RWTexture2D<float4> generated:register(u4);
SamplerState linearClamp:register(s0);
int2 bound(int2 p){return clamp(p,int2(0,0),int2(size)-1);}
float prev(float2 p){return previous.SampleLevel(linearClamp,(p+.5)/float2(size),0);}
float score(int2 p,float2 d){float e=0;[unroll]for(int y=-2;y<=2;y++)[unroll]for(int x=-2;x<=2;x++){
 int2 k=int2(x,y);float a=current.Load(int3(bound(p+k),0));e+=abs(a-prev(float2(p+k)+d));}return e/25.;}
float light(float3 c){return dot(c,float3(.2126,.7152,.0722));}
[numthreads(8,8,1)]void main(uint3 tid:SV_DispatchThreadID){uint2 p=tid.xy;if(any(p>=size))return;
#if PHASE==0
 // Filter all source texels represented by a luma pixel, rather than aliasing
 // bars, text and grain through a single point sample every four pixels.
 float2 at=(float2(p)+.5)*float2(sourceSize)/float2(size)-.5;float v=0;
 [unroll]for(int y=-1;y<=2;y++)[unroll]for(int x=-1;x<=2;x++){
 int2 q=clamp(int2(at)+int2(x,y),int2(0,0),int2(sourceSize)-1);v+=light(color.Load(int3(q,0)).rgb);}
 luma[p]=v/16.;
#elif PHASE==1
 if(reset){proposals[p]=0;return;}float2 best=0;float baseline=score(p,0),err=baseline;
 [loop]for(int y=-4;y<=4;y+=2)[loop]for(int x=-4;x<=4;x+=2){float2 d=float2(x,y);float e=score(p,d);if(e+.0005*length(d)<err){err=e;best=d;}}
 float2 center=best;[unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){float2 d=center+float2(x,y);float e=score(p,d);if(e+.0005*length(d)<err){err=e;best=d;}}
 // Lucas-Kanade refinement permits fractions of a pixel. Integer-only
 // vectors quantized slow character motion into unrelated four-pixel tiles.
 [unroll]for(int step=0;step<3;step++){
 float a=0,b=0,c=0,dx=0,dy=0;
 [unroll]for(int y=-2;y<=2;y++)[unroll]for(int x=-2;x<=2;x++){
  float2 q=float2(p)+float2(x,y)+best;float gx=(prev(q+float2(1,0))-prev(q-float2(1,0)))*.5;
  float gy=(prev(q+float2(0,1))-prev(q-float2(0,1)))*.5;
  float r=prev(q)-current.Load(int3(bound(int2(p)+int2(x,y)),0));a+=gx*gx;b+=gx*gy;c+=gy*gy;dx+=gx*r;dy+=gy*r;}
 float determinant=a*c-b*b;
 if(determinant>1e-7){float2 d=clamp(float2(c*dx-b*dy,a*dy-b*dx)/determinant,-.5,.5);float2 trial=best-d;float e=score(p,trial);if(e<err){best=trial;err=e;}}
 }
 float confidence=saturate((baseline-err)/max(baseline,.005))*saturate(1-err/.05);
 if(baseline<.002||err>.05||any(abs(best)>5.5))confidence=0;
 proposals[p]=float4(best,confidence,current.Load(int3(p,0)));
#elif PHASE==2
 uint2 q=min(uint2((float2(p)+.5)*float2(sourceSize)/float2(size)),sourceSize-1);depth[p]=saturate(rawDepth.Load(int3(q,0)));
#elif PHASE==3
 float2 q=(float2(p)+.5)*float2(sourceSize)/float2(size)-.5;
 int2 base=int2(floor(q));float sum=0;float2 v=0;float z=sceneDepth.Load(int3(p,0));float l=light(color.Load(int3(p,0)).rgb);
 [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
  int2 k=clamp(base+int2(x,y),int2(0,0),int2(sourceSize)-1);float4 candidate=candidates.Load(int3(k,0));
  int2 loc=clamp(int2((float2(k)+.5)*float2(size)/float2(sourceSize)),int2(0,0),int2(size)-1);
  float dz=abs(sceneDepth.Load(int3(loc,0))-z);
  float w=exp2(-dot(float2(k)-q,float2(k)-q)*1.5)*candidate.z*exp2(-abs(candidate.w-l)*20.);
  // Never mix foreground and background vectors across a depth discontinuity.
  if(dz>max(.00002,abs(z)*.0005))w=0;
  v+=candidate.xy*w;sum+=w;
 }
 motion[p]=sum>.05?v/sum*float2(size)/float2(sourceSize):0;
#else
 float4 a=color.Load(int3(p,0)),b=previousColor.Load(int3(p,0)),g=generated[p];
 // The HUDless snapshot and final real image differ ONLY by port overlays.
 // Preserve their already-composited pixels, including translucent edges,
 // before the static-image guard can erase them on alternating FG outputs.
 float4 ui=presentColor.Load(int3(p,0));
 if(any(ui!=a)){generated[p]=ui;return;}
 float change=max(max(abs(a.r-b.r),abs(a.g-b.g)),abs(a.b-b.b));
 float error=max(max(abs(g.r-a.r),abs(g.g-a.g)),abs(g.b-a.b));
 bool invalid=any(!isfinite(g));
 bool stationary=change<.025&&error>max(.012,change*1.5);
 bool outside=any(g.rgb<min(a.rgb,b.rgb)-.06)||any(g.rgb>max(a.rgb,b.rgb)+.06);
 float low=1,high=0;bool brightStroke=false;
 [unroll]for(int y=-3;y<=3;y+=3)[unroll]for(int x=-3;x<=3;x+=3){
  float3 c=color.Load(int3(bound(int2(p)+int2(x,y)),0)).rgb;float value=light(c);
  low=min(low,value);high=max(high,value);
  brightStroke=brightStroke||value>.65||(c.g>.45&&c.g>c.r*1.3&&c.g>c.b*1.15);
 }
 // Crisp white/green message glyphs must not become optical-flow patches.
 // Conservatively retain a real high-contrast stroke and its immediate edge.
 bool textEdge=brightStroke&&high-low>.25;

 // Fail closed for static structures/text and unsupported disocclusions.
 // This can repeat a real pixel rather than hallucinating a broken edge.
 if(reset||all(a==b)||invalid||stationary||outside||textEdge)generated[p]=a;
#endif
}
)";
bool pipelines(){
 D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,8,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,5,0,0,0};
 D3D12_ROOT_PARAMETER rp[3]{};rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[0].Constants={0,0,5};
 for(int i=0;i<2;i++){rp[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp[i+1].DescriptorTable={1,ranges+i};}
 D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=3;rd.pParameters=rp;D3D12_STATIC_SAMPLER_DESC linear{};linear.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;linear.AddressU=linear.AddressV=linear.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;linear.MaxLOD=D3D12_FLOAT32_MAX;linear.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;linear.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;rd.NumStaticSamplers=1;rd.pStaticSamplers=&linear;ComPtr<ID3DBlob> blob,error;
 if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)))return false;
 if(FAILED(s.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&s.root))))return false;
 for(int i=0;i<5;i++){
  std::string text="#define PHASE "+std::to_string(i)+"\n"+source;void* dxil=nullptr;size_t bytes=0;char* errors=nullptr;
  int rc=rsx_dxc_compile(text.data(),text.size(),"cs_6_2",&dxil,&bytes,&errors);
  if(rc){fprintf(stderr,"[FSR-FG] input shader: %.500s\n",errors?errors:"");free(dxil);free(errors);return false;}
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=s.root.Get();pd.CS={dxil,bytes};auto hr=s.device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&s.shaders[i]));free(dxil);free(errors);if(FAILED(hr))return false;
 }
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=13;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 s.inc=s.device->GetDescriptorHandleIncrementSize(hd.Type);return SUCCEEDED(s.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&s.heap)));
}
void compute(ID3D12GraphicsCommandList* cl,unsigned phase,unsigned sw,unsigned sh){
 ID3D12DescriptorHeap* heap=s.heap.Get();cl->SetDescriptorHeaps(1,&heap);cl->SetComputeRootSignature(s.root.Get());cl->SetPipelineState(s.shaders[phase].Get());
 unsigned w=phase<2?s.lw:s.qw,h=phase<2?s.lh:s.qh;unsigned k[]={w,h,sw,sh,s.reset?1u:0u};cl->SetComputeRoot32BitConstants(0,5,k,0);cl->SetComputeRootDescriptorTable(1,gpu(0));cl->SetComputeRootDescriptorTable(2,gpu(8));cl->Dispatch((w+7)/8,(h+7)/8,1);
}
ffxReturnCode_t generate(ffxDispatchDescFrameGeneration* params,void*){
 auto rc=s.dispatch(&s.fg,&params->header);
 if(!rc&&params->numGeneratedFrames){
  FsrCapture::before(params,s.device,s.hudless.Get(),s.motion.Get(),s.depth.Get());
  auto cl=(ID3D12GraphicsCommandList*)params->commandList;
  auto out=(ID3D12Resource*)params->outputs[0].resource;
  if(out&&out->GetDesc().Format==DXGI_FORMAT_R8G8B8A8_UNORM){
   uav(12,out,DXGI_FORMAT_R8G8B8A8_UNORM);
   // FSR output is restored by dispatch to the API resource state supplied
   // by its swapchain; transition around our final conservative guard.
   auto from=api_state(params->outputs[0].state);
   barrier(cl,out,from,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   D3D12_RESOURCE_BARRIER before{};before.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;before.UAV.pResource=out;cl->ResourceBarrier(1,&before);
   compute(cl,4,s.width,s.height);
   D3D12_RESOURCE_BARRIER fence{};fence.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;fence.UAV.pResource=out;cl->ResourceBarrier(1,&fence);
   barrier(cl,out,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,from);
  }
  FsrCapture::after(params);
  barrier(cl,s.hudless.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(cl,s.previous_color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
  cl->CopyResource(s.previous_color.Get(),s.hudless.Get());barrier(cl,s.previous_color.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(cl,s.hudless.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 }
 if(!rc){s.generated.fetch_add(params->numGeneratedFrames,std::memory_order_relaxed);s.generated_total.fetch_add(params->numGeneratedFrames,std::memory_order_relaxed);}else s.callback_error.store(rc);return rc;
}
void disable(){if(!s.fg)return;ffxConfigureDescFrameGeneration c{};c.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;c.swapChain=s.chain;c.frameID=s.id;s.configure(&s.fg,&c.header);}
void destroy_frame(){
 rsx_fsr31_wait();disable();if(s.fg)s.destroy(&s.fg,nullptr);s.fg=nullptr;
 s.hudless.Reset();s.luma.Reset();s.previous.Reset();s.motion.Reset();s.depth.Reset();s.candidates.Reset();s.previous_color.Reset();s.present_color.Reset();s.ready=false;s.have_previous=false;
}
bool library(){
 if(s.dll)return true;wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return false;
 wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return false;slash[1]=0;
 if(wcslen(path)+wcslen(L"amd_fidelityfx_framegeneration_dx12.dll")>=32768)return false;
 wcscat_s(path,L"amd_fidelityfx_framegeneration_dx12.dll");s.dll=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!s.dll)return false;
 s.create=(PfnFfxCreateContext)GetProcAddress(s.dll,"ffxCreateContext");s.destroy=(PfnFfxDestroyContext)GetProcAddress(s.dll,"ffxDestroyContext");
 s.configure=(PfnFfxConfigure)GetProcAddress(s.dll,"ffxConfigure");s.query=(PfnFfxQuery)GetProcAddress(s.dll,"ffxQuery");s.dispatch=(PfnFfxDispatch)GetProcAddress(s.dll,"ffxDispatch");
 return s.create&&s.destroy&&s.configure&&s.query&&s.dispatch;
}
}
extern "C" int rsx_fsr31_requested(){
 const char *on=getenv("CV_FSR_FRAME_GENERATION"),*fps=getenv("CV_FPS_TEST_60"),*ri=getenv("CV_RENDER_INTERPOLATION"),*raw=getenv("CV_FPS_BENCHMARK_RAW");
 return on&&!strcmp(on,"1")&&fps&&!strcmp(fps,"1")&&ri&&!strcmp(ri,"1")&&!(raw&&!strcmp(raw,"1"));
}
extern "C" int rsx_fsr31_swapchain(void* dev,void* queue,void* factory,void* hwnd,void* desc,void** output){
 if(!rsx_fsr31_requested())return 0;
 s.device=(ID3D12Device*)dev;if(!library()){fprintf(stderr,"[FSR-FG] AMD DLL unavailable; normal presentation retained\n");s.failed=true;return 0;}
 D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_2};
 if(FAILED(s.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof sm))||sm.HighestShaderModel<D3D_SHADER_MODEL_6_2){s.failed=true;fprintf(stderr,"[FSR-FG] shader model 6.2 unavailable; normal presentation retained\n");return 0;}
 s.swap_version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;s.swap_version.version=FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
 s.swap_desc=*(DXGI_SWAP_CHAIN_DESC1*)desc;s.create_swap.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12,&s.swap_version.header};
 s.factory=(IDXGIFactory*)factory;s.create_swap.gameQueue=(ID3D12CommandQueue*)queue;s.create_swap.dxgiFactory=s.factory.Get();s.create_swap.hwnd=(HWND)hwnd;s.create_swap.desc=&s.swap_desc;
 s.create_swap.swapchain=&s.swap_output;auto rc=s.create(&s.swap,&s.create_swap.header,nullptr);IDXGISwapChain4* chain=s.swap_output;
 if(rc||!chain){s.failed=true;fprintf(stderr,"[FSR-FG] swapchain creation failed (%u); normal presentation retained\n",rc);return 0;}
 auto hr=chain->QueryInterface(IID_IDXGISwapChain1,output);s.chain=chain;chain->Release();
 if(FAILED(hr)){s.failed=true;return 0;}fprintf(stderr,"[FSR-FG] official AMD swapchain active; 30 FPS mode excluded\n");return 1;
}
extern "C" void rsx_fsr31_wait(){if(s.swap&&s.dispatch){ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;s.dispatch(&s.swap,&d.header);FsrCapture::flush();}}
extern "C" int rsx_fsr31_begin(void* dev,unsigned w,unsigned h,unsigned scene,int reset){
 if(!s.swap||s.failed||!w||!h)return 0;rsx_fsr31_wait();if(unsigned error=s.callback_error.exchange(0)){fprintf(stderr,"[FSR-FG] generation error=%u; disabled\n",error);disable();s.failed=true;return 0;}s.device=(ID3D12Device*)dev;
 s.reset=reset||s.scene!=scene||!s.have_previous;s.scene=scene;s.have_depth=s.have_color=false;
 if(s.ready&&s.width==w&&s.height==h)return 1;
 destroy_frame();s.width=w;s.height=h;s.qw=w;s.qh=h;s.lw=(w+3)/4;s.lh=(h+3)/4;s.reset=true;
 uint64_t count=32,ids[32]{};const char* names[32]{};ffxQueryDescGetVersions q{};q.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;q.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;q.device=s.device;q.outputCount=&count;q.versionIds=ids;q.versionNames=names;
 auto rc=s.query(nullptr,&q.header);uint64_t chosen=0;for(unsigned i=0;!rc&&i<count&&i<32;i++)if(names[i]&&strstr(names[i],"3.1.6"))chosen=ids[i];
 if(!chosen)goto fail;
 s.backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;s.backend.device=s.device;
 s.version.header={FFX_API_DESC_TYPE_OVERRIDE_VERSION,&s.backend.header};s.version.versionId=chosen;
 s.api_version.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION,&s.version.header};s.api_version.version=FFX_FRAMEGENERATION_VERSION;
 s.hud_format.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS,&s.api_version.header};s.hud_format.hudlessBackBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
 s.create_fg.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&s.hud_format.header};s.create_fg.displaySize={w,h};s.create_fg.maxRenderSize={s.qw,s.qh};s.create_fg.backBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
 rc=s.create(&s.fg,&s.create_fg.header,nullptr);if(rc)goto fail;
 if(!s.root&&!pipelines())goto fail;
 if(!texture(s.hudless,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,false)||!texture(s.luma,s.lw,s.lh,DXGI_FORMAT_R16_FLOAT,true)||!texture(s.previous,s.lw,s.lh,DXGI_FORMAT_R16_FLOAT,false)||!texture(s.motion,s.qw,s.qh,DXGI_FORMAT_R16G16_FLOAT,true)||!texture(s.depth,s.qw,s.qh,DXGI_FORMAT_R32_FLOAT,true)||!texture(s.candidates,s.lw,s.lh,DXGI_FORMAT_R16G16B16A16_FLOAT,true)||!texture(s.previous_color,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,false)||!texture(s.present_color,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,false))goto fail;
 srv(0,s.hudless.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);srv(1,s.previous.Get(),DXGI_FORMAT_R16_FLOAT);srv(3,s.luma.Get(),DXGI_FORMAT_R16_FLOAT);
 srv(4,s.candidates.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);srv(5,s.previous_color.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);srv(6,s.depth.Get(),DXGI_FORMAT_R32_FLOAT);
 srv(7,s.present_color.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
 uav(8,s.luma.Get(),DXGI_FORMAT_R16_FLOAT);uav(9,s.motion.Get(),DXGI_FORMAT_R16G16_FLOAT);uav(10,s.depth.Get(),DXGI_FORMAT_R32_FLOAT);uav(11,s.candidates.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);
 s.ready=true;fprintf(stderr,"[FSR-FG] v2 3.1.6 ready %ux%u; full-resolution motion/depth %ux%u; subpixel filtering and static-edge protection enabled\n",w,h,s.qw,s.qh);return 1;
fail:
 fprintf(stderr,"[FSR-FG] initialization failed (%u); frame generation disabled\n",rc);destroy_frame();s.failed=true;return 0;
}
extern "C" void rsx_fsr31_depth(void* cmd,void* depth,unsigned w,unsigned h){
 if(!s.ready||!depth||!w||!h)return;auto cl=(ID3D12GraphicsCommandList*)cmd;auto r=(ID3D12Resource*)depth;
 auto desc=r->GetDesc();w=std::min(w,unsigned(desc.Width));h=std::min(h,desc.Height);srv(2,r,DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
 barrier(cl,r,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(cl,s.depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 compute(cl,2,w,h);barrier(cl,s.depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(cl,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);s.have_depth=true;
}
extern "C" void rsx_fsr31_hudless(void* cmd,void* color,unsigned state){
 if(!s.ready||!color)return;auto cl=(ID3D12GraphicsCommandList*)cmd;auto r=(ID3D12Resource*)color;auto d=r->GetDesc();
 if(d.Width!=s.width||d.Height!=s.height||d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return;
 barrier(cl,r,(D3D12_RESOURCE_STATES)state,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(cl,s.hudless.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);cl->CopyResource(s.hudless.Get(),r);
 barrier(cl,s.hudless.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(cl,r,D3D12_RESOURCE_STATE_COPY_SOURCE,(D3D12_RESOURCE_STATES)state);s.have_color=true;
}
extern "C" void rsx_fsr31_present_color(void* cmd,void* color,unsigned state){
 if(!s.ready||!color)return;auto cl=(ID3D12GraphicsCommandList*)cmd;auto r=(ID3D12Resource*)color;auto d=r->GetDesc();
 if(d.Width!=s.width||d.Height!=s.height||d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return;
 barrier(cl,r,(D3D12_RESOURCE_STATES)state,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(cl,s.present_color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);cl->CopyResource(s.present_color.Get(),r);
 barrier(cl,s.present_color.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(cl,r,D3D12_RESOURCE_STATE_COPY_SOURCE,(D3D12_RESOURCE_STATES)state);
}
extern "C" void rsx_fsr31_frame(void* cmd,void* chain,int eligible){
 if(!s.fg)return;if(s.failed){disable();return;}auto cl=(ID3D12GraphicsCommandList*)cmd;s.chain=chain;
 bool active=eligible&&s.ready&&s.have_depth&&s.have_color;double t=now();float dt=s.previous_time?float((t-s.previous_time)*1000):16.667f;s.previous_time=t;
 s.reset=s.reset||dt>100||dt<1||!active;
 ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;config.swapChain=chain;config.frameGenerationEnabled=active;config.allowAsyncWorkloads=false;config.frameGenerationCallback=generate;config.frameID=++s.id;
 config.generationRect={0,0,int(s.width),int(s.height)};if(active)config.HUDLessColor=ffxApiGetResourceDX12(s.hudless.Get());
 if(s.configure(&s.fg,&config.header)){s.failed=true;disable();return;}
 if(!active){s.have_previous=false;return;}
 barrier(cl,s.luma.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);compute(cl,0,s.width,s.height);barrier(cl,s.luma.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 barrier(cl,s.candidates.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);compute(cl,1,s.width,s.height);barrier(cl,s.candidates.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 barrier(cl,s.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);compute(cl,3,s.lw,s.lh);barrier(cl,s.motion.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 barrier(cl,s.luma.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(cl,s.previous.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);cl->CopyResource(s.previous.Get(),s.luma.Get());barrier(cl,s.previous.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(cl,s.luma.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 ffxDispatchDescFrameGenerationPrepareV2 p{};p.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;p.frameID=s.id;p.commandList=cl;p.renderSize={s.qw,s.qh};p.motionVectorScale={1,1};p.frameTimeDelta=std::clamp(dt,1.f,100.f);p.reset=s.reset;
 /* Recompiled RSX exposes arbitrary combined clip matrices, not world camera
  * metadata. Analytical 3.1 uses the normalized scene depth; no ML provider
  * or invented world-space camera velocity is selected. */
 p.cameraNear=1.f;p.cameraFar=100000.f;p.cameraFovAngleVertical=1.04719755f;p.viewSpaceToMetersFactor=1.f;p.cameraUp[1]=1;p.cameraRight[0]=1;p.cameraForward[2]=1;
 p.depth=ffxApiGetResourceDX12(s.depth.Get());p.motionVectors=ffxApiGetResourceDX12(s.motion.Get());
 auto rc=s.dispatch(&s.fg,&p.header);if(rc){fprintf(stderr,"[FSR-FG] prepare error=%u; disabled\n",rc);disable();s.failed=true;return;}
 s.have_previous=true;s.real++;s.resets+=s.reset;if(t-s.report_time>=5){fprintf(stderr,"[FSR-FG] real_frames=%llu generated_jobs=%llu resets=%llu (generated jobs are not native FPS)\n",s.real,s.generated.load(),s.resets);s.report_time=t;s.real=s.resets=0;s.generated=0;}
}
extern "C" void rsx_fsr31_shutdown(){
 if(!s.dll)return;destroy_frame();if(s.swap)s.destroy(&s.swap,nullptr);s.swap=nullptr;
 for(auto& p:s.shaders)p.Reset();s.root.Reset();s.heap.Reset();s.factory.Reset();
 /* The host may still hold an IDXGISwapChain proxy whose vtable belongs to
  * this module. Keep the successfully loaded SDK resident until process exit;
  * unloading it here would invalidate the host's final COM Release(). */
}
extern "C" void rsx_fsr31_capture_next(){if(s.ready)FsrCapture::request();}
extern "C" unsigned long long rsx_fsr31_generated_jobs(){return s.generated_total.load();}
extern "C" int rsx_fsr31_present_count(unsigned* count){
 if(!s.swap||s.failed)return 0;
 // The official proxy forwards this call to its REAL DXGI swapchain, whose
 // Presents include generated frames. This does not change UI composition.
 UINT value=0;auto hr=((IDXGISwapChain4*)s.chain)->GetLastPresentCount(&value);
 if(FAILED(hr))return -1;if(count)*count=value;return 1;
}
#else
extern "C" int rsx_fsr31_requested(){return 0;}
extern "C" int rsx_fsr31_swapchain(void*,void*,void*,void*,void*,void**){return 0;}
extern "C" void rsx_fsr31_wait(){}
extern "C" int rsx_fsr31_begin(void*,unsigned,unsigned,unsigned,int){return 0;}
extern "C" void rsx_fsr31_depth(void*,void*,unsigned,unsigned){}
extern "C" void rsx_fsr31_hudless(void*,void*,unsigned){}
extern "C" void rsx_fsr31_present_color(void*,void*,unsigned){}
extern "C" void rsx_fsr31_frame(void*,void*,int){}
extern "C" void rsx_fsr31_shutdown(){}
extern "C" void rsx_fsr31_capture_next(){}
extern "C" unsigned long long rsx_fsr31_generated_jobs(){return 0;}
extern "C" int rsx_fsr31_present_count(unsigned*){return 0;}
#endif
