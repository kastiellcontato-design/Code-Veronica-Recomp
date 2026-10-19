/* Exact-state batching for the texture-free shadow-map generator only.
 * Include after D3D12DrawRecord; no reordering, arithmetic changes or state
 * approximations. Padding differences conservatively reject a pair. */
#ifndef RSX_SHADOW_BATCH_H
#define RSX_SHADOW_BATCH_H
static int rsx_shadow_batch_pair(const D3D12DrawRecord* a,
    const D3D12DrawRecord* b,const void* au,const void* bu,
    const void* ak,const void* bk) {
    if(!a->is_vp||!b->is_vp||a->is_clear||b->is_clear||a->is_copy||b->is_copy||
       a->topology!=RSX_TOPOLOGY_TRIANGLES||b->topology!=RSX_TOPOLOGY_TRIANGLES||
       !a->vertex_count||!b->vertex_count||a->vertex_count%3||b->vertex_count%3||
       !a->fp_exp32||!b->fp_exp32||a->fp_bytes!=240||b->fp_bytes!=240||
       a->vcb_slot!=b->vcb_slot||!au||!bu||!ak||!bk||
       (uint64_t)a->vb_byte_offset+(uint64_t)a->vertex_count*256u!=b->vb_byte_offset||
       (uint64_t)a->vertex_count+b->vertex_count>UINT32_MAX)return 0;
    D3D12DrawRecord x,y; memcpy(&x,a,sizeof x); memcpy(&y,b,sizeof y);
    /* Only addresses/indices that do not affect shader-visible data vary.
     * Constants and program bytes are compared separately below. BEGIN/END
     * was already expanded into independent ordered triangle lists. */
    x.vb_byte_offset=y.vb_byte_offset=x.vertex_count=y.vertex_count=0;
    x.cb_slot=y.cb_slot=x.fp_snap=y.fp_snap=x.fp_addr=y.fp_addr=0;
    x.begin_epoch=y.begin_epoch=0;
    if(memcmp(&x,&y,sizeof x))return 0;
    /* Compare the exact CPU snapshot that vp_record_cb copies into b1.
     * Never read the upload heap back on the CPU: those mappings are for
     * writes and cost milliseconds across thousands of pairs. alpha_ctl is
     * already compared in the complete record above; NO_ALPHATEST is global.
     * This texture-free generator has no WPOS, so alpha.w/texture metadata
     * are unused. Caller disables batching for FP_ZERO_K diagnostics. */
    return !memcmp(au,bu,240)&&!memcmp(ak,bk,5u*16u);
}
#endif
