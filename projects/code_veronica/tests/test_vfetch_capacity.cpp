#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;
#define MAX_DRAWS 24576u
#define MAX_VERTICES 786432u
#define VP_VERT_STRIDE 256u
#define VF_DESC_SIZE 512u
#define VF_DESC_OFF (MAX_VERTICES*8u)
struct VPSlot{float v[4];};struct rsx_state{};
struct {void* vp_vb_mapped;u32 vp_parity;} s_d3d;
static u8*s_vf_mapped;static u32 s_vf_ndesc,s_vf_vmin,s_vf_vmax,s_vf_rmin,s_vf_rmax;static int s_vf_cur=-1;static const rsx_state*s_vf_state;
static u8*vf_parity_base(){return s_vf_mapped;}
static int vf_pose_enabled(){return 0;}static int s_pose_on=0;
static u32 vf_pose_stride(){return 0;}
static void rsx_pose_gpu_reset(void*){}static void pose_note_raw_vertex(const rsx_state*,VPSlot*,u32){}
#define VF_POSE_OFF 0
static u32 current_marker;
static void vf_flush(){if(s_vf_cur<0)return;auto*d=(u32*)(s_vf_mapped+VF_DESC_OFF+(u64)s_vf_cur*512u);d[0]=current_marker;s_vf_cur=-1;}

#ifndef VF_DESC_MAX
#define VF_DESC_MAX 49152u
#endif
static void vf_note(const rsx_state* state, VPSlot* dst, u32 vi)
{
    u8* vb = (u8*)s_d3d.vp_vb_mapped + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE;
    if ((u8*)dst < vb || (u8*)dst >= vb + (u64)MAX_VERTICES * VP_VERT_STRIDE) {
        memcpy(dst, &vi, 4);   /* a local copy (quads): vp_put places it */
        return;
    }
    if (s_vf_cur >= 0 && s_vf_state != state) vf_flush();
    if (s_vf_cur < 0) {
        if (s_vf_ndesc >= VF_DESC_MAX) {
            static int w = 0; if (w++ < 4) fprintf(stderr, "[VFETCH] descriptor table full%c", 10);
            s_vf_ndesc = VF_DESC_MAX - 1;
        }
        s_vf_cur = (int)s_vf_ndesc++; s_vf_state = state;
        s_vf_vmin = s_vf_rmin = ~0u; s_vf_vmax = s_vf_rmax = 0;
    }
    const u32 r = (u32)(((u8*)dst - vb) / VP_VERT_STRIDE);
    if(vf_pose_enabled()) {
        rsx_pose_gpu_reset(vf_parity_base()+VF_POSE_OFF+(size_t)r*vf_pose_stride());
        if(s_pose_on)pose_note_raw_vertex(state,dst,vi);
    }
    u32* rec = (u32*)vf_parity_base() + (u64)r * 2;
    rec[0] = vi; rec[1] = (u32)s_vf_cur;
    if (vi < s_vf_vmin) s_vf_vmin = vi;
    if (vi > s_vf_vmax) s_vf_vmax = vi;
    if (r < s_vf_rmin) s_vf_rmin = r;
    if (r > s_vf_rmax) s_vf_rmax = r;
}
int main(){
 s_d3d.vp_vb_mapped=VirtualAlloc(nullptr,(size_t)MAX_VERTICES*256u,MEM_RESERVE,PAGE_NOACCESS);assert(s_d3d.vp_vb_mapped);
 s_vf_mapped=(u8*)calloc(1,(size_t)VF_DESC_OFF+VF_DESC_MAX*512u);assert(s_vf_mapped);rsx_state st;
 const u32 n=18332;
 for(u32 i=0;i<n;i++){current_marker=i+1;vf_note(&st,(VPSlot*)((u8*)s_d3d.vp_vb_mapped+(size_t)i*256u),i);vf_flush();}
 u32 mismatches=0;
 for(u32 i=0;i<n;i++){auto*rec=(u32*)(s_vf_mapped+(size_t)i*8);auto*d=(u32*)(s_vf_mapped+VF_DESC_OFF+(size_t)rec[1]*512u);mismatches+=d[0]!=i+1;}
 printf("capacity=%u requests=%u corrupted_snapshots=%u\n",(unsigned)VF_DESC_MAX,n,mismatches);
#ifdef EXPECT_OLD
 assert(mismatches>0);
#else
 assert(mismatches==0);
 // Reach the supported descriptor boundary, ensuring identities stay stable.
 for(u32 i=n;i<VF_DESC_MAX;i++){current_marker=i+1;vf_note(&st,(VPSlot*)((u8*)s_d3d.vp_vb_mapped+(size_t)i*256u),i);vf_flush();}
 for(u32 i=0;i<VF_DESC_MAX;i++){auto*rec=(u32*)(s_vf_mapped+(size_t)i*8);auto*d=(u32*)(s_vf_mapped+VF_DESC_OFF+(size_t)rec[1]*512u);assert(d[0]==i+1);}
 puts("PASS unique descriptors through capacity; earlier data preserved");
#endif
 free(s_vf_mapped);VirtualFree(s_d3d.vp_vb_mapped,0,MEM_RELEASE);
}
