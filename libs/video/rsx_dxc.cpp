/* Optional, runtime-loaded DXC. The FXC input is never rewritten. Source
 * adjustments here apply only to the experimental DXIL path. Policy changes
 * must bump the cache tag, including changes to compilation arguments. */
#include "rsx_dxc.h"
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Unknwn.h>
#include <ObjIdl.h>
#include <dxcapi.h>
#include <cstdio>
#include <string>

namespace {
INIT_ONCE load_once = INIT_ONCE_STATIC_INIT;
DxcCreateInstanceProc create_instance = nullptr;
char cache_tag[64] = "dxc-exp1-unavailable";

BOOL CALLBACK load_compiler(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof path / sizeof *path));
    if (!n || n >= sizeof path / sizeof *path) return TRUE;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return TRUE;
    *++slash = 0;
    const std::wstring folder(path);
    const std::wstring compiler = folder + L"dxcompiler.dll";
    const std::wstring validator = folder + L"dxil.dll";
    WIN32_FILE_ATTRIBUTE_DATA files[2];
    if (!GetFileAttributesExW(compiler.c_str(), GetFileExInfoStandard, &files[0]) ||
        !GetFileAttributesExW(validator.c_str(), GetFileExInfoStandard, &files[1])) return TRUE;
    /* DLL identity is part of the experimental cache namespace. No DLLs are
     * searched in the working directory or PATH, nor unloaded between jobs. */
    unsigned long long identity = 14695981039346656037ull;
    for (const auto& f : files) {
        const DWORD words[] = {f.nFileSizeHigh, f.nFileSizeLow,
            f.ftLastWriteTime.dwHighDateTime, f.ftLastWriteTime.dwLowDateTime};
        for (DWORD w : words) { identity ^= w; identity *= 1099511628211ull; }
    }
    HMODULE module = LoadLibraryExW(compiler.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return TRUE;
    create_instance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
    if (create_instance)
        std::snprintf(cache_tag, sizeof cache_tag, "dxc-exp1-%016llX%s", identity,
                      std::getenv("RSX_DXC_DEBUG") ? "-dbg" : "");
    return TRUE;
}

struct ThreadCompiler {
    IDxcCompiler3* object = nullptr;
    ~ThreadCompiler() { if (object) object->Release(); }
};

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
        text.replace(at, from.size(), to);
}

std::string prepare_source(const char* src, size_t len) {
    std::string text(src, len);
    /* The RSX translator references these arrays with constant indices. DXC
     * retains whole arrays; the guest root signature only binds units 0..7.
     * Individual declarations allow unused units to be eliminated, just as
     * FXC did. The source passed to FXC is completely untouched. */
    const struct { const char* decl; const char* type; const char* name; char reg; } banks[] = {
        {"Texture2D    rsx_tex[16] : register(t0);", "Texture2D", "rsx_tex", 't'},
        {"SamplerState rsx_samp[16] : register(s0);", "SamplerState", "rsx_samp", 's'}
    };
    for (const auto& bank : banks) {
        if (text.find(bank.decl) == std::string::npos) continue;
        std::string declarations;
        for (int i = 0; i < 16; ++i) {
            const std::string index = std::to_string(i);
            declarations += std::string(bank.type) + " " + bank.name + index +
                " : register(" + bank.reg + index + ");\n";
        }
        replace_all(text, bank.decl, declarations);
        for (int i = 0; i < 16; ++i) {
            const std::string index = std::to_string(i);
            replace_all(text, std::string(bank.name) + "[" + index + "]", bank.name + index);
        }
    }
    /* Conservative arithmetic for multipass comparisons. This is NOT a
     * claim that DXC's shadow stripes are fixed: visual validation remains
     * necessary. Keep precision policy out of the FXC translator. */
    replace_all(text, "float4 r[48]; float4 h[48];", "precise float4 r[48]; precise float4 h[48];");
    replace_all(text, "float4 r[32]; float4 o[16];", "precise float4 r[32]; precise float4 o[16];");
    replace_all(text, "    float4 _p = o[0];", "    precise float4 _p = o[0];");
    return text;
}

