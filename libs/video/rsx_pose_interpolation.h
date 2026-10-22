/* Render-only, per-draw pose histories. No guest writes or upload-heap reads.
 * A topology/UV/pass key and bounded proximity search pair individual draws;
 * culling, reordering and new objects cannot reset the other objects. Without
 * engine mesh tags, ambiguous matches deliberately retain the original pose.
 */
#ifndef RSX_POSE_INTERPOLATION_H
#define RSX_POSE_INTERPOLATION_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rsx_pose_compare.h"
#if !defined(RSX_POSE_SCALAR_SAMPLES) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2))
#include <emmintrin.h>
#define RSX_POSE_VECTOR_SAMPLES 1
#endif

typedef struct { float xyz[3]; uint32_t uv,stamp; } RsxPoseVertex;
/* The optional raw-vertex pulling prelude uses ByteAddressBuffer.Load too.
 * Only vertex-texture operations in the translated GUEST body disqualify
 * geometry; the prelude itself must not disable all interpolation. */
static inline int rsx_pose_shader_inputs_known(const char* source) {
    if(!source)return 1;
    const char* guest=strstr(source,"VSOutput vf_main(VSInput input) {");
    if(!guest)guest=source;
    return !strstr(guest,".Load(")&&!strstr(guest,".SampleLevel(");
}
/* Captured skinned Code Veronica VPs decode a14 as normals/lighting and
 * read UVs from a15 (also a8 for the other texture-coordinate layout).
 * Animated normals must never participate in temporal mesh identity. */
static inline void rsx_pose_record_vertex(RsxPoseVertex* v,const float attributes[16][4],uint32_t stamp) {
    memcpy(v->xyz,attributes[0],3*sizeof(float));
    uint32_t uv[4];memcpy(uv,attributes[15],2*sizeof(float));memcpy(uv+2,attributes[8],2*sizeof(float));
    uint32_t hash=2166136261u;for(int i=0;i<4;i++)hash=(hash^uv[i])*16777619u;
    v->uv=hash;v->stamp=stamp;
}
/* Ordered UVs alone are not topology identity: untextured/mechanical parts
 * often have identical coordinates but refer to different source vertices.
 * Record the guest vertex index, never an upload slot or a rotating address.
 * All fetch/expansion paths must pass the SAME index that fetched attribute 0.
 * The xyz/uv/stamp capture ABI remains unchanged; uv is now a UV/source hash. */
static inline void rsx_pose_record_source_vertex(RsxPoseVertex* v,const float attributes[16][4],uint32_t stamp,uint32_t source_index) {
    rsx_pose_record_vertex(v,attributes,stamp);
    v->uv=(v->uv^source_index)*16777619u;
}
enum { RSX_POSE_CURRENT_LIGHT_CAMERA=1u,RSX_POSE_WORLD_LIGHT_GEOMETRY=2u,RSX_POSE_CAPTURE_STATIONARY=4u };
/* This captured program writes packed shadow moments using CURRENT FP
 * light constants. Moving just its VP projection would give the atlas a
 * different origin than its readers and the palette-skinned casters. */
static inline uint32_t rsx_pose_fragment_flags(uint32_t code) {
    return code==0x7EA4C4E7u?RSX_POSE_CURRENT_LIGHT_CAMERA:0u;
}
/* Temporal identity must include the frozen fragment PROGRAM, not its guest
 * address or animated inline constants. Different materials can share VP,
 * UVs and target layout; matching them invents movement in static scenery.
 * Exact geometry coherence remains independent of material identity. */
static inline uint64_t rsx_pose_material_key(uint64_t geometry_key,uint32_t fragment_code) {
    return (geometry_key^fragment_code)*1099511628211ull;
}
/* Captured world-space a0 consumers: shadow, depth and material variants.
 * Only these verified programs may establish a cross-projection constraint.
 * Unknown/local-space programs retain full-camera isolation. */
static inline uint32_t rsx_pose_program_flags(uint32_t fragment,uint32_t vertex) {
    uint32_t flags=rsx_pose_fragment_flags(fragment);
    if(vertex==0x6a970abfu||vertex==0x8cb38bc1u||vertex==0xe985b049u||vertex==0xcaa490eau||vertex==0xf830c5dau)
        flags|=RSX_POSE_WORLD_LIGHT_GEOMETRY;
    return flags;
}
typedef struct { uint64_t key; uint32_t first,count,cb,flags; } RsxPoseDraw;
typedef struct {
    const RsxPoseVertex* vertices; const float* cameras; const RsxPoseDraw* draws;
    uint32_t nv,nd,scene,stamp;
} RsxPoseFrame;
typedef struct { double changed_at; float alpha; int active; } RsxPoseTrack;
typedef struct {
    float *xyz,*old_xyz,*cameras,*old_cameras;
    RsxPoseDraw* draws; /* first indexes packed per-draw CPU storage */
    RsxPoseTrack* tracks;
    uint32_t* camera_ids; /* exact matrix equivalence, local to this snapshot */
    uint32_t* sample_ids; /* exact sampled-position equivalence; never an object identity */
    uint8_t* stationary; /* confirmed current-position constraints, retained with snapshot */
    uint32_t nv,nd,scene;
} RsxPoseSnapshot;
/* Consecutive candidates in the EXISTING hash chain. Bounds contain their
 * first vertex, whose distance is already a mandatory temporal rejection.
 * Skipping a distant block preserves candidate order and the 128-visit cap. */
typedef struct { float lo[3],hi[3]; int32_t after; uint32_t count; } RsxPoseBlock;
typedef struct { uint32_t previous,current,epoch; float transform; } RsxPoseCameraMemo;
typedef struct {float x[16],y[16],z[16];} RsxPoseSamples;
typedef struct {uint32_t current,previous,count,epoch,flags;} RsxPoseHistoryMemo;
typedef struct {uint32_t vertex,camera,epoch;int32_t head;} RsxPoseWeldMemo;
typedef struct {
    RsxPoseSamples samples;
    uint32_t count,indices[16],camera_id,sample_id;float xyz[16][3];const float* camera;
} RsxPoseQuery;
/* Optional caller-owned probe. NULL in normal play. Coarse sections cover
 * the full prepare; position/camera counters have no per-call clock probes.
 * No probe participates in matching. */
