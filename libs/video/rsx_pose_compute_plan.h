/* CPU builds immutable topology/ownership only. GPU resolves moving geometry,
 * cross-pass proposals, collective alias vetoes and exact point seams. */
#ifndef RSX_POSE_COMPUTE_PLAN_H
#define RSX_POSE_COMPUTE_PLAN_H
#include "rsx_pose_interpolation.h"
typedef struct { uint32_t* words;size_t capacity,bytes;uint32_t output_bytes,final_offset,nv,nd; } RsxPoseComputePlan;
enum {PC_NV,PC_ND,PC_VERT,PC_DRAW,PC_REFS,PC_GEOM_HEAD,PC_ALIAS_HEAD,PC_ALIAS_NEXT,
      PC_POINT_HEAD,PC_POINT_NEXT,PC_OLD,PC_CUR,PC_SEED,PC_PROPOSAL,PC_ALIAS,PC_POINT,PC_FINAL,PC_HISTORY_NV,PC_MESH_HEAD,PC_MESH_NEXT,PC_MESH_VETO,PC_MESH_ID,PC_HEADER=24};
static inline uint32_t pc_align(uint32_t n){return (n+255u)&~255u;}
static inline uint32_t* pc_at(RsxPoseComputePlan* p,uint32_t off){return p->words+off/4;}
static inline void rsx_pose_compute_plan_destroy(RsxPoseComputePlan* p){free(p->words);memset(p,0,sizeof *p);}
static inline int rsx_pose_compute_number(float f){uint32_t x;memcpy(&x,&f,4);x&=0x7fffffffu;return (!x||x>=0x20000000u)&&x<=0x49800000u;}
static inline int rsx_pose_compute_plan_build(RsxPoseComputePlan* p,RsxPoseHistory* h,const RsxPoseFrame* f,const uint32_t* veto) {
    if(!f->nv||!f->nd||f->nv>h->maxv||f->nd>h->maxd||!h->cur.nv||h->cur.nv>h->maxv||h->cur.nd!=f->nd)return 0;
    for(uint32_t i=0;i<f->nd;i++){
        const RsxPoseDraw *a=f->draws+i,*b=h->cur.draws+i;
        if(!a->count||a->first>f->nv||a->count>f->nv-a->first||b->first>h->cur.nv||a->count>h->cur.nv-b->first)return 0;
    }
    uint64_t refs=0;for(uint32_t i=0;i<f->nd;i++)refs+=f->draws[i].count;
    if(refs>4000000u)return 0;
    for(uint32_t i=0;i<h->cur.nv*3;i++)if(!rsx_pose_compute_number(h->cur.xyz[i])||!rsx_pose_compute_number(h->cur.old_xyz[i]))return 0;
    uint32_t header[PC_HEADER]={0},off=256;
    header[PC_NV]=f->nv;header[PC_ND]=f->nd;header[PC_HISTORY_NV]=h->cur.nv;
#define PC_ALLOC(field,n) do{header[field]=off;off=pc_align(off+(uint32_t)(n));}while(0)
    PC_ALLOC(PC_VERT,f->nv*32);PC_ALLOC(PC_DRAW,f->nd*32);PC_ALLOC(PC_REFS,refs*8);
    PC_ALLOC(PC_GEOM_HEAD,f->nd*4);PC_ALLOC(PC_ALIAS_HEAD,f->nd*4);PC_ALLOC(PC_ALIAS_NEXT,f->nv*4);
    PC_ALLOC(PC_POINT_HEAD,f->nv*4);PC_ALLOC(PC_POINT_NEXT,f->nv*4);
    PC_ALLOC(PC_MESH_HEAD,f->nd*4);PC_ALLOC(PC_MESH_NEXT,f->nv*4);PC_ALLOC(PC_MESH_ID,f->nv*4);
    PC_ALLOC(PC_OLD,h->cur.nv*12);PC_ALLOC(PC_CUR,h->cur.nv*12);
#undef PC_ALLOC
    if(off>p->capacity){void* q=realloc(p->words,off);if(!q)return 0;p->words=(uint32_t*)q;p->capacity=off;}
    p->bytes=off;p->nv=f->nv;p->nd=f->nd;
    off=0;header[PC_SEED]=off;off=pc_align(off+f->nd*4);
    header[PC_PROPOSAL]=off;off=pc_align(off+f->nv*16);
    header[PC_ALIAS]=off;off=pc_align(off+f->nd*4);
    header[PC_POINT]=off;off=pc_align(off+f->nv*16);
    header[PC_MESH_VETO]=off;off=pc_align(off+f->nd*4);
    header[PC_FINAL]=off;off=pc_align(off+f->nv*16);
    p->output_bytes=off;p->final_offset=header[PC_FINAL];
    memcpy(p->words,header,sizeof header);
    rsx_pose_stationary_vertices(h,f);
    uint32_t* v=pc_at(p,header[PC_VERT]);uint32_t* d=pc_at(p,header[PC_DRAW]);
    uint32_t* gh=pc_at(p,header[PC_GEOM_HEAD]);uint32_t* ah=pc_at(p,header[PC_ALIAS_HEAD]);
    uint32_t* an=pc_at(p,header[PC_ALIAS_NEXT]);uint32_t* ph=pc_at(p,header[PC_POINT_HEAD]);uint32_t* pn=pc_at(p,header[PC_POINT_NEXT]);
    memset(gh,0xff,f->nd*4);memset(ah,0xff,f->nd*4);memset(ph,0xff,f->nv*4);
    for(uint32_t i=0;i<f->nv;i++){memcpy(v+i*8,f->vertices[i].xyz,12);v[i*8+3]=veto[i]==2?2:0;v[i*8+4]=v[i*8+5]=0;v[i*8+6]=v[i*8+7]=UINT32_MAX;}
    for(uint32_t i=f->nd;i-->0;){const RsxPoseDraw* draw=f->draws+i;
        uint32_t group=(uint32_t)h->groups[i];if(group>=f->nd)return 0;
        uint32_t* m=d+i*8;m[0]=draw->first;m[1]=draw->count;m[2]=h->cur.draws[i].first;
        memcpy(m+3,&h->cur.tracks[i].alpha,4);m[4]=rsx_pose_draw_ready(h,i);m[5]=group;m[6]=gh[group];m[7]=0;gh[group]=i;
        for(uint32_t k=0;k<draw->count;k++)v[(draw->first+k)*8+5]++;
    }
    uint32_t cursor=0;for(uint32_t i=0;i<f->nv;i++){v[i*8+4]=cursor;cursor+=v[i*8+5];h->vertex_next[i]=0;if(v[i*8+5]&&v[i*8+3]!=2)v[i*8+3]=1;}
    /* Input state 3 records a proven stationary draw. It is not a veto:
     * point resolution vetoes only if a moving proposal contradicts it. */
    for(uint32_t i=0;i<f->nv;i++)if(h->stationary_vertices[i]&&v[i*8+3]!=2)v[i*8+3]=3;
    uint32_t* r=pc_at(p,header[PC_REFS]);
    for(uint32_t i=0;i<f->nd;i++)for(uint32_t k=0;k<f->draws[i].count;k++){
        uint32_t at=f->draws[i].first+k,index=v[at*8+4]+(uint32_t)h->vertex_next[at]++;r[index*2]=i;r[index*2+1]=k;}
    /* Static alias graph: same geometry and overlapping upload ranges. */
    memset(h->vertex_owners,0,f->nv*4);for(uint32_t i=0;i<f->nd;i++)h->next[i]=(int32_t)i;
    for(uint32_t i=0;i<f->nd;i++){rsx_pose_join(h,(int32_t)i,h->groups[i]);const RsxPoseDraw* draw=f->draws+i;
        for(uint32_t k=0;k<draw->count;k++){uint32_t* owner=h->vertex_owners+draw->first+k;if(*owner)rsx_pose_join(h,(int32_t)i,(int32_t)(*owner-1));else *owner=i+1;}}
    for(uint32_t i=f->nv;i-->0;)if(h->vertex_owners[i]){uint32_t root=(uint32_t)rsx_pose_component(h,(int32_t)h->vertex_owners[i]-1);v[i*8+6]=root;an[i]=ah[root];ah[root]=i;}
    /* Exact point/camera graph. Preserve original 128-visit/cap failure. */
    memset(h->vertex_groups,0,f->nv*4);memset(h->heads,0xff,h->buckets*4);uint32_t nodes=0;
    for(uint32_t i=0;i<f->nd;i++){const RsxPoseDraw* draw=f->draws+i;uint32_t camera=(uint32_t)h->camera_groups[i]+1;
        for(uint32_t k=0;k<draw->count;k++){uint32_t at=draw->first+k;if(!v[at*8+3])continue;
            if(!h->vertex_groups[at]){h->vertex_groups[at]=at+1;h->weld_hashes[at]=rsx_pose_hash_floats(1469598103934665603ull,f->vertices[at].xyz,3);}
            uint32_t bucket=rsx_pose_bucket(h,h->weld_hashes[at]^camera),visits=0;int32_t group=-1;
            for(int32_t g=h->heads[bucket];g>=0;g=h->vertex_next[g]){if(++visits>128)return 0;
                if(h->vertex_cameras[g]==camera&&rsx_pose_floats_equal(f->vertices[at].xyz,f->vertices[h->vertex_owners[g]].xyz,3)){group=g;break;}}
            if(group<0){if(nodes==h->maxv)return 0;uint32_t n=nodes++;h->vertex_owners[n]=at;h->vertex_cameras[n]=camera;h->vertex_next[n]=h->heads[bucket];h->heads[bucket]=(int32_t)n;}
            else {uint32_t a=rsx_pose_point_component(h,at),b=rsx_pose_point_component(h,h->vertex_owners[group]);if(a<b)h->vertex_groups[b]=a+1;else if(b<a)h->vertex_groups[a]=b+1;}
        }}
    for(uint32_t i=f->nv;i-->0;)if(h->vertex_groups[i]){uint32_t root=rsx_pose_point_component(h,i);v[i*8+7]=root;pn[i]=ph[root];ph[root]=i;}
    for(uint32_t i=0;i<f->nv;i++)h->vertex_owners[i]=v[i*8+7]==UINT32_MAX?0:v[i*8+7]+1;
    rsx_pose_mesh_components(h,f);
    uint32_t* mh=pc_at(p,header[PC_MESH_HEAD]);uint32_t* mn=pc_at(p,header[PC_MESH_NEXT]);uint32_t* mi=pc_at(p,header[PC_MESH_ID]);
    memset(mh,0xff,f->nd*4);memset(mi,0xff,f->nv*4);
    for(uint32_t i=f->nv;i-->0;)if(h->vertex_owners[i]) {
        uint32_t owner=h->vertex_cameras[h->vertex_owners[i]-1];
        uint32_t group=(uint32_t)rsx_pose_component(h,(int32_t)owner-1);mi[i]=group;mn[i]=mh[group];mh[group]=i;
    }
    memcpy(pc_at(p,header[PC_OLD]),h->cur.old_xyz,h->cur.nv*12);
    memcpy(pc_at(p,header[PC_CUR]),h->cur.xyz,h->cur.nv*12);
    return 1;
}
#endif
