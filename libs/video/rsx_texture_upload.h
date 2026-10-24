/* Shared production upload, also exercised on D3D12 WARP by offline tests. */
#pragma once
#include "rsx_texture_pack.h"
#include <d3d12.h>
#include <string.h>
static ID3D12Resource* texpack_gpu_upload(ID3D12Device* device,ID3D12GraphicsCommandList* cmd,const texpack_image* im,ID3D12Resource** staging)
{
    if(!device||!cmd||!im||!im->data||!im->levels||im->levels>16||!staging)return NULL;
    *staging=NULL;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = im->w; td.Height = im->h; td.DepthOrArraySize = 1;
    td.MipLevels = (UINT16)im->levels; td.Format = (DXGI_FORMAT)im->format;
    td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Resource* res = NULL; ID3D12Resource* up = NULL;
    if (FAILED(device->lpVtbl->CreateCommittedResource(device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&res)))
        return NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[16]; UINT rows[16]; UINT64 rowsz[16], total = 0;
    device->lpVtbl->GetCopyableFootprints(device, &td, 0, im->levels, 0, fp, rows, rowsz, &total);
    D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uint8_t* m = NULL; D3D12_RANGE nr = {0, 0};
    if (FAILED(device->lpVtbl->CreateCommittedResource(device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&up)) ||
        FAILED(up->lpVtbl->Map(up, 0, &nr, (void**)&m)) || !m) {
        if (up) up->lpVtbl->Release(up);
        res->lpVtbl->Release(res);
        return NULL;
    }
    for (uint32_t l = 0; l < im->levels; l++) {
        const size_t row_bytes=im->row_bytes[l];
        if(!row_bytes||row_bytes!=rowsz[l]||im->rows[l]!=rows[l]||im->level_off[l]>im->bytes||
            (size_t)rows[l]>(im->bytes-im->level_off[l])/row_bytes) {
            up->lpVtbl->Unmap(up,0,NULL);up->lpVtbl->Release(up);res->lpVtbl->Release(res);return NULL;
        }
        for (uint32_t y = 0; y < rows[l]; y++)
            memcpy(m + fp[l].Offset + (uint64_t)y * fp[l].Footprint.RowPitch,
                   im->data + im->level_off[l] + (size_t)y * row_bytes, row_bytes);
    }
    up->lpVtbl->Unmap(up, 0, NULL);
    for (uint32_t l = 0; l < im->levels; l++) {
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        dst.pResource = res; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = l;
        src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp[l];
        cmd->lpVtbl->CopyTextureRegion(cmd, &dst, 0, 0, 0, &src, NULL);
    }
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->lpVtbl->ResourceBarrier(cmd, 1, &b);
    *staging=up;   /* caller releases after its fence completes */
    return res;
}
