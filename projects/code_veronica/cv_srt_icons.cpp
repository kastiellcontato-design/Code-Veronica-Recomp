#include "cv_srt_icons.h"
#include "cv_srt_ring_mask.h"
#include "../../libs/filesystem/edat.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>

namespace cv_srt_icons {
namespace {
constexpr size_t atlas_pixels=768*768,atlas_bytes=atlas_pixels/2;
uint32_t be32(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint32_t rgb565(unsigned p) {
    const unsigned r=(p>>11)&31,g=(p>>5)&63,b=p&31;
    return 0xff000000|((r<<3|r>>2)<<16)|((g<<2|g>>4)<<8)|(b<<3|b>>2);
}
std::vector<uint8_t> read(const std::filesystem::path& p) {
    FILE* f=_wfopen(p.c_str(),L"rb");if(!f)return {};
    if(fseek(f,0,SEEK_END)!=0) {fclose(f);return {};}
    const long n=ftell(f);if(n<0||n>8*1024*1024) {fclose(f);return {};}
    rewind(f);std::vector<uint8_t> out(size_t(n),0);
    const bool ok=fread(out.data(),1,out.size(),f)==out.size();fclose(f);
    return ok?out:std::vector<uint8_t>{};
}
}
bool sprite(int id,Sprite& out) {
    // Observed cells in subtx tex0004 (page 0) / tex0003 (page 1).
    // Pitch is 102x72; large weapons occupy two adjacent cells. Numeric
    // placeholder cells and unconfirmed/file-only entries use the text fallback.
    struct Entry {int id,page,row,col,span;};
    static const Entry entries[]={
        {1,0,0,1,2},{2,0,0,3,2},{3,0,0,5,2},
        {4,0,1,1,1},{5,0,1,2,1},{6,0,1,3,1},{7,0,1,4,1},{8,0,1,5,1},{9,0,1,6,1},
        {0x0a,0,2,0,1},{0x0b,0,2,1,1},{0x0c,0,2,2,1},{0x0d,0,2,3,1},{0x0e,0,2,4,1},{0x0f,0,2,5,1},{0x10,0,2,6,1},
        {0x11,0,3,0,1},{0x12,0,3,1,1},{0x13,0,3,2,1},{0x14,0,3,3,1},{0x15,0,3,4,1},{0x16,0,3,5,1},{0x17,0,3,6,1},
        {0x18,0,4,0,1},{0x19,0,4,1,1},{0x1a,0,4,2,1},{0x1b,0,4,3,1},{0x1c,0,4,4,1},{0x1d,0,4,5,1},{0x1e,0,4,6,1},
        {0x1f,0,5,0,1},{0x20,0,5,1,1},{0x21,0,5,2,2},{0x22,0,5,4,2},{0x23,0,5,6,1},
        {0x24,0,6,0,1},{0x25,0,6,1,1},{0x27,0,6,3,1},{0x28,0,6,4,1},{0x29,0,6,5,1},{0x2a,0,6,6,1},
        {0x2b,0,7,0,1},{0x2c,0,7,1,1},{0x2d,0,7,2,1},{0x2e,0,7,3,1},{0x88,0,7,4,1},{0x8a,0,7,5,1},
        {0x32,0,8,0,1},{0x33,0,8,1,1},{0x34,0,8,2,1},{0x35,0,8,3,1},{0x36,0,8,4,1},{0x37,0,8,5,1},{0x38,0,8,6,1},
        {0x39,0,9,0,1},{0x3b,0,9,2,1},{0x3c,0,9,3,1},{0x3d,0,9,4,1},{0x3e,0,9,5,1},{0x3f,0,9,6,1},
        {0x40,1,0,0,1},{0x41,1,0,1,1},{0x42,1,0,2,1},{0x43,1,0,3,1},{0x44,1,0,4,1},{0x45,1,0,5,1},{0x46,1,0,6,1},
        {0x47,1,1,0,1},{0x48,1,1,1,1},{0x49,1,1,2,1},{0x4b,1,1,4,1},{0x4c,1,1,5,1},{0x4d,1,1,6,1},
        {0x4e,1,2,0,1},{0x4f,1,2,1,1},{0x50,1,2,2,1},{0x51,1,2,3,1},{0x52,1,2,4,1},{0x53,1,2,5,1},{0x54,1,2,6,1},
        {0x55,1,3,0,1},{0x56,1,3,1,1},{0x57,1,3,2,1},{0x58,1,3,3,1},{0x59,1,3,4,1},{0x5a,1,3,5,1},{0x5b,1,3,6,1},
        {0x5c,1,4,0,1},{0x5d,1,4,1,1},{0x5e,1,4,2,1},{0x5f,1,4,3,1},{0x60,1,4,4,1},{0x61,1,4,5,1},{0x62,1,4,6,1},
        {0x63,1,5,0,1},{0x64,1,5,1,1},{0x65,1,5,2,1},{0x90,1,5,3,1},{0x67,1,5,4,1},{0x68,1,5,5,1},{0x69,1,5,6,1},
        {0x6a,1,6,0,1},{0x6b,1,6,1,1},{0x6c,1,6,2,1},{0x6f,1,6,5,1},{0x70,1,6,6,1},
        {0x71,1,7,0,1},{0x72,1,7,1,1},{0x73,1,7,2,1},{0x74,1,7,3,1},{0x75,1,7,4,1},{0x76,1,7,5,1},{0x77,1,7,6,1},
        {0x78,1,8,0,1},{0x79,1,8,1,1},{0x7a,1,8,2,1},{0x7b,1,8,3,1},{0x7c,1,8,4,1},{0x7d,1,8,5,1},{0x7e,1,8,6,1},
        {0x8e,1,9,1,2},{0x83,1,9,4,1},{0x91,1,9,6,1},
        // Same physical object, but the game's inventory records a new ID.
        {0x80,0,6,5,1},{0x81,0,6,5,1},{0x82,0,5,6,1},{0x95,1,2,4,1},{0x96,1,7,0,1}
    };
    for(const auto& e:entries)if(e.id==id) {out={e.page,e.col*102,e.row*72,e.span*102,72};return true;}
    return false;
}
std::vector<uint8_t> unpack_archive(const std::vector<uint8_t>& file) {
    if(file.size()<24||std::memcmp(file.data(),"\0SFH",4)||file.size()>8*1024*1024)return {};
    std::vector<uint8_t> out;
    for(size_t p=16;p<file.size();p+=0x20000) {
        const size_t n=std::min<size_t>(0x1fff0,file.size()-p);
        out.insert(out.end(),file.begin()+p,file.begin()+p+n);
    }
    return out;
}
std::vector<uint32_t> decode_atlas(const std::vector<uint8_t>& archive,int page) {
    if(page<0||page>1||archive.size()<8||std::memcmp(archive.data()+1,"CRA",3))return {};
    const unsigned count=unsigned(archive[6])<<8|archive[7];
    if(count>(archive.size()-8)/80)return {};
    const char* wanted=page==0?"tex0004_BM":"tex0003_BM";
    for(unsigned i=0;i<count;i++) {
        const uint8_t* e=archive.data()+8+i*80;
        const std::string name(reinterpret_cast<const char*>(e),strnlen(reinterpret_cast<const char*>(e),64));
        const size_t cut=name.find_last_of('\\');
        if(name.substr(cut==std::string::npos?0:cut+1)!=wanted||be32(e+64)!=0x241f5deb)continue;
        const size_t packed=be32(e+68),size=be32(e+72)>>3,offset=be32(e+76);
        if(size!=atlas_bytes+20||offset>archive.size()||packed>archive.size()-offset)return {};
        std::vector<uint8_t> t(size);
        if(edat_inflate(archive.data()+offset,packed,t.data(),size)!=int(size))return {};
        // TEX, 768x768, DXT1, one level beginning at byte 20.
        if(std::memcmp(t.data(),"\0XET",4)||be32(t.data()+8)!=0x1800c001||
           be32(t.data()+12)!=0x00011401||be32(t.data()+16)!=20)return {};
        std::vector<uint32_t> out(atlas_pixels);
        for(int by=0;by<192;by++)for(int bx=0;bx<192;bx++) {
            const uint8_t* b=t.data()+20+(by*192+bx)*8;
            const unsigned c0=b[0]|unsigned(b[1])<<8,c1=b[2]|unsigned(b[3])<<8;
            uint32_t colors[]={rgb565(c0),rgb565(c1),0xff000000,0xff000000};
            for(unsigned shift=0;shift<24;shift+=8) {
                const unsigned a=(colors[0]>>shift)&255,c=(colors[1]>>shift)&255;
                colors[2]|=(c0>c1?(2*a+c)/3:(a+c)/2)<<shift;
                if(c0>c1)colors[3]|=((a+2*c)/3)<<shift;
            }
            if(c0<=c1)colors[3]=0;
            const uint32_t codes=uint32_t(b[4])|uint32_t(b[5])<<8|uint32_t(b[6])<<16|uint32_t(b[7])<<24;
            for(int j=0;j<16;j++)out[(by*4+j/4)*768+bx*4+j%4]=colors[(codes>>(2*j))&3];
        }
        return out;
    }
    return {};
}
Icon extract(const std::vector<uint32_t>& atlas,const Sprite& rect,int id) {
    if(atlas.size()!=atlas_pixels||rect.x<0||rect.y<0||rect.w<=0||rect.h<=0||rect.x+rect.w>768||rect.y+rect.h>768)return {};
    std::vector<uint32_t> pixels(size_t(rect.w)*rect.h);
    int left=rect.w,top=rect.h,right=0,bottom=0;
    const auto is_key=[](uint32_t color) {
        const unsigned r=(color>>16)&255,g=(color>>8)&255,b=color&255;
        return r<=24&&g<=16&&b>=32&&b<=112&&b>=r+24&&b>=g+24;
    };
    const bool keep_enclosed_key=id==0x2e; // Alfred's Jewel contains key-coloured pixels inside its outline.
    std::vector<uint8_t> exterior(size_t(rect.w)*rect.h,0);
    std::deque<size_t> queue;
    if(keep_enclosed_key) {
        for(int x=0;x<rect.w;x++) {
            if(is_key(atlas[rect.y*768+rect.x+x]))queue.push_back(size_t(x));
            const size_t i=size_t(rect.h-1)*rect.w+x;
            if(is_key(atlas[(rect.y+rect.h-1)*768+rect.x+x]))queue.push_back(i);
        }
        for(int y=0;y<rect.h;y++) {
            if(is_key(atlas[(rect.y+y)*768+rect.x]))queue.push_back(size_t(y)*rect.w);
            const size_t i=size_t(y)*rect.w+rect.w-1;
            if(is_key(atlas[(rect.y+y)*768+rect.x+rect.w-1]))queue.push_back(i);
        }
        while(!queue.empty()) {
            const size_t i=queue.front();queue.pop_front();
            if(exterior[i])continue;
            const int x=int(i%rect.w),y=int(i/rect.w);
            const uint32_t color=atlas[(rect.y+y)*768+rect.x+x];
            if(!is_key(color))continue;
            exterior[i]=1;
            if(x>0)queue.push_back(i-1);if(x+1<rect.w)queue.push_back(i+1);
            if(y>0)queue.push_back(i-rect.w);if(y+1<rect.h)queue.push_back(i+rect.w);
        }
    }
    const bool remove_divider=id==0x46||id==0x4d||id==0x54||id==0x5b||id==0x62||id==0x69||id==0x70||id==0x77||id==0x7e||id==0x91;
    for(int y=0;y<rect.h;y++)for(int x=0;x<rect.w;x++) {
        uint32_t color=atlas[(rect.y+y)*768+rect.x+x];
        // DXT1 stores the blue key as several nearby colours. Only the dark,
        // almost zero-red/green key range is removed; cyan herbs and blue gems
        // retain their colour. Alfred's Jewel keeps enclosed key-colored pixels
        // so the center of the jewel does not become transparent.
        if(id==0x2d) {
            unsigned alpha=0;
            if(x>=ring_mask_x&&x<ring_mask_x+ring_mask_w&&y>=ring_mask_y&&y<ring_mask_y+ring_mask_h)
                alpha=ring_mask[y-ring_mask_y][x-ring_mask_x];
            color=(color&0x00ffffffu)|(alpha<<24);
        } else if(is_key(color)&&(!keep_enclosed_key||exterior[size_t(y)*rect.w+x]))color=0;
        // These cells include the atlas's one-pixel black separator at x=101.
        if(remove_divider&&x==rect.w-1)color=0;
        pixels[size_t(y)*rect.w+x]=color;
        if(color>>24) {left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
    }
    if(left>=right||top>=bottom)return {};
    Icon icon;icon.width=right-left;icon.height=bottom-top;
    icon.argb.resize(size_t(icon.width)*icon.height);
    for(int y=0;y<icon.height;y++)std::copy_n(pixels.data()+(y+top)*rect.w+left,icon.width,icon.argb.data()+y*icon.width);
    return icon;
}
bool Library::load(const std::wstring& root) {
    if(attempted_&&root_==root)return !atlas_[0].empty()||!atlas_[1].empty();
    root_=root;attempted_=true;atlas_={};icons_={};if(root.empty())return false;
    const auto base=std::filesystem::path(root)/L"USRDIR/BHCV/nativePS3/biocv_disc";
    for(const wchar_t* lang:{L"eng",L"jpn",L"usa",L"fra",L"ger",L"ita",L"spa"}) {
        const auto archive=unpack_archive(read(base/lang/L"item1/subtx_tex.arc"));
        if(archive.empty())continue;
        atlas_[0]=decode_atlas(archive,0);atlas_[1]=decode_atlas(archive,1);
        if(!atlas_[0].empty()||!atlas_[1].empty()) {
            std::fprintf(stderr,"[SRT] Original item icons loaded from selected game (%ls).\n",lang);return true;
        }
    }
    std::fprintf(stderr,"[SRT] Item atlas unavailable; keeping item names.\n");return false;
}
const Icon* Library::get(int id) {
    if(id<=0||id>=int(icons_.size()))return nullptr;
    auto& icon=icons_[id];
    if(icon.argb.empty()) {Sprite rect;if(!sprite(id,rect))return nullptr;icon=extract(atlas_[rect.page],rect,id);}
    return icon.argb.empty()?nullptr:&icon;
}
}
