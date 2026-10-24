// Native counterpart of the local appearance preparation scripts. No bundled
// game resources or Python runtime. Conversion is lazy, bounded and cached.
#include "cv_appearance_prepare.h"
#include "cv_appearance_assets.h"
#include "cv_appearance_catalog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#define NOMINMAX
#include <windows.h>
#include "zlib.h"
namespace cv_appearance { namespace {
using B=std::vector<uint8_t>;
using V=std::array<double,3>;
constexpr size_t limit=32*1024*1024;
constexpr uint32_t ALD=0x74afe18c,ENEMY=0x108f442e,TEX=0x241f5deb,DDS=0x6505b384,SCRA=0x73850d05;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void bounds(const B& b,size_t p,size_t n){require(p<=b.size()&&n<=b.size()-p,"Appearance data outside resource");}
uint32_t u(const B& b,size_t p){bounds(b,p,4);return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
unsigned h(const B& b,size_t p){bounds(b,p,2);return unsigned(b[p])<<8|b[p+1];}
uint32_t le(const B& b,size_t p){bounds(b,p,4);return uint32_t(b[p])|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24;}
void put(B& b,size_t p,uint32_t v){bounds(b,p,4);for(unsigned i=0;i<4;i++)b[p+i]=uint8_t(v>>(24-i*8));}
void puth(B& b,size_t p,unsigned v){bounds(b,p,2);b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
void putle(B& b,size_t p,uint32_t v){bounds(b,p,4);for(unsigned i=0;i<4;i++)b[p+i]=uint8_t(v>>(i*8));}
double real(const B& b,size_t p){uint32_t bits=u(b,p);float f;std::memcpy(&f,&bits,4);require(std::isfinite(f),"Nonfinite appearance coordinate");return f;}
void putf(B& b,size_t p,double d){float f=float(d);require(std::isfinite(f),"Appearance coordinate overflow");uint32_t v;std::memcpy(&v,&f,4);put(b,p,v);}
B slice(const B& b,size_t p,size_t n){bounds(b,p,n);return B(b.begin()+p,b.begin()+p+n);}
void append(B& dst,const B& src){require(dst.size()+src.size()<=limit,"Appearance size exceeds limit");dst.insert(dst.end(),src.begin(),src.end());}
void word(B& b,uint32_t v){size_t p=b.size();b.resize(p+4);put(b,p,v);}
B read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);require(bool(f),"Appearance source archive missing");auto n=f.tellg();require(n>0&&n<=std::streamoff(limit),"Invalid appearance archive size");B b(size_t(n),0);f.seekg(0);require(bool(f.read((char*)b.data(),n)),"Cannot read appearance source archive");return b;}
std::string key(const std::vector<std::filesystem::path>& sources){
 uint64_t hash=14695981039346656037ull;
 auto feed=[&](const void* data,size_t size){auto p=(const uint8_t*)data;for(size_t i=0;i<size;i++){hash^=p[i];hash*=1099511628211ull;}};
 const std::string recipe="native-appearance-v1";feed(recipe.data(),recipe.size());
 for(const auto& p:sources){auto s=std::filesystem::absolute(p).lexically_normal().u8string();feed(s.data(),s.size());B b=read(p);const auto n=b.size();feed(&n,sizeof n);feed(b.data(),b.size());}
 std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
void atomic_write(const std::filesystem::path& file,const B& bytes){
 std::filesystem::create_directories(file.parent_path());auto temp=file;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
 try{std::ofstream f(temp,std::ios::binary|std::ios::trunc);require(bool(f),"Cannot write appearance cache");f.write((const char*)bytes.data(),bytes.size());f.close();require(bool(f),"Appearance cache write failed");require(MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot finalize appearance cache");}
 catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
std::vector<Resource> load(const std::filesystem::path& p){std::vector<Resource> r;std::string error;if(!load_archive_resources(p,r,error))throw std::runtime_error(error);return r;}
Resource& find(std::vector<Resource>& r,uint32_t type,const std::string& name={}){for(auto& a:r)if(a.type==type&&(name.empty()||a.name==name))return a;throw std::runtime_error("Required appearance resource missing");}
bool ends(const std::string& s,const std::string& tail){return s.size()>=tail.size()&&s.compare(s.size()-tail.size(),tail.size(),tail)==0;}
std::vector<B> parts(const B& b){std::vector<B> out;size_t p=0;while(p+4<=b.size()){uint32_t n=u(b,p);p+=4;if(n==0xffffffff)return out;out.push_back(slice(b,p,n));p+=n;require(out.size()<256,"Too many appearance model chunks");}throw std::runtime_error("Unterminated appearance model");}
std::array<B,3> player(std::vector<Resource>& r){auto& b=find(r,ALD).bytes;std::array<B,3> out;size_t p=0;for(auto& v:out){auto n=u(b,p);p+=4;v=slice(b,p,n);p+=n;}return out;}
std::array<uint8_t,16> checksum(const B& b,size_t start,size_t n){
 const unsigned rotations[]={7,2,5,4,6,3,1,4,2,6,7,3,5,7,1,5},seeds[]={135,85,7,181,75,4,165,174,199,103,190,203,1,80,88,68};
 std::array<uint8_t,16> out{};for(unsigned j=0;j<16;j++){unsigned v=seeds[j];for(size_t at=j;at<n;at+=16)v+=b[start+at];v&=255;out[j]=uint8_t((v<<rotations[j])|(v>>(8-rotations[j])));}return out;
}
B archive(const std::vector<Resource>& records,size_t fixed_logical=0){
 require(records.size()<=4096,"Too many generated appearance resources");B logical(8+80*records.size());put(logical,0,0x00435241);puth(logical,4,8);puth(logical,6,unsigned(records.size()));
 for(size_t i=0;i<records.size();i++){const auto& r=records[i];require(r.name.size()<64&&r.bytes.size()<limit,"Invalid generated appearance resource");size_t a=8+i*80;std::memcpy(logical.data()+a,r.name.data(),r.name.size());uLongf size=compressBound(uLong(r.bytes.size()));B data(size);
  require(compress2(data.data(),&size,r.bytes.data(),uLong(r.bytes.size()),Z_BEST_COMPRESSION)==Z_OK,"Appearance compression failed");data.resize(size);
  put(logical,a+64,r.type);put(logical,a+68,uint32_t(data.size()));put(logical,a+72,uint32_t(r.bytes.size()<<3)|2);put(logical,a+76,uint32_t(logical.size()));append(logical,data);
 }
 if(fixed_logical){require(logical.size()<=fixed_logical,"Generated lens archive exceeds original allocation");logical.resize(fixed_logical);}else logical.resize((logical.size()+15)&~size_t(15));
 B out(16);put(out,0,0x00534648);put(out,4,0x10001);put(out,8,uint32_t(logical.size()));
 for(size_t p=0;p<logical.size();p+=0x1fff0){B chunk=slice(logical,p,std::min(size_t(0x1fff0),logical.size()-p));chunk.resize((chunk.size()+15)&~size_t(15));auto sum=checksum(chunk,0,chunk.size());append(out,chunk);out.insert(out.end(),sum.begin(),sum.end());}return out;
}
B lens(const std::filesystem::path& system){
 B original=read(system/"pl03.arc");require(u(original,0)==0x00534648,"Unsupported original lens archive");auto p=load(system/"pl03.arc"),room=load(system.parent_path()/"rdx_lnk/rm_1002.arc");
 std::string prefix;for(const auto& r:room)if(r.type==0x7db518e8&&ends(r.name,"\\ob_1127")){prefix=r.name;break;}require(!prefix.empty(),"Wesker accessory source missing");
 Resource texture=find(room,TEX,prefix+"\\tex0000_BM");auto extra=find(room,DDS,prefix).bytes,scra=find(room,SCRA,prefix+"_tex").bytes;
 size_t d=0;while(d<p.size()&&p[d].type!=DDS)++d;require(d<p.size(),"Wesker DDS missing");
 require(le(p[d].bytes,8)==12&&le(extra,8)==1,"Unsupported Wesker DDS layout");putle(p[d].bytes,8,13);append(p[d].bytes,slice(extra,12,extra.size()-12));
 auto& s=find(p,SCRA).bytes;require(h(s,6)==13&&scra.size()>=16&&s.size()>=16,"Unsupported Wesker texture script");B tail=slice(s,s.size()-8,8);s.resize(s.size()-8);puth(s,6,14);append(s,slice(scra,8,8));append(s,tail);
 p.insert(p.begin()+d,std::move(texture));B out=archive(p,u(original,8));require(out.size()==original.size(),"Wesker overlay must preserve original file length");return out;
}
struct Node{B raw;V local{},world{};int parent=-1,child=-1,sibling=-1;};
std::vector<Node> rig(const B& mdl){
 unsigned count=h(mdl,6),header=h(mdl,4);uint32_t root=u(mdl,12);require(header==16&&count>0&&count<64,"Unsupported appearance skeleton");std::vector<Node> nodes(count);std::vector<bool> seen(count);
 for(unsigned i=0;i<count;i++){auto& n=nodes[i];n.raw=slice(mdl,header+root+52*i,52);for(unsigned a=0;a<3;a++){n.local[a]=real(n.raw,8+a*4);require(u(n.raw,20+a*4)==0&&std::abs(real(n.raw,32+a*4)-1)<1e-5,"Unsupported appearance bone transform");}
  auto link=[&](size_t p){uint32_t ref=u(n.raw,p);if(ref==0xffffffff)return -1;require(ref>=root&&(ref-root)%52==0&&(ref-root)/52<count,"Invalid appearance bone link");return int((ref-root)/52);};n.child=link(44);n.sibling=link(48);
 }
 auto walk=[&](auto& self,int i,int parent)->void{require(i>=0&&size_t(i)<nodes.size()&&!seen[i],"Cyclic appearance skeleton");seen[i]=true;auto& n=nodes[i];n.parent=parent;for(unsigned a=0;a<3;a++)n.world[a]=n.local[a]+(parent>=0?nodes[parent].world[a]:0);if(n.child>=0)self(self,n.child,i);if(n.sibling>=0)self(self,n.sibling,parent);};walk(walk,0,-1);require(std::all_of(seen.begin(),seen.end(),[](bool v){return v;}),"Disconnected appearance skeleton");return nodes;
}
std::vector<unsigned> mapping(const std::string& id,size_t count){std::vector<unsigned> m;
 if(id=="en51a00")m={0,1,2,3,4,5,7,8,9,11,12,13,14,15,16,18,19,20};
 else if(id.rfind("en01",0)==0)m={0,1,14,15,16,18,19,20,2,3,4,5,7,8,9,11,12,13};
 else if(id=="en26a00")m={0,1,14,15,16,14,18,19,20,18,2,3,4,5,7,8,9,11,12,13};
 else if(id=="en12a00")for(unsigned i=0;i<22;i++)m.push_back(i);
 else if(id=="en95a00")m={0,1,2,3,4,5,5,5,5,5,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
 require(m.size()==count,"Unsupported humanoid skeleton mapping");return m;
}
struct TexName{B name;uint32_t attr;};
std::vector<TexName> texlist(const B& mdl){size_t a=16+u(mdl,8);auto ptr=u(mdl,a),count=u(mdl,a+4);require(count<4096,"Invalid appearance texture count");std::vector<TexName> out;for(unsigned i=0;i<count;i++){size_t p=16+ptr+12*i,n=16+le(mdl,p),end=n;bounds(mdl,n,1);while(end<mdl.size()&&mdl[end])++end;require(end<mdl.size(),"Unterminated appearance texture name");out.push_back({slice(mdl,n,end-n),u(mdl,p+4)});}return out;}
B convert(const B& source,const B& skin,const B& baseline,const std::string& id){
 auto nodes=rig(source),target=rig(baseline);auto map=mapping(id,nodes.size());require(target.size()==22,"Invalid player template rig");std::array<V,22> globals{};std::array<bool,22> assigned{};
 for(size_t i=0;i<nodes.size();i++)if(!assigned[map[i]]){globals[map[i]]=nodes[i].world;assigned[map[i]]=true;}
 for(unsigned i=0;i<22;i++)if(!assigned[i]){require(target[i].parent>=0,"Missing humanoid root");globals[i]=globals[target[i].parent];}
 require(globals[7][0]>.5&&globals[11][0]<-.5&&globals[14][1]<globals[1][1]&&globals[18][1]<globals[1][1]&&globals[9][1]>globals[14][1]&&globals[13][1]>globals[18][1],"Humanoid joint orientation differs");
 require(u(skin,0)==0x534b494e,"Humanoid skin missing");std::vector<B> tables(nodes.size());size_t at=4;
 for(size_t i=0;i<nodes.size();i++){auto flags=u(skin,at);at+=4;require(flags==0||flags==1||flags==3,"Unsupported humanoid skin flags");if(!flags)continue;bounds(skin,at,4);at+=4;if(flags&2){auto n=u(skin,at);at+=4;require(n<65536,"Skin table too large");tables[i]=slice(skin,at,n*4);at+=n*4;}}
 require(skin.size()-at==0||skin.size()-at==4,"Unexpected humanoid skin tail");
 B verts,weights,poly;unsigned vertex_count=0;
 for(size_t i=0;i<nodes.size();i++){auto g=u(nodes[i].raw,4);if(g==0xffffffff)continue;size_t gp=16+g,vp=16+u(source,gp),pp=16+u(source,gp+4);bounds(source,vp,64);require(source[vp]==0x33,"Unsupported humanoid vertex format");unsigned first=h(source,vp+4),count=h(source,vp+6);bounds(source,vp+64+count*32,1);require(source[vp+64+count*32]==255&&vertex_count+count<65536,"Invalid humanoid vertex buffer");auto& table=tables[i];require(table.empty()||table.size()==count*4,"Humanoid skin/vertex mismatch");
  for(unsigned k=0;k<count;k++){B v=slice(source,vp+64+k*32,32);for(unsigned a=0;a<3;a++)putf(v,a*4,real(v,a*4)+nodes[i].world[a]-globals[1][a]);append(verts,v);B w=table.empty()?B{uint8_t(i),9,uint8_t(i),9}:slice(table,k*4,4);require(w[0]<nodes.size()&&(w[2]<nodes.size()||(w[2]==255&&w[3]==0)),"Invalid humanoid weights");w[0]=uint8_t(map[w[0]]);if(w[2]!=255)w[2]=uint8_t(map[w[2]]);append(weights,w);}
  append(poly,B{8,0,0,0});size_t p=pp;bool ended=false;
  for(unsigned guard=0;guard<4096;guard++){bounds(source,p,1);unsigned op=source[p];if(op==255){ended=true;break;}size_t size=op==8?4:4+h(source,p+2)*2;B c=slice(source,p,size);
   if(op==65||op==66){unsigned num=h(c,4);require(!(num>>14),"Unsupported humanoid polygon flags");size_t q=6;
    for(unsigned strip=0;strip<num;strip++){int count_strip=int16_t(h(c,q));unsigned n=unsigned(std::abs(count_strip));q+=2;require(n>=3,"Invalid humanoid strip");for(unsigned j=0;j<n;j++){unsigned old=h(c,q);require(old>=first&&old<first+count,"Humanoid index outside vertex buffer");puth(c,q,vertex_count+old-first);if(op==65)for(unsigned axis:{2u,4u}){int uv=int16_t(h(c,q+axis))*4;require(uv>=-32768&&uv<=32767,"Humanoid UV overflow");puth(c,q+axis,unsigned(uv));}q+=6;}}require(q<=size&&size-q<=2,"Invalid humanoid polygon size");c[0]=66;
   }else require(op==8||op==17||op==18||op==19,"Unknown humanoid polygon opcode");append(poly,c);p+=size;
  }require(ended,"Unterminated humanoid polygons");vertex_count+=count;
 }
 require(vertex_count>0&&weights.size()==vertex_count*4,"Empty humanoid mesh");for(size_t i=0;i<weights.size();i+=2)require(weights[i]!=0,"Humanoid vertex attached to player root");append(poly,B{255,0,0,0});
 B payload(22*52);auto add=[&](const B& b,size_t alignment=4){payload.resize((payload.size()+alignment-1)&~(alignment-1));unsigned p=unsigned(payload.size());append(payload,b);return p;};
 V center{},minimum{},maximum{};for(unsigned a=0;a<3;a++){minimum[a]=maximum[a]=real(verts,a*4);for(unsigned i=1;i<vertex_count;i++){double v=real(verts,i*32+a*4);minimum[a]=std::min(minimum[a],v);maximum[a]=std::max(maximum[a],v);}center[a]=(minimum[a]+maximum[a])*.5;}
 double radius=0;for(unsigned i=0;i<vertex_count;i++){double square=0;for(unsigned a=0;a<3;a++){double d=real(verts,i*32+a*4)-center[a];square+=d*d;}radius=std::max(radius,std::sqrt(square));}require(radius>0,"Invalid humanoid bounds");
 B geometry(24);for(unsigned a=0;a<3;a++)putf(geometry,8+a*4,center[a]);putf(geometry,20,-radius);unsigned geo=add(geometry);B vertices(64);vertices[0]=0x33;puth(vertices,2,unsigned((64+verts.size())/4-1));puth(vertices,6,vertex_count);append(vertices,verts);append(vertices,B{255,0,0,0});unsigned vpos=add(vertices,16),ppos=add(poly);put(payload,geo,vpos);put(payload,geo+4,ppos);
 auto names=texlist(source);unsigned tpos=add(B(8)),entries=add(B(12*names.size()));put(payload,tpos,entries);put(payload,tpos+4,unsigned(names.size()));
 for(size_t j=0;j<names.size();j++){B name=names[j].name;name.push_back(0);unsigned pos=add(name);putle(payload,entries+j*12,pos);putle(payload,entries+j*12+4,names[j].attr);}
 for(unsigned i=0;i<22;i++){B rec=target[i].raw;auto flags=u(rec,0);require((i==1)==!(flags&8),"Unexpected player evaluation flags");put(rec,4,i==1?geo:0xffffffff);for(unsigned a=0;a<3;a++){double p=globals[i][a]-(target[i].parent>=0?globals[target[i].parent][a]:0);require(!(flags&1)||std::abs(p)<1e-4,"Nonzero identity player joint");putf(rec,8+a*4,p);}put(rec,44,target[i].child<0?0xffffffff:target[i].child*52);put(rec,48,target[i].sibling<0?0xffffffff:target[i].sibling*52);std::copy(rec.begin(),rec.end(),payload.begin()+i*52);}
 B mdl(16);put(mdl,0,0x4d444cbc);puth(mdl,4,16);puth(mdl,6,22);put(mdl,8,tpos);payload.resize((payload.size()+3)&~size_t(3));append(mdl,payload);
 B sk{'S','K','I','N'};for(unsigned i=0;i<22;i++){word(sk,i==0?0:i==1?3:1);if(!i)continue;word(sk,0);if(i==1){word(sk,vertex_count);append(sk,weights);}}word(sk,0xffffffff);
 B data(8+12*names.size());put(data,0,8);put(data,4,unsigned(names.size()));for(size_t j=0;j<names.size();j++){unsigned pos=unsigned(data.size());append(data,names[j].name);data.push_back(0);putle(data,8+12*j,pos);putle(data,12+12*j,names[j].attr);}data.resize((data.size()+3)&~size_t(3));B expr(16);put(expr,0,0x4d444c13);puth(expr,4,16);put(expr,12,0xffffffff);append(expr,data);
 B out;for(const auto* b:{&sk,&mdl,&expr,&expr}){word(out,unsigned(b->size()));append(out,*b);}word(out,0xffffffff);require(out.size()<0x30000,"Converted humanoid exceeds player allocation");return out;
}
struct Selection{const char* id;const char* room;};
constexpr Selection selections[]={
 {"en01a00","rm_0070.arc"},{"en01a01","rm_0070.arc"},{"en01a02","rm_0030.arc"},{"en01a03","rm_1070.arc"},{"en01a04","rm_5610.arc"},{"en01a05","rm_3200.arc"},{"en01a06","rm_1120.arc"},{"en01a07","rm_1021.arc"},{"en01a08","rm_1100.arc"},{"en01a09","rm_0020.arc"},{"en01a10","rm_0021.arc"},{"en01a21","rm_9030.arc"},{"en01a31","rm_5520.arc"},{"en01a32","rm_0020.arc"},{"en01a33","rm_5530.arc"},{"en01a34","rm_9030.arc"},{"en01a35","rm_6020.arc"},{"en01a36","rm_6050.arc"},{"en01a37","rm_5610.arc"},{"en01a38","rm_5680.arc"},{"en01a39","rm_3140.arc"},{"en01a40","rm_3050.arc"},{"en01a41","rm_5630.arc"},{"en01a42","rm_5620.arc"},{"en26a00","rm_0110.arc"},{"en51a00","rm_3000.arc"},{"en12a00","rm_5820.arc"},{"en95a00","rm_2030.arc"}};
static_assert(std::size(selections)+original_appearance_count==appearance_count);
B humanoid(const std::filesystem::path& system,unsigned choice){
 const auto& item=selections[choice-original_appearance_count];std::string id=item.id;auto claire=load(system/"pl00.arc"),chris=load(system/"pl01.arc"),room=load(system.parent_path()/"rdx_lnk"/item.room);
 auto base_player=player(claire);B baseline;for(const auto& part:parts(base_player[0]))if(part.size()>=8&&u(part,0)>>8==0x4d444c&&h(part,6)==22){baseline=part;break;}require(!baseline.empty(),"Player conversion template missing");
 std::string name;for(const auto& r:room)if(r.type==ENEMY&&ends(r.name,"\\"+id)){name=r.name;break;}require(!name.empty(),"Selected humanoid missing from room");auto chunks=parts(find(room,ENEMY,name).bytes);B mdl;for(const auto& c:chunks)if(c.size()>=4&&(u(c,0)>>8)==0x4d444c){mdl=c;break;}require(!chunks.empty()&&!mdl.empty(),"Humanoid model chunks missing");B converted=convert(mdl,chunks[0],baseline,id);
 auto& donor=(id.rfind("en12",0)==0||id.rfind("en95",0)==0)?claire:chris;auto bp=player(donor);const auto& primary=find(donor,ALD);const auto& dds=find(room,DDS,name);const auto& scra=find(room,SCRA,name+"_tex");
 std::vector<Resource> records;std::string primary_name=primary.name.substr(0,primary.name.find_last_of('\\')+1)+catalog[choice].archive;B ald;for(const auto* b:{&converted,&bp[1],&bp[2]}){word(ald,unsigned(b->size()));append(ald,*b);}records.push_back({primary_name,ALD,std::move(ald)});
 unsigned count=le(dds.bytes,8);require(count<4096,"Invalid humanoid DDS count");size_t p=12;
 for(unsigned i=0;i<count;i++){unsigned n=le(dds.bytes,p+8);B raw=slice(dds.bytes,p+12,n);auto zero=std::find(raw.begin(),raw.end(),0);std::string tex(raw.begin(),zero);records.push_back(find(room,TEX,tex));p+=20+n;}
 require(h(scra.bytes,6)==count+1,"Humanoid material count differs");records.push_back(dds);records.push_back(scra);return archive(records);
}
bool valid(const std::filesystem::path& p){std::vector<Resource> records;std::string error;return load_archive_resources(p,records,error)&&std::any_of(records.begin(),records.end(),[](const auto& r){return r.type==ALD;});}
}
std::filesystem::path appearance_system_directory(const std::filesystem::path& game_root){
 const auto disc=game_root/"USRDIR/BHCV/nativePS3/biocv_disc";
 for(const auto* lang:{"jpn","usa","eng","fra","ger","ita","spa"}){
  const auto system=disc/lang/"system";std::error_code ec;
  if(std::filesystem::is_regular_file(system/"pl00.arc",ec))return system;
 }
 throw std::runtime_error("Selected game's character system directory not found");
}
std::filesystem::path appearance_cache_root(const std::filesystem::path& system){
 wchar_t exe[32768]{};DWORD n=GetModuleFileNameW(nullptr,exe,32768);require(n&&n<32768,"Cannot locate port folder");
 // Separate installations and languages even when their filenames are equal.
 auto path=std::filesystem::absolute(system).lexically_normal().u8string();uint64_t hash=14695981039346656037ull;for(auto c:path){hash^=uint8_t(c);hash*=1099511628211ull;}std::ostringstream out;out<<std::hex<<hash;
 return std::filesystem::path(exe).parent_path()/"appearance_cache/v1"/out.str();
}
bool prepare_appearance_asset(const std::filesystem::path& system,const std::filesystem::path& root,unsigned choice,std::filesystem::path& result,std::string& error){
 static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
 try{require(choice<appearance_count,"Invalid appearance selection");result=system/(std::string(catalog[choice].archive)+".arc");if(choice<original_appearance_count&&choice!=4){error.clear();return true;}
  std::vector<std::filesystem::path> sources;
  if(choice==4)sources={system/"pl03.arc",system.parent_path()/"rdx_lnk/rm_1002.arc"};
  else sources={system/"pl00.arc",system/"pl01.arc",system.parent_path()/"rdx_lnk"/selections[choice-original_appearance_count].room};
  std::string expected=key(sources);result=root/system.parent_path().filename()/(std::string(catalog[choice].archive)+".arc");auto stamp=result;stamp+=L".key";
  std::ifstream f(stamp);std::string saved;std::getline(f,saved);f.close();if(saved==expected&&valid(result)){error.clear();return true;}
  B generated=choice==4?lens(system):humanoid(system,choice);atomic_write(result,generated);require(valid(result),"Generated appearance archive validation failed");atomic_write(stamp,B(expected.begin(),expected.end()));
  std::fprintf(stderr,"[appearance] prepared %s from selected game in local cache\n",catalog[choice].archive);error.clear();return true;
 }catch(const std::exception& e){error=e.what();result.clear();return false;}
}
}
