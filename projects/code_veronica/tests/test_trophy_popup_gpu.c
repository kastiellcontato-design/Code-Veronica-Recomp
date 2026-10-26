#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <assert.h>
#include <stdio.h>
#include "../../../libs/video/rsx_trophy_texture.h"
static void run(IDXGIAdapter* adapter,const char* name){
 ID3D12Device* dev=0;assert(SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&dev)));
 ID3D12InfoQueue* info=0;dev->lpVtbl->QueryInterface(dev,&IID_ID3D12InfoQueue,(void**)&info);
 ID3D12CommandQueue* queue=0;D3D12_COMMAND_QUEUE_DESC qd={0};assert(SUCCEEDED(dev->lpVtbl->CreateCommandQueue(dev,&qd,&IID_ID3D12CommandQueue,(void**)&queue)));
 ID3D12CommandAllocator* allocator=0;ID3D12GraphicsCommandList* list=0;
 assert(SUCCEEDED(dev->lpVtbl->CreateCommandAllocator(dev,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator)));
 assert(SUCCEEDED(dev->lpVtbl->CreateCommandList(dev,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,0,&IID_ID3D12GraphicsCommandList,(void**)&list)));
 list->lpVtbl->Close(list);ID3D12Fence* fence=0;assert(SUCCEEDED(dev->lpVtbl->CreateFence(dev,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence)));HANDLE event=CreateEventW(0,FALSE,FALSE,0);assert(event);
 enum{W=448,H=140,PITCH=1792};uint32_t* pixels=malloc(W*H*4);assert(pixels);
 D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=PITCH*H;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource* readback=0;assert(SUCCEEDED(dev->lpVtbl->CreateCommittedResource(dev,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,0,&IID_ID3D12Resource,(void**)&readback)));
 rsx_trophy_texture slots[2]={{0}};
 for(unsigned frame=0;frame<8;frame++){
  unsigned fi=frame%2;uint64_t gen=frame<4?1:2;uint32_t color=gen==1?0xff563412:0xff214365;
  for(unsigned i=0;i<W*H;i++)pixels[i]=color;
  assert(SUCCEEDED(allocator->lpVtbl->Reset(allocator)));assert(SUCCEEDED(list->lpVtbl->Reset(list,allocator,0)));
  ID3D12Resource* before=slots[fi].texture;assert(rsx_trophy_texture_upload(&slots[fi],dev,list,pixels,W,H,gen));if(before)assert(before==slots[fi].texture);
  D3D12_RESOURCE_BARRIER br={0};br.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;br.Transition.pResource=slots[fi].texture;br.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;br.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;br.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->lpVtbl->ResourceBarrier(list,1,&br);
  D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=slots[fi].texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;dst.PlacedFootprint.Footprint.Width=W;dst.PlacedFootprint.Footprint.Height=H;dst.PlacedFootprint.Footprint.Depth=1;dst.PlacedFootprint.Footprint.RowPitch=PITCH;list->lpVtbl->CopyTextureRegion(list,&dst,0,0,0,&src,0);
  br.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;br.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;list->lpVtbl->ResourceBarrier(list,1,&br);assert(SUCCEEDED(list->lpVtbl->Close(list)));ID3D12CommandList* commands=(ID3D12CommandList*)list;queue->lpVtbl->ExecuteCommandLists(queue,1,&commands);assert(SUCCEEDED(queue->lpVtbl->Signal(queue,fence,frame+1)));assert(SUCCEEDED(fence->lpVtbl->SetEventOnCompletion(fence,frame+1,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
  uint32_t* mapped=0;D3D12_RANGE range={0,PITCH*H};assert(SUCCEEDED(readback->lpVtbl->Map(readback,0,&range,(void**)&mapped)));for(unsigned i=0;i<W*H;i++)assert(mapped[i]==color);readback->lpVtbl->Unmap(readback,0,0);
 }
 if(info){for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);i++){SIZE_T size=0;info->lpVtbl->GetMessage(info,i,0,&size);D3D12_MESSAGE* msg=malloc(size);info->lpVtbl->GetMessage(info,i,msg,&size);if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"%s\n",msg->pDescription);assert(0);}free(msg);}info->lpVtbl->Release(info);}
 for(unsigned i=0;i<2;i++)rsx_trophy_texture_release(&slots[i]);free(pixels);readback->lpVtbl->Release(readback);list->lpVtbl->Release(list);allocator->lpVtbl->Release(allocator);queue->lpVtbl->Release(queue);fence->lpVtbl->Release(fence);CloseHandle(event);dev->lpVtbl->Release(dev);printf("PASS %s: two frame slots; cached generations; texture reuse; GPU RGBA readback; no D3D12 errors\n",name);
}
int main(void){
 ID3D12Debug* debug=0;if(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);}
 IDXGIFactory4* factory=0;assert(SUCCEEDED(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory)));
 IDXGIAdapter* warp=0;assert(SUCCEEDED(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&warp)));run(warp,"WARP test adapter");warp->lpVtbl->Release(warp);
 for(UINT i=0;;i++){IDXGIAdapter1* adapter=0;if(factory->lpVtbl->EnumAdapters1(factory,i,&adapter)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc;adapter->lpVtbl->GetDesc1(adapter,&desc);if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){run((IDXGIAdapter*)adapter,"physical GPU");adapter->lpVtbl->Release(adapter);break;}adapter->lpVtbl->Release(adapter);}
 factory->lpVtbl->Release(factory);return 0;
}
