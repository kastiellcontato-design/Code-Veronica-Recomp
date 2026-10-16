/* Focused integration test. Optional target, built/run by build-release.cmd
 * with CV_DXC_TESTS=1. No game is launched and no game assets are modified. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include "../rsx_dxc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <fstream>
#include <iterator>
#include <string>

static int passed = 0, failed = 0;
static void check(const char* name, bool okay) {
    std::printf("[%s] %s\n", okay ? "PASS" : "FAIL", name);
    okay ? ++passed : ++failed;
}
static bool has_dxil(const void* data, size_t size) {
    if (!data || size < 32 || std::memcmp(data, "DXBC", 4)) return false;
    auto p = (const unsigned char*)data;
    unsigned count; std::memcpy(&count, p + 28, 4);
    if (count > (size - 32) / 4) return false;
    for (unsigned i = 0; i < count; ++i) {
        unsigned offset; std::memcpy(&offset, p + 32 + i * 4, 4);
        if (offset <= size - 8 && !std::memcmp(p + offset, "DXIL", 4)) return true;
    }
    return false;
}
static bool compile(const std::string& source, const char* profile, void** blob, size_t* size) {
    char* error = nullptr;
    int rc = rsx_dxc_compile(source.data(), source.size(), profile, blob, size, &error);
    if (rc) std::printf("DXC diagnostic: %s\n", error ? error : "none");
    std::free(error);
    return !rc && has_dxil(*blob, *size);
}
int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--expect-unavailable")) {
        check("missing local DLLs are detected without searching PATH", !rsx_dxc_available());
        void* blob = (void*)1; size_t size = 1; char* error = nullptr;
        const char source[] = "float4 main():SV_Target{return 1;}";
        int rc = rsx_dxc_compile(source, sizeof source - 1, "ps_6_0", &blob, &size, &error);
        check("missing DLLs return no object and an explanation", rc < 0 && !blob && !size && error && *error);
        std::free(error);
        std::printf("DXC unavailable results: %d passed, %d failed\n", passed, failed);
        return failed ? 1 : 0;
    }
    check("DXC DLL and validator available", rsx_dxc_available() != 0);
    if (!rsx_dxc_available()) return 1;
    check("cache has an experimental DLL-specific identity", std::strstr(rsx_dxc_cache_tag(), "dxc-exp1-") != nullptr);
    const std::string vs = "float4 main(uint id:SV_VertexID):SV_Position {"
        "float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,0,1);}";
    const std::string ps = "Texture2D    rsx_tex[16] : register(t0);\n"
        "SamplerState rsx_samp[16] : register(s0);\n"
        "float4 main():SV_Target{return rsx_tex[0].SampleLevel(rsx_samp[0],float2(0.5,0.5),0);}";
    void* vb = nullptr; size_t vn = 0;
    void* pb = nullptr; size_t pn = 0;
    check("vertex compiles to DXIL", compile(vs, "vs_6_0", &vb, &vn));
    check("16-entry guest banks compile to DXIL", compile(ps, "ps_6_0", &pb, &pn));
    ID3DBlob *fxc = nullptr, *messages = nullptr;
    HRESULT hr = D3DCompile(ps.data(), ps.size(), "fxc_reference", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &fxc, &messages);
    check("original FXC source still compiles as DXBC", SUCCEEDED(hr) && fxc &&
          !has_dxil(fxc->GetBufferPointer(), fxc->GetBufferSize()));
    if (fxc) fxc->Release(); if (messages) messages->Release();
    const char invalid[] = "this is invalid HLSL";
    void* bad = (void*)1; size_t bad_size = 1; char* error = nullptr;
    int rc = rsx_dxc_compile(invalid, sizeof invalid - 1, "ps_6_0", &bad, &bad_size, &error);
    check("invalid HLSL returns diagnostic and no object", rc < 0 && !bad && !bad_size && error && *error);
    std::free(error);

    ID3D12Device* device = nullptr;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    check("D3D12 test device", SUCCEEDED(hr) && device);
    if (device) {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 8;
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1; param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC samplers[8] = {};
        for (int i = 0; i < 8; ++i) {
            auto& s = samplers[i]; s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            s.ShaderRegister = i; s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            s.MaxLOD = D3D12_FLOAT32_MAX; s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        }
        D3D12_ROOT_SIGNATURE_DESC root = {};
        root.NumParameters = 1; root.pParameters = &param;
        root.NumStaticSamplers = 8; root.pStaticSamplers = samplers;
        ID3DBlob *serialized = nullptr, *root_error = nullptr;
        hr = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &root_error);
        ID3D12RootSignature* signature = nullptr;
        if (SUCCEEDED(hr)) hr = device->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&signature));
        check("8-unit guest root signature", SUCCEEDED(hr) && signature);
        if (signature && vb && pb) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature = signature; desc.VS = {vb, vn}; desc.PS = {pb, pn};
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.DepthClipEnable = TRUE;
            desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1; desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            ID3D12PipelineState* pipeline = nullptr;
            hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
            check("DXIL stages fit 8 units (unused bank entries removed)", SUCCEEDED(hr) && pipeline);
            if (FAILED(hr)) std::printf("Pipeline HRESULT: 0x%08lX\n", hr);
            if (pipeline) pipeline->Release();
        }
        if (signature) signature->Release();
        if (serialized) serialized->Release(); if (root_error) root_error->Release();
        device->Release();
    }
    std::free(vb); std::free(pb);
    /* Optionally compile captured HLSL files, without reading the game's
     * executable or running it. Invoke the already built test with filenames. */
    for (int i = 1; i < argc; ++i) {
        std::ifstream stream(argv[i], std::ios::binary);
        std::string source((std::istreambuf_iterator<char>(stream)), {});
        void* blob = nullptr; size_t size = 0;
        check(argv[i], stream.good() && !source.empty() && compile(source, "ps_6_0", &blob, &size));
        std::free(blob);
    }
    std::printf("DXC results: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
