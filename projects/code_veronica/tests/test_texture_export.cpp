#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "../cv_texture_export.h"
#include "../../../libs/video/rsx_texture_pack.h"
#include <cassert>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdio>
#include <cstring>
namespace fs=std::filesystem;
static void be(std::vector<uint8_t>& b,size_t o,uint32_t v){for(int i=3;i>=0;--i){b[o+i]=(uint8_t)v;v>>=8;}}
static void write(const fs::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);assert(f);f.write((const char*)b.data(),b.size());assert(f.good());}
static std::vector<uint8_t> read(const fs::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);auto n=f.tellg();assert(n>0);std::vector<uint8_t> b((size_t)n);f.seekg(0);f.read((char*)b.data(),n);assert(f.good());return b;}
static CvTextureExportStatus wait(){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(10);
    while(cv_texture_export_status().running){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    const auto s=cv_texture_export_status();assert(s.finished);return s;
}
static std::vector<uint8_t> texture(int fmt){
    size_t n=fmt==0x14?8:fmt==0x28?64:16;std::vector<uint8_t> t(20+n);memcpy(t.data(),"\0XET",4);
    be(t,4,0x20000098);be(t,8,(4<<19)|(4<<6)|1);be(t,12,0x10001|(fmt<<8));be(t,16,20);
    if(fmt==0x28){for(int y=0;y<4;++y)for(int x=0;x<4;++x){int i=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2);t[20+i*4]=255;t[21+i*4]=(uint8_t)(x*60);t[22+i*4]=(uint8_t)(y*60);}}
    else {size_t start=fmt==0x14?20:28;if(fmt==0x16)memset(t.data()+20,255,8);if(fmt==0x18)t[20]=t[21]=255;t[start]=0;t[start+1]=0xf8;}
    return t;
}
static std::vector<uint8_t> archive(const std::vector<std::vector<uint8_t>>& textures){
    size_t n=8+textures.size()*80;for(const auto& t:textures)n+=t.size();std::vector<uint8_t> b(n);memcpy(b.data(),"\0CRA",4);b[5]=8;b[6]=(uint8_t)(textures.size()>>8);b[7]=(uint8_t)textures.size();
    size_t off=8+textures.size()*80;
    for(size_t i=0;i<textures.size();++i){size_t e=8+i*80;snprintf((char*)b.data()+e,64,"test\\tex%zu",i);be(b,e+64,0x241f5deb);be(b,e+68,(uint32_t)textures[i].size());be(b,e+72,(uint32_t)textures[i].size()<<3);be(b,e+76,(uint32_t)off);memcpy(b.data()+off,textures[i].data(),textures[i].size());off+=textures[i].size();}return b;
}
int main(int argc,char** argv){
    setvbuf(stdout,nullptr,_IONBF,0);assert(argc>=2);const auto root=fs::absolute(fs::u8path(argv[1]));fs::create_directories(root);
    const auto src=root/L"source",dst=root/L"png";fs::create_directories(src);fs::create_directories(dst);
    // Both raw and SFH-framed archives, including duplicate entries.
    std::vector<std::vector<uint8_t>> data={texture(0x14),texture(0x16),texture(0x18),texture(0x28),texture(0x14)};
    auto b=archive(data);write(src/L"raw.arc",b);
    std::vector<uint8_t> sfh(16);memcpy(sfh.data(),"\0SFH",4);sfh.insert(sfh.end(),b.begin(),b.end());write(src/L"wrapped.arc",sfh);
    assert(!cv_texture_export_start(src,src/L"do-not-create"));assert(!fs::exists(src/L"do-not-create"));
    assert(!cv_texture_export_start(src/L"missing",dst));assert(cv_texture_export_status().failed);
    assert(cv_texture_export_start(src,dst));auto result=wait();assert(!result.failed&&!result.canceled&&result.files==2&&result.errors==0&&result.unsupported==0);
    // Fresh output gives four images; repeated test runs retain them safely.
    assert(result.written+result.existing==10);assert(result.written<=4);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;assert(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok);
    for(size_t i=0;i<4;++i){uint32_t format=i==0?0x86:i==1?0x87:i==2?0x88:0x85;const auto& t=data[i];
        uint64_t hash=texpack_hash(t.data()+20,t.size()-20,4,4,format);wchar_t leaf[100];swprintf(leaf,100,L"%016llX_4x4_%ls.png",(unsigned long long)hash,i==0?L"DXT1":i==1?L"DXT3":i==2?L"DXT5":L"ARGB");
        Gdiplus::Bitmap png((dst/leaf).c_str());assert(png.GetLastStatus()==Gdiplus::Ok&&png.GetWidth()==4&&png.GetHeight()==4);
        Gdiplus::Color pixel;png.GetPixel(0,0,&pixel);
        if(i<3)assert(pixel.GetR()>245&&pixel.GetG()<10&&pixel.GetB()<10&&pixel.GetA()>245);
        else for(int y=0;y<4;++y)for(int x=0;x<4;++x){png.GetPixel(x,y,&pixel);assert(pixel.GetR()==x*60&&pixel.GetG()==y*60&&pixel.GetB()==0&&pixel.GetA()==255);}
    }
    Gdiplus::GdiplusShutdown(token);
    // Never overwrite user edits; source bytes stay identical too.
    const auto before=read(src/L"raw.arc");
    uint64_t hash=texpack_hash(data[0].data()+20,8,4,4,0x86);wchar_t leaf[100];swprintf(leaf,100,L"%016llX_4x4_DXT1.png",(unsigned long long)hash);
    const auto changed=dst/leaf;const std::vector<uint8_t> edit={1,2,3,4,5};write(changed,edit);
    assert(cv_texture_export_start(src,dst));result=wait();assert(result.written==0&&result.existing==10&&read(changed)==edit&&read(src/L"raw.arc")==before);
    // Restore just the test's fake edit so the next invocation can inspect it.
    fs::remove(changed);assert(cv_texture_export_start(src,dst));wait();
    // Malformed texture and archive cannot walk out of bounds.
    auto bad=texture(0x14);be(bad,16,0xfffffff0);auto broken=archive({bad,texture(0xff)});write(root/L"bad.arc",broken);
    assert(cv_texture_export_start(root/L"bad.arc",root/L"bad-png"));result=wait();assert(result.errors==1&&result.unsupported==1);
    assert(cv_texture_export_start(src,root/L"cancel-png"));cv_texture_export_cancel();result=wait();assert(result.canceled);
    if(argc>=3){
        const auto actual=fs::u8path(argv[2]);assert(cv_texture_export_start(actual,root/L"real"));result=wait();
        std::printf("[TEX EXPORT REAL] files=%u/%u PNG=%u existing=%u unsupported=%u errors=%u\n",result.files,result.total,result.written,result.existing,result.unsupported,result.errors);
        assert(!result.failed&&!result.canceled&&result.total>0&&result.files==result.total&&result.written+result.existing>0&&result.errors==0);
    }
    std::puts("[TEX EXPORT TEST] PASS: hashes/formats, PNG colors/alpha, ARC/SFH, duplicates, preserved edits/source, invalid data, safe output, cancellation.");return 0;
}
