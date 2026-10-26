/* One immutable-size notification texture per fenced frame slot. Resources
 * and descriptors are reused only after the owner has waited for that slot.
 * No queue drain, extra fence or per-frame image upload is required. */
#pragma once
#include <d3d12.h>
#include <stdint.h>
#include <string.h>
typedef struct rsx_trophy_texture {ID3D12Resource* texture;ID3D12Resource* upload;uint64_t generation;UINT width,height;} rsx_trophy_texture;
static void rsx_trophy_texture_release(rsx_trophy_texture* image){
 if(image->texture)image->texture->lpVtbl->Release(image->texture);
 if(image->upload)image->upload->lpVtbl->Release(image->upload);memset(image,0,sizeof(*image));
}
static int rsx_trophy_texture_upload(rsx_trophy_texture* image,ID3D12Device* device,ID3D12GraphicsCommandList* commands,
 const uint32_t* rgba,UINT w,UINT h,uint64_t generation){
 if(!rgba||!generation||!w||!h||w>1024||h>256)return 0;
 if(image->texture&&image->generation==generation)return 1;
 if(image->texture&&(image->width!=w||image->height!=h))rsx_trophy_texture_release(image);
 UINT pitch=(w*4+255)&~255u;
 if(!image->texture){
  D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=(UINT64)pitch*h;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,0,&IID_ID3D12Resource,(void**)&image->upload)))return 0;
  D3D12_HEAP_PROPERTIES th={0};th.Type=D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
  if(FAILED(device->lpVtbl->CreateCommittedResource(device,&th,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,0,&IID_ID3D12Resource,(void**)&image->texture))){rsx_trophy_texture_release(image);return 0;}
  image->width=w;image->height=h;
 }
 void* mapped=0;D3D12_RANGE empty={0,0};
 if(FAILED(image->upload->lpVtbl->Map(image->upload,0,&empty,&mapped))||!mapped)return 0;
 for(UINT y=0;y<h;y++)memcpy((unsigned char*)mapped+(size_t)y*pitch,rgba+(size_t)y*w,(size_t)w*4);
 image->upload->lpVtbl->Unmap(image->upload,0,0);
 D3D12_RESOURCE_BARRIER barrier={0};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=image->texture;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 if(image->generation){barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;commands->lpVtbl->ResourceBarrier(commands,1,&barrier);}
 D3D12_TEXTURE_COPY_LOCATION source={0},target={0};source.pResource=image->upload;source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;source.PlacedFootprint.Footprint.Width=w;source.PlacedFootprint.Footprint.Height=h;source.PlacedFootprint.Footprint.Depth=1;source.PlacedFootprint.Footprint.RowPitch=pitch;
 target.pResource=image->texture;target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;commands->lpVtbl->CopyTextureRegion(commands,&target,0,0,0,&source,0);
 barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;commands->lpVtbl->ResourceBarrier(commands,1,&barrier);image->generation=generation;return 1;
}