enum {RSX_POSE_P_VALIDATE,RSX_POSE_P_INDEX,RSX_POSE_P_SETUP,RSX_POSE_P_SEARCH,RSX_POSE_P_RESOLVE,RSX_POSE_P_COUNT};
typedef struct {
    double (*clock)(void);
    double seconds[RSX_POSE_P_COUNT];
    uint32_t position_calls,camera_calls;
} RsxPoseSearchProfile;
static inline void rsx_pose_profile_section(RsxPoseSearchProfile* p,int section,double* mark) {
    if(p){double now=p->clock();p->seconds[section]+=now-*mark;*mark=now;}
}
typedef struct {
    RsxPoseSnapshot cur,prev;
    RsxPoseSearchProfile* search_profile;
    uint32_t maxv,maxd,buckets;
    int32_t *heads,*next,*groups,*camera_groups,*seeds,*vertex_sources,*camera_sources;
    uint32_t *vertex_owners,*vertex_cameras,*vertex_groups; int32_t* vertex_next; uint8_t *used,*stationary_draws,*stationary_vertices;
    RsxPoseBlock* blocks;
    int32_t *memo_heads,*memo_next,*memo_ids;
    uint64_t* memo_hashes;
    RsxPoseCameraMemo* camera_memo;
    uint32_t memo_slots,memo_epoch,camera_hits,camera_misses,camera_uncached;
    int camera_memo_disabled;
    int32_t *sample_heads,*sample_next;
    uint64_t* sample_hashes;
    RsxPoseCameraMemo* position_memo; /* same pair/epoch layout, transform holds the exact sample sum */
    uint32_t position_hits,position_misses,position_uncached;
    int position_memo_disabled;
    int position_compare_scalar;
    int stream_vectors; /* opt-in, exact positions and stationary checks */
    int repeat_reuse_disabled;
    RsxPoseHistoryMemo* history_memo;
    RsxPoseWeldMemo* weld_memo;uint32_t weld_epoch;
    uint32_t history_reuse_hits,history_reuse_misses,weld_reuse_hits,weld_reuse_misses;
    RsxPoseSamples* search_samples;
    uint32_t* search_epochs;
    uint32_t search_candidates,search_pruned,sample_groups_reused,search_blocks_pruned;
    uint8_t* bucket_capped; /* previous chains longer than the unchanged 128-visit budget */
    uint32_t capped_searches_skipped;int cap_precheck_disabled;
    uint8_t* camera_checked; /* scratch: reset for each validation/coherence call */
    uint64_t* coherence_hashes;
    int repeat_work_disabled;
    uint64_t* weld_hashes; /* initialized once per upload during each weld */
    uint32_t validated_vertices,reused_validations;
    int vertex_validation_cache_disabled;
    int have_cur,have_prev,reason;
    double last_at; float alpha;
    uint32_t matched,moving,ambiguous,capped,coherent,coherence_conflicts,seams,seam_conflicts,seam_components,seam_veto_vertices;
} RsxPoseHistory;
enum {RSX_POSE_OFF,RSX_POSE_WARMUP,RSX_POSE_NEW,RSX_POSE_REPEAT,RSX_POSE_LAYOUT,RSX_POSE_CUT,RSX_POSE_INVALID};
static inline void rsx_pose_reset(RsxPoseHistory* h) {
    h->have_cur=h->have_prev=0;h->matched=h->moving=h->ambiguous=h->capped=0;h->coherent=h->coherence_conflicts=h->seams=h->seam_conflicts=h->seam_components=h->seam_veto_vertices=0;h->alpha=1;
}
static inline void rsx_pose_destroy(RsxPoseHistory* h) {
    RsxPoseSnapshot* s[2]={&h->cur,&h->prev};
    for(int i=0;i<2;i++) {
        free(s[i]->xyz);free(s[i]->old_xyz);free(s[i]->cameras);free(s[i]->old_cameras);
        free(s[i]->draws);free(s[i]->tracks);free(s[i]->camera_ids);free(s[i]->sample_ids);free(s[i]->stationary);
    }
    free(h->heads);free(h->next);free(h->groups);free(h->camera_groups);free(h->vertex_owners);free(h->vertex_cameras);free(h->vertex_groups);free(h->vertex_next);free(h->seeds);
    free(h->vertex_sources);free(h->camera_sources);free(h->used);free(h->stationary_draws);free(h->stationary_vertices);free(h->blocks);
    free(h->memo_heads);free(h->memo_next);free(h->memo_ids);free(h->memo_hashes);free(h->camera_memo);
    free(h->sample_heads);free(h->sample_next);free(h->sample_hashes);free(h->position_memo);
    free(h->history_memo);free(h->weld_memo);
    free(h->search_samples);free(h->search_epochs);free(h->bucket_capped);free(h->camera_checked);free(h->coherence_hashes);free(h->weld_hashes);
    memset(h,0,sizeof *h);
}
static inline int rsx_pose_init(RsxPoseHistory* h,uint32_t maxv,uint32_t maxd) {
    memset(h,0,sizeof *h);
    if(!maxv||maxv>(uint32_t)INT32_MAX||!maxd||maxd>(uint32_t)INT32_MAX/2)return 0;
    h->maxv=maxv;h->maxd=maxd;h->buckets=maxd*2+1;
    RsxPoseSnapshot* s[2]={&h->cur,&h->prev};
    for(int i=0;i<2;i++) {
        s[i]->xyz=(float*)calloc((size_t)maxv*3,sizeof(float));
        s[i]->old_xyz=(float*)calloc((size_t)maxv*3,sizeof(float));
        s[i]->cameras=(float*)calloc((size_t)maxd*32,sizeof(float));
        s[i]->old_cameras=(float*)calloc((size_t)maxd*32,sizeof(float));
        s[i]->draws=(RsxPoseDraw*)calloc(maxd,sizeof(RsxPoseDraw));
        s[i]->tracks=(RsxPoseTrack*)calloc(maxd,sizeof(RsxPoseTrack));
        s[i]->camera_ids=(uint32_t*)calloc(maxd,sizeof(uint32_t));
        s[i]->sample_ids=(uint32_t*)calloc(maxd,sizeof(uint32_t));
        s[i]->stationary=(uint8_t*)calloc(maxd,1);
        if(!s[i]->xyz||!s[i]->old_xyz||!s[i]->cameras||!s[i]->old_cameras||!s[i]->draws||!s[i]->tracks||!s[i]->camera_ids||!s[i]->sample_ids||!s[i]->stationary) {
            rsx_pose_destroy(h);return 0;
        }
    }
    h->heads=(int32_t*)malloc((size_t)h->buckets*sizeof(int32_t));
    h->next=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));h->used=(uint8_t*)calloc(maxd,1);
    h->stationary_draws=(uint8_t*)calloc(maxd,1);h->stationary_vertices=(uint8_t*)calloc(maxv,1);
    h->groups=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));h->seeds=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));
    h->camera_groups=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));h->vertex_owners=(uint32_t*)calloc(maxv,sizeof(uint32_t));
    h->vertex_cameras=(uint32_t*)calloc(maxv,sizeof(uint32_t));h->vertex_next=(int32_t*)malloc((size_t)maxv*sizeof(int32_t));
    h->vertex_groups=(uint32_t*)calloc(maxv,sizeof(uint32_t));
    h->vertex_sources=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));h->camera_sources=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));
    h->blocks=(RsxPoseBlock*)malloc((size_t)maxd*sizeof(RsxPoseBlock));
    h->memo_heads=(int32_t*)malloc((size_t)h->buckets*sizeof(int32_t));
    h->memo_next=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));
    h->memo_ids=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));
    h->memo_hashes=(uint64_t*)malloc((size_t)maxd*sizeof(uint64_t));
    h->memo_slots=1;while(h->memo_slots<maxd*2)h->memo_slots*=2;
    h->camera_memo=(RsxPoseCameraMemo*)calloc(h->memo_slots,sizeof(RsxPoseCameraMemo));
    /* Optional: allocation failure keeps the original computations. */
    h->history_memo=(RsxPoseHistoryMemo*)calloc(h->memo_slots,sizeof(RsxPoseHistoryMemo));
    h->weld_memo=(RsxPoseWeldMemo*)calloc(h->memo_slots,sizeof(RsxPoseWeldMemo));
    h->sample_heads=(int32_t*)malloc((size_t)h->buckets*sizeof(int32_t));
    h->sample_next=(int32_t*)malloc((size_t)maxd*sizeof(int32_t));
    h->sample_hashes=(uint64_t*)malloc((size_t)maxd*sizeof(uint64_t));
    h->position_memo=(RsxPoseCameraMemo*)calloc(h->memo_slots,sizeof(RsxPoseCameraMemo));
    h->search_samples=(RsxPoseSamples*)malloc((size_t)maxd*sizeof(RsxPoseSamples));
    h->search_epochs=(uint32_t*)calloc(maxd,sizeof(uint32_t));
    h->bucket_capped=(uint8_t*)calloc(h->buckets,1);
    h->camera_checked=(uint8_t*)calloc(maxd,1);
    h->coherence_hashes=(uint64_t*)malloc((size_t)maxd*sizeof(uint64_t));
    h->weld_hashes=(uint64_t*)malloc((size_t)maxv*sizeof(uint64_t));
    if(!h->heads||!h->next||!h->used||!h->stationary_draws||!h->stationary_vertices||!h->groups||!h->camera_groups||!h->vertex_owners||!h->vertex_cameras||!h->vertex_groups||!h->vertex_next||!h->seeds||!h->vertex_sources||!h->camera_sources||!h->blocks||!h->memo_heads||!h->memo_next||!h->memo_ids||!h->memo_hashes||!h->camera_memo||!h->sample_heads||!h->sample_next||!h->sample_hashes||!h->position_memo||!h->search_samples||!h->search_epochs||!h->bucket_capped||!h->camera_checked||!h->coherence_hashes||!h->weld_hashes){rsx_pose_destroy(h);return 0;}return 1;
}
/* Observed Code Veronica VP layout: c0..c3 orthonormal orientation,
 * c4..c7 projection/translation. Unknown layouts (including UI) are skipped. */
static inline int rsx_pose_camera_valid(const float* m) {
    for(int i=0;i<32;i++)if(!isfinite(m[i])||fabsf(m[i])>1000000.f)return 0;
    if(fabsf(m[3])+fabsf(m[7])+fabsf(m[11])>.01f||fabsf(m[15]-1.f)>.01f)return 0;
    for(int r=0;r<3;r++) {
        float n=0;for(int k=0;k<3;k++)n+=m[r*4+k]*m[r*4+k];if(n<.8f||n>1.2f)return 0;
        for(int s=r+1;s<3;s++){float dot=0;for(int k=0;k<3;k++)dot+=m[r*4+k]*m[s*4+k];if(fabsf(dot)>.15f)return 0;}
    }
    return fabsf(m[19])+fabsf(m[23])+fabsf(m[27])>.1f;
}
static inline uint32_t rsx_pose_camera_id(RsxPoseHistory* h,const RsxPoseFrame* f,uint32_t cb);
/* Build exact camera IDs while validating. Once a bit-identical matrix was
 * validated, its other passes need no repeated norms/finite checks. Lookup
 * hashes are only hints: camera_id verifies every byte before sharing. */
static inline int rsx_pose_frame_valid(RsxPoseHistory* h,const RsxPoseFrame* f) {
    h->validated_vertices=h->reused_validations=0;
    if(!f->vertices||!f->draws||!f->cameras||f->nv>h->maxv||f->nd>h->maxd)return 0;
    memset(h->memo_heads,0xFF,(size_t)h->buckets*sizeof(int32_t));
    memset(h->memo_ids,0xFF,(size_t)h->maxd*sizeof(int32_t));
    memset(h->camera_checked,0,h->maxd);
    /* Immutable frame: lighting/depth/material copies often reuse one raw
     * upload range. Validate each physical vertex once, never once per pass.
     * This existing scratch is rebuilt again by weld, after preparation. */
    int cached=!h->vertex_validation_cache_disabled;
    if(cached)memset(h->vertex_groups,0,(size_t)f->nv*sizeof(uint32_t));
    uint32_t checked=0,reused=0;
    for(uint32_t i=0;i<f->nd;i++) {
        const RsxPoseDraw* d=f->draws+i;
        if(d->cb>=h->maxd||d->first>f->nv||d->count>f->nv-d->first||!d->count)return 0;
        uint32_t camera_id=rsx_pose_camera_id(h,f,d->cb);
        if(!h->camera_checked[camera_id]) {
            if(!rsx_pose_camera_valid(f->cameras+(size_t)d->cb*32))return 0;
            h->camera_checked[camera_id]=1;
        }
        for(uint32_t k=0;k<d->count;k++) {
            uint32_t index=d->first+k;
            if(cached&&h->vertex_groups[index]){reused++;continue;}
            const RsxPoseVertex* v=f->vertices+index;
            if(v->stamp!=f->stamp)return 0;
            for(int j=0;j<3;j++)if(!isfinite(v->xyz[j])||fabsf(v->xyz[j])>1000000.f)return 0;
            if(cached)h->vertex_groups[index]=1;
            checked++;
        }
    }
    h->validated_vertices=checked;h->reused_validations=reused;
    return 1;
}
static inline int rsx_pose_camera_cut(const float* a,const float* b) {
    /* Most scene/material passes keep an identical camera. Avoid repeated
     * matrix validation and square roots for every candidate of every draw. */
    if(!memcmp(a,b,32*sizeof(float)))return 0;
    for(int r=0;r<3;r++) {
        float dot=0,na=0,nb=0;
        for(int k=0;k<3;k++){float x=a[r*4+k],y=b[r*4+k];dot+=x*y;na+=x*x;nb+=y*y;}
        if(dot<.8660254f*sqrtf(na*nb))return 1;
    }
    float pa=0,pb=0,translation=0;
    /* Total projection energy alone misses an axis reversal/collapse: the
     * norms may be identical while their midpoint is singular. Compare each
     * projection basis vector before allowing temporal matrix blending. */
    for(int r=4;r<7;r++) {
        float na=0,nb=0,dot=0;
        for(int k=0;k<4;k++){float x=a[r*4+k],y=b[r*4+k];na+=x*x;nb+=y*y;dot+=x*y;}
        if((na>1e-12f)!=(nb>1e-12f))return 1;
        if(na>1e-12f&&(dot<=0||nb<na*.64f||nb>na*1.56f))return 1;
    }
    for(int k=16;k<28;k++){pa+=a[k]*a[k];pb+=b[k]*b[k];}
    if(pa>0&&(pb<pa*.64f||pb>pa*1.56f))return 1;
    for(int k=28;k<32;k++){float d=a[k]-b[k];translation+=d*d;}
    return translation>100.f;
}
static inline uint32_t rsx_pose_bucket(const RsxPoseHistory* h,uint64_t key) {
    return (uint32_t)((key^(key>>32))%h->buckets);
}
/* Canonical IDs require an exact 128-byte matrix comparison. Hash collisions
 * never establish equivalence. Only the input constant bank is a lookup hint;
 * these IDs and the pair memo are rebuilt/invalidated on EVERY prepare. */
