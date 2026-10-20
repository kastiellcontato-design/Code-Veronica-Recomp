/* Replay an F9 .pose snapshot through production coherence/merge/weld.
 * Usage: code_veronica_pose_replay_tests capture.pose -1 [output.xyz]
 * No assets are bundled; the diagnostic snapshot is supplied explicitly. */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cassert>
#include "rsx_pose_interpolation.h"
int main(int argc,char**argv){assert(argc>=2);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);std::vector<unsigned char>b(size);assert(fread(b.data(),1,size,f)==size);fclose(f);assert(size>=64);uint32_t hd[16];memcpy(hd,b.data(),64);assert(hd[0]==0x4d435031&&(hd[1]==1||hd[1]==2)&&hd[10]==sizeof(RsxPoseVertex)&&hd[11]==sizeof(RsxPoseDraw)&&hd[12]==sizeof(RsxPoseTrack));assert(hd[2]<=786432&&hd[3]<=24576&&hd[4]<=786432);unsigned nv=hd[2],nd=hd[3],pn=hd[4];size_t at=64;
 auto take=[&](void*dst,size_t n){assert(at<=size&&n<=size-at);if(dst)memcpy(dst,b.data()+at,n);at+=n;};
 std::vector<RsxPoseVertex>raw(nv);std::vector<float>actual(nv*3),cams(24576*32),before(nv*3),out(nv*3);std::vector<RsxPoseDraw>bind(nd);std::vector<int32_t>sources(nd);RsxPoseHistory h{};assert(rsx_pose_init(&h,nv>pn?nv:pn,24576));
 take(raw.data(),nv*sizeof(RsxPoseVertex));take(actual.data(),nv*12);take(h.cur.draws,nd*sizeof(RsxPoseDraw));take(bind.data(),nd*sizeof(RsxPoseDraw));take(h.cur.tracks,nd*sizeof(RsxPoseTrack));take(sources.data(),nd*4);take(nullptr,nd*4);take(h.cur.old_xyz,pn*12);take(h.cur.xyz,pn*12);take(h.cur.old_cameras,nd*128);take(h.cur.cameras,nd*128);h.cur.nv=pn;h.cur.nd=nd;h.cur.scene=hd[7];h.have_cur=1;
 for(unsigned d=0;d<nd;d++)h.stationary_draws[d]=h.cur.stationary[d]=(h.cur.draws[d].flags&RSX_POSE_CAPTURE_STATIONARY)!=0;
 for(unsigned d=0;d<nd;d++)memcpy(cams.data()+size_t(bind[d].cb)*32,h.cur.cameras+size_t(d)*32,128);
 RsxPoseFrame fr{raw.data(),cams.data(),bind.data(),nv,nd,hd[7],hd[8]};assert(rsx_pose_cohere(&h,&fr));unsigned srcdiff=0;for(unsigned d=0;d<nd;d++)srcdiff+=h.vertex_sources[d]!=sources[d];printf("nv=%u nd=%u pn=%u source_diffs=%u\n",nv,nd,pn,srcdiff);
 std::vector<uint32_t>state(nv),cs(24576);std::vector<uint64_t>recipes(nv,UINT64_MAX);
 for(unsigned d=0;d<nd;d++)for(unsigned k=0;k<bind[d].count;k++){unsigned v=bind[d].first+k;float xyz[3];int s=h.vertex_sources[d];if(s>=0)rsx_pose_vertex(&h,s,k,xyz);else memcpy(xyz,raw[v].xyz,12);rsx_pose_merge(out.data()+v*3,state.data()+v,xyz,3);}
 before=out;rsx_pose_protect_aliases(&h,&fr,state.data(),cs.data());assert(rsx_pose_weld(&h,&fr,out.data(),state.data()));
 unsigned mismatches=0;for(unsigned v=0;v<nv;v++){const float*final=state[v]==1?out.data()+v*3:raw[v].xyz;if(memcmp(final,actual.data()+v*3,12))mismatches++;}
 printf("without unaccepted aliases: final mismatches=%u seams=%u conflicts=%u\n",mismatches,h.seams,h.seam_conflicts);
 unsigned mixed_after=0,edge_before=0,edge_after=0,moving_after=0;double max_before=0,max_after=0;
 for(unsigned v=0;v<nv;v++){if(state[v]!=1)memcpy(out.data()+v*3,raw[v].xyz,12);moving_after+=memcmp(out.data()+v*3,raw[v].xyz,12)!=0;}
 for(unsigned d=0;d<nd;d++){
  unsigned veto=0;for(unsigned k=0;k<bind[d].count;k++)veto+=state[bind[d].first+k]==2;
  if(veto&&veto!=bind[d].count)mixed_after++;assert(!veto||veto==bind[d].count);
  int source=h.vertex_sources[d];if(source<0)continue;unsigned start=h.cur.draws[source].first;
  auto edge=[](const float*a,const float*b){double n=0;for(int k=0;k<3;k++){double v=double(a[k])-b[k];n+=v*v;}return n;};
  for(unsigned k=0;k+2<bind[d].count;k+=3)for(unsigned a=0;a<3;a++){
   unsigned c=(a+1)%3;size_t ia=size_t(start+k+a)*3,ib=size_t(start+k+c)*3;
   double base=edge(h.cur.old_xyz+ia,h.cur.old_xyz+ib),cur=edge(h.cur.xyz+ia,h.cur.xyz+ib);if(cur>base)base=cur;
   size_t va=size_t(bind[d].first+k+a)*3,vb=size_t(bind[d].first+k+c)*3;
   double before=edge(actual.data()+va,actual.data()+vb),after=edge(out.data()+va,out.data()+vb);base=base>1e-8?base:1e-8;
   double rb=sqrt(before/base),ra=sqrt(after/base);if(rb>max_before)max_before=rb;if(ra>max_after)max_after=ra;edge_before+=rb>2;edge_after+=ra>2;
  }
 }
 printf("REPLAY mixed_after=%u stretched_edges_before=%u after=%u max_ratio_before=%.6g max_ratio_after=%.6g moving_after=%u components=%u vetoed_vertices=%u\n",mixed_after,edge_before,edge_after,max_before,max_after,moving_after,h.seam_components,h.seam_veto_vertices);
 if(argc>3){FILE* dump=fopen(argv[3],"wb");assert(dump);assert(fwrite(out.data(),12,nv,dump)==nv);fclose(dump);}
 unsigned target=argc>2?atoi(argv[2]):1217;for(unsigned k=0;target<nd&&k<bind[target].count;k++){unsigned v=bind[target].first+k;printf("v=%u state=%u root=%u rootstate=%d\n",v,state[v],h.vertex_owners[v],h.vertex_owners[v]?h.vertex_next[h.vertex_owners[v]-1]:-99);
 auto print=[&](const char*name,const float*x){printf(" %s %.9g %.9g %.9g\n",name,x[0],x[1],x[2]);};print("raw",raw[v].xyz);print("before",before.data()+v*3);print("weld",out.data()+v*3);print("actual",actual.data()+v*3);
 for(unsigned d=0;d<nd;d++)for(unsigned j=0;j<bind[d].count;j++){unsigned id=bind[d].first+j;if(!memcmp(raw[id].xyz,raw[v].xyz,12)&&h.camera_groups[d]==h.camera_groups[target]){float val[3];int s=h.vertex_sources[d];if(s>=0)rsx_pose_vertex(&h,s,j,val);else memcpy(val,raw[id].xyz,12);printf(" draw=%u slot=%u count=%u source=%d alpha=%g flag=%u candidate=%.8g %.8g %.8g\n",d,id,bind[d].count,s,s<0?1:h.cur.tracks[s].alpha,bind[d].flags,val[0],val[1],val[2]);}}
 }
 rsx_pose_destroy(&h);
}
