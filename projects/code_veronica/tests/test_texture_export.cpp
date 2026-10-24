#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "../cv_texture_export.h"
#include "../cv_texture_paths.h"
#include "../../../libs/codec/stb_image.h"
#include <cstdlib>
#include "../../../libs/video/rsx_texture_pack.h"
#include "../../../libs/video/rsx_texture_dds.h"
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

static void check_texture_folders(const fs::path& root) {
    using namespace cv_texture_paths;
    const auto base=root/L"routing"/std::to_wstring(GetCurrentProcessId());fs::create_directories(base);
    const auto exe=base/L"program",input=base/L"mod-input",old_input=base/L"old-mod-input";
    for(const auto* code:{L"NPEB00553",L"NPJB00135",L"NPUB30467"}) {
        assert(mod_folder(exe,L"")==exe/L"mods"/L"textures");
        assert(dump_folder(exe,L"",code)==exe/L"mods"/L"texture-dumps"/code);
        assert(dump_folder(exe,input.wstring(),code)==input);
    }
    assert(mod_folder(exe,input.wstring())==input);
    fs::create_directories(input);
    assert(replacement_folder(input,L"NPJB00135")==input);
    assert(!fs::exists(input/L"NPJB00135")); // Opening/scanning a direct pack must not create an empty regional child.
    const auto regional=input/L"NPJB00135";fs::create_directories(regional);
    assert(replacement_folder(input,L"NPJB00135")==regional);
    assert(same_folder(regional,regional/L"."));
    const auto first=regional/L"saída-extraída",second=base/L"extraídas-B";
    fs::create_directories(first);
    constexpr uint64_t art=0x123456789abcdef0ULL,dumped_only=0x3456789abcdef012ULL,image=0x456789abcdef0123ULL;
    auto file=[](uint64_t h){wchar_t leaf[100];swprintf(leaf,100,L"%016llX_1x1_RGBA.png",(unsigned long long)h);return fs::path(leaf);};
    write(regional/file(art),{1});write(first/file(dumped_only),{2});
    auto env=[](const char* key,const fs::path& value){const auto utf8=value.u8string();assert(!_putenv_s(key,reinterpret_cast<const char*>(utf8.c_str())));};
    env("TEXTURE_DIR",old_input);assert(!_putenv_s("TEXTURE_GAME","NPJB00135"));
    assert(texpack_set_dump_directory(first.c_str()));
    // Changing output before launch must not freeze a stale replacement folder.
    env("TEXTURE_DIR",input);env("TEXTURE_DUMP_DIR",first);
    auto wait_file=[&](const fs::path& f){
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!fs::exists(f)){assert(std::chrono::steady_clock::now()<until);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    };
    auto queue=[](uint64_t hash,uint8_t r,uint8_t g,uint8_t b,uint8_t a){
        auto* bytes=static_cast<uint8_t*>(std::malloc(4));assert(bytes);bytes[0]=r;bytes[1]=g;bytes[2]=b;bytes[3]=a;
        texpack_dump(hash,1,1,"RGBA",bytes);
    };
    assert(texpack_dump_wanted(image));queue(image,255,0,0,255);
    const auto generation=texpack_dump_generation();
    assert(texpack_set_dump_directory(second.c_str())&&texpack_dump_generation()!=generation);
    assert(texpack_dump_wanted(image));queue(image,0,0,255,128);
    wait_file(first/file(image));wait_file(second/file(image));
    auto pixel=[&](const fs::path& f,uint8_t red,uint8_t blue,uint8_t alpha){
        const auto bytes=read(f);int w=0,h=0,c=0;auto* rgba=stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&c,4);
        assert(rgba&&w==1&&h==1&&rgba[0]==red&&rgba[1]==0&&rgba[2]==blue&&rgba[3]==alpha);stbi_image_free(rgba);
    };
    pixel(first/file(image),255,0,255);pixel(second/file(image),0,255,128);
    assert(!fs::exists(input/file(image))&&!fs::exists(regional/file(image)));
    assert(!texpack_dump_wanted(image));
    const auto same_generation=texpack_dump_generation();
    assert(texpack_set_dump_directory((second/L"").c_str())&&texpack_dump_generation()==same_generation);
    assert(!texpack_set_dump_directory(nullptr)&&texpack_dump_generation()==same_generation);
    assert(!texpack_set_dump_directory(std::wstring(1100,L'x').c_str()));
    // Pending writes never replace edited or existing PNGs.
    const std::vector<uint8_t> edit={7,8,9};write(second/file(image),edit);queue(image,0,255,0,255);
    constexpr uint64_t sentinel=0x56789abcdef01234ULL;assert(texpack_dump_wanted(sentinel));queue(sentinel,1,2,3,255);
    wait_file(second/file(sentinel));assert(read(second/file(image))==edit);
    assert(texpack_set_dump_directory(first.c_str()));assert(!texpack_dump_wanted(image));
    // The replacement index loads only mod input, excluding the extraction directory.
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    int found=-1;while((found=texpack_lookup(art))<0){assert(std::chrono::steady_clock::now()<until);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    assert(found==1&&texpack_lookup(dumped_only)==0&&texpack_lookup(image)==0);
    std::puts("[TEXTURE PATHS TEST] PASS: independent defaults, EU/JP/US folders, direct packs, live destinations, queued path snapshots, per-folder dedup, preserved PNG/alpha, mod input exclusion.");
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
    // DDS round trip: original BC blocks/mips and decoded ARGB alpha.
    auto bc_alpha=texture(0x18);bc_alpha[20]=128;bc_alpha[21]=64;
    auto rgba_alpha=texture(0x28);for(size_t p=20;p<rgba_alpha.size();p+=4)rgba_alpha[p]=128;
    auto bc_cutout=texture(0x14);memset(bc_cutout.data()+20,0,8);bc_cutout[22]=bc_cutout[23]=255;memset(bc_cutout.data()+24,255,4);
    auto chain=texture(0x14);chain.resize(52);be(chain,8,(4<<19)|(4<<6)|3);be(chain,16,28);be(chain,20,36);be(chain,24,44);
    for(size_t p=28;p<52;p++)chain[p]=(uint8_t)p;
    const auto dds_source=root/L"dds-test.arc",dds_output=root/L"dds";
    write(dds_source,archive({bc_alpha,rgba_alpha,bc_cutout,chain}));const auto source_before=read(dds_source);
    assert(cv_texture_export_start(dds_source,dds_output,true));result=wait();assert(!result.failed&&result.errors==0&&result.written+result.existing==4);
    size_t examined=0;
    for(const auto& item:fs::directory_iterator(dds_output))if(item.path().extension()==L".dds") {
        const auto bytes=read(item.path());auto* im=texpack_dds_parse(bytes.data(),bytes.size());assert(im);
        if(im->levels==3){assert(im->format==71&&im->bytes==24&&!memcmp(im->data,chain.data()+28,24));}
        else if(im->format==28){assert(im->data[3]==128);}
        else {std::vector<uint8_t> rgba(64);texpack_decode_bc(im->format==71?1:3,im->data,im->row_bytes[0],4,4,rgba.data());assert(rgba[3]==(im->format==71?0:128));}
        texpack_free(im);examined++;
    }
    assert(examined==4&&read(dds_source)==source_before);
    assert(cv_texture_export_start(dds_source,dds_output,true));result=wait();assert(result.written==0&&result.existing==4);
    std::puts("[DDS EXPORT TEST] PASS: source BC bytes/mips preserved, RGBA/BC alpha, round trip, preserved source and existing DDS.");
    check_texture_folders(root);
    if(argc>=3){
        const auto actual=fs::u8path(argv[2]);assert(cv_texture_export_start(actual,root/L"real"));result=wait();
        std::printf("[TEX EXPORT REAL] files=%u/%u PNG=%u existing=%u unsupported=%u errors=%u\n",result.files,result.total,result.written,result.existing,result.unsupported,result.errors);
        assert(!result.failed&&!result.canceled&&result.total>0&&result.files==result.total&&result.written+result.existing>0&&result.errors==0);
    }
    std::puts("[TEX EXPORT TEST] PASS: hashes/formats, PNG colors/alpha, ARC/SFH, duplicates, preserved edits/source, invalid data, safe output, cancellation.");return 0;
}