static inline uint32_t rsx_pose_camera_id(RsxPoseHistory* h,const RsxPoseFrame* f,uint32_t cb) {
    if(h->memo_ids[cb]>=0)return (uint32_t)h->memo_ids[cb];
    const float* matrix=f->cameras+(size_t)cb*32;uint64_t hash=1469598103934665603ull;
    for(uint32_t k=0;k<32;k++){uint32_t bits;memcpy(&bits,matrix+k,4);hash=(hash^bits)*1099511628211ull;}
    uint32_t bucket=rsx_pose_bucket(h,hash),visits=0;
    for(int32_t i=h->memo_heads[bucket];i>=0&&visits<128;i=h->memo_next[i],visits++) {
        if(h->memo_hashes[i]==hash&&!memcmp(matrix,f->cameras+(size_t)i*32,32*sizeof(float))) {
            h->memo_ids[cb]=i;return (uint32_t)i;
        }
    }
    /* If a hash chain is crowded, use a separate ID. Missing reuse is safe. */
    h->memo_hashes[cb]=hash;h->memo_next[cb]=h->memo_heads[bucket];h->memo_heads[bucket]=(int32_t)cb;
    h->memo_ids[cb]=(int32_t)cb;return cb;
}
static inline void rsx_pose_query(RsxPoseQuery* q,const RsxPoseFrame* f,uint32_t j,uint32_t camera_id) {
    const RsxPoseDraw* d=f->draws+j;q->count=d->count<16?d->count:16;
    q->camera=f->cameras+(size_t)d->cb*32;q->camera_id=camera_id;q->sample_id=j;
    for(uint32_t k=0;k<q->count;k++) {
        /* For small draws every vertex is sampled. Larger draws always
         * have 16 samples, so the denominator is the compile-time value 15. */
        uint32_t v=d->count<=16?k:(uint32_t)((uint64_t)k*(d->count-1)/15u);
        q->indices[k]=v;memcpy(q->xyz[k],f->vertices[d->first+v].xyz,3*sizeof(float));
        q->samples.x[k]=q->xyz[k][0];q->samples.y[k]=q->xyz[k][1];q->samples.z[k]=q->xyz[k][2];
    }
    for(uint32_t k=q->count;k<((q->count+3u)&~3u);k++)q->samples.x[k]=q->samples.y[k]=q->samples.z[k]=0;
}
/* Group only bit-identical samples with the same full vertex count. Hashes
 * select candidates; all sample coordinates are checked before sharing an ID.
 * A score cache hit skips repeated arithmetic, never a candidate or its visit. */
static inline uint32_t rsx_pose_sample_id(RsxPoseHistory* h,uint32_t j,const RsxPoseQuery* q) {
    const RsxPoseSnapshot* out=&h->cur;uint32_t count=out->draws[j].count;
    uint64_t hash=1469598103934665603ull^count;
    if(!q->count)return j;
    /* Compact bucket selector, NOT an identity: compare every sampled float
     * below, including interior vertices. Equal samples always hash alike. */
    const uint32_t pick[3]={0,q->count/2,q->count-1};
    for(uint32_t k=0;k<3;k++) {
        if(k&&pick[k]==pick[k-1])continue;
        for(uint32_t t=0;t<3;t++) {
            uint32_t bits;memcpy(&bits,&q->xyz[pick[k]][t],4);hash=(hash^bits)*1099511628211ull;
        }
    }
    uint32_t bucket=rsx_pose_bucket(h,hash),visits=0;
    for(int32_t i=h->sample_heads[bucket];i>=0&&visits<128;i=h->sample_next[i],visits++) {
        const RsxPoseDraw* d=out->draws+i;
        if(h->sample_hashes[i]!=hash||d->count!=count)continue;
        if(count<=16) {
            /* Small draws sample every vertex in order: one exact contiguous
             * compare replaces up to sixteen separate 12-byte comparisons. */
            if(memcmp(q->xyz,out->xyz+(size_t)d->first*3,(size_t)count*3*sizeof(float)))continue;
        }else {
            uint32_t k=0;
            for(;k<q->count;k++)if(memcmp(q->xyz[k],out->xyz+(size_t)(d->first+q->indices[k])*3,3*sizeof(float)))break;
            if(k!=q->count)continue;
        }
        h->sample_groups_reused++;return (uint32_t)i;
    }
    h->sample_hashes[j]=hash;h->sample_next[j]=h->sample_heads[bucket];h->sample_heads[bucket]=(int32_t)j;
    return j;
}
/* Cached only within the immutable PREVIOUS snapshot of this prepare.
 * Four sample lanes are packed once per visited draw, not gathered again
 * for every candidate comparison. Padding never reads past source vertices. */
static inline const RsxPoseSamples* rsx_pose_previous_samples(RsxPoseHistory* h,const RsxPoseSnapshot* s,uint32_t i,const RsxPoseQuery* q) {
    RsxPoseSamples* out=h->search_samples+i;
    if(h->search_epochs[i]!=h->memo_epoch) {
        const RsxPoseDraw* a=s->draws+i;
        for(uint32_t k=0;k<q->count;k++) {
            const float* x=s->xyz+(size_t)(a->first+q->indices[k])*3;
            out->x[k]=x[0];out->y[k]=x[1];out->z[k]=x[2];
        }
        for(uint32_t k=q->count;k<((q->count+3u)&~3u);k++)out->x[k]=out->y[k]=out->z[k]=0;
        h->search_epochs[i]=h->memo_epoch;
    }
    return out;
}
/* Upper bound on sums that can still satisfy the ORIGINAL rounded
 * sum/n-best <= epsilon predicate. Two upward float steps surround the
 * addition; one surrounds multiplication. Rejecting only above this bound
 * is conservative. The final candidate score still uses original division. */
/* Domain: finite positive values only (best >= 0, n in 1..16; the
 * search sentinel is 1e30). IEEE754 bit increment is nextafter toward +inf. */
static inline float rsx_pose_upper_float(float x) {
    uint32_t bits;memcpy(&bits,&x,4);bits++;memcpy(&x,&bits,4);return x;
}
static inline float rsx_pose_sum_bound(uint32_t n,float best) {
    float q=rsx_pose_upper_float(rsx_pose_upper_float(best+1e-6f));
    return rsx_pose_upper_float(q*(float)n);
}
/* Distances retain (dx*dx + dy*dy) + dz*dz and sample accumulation order.
 * A bound rejection returns -2, DISTINCT from an invalid distance (-1): it
 * must never be stored as a reusable distance for another best-score bound. */
static inline float rsx_pose_sample_sum_bounded(const RsxPoseSamples* a,const RsxPoseSamples* b,uint32_t n,float bound) {
    float sum=0;
#ifdef RSX_POSE_VECTOR_SAMPLES
    for(uint32_t k=0;k<n;k+=4) {
        __m128 dx=_mm_sub_ps(_mm_loadu_ps(a->x+k),_mm_loadu_ps(b->x+k));
        __m128 dy=_mm_sub_ps(_mm_loadu_ps(a->y+k),_mm_loadu_ps(b->y+k));
        __m128 dz=_mm_sub_ps(_mm_loadu_ps(a->z+k),_mm_loadu_ps(b->z+k));
        __m128 distance=_mm_add_ps(_mm_add_ps(_mm_mul_ps(dx,dx),_mm_mul_ps(dy,dy)),_mm_mul_ps(dz,dz));
        if(_mm_movemask_ps(_mm_cmpgt_ps(distance,_mm_set1_ps(64.f))))return -1;
        float lane[4];_mm_storeu_ps(lane,distance);
        for(uint32_t t=0;t<4&&k+t<n;t++)sum+=lane[t];
        /* Nonnegative terms and monotone float arithmetic guarantee that
         * this candidate cannot win OR affect the epsilon tie predicate. */
        if(sum>bound)return -2;
    }
#else
    for(uint32_t k=0;k<n;k++) {
        float dx=a->x[k]-b->x[k],dy=a->y[k]-b->y[k],dz=a->z[k]-b->z[k];
        float d=(dx*dx+dy*dy)+dz*dz;if(d>64.f)return -1;sum+=d;
        if((k==0||(k&3u)==3u||k+1==n)&&sum>bound)return -2;
    }
#endif
    return sum;
}
static inline float rsx_pose_sample_sum(const RsxPoseSamples* a,const RsxPoseSamples* b,uint32_t n,float best_score) {
    return rsx_pose_sample_sum_bounded(a,b,n,rsx_pose_sum_bound(n,best_score));
}
/* The immutable query owns a conservative sum limit. Recompute it only
 * when the winning score changes, not for every candidate or spatial block.
 * Probe misses still use the full-sum limit so cached distances stay reusable. */
