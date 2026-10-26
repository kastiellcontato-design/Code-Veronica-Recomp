#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "../../../libs/video/rsx_support_caps.h"
int main(void){IDXGIFactory4* f=NULL;ID3D12Device* d=NULL;
 if(FAILED(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&f)))return 1;
 if(FAILED(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d)))return 2;
 rsx_support_caps(d,f);d->lpVtbl->Release(d);
 IDXGIAdapter* warp=NULL;if(SUCCEEDED(f->lpVtbl->EnumWarpAdapter(f,&IID_IDXGIAdapter,(void**)&warp))&&SUCCEEDED(D3D12CreateDevice((IUnknown*)warp,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d))){rsx_support_caps(d,f);d->lpVtbl->Release(d);}
 if(warp)warp->lpVtbl->Release(warp);f->lpVtbl->Release(f);puts("PASS query-only hardware/WARP capability reports");return 0;}
