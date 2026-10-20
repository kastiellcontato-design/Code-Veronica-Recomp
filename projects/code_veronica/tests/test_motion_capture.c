#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <assert.h>
#include <stdio.h>
#include "rsx_motion_capture.h"
int main(void){
    assert(!mc_wants()&&!mc.fence); /* ordinary play allocates nothing */
    ID3D12Debug* debug=NULL;
    if(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);}
    ID3D12Device* dev=NULL;assert(SUCCEEDED(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&dev)));
    ID3D12CommandQueue* queue=NULL;D3D12_COMMAND_QUEUE_DESC q={0};assert(SUCCEEDED(dev->lpVtbl->CreateCommandQueue(dev,&q,&IID_ID3D12CommandQueue,(void**)&queue)));
    ID3D12CommandAllocator* alloc=NULL;ID3D12GraphicsCommandList* list=NULL;
    assert(SUCCEEDED(dev->lpVtbl->CreateCommandAllocator(dev,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&alloc)));
    assert(SUCCEEDED(dev->lpVtbl->CreateCommandList(dev,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list)));
    assert(SUCCEEDED(list->lpVtbl->Close(list)));
    ID3D12Fence* fence=NULL;assert(SUCCEEDED(dev->lpVtbl->CreateFence(dev,0,0,&IID_ID3D12Fence,(void**)&fence)));HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);
    ID3D12DescriptorHeap* heap=NULL;D3D12_DESCRIPTOR_HEAP_DESC hd={0};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    assert(SUCCEEDED(dev->lpVtbl->CreateDescriptorHeap(dev,&hd,&IID_ID3D12DescriptorHeap,(void**)&heap)));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap,&rtv);
    ID3D12Resource* color=NULL;unsigned width=1920,height=1080,captured=0;
    mc_request();for(unsigned i=0;i<MC_SLOTS;i++)mc.slots[i].state=2;
    assert(!mc_wants()&&mc.remaining==MC_FRAMES&&mc.dropped==1);
    for(unsigned i=0;i<MC_SLOTS;i++)mc.slots[i].state=0;
    for(unsigned i=0;mc.remaining&&i<1000;i++){
        if(!color||captured==12&&width==1920){
            if(color)color->lpVtbl->Release(color);if(captured==12){width=1600;height=900;}
            D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d={0};
            d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            assert(SUCCEEDED(dev->lpVtbl->CreateCommittedResource(dev,&hp,0,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&color)));
            dev->lpVtbl->CreateRenderTargetView(dev,color,NULL,rtv);
        }
        assert(SUCCEEDED(alloc->lpVtbl->Reset(alloc)));assert(SUCCEEDED(list->lpVtbl->Reset(list,alloc,NULL)));
        float clear[4]={.2f,.4f,.6f,1};list->lpVtbl->ClearRenderTargetView(list,rtv,clear,0,NULL);
        if(mc_wants()){
            char json[128];snprintf(json,sizeof json,"{\"sample\":%u,\"width\":%u,\"height\":%u}",captured,width,height);
            unsigned* payload=(unsigned*)malloc(16);for(unsigned j=0;j<4;j++)payload[j]=captured;
            if(mc_record(dev,list,color,width,height,json,payload,16))captured++;else free(payload);
        }
        assert(SUCCEEDED(list->lpVtbl->Close(list)));ID3D12CommandList* cmd=(ID3D12CommandList*)list;queue->lpVtbl->ExecuteCommandLists(queue,1,&cmd);mc_submit(queue);
        assert(SUCCEEDED(queue->lpVtbl->Signal(queue,fence,i+1)));assert(SUCCEEDED(fence->lpVtbl->SetEventOnCompletion(fence,i+1,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        Sleep(2);
    }
    assert(captured==MC_FRAMES);char folder[MAX_PATH];snprintf(folder,sizeof folder,"%s",mc.dir);mc_shutdown();
    for(unsigned i=0;i<captured;i++){
        char path[MAX_PATH+80];snprintf(path,sizeof path,"%s/frame_%03u.bmp",folder,i);FILE* f=fopen(path,"rb");assert(f);
        BITMAPFILEHEADER a;BITMAPINFOHEADER b;assert(fread(&a,sizeof a,1,f)==1&&fread(&b,sizeof b,1,f)==1);assert(a.bfType==0x4d42&&b.biWidth==(i<12?1920:1600)&&b.biHeight==-(i<12?1080:900));
        unsigned char pixel[4];assert(fread(pixel,4,1,f)==1);fclose(f);assert(pixel[0]==153&&pixel[1]==102&&pixel[2]==51&&pixel[3]==255);
        snprintf(path,sizeof path,"%s/frame_%03u.pose",folder,i);f=fopen(path,"rb");assert(f);unsigned data[4];assert(fread(data,sizeof data,1,f)==1);fclose(f);for(unsigned k=0;k<4;k++)assert(data[k]==i);
        snprintf(path,sizeof path,"%s/frame_%03u.json",folder,i);f=fopen(path,"rb");assert(f);char text[128]={0},expected[80];fread(text,1,127,f);fclose(f);snprintf(expected,sizeof expected,"\"sample\":%u,",i);assert(strstr(text,expected));
    }
    ID3D12InfoQueue* info=NULL;unsigned errors=0;if(SUCCEEDED(dev->lpVtbl->QueryInterface(dev,&IID_ID3D12InfoQueue,(void**)&info))){
        for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);i++){SIZE_T n=0;info->lpVtbl->GetMessage(info,i,NULL,&n);D3D12_MESSAGE* m=(D3D12_MESSAGE*)malloc(n);info->lpVtbl->GetMessage(info,i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"D3D12 ERROR %s\n",m->pDescription);errors++;}free(m);}info->lpVtbl->Release(info);
    }assert(!errors);
    color->lpVtbl->Release(color);heap->lpVtbl->Release(heap);fence->lpVtbl->Release(fence);list->lpVtbl->Release(list);alloc->lpVtbl->Release(alloc);queue->lpVtbl->Release(queue);dev->lpVtbl->Release(dev);CloseHandle(event);
    printf("PASS 36 asynchronous motion captures: exact pixels, matched metadata/pose, resize, bounded slots, zero idle allocations, no D3D12 errors\n");return 0;
}
