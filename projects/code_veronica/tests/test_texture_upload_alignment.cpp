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
#include "rsx_texture_upload_layout.h"
using Microsoft::WRL::ComPtr;
static void ok(HRESULT h){if(FAILED(h)){printf("FAILED HRESULT %08X\n",unsigned(h));abort();}}
static void no_errors(ID3D12InfoQueue* iq){
 for(UINT64 i=0;i<iq->GetNumStoredMessages();i++){
  SIZE_T n=0;ok(iq->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);
  auto m=(D3D12_MESSAGE*)b.data();ok(iq->GetMessage(i,m,&n));
  if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);abort();}
 }
 iq->ClearStoredMessages();
}
static D3D12_RESOURCE_DESC buffer_desc(UINT64 size){
 D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=size;b.Height=1;b.DepthOrArraySize=1;b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return b;
}
static unsigned char pattern(unsigned face,unsigned row,UINT64 byte){return (unsigned char)(17+face*37+row*13+byte*3);}
int main(int argc,char**){
 ComPtr<ID3D12Debug> dbg;ok(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)));dbg->EnableDebugLayer();
 ComPtr<IDXGIFactory4> fac;ok(CreateDXGIFactory1(IID_PPV_ARGS(&fac)));ComPtr<IDXGIAdapter> warp;
 if(argc>1){
  ComPtr<IDXGIAdapter1> adapter;ok(fac->EnumAdapters1(0,&adapter));DXGI_ADAPTER_DESC1 desc;ok(adapter->GetDesc1(&desc));
  assert(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE));printf("Hardware: %ls\n",desc.Description);ok(adapter.As(&warp));
 }else ok(fac->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
 ComPtr<ID3D12Device> dev;ok(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
 ComPtr<ID3D12InfoQueue> iq;ok(dev.As(&iq));
 D3D12_FEATURE_DATA_D3D12_OPTIONS13 caps={};HRESULT ch=dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13,&caps,sizeof caps);
 printf("%s OPTIONS13 unrestricted copies: hr=%08X supported=%d. Strict 512-byte contract checked separately.\n",argc>1?"Hardware":"WARP",unsigned(ch),caps.UnrestrictedBufferTextureCopyPitchSupported);
 ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd={};ok(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
 ComPtr<ID3D12Fence> fence;ok(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 tick=0;
 unsigned cases=0,legacy_invalid=0;
 for(DXGI_FORMAT fmt:{DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC3_UNORM})
 for(unsigned size:{1u,2u,3u,4u,5u,12u,64u})for(unsigned faces:{1u,6u}){
  bool bc=fmt==DXGI_FORMAT_BC1_UNORM||fmt==DXGI_FORMAT_BC3_UNORM;
  if(bc&&size%4)continue;
  D3D12_RESOURCE_DESC td={};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=td.Height=size;td.DepthOrArraySize=(UINT16)faces;td.MipLevels=1;td.Format=fmt;td.SampleDesc.Count=1;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[6];UINT rows[6];UINT64 bytes[6],total=0;
  dev->GetCopyableFootprints(&td,0,faces,0,fp,rows,bytes,&total);
  UINT pitch=fp[0].Footprint.RowPitch;UINT64 stride=rsx_texture_upload_face_stride(pitch,rows[0],faces==6);
  for(unsigned f=0;f<faces;f++){
   // This is required even on local hardware whose OPTIONS13 happens to relax it.
   assert(f*stride%D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT==0);
   assert(f*stride==fp[f].Offset);
   assert(f*stride+(rows[f]-1)*pitch+bytes[f]<=stride*faces);
  }
  if(faces==6&&(UINT64(pitch)*rows[0])%512){legacy_invalid++;assert(stride!=UINT64(pitch)*rows[0]);}
  else assert(stride==UINT64(pitch)*rows[0]);
  D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> tex;ok(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&tex)));
  hp.Type=D3D12_HEAP_TYPE_UPLOAD;auto bd=buffer_desc(stride*faces);ComPtr<ID3D12Resource> up;ok(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&up)));
  unsigned char* mapped;D3D12_RANGE empty={0,0};ok(up->Map(0,&empty,(void**)&mapped));memset(mapped,0xCD,size_t(bd.Width));
  for(unsigned f=0;f<faces;f++)for(unsigned y=0;y<rows[f];y++)for(UINT64 x=0;x<bytes[f];x++)mapped[f*stride+y*pitch+x]=pattern(f,y,x);
  up->Unmap(0,nullptr);
  hp.Type=D3D12_HEAP_TYPE_READBACK;bd=buffer_desc(total);ComPtr<ID3D12Resource> rb;ok(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
  ComPtr<ID3D12CommandAllocator> alloc;ok(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));ComPtr<ID3D12GraphicsCommandList> cl;ok(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cl)));
  for(unsigned f=0;f<faces;f++){
   D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=up.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp[f];src.PlacedFootprint.Offset=f*stride;
   D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=tex.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.SubresourceIndex=f;cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  }
  D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=tex.Get();b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;cl->ResourceBarrier(1,&b);
  for(unsigned f=0;f<faces;f++){
   D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=tex.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=f;
   D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp[f];cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  }
  ok(cl->Close());ID3D12CommandList* lists[]={cl.Get()};q->ExecuteCommandLists(1,lists);ok(q->Signal(fence.Get(),++tick));ok(fence->SetEventOnCompletion(tick,event));assert(WaitForSingleObject(event,30000)==WAIT_OBJECT_0);ok(dev->GetDeviceRemovedReason());
  D3D12_RANGE range={0,SIZE_T(total)};ok(rb->Map(0,&range,(void**)&mapped));
  for(unsigned f=0;f<faces;f++)for(unsigned y=0;y<rows[f];y++)for(UINT64 x=0;x<bytes[f];x++)assert(mapped[fp[f].Offset+y*pitch+x]==pattern(f,y,x));
  rb->Unmap(0,&empty);no_errors(iq.Get());cases++;
 }
 assert(legacy_invalid>0);
 assert(rsx_texture_upload_face_stride(256,1,1)==512);
 assert(rsx_texture_upload_face_stride(256,1,0)==256);
 assert(rsx_texture_upload_face_stride(256,64,1)==16384);
 assert(rsx_texture_upload_face_stride(256,0xffffffffu,1)==1099511627776ULL);
 printf("PASS %u D3D12 %s uploads/readbacks, all bytes preserved, no validation errors.\n",cases,argc>1?"hardware":"WARP");
 printf("PASS %u cube layouts violate the strict contract in legacy code and conform after padding.\n",legacy_invalid);
 puts("PASS 1x1 lighter LUT formats, 64x64 LUT, odd heights, BC blocks, floats, 2D unchanged, 64-bit size arithmetic.");CloseHandle(event);
}
