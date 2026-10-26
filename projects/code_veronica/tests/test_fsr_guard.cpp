#include "../../../libs/video/rsx_fsr31.cpp"
#include <d3d12sdklayers.h>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);ExitProcess(3);}}while(0)
int main(){
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<ID3D12Device> dev;CHECK(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev))));s.device=dev.Get();
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};CHECK(SUCCEEDED(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
 ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> cl;ComPtr<ID3D12Fence> fence;
 CHECK(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))));
 CHECK(SUCCEEDED(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cl))));cl->Close();
 CHECK(SUCCEEDED(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 s.width=s.qw=64;s.height=s.qh=8;s.lw=16;s.lh=2;CHECK(pipelines());
 ComPtr<ID3D12Resource> generated;
 CHECK(texture(s.hudless,64,8,DXGI_FORMAT_R8G8B8A8_UNORM,false));CHECK(texture(s.previous_color,64,8,DXGI_FORMAT_R8G8B8A8_UNORM,false));
 CHECK(texture(s.present_color,64,8,DXGI_FORMAT_R8G8B8A8_UNORM,false));CHECK(texture(s.motion,64,8,DXGI_FORMAT_R16G16_FLOAT,true));CHECK(texture(generated,64,8,DXGI_FORMAT_R8G8B8A8_UNORM,true));
 srv(0,s.hudless.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);srv(5,s.previous_color.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);srv(7,s.present_color.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);srv(8,s.motion.Get(),DXGI_FORMAT_R16G16_FLOAT);uav(13,generated.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
 auto buffer=[&](D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> r;D3D12_HEAP_PROPERTIES hp{};hp.Type=heap;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=2048;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;CHECK(SUCCEEDED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))));return r;};
 unsigned long long serial=0;
 for(int test=0;test<7;test++){
  CHECK(SUCCEEDED(alloc->Reset()));CHECK(SUCCEEDED(cl->Reset(alloc.Get(),nullptr)));
  std::vector<unsigned char> a(2048),b(2048),g(2048),ui(2048),expected(2048);std::vector<unsigned short> vectors(1024,0);
  for(int y=0;y<8;y++)for(int x=0;x<64;x++){
   auto at=(y*64+x)*4;unsigned char bg=26,value=test==1?102:204;
   unsigned char ca=(x>=24&&x<44)?value:bg,cb=(x>=20&&x<40)?value:bg,cg=(x>=22&&x<42)?value:bg;
   if(test>=2){ca=cb=(x>=24&&x<28)?240:bg;cg=80;}
   for(int c=0;c<3;c++){a[at+c]=ca;b[at+c]=cb;g[at+c]=cg;}a[at+3]=b[at+3]=g[at+3]=255;
   if(test<2||test==6)vectors[(y*64+x)*2]=0xc400; // -4 pixels, IEEE half.
  }
  if(test==3){for(int y=0;y<8;y++)for(int x=24;x<28;x++){auto at=(y*64+x)*4;b[at]=b[at+1]=b[at+2]=26;}}
  if(test==6){for(int y=0;y<8;y++)for(int x=24;x<28;x++){auto at=(y*64+x)*4;b[at]=b[at+1]=b[at+2]=26;}} // Wrong motion alone must not unlock new text.
  ui=a;if(test==4)for(int y=0;y<8;y++)for(int x=2;x<6;x++)for(int c=0;c<3;c++)ui[(y*64+x)*4+c]=153;
  expected=test<2?g:(test==4?ui:a);s.reset=test==5;
  std::vector<ComPtr<ID3D12Resource>> uploads;
  auto upload=[&](ID3D12Resource* dst,const void* data,DXGI_FORMAT fmt){auto up=buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* p;CHECK(SUCCEEDED(up->Map(0,nullptr,&p)));memcpy(p,data,2048);up->Unmap(0,nullptr);
   barrier(cl.Get(),dst,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=up.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint.Footprint={fmt,64,8,1,256};to.pResource=dst;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;cl->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier(cl.Get(),dst,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(up);};
  upload(s.hudless.Get(),a.data(),DXGI_FORMAT_R8G8B8A8_UNORM);upload(s.previous_color.Get(),b.data(),DXGI_FORMAT_R8G8B8A8_UNORM);upload(s.present_color.Get(),ui.data(),DXGI_FORMAT_R8G8B8A8_UNORM);upload(s.motion.Get(),vectors.data(),DXGI_FORMAT_R16G16_FLOAT);upload(generated.Get(),g.data(),DXGI_FORMAT_R8G8B8A8_UNORM);
  barrier(cl.Get(),generated.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);compute(cl.Get(),4,64,8);
  barrier(cl.Get(),generated.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto read=buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=generated.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,64,8,1,256};cl->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier(cl.Get(),generated.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  CHECK(SUCCEEDED(cl->Close()));ID3D12CommandList* lists[]={cl.Get()};queue->ExecuteCommandLists(1,lists);CHECK(SUCCEEDED(queue->Signal(fence.Get(),++serial)));CHECK(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
  void* p;CHECK(SUCCEEDED(read->Map(0,nullptr,&p)));auto actual=(unsigned char*)p;unsigned mismatches=0;for(unsigned i=0;i<2048;i++)if(abs(int(actual[i])-int(expected[i]))>1)mismatches++;read->Unmap(0,nullptr);
  printf("case=%d mismatched channels=%u\n",test,mismatches);CHECK(mismatches==0);
 }
 ComPtr<ID3D12InfoQueue> info;unsigned errors=0;if(SUCCEEDED(dev.As(&info)))for(UINT64 i=0;i<info->GetNumStoredMessages();i++){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<char> data(n);auto m=(D3D12_MESSAGE*)data.data();info->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);errors++;}}
 printf("GPU guard checks: moving bright/dark, static text, appearing text, overlay, reset, untrusted motion; errors=%u debug=%d\n",errors,debug?1:0);CHECK(!errors);CloseHandle(event);
}