static inline float rsx_pose_position_score_bounded(RsxPoseHistory* h,const RsxPoseSnapshot* s,uint32_t i,const RsxPoseQuery* q,float bound,float probe_bound) {
    const RsxPoseDraw* a=s->draws+i;
    uint32_t previous=s->sample_ids[i],current=q->sample_id;
    uint32_t slot=((previous*0x9E3779B1u)^(current*0x85EBCA6Bu))&(h->memo_slots-1);
    RsxPoseCameraMemo* memo=h->position_memo+slot;
    if(!h->position_memo_disabled&&memo->epoch==h->memo_epoch&&memo->previous==previous&&memo->current==current) {
        h->position_hits++;return memo->transform;
    }
    if(h->position_memo_disabled)h->position_uncached++;else h->position_misses++;
    /* Only use best-score bounds after the low-reuse memo bypass. Cache
     * misses while probing still compute a full, reusable sum. */
    if(h->position_memo_disabled) {
        const float* x=s->xyz+(size_t)a->first*3;float d=0;
        for(int t=0;t<3;t++){float delta=x[t]-q->xyz[0][t];d+=delta*delta;}
        if(d>64.f)return -1;
        if(d>bound){h->search_pruned++;return -2;}
    }
    const RsxPoseSamples* samples=rsx_pose_previous_samples(h,s,i,q);
    float sum=rsx_pose_sample_sum_bounded(samples,&q->samples,q->count,h->position_memo_disabled?bound:probe_bound);
    if(sum==-2){h->search_pruned++;return -2;}
    if(!h->position_memo_disabled) {
        memo->previous=previous;memo->current=current;memo->epoch=h->memo_epoch;memo->transform=sum;
        if(h->position_misses>=512&&h->position_hits<h->position_misses/16)h->position_memo_disabled=1;
    }
    return sum;
}
static inline float rsx_pose_position_score(RsxPoseHistory* h,const RsxPoseSnapshot* s,uint32_t i,const RsxPoseQuery* q,float best_score) {
    return rsx_pose_position_score_bounded(h,s,i,q,rsx_pose_sum_bound(q->count,best_score),rsx_pose_sum_bound(q->count,1e30f));
}
static inline float rsx_pose_camera_score(RsxPoseHistory* h,const RsxPoseSnapshot* s,uint32_t i,const RsxPoseQuery* q) {
    const float* ca=s->cameras+(size_t)i*32;
    if(h->camera_memo_disabled) {
        h->camera_uncached++;
        if(rsx_pose_camera_cut(ca,q->camera))return -1;
        float transform=0;for(uint32_t k=0;k<32;k++){float delta=ca[k]-q->camera[k];transform+=delta*delta;}
        return transform;
    }
    uint32_t previous=s->camera_ids[i],current=q->camera_id;
    uint32_t slot=((previous*0x9E3779B1u)^(current*0x85EBCA6Bu))&(h->memo_slots-1);
    RsxPoseCameraMemo* memo=h->camera_memo+slot;
    if(memo->epoch==h->memo_epoch&&memo->previous==previous&&memo->current==current) {
        h->camera_hits++;return memo->transform;
    }
    h->camera_misses++;
    float transform=0;
    if(rsx_pose_camera_cut(ca,q->camera))transform=-1;
    else for(uint32_t k=0;k<32;k++){float delta=ca[k]-q->camera[k];transform+=delta*delta;}
    memo->previous=previous;memo->current=current;memo->epoch=h->memo_epoch;memo->transform=transform;
    /* Probe afresh each frame. Distinct per-object matrices may have no reuse;
     * avoid paying a cache lookup on every candidate in that workload. Exact
     * matrix IDs are STILL built, so the next frame can recover full reuse. */
    if(h->camera_misses>=512&&h->camera_hits<h->camera_misses/16)h->camera_memo_disabled=1;
    return transform;
}
static inline float rsx_pose_score_bounded(RsxPoseHistory* h,const RsxPoseSnapshot* s,uint32_t i,const RsxPoseFrame* f,uint32_t j,const RsxPoseQuery* q,float best_score,float bound,float probe_bound) {
    const RsxPoseDraw *a=s->draws+i,*b=f->draws+j;
    if(a->key!=b->key||a->count!=b->count||!a->count)return -1;
    h->search_candidates++;
    RsxPoseSearchProfile* p=h->search_profile;
    if(p)p->position_calls++;
    uint32_t n=q->count;float sum=rsx_pose_position_score_bounded(h,s,i,q,bound,probe_bound);
    if(sum<0)return -1;
    if(sum/(float)n-best_score>1e-6f){h->search_pruned++;return -1;}
    /* Reject spatially impossible candidates BEFORE inspecting their camera.
     * Shadow passes repeat a small set of UV/topology keys throughout a room;
     * most candidates are distant. This changes only predicate order, never
     * the set of candidates, the score, ambiguity rules or search budget. */
    /* Identical local-space instances can carry their movement in constants
     * rather than vertices. Include transform proximity to disambiguate them. */
    if(p)p->camera_calls++;
    float transform=rsx_pose_camera_score(h,s,i,q);
    if(transform<0)return -1;
    return sum/(float)n+transform*.01f;
}
static inline float rsx_pose_score(RsxPoseHistory* h,const RsxPoseSnapshot* s,uint32_t i,const RsxPoseFrame* f,uint32_t j,const RsxPoseQuery* q,float best_score) {
    return rsx_pose_score_bounded(h,s,i,f,j,q,best_score,rsx_pose_sum_bound(q->count,best_score),rsx_pose_sum_bound(q->count,1e30f));
}
static inline int rsx_pose_same_track(const RsxPoseSnapshot* s,uint32_t a,uint32_t b) {
    const RsxPoseDraw *x=s->draws+a,*y=s->draws+b;size_t bytes=(size_t)x->count*3*sizeof(float);
    return x->count==y->count&&s->tracks[a].changed_at==s->tracks[b].changed_at&&
        !memcmp(s->xyz+(size_t)x->first*3,s->xyz+(size_t)y->first*3,bytes)&&
        !memcmp(s->old_xyz+(size_t)x->first*3,s->old_xyz+(size_t)y->first*3,bytes)&&
        !memcmp(s->cameras+(size_t)a*32,s->cameras+(size_t)b*32,32*sizeof(float))&&
        !memcmp(s->old_cameras+(size_t)a*32,s->old_cameras+(size_t)b*32,32*sizeof(float));
}
static inline void rsx_pose_build_blocks(RsxPoseHistory* h,const RsxPoseSnapshot* s) {
    for(uint32_t bucket=0;bucket<h->buckets;bucket++) {
        /* Count includes collisions and zero-count draws: these chains
         * always use the cap fallback, so no bounds are queried. */
        if(!h->repeat_work_disabled&&!h->cap_precheck_disabled&&h->bucket_capped[bucket]>128){h->bucket_capped[bucket]=1;continue;}
        uint32_t visits=0;
        int32_t first=h->heads[bucket];
        for(;first>=0&&visits<128;) {
            RsxPoseBlock* block=h->blocks+first;
            for(int k=0;k<3;k++){block->lo[k]=INFINITY;block->hi[k]=-INFINITY;}
            int32_t i=first;block->count=0;
            for(;i>=0&&block->count<8;i=h->next[i],block->count++) {
                const RsxPoseDraw* d=s->draws+i;
                /* An over-capacity draw has count zero and cannot match.
                 * Its first index may equal maxv: never dereference it. */
                if(!d->count)continue;
                const float* xyz=s->xyz+(size_t)d->first*3;
                for(int k=0;k<3;k++) {
                    if(xyz[k]<block->lo[k])block->lo[k]=xyz[k];
                    if(xyz[k]>block->hi[k])block->hi[k]=xyz[k];
                }
            }
            block->after=i;first=i;visits+=block->count;
        }
        h->bucket_capped[bucket]=(uint8_t)(first>=0&&visits>=128);
    }
}
/* SIMD lower bound for ONE existing block. Keep the exact scalar order
 * (dx*dx+dy*dy)+dz*dz; the fourth lane is zero and never reads past xyz[2].
 * No candidate reordering, new bounds or changed visit accounting. */
static inline float rsx_pose_block_distance(const RsxPoseBlock* block,const float* xyz) {
#ifdef RSX_POSE_VECTOR_SAMPLES
    __m128 point=_mm_set_ps(0,xyz[2],xyz[1],xyz[0]);
    __m128 lo=_mm_set_ps(0,block->lo[2],block->lo[1],block->lo[0]);
    __m128 hi=_mm_set_ps(0,block->hi[2],block->hi[1],block->hi[0]);
    __m128 delta=_mm_max_ps(_mm_max_ps(_mm_sub_ps(lo,point),_mm_sub_ps(point,hi)),_mm_setzero_ps());
    __m128 squares=_mm_mul_ps(delta,delta);
    __m128 xy=_mm_add_ss(squares,_mm_shuffle_ps(squares,squares,_MM_SHUFFLE(1,1,1,1)));
    return _mm_cvtss_f32(_mm_add_ss(xy,_mm_shuffle_ps(squares,squares,_MM_SHUFFLE(2,2,2,2))));
#else
    float distance=0;
    for(int k=0;k<3;k++) {
        float delta=xyz[k]<block->lo[k]?block->lo[k]-xyz[k]:xyz[k]>block->hi[k]?xyz[k]-block->hi[k]:0;
        distance+=delta*delta;
    }
    return distance;
#endif
}
static inline int rsx_pose_block_distant(const RsxPoseBlock* block,const float* xyz) {
    return rsx_pose_block_distance(block,xyz)>64.f;
}
/* Same conservative upper sum bound as v27; only calculation of spatial
 * distance is shared and branchless on SSE2 hosts. */
static inline int rsx_pose_block_cannot_win_bounded(const RsxPoseBlock* block,const float* xyz,uint32_t samples,float bound) {
    float distance=rsx_pose_block_distance(block,xyz);
    return distance>64.f||(samples&&distance>bound);
}
static inline int rsx_pose_block_cannot_win(const RsxPoseBlock* block,const float* xyz,uint32_t samples,float best_score) {
    return rsx_pose_block_cannot_win_bounded(block,xyz,samples,rsx_pose_sum_bound(samples,best_score));
}
/* Reappearance may miss the immediately preceding list (culling/light-pass
 * changes). Carry ONLY exact stationary geometry from the last two snapshots,
 * with the same complete camera. Not a proximity test or a temporal source.
 * A verified match is a current-position constraint, never an invented pose.
 * Hash/chain storage is rebuilt by the ordinary matcher immediately after. */
/* Pack four strided capture records into twelve contiguous position floats.
 * Integer shuffles preserve every bit, including signed zero. Loads include
 * only xyz/uv of each complete record, never the next record or allocation.
 * No identity, correspondence, camera or fallback decision is cached here. */
