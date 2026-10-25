#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "rsx_clear_rect.h"
using Microsoft::WRL::ComPtr;
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static D3D12_RECT rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned tw,unsigned th,float scale){RsxClearRect a;assert(rsx_clear_rect(&a,x,y,w,h,tw,th,scale));return {a.left,a.top,a.right,a.bottom};}
int main(){
 RsxClearRect a;
 assert(!rsx_clear_rect(&a,0,0,0,240,768,512,1));
 assert(!rsx_clear_rect(&a,768,0,1,1,768,512,1));
 assert(rsx_clear_rect(&a,760,500,0xffffffffu,0xffffffffu,768,512,3)&&a.right==2304&&a.bottom==1536);
 assert(rsx_clear_rect(&a,0,0,4096,4096,1280,720,1.5f)&&a.right==1920&&a.bottom==1080);
 ComPtr<ID3D12Debug> dbg;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))dbg->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory;ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> warp;ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
 ComPtr<ID3D12Device> dev;ok(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q={};ok(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
 ComPtr<ID3D12Fence> fence;ok(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 tick=0;
 const float colors[6][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1},{1,0,1,1},{0,1,1,1}};
 for(int scale:{1,3})for(int legacy:{1,0}){
  UINT width=768*scale,height=512*scale;
  ComPtr<ID3D12CommandAllocator> alloc;ok(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
  ComPtr<ID3D12GraphicsCommandList> cl;ok(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cl)));
  D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ComPtr<ID3D12Resource> tex;ok(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&tex)));
  D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> heap;ok(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto rtv=heap->GetCPUDescriptorHandleForHeapStart();dev->CreateRenderTargetView(tex.Get(),nullptr,rtv);
  const float white[4]={1,1,1,1};cl->ClearRenderTargetView(rtv,white,0,nullptr);
  for(unsigned i=0;i<6;i++){
   auto face=rect((i%3)*256,(i/3)*256,256,256,768,512,float(scale));
   // Legacy behavior clears all six faces before drawing this face.
   cl->ClearRenderTargetView(rtv,white,legacy?0:1,legacy?nullptr:&face);
   auto inner=rect((i%3)*256+8,(i/3)*256+8,240,240,768,512,float(scale));cl->ClearRenderTargetView(rtv,colors[i],1,&inner);
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;UINT rows;UINT64 bytes,rowbytes;dev->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowbytes,&bytes);
  hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> readback;ok(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
  D3D12_RESOURCE_BARRIER barrier={};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=tex.Get();barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;cl->ResourceBarrier(1,&barrier);
  D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=tex.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  ok(cl->Close());ID3D12CommandList* list[]={cl.Get()};queue->ExecuteCommandLists(1,list);ok(queue->Signal(fence.Get(),++tick));ok(fence->SetEventOnCompletion(tick,event));assert(WaitForSingleObject(event,30000)==WAIT_OBJECT_0);
  unsigned char* p;D3D12_RANGE range={0,SIZE_T(bytes)};ok(readback->Map(0,&range,(void**)&p));unsigned preserved=0;
  for(unsigned i=0;i<6;i++){
   unsigned x=((i%3)*256+128)*scale,y=((i/3)*256+128)*scale;auto pixel=p+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;bool match=true;
   for(int k=0;k<4;k++)if(pixel[k]!=(unsigned char)(colors[i][k]*255))match=false;
   preserved+=match;
  }
  assert(preserved==unsigned(legacy?1:6));D3D12_RANGE written={0,0};readback->Unmap(0,&written);
  printf("PASS D3D12 WARP: scale=%d legacy=%d surviving faces=%u/6\n",scale,legacy,preserved);
 }
 ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(dev.As(&info))){for(UINT64 i=0;i<info->GetNumStoredMessages();i++){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<unsigned char> b(n);auto m=(D3D12_MESSAGE*)b.data();info->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);assert(false);}}}
 CloseHandle(event);puts("PASS: no D3D12 errors; clipping, empty regions, overflow and default full clears verified.");
}
