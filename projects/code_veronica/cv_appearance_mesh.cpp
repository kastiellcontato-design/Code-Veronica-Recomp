#include "cv_appearance_assets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace cv_appearance {namespace {
using Bytes=std::vector<uint8_t>;
void check(const Bytes& b,size_t at,size_t n){if(at>b.size()||n>b.size()-at)throw std::runtime_error("Preview resource bounds");}
uint32_t u32(const Bytes& b,size_t at){check(b,at,4);return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];}
uint16_t u16(const Bytes& b,size_t at){check(b,at,2);return uint16_t(b[at])<<8|b[at+1];}
float f32(const Bytes& b,size_t at){uint32_t v=u32(b,at);float f;std::memcpy(&f,&v,4);if(!std::isfinite(f)||std::fabs(f)>100000)throw std::runtime_error("Invalid preview coordinate");return f;}
std::array<unsigned,4> rgb565(unsigned n){return {((n>>11)&31)*255/31,((n>>5)&63)*255/63,(n&31)*255/31,255};}
PreviewTexture texture(const Bytes& b){
 if(u32(b,0)!=0x00584554)throw std::runtime_error("Unsupported texture magic");
 unsigned dims=u32(b,8);PreviewTexture out;out.width=(dims>>6)&8191;out.height=(dims>>19)&8191;
 size_t off=u32(b,16);if(!out.width||!out.height||out.width>2048||out.height>2048||(out.width%4)||(out.height%4))throw std::runtime_error("Unsupported texture dimensions");
 // PS3 guest TEX formats are interpreted separately from actor data. Only
 // the observed DXT1 first mip is accepted; unknown formats fail closed.
 const bool bc3=u32(b,12)==0x00011801;
 if(u32(b,12)!=0x00011401&&!bc3)throw std::runtime_error("Unsupported preview texture format");
 check(b,off,size_t(out.width/4)*(out.height/4)*(bc3?16:8));out.argb.resize(size_t(out.width)*out.height);
 for(unsigned by=0;by<out.height;by+=4)for(unsigned bx=0;bx<out.width;bx+=4){
  unsigned alpha[8]={};uint64_t alpha_bits=0;
  if(bc3){
   alpha[0]=b[off];alpha[1]=b[off+1];
   if(alpha[0]>alpha[1])for(unsigned i=1;i<=6;i++)alpha[i+1]=((7-i)*alpha[0]+i*alpha[1])/7;
   else {for(unsigned i=1;i<=4;i++)alpha[i+1]=((5-i)*alpha[0]+i*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
   for(unsigned i=0;i<6;i++)alpha_bits|=uint64_t(b[off+2+i])<<(i*8);
   off+=8;
  }
  // BC1 blocks retain the RSX little-endian 565 endpoint and selector layout.
  const unsigned c0=b[off]|unsigned(b[off+1])<<8,c1=b[off+2]|unsigned(b[off+3])<<8;
  const uint32_t bits=uint32_t(b[off+4])|uint32_t(b[off+5])<<8|uint32_t(b[off+6])<<16|uint32_t(b[off+7])<<24;
  std::array<std::array<unsigned,4>,4> colors{};colors[0]=rgb565(c0);colors[1]=rgb565(c1);
  for(unsigned c=0;c<3;c++){colors[2][c]=(bc3||c0>c1)?(2*colors[0][c]+colors[1][c])/3:(colors[0][c]+colors[1][c])/2;colors[3][c]=(bc3||c0>c1)?(colors[0][c]+2*colors[1][c])/3:0;}
  colors[2][3]=255;colors[3][3]=(bc3||c0>c1)?255:0;
  for(unsigned p=0;p<16;p++){const auto& c=colors[(bits>>(p*2))&3];out.argb[size_t(by+p/4)*out.width+bx+p%4]=(bc3?alpha[(alpha_bits>>(p*3))&7]:c[3])<<24|c[0]<<16|c[1]<<8|c[2];}off+=8;
 }
 return out;
}
// Static display pose only. Rotate the arm hierarchy around its shoulder,
// including its normals and the separately loaded hand. Guest assets and the
// gameplay skeleton are never changed.
void lower_arm(PreviewVertex& v,const std::array<float,3>& shoulder,int side,float weight=1.f){
 const float angle=-float(side)*weight*1.36135682f; // 78 degrees below the rest pose
 const float c=std::cos(angle),s=std::sin(angle);
 const float x=v.x-shoulder[0],y=v.y-shoulder[1],nx=v.nx,ny=v.ny;
 v.x=shoulder[0]+c*x-s*y;v.y=shoulder[1]+s*x+c*y;
 v.nx=c*nx-s*ny;v.ny=s*nx+c*ny;
}
void body_mesh(const Bytes& m,PreviewModel& out,unsigned expected=22,const std::array<float,3>& origin={0,0,0},unsigned texture_base=0,std::vector<std::array<float,3>>* joints=nullptr,int attachment_side=0,const std::array<float,3>& attachment_shoulder={0,0,0},const Bytes* skin_weights=nullptr){
 const unsigned header=u16(m,4),nodes=u16(m,6);size_t root=u32(m,12);if(header!=16||nodes!=expected)throw std::runtime_error("Preview requires player body");
 check(m,header+root,size_t(nodes)*52);
 std::vector<int> parent(nodes,-1);std::vector<unsigned> visited(nodes,0);
 auto walk=[&](auto&& self,unsigned node,int p)->void{
  if(node>=nodes||visited[node]++)throw std::runtime_error("Invalid preview skeleton tree");parent[node]=p;
  for(unsigned refat:{44u,48u}){unsigned ref=u32(m,header+root+node*52+refat);if(ref==0xffffffffu)continue;
   if(ref<root||(ref-root)%52)throw std::runtime_error("Invalid tree reference");self(self,(ref-root)/52,refat==44?int(node):p);}
 };walk(walk,0,-1);
 std::vector<std::array<float,3>> positions(nodes),scales(nodes);
 for(unsigned i=0;i<nodes;i++){
  const size_t node=header+root+i*52;for(unsigned axis=0;axis<3;axis++){
   if(expected!=1&&u32(m,node+20+axis*4)!=0)throw std::runtime_error("Preview rest transform not supported");
   const float ps=parent[i]>=0?scales[parent[i]][axis]:1.f;
   scales[i][axis]=f32(m,node+32+axis*4)*ps;
   positions[i][axis]=f32(m,node+8+axis*4)*ps+(parent[i]>=0?positions[parent[i]][axis]:origin[axis]);
  }
 }
 for(unsigned i=0;i<nodes;i++){
  const size_t node=header+root+i*52;
  unsigned geo=u32(m,node+4);if(geo==0xffffffffu)continue;size_t gp=header+size_t(geo);unsigned vp=u32(m,gp),pp=u32(m,gp+4);
  const size_t va=header+size_t(vp);check(m,va,64);if(m[va]!=0x33)throw std::runtime_error("Unknown vertex chunk");
  unsigned first=u16(m,va+4),count=u16(m,va+6);if(count>65536-first)throw std::runtime_error("Vertex index overflow");check(m,va+64,size_t(count)*32);
  std::vector<PreviewVertex> vertices(first+count);
  for(unsigned k=0;k<count;k++){size_t at=va+64+size_t(k)*32;auto& v=vertices[first+k];v={f32(m,at),f32(m,at+4),f32(m,at+8),f32(m,at+16),f32(m,at+20),f32(m,at+24),0,0};
   if(expected==1){
    auto rotate=[&](float& a,float& b,unsigned axis){
     const float radians=float(int32_t(u32(m,node+20+axis*4)))*6.28318530718f/65536.f;
     const float c=std::cos(radians),s=std::sin(radians),old=a;a=c*a-s*b;b=s*old+c*b;
    };
    rotate(v.y,v.z,0);rotate(v.z,v.x,1);rotate(v.x,v.y,2);
    rotate(v.ny,v.nz,0);rotate(v.nz,v.nx,1);rotate(v.nx,v.ny,2);
   }
   v.x=v.x*scales[i][0]+positions[i][0];v.y=v.y*scales[i][1]+positions[i][1];v.z=v.z*scales[i][2]+positions[i][2];
   if(expected==22 && skin_weights){
    check(*skin_weights,size_t(k)*4,4);
    const auto original=v;PreviewVertex result{};float total=0;
    // The position stream uses the first joint/coefficient pair. The
    // other pair belongs to the alternate stream, not a second position weight.
    for(unsigned influence=0;influence<1;influence++){
     const unsigned bone=(*skin_weights)[size_t(k)*4+influence*2];
     const unsigned amount=(*skin_weights)[size_t(k)*4+influence*2+1];
     if(bone==255)continue;
     PreviewVertex posed=original;
     if(bone>=7&&bone<=9)lower_arm(posed,positions[7],1,bone==7?float(amount)/9.f:1.f);
     else if(bone>=11&&bone<=13)lower_arm(posed,positions[11],-1,bone==11?float(amount)/9.f:1.f);
     // A zero angle coefficient still inherits its parent rotation.
     const float weight=1.f;total+=weight;
     result.x+=posed.x*weight;result.y+=posed.y*weight;result.z+=posed.z*weight;
     result.nx+=posed.nx*weight;result.ny+=posed.ny*weight;result.nz+=posed.nz*weight;
    }
    if(total>0){v.x=result.x/total;v.y=result.y/total;v.z=result.z/total;
     v.nx=result.nx/total;v.ny=result.ny/total;v.nz=result.nz/total;}
   }else if(expected==22){
    // The packed body is already skinned into the rest pose: triangles of
    // the arms can live in a torso node. Apply one continuous shoulder
    // deformation to world-space vertices, so shared seam vertices receive
    // exactly the same pose regardless of the chunk that contains them.
    const int side=v.x>=0?1:-1;const auto& shoulder=positions[side>0?7:11];
    const float t=std::clamp((std::fabs(v.x)-(std::fabs(shoulder[0])-.45f))/.85f,0.f,1.f);
    const float h=std::clamp((v.y-(shoulder[1]-1.4f))/.6f,0.f,1.f);
    const float weight=t*t*(3.f-2.f*t)*h*h*(3.f-2.f*h);
    if(weight>0)lower_arm(v,shoulder,side,weight);
   }else if(attachment_side)lower_arm(v,attachment_shoulder,attachment_side);
  }
  check(m,va+64+size_t(count)*32,1);
  if(m[va+64+size_t(count)*32]!=255)throw std::runtime_error("Multiple vertex chunks not supported");
  size_t at=header+size_t(pp);unsigned tex=0;bool ended=false;
  for(unsigned chunks=0;chunks<4096;chunks++){
   check(m,at,2);unsigned op=m[at];if(op==255){ended=true;break;}
   if(op==8){tex=(u16(m,at+2)&8191)+texture_base;if(tex>=out.textures.size())throw std::runtime_error("Preview texture index missing");at+=4;continue;}
   if(op!=17&&op!=18&&op!=19&&op!=65&&op!=66)throw std::runtime_error("Unknown preview polygon chunk");
   size_t length=u16(m,at+2)*2,end=at+4+length;check(m,at+4,length);
   if(op==65||op==66){unsigned strips=u16(m,at+4)&0x3fff;if((u16(m,at+4)>>14)!=0)throw std::runtime_error("Strip user data unsupported");size_t p=at+6;
    for(unsigned strip=0;strip<strips;strip++){
     if(p+2>end)throw std::runtime_error("Truncated strip");int signed_count=int16_t(u16(m,p));p+=2;unsigned n=unsigned(std::abs(signed_count));if(n<3||n>(end-p)/6)throw std::runtime_error("Invalid strip length");
     std::vector<PreviewVertex> sequence;sequence.reserve(n);
     for(unsigned k=0;k<n;k++,p+=6){unsigned ix=u16(m,p);if(ix<first||ix>=vertices.size())throw std::runtime_error("Strip index outside vertices");PreviewVertex v=vertices[ix];v.u=float(int16_t(u16(m,p+2)))/(op==65?256.f:1024.f);v.v=float(int16_t(u16(m,p+4)))/(op==65?256.f:1024.f);sequence.push_back(v);}
     for(unsigned k=2;k<n;k++){PreviewTriangle triangle{};bool flip=((k&1)!=0)^(signed_count<0);triangle.vertices[0]=sequence[k-2];triangle.vertices[1]=sequence[flip?k:k-1];triangle.vertices[2]=sequence[flip?k-1:k];triangle.texture=tex;out.triangles.push_back(triangle);if(out.triangles.size()>50000)throw std::runtime_error("Preview mesh limit");}
    }
    if(p>end||end-p>2)throw std::runtime_error("Strip length disagreement");
   }
   at=end;
  }
  if(!ended)throw std::runtime_error("Polygon terminator missing");
 }
 if(joints)*joints=std::move(positions);
}
}
bool make_preview(const PlayerAsset& asset,PreviewModel& out,std::string& error){
 try {PreviewModel result;for(const auto& r:asset.resources)if(r.type==0x241f5deb)result.textures.push_back(texture(r.bytes));
  const bool converted=std::any_of(asset.resources.begin(),asset.resources.end(),[](const auto& r){return r.type==0x74afe18c&&r.name.find("\\cvskin")!=std::string::npos;});
  size_t at=0;bool found=false;Bytes weights;
  while(at<asset.model.size()){
   size_t n=u32(asset.model,at);at+=4;if(n==0xffffffffu)break;check(asset.model,at,n);
   if(converted&&n>=4&&u32(asset.model,at)==0x534b494e){
    size_t p=at+4,end=at+n;
    for(unsigned node=0;node<22;node++){
     check(asset.model,p,4);const unsigned flags=u32(asset.model,p);p+=4;
     if(!flags)continue;
     if(flags!=1&&flags!=3)throw std::runtime_error("Invalid converted preview skin");
     check(asset.model,p,4);p+=4;
     if(flags&2){const unsigned count=u32(asset.model,p);p+=4;check(asset.model,p,size_t(count)*4);
      if(p+size_t(count)*4>end)throw std::runtime_error("Converted preview weights exceed skin");
      weights=Bytes(asset.model.begin()+p,asset.model.begin()+p+size_t(count)*4);p+=size_t(count)*4;}
    }
   }
   if(n>=16&&asset.model[at]=='M'&&asset.model[at+1]=='D'&&asset.model[at+2]=='L'&&u16(asset.model,at+6)==22){
    if(converted&&weights.empty())throw std::runtime_error("Converted preview weights missing");
    body_mesh(Bytes(asset.model.begin()+at,asset.model.begin()+at+n),result,22,{0,0,0},0,nullptr,0,{0,0,0},converted?&weights:nullptr);found=true;break;
   }at+=n;
  }
  if(!found||result.triangles.empty())throw std::runtime_error("Preview body missing");out=std::move(result);error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool add_preview_glasses(const PlayerAsset& asset,PreviewModel& out,std::string& error){
 try {
  size_t at=0;unsigned index=0;std::vector<std::array<float,3>> joints;
  PreviewModel scratch;scratch.textures=out.textures;
  while(at+4<=asset.model.size()){
   size_t n=u32(asset.model,at);at+=4;if(n==0xffffffffu)break;check(asset.model,at,n);
   if(n>=16&&asset.model[at]=='M'&&asset.model[at+1]=='D'&&asset.model[at+2]=='L'){
    Bytes mdl(asset.model.begin()+at,asset.model.begin()+at+n);
    if(index==0)body_mesh(mdl,scratch,22,{0,0,0},0,&joints);
    if(index++==4){
     if(joints.size()!=22||u16(mdl,6)!=1)throw std::runtime_error("Glasses attachment missing");
     const size_t first=out.triangles.size();
     // WKGRAS2 is the independent ob_1127 texture, not Wesker's glove atlas.
     unsigned lens=0;bool found=false;
     for(const auto& r:asset.resources)if(r.type==0x241f5deb){
      if(r.name.ends_with("\\ob_1127\\tex0000_BM")){found=true;break;}++lens;
     }
     if(!found)throw std::runtime_error("Original WKGRAS2 lens texture missing; use the lens test launcher");
     body_mesh(mdl,out,1,joints[5],lens);
     for(size_t i=first;i<out.triangles.size();i++)for(auto& v:out.triangles[i].vertices){
      v.y+=.87f;v.z-=1.05f;
     }
     error.clear();return true;
    }
   }at+=n;
  }
  throw std::runtime_error("Glasses model missing");
 }catch(const std::exception& e){error=e.what();return false;}
}
bool add_preview_hands(const PlayerAsset& asset,const std::vector<Resource>& hands,PreviewModel& out,std::string& error){
 try {
  size_t at=0;std::vector<std::array<float,3>> joints;PreviewModel scratch; scratch.textures=out.textures;
  while(at<asset.model.size()){
   size_t n=u32(asset.model,at);at+=4;if(n==0xffffffffu)break;check(asset.model,at,n);
   if(n>=16&&asset.model[at]=='M'&&asset.model[at+1]=='D'&&asset.model[at+2]=='L'&&u16(asset.model,at+6)==22){body_mesh(Bytes(asset.model.begin()+at,asset.model.begin()+at+n),scratch,22,{0,0,0},0,&joints);break;}at+=n;
  }
  if(joints.size()!=22)throw std::runtime_error("Body attachment nodes missing");
  PreviewModel additions;unsigned found=0;
  for(const auto& r:hands)if(r.type==0x0a736313){
   const bool right=r.name.size()>=2&&r.name.compare(r.name.size()-2,2,"_R")==0;
   const bool left=r.name.size()>=2&&r.name.compare(r.name.size()-2,2,"_L")==0;
   if(!right&&!left)continue;unsigned bit=right?1:2;if(found&bit)throw std::runtime_error("Duplicate preview hand");
   PreviewModel piece;for(const auto& t:hands)if(t.type==0x241f5deb&&t.name.rfind(r.name+"\\",0)==0)piece.textures.push_back(texture(t.bytes));
   if(piece.textures.empty())throw std::runtime_error("Hand texture missing");
   const size_t n=u32(r.bytes,0);check(r.bytes,4,n);Bytes mdl(r.bytes.begin()+4,r.bytes.begin()+4+n);
   body_mesh(mdl,piece,1,joints[right?9:13],0,nullptr,right?1:-1,joints[right?7:11]);
   const unsigned offset=unsigned(out.textures.size()+additions.textures.size());
   for(auto triangle:piece.triangles){triangle.texture+=offset;additions.triangles.push_back(triangle);}
   additions.textures.insert(additions.textures.end(),piece.textures.begin(),piece.textures.end());found|=bit;
  }
  if(found!=3)throw std::runtime_error("Both unarmed hands are required");
  PreviewModel complete=out;
  complete.textures.insert(complete.textures.end(),additions.textures.begin(),additions.textures.end());
  complete.triangles.insert(complete.triangles.end(),additions.triangles.begin(),additions.triangles.end());
  out=std::move(complete);error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}

}