static inline void rsx_pose_pack_positions(float* out,const RsxPoseVertex* in,uint32_t count,int vectors) {
    uint32_t i=0;
#ifdef RSX_POSE_VECTOR_SAMPLES
    if(vectors)for(;i+4<=count;i+=4) {
        __m128i a=_mm_loadu_si128((const __m128i*)(in+i));
        __m128i b=_mm_loadu_si128((const __m128i*)(in+i+1));
        __m128i c=_mm_loadu_si128((const __m128i*)(in+i+2));
        __m128i d=_mm_loadu_si128((const __m128i*)(in+i+3));
        __m128i x=_mm_or_si128(_mm_srli_si128(_mm_slli_si128(a,4),4),_mm_slli_si128(b,12));
        __m128i y=_mm_or_si128(_mm_srli_si128(_mm_slli_si128(b,4),8),_mm_slli_si128(c,8));
        __m128i z=_mm_or_si128(_mm_srli_si128(_mm_slli_si128(c,4),12),_mm_slli_si128(d,4));
        _mm_storeu_si128((__m128i*)(out+(size_t)i*3),x);
        _mm_storeu_si128((__m128i*)(out+(size_t)i*3+4),y);
        _mm_storeu_si128((__m128i*)(out+(size_t)i*3+8),z);
    }
#else
    (void)vectors;
#endif
    for(;i<count;i++)memcpy(out+(size_t)i*3,in[i].xyz,3*sizeof(float));
}
static inline int rsx_pose_stationary_stream(const float* a,const float* b,uint32_t count,int vectors) {
    uint32_t i=0;
#ifdef RSX_POSE_VECTOR_SAMPLES
    if(vectors) {
        const __m128 mask=_mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
        const __m128 limit=_mm_set1_ps(1e-5f);
        for(;i+4<=count;i+=4) {
            __m128 delta=_mm_and_ps(_mm_sub_ps(_mm_loadu_ps(a+i),_mm_loadu_ps(b+i)),mask);
            /* Ordered > retains the scalar fabsf(delta)>limit semantics. */
            if(_mm_movemask_ps(_mm_cmpgt_ps(delta,limit)))return 0;
        }
    }
#else
    (void)vectors;
#endif
    for(;i<count;i++)if(fabsf(a[i]-b[i])>1e-5f)return 0;
    return 1;
}
static inline int rsx_pose_stationary_endpoints(const float* a,const float* b,uint32_t count) {
    for(uint32_t k=0;k<count;k++)if(fabsf(a[k]-b[k])>1e-5f)return 0;return 1;
}
static inline uint64_t rsx_pose_anchor_hash(uint32_t count,const float* point) {
    uint32_t bits[3];memcpy(bits,point,12);uint64_t hash=count;
    for(int a=0;a<3;a++)hash=(hash^bits[a])*1099511628211ull;return hash;
}
static inline void rsx_pose_carry_stationary(RsxPoseHistory* h,const RsxPoseFrame* f) {
    const RsxPoseSnapshot* snapshots[2]={&h->cur,&h->prev};
    for(int frame=0;frame<2;frame++) {
        const RsxPoseSnapshot* old=snapshots[frame];if(old->scene!=f->scene)continue;
        memset(h->heads,0xFF,(size_t)h->buckets*sizeof(int32_t));
        for(uint32_t i=0;i<old->nd;i++)if(old->draws[i].count&&
            (old->stationary[i]||rsx_pose_stationary_stream(old->xyz+(size_t)old->draws[i].first*3,
                old->old_xyz+(size_t)old->draws[i].first*3,old->draws[i].count*3,h->stream_vectors))) {
            const RsxPoseDraw* d=old->draws+i;
            uint32_t bucket=rsx_pose_bucket(h,rsx_pose_anchor_hash(d->count,old->xyz+(size_t)d->first*3));
            h->next[i]=h->heads[bucket];h->heads[bucket]=(int32_t)i;
        }
        for(uint32_t i=0;i<f->nd;i++)if(!h->stationary_draws[i]) {
            const RsxPoseDraw* d=f->draws+i;
            uint32_t bucket=rsx_pose_bucket(h,rsx_pose_anchor_hash(d->count,f->vertices[d->first].xyz)),visits=0;
            for(int32_t j=h->heads[bucket];j>=0&&visits++<128;j=h->next[j]) {
                const RsxPoseDraw* previous=old->draws+j;
                if(previous->count!=d->count||memcmp(f->cameras+(size_t)d->cb*32,old->cameras+(size_t)j*32,128))continue;
                uint32_t k=0;for(;k<d->count;k++)if(memcmp(f->vertices[d->first+k].xyz,old->xyz+(size_t)(previous->first+k)*3,12))break;
                if(k==d->count){h->stationary_draws[i]=1;break;}
            }
        }
    }
}
/* A static world-space caster remains static in every light projection.
 * Share the constraint only between the whitelisted world-space shadow VP,
 * with exact full geometry in the verified common world input space.
 * Only shadow targets inherit it; never share interpolated values/cameras. */
