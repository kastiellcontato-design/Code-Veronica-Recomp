#define COBJMACROS
#include "rsx_texture_upload.h"
ID3D12Resource* texpack_gpu_test_upload(ID3D12Device* d,ID3D12GraphicsCommandList* c,const texpack_image* i,ID3D12Resource** u){return texpack_gpu_upload(d,c,i,u);}
