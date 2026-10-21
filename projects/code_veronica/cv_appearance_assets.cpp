#include "cv_appearance_assets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
namespace cv_appearance { namespace {
using Bytes=std::vector<uint8_t>;
constexpr size_t kLimit=32*1024*1024;
void range(const Bytes& b,size_t at,size_t n) { if(at>b.size()||n>b.size()-at)throw std::runtime_error("Truncated resource"); }
uint32_t be32(const Bytes& b,size_t a) {range(b,a,4);return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];}
uint16_t be16(const Bytes& b,size_t a) {range(b,a,2);return uint16_t(b[a])<<8|b[a+1];}
Bytes part(const Bytes& b,size_t a,size_t n) {range(b,a,n);return Bytes(b.begin()+a,b.begin()+a+n);}
float real(const Bytes& b,size_t a) {uint32_t n=be32(b,a);float f;std::memcpy(&f,&n,4);if(!std::isfinite(f))throw std::runtime_error("Nonfinite skeleton");return f;}
uint32_t adler(const Bytes& b) {uint32_t a=1,c=0;for(uint8_t v:b){a=(a+v)%65521;c=(c+a)%65521;}return c<<16|a;}
Bytes unwrap(const Bytes& b) {
 if(b.size()<4||be32(b,0)!=0x00534648)return b;
 if(be32(b,4)!=0x00010001)throw std::runtime_error("Unsupported SFH version");
 size_t expected=be32(b,8);if(expected>kLimit)throw std::runtime_error("SFH size exceeds limit");
 Bytes out;out.reserve(expected);
 for(size_t at=0;at<b.size()&&out.size()<expected;at+=0x20000){
  range(b,at,16);size_t n=std::min({size_t(0x1fff0),b.size()-at-16,expected-out.size()});
  out.insert(out.end(),b.begin()+at+16,b.begin()+at+16+n);
 }
 if(out.size()!=expected)throw std::runtime_error("Truncated SFH payload");return out;
}
Bytes decode(const Bytes& b,size_t at,size_t n,size_t capacity) {
 range(b,at,n);if(!capacity||capacity>kLimit)throw std::runtime_error("Invalid ARC resource capacity");
 const bool zlib=n>=6&&(b[at]&15)==8&&((uint32_t(b[at])<<8|b[at+1])%31)==0;
 if(!zlib){if(n>capacity)throw std::runtime_error("Raw resource exceeds capacity");return part(b,at,n);}
 Bytes out(capacity);int decoded=stbi_zlib_decode_buffer(reinterpret_cast<char*>(out.data()),int(out.size()),reinterpret_cast<const char*>(b.data()+at),int(n));
 if(decoded<0)throw std::runtime_error("Invalid zlib resource");out.resize(size_t(decoded));
 if(adler(out)!=be32(b,at+n-4))throw std::runtime_error("Resource checksum mismatch");return out;
}
struct Bone {std::array<float,3> p,s;std::array<uint32_t,3> a;uint32_t child,sibling;};
std::vector<Bone> body(const Bytes& model) {
 size_t pos=0;bool ended=false;std::vector<Bone> result;
 for(unsigned chunks=0;chunks<64;chunks++) {
  const size_t n=be32(model,pos);pos+=4;
  if(n==0xffffffffu){ended=true;break;}range(model,pos,n);
  if(n>=16&&model[pos]=='M'&&model[pos+1]=='D'&&model[pos+2]=='L') {
   size_t header=be16(model,pos+4),count=be16(model,pos+6),offset=be32(model,pos+12);
   if(header!=16||count>256)throw std::runtime_error("Unsupported MDL skeleton");
   if(count&&result.empty()) {
    if(count!=22)throw std::runtime_error("Player body is not a 22-node skeleton");
    if(offset>n-header||count*52>n-header-offset)throw std::runtime_error("MDL nodes outside chunk");
    auto link=[&](uint32_t ref) {if(ref==0xffffffffu)return ref;if(ref<offset||(ref-offset)%52||(ref-offset)/52>=count)throw std::runtime_error("Invalid skeleton link");return uint32_t((ref-offset)/52);};
    for(size_t i=0;i<count;i++) {
     size_t a=pos+header+offset+i*52;Bone bone{};
     for(size_t axis=0;axis<3;axis++){bone.p[axis]=real(model,a+8+axis*4);bone.a[axis]=be32(model,a+20+axis*4);bone.s[axis]=real(model,a+32+axis*4);}
     bone.child=link(be32(model,a+44));bone.sibling=link(be32(model,a+48));result.push_back(bone);
    }
   }
  }
  pos+=n;
 }
 if(!ended||result.empty())throw std::runtime_error("Incomplete player model");return result;
}
}
bool load_archive_resources(const std::filesystem::path& path,std::vector<Resource>& out,std::string& error) {
 try {
  std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Player archive not found");
  auto length=file.tellg();if(length<=0||length>std::streamoff(kLimit))throw std::runtime_error("Invalid archive length");
  Bytes physical(static_cast<size_t>(length));file.seekg(0);if(!file.read(reinterpret_cast<char*>(physical.data()),length))throw std::runtime_error("Archive read failed");
  Bytes arc=unwrap(physical);if(be32(arc,0)!=0x00435241||be16(arc,4)!=8)throw std::runtime_error("Unsupported player ARC");
  size_t count=be16(arc,6),table=8+count*80;if(count>4096)throw std::runtime_error("Invalid ARC count");range(arc,0,table);
  std::vector<Resource> pending;
  for(size_t i=0;i<count;i++) {
   size_t a=8+i*80;const char* name=reinterpret_cast<const char*>(arc.data()+a);size_t len=0;while(len<64&&name[len])len++;
   if(len==64)throw std::runtime_error("Unterminated resource name");
   Resource r;r.name.assign(name,len);r.type=be32(arc,a+64);
   size_t compressed=be32(arc,a+68),capacity=be32(arc,a+72)>>3,offset=be32(arc,a+76);
   if(offset<table)throw std::runtime_error("Resource overlaps ARC table");r.bytes=decode(arc,offset,compressed,capacity);
   pending.push_back(std::move(r));
  }
  out=std::move(pending);error.clear();return true;
 } catch(const std::exception& e) {error=e.what();return false;}
}
bool load_player_asset(const std::filesystem::path& path,PlayerAsset& out,std::string& error) {
 try {
  PlayerAsset pending;unsigned players=0;
  auto source=path;
  if(path.stem()=="pl03")if(const char* overlay=std::getenv("CV_WESKER_LENS_ARC"))
   if(*overlay)source=std::filesystem::path(overlay);
  if(!load_archive_resources(source,pending.resources,error))return false;
  for(const auto& r:pending.resources)if(r.type==0x74afe18c) {
   if(++players!=1)throw std::runtime_error("Ambiguous player resource");size_t at=0;
   Bytes* blocks[]={&pending.model,&pending.motion,&pending.translation};
   for(auto* block:blocks){size_t n=be32(r.bytes,at);at+=4;*block=part(r.bytes,at,n);at+=n;}
  }
  if(players!=1)throw std::runtime_error("Player resource missing");pending.body_nodes=uint32_t(body(pending.model).size());
  out=std::move(pending);error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool compatible_player_skin(const PlayerAsset& a,const PlayerAsset& b,std::string& error) {
 try {
  auto x=body(a.model),y=body(b.model);
  if(x.size()!=22||y.size()!=22)throw std::runtime_error("Skin requires the 22-joint player rig");
  for(size_t i=0;i<x.size();i++)
   if(x[i].child!=y[i].child||x[i].sibling!=y[i].sibling)throw std::runtime_error("Skin joint hierarchy differs");
  error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool compatible_claire_variant(const PlayerAsset& a,const PlayerAsset& b,std::string& error) {
 try {
  if(a.motion.empty()||a.motion!=b.motion)throw std::runtime_error("Claire motion blocks differ");
  auto x=body(a.model),y=body(b.model);if(x.size()!=y.size())throw std::runtime_error("Body skeleton count differs");
  for(size_t i=0;i<x.size();i++) {
   if(x[i].child!=y[i].child||x[i].sibling!=y[i].sibling||x[i].a!=y[i].a||x[i].s!=y[i].s)throw std::runtime_error("Claire skeleton hierarchy differs");
   for(size_t axis=0;axis<3;axis++)if(std::fabs(x[i].p[axis]-y[i].p[axis])>0.0001f)throw std::runtime_error("Claire bone lengths differ");
  }
  error.clear();return true;
 } catch(const std::exception& e) {error=e.what();return false;}
}
}