static inline void rsx_pose_share_world_stationary(RsxPoseHistory* h,const RsxPoseFrame* f) {
    memset(h->heads,0xFF,(size_t)h->buckets*sizeof(int32_t));
    for(uint32_t i=0;i<f->nd;i++)if(h->stationary_draws[i]&&(f->draws[i].flags&RSX_POSE_WORLD_LIGHT_GEOMETRY)) {
        const RsxPoseDraw* d=f->draws+i;uint32_t bucket=rsx_pose_bucket(h,rsx_pose_anchor_hash(d->count,f->vertices[d->first].xyz));
        h->next[i]=h->heads[bucket];h->heads[bucket]=(int32_t)i;
    }
    for(uint32_t i=0;i<f->nd;i++)if(!h->stationary_draws[i]&&((f->draws[i].flags&3u)==3u)) {
        const RsxPoseDraw* d=f->draws+i;uint32_t visits=0,bucket=rsx_pose_bucket(h,rsx_pose_anchor_hash(d->count,f->vertices[d->first].xyz));
        for(int32_t j=h->heads[bucket];j>=0&&visits++<128;j=h->next[j]) {
            const RsxPoseDraw* other=f->draws+j;
            if(d->count!=other->count)continue;
            uint32_t k=0;for(;k<d->count;k++)if(memcmp(f->vertices[d->first+k].xyz,f->vertices[other->first+k].xyz,12))break;
            if(k==d->count){h->stationary_draws[i]=h->cur.stationary[i]=1;break;}
        }
    }
}
static inline int rsx_pose_history_compare(RsxPoseHistory* h,uint32_t current,uint32_t previous,
                                          const float* xyz,const float* old,uint32_t count,int* changed) {
    /* IDs for <=16 vertices compare ALL coordinates bit-for-bit, including
     * count. Bigger draws only sample coordinates and must not use this cache.
     * Epoch is specific to this pair of immutable snapshots, never cross-frame. */
    if(!h->repeat_reuse_disabled&&h->history_memo&&count<=16) {
        uint32_t slot=(current*2654435761u^previous*2246822519u^count)&(h->memo_slots-1);
        RsxPoseHistoryMemo* m=h->history_memo+slot;
        if(m->epoch==h->memo_epoch&&m->current==current&&m->previous==previous&&m->count==count) {
            h->history_reuse_hits++;*changed|=(int)((m->flags>>1)&1u);return (int)(m->flags&1u);
        }
        int geometry_changed=0;
        int jump=h->position_compare_scalar?rsx_pose_compare_scalar(xyz,old,count,&geometry_changed):
            rsx_pose_compare_vectors(xyz,old,count,&geometry_changed);
        m->current=current;m->previous=previous;m->count=count;
        m->flags=(uint32_t)jump|((uint32_t)geometry_changed<<1);m->epoch=h->memo_epoch;
        *changed|=geometry_changed;h->history_reuse_misses++;return jump;
    }
    return h->position_compare_scalar?rsx_pose_compare_scalar(xyz,old,count,changed):
        rsx_pose_compare_vectors(xyz,old,count,changed);
}
static inline int rsx_pose_prepare(RsxPoseHistory* h,const RsxPoseFrame* f,double now) {
    RsxPoseSearchProfile* profile=h->search_profile;
    double profile_mark=profile?profile->clock():0;
    h->capped_searches_skipped=0;
    h->history_reuse_hits=h->history_reuse_misses=h->weld_reuse_hits=h->weld_reuse_misses=0;
    h->validated_vertices=h->reused_validations=0;
    h->camera_hits=h->camera_misses=h->camera_uncached=0;h->camera_memo_disabled=0;
    h->position_hits=h->position_misses=h->position_uncached=0;h->position_memo_disabled=0;
    h->search_candidates=h->search_pruned=h->sample_groups_reused=h->search_blocks_pruned=0;
    h->matched=h->moving=h->ambiguous=h->capped=0;h->coherent=h->coherence_conflicts=h->seams=h->seam_conflicts=h->seam_components=h->seam_veto_vertices=0;h->alpha=1;
    memset(h->stationary_draws,0,h->maxd);
    if(!f->scene||!f->nd){rsx_pose_reset(h);h->reason=RSX_POSE_OFF;return 0;}
    int valid=isfinite(now)&&rsx_pose_frame_valid(h,f);
    rsx_pose_profile_section(profile,RSX_POSE_P_VALIDATE,&profile_mark);
    if(!valid){rsx_pose_reset(h);h->reason=RSX_POSE_INVALID;return 0;}
    int warm=!h->have_cur;
    int reset=warm||f->scene!=h->cur.scene||now<h->last_at||now-h->last_at>.25;
    if(!reset)rsx_pose_carry_stationary(h,f);
    RsxPoseSnapshot tmp=h->prev;h->prev=h->cur;h->cur=tmp;
    RsxPoseSnapshot *s=&h->prev,*out=&h->cur;
    memset(out->stationary,0,h->maxd);
    out->nd=f->nd;out->scene=f->scene;out->nv=0;
    memset(h->heads,0xFF,(size_t)h->buckets*sizeof(int32_t));memset(h->used,0,h->maxd);
    memset(h->bucket_capped,0,h->buckets);
    memset(h->sample_heads,0xFF,(size_t)h->buckets*sizeof(int32_t));
    if(++h->memo_epoch==0){memset(h->camera_memo,0,(size_t)h->memo_slots*sizeof(RsxPoseCameraMemo));memset(h->position_memo,0,(size_t)h->memo_slots*sizeof(RsxPoseCameraMemo));memset(h->search_epochs,0,(size_t)h->maxd*sizeof(uint32_t));if(h->history_memo)memset(h->history_memo,0,(size_t)h->memo_slots*sizeof(*h->history_memo));h->memo_epoch=1;}
    if(!reset)for(uint32_t i=0;i<s->nd;i++) {
        uint32_t bucket=rsx_pose_bucket(h,s->draws[i].key);
        h->next[i]=h->heads[bucket];h->heads[bucket]=(int32_t)i;
        if(h->bucket_capped[bucket]<129)h->bucket_capped[bucket]++;
    }
    if(!reset)rsx_pose_build_blocks(h,s);
    rsx_pose_profile_section(profile,RSX_POSE_P_INDEX,&profile_mark);
    int any_new=0,apply=0;
    for(uint32_t j=0;j<f->nd;j++) {
        const RsxPoseDraw* b=f->draws+j;RsxPoseDraw* d=out->draws+j;
        *d=*b;d->first=out->nv;out->stationary[j]=h->stationary_draws[j];
        RsxPoseTrack* tr=out->tracks+j;tr->changed_at=now;tr->alpha=1;tr->active=0;
        if(b->count>h->maxv-out->nv){d->count=0;h->capped++;rsx_pose_profile_section(profile,RSX_POSE_P_SETUP,&profile_mark);continue;}
        float *xyz=out->xyz+(size_t)d->first*3,*old=out->old_xyz+(size_t)d->first*3;
        float *cam=out->cameras+(size_t)j*32,*oldcam=out->old_cameras+(size_t)j*32;
        out->nv+=b->count;
        rsx_pose_pack_positions(xyz,f->vertices+b->first,b->count,h->stream_vectors);
        memcpy(cam,f->cameras+(size_t)b->cb*32,32*sizeof(float));
        /* Fill fallback history only when needed. A successful match below
         * always overwrites these buffers with previous-frame history. */
        out->camera_ids[j]=rsx_pose_camera_id(h,f,b->cb);
        RsxPoseQuery query;rsx_pose_query(&query,f,j,out->camera_ids[j]);
        /* Probe the current input, not old memo hits: recovery still works
         * after a unique-geometry frame assigned only singleton IDs. Stop
         * hashing all remaining samples when 512 draws found little reuse.
         * Singleton j cannot alias any earlier group's representative (<j). */
        if(j>=512&&h->sample_groups_reused<16) {
            query.sample_id=out->sample_ids[j]=j;h->position_memo_disabled=1;
        }else query.sample_id=out->sample_ids[j]=rsx_pose_sample_id(h,j,&query);
        rsx_pose_profile_section(profile,RSX_POSE_P_SETUP,&profile_mark);
        int32_t best=-1;float best_score=1e30f;int ambiguous=0;uint32_t visits=0;
        const float probe_bound=rsx_pose_sum_bound(query.count,1e30f);float bound=probe_bound;
        const uint32_t bucket=rsx_pose_bucket(h,b->key);
        /* All branches consume visits, including used/key-mismatched draws
         * and spatially skipped blocks. A chain with a 129th entry ALWAYS
         * discards best at the cap, regardless of scores or ambiguity.
         * Preserve that fallback without evaluating discarded candidates.
         * Ambiguity diagnostics now count only searches that finish. */
        int32_t head=h->heads[bucket];
        if(!h->cap_precheck_disabled&&h->bucket_capped[bucket]) {
            h->capped++;h->capped_searches_skipped++;head=-1;
        }
        for(int32_t i=head;i>=0;) {
            if(visits>=128){best=-1;h->capped++;break;}
            const RsxPoseBlock* block=h->blocks+i;
            if(block->count>1&&rsx_pose_block_cannot_win_bounded(block,xyz,query.count,bound)) {
                h->search_blocks_pruned++;visits+=block->count;i=block->after;continue;
            }
            for(uint32_t k=0;k<block->count;k++,i=h->next[i]) {
                ++visits;
                if(h->used[i])continue;
                float score=rsx_pose_score_bounded(h,s,(uint32_t)i,f,j,&query,best_score,bound,probe_bound);if(score<0)continue;
                if(best<0||score<best_score-1e-6f){best=i;best_score=score;bound=rsx_pose_sum_bound(query.count,best_score);ambiguous=0;}
                else if(fabsf(score-best_score)<=1e-6f&&!rsx_pose_same_track(s,(uint32_t)best,(uint32_t)i))ambiguous=1;
            }
        }
        rsx_pose_profile_section(profile,RSX_POSE_P_SEARCH,&profile_mark);
        if(ambiguous||best<0) {
            if(ambiguous)h->ambiguous++;
            memcpy(old,xyz,(size_t)b->count*3*sizeof(float));memcpy(oldcam,cam,32*sizeof(float));
            rsx_pose_profile_section(profile,RSX_POSE_P_RESOLVE,&profile_mark);
            continue;
        }
        const RsxPoseDraw* a=s->draws+best;
        const float *prevxyz=s->xyz+(size_t)a->first*3,*prevcam=s->cameras+(size_t)best*32;
        int changed=memcmp(cam,prevcam,32*sizeof(float))!=0,jump=0;
        jump=rsx_pose_history_compare(h,out->sample_ids[j],s->sample_ids[best],xyz,prevxyz,b->count,&changed);
        if(jump) {
            memcpy(old,xyz,(size_t)b->count*3*sizeof(float));memcpy(oldcam,cam,32*sizeof(float));
            rsx_pose_profile_section(profile,RSX_POSE_P_RESOLVE,&profile_mark);
            continue;
        }
        h->used[best]=1;h->matched++;
        if(changed) {
            memcpy(old,prevxyz,(size_t)b->count*3*sizeof(float));memcpy(oldcam,prevcam,32*sizeof(float));
            tr->active=1;any_new=1;
        }else {
            memcpy(old,s->old_xyz+(size_t)a->first*3,(size_t)b->count*3*sizeof(float));
            memcpy(oldcam,s->old_cameras+(size_t)best*32,32*sizeof(float));*tr=s->tracks[best];
        }
        /* Only a successful temporal match can establish a stationary
         * surface. Unmatched/newly-visible draws with old==new are NOT proof.
         * A repeated animated midpoint still has old!=new, so is not pinned. */
        h->stationary_draws[j]|=(uint8_t)rsx_pose_stationary_stream(old,xyz,b->count*3,h->stream_vectors);
        out->stationary[j]=h->stationary_draws[j];
        if(tr->active) {
            /* Midpoint on arrival, endpoint after one 60 Hz refresh. Real
             * elapsed time handles jitter without touching the game clock. */
            double alpha=.5+(now-tr->changed_at)*30.0;
            tr->alpha=(float)(alpha>1?1:alpha<.5?.5:alpha);
            if(tr->alpha<1){h->moving++;apply=1;h->alpha=tr->alpha;}
        }
        rsx_pose_profile_section(profile,RSX_POSE_P_RESOLVE,&profile_mark);
    }
    rsx_pose_share_world_stationary(h,f);
    h->have_cur=1;h->have_prev=apply;h->last_at=now;
    h->reason=reset?(warm?RSX_POSE_WARMUP:RSX_POSE_LAYOUT):!h->matched?RSX_POSE_CUT:any_new?RSX_POSE_NEW:RSX_POSE_REPEAT;
    return apply;
}
static inline int rsx_pose_draw_ready(const RsxPoseHistory* h,uint32_t draw) {
    return draw<h->cur.nd&&h->cur.draws[draw].count&&h->cur.tracks[draw].active&&h->cur.tracks[draw].alpha<1;
}
static inline void rsx_pose_vertex(const RsxPoseHistory* h,uint32_t draw,uint32_t vertex,float out[3]) {
    const RsxPoseDraw* d=h->cur.draws+draw;
    const float *a=h->cur.old_xyz+(size_t)(d->first+vertex)*3,*b=h->cur.xyz+(size_t)(d->first+vertex)*3;
    float t=h->cur.tracks[draw].alpha;for(int k=0;k<3;k++)out[k]=a[k]+(b[k]-a[k])*t;
}
static inline void rsx_pose_camera(const RsxPoseHistory* h,uint32_t draw,float out[32]) {
    const float *a=h->cur.old_cameras+(size_t)draw*32,*b=h->cur.cameras+(size_t)draw*32;
    float t=h->cur.tracks[draw].alpha;for(int k=0;k<32;k++)out[k]=a[k]+(b[k]-a[k])*t;
}
static inline int rsx_pose_floats_equal(const float* a,const float* b,uint32_t n) {
    uint32_t i=0;
#ifdef RSX_POSE_VECTOR_SAMPLES
    /* Retain scalar != semantics (NaNs and signed zero); load only complete
     * groups so a three-float vertex never reads beyond its allocation. */
    for(;i+4<=n;i+=4)if(_mm_movemask_ps(_mm_cmpneq_ps(_mm_loadu_ps(a+i),_mm_loadu_ps(b+i))))return 0;
#endif
    for(;i<n;i++)if(a[i]!=b[i])return 0;return 1;
}
static inline uint64_t rsx_pose_hash_floats(uint64_t hash,const float* f,uint32_t n) {
    for(uint32_t i=0;i<n;i++){uint32_t bits=0;if(f[i]!=0)memcpy(&bits,f+i,4);hash=(hash^bits)*1099511628211ull;}return hash;
}
static inline int rsx_pose_same_geometry(const RsxPoseFrame* f,uint32_t a,uint32_t b) {
    const RsxPoseDraw *x=f->draws+a,*y=f->draws+b;
    if(x->count!=y->count||!rsx_pose_floats_equal(f->cameras+(size_t)x->cb*32,f->cameras+(size_t)y->cb*32,32))return 0;
    for(uint32_t k=0;k<x->count;k++)if(!rsx_pose_floats_equal(f->vertices[x->first+k].xyz,f->vertices[y->first+k].xyz,3))return 0;
    return 1;
}
/* A depth prepass, additive lighting and the material pass may have different
 * shaders, UV layouts and temporal matches, yet rasterize the SAME surface.
 * All current-frame copies must receive bit-identical positions/constants.
 * A recognized history seeds the entire exact group. Conflicting histories
 * snap the group together; an unmatched copy must never use its own phase.
 * Cameras are grouped separately: all geometry using one original camera
 * receives the same camera, including newly appeared objects.
 */
