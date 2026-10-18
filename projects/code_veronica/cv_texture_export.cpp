/* Offline texture extraction. One bounded background worker; no game state,
 * map loading, environment variables or renderer queues are touched. */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "cv_texture_export.h"
#include "../../libs/filesystem/edat.h"
#include "../../libs/video/rsx_texture_pack.h"
#include "../../libs/video/rsx_texture_layout.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace {
namespace fs=std::filesystem;
constexpr size_t MAX_FILE=256u<<20,MAX_TEXTURE=128u<<20;
struct Worker {
    std::mutex mutex;
    std::thread thread;
    std::atomic<bool> stop{false};
    CvTextureExportStatus status;
    ~Worker(){stop=true;if(thread.joinable())thread.join();}
} worker;
template<class F> void update(F fn){std::lock_guard<std::mutex> lock(worker.mutex);fn(worker.status);}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
std::vector<uint8_t> read(const fs::path& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);const auto n=f.tellg();
    if(!f||n<=0||uint64_t(n)>MAX_FILE)return {};
    std::vector<uint8_t> b((size_t)n);f.seekg(0);if(!f.read((char*)b.data(),n))return {};return b;
}
bool png(const fs::path& path,uint32_t w,uint32_t h,const std::vector<uint8_t>& rgba,const CLSID& codec){
    using namespace Gdiplus;
    Bitmap bitmap(w,h,PixelFormat32bppARGB);Rect rect(0,0,w,h);BitmapData bits{};
    if(bitmap.LockBits(&rect,ImageLockModeWrite,PixelFormat32bppARGB,&bits)!=Ok)return false;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
        auto* d=(uint8_t*)bits.Scan0+(ptrdiff_t)y*bits.Stride+x*4;const auto* s=rgba.data()+((size_t)y*w+x)*4;
        d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=s[3];
    }
    bitmap.UnlockBits(&bits);
    // Unique temporary output. Never replace an existing user's PNG, even if
    // it appears after the worker's initial existence check.
    fs::path tmp=path;tmp+=L".export-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    if(bitmap.Save(tmp.c_str(),&codec,nullptr)!=Ok){DeleteFileW(tmp.c_str());return false;}
    if(MoveFileExW(tmp.c_str(),path.c_str(),0))return true;
    const bool exists=GetLastError()==ERROR_ALREADY_EXISTS||GetLastError()==ERROR_FILE_EXISTS;
    DeleteFileW(tmp.c_str());return exists;
}
struct Exporter {
    fs::path output;
    CLSID codec{};
    std::unordered_set<uint64_t> seen;
    std::ofstream report;
    void texture(const std::vector<uint8_t>& b,const fs::path& archive,const std::string& asset,bool reference=false){
        if(worker.stop||b.size()<20||memcmp(b.data(),"\0XET",4))return;
        const uint32_t dims=be32(b.data()+8),w=(dims>>6)&8191,h=(dims>>19)&8191,mips=dims&63;
        const uint32_t descriptor=be32(b.data()+12),format=(descriptor>>8)&255;
        if((descriptor&255)==6&&!reference){
            // Cube resources carry 27 SH coefficients before their offset table.
            // Export all faces for reference. The current mod renderer replaces
            // only 2D textures, so these stay in a clearly separate subfolder.
            const size_t table=16+27*4,header=table+(size_t)mips*6*4;
            const size_t bytes=format==0x28?(size_t)w*h*4:((size_t)(w+3)/4)*((h+3)/4)*(format==0x14||format==0x1e?8:16);
            if(!w||!h||!mips||header>b.size()||!bytes||bytes>MAX_TEXTURE){update([](auto& s){++s.errors;});return;}
            for(uint32_t face=0;face<6&&!worker.stop;++face){
                const uint32_t offset=be32(b.data()+table+face*mips*4);
                if(offset<header||offset>b.size()||bytes>b.size()-offset){update([](auto& s){++s.errors;});continue;}
                std::vector<uint8_t> proxy(20+bytes);memcpy(proxy.data(),b.data(),16);
                auto store=[&](size_t pos,uint32_t value){for(int i=3;i>=0;--i){proxy[pos+i]=(uint8_t)value;value>>=8;}};
                store(8,(dims&~63u)|1);store(12,(descriptor&~255u)|1);store(16,20);
                memcpy(proxy.data()+20,b.data()+offset,bytes);
                texture(proxy,archive,asset+"#cube-face-"+std::to_string(face),true);
            }
            return;
        }
        const uint32_t start=be32(b.data()+16);
        int bc=0;uint32_t rsx=0;const char* name=nullptr;
        if(format==0x14||format==0x1e){bc=1;rsx=0x86;name="DXT1";}
        else if(format==0x16){bc=2;rsx=0x87;name="DXT3";}
        else if(format==0x18){bc=3;rsx=0x88;name="DXT5";}
        else if(format==0x28){rsx=0x85;name="ARGB";}
        else {update([](auto& s){++s.unsupported;});return;}
        const size_t n=bc?((size_t)(w+3)/4)*((h+3)/4)*(bc==1?8:16):(size_t)w*h*4;
        if(!w||!h||w>8192||h>8192||!mips||start<16+4*mips||start>b.size()||n>b.size()-start||n>MAX_TEXTURE){update([](auto& s){++s.errors;});return;}
        const uint8_t* data=b.data()+start;
        const auto hash=texpack_hash(data,n,w,h,rsx);
        char leaf[128];snprintf(leaf,sizeof leaf,"%016llX_%ux%u_%s.png",(unsigned long long)hash,w,h,name);
        // Record duplicates too: one shared PNG can belong to several maps.
        auto quote=[](std::string v){std::string q="\"";for(char c:v){if(c=='\"')q+='\"';q+=c;}return q+'\"';};
        const std::string relative=reference?std::string("reference/cubemaps/")+leaf:leaf;
        const auto u8=archive.u8string();report<<quote(std::string(u8.begin(),u8.end()))<<','<<quote(asset)<<','<<relative<<'\n';
        if(!seen.insert(hash^(reference?0xFEDCBA9876543210ull:0)).second){update([](auto& s){++s.existing;});return;}
        const auto target=output/fs::u8path(relative);std::error_code ec;
        if(reference){fs::create_directories(target.parent_path(),ec);if(ec){update([](auto& s){++s.errors;});return;}}
        if(fs::exists(target,ec)){update([](auto& s){++s.existing;});return;}
        std::vector<uint8_t> rgba((size_t)w*h*4);
        if(bc)texpack_decode_bc(bc,data,((size_t)(w+3)/4)*(bc==1?8:16),w,h,rgba.data());
        else {
            // Match the live renderer, including Morton order in POT ARGB art.
            rsx_tex_layout layout{};rsx_texture_layout(0x85,w,h,&layout);
            rsx_texture_decode(rgba.data(),w*4,data,w,h,&layout,0);
        }
        if(worker.stop)return;
        if(png(target,w,h,rgba,codec))update([](auto& s){++s.written;});else update([](auto& s){++s.errors;});
    }
    void file(const fs::path& path){
        auto b=read(path);if(b.empty()){update([](auto& s){++s.errors;});return;}
        if(b.size()>16&&!memcmp(b.data(),"\0SFH",4)){
            std::vector<uint8_t> raw;raw.reserve(b.size());
            for(size_t k=16;k<b.size();k+=0x20000){size_t n=std::min<size_t>(0x1fff0,b.size()-k);raw.insert(raw.end(),b.begin()+k,b.begin()+k+n);}
            b=std::move(raw);
        }
        if(b.size()>=4&&!memcmp(b.data(),"\0XET",4)){texture(b,path,"");return;}
        if(b.size()>=4&&!memcmp(b.data(),"\0SFX",4))return; // streaming material, no embedded texture image
        if(b.size()<8||memcmp(b.data(),"\0CRA",4)){update([](auto& s){++s.unsupported;});return;}
        const uint32_t count=uint32_t(b[6])<<8|b[7];
        if(count>(b.size()-8)/80){update([](auto& s){++s.errors;});return;}
        for(uint32_t i=0;i<count&&!worker.stop;++i){
            const auto* e=b.data()+8+i*80;
            if(be32(e+64)!=0x241f5deb)continue; // MT Framework texture resource
            const uint32_t compressed=be32(e+68),size=be32(e+72)>>3,offset=be32(e+76);
            if(!size||size>MAX_TEXTURE||offset>b.size()||compressed>b.size()-offset){update([](auto& s){++s.errors;});continue;}
            std::vector<uint8_t> tex(size);
            if(compressed==size)memcpy(tex.data(),b.data()+offset,size);
            else {
                // The ARC size is an allocation bound; room textures often
                // inflate to fewer bytes because alignment padding is omitted.
                const int decoded=edat_inflate(b.data()+offset,compressed,tex.data(),size);
                if(decoded<20){update([](auto& s){++s.errors;});continue;}
                tex.resize((size_t)decoded);
            }
            if(tex.size()<20||memcmp(tex.data(),"\0XET",4)){update([](auto& s){++s.errors;});continue;}
            texture(tex,path,std::string((const char*)e,strnlen((const char*)e,64)));
        }
    }
};
bool within(const fs::path& a,const fs::path& b){
    auto x=a.begin(),y=b.begin();for(;y!=b.end();++y,++x)if(x==a.end()||_wcsicmp(x->c_str(),y->c_str()))return false;return true;
}
void run(fs::path source,fs::path output){
    try{
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)throw 1;
        struct Shutdown {ULONG_PTR token;~Shutdown(){Gdiplus::GdiplusShutdown(token);}} shutdown{token};
        Exporter exporter;exporter.output=output;
        UINT count=0,bytes=0;Gdiplus::GetImageEncodersSize(&count,&bytes);if(!bytes)throw 1;
        std::vector<uint8_t> memory(bytes);auto* codecs=(Gdiplus::ImageCodecInfo*)memory.data();
        Gdiplus::GetImageEncoders(count,bytes,codecs);bool found=false;
        for(UINT i=0;i<count;++i)if(!wcscmp(codecs[i].MimeType,L"image/png")){exporter.codec=codecs[i].Clsid;found=true;break;}
        if(!found)throw 1;
        std::error_code ec;std::vector<fs::path> files;
        if(fs::is_regular_file(source,ec))files.push_back(source);
        else for(fs::recursive_directory_iterator it(source,fs::directory_options::skip_permission_denied,ec),end;it!=end&&!worker.stop;it.increment(ec)){
            if(ec){update([](auto& s){++s.errors;});ec.clear();continue;}
            if(it->is_symlink(ec)){if(it->is_directory(ec))it.disable_recursion_pending();continue;}
            if(!it->is_regular_file(ec))continue;auto ext=it->path().extension().wstring();
            for(auto& c:ext)c=(wchar_t)towlower(c);
            if(ext==L".arc"||ext==L".tex"||ext==L".stex")files.push_back(it->path());
        }
        std::sort(files.begin(),files.end());if(files.empty()&&!worker.stop)throw 1;
        update([&](auto& s){s.total=(uint32_t)files.size();});
        if(!worker.stop){fs::create_directories(output,ec);if(ec)throw 1;}
        if(!worker.stop){
            exporter.report.open(output/(L"extraction-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".csv"),std::ios::binary);
            if(!exporter.report)throw 1;exporter.report<<"archive,asset,png\n";
        }
        for(const auto& path:files){if(worker.stop)break;exporter.file(path);update([](auto& s){++s.files;});}
        if(exporter.report.is_open()){exporter.report.flush();if(!exporter.report)throw 1;}
    }catch(...){update([](auto& s){s.failed=true;});}
    update([](auto& s){s.canceled=worker.stop.load();s.running=false;s.finished=true;});
}
}
bool cv_texture_export_start(const std::filesystem::path& source,const std::filesystem::path& output){
    if(cv_texture_export_status().running)return false;
    if(worker.thread.joinable())worker.thread.join();
    auto failed=[](){update([](auto& s){s={};s.failed=true;s.finished=true;});return false;};
    std::error_code ec;const auto src=std::filesystem::weakly_canonical(source,ec);if(ec||source.empty()||!fs::exists(src,ec))return failed();
    const auto dst=std::filesystem::weakly_canonical(output,ec);if(ec||output.empty()||within(dst,src))return failed();
    worker.stop=false;update([](auto& s){s={};s.running=true;});
    try{worker.thread=std::thread(run,src,dst);}catch(...){update([](auto& s){s.running=false;s.failed=true;s.finished=true;});return false;}return true;
}
void cv_texture_export_cancel(){worker.stop=true;}
CvTextureExportStatus cv_texture_export_status(){std::lock_guard<std::mutex> lock(worker.mutex);return worker.status;}
