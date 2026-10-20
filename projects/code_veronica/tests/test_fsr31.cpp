#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "rsx_fsr31.h"
using Microsoft::WRL::ComPtr;
static void transition(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);}
static LONG WINAPI exception(EXCEPTION_POINTERS* e){
 if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
  fprintf(stderr,"AV at %p access=%llu addr=%p\n",e->ExceptionRecord->ExceptionAddress,e->ExceptionRecord->ExceptionInformation[0],(void*)e->ExceptionRecord->ExceptionInformation[1]);
  void* frames[30];unsigned n=CaptureStackBackTrace(0,30,frames,nullptr);for(unsigned i=0;i<n;i++){HMODULE m=nullptr;char name[512]{};GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)frames[i],&m);GetModuleFileNameA(m,name,512);fprintf(stderr,"  %s + %llx\n",name,(unsigned long long)((char*)frames[i]-(char*)m));}
 }return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char** argv){AddVectoredExceptionHandler(1,exception);
 const bool full=argc>1&&!strcmp(argv[1],"--1080");
 _putenv_s("CV_FSR_FRAME_GENERATION","1");_putenv_s("CV_FPS_TEST_60","");assert(!rsx_fsr31_requested());
 unsigned presents=123;assert(rsx_fsr31_present_count(&presents)==0&&presents==123);
 void* empty=nullptr;assert(!rsx_fsr31_swapchain(nullptr,nullptr,nullptr,nullptr,nullptr,&empty));assert(!GetModuleHandleW(L"amd_fidelityfx_framegeneration_dx12.dll"));
 _putenv_s("CV_FPS_TEST_60","1");_putenv_s("CV_RENDER_INTERPOLATION","1");assert(rsx_fsr31_requested());
 _putenv_s("CV_FPS_BENCHMARK_RAW","1");assert(!rsx_fsr31_requested());_putenv_s("CV_FPS_BENCHMARK_RAW","");
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};assert(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
 auto wnd=CreateWindowExW(0,L"STATIC",L"FSR integration test",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(wnd);
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=full?1920:640;desc.Height=full?1080:360;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ComPtr<IDXGISwapChain1> raw;
 if(argc>1&&!strcmp(argv[1],"--missing")){
  assert(!rsx_fsr31_swapchain(device.Get(),queue.Get(),factory.Get(),wnd,&desc,(void**)raw.GetAddressOf()));
  assert(!GetModuleHandleW(L"amd_fidelityfx_framegeneration_dx12.dll"));
  assert(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),wnd,&desc,nullptr,nullptr,&raw)));
  raw.Reset();DestroyWindow(wnd);puts("PASS missing SDK: ordinary DXGI swapchain remains available");return 0;
 }
 assert(rsx_fsr31_swapchain(device.Get(),queue.Get(),factory.Get(),wnd,&desc,(void**)raw.GetAddressOf()));
 ComPtr<IDXGISwapChain3> chain;assert(SUCCEEDED(raw.As(&chain)));raw.Reset();
 ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
 assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
 ComPtr<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))));hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv))));
 ComPtr<ID3D12Resource> depth;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=640;td.Height=360;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depth))));D3D12_DEPTH_STENCIL_VIEW_DESC dd{};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(depth.Get(),&dd,dsv->GetCPUDescriptorHandleForHeapStart());
 const auto inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);unsigned long long value=0;
 unsigned width=desc.Width,height=desc.Height;
 unsigned first_present=0;assert(rsx_fsr31_present_count(&first_present)==1);
 unsigned last_present=first_present;unsigned measured_presents=0;
 unsigned long long last_jobs=0;
 for(unsigned i=0;i<36;i++){
  if(i==24){rsx_fsr31_wait();width=full?1600:800;height=full?900:450;assert(SUCCEEDED(chain->ResizeBuffers(2,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,0)));}
  rsx_fsr31_wait();assert(rsx_fsr31_begin(device.Get(),width,height,i<20?1:2,i==0||i==20));assert(SUCCEEDED(alloc->Reset()));assert(SUCCEEDED(list->Reset(alloc.Get(),nullptr)));
  ComPtr<ID3D12Resource> back;assert(SUCCEEDED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back))));auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(back.Get(),nullptr,handle);
  transition(list.Get(),back.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);float color[]={.03f,.06f,.08f,1};list->ClearRenderTargetView(handle,color,0,nullptr);unsigned offset=i>=8&&i<=12?7*4:i*4;D3D12_RECT rect={int(offset),40,int(offset+80),180};float moving[]={.8f,.3f,.1f,1};list->ClearRenderTargetView(handle,moving,1,&rect);
  if(i>=8){
   float message_white[]={.95f,.95f,.95f,1},message_green[]={.1f,.85f,.1f,1};
   D3D12_RECT stroke={250,200,254,230},stroke2={260,200,264,230};
   list->ClearRenderTargetView(handle,message_white,1,&stroke);list->ClearRenderTargetView(handle,message_green,1,&stroke2);
  }
  list->ClearDepthStencilView(dsv->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,.5f,0,0,nullptr);rsx_fsr31_depth(list.Get(),depth.Get(),640,360);rsx_fsr31_hudless(list.Get(),back.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);if(i==8)rsx_fsr31_capture_next();
  // Port HUD stays in the real frame and is extracted separately by FSR.
  D3D12_RECT hud={500,10,620,35};float white[]={i==9?.4f:1.f,i==10?.6f:1.f,1,1};list->ClearRenderTargetView(handle,white,1,&hud);
  rsx_fsr31_present_color(list.Get(),back.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
  transition(list.Get(),back.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);rsx_fsr31_frame(list.Get(),chain.Get(),i!=15);
  assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);assert(SUCCEEDED(chain->Present(0,0)));assert(SUCCEEDED(queue->Signal(fence.Get(),++value)));assert(SUCCEEDED(fence->SetEventOnCompletion(value,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);Sleep(18);
  rsx_fsr31_wait();unsigned present_now=0;assert(rsx_fsr31_present_count(&present_now)==1);
  const unsigned delta=present_now-last_present;const auto jobs=rsx_fsr31_generated_jobs();
  printf("frame=%u actual_presents=%u generated_jobs=%llu\n",i,delta,jobs-last_jobs);
  assert(delta==1+jobs-last_jobs); // Includes the disabled frame and resize.
  if(i==15)assert(delta==1&&jobs==last_jobs);
  measured_presents+=delta;last_present=present_now;last_jobs=jobs;
 }
 rsx_fsr31_wait();
 unsigned capture_count=0;for(const auto& dir:std::filesystem::directory_iterator("fsr-captures")){
  if(dir.path().filename().string().find("session_"+std::to_string(GetCurrentProcessId())+"_")!=0)continue;
  for(const auto& f:std::filesystem::recursive_directory_iterator(dir.path())){
   if(f.path().filename()!="generated-filtered.bmp")continue;
   auto read=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::vector<char>((std::istreambuf_iterator<char>(in)),{});};
   auto expected=read(f.path().parent_path()/"real.bmp"),actual=read(f.path());
   assert(!expected.empty()&&expected.size()==actual.size());
   // Captures exclude the port HUD in real.bmp; generated output must keep it.
   unsigned w=*(const unsigned*)(expected.data()+18),h=-(int)*(const unsigned*)(expected.data()+22);
   unsigned start=*(const unsigned*)(expected.data()+10);
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<3;c++){
    auto at=start+((size_t)y*w+x)*4+c;
    if(x>=500&&x<620&&y>=10&&y<35){
     unsigned sample=unsigned(std::stoul(f.path().parent_path().filename().string().substr(6)));
     unsigned target=c==2&&sample==1?102:c==1&&sample==2?153:255;
     assert((unsigned char)actual[at]==target);
    }
    else assert(expected[at]==actual[at]);
   }
   capture_count++;
  }
 }
 assert(capture_count==3);printf("PASS 3 generated-frame captures preserve static pixels/text AND changing port HUD in generated frames\n");
 printf("PASS actual DXGI Presents=%u (36 real + %llu generated); paused FG adds only one\n",measured_presents,rsx_fsr31_generated_jobs());
 assert(measured_presents==36+rsx_fsr31_generated_jobs());
 printf("FSR generated jobs=%llu\n",rsx_fsr31_generated_jobs());assert(rsx_fsr31_generated_jobs()>10);
 ComPtr<ID3D12InfoQueue> info;unsigned errors=0;if(SUCCEEDED(device.As(&info))){for(UINT64 i=0;i<info->GetNumStoredMessages();i++){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<char> data(n);auto m=(D3D12_MESSAGE*)data.data();info->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){printf("D3D12 ERROR %s\n",m->pDescription);errors++;}}}assert(!errors);
 rsx_fsr31_shutdown();chain.Reset();DestroyWindow(wnd);CloseHandle(event);puts("PASS FSR 3.1.6: native/RAW gates, real SDK dispatch/present, motion/depth, HUD, reset, scene/resize and disable/re-enable");
}