static inline int rsx_pose_cohere_ex(RsxPoseHistory* h,const RsxPoseFrame* f,int geometry_on_gpu) {
    h->coherent=h->coherence_conflicts=0;
    memset(h->vertex_sources,0xFF,(size_t)f->nd*sizeof(int32_t));
    memset(h->camera_sources,0xFF,(size_t)f->nd*sizeof(int32_t));
    memset(h->camera_checked,0,h->maxd);
    for(int geometry=0;geometry<2;geometry++) {
        memset(h->heads,0xFF,(size_t)h->buckets*sizeof(int32_t));
        for(uint32_t i=0;i<f->nd;i++) {
            const RsxPoseDraw* d=f->draws+i;
            const float* camera=f->cameras+(size_t)d->cb*32;
            /* Reuse the same canonical-zero hash while this frame is
             * immutable, retaining the original buckets and visit limits. */
            if(!h->camera_checked[d->cb]) {
                h->coherence_hashes[d->cb]=rsx_pose_hash_floats(1469598103934665603ull,camera,32);
                h->camera_checked[d->cb]=1;
            }
            uint64_t key=h->coherence_hashes[d->cb];
            if(geometry){key=(key^d->count)*1099511628211ull;for(uint32_t k=0;k<d->count;k++)key=rsx_pose_hash_floats(key,f->vertices[d->first+k].xyz,3);}
            uint32_t bucket=rsx_pose_bucket(h,key),visits=0;int32_t group=-1;
            for(int32_t g=h->heads[bucket];g>=0;g=h->next[g]) {
                if(++visits>128)goto overflow;
                if(geometry?rsx_pose_same_geometry(f,i,(uint32_t)g):rsx_pose_floats_equal(camera,f->cameras+(size_t)f->draws[g].cb*32,32)){group=g;break;}
            }
            if(group<0){group=(int32_t)i;h->next[i]=h->heads[bucket];h->heads[bucket]=(int32_t)i;h->seeds[i]=-1;}
            h->groups[i]=group;
            /* A light matrix is not an independent display camera. Until
             * all FP readers and palette banks use the same interpolated
             * light, retain this projection collectively, including copies
             * with no temporal history. Geometry may still interpolate. */
            if(!geometry&&(d->flags&RSX_POSE_CURRENT_LIGHT_CAMERA)){h->seeds[group]=-2;continue;}
            /* Compute mode builds exact groups here; numerical geometry
             * proposals/conflicts are resolved collectively by GPU passes. */
            if(geometry&&geometry_on_gpu)continue;
            if(!rsx_pose_draw_ready(h,i)||h->seeds[group]==-2)continue;
            int32_t seed=h->seeds[group];int changed=0,conflict=0;
            if(geometry) {
                const RsxPoseDraw* current=h->cur.draws+i;
                if(!memcmp(h->cur.xyz+(size_t)current->first*3,h->cur.old_xyz+(size_t)current->first*3,(size_t)current->count*3*sizeof(float)))continue;
                changed=1;
                if(seed>=0)for(uint32_t k=0;k<d->count;k++){float a[3],b[3];rsx_pose_vertex(h,i,k,a);rsx_pose_vertex(h,(uint32_t)seed,k,b);if(!rsx_pose_floats_equal(a,b,3)){conflict=1;break;}}
            }else {
                float a[32],b[32];rsx_pose_camera(h,i,a);changed=!rsx_pose_floats_equal(a,camera,32);
                if(changed&&seed>=0){rsx_pose_camera(h,(uint32_t)seed,b);conflict=!rsx_pose_floats_equal(a,b,32);}
            }
            if(conflict){h->seeds[group]=-2;h->coherence_conflicts++;}
            else if(changed&&seed<0)h->seeds[group]=(int32_t)i;
        }
        for(uint32_t i=0;i<f->nd;i++) {
            int32_t seed=h->seeds[h->groups[i]];
            if(geometry)h->vertex_sources[i]=seed;else {h->camera_sources[i]=seed;h->camera_groups[i]=h->groups[i];}
            if(seed>=0&&(uint32_t)seed!=i)h->coherent++;
        }
    }
    return 1;
overflow:
    /* Incomplete grouping cannot guarantee cross-pass consistency. */
    memset(h->vertex_sources,0xFF,(size_t)f->nd*sizeof(int32_t));
    memset(h->camera_sources,0xFF,(size_t)f->nd*sizeof(int32_t));
    h->coherent=0;h->coherence_conflicts++;return 0;
}
static inline int rsx_pose_cohere(RsxPoseHistory* h,const RsxPoseFrame* f) {
    return rsx_pose_cohere_ex(h,f,0);
}
static inline int32_t rsx_pose_component(RsxPoseHistory* h,int32_t i) {
    int32_t root=i;while(h->next[root]!=root)root=h->next[root];
    while(h->next[i]!=i){int32_t next=h->next[i];h->next[i]=root;i=next;}return root;
}
static inline void rsx_pose_join(RsxPoseHistory* h,int32_t a,int32_t b) {
    a=rsx_pose_component(h,a);b=rsx_pose_component(h,b);if(a!=b){if(a<b)h->next[b]=a;else h->next[a]=b;}
}
/* A final alias veto must be collective too: reverting just one upload slot
 * after cohere would put a depth/material copy back on a different pose.
 * Union exact surface copies and all overlapping upload ranges, then spread
 * vetoes through their connected components. Cameras use their own groups.
 * The supplied arrays use 0=unused, 1=proposed write, 2=retain original.
 */
static inline void rsx_pose_protect_aliases(RsxPoseHistory* h,const RsxPoseFrame* f,uint32_t* vertices,uint32_t* cameras) {
    memset(h->vertex_owners,0,(size_t)f->nv*sizeof(uint32_t));
    for(uint32_t i=0;i<f->nd;i++)h->next[i]=(int32_t)i;
    for(uint32_t i=0;i<f->nd;i++) {
        rsx_pose_join(h,(int32_t)i,h->groups[i]);
        const RsxPoseDraw* d=f->draws+i;
        for(uint32_t k=0;k<d->count;k++) {
            uint32_t* owner=h->vertex_owners+d->first+k;
            if(*owner)rsx_pose_join(h,(int32_t)i,(int32_t)(*owner-1));else *owner=i+1;
        }
    }
    memset(h->seeds,0,(size_t)f->nd*sizeof(int32_t));
    for(uint32_t i=0;i<f->nd;i++) {
        const RsxPoseDraw* d=f->draws+i;
        for(uint32_t k=0;k<d->count;k++)if(vertices[d->first+k]==2){h->seeds[rsx_pose_component(h,(int32_t)i)]=1;break;}
    }
    for(uint32_t i=0;i<f->nd;i++)if(h->seeds[rsx_pose_component(h,(int32_t)i)]) {
        const RsxPoseDraw* d=f->draws+i;for(uint32_t k=0;k<d->count;k++)vertices[d->first+k]=2;
    }
    memset(h->seeds,0,(size_t)f->nd*sizeof(int32_t));
    for(uint32_t i=0;i<f->nd;i++)if(cameras[f->draws[i].cb]==2)h->seeds[h->camera_groups[i]]=1;
    for(uint32_t i=0;i<f->nd;i++)if(h->seeds[h->camera_groups[i]])cameras[f->draws[i].cb]=2;
}
/* Upload slots are nodes, exact point/camera matches are edges. Sharing one
 * upload across two cameras links both sets of seam copies transitively.
 * Merely having equal positions in different cameras does NOT link them. */
static inline uint32_t rsx_pose_point_component(RsxPoseHistory* h,uint32_t v) {
    uint32_t root=v;while(h->vertex_groups[root]!=root+1)root=h->vertex_groups[root]-1;
    while(v!=root){uint32_t next=h->vertex_groups[v]-1;h->vertex_groups[v]=root+1;v=next;}return root;
}
/* Same upload or exact point/camera joins draws into a surface component.
 * Scratch is no longer needed by the completed point hash at this stage.
 * Used by CPU resolve and GPU plan construction; no positional tolerance. */
static inline void rsx_pose_mesh_components(RsxPoseHistory* h,const RsxPoseFrame* f) {
    memset(h->vertex_cameras,0,(size_t)f->nv*sizeof(uint32_t));
    for(uint32_t i=0;i<f->nd;i++)h->next[i]=(int32_t)i;
    for(uint32_t i=0;i<f->nd;i++) {
        const RsxPoseDraw* d=f->draws+i;
        for(uint32_t k=0;k<d->count;k++) {
            uint32_t owner=h->vertex_owners[d->first+k];if(!owner)continue;
            uint32_t* draw=h->vertex_cameras+owner-1;
            if(*draw)rsx_pose_join(h,(int32_t)i,(int32_t)(*draw-1));else *draw=i+1;
        }
    }
    /* Eyes, mouth inserts and translucent lashes need not share a vertex
     * with the skin. If their enclosing animated surface is retained, they
     * must retain the same current pose. This is ONLY a conservative fallback
     * relation: it never welds nearby points or invents an interpolated pose.
     * Require exact camera identity, full bounds containment, substantially
     * larger geometry/volume, and real positional animation in the container.
     * Unrelated objects outside these bounds keep their interpolation.
     * Search scratch is dead after prepare and rebuilt on the next prepare. */
    memset(h->blocks,0,(size_t)f->nd*sizeof(RsxPoseBlock));
    memset(h->used,0,f->nd);
    for(uint32_t i=0;i<f->nd;i++) {
        uint32_t root=(uint32_t)rsx_pose_component(h,(int32_t)i);
        RsxPoseBlock* box=h->blocks+root;const RsxPoseDraw* d=f->draws+i;
        if(!box->count){box->after=h->camera_groups[i];for(int a=0;a<3;a++){box->lo[a]=INFINITY;box->hi[a]=-INFINITY;}}
        else if(box->after!=h->camera_groups[i])box->after=-2;
        box->count=UINT32_MAX-box->count<d->count?UINT32_MAX:box->count+d->count;
        for(uint32_t k=0;k<d->count;k++)for(int a=0;a<3;a++) {
            float v=f->vertices[d->first+k].xyz[a];if(v<box->lo[a])box->lo[a]=v;if(v>box->hi[a])box->hi[a]=v;
        }
        if(!h->used[root]&&rsx_pose_draw_ready(h,i)) {
            const RsxPoseDraw* old=h->cur.draws+i;
            for(uint32_t k=0;k<old->count*3;k++)if(fabsf(h->cur.xyz[(size_t)old->first*3+k]-h->cur.old_xyz[(size_t)old->first*3+k])>1e-5f){h->used[root]=1;break;}
        }
    }
    uint32_t parents=0;
    for(uint32_t i=0;i<f->nd;i++)if(h->blocks[i].count>=24&&h->blocks[i].after>=0&&h->used[i]) {
        /* Too many potential containers: omit optional enclosure grouping.
         * Exact seam/alias protection remains intact; no quadratic scan. */
        if(parents==128)return;h->heads[parents++]=(int32_t)i;
    }
    for(uint32_t i=0;i<f->nd;i++) {
        const RsxPoseBlock* child=h->blocks+i;if(!child->count||child->after<0)continue;
        int32_t container=-1;int ambiguous=0;
        for(uint32_t k=0;k<parents;k++) {
            uint32_t j=(uint32_t)h->heads[k];const RsxPoseBlock* parent=h->blocks+j;
            if(i==j||child->after!=parent->after||child->count>parent->count/4)continue;
            int contains=1;double cv=1,pv=1;
            for(int a=0;a<3;a++) {
                if(child->lo[a]<parent->lo[a]||child->hi[a]>parent->hi[a]){contains=0;break;}
                cv*=child->hi[a]-child->lo[a];pv*=parent->hi[a]-parent->lo[a];
            }
            if(!contains||pv<=0||pv<cv*8)continue;
            if(container<0){container=(int32_t)j;continue;}
            const RsxPoseBlock* chosen=h->blocks+container;int inside=1,outside=1;
            for(int a=0;a<3;a++){
                if(parent->lo[a]<chosen->lo[a]||parent->hi[a]>chosen->hi[a])inside=0;
                if(chosen->lo[a]<parent->lo[a]||chosen->hi[a]>parent->hi[a])outside=0;
            }
            // Nested containers form one hierarchy. Overlapping unrelated
            // containers are ambiguous: never tie two characters together.
            if(inside)container=(int32_t)j;else if(!outside){ambiguous=1;break;}
        }
        if(container>=0&&!ambiguous)rsx_pose_join(h,(int32_t)i,container);
    }
}
/* Whole-draw coherence is insufficient at a UV/material boundary: two
 * adjacent, differently-shaped draws duplicate their common edge. Their
 * histories may have different phases or one may be newly visible.
 * Weld only EXACT current-frame positions in the SAME original camera.
 * No distance-based weld: nearby skin, eyelids and separate objects stay apart.
 * One recognized candidate can drive every copy of the point. Conflicting
 * candidates snap that point together, not each patch independently.
 */
/* Optional recipe follows exactly the same winning proposal as output.
 * UINT64_MAX means retain the CPU result. No recipe changes before graph
 * validation, so the bounded/atomic failure behavior is identical. */
/* Per-upload constraints, including exact world-space shadow copies with
 * different draw splits. Hashes are hints; verify exact world xyz. Only
 * verified world-space programs establish this constraint for shadow targets.
 * Temporary hash storage is rebuilt by weld/compute topology afterwards. */
