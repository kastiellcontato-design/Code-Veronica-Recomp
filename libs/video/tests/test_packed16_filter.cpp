// Execute the production HLSL on WARP and compare against double-precision
// bilinear filtering. Carries and sub-LSB results distinguish the old path.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "rsx_packed16_filter.h"
using Microsoft::WRL::ComPtr;
static bool ok(HRESULT h, const char* operation) {
    if (FAILED(h)) std::printf("FAIL %s: %08lX\n", operation, h);
    return SUCCEEDED(h);
}
int main() {
    const char shader[] = RSX_PACKED16_FILTER_HLSL
        "Texture2D tex:register(t0); RWStructuredBuffer<float2> dst:register(u0);\n"
        "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
        " float2 uv=id.x==64 ? float2(0.5,0.25) : float2(id.x%8,id.x/8)/7.0;\n"
        " dst[id.x]=rsx_filter_pair16(tex,uv); }\n";
    ComPtr<ID3DBlob> code, error;
    HRESULT hr=D3DCompile(shader,sizeof shader-1,"production packed16",nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
    if (!ok(hr,"compile")) { if(error) std::puts((const char*)error->GetBufferPointer());return 1; }
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    if (!ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context),"WARP")) return 1;
    ComPtr<ID3D11ComputeShader> cs;
    if (!ok(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs),"CS"))return 1;
    const uint16_t values[4][2]={{255,32767},{256,32768},{511,65535},{512,0}};
    uint8_t pixels[16];
    for(int i=0;i<4;i++) {pixels[i*4]=(uint8_t)values[i][0];pixels[i*4+1]=(uint8_t)(values[i][1]>>8);
        pixels[i*4+2]=(uint8_t)values[i][1];pixels[i*4+3]=(uint8_t)(values[i][0]>>8);}
    D3D11_TEXTURE2D_DESC td={}; td.Width=td.Height=2;td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
    td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init={pixels,8,16};ComPtr<ID3D11Texture2D> tex;
    if (!ok(device->CreateTexture2D(&td,&init,&tex),"texture"))return 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (!ok(device->CreateShaderResourceView(tex.Get(),nullptr,&srv),"SRV"))return 1;
    D3D11_BUFFER_DESC bd={};bd.ByteWidth=65*2*sizeof(float);bd.Usage=D3D11_USAGE_DEFAULT;
    bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=8;
    ComPtr<ID3D11Buffer> output,readback;
    if (!ok(device->CreateBuffer(&bd,nullptr,&output),"output"))return 1;
    ComPtr<ID3D11UnorderedAccessView> uav;
    if (!ok(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav),"UAV"))return 1;
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if (!ok(device->CreateBuffer(&bd,nullptr,&readback),"readback"))return 1;
    context->CSSetShader(cs.Get(),nullptr,0);ID3D11ShaderResourceView* views[]={srv.Get()};
    ID3D11UnorderedAccessView* targets[]={uav.Get()};
    context->CSSetShaderResources(0,1,views);context->CSSetUnorderedAccessViews(0,1,targets,nullptr);
    context->Dispatch(65,1,1);context->CopyResource(readback.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped={};
    if (!ok(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map"))return 1;
    const float* results=(const float*)mapped.pData;int fails=0;
    for(int n=0;n<65;n++) {
        double u=n==64?.5:double(n%8)/7, v=n==64?.25:double(n/8)/7;
        double x=u*2-.5,y=v*2-.5;int ix=(int)std::floor(x),iy=(int)std::floor(y);
        double fx=x-ix,fy=y-iy;
        auto val=[&](int dx,int dy,int channel) {return double(values[std::clamp(iy+dy,0,1)*2+std::clamp(ix+dx,0,1)][channel]);};
        for(int c=0;c<2;c++) {
            double a=val(0,0,c)*(1-fx)+val(1,0,c)*fx;
            double b=val(0,1,c)*(1-fx)+val(1,1,c)*fx;
            double expected=(a*(1-fy)+b*fy)/65535.0;
            if(std::abs(results[n*2+c]-expected)>2e-7) {
                std::printf("FAIL sample=%d channel=%d got=%.9g expected=%.9g\n",n,c,results[n*2+c],expected);fails++;
            }
        }
    }
    // This half-integer result was rounded to an integer by the old code.
    const double midpoint=255.5/65535.0;
    if(std::abs(results[128]-midpoint)>2e-7)fails++;
    std::printf("%s GPU/WARP packed16: 130 channels, carries, fractions, clamps; midpoint=%.9g\n",fails?"FAIL":"PASS",results[128]);
    context->Unmap(readback.Get(),0);
    return fails?1:0;
}
