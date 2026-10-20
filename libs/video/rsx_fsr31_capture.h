/* Opt-in GPU captures of the actual generated output, not just real frames.
 * All copies execute on the SDK list; mapping follows its present fence. */
#pragma once
#include <array>
#include <filesystem>
#include <vector>
namespace FsrCapture {
using Microsoft::WRL::ComPtr;
struct Capture {
 ComPtr<ID3D12Resource> buffer;
 std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,5> layout{};
 UINT64 bytes{};UINT widths[5]{},heights[5]{};
 std::atomic<int> requested{0};std::atomic<bool> ready{false};
 bool recording{};unsigned index{};uint64_t frame{};
} c;
inline D3D12_RESOURCE_STATES state(unsigned f){unsigned r=0;
 if(f&FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)r|=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
 if(f&FFX_API_RESOURCE_STATE_COMPUTE_READ)r|=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
 if(f&FFX_API_RESOURCE_STATE_PIXEL_READ)r|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
 if(f&FFX_API_RESOURCE_STATE_COPY_SRC)r|=D3D12_RESOURCE_STATE_COPY_SOURCE;
 if(f&FFX_API_RESOURCE_STATE_COPY_DEST)r|=D3D12_RESOURCE_STATE_COPY_DEST;
 if(f&FFX_API_RESOURCE_STATE_RENDER_TARGET)r|=D3D12_RESOURCE_STATE_RENDER_TARGET;
 return (D3D12_RESOURCE_STATES)r;
}
inline void transition(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
 if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cl->ResourceBarrier(1,&x);
}
inline void copy(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES before,unsigned i){
 transition(cl,r,before,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=c.buffer.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=c.layout[i];b.pResource=r;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 cl->CopyTextureRegion(&a,0,0,0,&b,nullptr);transition(cl,r,D3D12_RESOURCE_STATE_COPY_SOURCE,before);
}
inline void before(ffxDispatchDescFrameGeneration* p,ID3D12Device* d,ID3D12Resource* original,ID3D12Resource* motion,ID3D12Resource* depth){
 c.recording=false;if(!c.requested.load()||c.ready.load()||!p->numGeneratedFrames)return;
 auto out=(ID3D12Resource*)p->outputs[0].resource;if(!out||out->GetDesc().Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return;
 ID3D12Resource* resources[]={original,out,motion,depth,out};UINT64 end=0;
 for(unsigned i=0;i<5;i++){
  auto desc=resources[i]->GetDesc();c.widths[i]=UINT(desc.Width);c.heights[i]=desc.Height;UINT64 size=0;
  d->GetCopyableFootprints(&desc,0,1,(end+511)&~UINT64(511),&c.layout[i],nullptr,nullptr,&size);end=c.layout[i].Offset+UINT64(c.layout[i].Footprint.RowPitch)*desc.Height;
 }
 if(!c.buffer||end>c.bytes){
  c.buffer.Reset();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC desc{};
  desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=end;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&c.buffer)))){c.requested=0;return;}c.bytes=end;
 }
 auto cl=(ID3D12GraphicsCommandList*)p->commandList;
 copy(cl,original,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,0);copy(cl,out,state(p->outputs[0].state),1);
 copy(cl,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,2);copy(cl,depth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,3);
 c.frame=p->frameID;c.recording=true;
}
inline void after(ffxDispatchDescFrameGeneration* p){
 if(!c.recording)return;copy((ID3D12GraphicsCommandList*)p->commandList,(ID3D12Resource*)p->outputs[0].resource,state(p->outputs[0].state),4);
 c.requested.fetch_sub(1);c.recording=false;c.ready.store(true,std::memory_order_release);
}
inline const std::filesystem::path& root(){static auto p=std::filesystem::path("fsr-captures")/("session_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));return p;}
inline void flush(){
 if(!c.ready.load(std::memory_order_acquire))return;void* mapped=nullptr;D3D12_RANGE read={0,SIZE_T(c.bytes)};
 if(FAILED(c.buffer->Map(0,&read,&mapped))){c.ready=false;c.requested=0;return;}
 std::error_code ec;auto dir=root()/("frame_"+std::to_string(c.index++));std::filesystem::create_directories(dir,ec);
 const char* names[]={"real","generated-raw","motion","depth","generated-filtered"};
 for(unsigned i=0;i<5&&!ec;i++){
  unsigned row=c.widths[i]*4;
  auto at=(const unsigned char*)mapped+c.layout[i].Offset;
  if(i==0||i==1||i==4){
   FILE* f=nullptr;_wfopen_s(&f,(dir/(std::string(names[i])+".bmp")).c_str(),L"wb");if(!f)continue;
   BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+row*c.heights[i];
   info.biSize=sizeof(info);info.biWidth=c.widths[i];info.biHeight=-(LONG)c.heights[i];info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
   fwrite(&file,sizeof(file),1,f);fwrite(&info,sizeof(info),1,f);std::vector<unsigned char> scan(row);
   for(unsigned y=0;y<c.heights[i];y++){
    auto p=at+size_t(y)*c.layout[i].Footprint.RowPitch;for(unsigned x=0;x<c.widths[i];x++){scan[x*4]=p[x*4+2];scan[x*4+1]=p[x*4+1];scan[x*4+2]=p[x*4];scan[x*4+3]=255;}fwrite(scan.data(),row,1,f);
   }fclose(f);
  }else{
   FILE* f=nullptr;_wfopen_s(&f,(dir/(std::string(names[i])+".bin")).c_str(),L"wb");if(f){for(unsigned y=0;y<c.heights[i];y++)fwrite(at+size_t(y)*c.layout[i].Footprint.RowPitch,row,1,f);fclose(f);}
  }
 }
 FILE* metadata=nullptr;_wfopen_s(&metadata,(dir/L"metadata.json").c_str(),L"wb");if(metadata){fprintf(metadata,"{\"frameID\":%llu,\"width\":%u,\"height\":%u,\"motionFormat\":\"RG16_FLOAT\",\"depthFormat\":\"R32_FLOAT\",\"stage\":\"SDK output before port UI composition\"}\n",c.frame,c.widths[0],c.heights[0]);fclose(metadata);}
 D3D12_RANGE no_write={0,0};c.buffer->Unmap(0,&no_write);c.ready.store(false);
 fprintf(stderr,"[FSR-CAPTURE] generated/raw/real/motion/depth -> %s\n",dir.string().c_str());
 if(!c.requested.load()){c.buffer.Reset();c.bytes=0;}
}
inline void request(){c.requested.store(3);}
}
