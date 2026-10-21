#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_appearance_runtime.h"
#include "cv_appearance_catalog.h"
#include "cv_items.h"
#define NOMINMAX
#include <windows.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cv_appearance_bindings.h"
void func_001DBCA0(ppu_context*);
void cv_original_001DBCA0(ppu_context*);
void func_001DD208(ppu_context*);
void func_001DC3A4(ppu_context*);
void func_00197FC8(ppu_context*);
void func_001E2324(ppu_context*);
void func_00031E14(ppu_context*);
void func_00167350(ppu_context*);
void func_000121AC(ppu_context*);
void func_000322AC(ppu_context*);
void func_0003F7A4(ppu_context*);
void cv_original_0003ECC0(ppu_context*);
void cv_original_0004A3F8(ppu_context*);
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" uint8_t* vm_base;
// The lift supplies its own compatible ppu_context declaration; including the
// runtime's full syscall header here would define that structure twice.
extern "C" int64_t sys_memory_allocate(ppu_context*);

namespace cv_appearance { namespace {
// One allocation per cached appearance: no room-heap storage or guessed guest addresses.
constexpr uint32_t kCapacity=0x200000,kModelOffset=0x100000,kWorkOffset=0x130000,kJointOffset=0x131000;
constexpr uint32_t kJointBytes=22*72;
constexpr uint8_t kLoaderCanary=0xA5;constexpr uint32_t kLoaderCanaryBytes=0x10000;
struct Snapshot {
 uint32_t actor=0,skin=0,object=0,textures=0;
 std::array<uint32_t,6> model{};
 std::array<uint32_t,2> expression_textures{},expression_work_textures{};
 unsigned auxiliary_count=0;
 std::array<uint32_t,12> auxiliary_objects{},auxiliary_work_objects{},auxiliary_textures{},auxiliary_work_textures{};
};
struct HiddenNode {uint32_t address,flags;};
std::vector<HiddenNode> hidden_nodes,hidden_polygons;
uint32_t glasses_entity=0;
uint32_t motion_destination=0;
std::array<uint8_t,100*24> saved_motions{};
Snapshot original;
struct Visual {
 uint32_t storage=0,variant_skin=0,variant_archive=0;
 uint32_t capacity=kCapacity,model_offset=kModelOffset,work_offset=kWorkOffset,joint_offset=kJointOffset;
 uint32_t motion_offset=0x140000,table_offset=0x190000,bank_offset=0x1a0000;
 std::array<uint32_t,6> variant{};
 std::array<uint32_t,2> variant_expressions{};
 std::array<size_t,2> expression_offsets{};
 std::vector<uint8_t> model_bytes,motion_bytes;
 std::array<uint32_t,100> motion_offsets{};
 std::array<bool,100> motion_valid{};
 uint32_t motion_table=0,glasses_work=0;
 size_t mdl_offset=0,skin_offset=0,glasses_offset=0;
 uint32_t glasses_object=0,glasses_textures=0;
 bool ready=false;
};
Visual visuals[appearance_count];
unsigned desired=0,current=0;
bool loading=false,failed=false;
Visual& visual(){return visuals[desired];}
thread_local bool inside_frame=false;
thread_local bool custom_path_scope=false;
std::chrono::steady_clock::time_point requested_at;
std::string message="Select an appearance to apply";

bool mapped(uint32_t address,size_t size) {
 if(!vm_base||!address||size>0x100000000ull-address)return false;
 const auto* p=vm_base+address;const auto* end=p+size;
 while(p<end){MEMORY_BASIC_INFORMATION m{};
  if(!VirtualQuery(p,&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
  const auto* next=static_cast<const uint8_t*>(m.BaseAddress)+m.RegionSize;
  if(next<=p)return false;p=next;
 }return true;
}
void hide_tree(uint32_t source,unsigned count){
 if(!count||count>64||!mapped(source,count*52))return;
 for(unsigned n=0;n<count;n++){
  const uint32_t node=source+n*52;bool known=false;
  for(const auto& item:hidden_nodes)if(item.address==node){known=true;break;}
  if(!known)hidden_nodes.push_back({node,vm_read32(node)});
  vm_write32(node,vm_read32(node)|8u);
  const uint32_t geometry=vm_read32(node+4);if(!mapped(geometry,8))continue;
  const uint32_t polygon=vm_read32(geometry+4);if(!mapped(polygon,4))continue;
  known=false;for(const auto& item:hidden_polygons)if(item.address==polygon){known=true;break;}
  if(!known)hidden_polygons.push_back({polygon,vm_read32(polygon)});
  vm_write32(polygon,vm_read32(polygon)|0xff000000u);
 }
}
void hide_attached_claire_hair(uint32_t actor){
 constexpr uint32_t pool=kEntityPool;
 if(!mapped(pool,512*0x4e4))return;
 for(unsigned i=0;i<512;i++){
  const uint32_t e=pool+i*0x4e4;
  if(e==glasses_entity||!(vm_read32(e)&1)||vm_read32(e+0x2e4)!=actor||vm_read32(e+0x2e0)!=5)continue;
  const uint32_t work=vm_read32(e+0x2d8);if(!mapped(work,24))continue;
  hide_tree(vm_read32(work+12),vm_read32(work+4));
 }
}
struct Call {
 ppu_context c;ppu_context* previous=g_active_ctx;
 void (*tail)(void*)=g_trampoline_fn;
 explicit Call(ppu_context* parent):c(*parent){c.gpr[1]-=0x2000;vm_write64(c.gpr[1],parent->gpr[1]);c.gpr[2]=CV_TOC;g_active_ctx=&c;g_trampoline_fn=nullptr;}
 ~Call(){g_active_ctx=previous;g_trampoline_fn=tail;}
 uint32_t run(void (*fn)(ppu_context*),uint32_t a=0,uint32_t b=0,uint32_t d=0,uint32_t e=0){
  c.gpr[3]=a;c.gpr[4]=b;c.gpr[5]=d;c.gpr[6]=e;
  fn(&c);while(g_trampoline_fn){auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&c);}return uint32_t(c.gpr[3]);
 }
};
uint32_t be32(const std::vector<uint8_t>& b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool normalize_steve_uvs(){
 auto& bytes=visual().model_bytes;const size_t base=visual().mdl_offset;
 auto short_at=[&](size_t at)->unsigned {if(at+2>bytes.size())return 0;return unsigned(bytes[at])*256+bytes[at+1];};
 auto put=[&](size_t at,unsigned v){bytes[at]=uint8_t(v>>8);bytes[at+1]=uint8_t(v);};
 if(base+16>bytes.size())return false;
 const size_t header=base+short_at(base+4),root=be32(bytes,base+12);unsigned converted=0;
 for(unsigned i=0;i<22;i++){
  const size_t node=header+root+i*52;if(node+52>bytes.size())return false;
  const uint32_t geometry=be32(bytes,node+4);if(geometry==0xffffffffu)continue;
  if(header+geometry+8>bytes.size())return false;
  size_t at=header+be32(bytes,header+geometry+4);
  for(unsigned n=0;n<4096;n++){
   if(at+2>bytes.size())return false;
   const unsigned op=bytes[at];if(op==255)break;
   if(op==8){at+=4;continue;}
   if(at+4>bytes.size())return false;
   const size_t end=at+4+short_at(at+2)*2;if(end>bytes.size())return false;
   if(op==65){
    const unsigned strips=short_at(at+4)&0x3fff;size_t p=at+6;
    for(unsigned j=0;j<strips;j++){
     if(p+2>end)return false;const unsigned count=unsigned(std::abs(int(int16_t(short_at(p)))));p+=2;
     if(count>(end-p)/6)return false;
     for(unsigned k=0;k<count;k++,p+=6)for(unsigned axis:{2u,4u}){
      const int uv=int(int16_t(short_at(p+axis)))*4;if(uv < -32768||uv>32767)return false;put(p+axis,unsigned(uv));
     }
    }
    bytes[at]=66;++converted;
   }
   at=end;
  }
 }
 std::fprintf(stderr,"[appearance] Steve UVN strips normalized to UVH: %u\n",converted);return true;
}
bool prepare_motion(const PlayerAsset* asset){
 if(!catalog[desired].male_motion)return true;
 if(asset->motion.size()>0x40000)return false;
 visual().motion_bytes=asset->motion;size_t p=0;unsigned count=0;
 while(p+4<=asset->motion.size()){
  const size_t n=be32(asset->motion,p);p+=4;if(n==0xffffffffu)break;
  if(count>=100||n>asset->motion.size()-p)return false;
  visual().motion_offsets[count]=uint32_t(p);
  visual().motion_valid[count]=n>=16&&asset->motion[p]=='M'&&asset->motion[p+1]=='T'&&asset->motion[p+2]=='N'&&asset->motion[p+6]==0&&asset->motion[p+7]==22;
  p+=n;++count;
 }
 return count==100;
}
// The equipment already supplies posed gripping hands. Enemy meshes also
// contain open hands; drawing both produces extra fingers through the knife.
// Only the runtime copy is trimmed; the menu keeps the complete original mesh.
// Alexia (human) has no hands of her own, so nothing is removed from her.
bool equipment_hands(){
 if(desired<original_appearance_count)return true;
 auto& b=visual().model_bytes;
 auto h=[&](size_t p){return uint16_t(unsigned(b[p])*256+b[p+1]);};
 auto put=[&](std::vector<uint8_t>& out,unsigned v){out.push_back(uint8_t(v>>8));out.push_back(uint8_t(v));};
 auto word=[&](size_t p,uint32_t v){for(unsigned i=0;i<4;i++)b[p+i]=uint8_t(v>>(24-i*8));};
 // Converted bodies follow the player layout: hip node1 owns the merged
 // geometry, and SKIN has no root entry before node1's influence table.
 const size_t base=visual().mdl_offset+16,root=base+be32(b,visual().mdl_offset+12);
 if(root+2*52>b.size())return false;
 const size_t geo=base+be32(b,root+52+4),vertices=base+be32(b,geo),polygons=base+be32(b,geo+4);
 const size_t skin=visual().skin_offset;
 if(geo+24>b.size()||vertices+64>b.size()||skin+16>b.size()||be32(b,skin)!=0||be32(b,skin+4)!=3)return false;
 const unsigned count=be32(b,skin+12),first=h(vertices+4);
 const size_t weights=skin+16;
 if(weights+size_t(count)*4>b.size()||count!=h(vertices+6))return false;
 auto hand=[&](unsigned ix){if(ix<first||ix>=first+count)return false;
  const unsigned joint=b[weights+(ix-first)*4];return joint==9||joint==13;};
 std::vector<uint8_t> out;size_t at=polygons;unsigned removed=0;bool ended=false;
 for(unsigned chunk=0;chunk<4096;chunk++){
  if(at+4>b.size())return false;
  const unsigned op=b[at];if(op==255){ended=true;break;}
  const size_t size=op==8?4:4+size_t(h(at+2))*2;
  if(size>b.size()-at)return false;
  if(op!=65&&op!=66){out.insert(out.end(),b.begin()+at,b.begin()+at+size);at+=size;continue;}
  // Keep the original strips and cut them only around hand triangles. Every
  // strip is a separate draw in the six-face lighter/muzzle-flash shadow
  // pass; one strip per triangle (~10k draws) turned the screen white.
  size_t q=at+6;const unsigned header=h(at+4),strips=header&0x3fff;
  std::vector<uint8_t> body;unsigned kept_strips=0;
  for(unsigned strip=0;strip<strips;strip++){
   if(q+2>at+size)return false;
   const int signed_count=int16_t(h(q));const unsigned n=unsigned(std::abs(signed_count));q+=2;
   if(n<3||size_t(n)*6>at+size-q)return false;
   auto emit=[&](unsigned first_triangle,unsigned last_triangle){
    // Vertices first_triangle-2..last_triangle. Starting on an odd vertex
    // flips the strip's winding parity, so the sign compensates for it.
    const unsigned start=first_triangle-2,count=last_triangle-start+1;
    const bool negative=(signed_count<0)!=bool(start&1);
    put(body,unsigned(uint16_t(negative?-int(count):int(count))));
    body.insert(body.end(),b.begin()+q+start*6,b.begin()+q+(last_triangle+1)*6);++kept_strips;
   };
   unsigned run=0;
   for(unsigned k=2;k<n;k++){
    if(hand(h(q+(k-2)*6))&&hand(h(q+(k-1)*6))&&hand(h(q+k*6))){
     ++removed;if(run)emit(run,k-1);run=0;
    }else if(!run)run=k;
   }
   if(run)emit(run,n-1);
   q+=n*6;
  }
  at+=size;
  if(!kept_strips)continue;
  if(kept_strips>0x3fff)return false;
  // Same opcode/flags; pad so the chunk keeps a 4-byte multiple length.
  if((body.size()+2)%4)body.insert(body.end(),{0,0});
  out.push_back(uint8_t(op));out.push_back(b[at-size+1]);put(out,unsigned((body.size()+2)/2));
  put(out,(header&0xc000)|kept_strips);out.insert(out.end(),body.begin(),body.end());
 }
 if(!ended)return false;
 if(!removed)return true;
 out.insert(out.end(),{255,0,0,0});
 b.resize((b.size()+3)&~size_t(3));const size_t destination=b.size();
 if(destination+out.size()>0x30000)return false;
 b.insert(b.end(),out.begin(),out.end());word(geo+4,uint32_t(destination-base));
 std::fprintf(stderr,"[appearance] equipment grip: omitted %u open-hand triangles; preview preserved\n",removed);
 return true;
}
bool prepare(const PlayerAsset* asset){
 if(!asset||asset->body_nodes!=22||asset->model.size()>0x30000)return false;
 size_t total=0;
 for(const auto& resource:asset->resources){
  if(resource.bytes.size()>0xf0000-total)return false;total+=resource.bytes.size();
 }
 if(desired>=original_appearance_count){
  auto page=[](size_t n){return uint32_t((n+0xffffu)&~size_t(0xffffu));};
  visual().model_offset=page(total+0x1000);
  // Room for a private polygon stream used by equipment hands.
  visual().work_offset=visual().model_offset+0x30000;
  visual().joint_offset=visual().work_offset+0x1000;
  visual().motion_offset=visual().work_offset+0x10000;
  visual().table_offset=visual().motion_offset+0x40000;
  visual().bank_offset=visual().table_offset+0x10000;
  visual().capacity=page(visual().table_offset+100*24);
 }
 size_t p=0;bool skin=false;unsigned bodies=0;
 while(p+4<=asset->model.size()){
  const size_t n=be32(asset->model,p);p+=4;
  if(n==0xffffffffu||n>asset->model.size()-p)break;
  if(n>=4&&be32(asset->model,p)==0x534b494e&&bodies==0){visual().skin_offset=p+4;skin=true;}
  if(n>=16&&asset->model[p]=='M'&&asset->model[p+1]=='D'&&asset->model[p+2]=='L'){
   const unsigned nodes=unsigned(asset->model[p+6])*256+asset->model[p+7];
   if(bodies==0){if(!skin||nodes!=22)return false;visual().mdl_offset=p;}
   else if(bodies<=2){
    // Texture-only MDLs for the two blink states. Unlike the original
    // outfit (five textures), pl04 has six entries in ALL three states.
    if(nodes!=0||asset->model[p+3]!=0x13||be32(asset->model,p+12)!=0xffffffffu)return false;
    visual().expression_offsets[bodies-1]=p;
   }
   if(bodies==4){
    if(nodes!=1)return false;visual().glasses_offset=p;
   }
   ++bodies;
   if((desired!=4&&bodies==3)||(desired==4&&bodies==5)){visual().model_bytes=asset->model;return (desired!=3||normalize_steve_uvs())&&equipment_hands()&&prepare_motion(asset);}
  }p+=n;
 }return false;
}

bool claire(uint32_t& sys,uint32_t& actor){
 if(!mapped(kSys,4)||!mapped(kPlayer,4))return false;
 sys=vm_read32(kSys);actor=vm_read32(kPlayer);
 if(!mapped(sys,0x28a88)||!mapped(actor,0x380))return false;
 // This is cosmetic Claire only. Official costume flags also affect scripts;
 // they are never written, nor are character, animation, or inventory fields.
 // Pause blocks the room task in +0x68 without unloading the actor.
 // Validate the scheduled room task; frame() separately gates live gameplay.
 const uint32_t tasks=vm_read32(sys+0x64);
 return (tasks&0x80)&&int32_t(vm_read32(CV_SRT_HEALTH))>0&&
        vm_read8(sys+0x94)==0&&vm_read32(sys+0x28a84)==0&&
        vm_read32(CV_SRT_RDX)==0x3d650&&vm_read32(actor+0x90)>0&&
        vm_read32(actor+0x90)<=16&&vm_read32(actor+0x15c)==22;
}
void restore(){
 if(!current)return;
 for(const auto& node:hidden_nodes)if(mapped(node.address,4))vm_write32(node.address,node.flags);
 hidden_nodes.clear();
 for(const auto& polygon:hidden_polygons)if(mapped(polygon.address,4))vm_write32(polygon.address,polygon.flags);
 hidden_polygons.clear();
 if(motion_destination&&mapped(motion_destination,saved_motions.size()))std::memcpy(vm_base+motion_destination,saved_motions.data(),saved_motions.size());
 motion_destination=0;
 if(glasses_entity&&mapped(glasses_entity,0x4e4)&&vm_read32(glasses_entity+0x2d8)==visual().glasses_work)vm_write32(glasses_entity,0);
 glasses_entity=0;
 const uint32_t a=original.actor;
 if(mapped(a,0x1a0)&&vm_read32(a+0x164)==visual().variant[3]){
  for(unsigned i=0;i<6;i++)vm_write32(a+0x158+i*4,original.model[i]);
  for(unsigned i=0;i<2;i++){
   vm_write32(a+0x11c+i*4,original.expression_textures[i]);
   vm_write32(a+0x180+i*24,original.expression_work_textures[i]);
  }
  for(unsigned i=0;i<original.auxiliary_count;i++){
   vm_write32(a+0xe8+i*4,original.auxiliary_objects[i]);
   vm_write32(a+0x1c4+i*24,original.auxiliary_work_objects[i]);
   vm_write32(a+0x128+i*4,original.auxiliary_textures[i]);
   vm_write32(a+0x1c8+i*24,original.auxiliary_work_textures[i]);
  }
  vm_write32(a+0x98,original.skin);vm_write32(a+0xd8,original.object);vm_write32(a+0x118,original.textures);
 }
 current=0;
}
// The auxiliary model pass tests entity+0x88 bit 0x20 before using +0x118.
// Without this selector it treats material12 as a room-particle texture index,
// ignoring our registered WKGRAS2 bank entirely (00042850 in the JP lift).
void bind_glasses_bank(){
 vm_write32(glasses_entity+0x118,visual().glasses_textures);
 vm_write32(glasses_entity+0x88,vm_read32(glasses_entity+0x88)|0x20u);
}
void install(ppu_context* ctx,uint32_t actor){
 if(current||!visual().ready)return;
 if(vm_read32(actor+0x90)<3||vm_read32(actor+0x174)!=0||vm_read32(actor+0x18c)!=0)return;
 if(desired>=2){
  if(vm_read32(actor+0x90)<4)return;
  for(unsigned i=4;i<vm_read32(actor+0x90);i++){
   const uint32_t slot=actor+0x158+i*24,count=vm_read32(slot+4),source=vm_read32(slot+12);
   if(count>64||(count&&!mapped(source,count*52)))return;
   for(unsigned n=0;n<count;n++)for(unsigned link:{44u,48u}){
    const uint32_t target=vm_read32(source+n*52+link);
    if(target&&(target<source||target>=source+count*52||(target-source)%52))return;
   }
  }
 }
 original.actor=actor;original.auxiliary_count=0;
 for(unsigned i=0;i<2;i++){
  original.expression_textures[i]=vm_read32(actor+0x11c+i*4);
  original.expression_work_textures[i]=vm_read32(actor+0x180+i*24);
 }
 for(unsigned i=0;i<6;i++)original.model[i]=vm_read32(actor+0x158+i*4);
 original.skin=vm_read32(actor+0x98);original.object=vm_read32(actor+0xd8);original.textures=vm_read32(actor+0x118);
 // Give the variant its own joint work. Its renderer must not mutate the
 // work owned by the original body, which must remain available for restore.
 if(!mapped(original.model[5],kJointBytes)){original={};return;}
 if(desired>=original_appearance_count){
  // Weapon/arm IK retains references to the player's live joint work. A
  // one-time snapshot leaves the replacement skin on stale arm matrices.
  visual().variant[5]=original.model[5];
 }else{
  visual().variant[5]=visual().storage+visual().joint_offset;
  std::memcpy(vm_base+visual().variant[5],vm_base+original.model[5],kJointBytes);
 }
 for(unsigned i=0;i<6;i++)vm_write32(actor+0x158+i*4,visual().variant[i]);
 for(unsigned i=0;i<2;i++){
  vm_write32(actor+0x11c+i*4,visual().variant_expressions[i]);
  vm_write32(actor+0x180+i*24,visual().variant_expressions[i]);
 }
 vm_write32(actor+0x98,visual().variant_skin);vm_write32(actor+0xd8,visual().variant[3]);vm_write32(actor+0x118,visual().variant[4]);
 if(desired>=2){
  // Auxiliary objects retain these source trees independently of the actor's
  // descriptors. Hide the actual geometry, without destroying skin/physics data.
  for(unsigned i=4;i<vm_read32(actor+0x90);i++){
   const uint32_t slot=actor+0x158+i*24,source=vm_read32(slot+12),count=vm_read32(slot+4);
   hide_tree(source,count);
  }
  hide_attached_claire_hair(actor);
  // Keep the descriptor addresses stable: current animation contexts can refer
  // directly to them. Replace only compatible entries and restore byte-for-byte.
  const uint32_t base=vm_read32(actor+0x360);
  if(mapped(base,saved_motions.size())){
   motion_destination=base;std::memcpy(saved_motions.data(),vm_base+base,saved_motions.size());
   unsigned replaced=0;
   for(unsigned i=0;i<100;i++)if(visual().motion_valid[i]){
    std::memcpy(vm_base+base+i*24,vm_base+visual().motion_table+i*24,24);++replaced;
   }
   std::fprintf(stderr,"[appearance] native motion entries installed: %u/100 base=%08X\n",replaced,base);
  }
 }
 if(desired==4){
  Call call(ctx);const uint32_t position=uint32_t(call.c.gpr[1])+0x100;
  // Constructor reads six floats: world position and parent-local position.
  for(unsigned i=0;i<6;i++)vm_write32(position+i*4,0);
  const int index=int32_t(call.run(func_0003F7A4,0,position,actor,5));
  if(index>=0&&index<512){
   glasses_entity=kEntityPool+uint32_t(index)*0x4e4;
   vm_write32(glasses_entity+0x2d8,visual().glasses_work);
   bind_glasses_bank();
   std::fprintf(stderr,"[appearance] independent head accessory entity=%08X parent=%08X joint=5 bank-selector=0x20\n",glasses_entity,actor);
  }else std::fprintf(stderr,"[appearance] no accessory entity slot available\n");
 }
 current=desired;message="Appearance applied";
 std::fprintf(stderr,"[appearance] visual body and both blink texture states applied actor=%08X; campaign identity retained; selected motion table active\n",actor);
}
void fail(const char* why){failed=true;loading=false;message=why;std::fprintf(stderr,"[appearance] %s\n",why);}
bool allocate(Call& call){
 if(visual().storage)return true;
 const uint32_t result=uint32_t(call.c.gpr[1])+0x100;
 // Keep every cached model's addresses stable, but reserve its measured
 // footprint on 64 KiB pages instead of wasting 2 MiB per menu entry.
 if(desired>=original_appearance_count){
  if(!visual().model_offset||visual().capacity>kCapacity)return false;
 }
 call.c.gpr[3]=visual().capacity;call.c.gpr[4]=0x200;call.c.gpr[5]=result;
 if(sys_memory_allocate(&call.c)!=0)return false;
 visual().storage=vm_read32(result);return mapped(visual().storage,visual().capacity);
}
bool right_resource(Call& call,uint32_t sys){
 const uint32_t strings=uint32_t(call.c.gpr[1])+0x200;
 call.run(func_001DBCA0,vm_read32(sys+0x30),catalog[desired].file_id,strings);
 // File IDs come from the game's own system-archive table; check the
 // resolved entry as well, rather than assuming IDs alone establish identity.
 for(unsigned at:{0u,0x40u}){
  if(!mapped(strings+at,0x40))return false;
  const char* s=reinterpret_cast<const char*>(vm_base+strings+at);
  const void* end=std::memchr(s,0,0x40);
  if(!end)continue;
  std::string name(s,static_cast<const char*>(end));
  const auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name.erase(0,slash+1);
  if(name==catalog[desired].archive||name==std::string(catalog[desired].archive)+".arc")return true;
 }return false;
}
bool realize(Call& call,uint32_t list){
 if(!mapped(list,0x68))return false;
 // These host-decoded assets retain the PS3 big-endian layout.
 if(vm_read32(kEndian)!=0)return false;
 const uint32_t model=visual().storage+visual().model_offset,work=visual().storage+visual().work_offset;
 if(desired>=original_appearance_count){
  for(uint32_t i=0;i<kLoaderCanaryBytes;i++)if(vm_base[model+i]!=kLoaderCanary){
   std::fprintf(stderr,"[appearance] archive load exceeded reserved area at +%X (model area %X)\n",i,visual().model_offset);
   return false;
  }
 }
 std::memcpy(vm_base+model,visual().model_bytes.data(),visual().model_bytes.size());
 std::memset(vm_base+work,0,24);
 call.run(func_00031E14,model+uint32_t(visual().mdl_offset),work);
 if(vm_read32(work+4)!=22||!mapped(vm_read32(work+12),52*22)||!mapped(vm_read32(work+16),8))return false;
 if(desired>=original_appearance_count){
  unsigned geometries=0;
  const uint32_t begin=model+uint32_t(visual().mdl_offset),end=model+uint32_t(visual().model_bytes.size());
  const uint32_t nodes=vm_read32(work+12);
  for(unsigned i=0;i<22;i++){
   const uint32_t geometry=vm_read32(nodes+i*52+4);if(!geometry)continue;
   if(geometry<begin||geometry>end-24)return false;
   const uint32_t vertices=vm_read32(geometry),polygons=vm_read32(geometry+4);
   if(vertices<begin||vertices>end-64||polygons<begin||polygons>end-4)return false;
   const unsigned count=vm_read16(vertices+6);
   if(!count||size_t(count)*32>end-vertices-64)return false;
   ++geometries;
  }
  if(!geometries)return false;
  std::fprintf(stderr,"[appearance] converted geometry relocation validated: %u buffers\n",geometries);
 }
 visual().variant_skin=model+uint32_t(visual().skin_offset);
 call.run(func_00167350,vm_read32(work+12),visual().variant_skin);
 if(desired==4){
  // Register one complete bank; the accessory shares this exact pointer
  // and its material opcode selects the original WKGRAS2 entry12.
  const uint32_t source=vm_read32(work+16),full=visual().storage+visual().bank_offset,entries=full+0x20;
  if(!mapped(source,8)||vm_read32(source+4)!=12||!mapped(vm_read32(source),12*12))return false;
  std::memcpy(vm_base+entries,vm_base+vm_read32(source),12*12);
  std::memset(vm_base+entries+12*12,0,12);
  std::memcpy(vm_base+full+0x100,"WKGRAS2",8);
  vm_write32(entries+12*12,full+0x100);
  vm_write32(full,entries);vm_write32(full+4,13);vm_write32(work+16,full);
 }
 if(!call.run(func_000121AC,vm_read32(work+16),list,0,1))return false;
 vm_write32(work,vm_read32(work)|0x200);
 for(unsigned i=0;i<6;i++)visual().variant[i]=vm_read32(work+i*4);
 // Realize and register both texture-only expression lists from this
 // same archive. Leaving pl00's lists here caused all body materials to
 // switch to the five-entry original table whenever Claire blinked.
 for(unsigned i=0;i<2;i++){
  const uint32_t expression_work=work+24*(i+1);
  std::memset(vm_base+expression_work,0,24);
  call.run(func_00031E14,model+uint32_t(visual().expression_offsets[i]),expression_work);
  visual().variant_expressions[i]=vm_read32(expression_work+16);
  if(vm_read32(expression_work+4)!=0||!mapped(visual().variant_expressions[i],8))return false;
  if(!call.run(func_000121AC,visual().variant_expressions[i],list,0,1))return false;
 }
 std::fprintf(stderr,"[appearance] texture states base=%08X blinkA=%08X blinkB=%08X\n",visual().variant[4],visual().variant_expressions[0],visual().variant_expressions[1]);
 if(desired==4){
  const uint32_t glasses_work=work+72;visual().glasses_work=glasses_work;
  std::memset(vm_base+glasses_work,0,24);
  call.run(func_00031E14,model+uint32_t(visual().glasses_offset),glasses_work);
  visual().glasses_object=vm_read32(glasses_work+12);
  visual().glasses_textures=vm_read32(glasses_work+16);
  if(vm_read32(glasses_work+4)!=1||!mapped(visual().glasses_object,52)||!mapped(visual().glasses_textures,8))return false;
  const uint32_t geometry=vm_read32(visual().glasses_object+4);
  if(!mapped(geometry,8))return false;
  const uint32_t vertices=vm_read32(geometry);
  if(!mapped(vertices,64)||vm_read8(vertices)!=0x33)return false;
  const unsigned count=vm_read16(vertices+6);
  if(!mapped(vertices+64,size_t(count)*32))return false;
  for(unsigned i=0;i<count;i++)for(unsigned axis:{1u,2u}){
   const uint32_t at=vertices+64+i*32+axis*4;uint32_t bits=vm_read32(at);float f;
   std::memcpy(&f,&bits,4);f+=axis==1?.87f:-1.05f;std::memcpy(&bits,&f,4);vm_write32(at,bits);
  }
  // 000121AC registers a TEXLIST pointer -> DDS resource association; it
  // does not fill TEXNAME.texaddr. Reuse the registered complete bank and
  // remap the accessory's local material0 to the original lens at DDS12.
  const uint32_t polygon=vm_read32(geometry+4);uint32_t at=polygon;
  bool remapped=false,ended=false;
  for(unsigned chunk=0;chunk<256;chunk++){
   if(!mapped(at,4))return false;
   const unsigned op=vm_read8(at);
   if(op==255){ended=true;break;}
   if(op==8){
    const uint16_t flags=vm_read16(at+2);
    if((flags&0x1fff)!=0)return false;
    vm_write16(at+2,(flags&0xe000)|12);at+=4;remapped=true;
   }else if(op==17||op==19||op==65||op==66){
    const uint32_t bytes=4+uint32_t(vm_read16(at+2))*2;
    if(bytes>0x10000||!mapped(at,bytes))return false;at+=bytes;
   }else return false;
  }
  if(!ended||!remapped)return false;
  visual().glasses_textures=visual().variant[4];
  vm_write32(glasses_work+16,visual().glasses_textures);
  std::fprintf(stderr,"[appearance] WKGRAS2 accessory uses registered bank=%08X material=12\n",visual().glasses_textures);
  vm_write32(glasses_work,vm_read32(glasses_work)|0x200);
  const uint32_t own_work=visual().storage+visual().joint_offset+22*72;
  std::memset(vm_base+own_work,0,72);vm_write32(glasses_work+20,own_work);
 }
 if(catalog[desired].male_motion){
  const uint32_t motion=visual().storage+visual().motion_offset;
  visual().motion_table=visual().storage+visual().table_offset;
  std::memcpy(vm_base+motion,visual().motion_bytes.data(),visual().motion_bytes.size());
  std::memset(vm_base+visual().motion_table,0,100*24);
  for(unsigned i=0;i<100;i++)if(visual().motion_valid[i])
   call.run(func_000322AC,motion+visual().motion_offsets[i],visual().motion_table+i*24);
 }
 visual().ready=true;return true;
}
} // namespace
unsigned applied(){return current;}
bool busy(){return loading;}
const char* status(){return message.c_str();}
bool request(ppu_context* ctx,unsigned choice,const PlayerAsset* asset){
 std::fprintf(stderr,"[appearance] request choice=%u asset=%u loading=%u current=%u\n",choice,unsigned(asset!=nullptr),unsigned(loading),current);
 if(choice>=appearance_count||loading)return false;
 if(choice!=desired){restore();desired=choice;}
 if(!choice){failed=false;restore();message="Original appearance applied";return true;}
 if(!prepare(asset)){failed=true;message="Incompatible model; original appearance retained";return false;}
 failed=false;
 uint32_t sys=0,actor=0;
 if(!claire(sys,actor)){
  message="Selected; applies when Claire enters gameplay";
  std::fprintf(stderr,"[appearance] selection queued until Claire is loaded sys=%08X actor=%08X\n",sys,actor);
  return true;
 }
 if(visual().ready){install(ctx,actor);return true;}
 message="Loading appearance...";frame(ctx,true);return !failed;
}
void frame(ppu_context* ctx,bool in_menu){
 if(!desired||failed||inside_frame)return;
 struct Guard{Guard(){inside_frame=true;}~Guard(){inside_frame=false;}} guard;
 if(!in_menu&&!cv_items::read().playing)return;
 uint32_t sys=0,actor=0;
 if(!claire(sys,actor))return;
 if(current){if(actor!=original.actor||vm_read32(actor+0x164)!=visual().variant[3]){restore();original={};}else {
  // Rooms can clear/recycle the auxiliary object pool without replacing
  // the player's body. Reinstall only if our own attachment was lost.
  if(current==4&&(!mapped(glasses_entity,0x4e4)||!(vm_read32(glasses_entity)&1)||
     vm_read32(glasses_entity+0x2d8)!=visual().glasses_work||
     vm_read32(glasses_entity+0x2e4)!=actor)){
   Call call(ctx);const uint32_t position=uint32_t(call.c.gpr[1])+0x100;
   for(unsigned i=0;i<6;i++)vm_write32(position+i*4,0);
   const int index=int32_t(call.run(func_0003F7A4,0,position,actor,5));
   if(index>=0&&index<512){
    glasses_entity=kEntityPool+uint32_t(index)*0x4e4;
    vm_write32(glasses_entity+0x2d8,visual().glasses_work);
    bind_glasses_bank();
    std::fprintf(stderr,"[appearance] restored head accessory after pool reset entity=%08X\n",glasses_entity);
   }
  }
  if(desired>=2)hide_attached_claire_hair(actor);
  for(const auto& node:hidden_nodes)vm_write32(node.address,vm_read32(node.address)|8u);
  return;
 }}
 if(visual().ready){install(ctx,actor);return;}
 if(loading&&std::chrono::steady_clock::now()-requested_at>std::chrono::seconds(15)){
  fail("Loading timed out; original appearance retained");return;
 }
 Call call(ctx);
 // Only our synchronous path-resolution calls can borrow the pl04 ALD slot.
 // The game always sees the original path outside this scope.
 struct PathScope {bool previous=custom_path_scope;PathScope(){custom_path_scope=desired>=original_appearance_count;}~PathScope(){custom_path_scope=previous;}} path_scope;
 if(!loading){
  if(vm_read32(sys+0x1b108)!=0)return;
  if(call.run(func_00197FC8)!=0)return;
  if(!allocate(call)){fail("Memory unavailable; original appearance retained");return;}
  if(!right_resource(call,sys)){fail("Appearance archive not confirmed; original retained");return;}
  if(!call.run(func_001DD208,vm_read32(sys+0x30),catalog[desired].file_id))return;
  // ALD load normally releases the root at AB01A0. That root still owns
  // Claire's original body AND the face/hair used by both appearances. Move
  // its reference out of the loader slot, retain the returned alternate root
  // ourselves, and restore the engine slot before returning to the game.
  // No guessed refcount increments and no root is released while in use.
  // Converted models reserve only their measured archive footprint. Mark the
  // start of the model area so realize() can reject a load that overran it.
  if(desired>=original_appearance_count)
   std::memset(vm_base+visual().storage+visual().model_offset,kLoaderCanary,kLoaderCanaryBytes);
  const uint32_t original_archive=vm_read32(kPlayerArchive);
  vm_write32(kPlayerArchive,0);
  call.run(func_001DC3A4,vm_read32(sys+0x30),catalog[desired].file_id,visual().storage);
  visual().variant_archive=vm_read32(kPlayerArchive);
  vm_write32(kPlayerArchive,original_archive);
  std::fprintf(stderr,"[appearance] archive ownership original=%08X alternate=%08X\n",original_archive,visual().variant_archive);
  if(!visual().variant_archive){fail("Resource load failed; original retained");return;}
  loading=true;requested_at=std::chrono::steady_clock::now();
  std::fprintf(stderr,"[appearance] %s load started; paused room accepted actor=%08X\n",catalog[desired].archive,actor);return;
 }
 if(call.run(func_00197FC8)!=0)return;
 // Query the alternate root without leaving it in the engine's player slot.
 const uint32_t engine_archive=vm_read32(kPlayerArchive);
 vm_write32(kPlayerArchive,visual().variant_archive);
 const uint32_t list=call.run(func_001E2324,0);
 vm_write32(kPlayerArchive,engine_archive);
 std::fprintf(stderr,"[appearance] resource complete texture_list=%08X\n",list);
 if(!realize(call,list)){fail("Appearance setup failed; original retained");return;}
 loading=false;install(ctx,actor);
}
// Preserve the original system directory and ALD type; only the basename is
// private. This avoids editing game tables or sharing one cached archive name.
void resolved_path(uint32_t file,uint32_t destination){
 if(!custom_path_scope||desired<original_appearance_count||file!=14||!mapped(destination,0xd0))return;
 const char* name=catalog[desired].archive;
 const size_t length=std::strlen(name);if(length>=0x40)return;
 std::memset(vm_base+destination+0x40,0,0x40);
 std::memcpy(vm_base+destination+0x40,name,length);
}
// Restore before the regular room/player loader replaces its display models.
void before_player_load(){restore();original={};}
// Entity ID zero normally runs a timed particle state machine. Our accessory
// uses that ID only for dispatch; submit its real ML_WORK to the model pass.
bool update_glasses(uint32_t entity){
 if(!entity||entity!=glasses_entity||current!=4||desired!=4||
    vm_read32(entity+0x2d8)!=visual().glasses_work)return false;
 bind_glasses_bank();
 const uint32_t sys=vm_read32(kSys);
 if(!mapped(sys+0x222cc,4)||!mapped(sys+0x222fc,512*4))return true;
 const uint32_t n=vm_read32(sys+0x222cc);
 if(n>=512)return true;
 for(unsigned i=0;i<n;i++)if(vm_read32(sys+0x222fc+i*4)==entity)return true;
 vm_write32(sys+0x222fc+n*4,entity);vm_write32(sys+0x222cc,n+1);
 return true;
}
} // namespace cv_appearance

void func_0003ECC0(ppu_context* ctx){
 cv_appearance::before_player_load();cv_original_0003ECC0(ctx);
}
void func_0004A3F8(ppu_context* ctx){
 if(!cv_appearance::update_glasses(uint32_t(ctx->gpr[3])))cv_original_0004A3F8(ctx);
}

void func_001DBCA0(ppu_context* ctx){
 const uint32_t file=uint32_t(ctx->gpr[4]),destination=uint32_t(ctx->gpr[5]);
 cv_original_001DBCA0(ctx);
 cv_appearance::resolved_path(file,destination);
}

