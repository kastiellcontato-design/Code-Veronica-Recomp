#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include "rsx_cube_stride.h"
using Microsoft::WRL::ComPtr;
static void ok(HRESULT x){assert(SUCCEEDED(x));}
int main(){
 assert(rsx_cube_stride(4,0)==4&&rsx_cube_stride(4,1)==128);
 assert(rsx_cube_stride(64*64*4,0)==16384&&rsx_cube_stride(64*64*4,1)==16384);
 assert(rsx_cube_stride(8*8*4+4*4*4+2*2*4+4,1)==384);
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL feature;
 ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&feature,&ctx));
 const char* hlsl="TextureCube<float4> t:register(t0); SamplerState s:register(s0); RWStructuredBuffer<float4> outv:register(u0); [numthreads(6,1,1)]void main(uint3 i:SV_DispatchThreadID){float3 dirs[6]={float3(1,0,0),float3(-1,0,0),float3(0,1,0),float3(0,-1,0),float3(0,0,1),float3(0,0,-1)};outv[i.x]=t.SampleLevel(s,dirs[i.x],0);}";
 ComPtr<ID3DBlob> blob,error;ok(D3DCompile(hlsl,strlen(hlsl),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error));ComPtr<ID3D11ComputeShader> shader;ok(dev->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader));
 unsigned char guest[1024];memset(guest,19,sizeof guest);const unsigned char face[6][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{255,0,255,255},{0,255,255,255}};for(int i=0;i<6;i++)memcpy(guest+i*16,face[i],4);
 assert(rsx_cube_row_bytes(4,16,0)==16); assert(rsx_cube_row_bytes(4,16,1)==4); assert(rsx_cube_row_bytes(256,16,0)==256);
 for(int legacy:{2,1,0}){
  D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=td.MipLevels=1;td.ArraySize=6;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
  D3D11_SUBRESOURCE_DATA data[6]={};for(int i=0;i<6;i++){data[i].pSysMem=guest+i*(legacy==2?128u:legacy==1?4u:rsx_cube_row_bytes(4,16,0));data[i].SysMemPitch=4;}
  ComPtr<ID3D11Texture2D> tex;ok(dev->CreateTexture2D(&td,data,&tex));ComPtr<ID3D11ShaderResourceView> srv;ok(dev->CreateShaderResourceView(tex.Get(),nullptr,&srv));
  D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;ok(dev->CreateSamplerState(&sd,&sampler));
  D3D11_BUFFER_DESC bd={};bd.ByteWidth=6*16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
  ComPtr<ID3D11Buffer> output,staging;ok(dev->CreateBuffer(&bd,nullptr,&output));ComPtr<ID3D11UnorderedAccessView> uav;ok(dev->CreateUnorderedAccessView(output.Get(),nullptr,&uav));
  bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;bd.MiscFlags=0;bd.StructureByteStride=0;ok(dev->CreateBuffer(&bd,nullptr,&staging));
  auto s=shader.Get();ctx->CSSetShader(s,nullptr,0);auto v=srv.Get();ctx->CSSetShaderResources(0,1,&v);auto sp=sampler.Get();ctx->CSSetSamplers(0,1,&sp);auto u=uav.Get();ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);ctx->Dispatch(1,1,1);ctx->CopyResource(staging.Get(),output.Get());
  D3D11_MAPPED_SUBRESOURCE map;ok(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));const float* p=(const float*)map.pData;int correct=0;for(int i=0;i<6;i++){bool match=true;for(int k=0;k<4;k++)if(abs(p[i*4+k]-face[i][k]/255.f)>1e-6f)match=false;correct+=match;}ctx->Unmap(staging.Get(),0);assert(correct==(legacy?1:6));printf("PASS WARP cube sampling: legacy_stride=%d correct faces=%d/6\n",legacy,correct);
  v=nullptr;ctx->CSSetShaderResources(0,1,&v);u=nullptr;ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
 }
 puts("PASS: pitched linear faces sampled correctly; packed and 128-byte strides reproduce failure.");
}