static inline void rsx_pose_stationary_vertices(RsxPoseHistory* h,const RsxPoseFrame* f) {
    memset(h->stationary_vertices,0,f->nv);memset(h->heads,0xFF,(size_t)h->buckets*4);
    uint32_t nodes=0;
    for(uint32_t i=0;i<f->nd;i++)if(h->stationary_draws[i]) {
        const RsxPoseDraw* d=f->draws+i;
        for(uint32_t k=0;k<d->count;k++)h->stationary_vertices[d->first+k]=1;
    }
    for(uint32_t i=0;i<f->nd;i++)if(h->stationary_draws[i]&&(f->draws[i].flags&RSX_POSE_WORLD_LIGHT_GEOMETRY)) {
        const RsxPoseDraw* d=f->draws+i;
        for(uint32_t k=0;k<d->count;k++) {
            uint32_t v=d->first+k,bucket=rsx_pose_bucket(h,rsx_pose_hash_floats(1469598103934665603ull,f->vertices[v].xyz,3)),visits=0;int found=0;
            for(int32_t n=h->heads[bucket];n>=0;n=h->vertex_next[n]) {
                if(++visits>128)return;
                if(rsx_pose_floats_equal(f->vertices[v].xyz,f->vertices[h->vertex_owners[n]].xyz,3) ){found=1;break;}
            }
            if(!found){if(nodes==h->maxv)return;h->vertex_owners[nodes]=v;h->vertex_next[nodes]=h->heads[bucket];h->heads[bucket]=(int32_t)nodes++;}
        }
    }
    for(uint32_t i=0;i<f->nd;i++)if((f->draws[i].flags&3u)==3u) {
        const RsxPoseDraw* d=f->draws+i;
        for(uint32_t k=0;k<d->count;k++) {
            uint32_t v=d->first+k;if(h->stationary_vertices[v])continue;
            uint32_t bucket=rsx_pose_bucket(h,rsx_pose_hash_floats(1469598103934665603ull,f->vertices[v].xyz,3)),visits=0;
            for(int32_t n=h->heads[bucket];n>=0&&visits++<128;n=h->vertex_next[n])
                if(rsx_pose_floats_equal(f->vertices[v].xyz,f->vertices[h->vertex_owners[n]].xyz,3) ){h->stationary_vertices[v]=1;break;}
        }
    }
}
static inline int rsx_pose_weld_recipe(RsxPoseHistory* h,const RsxPoseFrame* f,float* output,uint32_t* states,uint64_t* recipes) {
    h->seams=h->seam_conflicts=h->seam_components=h->seam_veto_vertices=0;
    h->weld_reuse_hits=h->weld_reuse_misses=0;
    if(++h->weld_epoch==0){if(h->weld_memo)memset(h->weld_memo,0,(size_t)h->memo_slots*sizeof(*h->weld_memo));h->weld_epoch=1;}
    rsx_pose_stationary_vertices(h,f);
    memset(h->vertex_groups,0,(size_t)f->nv*sizeof(uint32_t));
    memset(h->heads,0xFF,(size_t)h->buckets*sizeof(int32_t));
    uint32_t nodes=0;
    /* Build the entire graph before changing proposals. A bounded-search or
     * capacity failure therefore leaves all output/state arrays untouched. */
    for(uint32_t i=0;i<f->nd;i++) {
        const RsxPoseDraw* d=f->draws+i;uint32_t camera=(uint32_t)h->camera_groups[i]+1;
        for(uint32_t k=0;k<d->count;k++) {
            uint32_t v=d->first+k;if(!states[v])continue;
            int repeated_upload=h->vertex_groups[v]!=0;
            if(!h->vertex_groups[v]){
                h->vertex_groups[v]=v+1;
                if(!h->repeat_work_disabled)h->weld_hashes[v]=rsx_pose_hash_floats(1469598103934665603ull,f->vertices[v].xyz,3);
            }
            uint64_t key=(h->repeat_work_disabled?rsx_pose_hash_floats(1469598103934665603ull,f->vertices[v].xyz,3):h->weld_hashes[v])^camera;
            uint32_t bucket=rsx_pose_bucket(h,key),visits=0;int32_t group=-1;
            RsxPoseWeldMemo* memo=NULL;
            /* Only repeated uploads use the memo. An unchanged head proves
             * the entire node chain is unchanged, so the old first match and
             * 128-visit outcome are unchanged. The previous union is still
             * connected: unions can merge components, never split them. */
            if(repeated_upload&&!h->repeat_reuse_disabled&&h->weld_memo) {
                memo=h->weld_memo+((v*2654435761u^camera*2246822519u)&(h->memo_slots-1));
                if(memo->epoch==h->weld_epoch&&memo->vertex==v&&memo->camera==camera&&memo->head==h->heads[bucket]) {
                    h->weld_reuse_hits++;continue;
                }
                h->weld_reuse_misses++;
            }
            for(int32_t g=h->heads[bucket];g>=0;g=h->vertex_next[g]) {
                if(++visits>128)return 0;
                if(h->vertex_cameras[g]==camera&&rsx_pose_floats_equal(f->vertices[v].xyz,f->vertices[h->vertex_owners[g]].xyz,3)){group=g;break;}
            }
            if(group<0) {
                if(nodes==h->maxv)return 0;
                uint32_t n=nodes++;h->vertex_owners[n]=v;h->vertex_cameras[n]=camera;
                h->vertex_next[n]=h->heads[bucket];h->heads[bucket]=(int32_t)n;
            }else {
                uint32_t a=rsx_pose_point_component(h,v),b=rsx_pose_point_component(h,h->vertex_owners[group]);
                if(a<b)h->vertex_groups[b]=a+1;else if(b<a)h->vertex_groups[a]=b+1;
            }
            if(memo){memo->vertex=v;memo->camera=camera;memo->head=h->heads[bucket];memo->epoch=h->weld_epoch;}
        }
    }
    /* Hash-node owners are no longer needed; map each upload to its root. */
    for(uint32_t v=0;v<f->nv;v++)h->vertex_owners[v]=h->vertex_groups[v]?rsx_pose_point_component(h,v)+1:0;
    /* Confirmed stationary geometry is a real constraint, not an absent
     * moving proposal. An erroneous nearby match must not drag its shared
     * points away. Reuse dead point-DSU scratch only AFTER owners are final. */
    memset(h->vertex_groups,0,(size_t)f->nv*sizeof(uint32_t));
    for(uint32_t v=0;v<f->nv;v++)if(h->stationary_vertices[v]){
        uint32_t owner=h->vertex_owners[v];if(owner)h->vertex_groups[owner-1]=1;
    }
    /* The chains are no longer needed: reuse them as representative states.
     * 0 no moving candidate, 1 moving seed, 2 hard alias veto, 3 disagreement. */
    memset(h->vertex_next,0,(size_t)f->nv*sizeof(int32_t));
    for(uint32_t v=0;v<f->nv;v++) {
        if(!h->vertex_owners[v])continue;
        uint32_t root=h->vertex_owners[v]-1;int32_t* state=h->vertex_next+root;
        if(states[v]==2){*state=2;continue;}
        if(*state==2||*state==3)continue;
        int unchanged=rsx_pose_floats_equal(output+(size_t)v*3,f->vertices[v].xyz,3);
        if(h->vertex_groups[root]&&!unchanged){*state=3;h->seam_conflicts++;continue;}
        if(unchanged)continue;
        if(!*state){memcpy(output+(size_t)root*3,output+(size_t)v*3,3*sizeof(float));if(recipes)recipes[root]=recipes[v];*state=1;}
        else if(!rsx_pose_floats_equal(output+(size_t)root*3,output+(size_t)v*3,3)){*state=3;h->seam_conflicts++;}
    }
    /* A point-only snap mixes current and intermediate vertices within one
     * triangle. Replay of JP F9 frame_033 stretched an edge 24.4x this way.
     * Spread conflict/alias fallbacks over the connected surface BEFORE any
     * final writes. Clean, disconnected surfaces keep their interpolation. */
    int blocked=0;
    for(uint32_t v=0;v<f->nv&&!blocked;v++)if(h->vertex_owners[v]) {
        int32_t state=h->vertex_next[h->vertex_owners[v]-1];blocked=state==2||state==3;
    }
    if(blocked) {
        rsx_pose_mesh_components(h,f);memset(h->seeds,0,(size_t)f->nd*sizeof(int32_t));
        for(uint32_t v=0;v<f->nv;v++)if(h->vertex_owners[v]) {
            uint32_t root=h->vertex_owners[v]-1;int32_t state=h->vertex_next[root];
            if(state==2||state==3)h->seeds[rsx_pose_component(h,(int32_t)h->vertex_cameras[root]-1)]=1;
        }
        for(uint32_t i=0;i<f->nd;i++) {
            int32_t group=rsx_pose_component(h,(int32_t)i);if(!h->seeds[group])continue;
            if(h->seeds[group]==1){h->seam_components++;h->seeds[group]=2;}
            const RsxPoseDraw* d=f->draws+i;
            for(uint32_t k=0;k<d->count;k++) {
                uint32_t owner=h->vertex_owners[d->first+k];if(owner)h->vertex_next[owner-1]=2;
            }
        }
    }
    for(uint32_t v=0;v<f->nv;v++) {
        if(!h->vertex_owners[v])continue;
        uint32_t root=h->vertex_owners[v]-1;int32_t state=h->vertex_next[root];
        if(state==2&&!rsx_pose_floats_equal(output+(size_t)v*3,f->vertices[v].xyz,3))h->seam_veto_vertices++;
        const float* value=state==1?output+(size_t)root*3:f->vertices[v].xyz;
        if(!rsx_pose_floats_equal(output+(size_t)v*3,value,3))h->seams++;
        memcpy(output+(size_t)v*3,value,3*sizeof(float));
        states[v]=state==2?2u:1u;
        if(recipes)recipes[v]=state==1?recipes[root]:UINT64_MAX;
    }
    return 1;
}
static inline int rsx_pose_weld(RsxPoseHistory* h,const RsxPoseFrame* f,float* output,uint32_t* states) {
    return rsx_pose_weld_recipe(h,f,output,states,NULL);
}
/* Conflicting aliases, including an unmatched pass using a matched range,
 * retain the recorded data. Merge on CPU before writing any upload bytes. */
static inline void rsx_pose_merge(float* out,uint32_t* state,const float* value,uint32_t n) {
    if(*state==2)return;
    if(!*state){memcpy(out,value,(size_t)n*sizeof(float));*state=1;}
    else if(memcmp(out,value,(size_t)n*sizeof(float)))*state=2;
}
#endif