void diagnostic(char** error, const char* message, size_t len) {
    if (!error || !message) return;
    *error = (char*)std::malloc(len + 1);
    if (*error) { std::memcpy(*error, message, len); (*error)[len] = 0; }
}
}

extern "C" int rsx_dxc_available(void) {
    InitOnceExecuteOnce(&load_once, load_compiler, nullptr, nullptr);
    return create_instance != nullptr;
}
extern "C" const char* rsx_dxc_cache_tag(void) {
    rsx_dxc_available();
    return cache_tag;
}
extern "C" int rsx_dxc_compile(const char* src, size_t len, const char* profile,
                               void** out, size_t* out_size, char** error) {
    if (out) *out = nullptr;
    if (out_size) *out_size = 0;
    if (error) *error = nullptr;
    if (!out || !out_size || !src || !len || !profile) return -1;
    if (!rsx_dxc_available()) {
        const char* why = "dxcompiler.dll/dxil.dll are unavailable beside the executable";
        diagnostic(error, why, std::strlen(why)); return -2;
    }
    static thread_local ThreadCompiler compiler;
    if (!compiler.object && FAILED(create_instance(CLSID_DxcCompiler,
        __uuidof(IDxcCompiler3), (void**)&compiler.object))) return -3;
    try {
        std::wstring target(profile, profile + std::strlen(profile));
        LPCWSTR args[] = {L"-E", L"main", L"-T", target.c_str(),
            L"-HV", L"2016", L"-Gis", L"-O3", L"-Qstrip_debug", L"-Qstrip_reflect"};
        /* RSX_DXC_DEBUG=1: embed the HLSL and debug info (same -O3 code) so
         * RenderDoc can step a guest shader by its source and variables. */
        LPCWSTR dbg_args[] = {L"-E", L"main", L"-T", target.c_str(),
            L"-HV", L"2016", L"-Gis", L"-O3", L"-Zi", L"-Qembed_debug"};
        const bool dbg = std::getenv("RSX_DXC_DEBUG") != nullptr;
        std::string source = prepare_source(src, len);
        DxcBuffer input = {source.data(), source.size(), DXC_CP_UTF8};
        IDxcResult* result = nullptr;
        HRESULT hr = dbg
            ? compiler.object->Compile(&input, dbg_args, (UINT32)(sizeof dbg_args / sizeof *dbg_args),
                                       nullptr, __uuidof(IDxcResult), (void**)&result)
            : compiler.object->Compile(&input, args, (UINT32)(sizeof args / sizeof *args),
                                       nullptr, __uuidof(IDxcResult), (void**)&result);
        if (FAILED(hr) || !result) { if (result) result->Release(); return -4; }
        IDxcBlobUtf8* messages = nullptr;
        if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), (void**)&messages, nullptr)) && messages)
            diagnostic(error, messages->GetStringPointer(), messages->GetStringLength());
        if (messages) messages->Release();
        HRESULT status = E_FAIL;
        result->GetStatus(&status);
        int rc = -5;
        IDxcBlob* object = nullptr;
        if (SUCCEEDED(status) && SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT,
            __uuidof(IDxcBlob), (void**)&object, nullptr)) && object && object->GetBufferSize()) {
            *out = std::malloc(object->GetBufferSize());
            if (*out) {
                std::memcpy(*out, object->GetBufferPointer(), object->GetBufferSize());
                *out_size = object->GetBufferSize(); rc = 0;
            }
        }
        if (object) object->Release();
        result->Release();
        return rc;
    } catch (...) { return -6; }
}
#else
extern "C" int rsx_dxc_available(void) { return 0; }
extern "C" const char* rsx_dxc_cache_tag(void) { return "dxc-unavailable"; }
extern "C" int rsx_dxc_compile(const char*, size_t, const char*, void** out, size_t* size, char** error) {
    if (out) *out = nullptr; if (size) *size = 0; if (error) *error = nullptr; return -1;
}
#endif
