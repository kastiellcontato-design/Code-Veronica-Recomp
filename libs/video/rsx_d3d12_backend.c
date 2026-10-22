/*
 * ps3recomp - D3D12 RSX Backend
 *
 * Translates RSX GPU state to D3D12 rendering commands.
 *
 * Phase 1 implementation:
 *   - Win32 window + D3D12 device + swap chain
 *   - Clear render target to RSX clear color
 *   - Present with vsync
 *   - Basic vertex-colored triangle rendering
 *
 * This file is C with COM calls (D3D12 is a COM API).
 * We use the C interface (__uuidof not available in C, so we
 * define GUIDs manually).
 */

#ifdef _WIN32

#include <time.h>
#include "rsx_d3d12_backend.h"
#include "rsx_primitives.h"
#include "rsx_vertex_fetch.h"
#include "rsx_pose_interpolation.h"
#include "rsx_pose_gpu.h"
#include "rsx_render_optimizations.h"
#include "rsx_prepared_repeat.h"
#include "rsx_packed16_filter.h"
#include "rsx_ui_batch.h"
#include "rsx_ui_cadence.h"
#include "rsx_texture_layout.h"
#include "rsx_tex_hook.h"
#include "rsx_perf_overlay.h"
#include "rsx_texture_pack.h"
#include "../codec/stb_image_write.h"
#include "rsx_fp_decompiler.h"
#include "rsx_dxc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* D3D12 headers */
#include <d3d12.h>
#include <d3d12sdklayers.h>   /* ID3D12Debug / ID3D12InfoQueue */
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

/* We need these GUIDs â€” define them here to avoid uuid.lib dependency */
#include <initguid.h>
#include "rsx_pose_compute_d3d12.h"
#include "rsx_fsr31.h"
static int s_fsr_ready;
static u32 s_fsr_depth_at=0xFFFFFFFFu,s_fsr_depth_w,s_fsr_depth_h;

#include "rsx_getenv_cache.h"   /* getenv() per call site, cached: per-draw paths */
static int s_vsync = 0, s_tearing = 0;   /* present mode, see the swap chain setup */
#include "../../runtime/platform/host_thread_name.h"

/* Link libraries */
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

/* ---------------------------------------------------------------------------
 * Constants
 * -----------------------------------------------------------------------*/

#define FRAME_COUNT         2   /* double buffering */
#define MAX_VERTICES    786432  /* per-frame vertex buffer. Code Veronica's
                                 * rm_1130 water draws two 138612-vertex
                                 * meshes; with the lighter lit the frame hit
                                 * the old 393216, and the draws past it --
                                 * post-processing and the copy to the display
                                 * -- were dropped, so the screen flashed
                                 * black. The VP copy is 256 B x2 per vertex. */
#define MAX_DRAWS        24576  /* per-batch draw records. JP rm_2050 repeatedly
                                 * reaches 12288 during Steve's gunfire; dropping
                                 * the tail can omit post-processing/display
                                 * copies while Present continues at 30 FPS.
                                 * Keep headroom for this cutscene. All draw,
                                 * CB and descriptor capacities derive from
                                 * this limit; VP constants use ~396 MiB x2
                                 * parity combined; FP constants use the same. */
/* Per-draw VP constant-buffer slot: vp_c[512] + posscale + posoffset
 * (514 vec4 = 8224 B) rounded up to D3D12's 256-byte CBV alignment.
 * Constants are snapshotted at RECORD time -- wave's passes each set their
 * own texScale/offset uniforms, so one per-frame snapshot ran every pass
 * with the LAST pass's constants. */
#define VP_CB_STRIDE      8448
/* b1 per-draw FP slot: rsx_texscale[4] + rsx_alphatest + fp_k[FP_MAX_K]
 * = 133 float4 = 2128 B, rounded to D3D12's 256-byte CBV alignment.
 * Code Veronica's exposure-curve programs carry 87 inline constants; with
 * room for only 64 they fell back to literals baked from the FIRST frame
 * that used them, and the colour-grading curve never followed the scene. */
/* 512: Code Veronica's outdoor lighting programs carry 129 and 166. Past
 * the limit a program's constants become literals and its pipeline is keyed
 * on their values -- which the game rewrites every frame, so those programs
 * recompiled without end and the draws using them were skipped meanwhile:
 * the character drawn flat or missing, the whole scene black for seconds.
 * 512 leaves headroom; only the constants a program uses are written per
 * draw, so the larger slot costs upload-heap memory (system RAM), not time. */
#define FP_MAX_K          512
/* Programs are sized through END and copied at that exact length. The
 * decompiler owns the shared safety ceiling; no 16 KiB per-program cutoff. */
/* Fragment texture units recorded and bound per draw (t0..t7). RSX has 16;
 * Code Veronica's lighter flame samples unit 4. b1 starts with one float4
 * per unit (rsx_texaddr), then rsx_alpha_k, then fp_k[] -- 137 float4. */
#define FP_TEX_UNITS      8
#define FP_ALPHA_BASE     (FP_TEX_UNITS * 4)
#define FP_K_BASE         (FP_ALPHA_BASE + 4)
#define VP_FPCB_STRIDE    8448   /* (9 + FP_MAX_K) float4 = 8336 B, 256-aligned */
#define VERTEX_STRIDE       36  /* bytes per host vertex: pos3 + col4 + uv2 */

typedef struct {
    u32 vb_byte_offset; /* offset into vb_mapped where this draw's verts live */
    u32 vertex_count;
    u32 topology;       /* rsx_topology (NOT a D3D value; see topo_to_d3d) */
    int textured;       /* 1 = sample the bound font/atlas texture (dbgfont) */
    int is_vp;          /* 1 = real VP path: vb_byte_offset indexes vp_vb (float4) */
    /* VP path per-draw shader/texture state, captured at draw_arrays time. */
    u32 fp_addr;        /* SET_SHADER_PROGRAM value (guest FP ucode location)   */
    int fp_exp32;       /* SET_SHADER_CONTROL 32-bit-exports bit at draw time   */
    u32 alpha_ctl;      /* alpha test: enable<<16 | (func&0xFF)<<8 | ref */
    u32 begin_epoch;    /* SET_BEGIN_END generation, for batch concatenation */
    u32 cull;           /* packed face culling: bit0 enable, bit1 cull FRONT
                         * (else BACK), bit2 front face is CCW. RSX culls back
                         * faces on most solid geometry; rendering everything
                         * double-sided lets a shell's interior faces show
                         * through and shade unlit (black). */
    u32 cmask;          /* D3D write mask from SET_COLOR_MASK at draw time
                         * (wave's sim passes write single lanes of the height
                         * maps; ignoring the mask stomped persistent state) */
    /* Per-unit textures (t0-t3): decompiled FPs sample up to 4 units
     * (demosaic's interpolation passes read 3). Captured at draw time. */
    struct {
        u32 off;        /* resolved vm offset (guest upload source), 0 = none */
        u32 raw;        /* raw RSX offset (offscreen-RT matching) */
        u32 w, h, fmt;  /* dims + RSX base format */
        u32 ctrl1;      /* NV4097 TEXTURE_CONTROL1: component remap crossbar */
        u32 addr;       /* NV4097 TEXTURE_ADDRESS: wrap modes, S [3:0] T [11:8] */
        u32 mips;       /* SET_TEXTURE_FORMAT bits 16..31: mipmap level count.
                         * Cube faces sit one whole mip pyramid apart, so this
                         * is what sets the face stride. */
        int cube;       /* SET_TEXTURE_FORMAT bit 2: a cube texture. Sampled
                         * with a 3-component direction, not a 2D uv -- treating
                         * one as 2D is what makes an environment-mapped chrome
                         * surface come out with black patches and banding. */
        int set;
    } tex[FP_TEX_UNITS];
    int tex_rt[FP_TEX_UNITS];      /* pre-pass: OffRT index sampled by unit, -1 = none */
    int tex_slot;       /* legacy single-slot path (atlas); -1 = none */
    int vs_idx;         /* VPVSEntry slot for this draw's vertex program (-1 = primary) */
    int blend;          /* guest blend enable at draw time */
    u32 blend_key;      /* packed guest blend state (factors+equation), PSO key */
    /* Render-to-texture: which colour surface this op targets. 0 = a display
     * buffer (the swap-chain backbuffer); else the resolved vm offset of an
     * offscreen surface (demosaic chains its effect passes through local-
     * memory buffers and composites from them). */
    u32 rt_off;
    /* Which per-draw constant slot this record's constants were written to.
     * vp_record_cb writes them keyed by the RECORD index, so any pass that
     * reorders or compacts s_d3d.draws (DRAW_KEEP_TEX, DRAW_LAST_TEX,
     * DRAW_LIMIT) would otherwise make every surviving draw read some other
     * object's MVP -- silently transforming it somewhere else entirely. */
    u32 cb_slot;
    u32 vcb_slot;       /* vertex-constant (b0) slot: cb_slot, or an earlier draw's
                         * when no constant changed in between (vp_record_cb) */
    u32 rt_mrt[3];      /* colour targets B,C,D (MRT1/2/3), 0 = none. Deferred
                         * shading G-buffers write 3-4 targets in one pass. */
    u32 rt_w, rt_h;     /* surface clip dims at record time (offscreen RT size) */
    u32 rt_fmt;         /* RSX surface colour format (SET_SURFACE_FORMAT [4:0]) */
    /* Guest viewport rect at draw time (target pixels). Sub-viewport layouts
     * (wave's debug tiles) position quads with this, not with constants --
     * forcing full-target viewports drew every tile window-sized. */
    u32 vp_x, vp_y, vp_w, vp_h;
    /* Ordered clear op (offscreen surfaces only; display clears stay the
     * frame-start backbuffer clear). is_clear records also set is_vp so the
     * legacy replay pass skips them. */
    int   is_clear;
    int is_copy;
    u32 copy_src, copy_sx, copy_sy, copy_dx, copy_dy, copy_w, copy_h;
    float cc[4];
    /* CLEAR_SURFACE mask (bit0 Z, bit1 stencil, bits4-7 RGBA) and the
     * depth it clears to. Code Veronica clears depth (0x3) and colour
     * (0xF0) separately. */
    u32 clr_flags;
    float clr_depth;
    u8 clr_stencil;
    /* Depth state at record time, for rsxcap manifests: the zeta surface
     * offset and whether/how the draw tests against it. */
    u32 zeta_off;
    int dtest;
    u32 dfunc;
    /* Vertex textures (TXL in the transform program) bound at draw time.
     * Their contents are snapshotted into s_vtex_arena when the draw is
     * recorded: Code Veronica keeps each room piece's world matrix in one,
     * and rewrites it between draws. */
    u32 vtex_mask;
    struct { u32 fmt, w, h, row_bytes, data_off, hash; } vtx[4];
    /* Raw surface registers at record time, for rsxcap: which target the
     * guest selected, offsets A/B and the clip, before current_rt_off's
     * display / size rules turned them into rt_off. */
    u32 s_tgt, s_offa, s_offb, s_clipw, s_cliph, s_pitch;
    /* Copy of the fragment program's ucode taken when the draw was recorded:
     * offset + 1 into s_fp_arena (0 = none, read guest memory). */
    u32 fp_snap;
    u32 fp_bytes;              /* exact complete bytecode length, 0 = invalid */
} D3D12DrawRecord;
#include "rsx_shadow_batch.h"
static const u8* fp_snap_ptr(const D3D12DrawRecord* dr);
static u32 fp_snap_code(const D3D12DrawRecord* dr);
static u32 fp_snap_code_in(const D3D12DrawRecord* dr,const u8* arena);
static u32 dr_cube_mask(const D3D12DrawRecord* dr);
static u32 fp_guest_program_size(const u8* src, u32* constants);

/* Offscreen render target (render-to-texture). Persistent across frames --
 * pass N's output is sampled by pass N+1 and possibly by later frames. */
typedef struct {
    ID3D12Resource*       res;
    ID3D12Resource*       up;          /* init-upload staging (kept alive) */
    u32                   off, w, h;   /* raw RSX offset + dims */
    float                 sc;          /* RES_SCALE applied to this target (1 = native) */
    u32                   pw, ph;      /* resource dims: w,h times sc */
    u32                   dxgi;        /* DXGI_FORMAT of the resource */
    D3D12_RESOURCE_STATES st;          /* tracked resource state */
    int                   used;        /* referenced this frame */
    u32                   gpitch;      /* guest surface pitch (for write-back) */
    ID3D12Resource*       wb;          /* readback buffer for write-back */
    int                   wb_pending;  /* wb holds this frame's copy */
} OffRT;
#define MAX_OFF_RTS  31  /* demosaic double-buffers its 6-surface pass chain; Code Veronica
                          * uses 17 per gameplay frame. RT SRVs must end below DRAW_SRV_BASE. */
#define RT_SRV_BASE  97  /* VP texture SRVs occupy slots 1..96; RTs use 97..127. */
/* Per-draw SRV windows: each VP draw gets 4 consecutive descriptors (t0-t3)
 * so multi-unit fragment programs see all their textures (demosaic's
 * interpolation passes sample 3 units). */
#define DRAW_SRV_BASE 128
/* One 4-slot window past the per-draw ones: the depth buffer as a texture,
 * read by the zeta-to-colour conversion pass (zconv_run). */
#define ZCONV_SRV     (DRAW_SRV_BASE + MAX_DRAWS * FP_TEX_UNITS)
/* Per-draw vertex-texture windows (t16-t19), indexed like the t0-t3 ones. */
#define VTEX_SRV_BASE (ZCONV_SRV + FP_TEX_UNITS)
#define AA_SRV_BASE   (VTEX_SRV_BASE + MAX_DRAWS * 4)   /* anti-aliasing pass: 5 views */
#define DISPLAY_SRV_BASE (AA_SRV_BASE + 8)   /* display pass: 5 views per frame slot */
#define SPRITE_SRV    (DISPLAY_SRV_BASE + 5 * FRAME_COUNT)   /* the SRT's image atlas */
#define SRV_HEAP_SIZE (SPRITE_SRV + 4)

/* Per-frame VP texture slot: a guest texture uploaded for this frame's VP
 * draws (re-uploaded every frame -- gcm/cube's plasma animates in guest
 * memory). SRV lives at heap index 1+slot. */
typedef struct {
    ID3D12Resource* res;
    ID3D12Resource* up;
    u32 off, w, h, fmt; /* current contents (resource reused when dims match) */
    u32 csum;           /* sparse checksum of the source bytes last uploaded */
    int cube;           /* resource is a 6-face cube, sampled as TextureCube */
    u32 key;            /* the ORIGINAL bound offset -- the cache lookup key.
                         * off is the RESOLVED source after TEX_OFF_BIAS/TEX_REMAP
                         * shift it, so keying on off never matches the raw offset
                         * a later draw arrives with, and every draw re-uploads. */
    int used;           /* referenced this frame */
    u64 stamp;          /* render frame it was last referenced in (eviction age) */
    u64 thash;          /* texture mods: the contents' hash (rsx_texture_pack.h) */
    int thash_ok;       /* thash is set */
    int thash_can;      /* a moddable format, not animating: thash can be taken */
    u32 thash_bytes;    /* the guest bytes it is taken over */
    int dump_seen;      /* texture dumping has looked at these contents */
    uint32_t dump_generation; /* destination used when these contents were dumped */
    u64 upload_frame;   /* render frame its contents were last uploaded */
    ID3D12Resource* ui_res; /* optional title-owned HD artwork, same logical UVs */
    u64 ui_id;
    float ui_scale;
    int nlev;           /* mip levels in the resource (1 unless RSX_TEX_MIPS) */
} VPTexSlot;
static u64 s_tex_frame = 1;   /* render frames, for VPTexSlot.stamp */
/* Distinct textures uploadable per frame. Four was enough for a UI/atlas-style
 * frame but not for a scene: this title binds ~20 distinct textures per frame,
 * so every draw past the fourth got slot -1 from vp_upload_tex_slot ("out of
 * slots") and drew UNTEXTURED. That reads as a correctly-lit but solid black
 * object, which is indistinguishable from a missing texture upload. */
#define VP_TEX_SLOTS 384   /* 96 churned in the lighter scene: re-uploads every frame */

/* Decompiled-VS cache: one entry per distinct vertex-program ucode seen at
 * draw time (hashed). Apps switch VPs between draws (gcm/cube: its MVP cube VP
 * vs dbgfont-gcm's text VP); compiling only the first left later draws
 * transformed by the wrong program (text offscreen). */
typedef struct {
    u32 hash;               /* FNV-1a of the ucode */
    ID3DBlob* vs;
    int uses_c03;
    int pose_inputs_known;  /* cached once; never rescan the GPU prelude per draw */
    char* hlsl;             /* decompiled source, kept for rsxcap */
    u64 last_used;          /* frame of the last lookup: the least recent is replaced */
    volatile LONG pending;  /* compiling in the background: vs is still NULL */
} VPVSEntry;
/* 4096: Code Veronica's outdoor areas went past 256 distinct programs (log:
 * "VS cache full"). A full cache replaces its least recently used slot in
 * place; FP pipelines are keyed on the VS content (vs_hash), not the slot, so
 * a replacement no longer re-keys every cached pipeline. */
#define VP_VS_CACHE 4096 /* Code Veronica's gameplay uses 16+ VPs, and at 16 every eviction
                         * re-keyed every cached FP pipeline: ~90% of the frame went to
                         * recompiling shaders. wave uses 5+ distinct VPs; at 4 the cache
                         * thrashed every frame and eviction shifted slots
                         * under recorded vs_idx values -- the display mesh
                         * drew with the WRONG vertex program (sub-rect +
                         * edge slivers instead of fullscreen) */

/* Compiled guest-FP pipeline cache: decompiled VS (by cache slot) + decompiled
 * PS (fragment ucode at fp_addr). */
typedef struct {
    u32 fp_addr;
    int vs_idx;             /* VPVSEntry slot this PSO's VS came from */
    u32 vs_hash;            /* validates the slot hasn't been evicted */
    u32 gen;
    u32 blend;              /* packed guest blend key (enable+factors+eq, PSO key) */
    int nrt;                /* bound colour target count (PSO key)       */
    u32 rtfmt;              /* DXGI format of the colour targets (PSO key) */
    int exp32;              /* 32-bit-exports control bit (PSO key)       */
    u32 ucode_hash;         /* FNV-1a of the FP ucode: apps re-patch inline
                             * constants per frame (wave's stamp position/
                             * amplitude) -- address-only keying served the
                             * stale compile forever. */
    u32 cmask;              /* colour write mask (PSO key) */
    u32 cull;               /* packed face culling (PSO key) */
    u32 cube_mask;          /* which units are cube textures (PSO key): the HLSL
                             * declares those samplers as TextureCube, so a cube
                             * and a 2D variant of the same program are different
                             * pipelines and must not share a cache entry. */
    ID3D12PipelineState* pso;
    u8 pending;             /* a shader worker is still building it */
    u8 skip;                /* untranslatable program: drop its draws */
} VPFPEntry;
/* Guest-FP PSO cache. 16 entries thrashed badly: this title needs far more
 * distinct (fp, vs, blend, rt, cmask, ucode-hash) combinations than that in a
 * single frame, so the FIFO evicted entries that were needed again immediately
 * and ~17 shaders were recompiled EVERY frame -- about half the frame's CPU
 * time. Entries are small (a PSO pointer plus key fields). */
#define VP_FP_CACHE 16384

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

typedef struct {
    /* Window */
    HWND hwnd;
    u32  width;
    u32  height;
    int  window_closed;

    /* D3D12 core */
    ID3D12Device*              device;
    ID3D12CommandQueue*        cmd_queue;
    IDXGISwapChain3*           swap_chain;
    ID3D12DescriptorHeap*      rtv_heap;
    u32                        rtv_descriptor_size;
    ID3D12Resource*            render_targets[FRAME_COUNT];
    ID3D12CommandAllocator*    cmd_allocators[FRAME_COUNT];
    ID3D12GraphicsCommandList* cmd_list;

    /* Synchronization */
    ID3D12Fence* fence;
    HANDLE       fence_event;
    u64          fence_values[FRAME_COUNT];
    u32          frame_index;

    /* Pipeline */
    ID3D12RootSignature*  root_signature;
    ID3D12PipelineState*  pipeline_state;         /* triangle class â€” default */
    ID3D12PipelineState*  pipeline_state_lines;   /* line class */
    ID3D12PipelineState*  pipeline_state_points;  /* point class */
    ID3D12PipelineState*  pipeline_state_ui;      /* alpha UI batch, no depth */

    /* Depth/stencil */
    ID3D12DescriptorHeap* dsv_heap;
    ID3D12Resource*       depth_buffer;

    /* Dynamic vertex buffer (upload heap) */
    ID3D12Resource*       vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vb_view;
    void*                 vb_mapped;      /* persistently mapped */
    u32                   vb_offset;      /* current write position */

    int                   pipeline_ready; /* 1 if root sig + PSO created */

    /* Debug: copy the presented backbuffer to disk as BMP for the first N
     * frames (enabled by env CELLMARK_DUMP). Lets us verify what actually
     * rendered without racing the window/process lifetime. */
    ID3D12Resource*       readback_buf;
    u32                   readback_pitch;
    int                   dump_frames_left;
    int                   dump_skip_left;   /* CELLMARK_DUMP_SKIP: presents to ignore first */

    /* Textured pipeline (dbgfont / 2D atlas quads). The font atlas is an 8-bit
     * coverage texture uploaded as R8_UNORM and sampled in the pixel shader. */
    ID3D12PipelineState*  pipeline_state_tex;   /* triangle + texture + blend */
    ID3D12DescriptorHeap* srv_heap;             /* shader-visible, 1 SRV slot  */
    ID3D12Resource*       tex_resource;         /* current atlas texture       */
    ID3D12Resource*       tex_upload;           /* staging buffer for uploads  */
    u32                   tex_w, tex_h;         /* dims of tex_resource         */
    int                   tex_ready;            /* 1 once an atlas is uploaded  */
    u32                   tex_src_offset;       /* guest RSX offset of atlas    */
    int                   tex_bound;            /* a texture is bound for draws */
    int                   tex_dirty;            /* re-upload needed this frame  */

    /* Real RSX vertex-program path: the captured VP is decompiled to HLSL and
     * used as the vertex shader, fed the raw float4 attrib0 + the vp_c[]
     * constant bank. This produces exact position + texcoord (vs. the frac
     * approximation). Used for the 2D/dbgfont quad draws. */
    ID3D12RootSignature*  vp_root_sig;          /* CBV(b0) + SRV table + sampler */
    ID3D12PipelineState*  pipeline_state_vp;    /* decompiled VS + atlas PS      */
    ID3D12PipelineState*  pipeline_state_vp_color; /* decompiled VS + colour PS (untextured 3D) */
    ID3D12Resource*       vp_vb;                /* raw float4 attrib0, per-frame */
    void*                 vp_vb_mapped;
    u32                   vp_vb_offset;
    ID3D12Resource*       vp_cb;                /* per-draw VP constant slots    */
    void*                 vp_cb_mapped;
    ID3D12Resource*       vp_fpcb;              /* per-draw FP texscale (b1)     */
    void*                 vp_fpcb_mapped;
    int                   vp_ready;             /* VS+PSO compiled ok            */
    u32                   vp_compiled_bytes;    /* ucode size when last compiled */
    ID3DBlob*             vp_vs_blob;           /* kept for guest-FP PSO builds  */
    u32                   vp_gen;               /* bumped per VP recompile       */
    int                   vp_uses_c03;          /* VS references vp_c[0..3]      */
    VPTexSlot             vp_tex[VP_TEX_SLOTS]; /* per-frame VP texture slots    */
    VPVSEntry             vp_vs[VP_VS_CACHE];   /* per-draw decompiled VS cache  */
    int                   vp_vs_n;
    VPFPEntry             vp_fp[VP_FP_CACHE];   /* guest-FP PSO cache            */
    int                   vp_fp_n;
    u32                   srv_inc;              /* CBV_SRV_UAV descriptor size   */
    /* VP path: latest texture bound per unit (t0-t3). */
    struct { u32 off, raw, w, h, fmt, ctrl1, addr, mips; int cube; int set; } cur_texs[FP_TEX_UNITS];

    /* Render-to-texture: offscreen RT pool + their RTV heap. */
    OffRT                 off_rt[MAX_OFF_RTS];
    /* DRAW_ARRAYS batch merging (see d3d12_draw_arrays): the vertex index one
     * past the last batch, and whether the current BEGIN/END may still be
     * extended. */
    u32                   merge_first_end;
    int                   merge_prev_draw;      /* 1 = triangle list, 6 = strip, 7 = fan */
    u32                   merge_strip_first;    /* first vertex of the strip/fan being extended */
    u32                   merge_pending_epoch;  /* BEGIN/END of a strip head too short to draw */
    ID3D12DescriptorHeap* rt_rtv_heap;          /* MAX_OFF_RTS RTVs (CPU only)   */

    /* Frame-parity double buffering for the per-draw upload streams (vp_vb
     * vertices, vp_cb constants, vp_fpcb texscales): records for frame N+1
     * are written while frame N's GPU work may still be reading -- writes go
     * to the other half. Toggled at the end of render_frame. */
    int                   vp_parity;

    /* Per-frame draw recording */
    int                   frame_recording; /* 1 if cmd list is open for recording */
    u32                   draw_count;      /* draws this frame */
    D3D12DrawRecord*      draws;           /* the RECORDING batch (s_draw_bufs[k]) */

    /* Pointer to current RSX state (set before draw calls) */
    const rsx_state*      current_rsx_state;

    /* Current frame state */
    float clear_color[4];  /* RGBA float */

    /* Stats */
    u64 frame_count;
    u64 last_fps_time;
    u32 fps;

    int initialized;
} D3D12State;

static D3D12State s_d3d;

/* RSX_GPU_VFETCH=1: the vertex shader reads the guest's raw vertex bytes and
 * converts them itself. The record thread then writes one 8-byte record per
 * output vertex (guest vertex index, draw descriptor) and copies each draw's
 * vertex range once, instead of converting every vertex into a 256-byte slot
 * (~45% of the FIFO thread in Code Veronica's flooded rooms). Fixed for the
 * process. Interpolation captures only position/UVs on CPU and supplies a
 * separate optional float3 override; the other attributes stay on GPU. */
#define VF_REC_BYTES   ((u64)MAX_VERTICES * 8u)
#define VF_DESC_SIZE   512u                     /* 16 attributes x 32 bytes */
#define VF_DESC_MAX    16384u
#define VF_DESC_OFF    VF_REC_BYTES
#define VF_ARENA_OFF   (VF_DESC_OFF + (u64)VF_DESC_SIZE * VF_DESC_MAX)
#define VF_ARENA_BYTES (48u << 20)
#define VF_BASE_PARITY_BYTES (VF_ARENA_OFF + VF_ARENA_BYTES)
#define VF_POSE_OFF VF_BASE_PARITY_BYTES
static RsxPoseComputeD3D12 s_pose_compute;
static RsxPoseComputePlan s_pose_compute_plan;
static int s_pose_compute_failed,s_pose_compute_capture_ready;
static const u8* s_pose_compute_capture;
static int pose_compute_mode(void) {
    static int requested=-1;
    if(requested<0){const char *mode=getenv("CV_POSE_COMPUTE"),*fps=getenv("CV_FPS_TEST_60"),*ri=getenv("CV_RENDER_INTERPOLATION"),*raw=getenv("CV_FPS_BENCHMARK_RAW"),*cpu=getenv("CV_POSE_CPU"),*dxc=getenv("SHADER_COMPILER");
        requested=mode&&!strcmp(mode,"1")&&fps&&!strcmp(fps,"1")&&ri&&!strcmp(ri,"1")&&!(raw&&*raw=='1')&&!(cpu&&*cpu=='1')&&dxc&&!_stricmp(dxc,"dxc");}
    return requested&&!s_pose_compute_failed;
}
static ID3D12Resource* s_vf_buf;
static u8* s_vf_mapped;
static u32 s_vf_ndesc, s_vf_aused;
static int vf_wrap_vs(char* hlsl, size_t cap);
static void vf_flush(void);
static void vf_frame_reset(void);
static void gc_install(ID3D12GraphicsCommandList* cl);
static void vf_capture_vertex(u32 parity,u32 vertex,float attributes[16][4]);
static int vf_pose_enabled(void) {
    static int on=-1;
    if(on<0) {
        const char *ri=getenv("CV_RENDER_INTERPOLATION"),*fps=getenv("CV_FPS_TEST_60"),*raw=getenv("CV_FPS_BENCHMARK_RAW"),*cpu=getenv("CV_POSE_CPU");
        on=ri&&*ri=='1'&&fps&&*fps=='1'&&!(raw&&*raw=='1')&&!(cpu&&*cpu=='1');
    }
    return on;
}
static u32 vf_pose_stride(void) {
    static u32 stride;
    if(!stride){const char* cpu=getenv("CV_POSE_CPU_BLEND");stride=!pose_compute_mode()&&cpu&&*cpu=='0'?RSX_POSE_GPU_STRIDE:RSX_POSE_GPU_CPU_STRIDE;}
    return stride;
}
static u64 vf_parity_bytes(void) {
    return VF_BASE_PARITY_BYTES+(vf_pose_enabled()?(u64)MAX_VERTICES*vf_pose_stride():0);
}
/* Upload stream sets (vp_vb, vp_cb, vp_fpcb, vf records and the pose
 * snapshots), indexed by vp_parity. Two by default. The 60 FPS CPU pose
 * overlap (pose_overlap_on) uses a third, so the producer can record the
 * next batch while the GPU still draws the previous one. Decided once,
 * before the buffers are allocated; CV_POSE_TRIPLE=0 keeps two. */
static int vp_sets(void) {
    static int sets;
    if(!sets) {
        const char *ri=getenv("CV_RENDER_INTERPOLATION"),*fps=getenv("CV_FPS_TEST_60"),*raw=getenv("CV_FPS_BENCHMARK_RAW");
        const char *overlap=getenv("CV_POSE_OVERLAP"),*triple=getenv("CV_POSE_TRIPLE");
        int interp=ri&&!strcmp(ri,"1")&&fps&&!strcmp(fps,"1")&&!(raw&&!strcmp(raw,"1"));
        sets=interp&&!pose_compute_mode()&&!(overlap&&!strcmp(overlap,"0"))&&!(triple&&!strcmp(triple,"0"))?3:2;
    }
    return sets;
}
static int vf_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("RSX_GPU_VFETCH");
        const char* ri = getenv("CV_RENDER_INTERPOLATION");
        on = (e && *e == '1' && (!(ri && *ri == '1')||vf_pose_enabled())) ? 1 : 0;
        if (on) fprintf(stderr, "[VFETCH] vertex conversion on the GPU%c", 10);
    }
    return on;
}

/* RSX_ANISO=2|4|8|16: anisotropic filtering on the guest's pixel texture
 * units, sharper floors and walls seen at an angle. Screen-aligned quads (HUD,
 * text, post passes) sample the same as before. 0/unset = trilinear, as the
 * guest asked. */
static UINT tex_aniso(void)
{
    static int n = -1;
    if (n < 0) {
        const char* e = getenv("RSX_ANISO");
        const int v = e ? atoi(e) : 0;
        n = v >= 16 ? 16 : v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 0;
        if (n) fprintf(stderr, "[D3D12] anisotropic filtering %dx%c", n, 10);
    }
    return (UINT)n;
}

/* RES_SCALE=<f> (1 < f <= 4; 1.5 = 1080p): render at f times the guest
 * resolution. Everything the backend records stays in GUEST pixels; the
 * scale is applied where they meet D3D12 -- resource sizes, viewports,
 * scissors and copy rectangles -- so no path above has to know. Targets of
 * at most 16384 pixels stay native: the CPU reads some of those back (Code
 * Veronica's 40x22 exposure target), and they gain nothing from more pixels.
 * Unset or 1: nothing changes. */
static float s_rs = 1.0f;
static u32 rs_dim(u32 v, float sc) { return sc == 1.0f ? v : (u32)((float)v * sc + 0.5f); }
/* Submitted by the game thread; consumed only on the render thread.
 * A batch belongs to the guest frame it was submitted in -- the one ended by
 * the title's next flip -- and is drawn over that frame only. The game's own
 * frame reaches the screen a variable time after it is built, so a batch
 * drawn on whatever present came next ran ahead of the game's layers (a
 * fade it should darken with) or lingered after its submitter stopped (a
 * menu the game no longer runs). A few slots, by flip, so the game can
 * build the next frame's batch while this one is still being rendered. */
/* The appearance preview plus all text can exceed 131072 quads at some
 * angles. Never drop the entire menu merely because the model rotates. */
#define UI_MAX_QUADS 262144u
#define UI_SLOTS 8u
u32 cellGcm_flip_submit_count(void);
static SRWLOCK s_ui_lock = SRWLOCK_INIT;
static rsx_ui_quad s_ui_quads[UI_SLOTS][UI_MAX_QUADS];
static u32 s_ui_count[UI_SLOTS], s_ui_frame[UI_SLOTS];
static uint8_t s_ui_valid[UI_SLOTS];
/* Explicit ownership for a host menu whose preview can take longer than one
 * guest flip. Default-off: normal UI cadence is unchanged. Empty batches still
 * clear immediately; a short lease prevents orphaned UI after a stopped owner. */
static volatile LONG s_ui_retained;
static ULONGLONG s_ui_last_submit;
void rsx_d3d12_set_ui_retained(int active) {
    InterlockedExchange(&s_ui_retained,active?1:0);
}
static u32 ui_reuse_age(void);

static int ui_interpolated(void) {
    static int on=-1;
    if(on<0){const char* fps=getenv("CV_FPS_TEST_60"),*pose=getenv("CV_RENDER_INTERPOLATION");
        on=fps&&!strcmp(fps,"1")&&pose&&!strcmp(pose,"1");}
    return on;
}
static u32 s_ui_render_flip;   /* the frame being rendered: s_rb's, set by render_frame */
/* A per-frame upload buffer of quad vertices, grown on demand. */
typedef struct QuadVb { ID3D12Resource* vb[FRAME_COUNT]; void* mapped[FRAME_COUNT]; u32 quads[FRAME_COUNT]; } QuadVb;
static QuadVb s_ui_vbs, s_ov_vbs, s_srt_vbs, s_spr_vbs;   /* the game's UI batch; the performance overlay; the SRT and its images */
typedef struct UiVertex { float x,y,z,r,g,b,a,u,v; } UiVertex;
void rsx_d3d12_set_ui_quads(const rsx_ui_quad* quads,u32 count)
{
    if (count > UI_MAX_QUADS) {
        fprintf(stderr,"[ui] rejected oversized batch: %u > %u\n",count,UI_MAX_QUADS);
        return; /* Keep the previous complete menu rather than flashing empty. */
    }
    if (!quads) count=0;
    const u32 frame=cellGcm_flip_submit_count()+1, slot=frame%UI_SLOTS;
    AcquireSRWLockExclusive(&s_ui_lock);
    if(count) memcpy(s_ui_quads[slot],quads,(size_t)count*sizeof(*quads));
    s_ui_count[slot]=count;s_ui_frame[slot]=frame;s_ui_valid[slot]=1;
    s_ui_last_submit=GetTickCount64();
    ReleaseSRWLockExclusive(&s_ui_lock);
}
/* Called with s_ui_lock held by both drawing and frame-generation gating. */
static u32 ui_reuse_age(void) {
    if(InterlockedCompareExchange(&s_ui_retained,0,0)&&GetTickCount64()-s_ui_last_submit<=500)
        return 0x7FFFFFFFu;
    return ui_interpolated()?3u:0u;
}
/* The display layer. The game renders at its render size into its own
 * targets (s_d3d.render_targets); the swap chain is the window's client size,
 * and each presented frame is the game image scaled into it, with the
 * port's overlays (SRT, performance panel, notices) drawn on top at the
 * window's own pixels, so they stay sharp whatever the render size.
 * DISPLAY_SCALE=0 goes back to the swap chain at render size, stretched by
 * DXGI, with the overlays drawn into the game image. */
static int s_display = 0;
static ID3D12Resource* s_disp_bufs[FRAME_COUNT];
static ID3D12DescriptorHeap* s_disp_rtv;
static u32 s_disp_w, s_disp_h;
/* Where quad_batch_draw draws: the game target of the frame by default. */
static int s_qb_display;
static D3D12_CPU_DESCRIPTOR_HANDLE s_qb_rtv;
static u32 s_qb_w, s_qb_h;

static ID3D12Resource* game_rt_new(u32 w, u32 h)
{
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv = {0}; cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    memcpy(cv.Color, s_d3d.clear_color, sizeof cv.Color);
    ID3D12Resource* r = NULL;
    /* PRESENT (= COMMON): the frame's barriers treat it like a swap chain buffer */
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_PRESENT, &cv, &IID_ID3D12Resource, (void**)&r))) return NULL;
    return r;
}

/* (Re)creates the game targets at the render size, and their RTVs. */
static int game_rts_create(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv);
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        s_d3d.render_targets[i] = game_rt_new(rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs));
        if (!s_d3d.render_targets[i]) return 0;
        s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, s_d3d.render_targets[i], NULL, rtv);
        rtv.ptr += s_d3d.rtv_descriptor_size;
    }
    return 1;
}

/* The swap chain buffers (window size) and their RTVs. */
static int display_bufs_get(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    s_disp_rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_disp_rtv, &rtv);
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        if (FAILED(s_d3d.swap_chain->lpVtbl->GetBuffer(s_d3d.swap_chain, i, &IID_ID3D12Resource,
                                                       (void**)&s_disp_bufs[i]))) return 0;
        s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, s_disp_bufs[i], NULL, rtv);
        rtv.ptr += s_d3d.rtv_descriptor_size;
    }
    return 1;
}

static int window_client(u32* w, u32* h)
{
    RECT cr;
    if (!s_d3d.hwnd || !GetClientRect(s_d3d.hwnd, &cr)) return 0;
    if (cr.right - cr.left < 64 || cr.bottom - cr.top < 64) return 0;   /* minimised */
    *w = (u32)(cr.right - cr.left); *h = (u32)(cr.bottom - cr.top);
    return 1;
}

/* Room for `count` quads in this frame's slot of `vbs`; the count that fits. */
static u32 quad_vb_reserve(u32 fi, u32 count, QuadVb* vbs)
{
    if(!count || fi>=FRAME_COUNT)return 0;
    if(!vbs->vb[fi] || vbs->quads[fi] < count) {
        u32 capacity=vbs->quads[fi] ? vbs->quads[fi] : 1024u;
        while(capacity<count && capacity<UI_MAX_QUADS)capacity*=2;
        if(capacity>UI_MAX_QUADS)capacity=UI_MAX_QUADS;
        if(count>capacity)count=capacity;
        D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd={0};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width=(u64)capacity*6*sizeof(UiVertex);rd.Height=1;
        rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;
        rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* grown=NULL;
        HRESULT hr=s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,&hp,
            D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,
            &IID_ID3D12Resource,(void**)&grown);
        D3D12_RANGE no_read={0,0};
        void* grown_mapped=NULL;
        if(SUCCEEDED(hr))hr=grown->lpVtbl->Map(grown,0,&no_read,&grown_mapped);
        if(FAILED(hr)) {
            if(grown)grown->lpVtbl->Release(grown);
            return 0;
        }
        if(vbs->vb[fi])vbs->vb[fi]->lpVtbl->Release(vbs->vb[fi]);
        vbs->vb[fi]=grown;vbs->mapped[fi]=grown_mapped;vbs->quads[fi]=capacity;
    }
    return count<vbs->quads[fi]?count:vbs->quads[fi];
}

/* Writes `quads` into this frame's slot of `vbs`; the count written. */
static u32 quad_vb_fill(u32 fi, const rsx_ui_quad* quads, u32 count, QuadVb* vbs)
{
    count=quad_vb_reserve(fi,count,vbs);
    UiVertex* dst=count?(UiVertex*)vbs->mapped[fi]:NULL;
    /* Two triangles per quad, split along the diagonal whose corners agree
     * best, so interpolated corner alphas follow an edge instead of crossing
     * it. */
    static const unsigned split_bl_tr[6]={0,1,2,2,1,3}, split_tl_br[6]={0,1,3,0,3,2};
    for(u32 q=0;q<count;q++) {
        const rsx_ui_quad* r=&quads[q];
        const u32 ca=r->corner_a?r->corner_a:0xFFFFFFFFu;
        const int a0=(int)(ca&255),a1=(int)((ca>>8)&255),a2=(int)((ca>>16)&255),a3=(int)(ca>>24);
        const unsigned* corners=abs(a0-a3)<abs(a1-a2)?split_tl_br:split_bl_tr;
        for(unsigned k=0;k<6;k++) {
            unsigned c=corners[k];
            UiVertex v={r->x+((c&2)?r->w:0),r->y+((c&1)?r->h:0),0,
                ((r->argb>>16)&255)/255.f,((r->argb>>8)&255)/255.f,
                (r->argb&255)/255.f,(r->argb>>24)/255.f*(float)((ca>>(8*c))&255)/255.f,0,0};
            dst[q*6+k]=v;
        }
    }
    return count;
}

/* Draws quads [first, first+n) of this frame's slot of `vbs` with `pso`. */
static void quad_vb_draw(u32 fi, QuadVb* vbs, u32 first, u32 n, const float mvp[16], ID3D12PipelineState* pso)
{
    ID3D12GraphicsCommandList* cl=s_d3d.cmd_list;
    if(!n || !pso || fi>=FRAME_COUNT || !vbs->vb[fi])return;
    u32 w=rs_dim(s_d3d.width,s_rs),h=rs_dim(s_d3d.height,s_rs);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap,&rtv);
    rtv.ptr+=(u64)fi*s_d3d.rtv_descriptor_size;
    if(s_qb_display) {rtv=s_qb_rtv;w=s_qb_w;h=s_qb_h;}
    cl->lpVtbl->OMSetRenderTargets(cl,1,&rtv,FALSE,NULL);
    D3D12_VIEWPORT vp={0,0,(float)w,(float)h,0,1};
    D3D12_RECT sc={0,0,(LONG)w,(LONG)h};
    cl->lpVtbl->RSSetViewports(cl,1,&vp);cl->lpVtbl->RSSetScissorRects(cl,1,&sc);
    cl->lpVtbl->SetGraphicsRootSignature(cl,s_d3d.root_signature);
    cl->lpVtbl->SetPipelineState(cl,pso);
    cl->lpVtbl->SetGraphicsRoot32BitConstants(cl,0,16,mvp,0);
    D3D12_VERTEX_BUFFER_VIEW vb={vbs->vb[fi]->lpVtbl->GetGPUVirtualAddress(vbs->vb[fi]),
        (first+n)*6*sizeof(UiVertex),sizeof(UiVertex)};
    cl->lpVtbl->IASetVertexBuffers(cl,0,1,&vb);
    cl->lpVtbl->IASetPrimitiveTopology(cl,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->lpVtbl->DrawInstanced(cl,n*6,1,first*6,0);
}

/* Draw `count` quads into back buffer `fi` with the flat UI pipeline, `mvp`
 * mapping their coordinates to clip space. Holds no lock: the caller keeps
 * `quads` alive until this returns. */
static void quad_batch_draw(u32 fi, const rsx_ui_quad* quads, u32 count, const float mvp[16], QuadVb* vbs)
{
    if(!count || !s_d3d.pipeline_state_ui)return;
    count=quad_vb_fill(fi,quads,count,vbs);
    quad_vb_draw(fi,vbs,0,count,mvp,s_d3d.pipeline_state_ui);
}

/* The SRT's images (rsx_ui_sprite): one atlas texture, uploaded through the
 * frame's command list when its generation changes, drawn by a pipeline like
 * the flat UI one that samples it. */
static void wait_for_gpu(void);
static void srv_write(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping);
static ID3D12PipelineState* s_spr_pso;
static ID3D12Resource *s_spr_tex, *s_spr_up;
static u64 s_spr_gen;
static int s_spr_failed;
static int sprite_pso(void)
{
    if(s_spr_pso || s_spr_failed)return s_spr_pso!=NULL;
    static const char vs[] =
        "cbuffer cb0 : register(b0){float4 c0;float4 c1;float4 c2;float4 c3;};\n"
        "struct VSIn { float3 pos:POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
        "struct VSOut{ float4 pos:SV_POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
        "VSOut main(VSIn i){ VSOut o; float4 p=float4(i.pos,1.0);\n"
        "  o.pos=c0*p.x+c1*p.y+c2*p.z+c3*p.w; o.col=i.col; o.uv=i.uv; return o; }\n";
    static const char ps[] =
        "Texture2D tex : register(t0);\n"
        "SamplerState smp : register(s0);\n"
        "struct PSIn{ float4 pos:SV_POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
        "float4 main(PSIn i):SV_TARGET{ return tex.SampleLevel(smp, i.uv, 0) * i.col; }\n";
    ID3DBlob *vb=NULL,*pb=NULL,*err=NULL;
    D3DCompile(vs,sizeof vs-1,"vs_sprite",NULL,NULL,"main","vs_5_0",0,0,&vb,&err);
    if(err){err->lpVtbl->Release(err);err=NULL;}
    D3DCompile(ps,sizeof ps-1,"ps_sprite",NULL,NULL,"main","ps_5_0",0,0,&pb,&err);
    if(err){err->lpVtbl->Release(err);err=NULL;}
    if(vb&&pb) {
        D3D12_INPUT_ELEMENT_DESC il[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={0};
        pd.pRootSignature=s_d3d.root_signature;
        pd.VS.pShaderBytecode=vb->lpVtbl->GetBufferPointer(vb);pd.VS.BytecodeLength=vb->lpVtbl->GetBufferSize(vb);
        pd.PS.pShaderBytecode=pb->lpVtbl->GetBufferPointer(pb);pd.PS.BytecodeLength=pb->lpVtbl->GetBufferSize(pb);
        pd.InputLayout.pInputElementDescs=il;pd.InputLayout.NumElements=3;
        pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pd.BlendState.RenderTarget[0].BlendEnable=TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp=D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha=D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha=D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask=UINT_MAX;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_UNKNOWN;
        pd.SampleDesc.Count=1;
        if(FAILED(s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device,&pd,&IID_ID3D12PipelineState,(void**)&s_spr_pso)))s_spr_pso=NULL;
    }
    if(vb)vb->lpVtbl->Release(vb);
    if(pb)pb->lpVtbl->Release(pb);
    if(!s_spr_pso) {s_spr_failed=1;printf("[D3D12] SRT image pipeline unavailable\n");}
    return s_spr_pso!=NULL;
}
static int sprite_atlas(const uint32_t* rgba, u32 w, u32 h, u64 gen)
{
    if(!rgba || !w || !h || !s_d3d.srv_heap)return 0;
    if(s_spr_tex && s_spr_gen==gen)return 1;
    if(s_spr_tex || s_spr_up) {   /* rare: the overlay's size changed */
        wait_for_gpu();
        if(s_spr_tex){s_spr_tex->lpVtbl->Release(s_spr_tex);s_spr_tex=NULL;}
        if(s_spr_up){s_spr_up->lpVtbl->Release(s_spr_up);s_spr_up=NULL;}
    }
    const u32 pitch=(w*4+255)&~255u;
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd={0};
    bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=(u64)pitch*h;bd.Height=1;
    bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,&hp,D3D12_HEAP_FLAG_NONE,&bd,
            D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void**)&s_spr_up))){s_spr_up=NULL;return 0;}
    u8* m=NULL;D3D12_RANGE nr={0,0};
    if(FAILED(s_spr_up->lpVtbl->Map(s_spr_up,0,&nr,(void**)&m))||!m)return 0;
    for(u32 y=0;y<h;y++)memcpy(m+(u64)y*pitch,rgba+(size_t)y*w,(size_t)w*4);
    s_spr_up->lpVtbl->Unmap(s_spr_up,0,NULL);
    D3D12_HEAP_PROPERTIES th={0};th.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td={0};
    td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
    if(FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,&th,D3D12_HEAP_FLAG_NONE,&td,
            D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&s_spr_tex))){s_spr_tex=NULL;return 0;}
    D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};
    src.pResource=s_spr_up;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width=w;src.PlacedFootprint.Footprint.Height=h;
    src.PlacedFootprint.Footprint.Depth=1;src.PlacedFootprint.Footprint.RowPitch=pitch;
    dst.pResource=s_spr_tex;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list,&dst,0,0,0,&src,NULL);
    D3D12_RESOURCE_BARRIER br={0};
    br.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;br.Transition.pResource=s_spr_tex;
    br.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    br.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;br.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&br);
    srv_write(SPRITE_SRV,s_spr_tex,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    s_spr_gen=gen;
    return 1;
}
/* The SRT's quads with its images between them: each image right after the
 * first `after` quads. */
static void srt_layers_draw(u32 fi, const rsx_ui_quad* q, u32 nq, const rsx_ui_sprite* sp, u32 ns, const float mvp[16])
{
    ID3D12GraphicsCommandList* cl=s_d3d.cmd_list;
    nq=nq&&s_d3d.pipeline_state_ui?quad_vb_fill(fi,q,nq,&s_srt_vbs):0;
    ns=quad_vb_reserve(fi,ns,&s_spr_vbs);
    UiVertex* dst=ns?(UiVertex*)s_spr_vbs.mapped[fi]:NULL;
    for(u32 i=0;i<ns;i++) {
        const rsx_ui_sprite* r=&sp[i];
        const float c[4]={((r->argb>>16)&255)/255.f,((r->argb>>8)&255)/255.f,(r->argb&255)/255.f,(r->argb>>24)/255.f};
        static const unsigned order[6]={0,1,2,2,1,3};
        for(unsigned k=0;k<6;k++) {
            const unsigned corner=order[k];
            UiVertex v={r->x+((corner&2)?r->w:0),r->y+((corner&1)?r->h:0),0,c[0],c[1],c[2],c[3],
                (corner&2)?r->u1:r->u0,(corner&1)?r->v1:r->v0};
            dst[i*6+k]=v;
        }
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gh;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap,&gh);
    gh.ptr+=(u64)SPRITE_SRV*s_d3d.srv_inc;
    u32 done=0;
    for(u32 i=0;i<ns;) {
        const u32 upto=sp[i].after<nq?sp[i].after:nq;
        if(upto>done){quad_vb_draw(fi,&s_srt_vbs,done,upto-done,mvp,s_d3d.pipeline_state_ui);done=upto;}
        u32 j=i;while(j<ns&&sp[j].after==sp[i].after)j++;
        cl->lpVtbl->SetDescriptorHeaps(cl,1,&s_d3d.srv_heap);
        cl->lpVtbl->SetGraphicsRootSignature(cl,s_d3d.root_signature);
        cl->lpVtbl->SetGraphicsRootDescriptorTable(cl,1,gh);
        quad_vb_draw(fi,&s_spr_vbs,i,j-i,mvp,s_spr_pso);
        i=j;
    }
    if(nq>done)quad_vb_draw(fi,&s_srt_vbs,done,nq-done,mvp,s_d3d.pipeline_state_ui);
}

/* The game's UI batch: logical 640x480, centred at 4:3. */
static void ui_batch_draw(u32 fi)
{
    const u32 flip=s_ui_render_flip;
    AcquireSRWLockShared(&s_ui_lock);
    /* UI producer can run at native cadence while render flips advance.
     * Hold at most one intervening update (up to four guest flips), including
     * offscreen flips, and honor a newer clear. Native 30 stays exact-frame. */
    int slot=rsx_ui_select_batch(flip,s_ui_frame,s_ui_valid,UI_SLOTS,ui_reuse_age());
    u32 count=slot>=0?s_ui_count[slot]:0;
    if(count) {
        u32 w=rs_dim(s_d3d.width,s_rs),h=rs_dim(s_d3d.height,s_rs);
        float mvp[16]={0};
        mvp[0]=2.f*(float)h/(480.f*(float)w);mvp[5]=-2.f/480.f;
        mvp[10]=mvp[15]=1;mvp[12]=-640.f*(float)h/(480.f*(float)w);mvp[13]=1;
        quad_batch_draw(fi,s_ui_quads[slot],count,mvp,&s_ui_vbs);
    }
    ReleaseSRWLockShared(&s_ui_lock);
}

/* The performance overlay (rsx_perf_overlay.h): back-buffer pixels, over the
 * finished frame, so anti-aliasing and the colour controls leave it alone. */
/* Optional port-owned, read-only telemetry overlay. Independent GPU buffers
 * keep its vertices alive alongside the performance panel in the same frame. */
const rsx_ui_quad* (*g_rsx_srt_hook)(uint32_t,uint32_t,uint32_t*);
/* The images that go with the quads g_rsx_srt_hook just returned. */
const rsx_ui_sprite* (*g_rsx_srt_sprites_hook)(uint32_t*,const uint32_t**,uint32_t*,uint32_t*,uint64_t*);
static void perf_overlay_draw(u32 fi)
{
    u32 w=rs_dim(s_d3d.width,s_rs),h=rs_dim(s_d3d.height,s_rs),count=0;
    if(s_qb_display) {w=s_qb_w;h=s_qb_h;}
    float mvp[16]={0};
    mvp[0]=2.f/(float)w;mvp[5]=-2.f/(float)h;mvp[10]=mvp[15]=1;mvp[12]=-1;mvp[13]=1;
    if(g_rsx_srt_hook) {
        const rsx_ui_quad* srt=g_rsx_srt_hook(w,h,&count);
        u32 ns=0,aw=0,ah=0;u64 gen=0;const uint32_t* atlas=NULL;
        const rsx_ui_sprite* sp=g_rsx_srt_sprites_hook?g_rsx_srt_sprites_hook(&ns,&atlas,&aw,&ah,&gen):NULL;
        if(ns&&sp&&sprite_pso()&&sprite_atlas(atlas,aw,ah,gen))srt_layers_draw(fi,srt,count,sp,ns,mvp);
        else if(count)quad_batch_draw(fi,srt,count,mvp,&s_srt_vbs);
    }
    const rsx_ui_quad* quads=perf_overlay_quads(w,h,&count);
    if(count)quad_batch_draw(fi,quads,count,mvp,&s_ov_vbs);
}

static float rs_for(u32 w, u32 h) { return (s_rs != 1.0f && (u64)w * h > 16384u) ? s_rs : 1.0f; }
static void rs_set_vp(float sc, const D3D12_VIEWPORT* vp, const D3D12_RECT* rc)
{
    D3D12_VIEWPORT v = *vp; D3D12_RECT r = *rc;
    if (sc != 1.0f) {
        v.TopLeftX *= sc; v.TopLeftY *= sc; v.Width *= sc; v.Height *= sc;
        r.left = (LONG)((float)r.left * sc + 0.5f);  r.top = (LONG)((float)r.top * sc + 0.5f);
        r.right = (LONG)((float)r.right * sc + 0.5f); r.bottom = (LONG)((float)r.bottom * sc + 0.5f);
    }
    s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &v);
    s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list, 1, &r);
}

/* Two batches: the FIFO drain records into s_d3d.draws while the render
 * thread builds and submits the frozen one in s_rb. render_frame() freezes
 * the recording batch into s_rb, points recording at the other buffer, and
 * hands s_rb to the render thread (or runs it inline with
 * RSX_RENDER_THREAD=0). Before this the drain and the D3D12 work ran one
 * after the other on the frame clock, and the guest's main thread spent
 * most of each frame polling a label the drain could not write while a
 * frame was being rendered.
 *
 * Everything render-side reads the batch through s_rb: the draw list and
 * count, the upload-stream parity, the FP/vertex-texture snapshot arenas
 * and the present flag. Record-side code keeps using s_d3d.* and the
 * s_fp_arena / s_vtex_arena of the recording batch. */
static D3D12DrawRecord s_draw_bufs[2][MAX_DRAWS];
typedef struct {
    D3D12DrawRecord* draws;
    u32  count;
    int  parity;
    u8*  fp_arena;
    u8*  vtex_arena;
    int  present;
    u32 pose_stamp,pose_nv,pose_scene;
    int pose_done;   /* replay: the producer already interpolated this batch */
    u64 pose_seq;    /* ...or the pose thread is: wait for this job (pose_thread_wait) */
    double profile_producer,profile_backpressure;
    double profile_producer_cpu; int profile_producer_valid;
} RenderBatch;
static RenderBatch s_rb;
/* Optional CPU-side position/UV snapshots. Never read the write-combined
 * upload heap to obtain history. Normal runs allocate none of these. */
static int s_pose_on;
static int opt30_mode(void) {
    static int mode=-2;
    if(mode==-2)mode=rsx_opt30_mode(getenv("CV30_OPTIMIZATIONS"),getenv("CV_FPS_TEST_60"));
    return mode;
}
static int precomputed_tex_edges_on(void) {return opt30_mode()!=0;}
static u64 s_prepass_repeat_reused;
static int prepass_repeat_on(void);
static int prep_reuse_on(void) {
    static int disabled=-1;if(disabled<0)disabled=getenv("CV_PREP_NOREUSE")?1:0;
    return rsx_opt30_prep(opt30_mode(),s_pose_on,disabled);
}
static u64 s_prep_pipelines,s_prep_vtex_reused,s_prep_vtex_written;
static u64 s_shadow_batch_groups,s_shadow_batch_saved;
static int prepass_repeat_on(void) {
    static int disabled=-1;
    if(disabled<0){const char* e=getenv("CV_PREP_REPEAT");disabled=e&&!strcmp(e,"0");}
    return s_pose_on&&prep_reuse_on()&&!disabled;
}
static int shadow_batch_on(void) {
    static int on=-1;
    if(on<0){const char* e=getenv("CV_SHADOW_BATCH");
        const char* fps=getenv("CV_FPS_TEST_60"),*pose=getenv("CV_RENDER_INTERPOLATION");
        int legacy=fps&&!strcmp(fps,"1")&&pose&&!strcmp(pose,"1");
        on=rsx_opt30_shadow(opt30_mode(),legacy,e&&!strcmp(e,"0"));}
    return on;
}

static volatile LONG s_pose_scene;
static u32 s_pose_stamp=1;
static RsxPoseVertex* s_pose_vertices;
static float* s_pose_cameras;
static RsxPoseDraw* s_pose_draws;
/* Merge/upload state of the batch being interpolated: slices of the per-set
 * arrays below, so a replay can undo one set while the next one is posed. */
static u32 *s_pose_seen_vertices,*s_pose_seen_cameras;
static float *s_pose_out_vertices,*s_pose_out_cameras;
static u32 *s_pose_seen_vertices_sets,*s_pose_seen_cameras_sets;
static float *s_pose_out_vertices_sets,*s_pose_out_cameras_sets;
static u8 s_pose_accepted[MAX_DRAWS];
static RsxPoseHistory s_pose_history;
static int s_pose_gpu_blend;
static u64* s_pose_recipes;
static u32 s_pose_shadow_draws;
static int s_pose_profile,s_pose_search_profile;
static int s_pose_written[3];   /* per upload set: holds interpolated positions/cameras */
static int s_replay_pass;    /* render thread: drawing the current batch a second time */
static int s_pose_conflict_follow;   /* CV_POSE_CONFLICT_FOLLOW, see pose_interpolate_batch_for */
static double pose_time(void) {
    static LARGE_INTEGER frequency;LARGE_INTEGER now;
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);return (double)now.QuadPart/(double)frequency.QuadPart;
}
static double pose_phase_elapsed(double* mark) {double now=pose_time(),elapsed=now-*mark;*mark=now;return elapsed;}
/* One of 61 large prepares. Only coarse section clocks; v28's very short
 * nested estimates were timer-dominated and are deliberately no longer emitted. */
static int pose_prepare_profiled(RsxPoseHistory* history,const RsxPoseFrame* frame,double now) {
    if(!s_pose_search_profile||frame->nd<1000)return rsx_pose_prepare(history,frame,now);
    static u32 sequence,sampled;static double reported;
    static double sections[RSX_POSE_P_COUNT];static u64 positions,cameras;
    if(sequence++%61u)return rsx_pose_prepare(history,frame,now);
    RsxPoseSearchProfile probe={0};probe.clock=pose_time;
    history->search_profile=&probe;
    int apply=rsx_pose_prepare(history,frame,now);
    history->search_profile=NULL;
    for(int i=0;i<RSX_POSE_P_COUNT;i++)sections[i]+=probe.seconds[i];
    positions+=probe.position_calls;cameras+=probe.camera_calls;sampled++;
    if(!reported)reported=now;
    if(now-reported>=5) {
        double total=0;for(int i=0;i<RSX_POSE_P_COUNT;i++)total+=sections[i];
        fprintf(stderr,"[POSE-SEARCH] v29 sampled_prepares=%u cadence=1/61 CPU_ms/sampled_prepare validate=%.3f index=%.3f setup=%.3f search=%.3f resolve=%.3f total=%.3f position_calls=%.1f camera_calls=%.1f (sections include probe overhead; no nested time estimates; not FPS benchmark)\n",
            sampled,sections[0]*1000/sampled,sections[1]*1000/sampled,sections[2]*1000/sampled,
            sections[3]*1000/sampled,sections[4]*1000/sampled,total*1000/sampled,
            (double)positions/sampled,(double)cameras/sampled);
        memset(sections,0,sizeof sections);positions=cameras=0;sampled=0;reported=now;
    }
    return apply;
}
void rsx_d3d12_pose_scene(u32 scene) {InterlockedExchange(&s_pose_scene,(LONG)scene);}
static void pose_init_recording(void) {
    const char *e=getenv("CV_RENDER_INTERPOLATION"),*fps=getenv("CV_FPS_TEST_60"),*raw=getenv("CV_FPS_BENCHMARK_RAW");
    if(!e||strcmp(e,"1")||!fps||strcmp(fps,"1")||(raw&&!strcmp(raw,"1")))return;
    {const char* profile=getenv("CV_POSE_PROFILE");s_pose_profile=profile&&*profile=='1';}
    {const char* detail=getenv("CV_POSE_SEARCH_PROFILE");s_pose_search_profile=detail&&!strcmp(detail,"1");}
    s_pose_vertices=(RsxPoseVertex*)calloc((size_t)MAX_VERTICES*vp_sets(),sizeof(*s_pose_vertices));
    s_pose_cameras=(float*)calloc((size_t)MAX_DRAWS*vp_sets()*32,sizeof(float));
    s_pose_draws=(RsxPoseDraw*)calloc(MAX_DRAWS,sizeof(*s_pose_draws));
    s_pose_seen_vertices=s_pose_seen_vertices_sets=(u32*)calloc((size_t)MAX_VERTICES*vp_sets(),sizeof(u32));
    s_pose_seen_cameras=s_pose_seen_cameras_sets=(u32*)calloc((size_t)MAX_DRAWS*vp_sets(),sizeof(u32));
    s_pose_out_vertices=s_pose_out_vertices_sets=(float*)calloc((size_t)MAX_VERTICES*3*vp_sets(),sizeof(float));
    s_pose_out_cameras=s_pose_out_cameras_sets=(float*)calloc((size_t)MAX_DRAWS*32*vp_sets(),sizeof(float));
    if(!s_pose_vertices||!s_pose_cameras||!s_pose_draws||!s_pose_seen_vertices||!s_pose_seen_cameras||!s_pose_out_vertices||!s_pose_out_cameras||!rsx_pose_init(&s_pose_history,MAX_VERTICES,MAX_DRAWS)) {
        free(s_pose_vertices);free(s_pose_cameras);free(s_pose_draws);free(s_pose_seen_vertices);free(s_pose_seen_cameras);
        s_pose_vertices=NULL;s_pose_cameras=NULL;s_pose_draws=NULL;s_pose_seen_vertices=s_pose_seen_cameras=NULL;
        free(s_pose_out_vertices);free(s_pose_out_cameras);s_pose_out_vertices=s_pose_out_cameras=NULL;
        s_pose_seen_vertices_sets=s_pose_seen_cameras_sets=NULL;s_pose_out_vertices_sets=s_pose_out_cameras_sets=NULL;
        fprintf(stderr,"[POSE] allocation failed; original drawing retained\n");return;
    }
    {const char* cpu=getenv("CV_POSE_CPU_BLEND");
     s_pose_gpu_blend=!pose_compute_mode()&&s_vf_mapped&&cpu&&*cpu=='0';
     if(s_pose_gpu_blend)s_pose_recipes=(u64*)malloc((size_t)MAX_VERTICES*sizeof(u64));
     if(!s_pose_recipes)s_pose_gpu_blend=0;}
    {const char* disabled=getenv("CV_POSE_NOVERTEX_VALIDATION_CACHE");s_pose_history.vertex_validation_cache_disabled=disabled&&*disabled=='1';}
    {const char* cap=getenv("CV_POSE_CAP_PRECHECK");s_pose_history.cap_precheck_disabled=cap&&!strcmp(cap,"0");}
    {const char* e=getenv("CV_POSE_CONFLICT_FOLLOW");s_pose_conflict_follow=!(e&&!strcmp(e,"0"));
     fprintf(stderr,"[POSE-CONFLICT] v4 follow=%s; a conflicted draw half pulled by a seam moves whole\n",s_pose_conflict_follow?"on":"off (CV_POSE_CONFLICT_FOLLOW=0)");}
    {const char* off=getenv("CV_POSE_REPEAT_WORK");s_pose_history.repeat_work_disabled=off&&!strcmp(off,"0");}
    {const char* stream=getenv("CV_POSE_STREAM_VECTORS");s_pose_history.stream_vectors=!stream||strcmp(stream,"0");}
    fprintf(stderr,"[POSE-STREAM] v1 %s; exact position packing and stationary checks\n",s_pose_history.stream_vectors?"SSE2":"original");
    s_pose_on=1;
    if(pose_compute_mode())fprintf(stderr,"[POSE-COMPUTE] v42 enabled; GPU geometry coherence/merge/alias/seams/output; CPU matching/topology/cameras; logic 30 Hz, target 60 Hz\n");
    else fprintf(stderr,"[POSE] v20 final position blend=%s; CPU matching/alias/seam safeguards; logic 30 Hz, target 60 Hz\n",s_pose_gpu_blend?"GPU":"CPU");
}
static u64 pose_mix(u64 h,u32 x) {return (h^x)*1099511628211ull;}
/* Topology and temporal identity stay on CPU; no geometry interpolation,
 * proposal comparison or numerical seam resolution executes here. */
static int pose_compute_prepare(const RsxPoseFrame* frame,u32* changed_cameras) {
    if(!pose_compute_mode()||!s_pose_compute.ready)return 0;
    if(!rsx_pose_cohere_ex(&s_pose_history,frame,1))return 0;
    memset(s_pose_seen_vertices,0,MAX_VERTICES*sizeof(u32));
    memset(s_pose_seen_cameras,0,MAX_DRAWS*sizeof(u32));
    for(u32 i=0;i<s_rb.count&&i<MAX_DRAWS;i++) {
        const D3D12DrawRecord* d=&s_rb.draws[i];if(s_pose_accepted[i]||!d->is_vp||d->is_clear||d->is_copy)continue;
        if(d->vcb_slot<MAX_DRAWS&&(d->vs_idx<0||d->vs_idx>=s_d3d.vp_vs_n||s_d3d.vp_vs[d->vs_idx].uses_c03))s_pose_seen_cameras[d->vcb_slot]=2;
        if(d->vb_byte_offset%256)continue;u32 first=d->vb_byte_offset/256;
        if(first>frame->nv||d->vertex_count>frame->nv-first)continue;
        for(u32 k=0;k<d->vertex_count;k++)s_pose_seen_vertices[first+k]=2;
    }
    if(!rsx_pose_compute_plan_build(&s_pose_compute_plan,&s_pose_history,frame,s_pose_seen_vertices)||
       !pc_upload(&s_pose_compute,s_d3d.device,&s_pose_compute_plan))return 0;
    /* Camera projections retain their existing CPU/light-bank protection. */
    for(u32 i=0;i<frame->nd;i++) {
        const RsxPoseDraw* d=frame->draws+i;if(s_pose_seen_cameras[d->cb]==2)continue;
        float matrix[32];int32_t source=s_pose_history.camera_sources[i];
        if(source>=0)rsx_pose_camera(&s_pose_history,(u32)source,matrix);else memcpy(matrix,frame->cameras+(size_t)d->cb*32,sizeof matrix);
        rsx_pose_merge(s_pose_out_cameras+(size_t)d->cb*32,s_pose_seen_cameras+d->cb,matrix,32);
    }
    memset(s_pose_history.seeds,0,frame->nd*sizeof(int32_t));
    for(u32 i=0;i<frame->nd;i++)if(s_pose_seen_cameras[frame->draws[i].cb]==2)s_pose_history.seeds[s_pose_history.camera_groups[i]]=1;
    for(u32 i=0;i<frame->nd;i++)if(s_pose_history.seeds[s_pose_history.camera_groups[i]])s_pose_seen_cameras[frame->draws[i].cb]=2;
    u8* cb=(u8*)s_d3d.vp_cb_mapped+(size_t)s_rb.parity*MAX_DRAWS*VP_CB_STRIDE;
    for(u32 c=0;c<MAX_DRAWS;c++)if(s_pose_seen_cameras[c]==1&&memcmp(frame->cameras+(size_t)c*32,s_pose_out_cameras+(size_t)c*32,128)){
        memcpy(cb+(size_t)c*VP_CB_STRIDE,s_pose_out_cameras+(size_t)c*32,128);(*changed_cameras)++;
    }
    return 1;
}

static void pose_interpolate_batch_for(const RenderBatch* rb) {
    s_pose_compute.active=0;
    if(!s_pose_on||!s_d3d.vp_vb_mapped||!s_d3d.vp_cb_mapped)return;
    /* A partial/readback batch is not a new displayed frame. */
    if(!rb->present)return;
    if((u32)rb->parity>=(u32)vp_sets())return;
    s_pose_written[rb->parity]=0;
    s_pose_seen_vertices=s_pose_seen_vertices_sets+(size_t)rb->parity*MAX_VERTICES;
    s_pose_seen_cameras=s_pose_seen_cameras_sets+(size_t)rb->parity*MAX_DRAWS;
    s_pose_out_vertices=s_pose_out_vertices_sets+(size_t)rb->parity*MAX_VERTICES*3;
    s_pose_out_cameras=s_pose_out_cameras_sets+(size_t)rb->parity*MAX_DRAWS*32;
    double phases[7]={0},phase=s_pose_profile?pose_time():0;
    const RsxPoseVertex* vertices=s_pose_vertices+(size_t)rb->parity*MAX_VERTICES;
    const float* cameras=s_pose_cameras+(size_t)rb->parity*MAX_DRAWS*32;
    u32 nd=0;s_pose_shadow_draws=0;memset(s_pose_accepted,0,sizeof s_pose_accepted);
    for(u32 i=0;i<rb->count&&i<MAX_DRAWS;i++) {
        const D3D12DrawRecord* d=&rb->draws[i];
        /* Additive lighting is blended 3D geometry, not a screen overlay.
         * It must follow the same pose as the depth and material passes. */
        if(!d->is_vp||d->is_clear||d->is_copy||!d->dtest||d->topology!=RSX_TOPOLOGY_TRIANGLES||d->vertex_count<3||d->vcb_slot>=MAX_DRAWS||d->vs_idx<0||d->vs_idx>=s_d3d.vp_vs_n)continue;
        const VPVSEntry* vs=&s_d3d.vp_vs[d->vs_idx];
        if(!vs->uses_c03||!rsx_pose_camera_valid(cameras+(size_t)d->vcb_slot*32))continue;
        if(!vs->pose_inputs_known)continue;
        if(d->vb_byte_offset%256)continue;u32 first=d->vb_byte_offset/256,n=d->vertex_count;
        if(first>rb->pose_nv||n>rb->pose_nv-first)continue;
        u64 key=1469598103934665603ull;
        /* Surface/texture addresses rotate between banks. Identity uses pass
         * shape, material layout and ordered UVs, then a spatial match. */
        const u32 props[]={vs->hash,n,d->rt_fmt,d->rt_w,d->rt_h,d->vp_x,d->vp_y,d->vp_w,d->vp_h,d->cmask,d->dfunc,d->cull,d->alpha_ctl,d->s_tgt,d->blend,d->blend_key};
        for(u32 j=0;j<sizeof(props)/sizeof(props[0]);j++)key=pose_mix(key,props[j]);
        for(u32 t=0;t<FP_TEX_UNITS;t++){key=pose_mix(key,d->tex[t].fmt);key=pose_mix(key,d->tex[t].w);key=pose_mix(key,d->tex[t].h);key=pose_mix(key,d->tex[t].cube);key=pose_mix(key,d->tex[t].set);}
        int recorded=1;
        for(u32 k=0;k<n;k++){const RsxPoseVertex* v=vertices+first+k;if(v->stamp!=rb->pose_stamp){recorded=0;break;}key=pose_mix(key,v->uv);}
        if(!recorded)continue;
        const u32 fragment_code=fp_snap_code_in(d,rb->fp_arena);
        /* Missing frozen identity cannot establish a safe temporal material
         * match. Existing unaccepted-upload protection keeps these original. */
        if(!fragment_code)continue;
        key=rsx_pose_material_key(key,fragment_code);
        RsxPoseDraw* out=s_pose_draws+nd++;out->key=key;out->first=first;out->count=n;out->cb=d->vcb_slot;
        /* Read the cached code hash from THIS frozen batch, not the guest
         * address (programs rotate), and never the recording thread's arena. */
        out->flags=rsx_pose_program_flags(fragment_code,vs->hash);
        if(out->flags&RSX_POSE_CURRENT_LIGHT_CAMERA)s_pose_shadow_draws++;
        s_pose_accepted[i]=1;
    }
    RsxPoseFrame frame={vertices,cameras,s_pose_draws,rb->pose_nv,nd,rb->pose_scene,rb->pose_stamp};
    double now=pose_time();if(s_pose_profile)phases[0]=pose_phase_elapsed(&phase);
    int apply=pose_prepare_profiled(&s_pose_history,&frame,now);
    if(s_pose_profile)phases[1]=pose_phase_elapsed(&phase);
    u32 moved=0,changed_cameras=0,conflicts=0,gpu_blended=0;
    const int compute_applied=apply&&pose_compute_prepare(&frame,&changed_cameras);
    if(apply&&!compute_applied&&!rsx_pose_cohere(&s_pose_history,&frame))apply=0;
    if(s_pose_profile)phases[2]=pose_phase_elapsed(&phase);
    if(s_pose_profile&&pose_compute_mode()) {
        static double at;static u64 frames,active,fallback;frames++;active+=compute_applied!=0;fallback+=apply&&!compute_applied;
        if(now-at>=5.){fprintf(stderr,"[POSE-COMPUTE] v42 frames=%llu compute=%llu cpu_fallback=%llu input_kb=%.1f (GPU: cross-pass proposals/merge/alias/seams/output; CPU: temporal matching/topology/cameras; no frame readback)\n",
            frames,active,fallback,s_pose_compute_plan.bytes/1024.0);at=now;frames=active=fallback=0;}
    }
    if(apply&&!compute_applied) {
        s_pose_written[rb->parity]=1;
        memset(s_pose_seen_vertices,0,MAX_VERTICES*sizeof(u32));memset(s_pose_seen_cameras,0,MAX_DRAWS*sizeof(u32));
        u8* vb=(u8*)s_d3d.vp_vb_mapped+(size_t)rb->parity*MAX_VERTICES*256;
        u8* cb=(u8*)s_d3d.vp_cb_mapped+(size_t)rb->parity*MAX_DRAWS*VP_CB_STRIDE;
        for(u32 i=0;i<nd;i++) {const RsxPoseDraw* d=s_pose_draws+i;
            const int32_t source=s_pose_history.vertex_sources[i],camera_source=s_pose_history.camera_sources[i];
            for(u32 k=0;k<d->count;k++) {
                const u32 v=d->first+k;if(s_pose_seen_vertices[v]==2)continue;float xyz[3];
                if(source>=0)rsx_pose_vertex(&s_pose_history,(u32)source,k,xyz);else memcpy(xyz,vertices[v].xyz,sizeof xyz);
                if(s_pose_gpu_blend&&!s_pose_seen_vertices[v])s_pose_recipes[v]=source<0?UINT64_MAX:
                    ((u64)(u32)source<<32)|(u64)(s_pose_history.cur.draws[source].first+k);
                rsx_pose_merge(s_pose_out_vertices+(size_t)v*3,s_pose_seen_vertices+v,xyz,3);
            }
            if(s_pose_seen_cameras[d->cb]==2)continue;
            float matrix[32];
            if(camera_source>=0)rsx_pose_camera(&s_pose_history,(u32)camera_source,matrix);else memcpy(matrix,cameras+(size_t)d->cb*32,sizeof matrix);
            rsx_pose_merge(s_pose_out_cameras+(size_t)d->cb*32,s_pose_seen_cameras+d->cb,matrix,32);
        }
        /* Unsupported draws can share an upload range or constant bank.
         * Protect those aliases before writing any interpolated bytes. */
        for(u32 i=0;i<rb->count&&i<MAX_DRAWS;i++) {
            const D3D12DrawRecord* d=&rb->draws[i];
            if(s_pose_accepted[i]||!d->is_vp||d->is_clear||d->is_copy)continue;
            /* A screen shader that never reads c0..c3 cannot be affected by
             * these camera constants, even if its stale bank was deduplicated. */
            if(d->vcb_slot<MAX_DRAWS&&(d->vs_idx<0||d->vs_idx>=s_d3d.vp_vs_n||s_d3d.vp_vs[d->vs_idx].uses_c03))s_pose_seen_cameras[d->vcb_slot]=2;
            if(d->vb_byte_offset%256)continue;
            u32 first=d->vb_byte_offset/256;
            if(first>rb->pose_nv||d->vertex_count>rb->pose_nv-first)continue;
            for(u32 k=0;k<d->vertex_count;k++)s_pose_seen_vertices[first+k]=2;
        }
        if(s_pose_profile)phases[3]=pose_phase_elapsed(&phase);
        rsx_pose_protect_aliases(&s_pose_history,&frame,s_pose_seen_vertices,s_pose_seen_cameras);
        if(s_pose_profile)phases[4]=pose_phase_elapsed(&phase);
        /* Resolve duplicate points across UV/material boundaries AFTER the
         * alias vetoes: all copies of an edge must still close exactly. */
        if(!rsx_pose_weld_recipe(&s_pose_history,&frame,s_pose_out_vertices,s_pose_seen_vertices,s_pose_gpu_blend?s_pose_recipes:NULL)) {
            for(u32 v=0;v<rb->pose_nv;v++)s_pose_seen_vertices[v]=2;
            for(u32 c=0;c<MAX_DRAWS;c++)s_pose_seen_cameras[c]=2;
            apply=0;
        }
        /* A coherence conflict (-2) keeps the current pose, but the weld
         * still moves its points shared with an interpolated neighbour. Half
         * pulled, its triangles tore (Steve's mouth and hair, JP F9: 254 of
         * 440 conflicted draws). Move its remaining points by the mean pull,
         * so the draw follows the neighbour whole. Nothing else changes. */
        if(apply&&s_pose_conflict_follow)for(u32 i=0;i<nd;i++) {
            if(s_pose_history.vertex_sources[i]!=-2)continue;
            const RsxPoseDraw* d=s_pose_draws+i;double pull[3]={0,0,0};u32 pulled=0,still=0;
            for(u32 k=0;k<d->count;k++) {
                const u32 v=d->first+k;if(s_pose_seen_vertices[v]!=1)continue;
                const float* out=s_pose_out_vertices+(size_t)v*3;
                if(!memcmp(vertices[v].xyz,out,3*sizeof(float))){still++;continue;}
                for(int a=0;a<3;a++)pull[a]+=out[a]-vertices[v].xyz[a];pulled++;
            }
            if(!pulled||!still)continue;
            for(u32 k=0;k<d->count;k++) {
                const u32 v=d->first+k;if(s_pose_seen_vertices[v]!=1)continue;
                float* out=s_pose_out_vertices+(size_t)v*3;
                if(memcmp(vertices[v].xyz,out,3*sizeof(float)))continue;
                for(int a=0;a<3;a++)out[a]=vertices[v].xyz[a]+(float)(pull[a]/pulled);
                if(s_pose_gpu_blend)s_pose_recipes[v]=UINT64_MAX;
            }
        }
        if(s_pose_profile)phases[5]=pose_phase_elapsed(&phase);
        for(u32 v=0;v<rb->pose_nv;v++) {
            if(s_pose_seen_vertices[v]==1&&memcmp(vertices[v].xyz,s_pose_out_vertices+(size_t)v*3,3*sizeof(float)))
                {if(s_vf_mapped) {
                    u8* record=s_vf_mapped+(u64)rb->parity*vf_parity_bytes()+VF_POSE_OFF+(size_t)v*vf_pose_stride();
                    int blended=0;
                    if(s_pose_gpu_blend&&s_pose_recipes[v]!=UINT64_MAX) {
                        u32 draw=(u32)(s_pose_recipes[v]>>32),at=(u32)s_pose_recipes[v];
                        const RsxPoseSnapshot* snapshot=&s_pose_history.cur;
                        if(draw<snapshot->nd&&at<snapshot->nv)blended=rsx_pose_gpu_write_blend(record,
                            snapshot->old_xyz+(size_t)at*3,snapshot->xyz+(size_t)at*3,snapshot->tracks[draw].alpha);
                    }
                    if(!blended)rsx_pose_gpu_write(record,s_pose_out_vertices+(size_t)v*3);
                    gpu_blended+=(u32)blended;
                 }
                 else memcpy(vb+(size_t)v*256,s_pose_out_vertices+(size_t)v*3,3*sizeof(float));moved++;}
            else if(s_pose_seen_vertices[v]==2)conflicts++;
        }
        for(u32 c=0;c<MAX_DRAWS;c++)if(s_pose_seen_cameras[c]==1&&memcmp(cameras+(size_t)c*32,s_pose_out_cameras+(size_t)c*32,32*sizeof(float)))
            {memcpy(cb+(size_t)c*VP_CB_STRIDE,s_pose_out_cameras+(size_t)c*32,32*sizeof(float));changed_cameras++;}
        if(s_pose_profile)phases[6]=pose_phase_elapsed(&phase);
    }
    if(s_pose_profile&&s_pose_gpu_blend) {
        static double at;static u64 count,total,moved_total;
        count++;total+=gpu_blended;moved_total+=moved;
        if(now-at>=5.){fprintf(stderr,"[POSE-GPU] %llu presents GPU_blended=%.1f CPU_fallback=%.1f vertices/present; no GPU readback\n",
            (unsigned long long)count,(double)total/count,(double)(moved_total-total)/count);at=now;count=total=moved_total=0;}
    }
    if(s_pose_profile) {
        static double totals[7],reported;static u32 count;static u64 camera_hits,camera_misses,camera_uncached,search_candidates,search_pruned,blocks_pruned;static u64 validated_vertices,reused_validations,sample_groups,position_hits,position_misses,position_bypass,cap_avoided;
        cap_avoided+=s_pose_history.capped_searches_skipped;
        camera_hits+=s_pose_history.camera_hits;camera_misses+=s_pose_history.camera_misses;camera_uncached+=s_pose_history.camera_uncached;
        sample_groups+=s_pose_history.sample_groups_reused;position_hits+=s_pose_history.position_hits;position_misses+=s_pose_history.position_misses;position_bypass+=s_pose_history.position_uncached;
        search_candidates+=s_pose_history.search_candidates;search_pruned+=s_pose_history.search_pruned;blocks_pruned+=s_pose_history.search_blocks_pruned;
        validated_vertices+=s_pose_history.validated_vertices;reused_validations+=s_pose_history.reused_validations;
        for(int i=0;i<7;i++)totals[i]+=phases[i];count++;
        if(!reported)reported=now;
        if(now-reported>=5) {
            double total=0;for(int i=0;i<7;i++)total+=totals[i];
            fprintf(stderr,"[POSE-PERF] %u frames fetch=%s CPU_ms/present collect=%.3f history=%.3f coherence=%.3f merge=%.3f aliases=%.3f seams=%.3f upload=%.3f total=%.3f camera_hits=%.1f camera_misses=%.1f camera_bypass=%.1f search_candidates=%.1f search_pruned=%.1f (counts/present; excludes record/GPU)\n",count,s_vf_mapped?"GPU":"CPU",
                totals[0]*1000/count,totals[1]*1000/count,totals[2]*1000/count,totals[3]*1000/count,totals[4]*1000/count,totals[5]*1000/count,totals[6]*1000/count,total*1000/count,(double)camera_hits/count,(double)camera_misses/count,(double)camera_uncached/count,(double)search_candidates/count,(double)search_pruned/count);
            fprintf(stderr,"[POSE-VALIDATE] vertices_checked=%.1f repeated_checks_avoided=%.1f (counts/present)\n",(double)validated_vertices/count,(double)reused_validations/count);
            validated_vertices=reused_validations=0;
            fprintf(stderr,"[POSE-BLOCKS] rejected_blocks=%.1f (counts/present; order and 128-visit cap retained)\n",(double)blocks_pruned/count);
            fprintf(stderr,"[POSE-CAP] v32 avoided_searches=%.1f (per present; >128-entry chains retain original fallback; ambiguity diagnostic excludes discarded searches)\n",(double)cap_avoided/count);
            cap_avoided=0;
            fprintf(stderr,"[POSE-REUSE] sample_groups=%.1f position_hits=%.1f position_misses=%.1f position_bypass=%.1f (per present; exact samples, original search order/cap)\n",(double)sample_groups/count,(double)position_hits/count,(double)position_misses/count,(double)position_bypass/count);
            sample_groups=position_hits=position_misses=position_bypass=0;
            blocks_pruned=0;camera_hits=camera_misses=camera_uncached=search_candidates=search_pruned=0;
            memset(totals,0,sizeof totals);count=0;reported=now;
        }
    }
    static u32 reports=0,presents=0,subframes=0;static double reported_at=0;
    presents++;if(moved||changed_cameras)subframes++;
    if(reports++<8||now-reported_at>=5.0) {
        fprintf(stderr,"[POSE] v25 scene=%08X eligible=%u matched=%u smooth=%u apply=%d vertices=%u cameras=%u conflicts=%u ambiguous=%u capped=%u coherent=%u coherence_conflicts=%u seams=%u seam_conflicts=%u seam_components=%u seam_veto_vertices=%u shadow_draws=%u alpha=%.3f reason=%d subframes=%u/%u\n",
            frame.scene,nd,s_pose_history.matched,s_pose_history.moving,apply,moved,changed_cameras,conflicts,s_pose_history.ambiguous,s_pose_history.capped,s_pose_history.coherent,s_pose_history.coherence_conflicts,s_pose_history.seams,s_pose_history.seam_conflicts,s_pose_history.seam_components,s_pose_history.seam_veto_vertices,s_pose_shadow_draws,(double)s_pose_history.alpha,s_pose_history.reason,subframes,presents);
        reported_at=now;presents=subframes=0;
    }
}
static void pose_interpolate_batch(void) {pose_interpolate_batch_for(&s_rb);}

/* Window title written by the render thread, applied by the window thread. */
static SRWLOCK s_title_lock = SRWLOCK_INIT;
static char    s_title_pending[256];
static int     s_title_dirty = 0;
char g_rsx_title_base[128] = "ps3recomp";
static u32 s_dbg_last_draws = 0;
static int s_rtnew_dump = 0;      /* CV_SMALLFRAME: frames to dump after an RT was (re)created */
static int s_gameplay_seen = 0;   /* a 3D frame (>1000 records) went by: CV_SMALLFRAME */
static u64 s_req_verts = 0, s_req_draws = 0, s_drop_draws = 0;
/* Raw bind offset of the duck's texture, identified by CONTENT (duck.tga is
 * overwhelmingly yellow, avg 243,191,23). VRAM offsets move between runs, so a
 * hard-coded offset in a filter silently matches nothing and the run looks like
 * "renders nothing" -- which cost real time. DRAW_KEEP_TEX=duck resolves here. */
/* Copy of the last completed frame, bound wherever a draw samples a
 * DISPLAY-SIZED texture. On RSX this title renders its reflection into a corner
 * of the render surface and then samples that surface as a texture; our backend
 * renders into a D3D backbuffer, so the guest memory behind that sampler is
 * never written and it reads empty -- which is why the water had no reflection.
 * Feeding it the previous frame costs one frame of latency, which for a water
 * reflection is not visible. */
static ID3D12Resource* s_screen_copy = NULL;
/* Sub-viewport render-to-texture. This title renders its reflection/refraction
 * pre-pass into a REGION of the same surface (vp 0,208 512x512 and 0,208
 * 1024x512, cmask=F) and then samples a texture of exactly that size. On
 * hardware the pass lands in the buffer that texture points at; our backend
 * renders into D3D resources, so that guest buffer is never written and every
 * fluid sampler reads zero. Capture each region into a matching resource and
 * bind it for samplers whose guest source is empty and whose size matches.
 *
 * SUBVP_RTT=1 to enable. OFF by default because it cannot be validated on this
 * title yet: the fluid draws that would consume these captures never rasterize
 * (see below), so binding them changes nothing observable. The capture and
 * binding themselves are confirmed working -- SUBVP_DBG shows the 512x512 and
 * 256x256 regions captured and handed to fp 0x5DB81/0x5EA81/0x5EF81/0x5E381. */
#define SUBVP_SLOTS 4
static struct { ID3D12Resource* res; u32 x, y, w, h; } s_subvp[SUBVP_SLOTS];
static int s_subvp_n = 0;

static void subvp_note(u32 x, u32 y, u32 w, u32 h)
{
    if (!w || !h || w > s_d3d.width || h > s_d3d.height) return;
    if (w == s_d3d.width && h == s_d3d.height) return;      /* full-screen pass */
    for (int i = 0; i < s_subvp_n; i++)
        if (s_subvp[i].w == w && s_subvp[i].h == h) { s_subvp[i].x = x; s_subvp[i].y = y; return; }
    if (s_subvp_n >= SUBVP_SLOTS) return;
    s_subvp[s_subvp_n].x = x; s_subvp[s_subvp_n].y = y;
    s_subvp[s_subvp_n].w = w; s_subvp[s_subvp_n].h = h;
    s_subvp[s_subvp_n].res = NULL;
    s_subvp_n++;
}

/* Copy each noted region out of the backbuffer. Called at the same moment as
 * screen_copy_capture -- after the reduced-viewport passes and before the
 * full-screen scene overwrites them. */
static void subvp_capture(u32 fi)
{
    static int en = -1;
    if (en < 0) { const char* e = getenv("SUBVP_RTT"); en = e ? atoi(e) : 0; }
    if (!en || !s_d3d.device || !s_subvp_n) return;
    for (int i = 0; i < s_subvp_n; i++) {
        if (!s_subvp[i].res) {
            D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td = {0};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = s_subvp[i].w; td.Height = s_subvp[i].h;
            td.DepthOrArraySize = 1; td.MipLevels = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL,
                    &IID_ID3D12Resource, (void**)&s_subvp[i].res)))
                { s_subvp[i].res = NULL; continue; }
        }
        D3D12_RESOURCE_BARRIER bb[2] = {0};
        bb[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bb[0].Transition.pResource   = s_d3d.render_targets[fi];
        bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        bb[1] = bb[0];
        bb[1].Transition.pResource   = s_subvp[i].res;
        bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);

        D3D12_TEXTURE_COPY_LOCATION dstl = {0}, srcl = {0};
        dstl.pResource = s_subvp[i].res;
        dstl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dstl.SubresourceIndex = 0;
        srcl.pResource = s_d3d.render_targets[fi];
        srcl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; srcl.SubresourceIndex = 0;
        D3D12_BOX box; box.left = s_subvp[i].x; box.top = s_subvp[i].y;
        box.front = 0; box.right = s_subvp[i].x + s_subvp[i].w;
        box.bottom = s_subvp[i].y + s_subvp[i].h; box.back = 1;
        if (box.right > s_d3d.width)  box.right  = s_d3d.width;
        if (box.bottom > s_d3d.height) box.bottom = s_d3d.height;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dstl, 0, 0, 0, &srcl, &box);

        bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);
        { static int _n = 0; if (getenv("SUBVP_DBG") && _n++ < 8)
            fprintf(stderr, "[SUBVP] captured %ux%u from (%u,%u)%c",
                    s_subvp[i].w, s_subvp[i].h, s_subvp[i].x, s_subvp[i].y, 10); }
    }
}
static void screen_copy_capture(u32 fi);   /* fwd */
static int  s_sc_dump_pending = 0;
static u32 s_duck_raw = 0;   /* as BOUND (what draw records carry) */
static u32 s_duck_off = 0;   /* as RESOLVED (what the uploader sees)   */
/* PERF=1: where a frame's CPU time actually goes. Guessing at this is how you
 * spend an afternoon optimising the wrong loop. */
static double s_perf_tex = 0.0, s_perf_frame = 0.0, s_perf_vtx = 0.0, s_perf_rf = 0.0;
static double s_perf_gpu = 0.0, s_perf_pre = 0.0, s_perf_srv = 0.0, s_perf_pso = 0.0;
/* vp_record_cb: vertex-constant banks written vs reused, and the recording
 * batch they belong to (bumped when render_frame hands a batch over). */
static unsigned s_vcb_copied = 0, s_vcb_reused = 0, s_vcb_batch = 0;
static unsigned long long s_vmemo_hits = 0, s_vmemo_miss = 0;   /* read_vp_vertex */
static int s_perf_pso_calls = 0, s_perf_pso_miss = 0, s_perf_pso_hashbytes = 0;
static u64    s_perf_nverts = 0;
static u64    s_perf_texbytes = 0;
static int    s_perf_ntex = 0;
static double perf_now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static int perf_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("PERF"); v = e ? atoi(e) : 0; }
    return v;
}
/* [PERF-THREADS]: CPU each host thread burned since the last report, busiest
 * first, by the names ps3_host_thread_name() gave them (ppu:<guest name>,
 * spu:..., spurs:...). "guest 64%" says the game side is the bottleneck;
 * this says which thread. */
#include <tlhelp32.h>
static void perf_thread_report(double wall)
{
    typedef HRESULT (WINAPI *get_desc_fn)(HANDLE, PWSTR*);
    static get_desc_fn gd = NULL; static int looked = 0;
    if (!looked) { HMODULE k = GetModuleHandleA("kernel32.dll");
                   gd = k ? (get_desc_fn)(void*)GetProcAddress(k, "GetThreadDescription") : NULL;
                   looked = 1; }
    enum { MAXT = 256 };
    static DWORD prev_tid[MAXT]; static unsigned long long prev_t[MAXT]; static int nprev = 0;
    DWORD tid[MAXT]; unsigned long long tot[MAXT]; double dt[MAXT]; char name[MAXT][48];
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te; te.dwSize = sizeof te;
    DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok && n < MAXT; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetThreadTimes(h, &c, &e, &k, &u)) {
            unsigned long long t = ((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime)
                                 + ((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime);
            unsigned long long p = 0;
            for (int i = 0; i < nprev; i++) if (prev_tid[i] == te.th32ThreadID) { p = prev_t[i]; break; }
            tid[n] = te.th32ThreadID; tot[n] = t; dt[n] = (double)(t - p) / 1e7;
            snprintf(name[n], sizeof name[n], "tid%lu", (unsigned long)te.th32ThreadID);
            PWSTR w = NULL;
            if (gd && SUCCEEDED(gd(h, &w)) && w) {
                if (w[0]) { int i = 0; for (; w[i] && i < 47; i++) name[n][i] = (char)w[i]; name[n][i] = 0; }
                LocalFree(w);
            }
            n++;
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    for (int i = 0; i < n; i++) { prev_tid[i] = tid[i]; prev_t[i] = tot[i]; }
    int first = (nprev == 0); nprev = n;
    if (first || wall <= 0.0) return;
    fprintf(stderr, "[PERF-THREADS] %.1fs wall, %% of one core:", wall);
    for (int shown = 0; shown < 10; shown++) {
        int b = -1;
        for (int i = 0; i < n; i++) if (dt[i] >= 0 && (b < 0 || dt[i] > dt[b])) b = i;
        if (b < 0 || dt[b] < 0.02 * wall) break;
        fprintf(stderr, " | %s %.0f%%", name[b], 100.0 * dt[b] / wall);
        dt[b] = -1;
    }
    fprintf(stderr, "%c", 10);
}
/* DBG_LOCK: running clip-space centroid of the tracked mesh (RSX_DBG_VTX's
 * texture), so the debug camera can follow it. The ducks are driven by the
 * physics sim and drift every frame, so a fixed DBG_CENTER loses them as soon
 * as the magnification is high enough to make one recognisable. */
static float s_lock_x = 0.0f, s_lock_y = 0.0f;
static int   s_lock_valid = 0;
/* Accumulated over one frame, published at the frame boundary. Averaging as the
 * frame is recorded makes the aim wander while it is being used, which at high
 * magnification walks the target off-screen entirely. */
static double s_lock_sx = 0.0, s_lock_sy = 0.0;
static u32    s_lock_n  = 0;

/* ---------------------------------------------------------------------------
 * Win32 window
 * -----------------------------------------------------------------------*/

/* WINDOW_MODE: 0 "window" (default) a framed window, 1 "borderless" a
 * frameless window covering the monitor, 2 "fullscreen" the same kept on top
 * with the mouse cursor hidden. All three are ordinary windows (never DXGI
 * exclusive fullscreen), so capture tools see them. The swapchain keeps the
 * render size and DXGI stretches it to the window, so the window is sized
 * independently: a framed window is at least the 1280x720 guest frame (480p
 * rendering still shows at 720p) and shrinks to fit the work area, keeping
 * 16:9; a monitor that is not 16:9 gets the largest 16:9 rect, centred, over
 * a black backdrop that cannot take focus. The mode can change while the game
 * runs (rsx_d3d12_set_window_mode, Alt+Enter); the switch is applied on the
 * window's own thread. */
#define WM_RSX_SET_MODE (WM_APP + 0x51)
/* Opt-in Code Veronica shadow isolation. F10 changes only this process's
 * shadow visibility term; geometry, depth, light colour and guest data stay.
 * Reserved b1 metadata lane is used only by these diagnostic shaders. */
static int s_lighter_probe;
static volatile LONG s_lighter_shadow_bypass;
static int lighter_shadow_code(u32 code) {
    return code == 0x0C121A16u || code == 0x59540949u ||
           code == 0x4D26314Du;
}
static int lighter_probe_key(WPARAM wp, LPARAM lp) {
    if (!s_lighter_probe || wp != VK_F10) return 0;
    if (!(lp & ((LPARAM)1 << 30))) {
        LONG mode = InterlockedXor(&s_lighter_shadow_bypass, 1) ^ 1;
        fprintf(stderr, "[LIGHTER] F10: shadow visibility %s (light remains on)\n",
                mode ? "BYPASSED" : "NORMAL");
    }
    return 1;
}
static int  s_hide_cursor;
static int  s_wmode;
static HWND s_backdrop;
static u32  s_win_w = 1280, s_win_h = 720;   /* render size, for the framed window */
void (*g_rsx_menu_hook)(void);               /* Insert: the title's options menu */
void (*g_rsx_key_hook)(unsigned vk);         /* any other key pressed (not repeats): the title's hotkeys */
static void mc_request(void);
static volatile LONG s_shot_request;   /* F10 or a controller chord, taken by shot_record() */
void rsx_d3d12_request_screenshot(void) { InterlockedExchange(&s_shot_request, 1); }

static void window_apply_mode(HWND hwnd, int mode)
{
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi; mi.cbSize = sizeof mi; GetMonitorInfoA(mon, &mi);
    const RECT mr = mi.rcMonitor, wa = mi.rcWork;
    s_wmode = mode;
    s_hide_cursor = (mode == 2);
    if (mode != 0) {
        const LONG mw = mr.right - mr.left, mh = mr.bottom - mr.top;
        LONG w = mw, h = mw * 9 / 16;
        if (h > mh) { h = mh; w = mh * 16 / 9; }
        const HWND z = (mode == 2) ? HWND_TOPMOST : HWND_NOTOPMOST;
        if (w != mw || h != mh) {
            if (!s_backdrop) {
                WNDCLASSEXA bc = {0};
                bc.cbSize = sizeof bc; bc.lpfnWndProc = DefWindowProcA; bc.hInstance = GetModuleHandle(NULL);
                bc.hCursor = LoadCursor(NULL, IDC_ARROW); bc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
                bc.lpszClassName = "ps3recomp_backdrop";
                RegisterClassExA(&bc);
                s_backdrop = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, "ps3recomp_backdrop", "",
                                             WS_POPUP, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
            }
            SetWindowPos(s_backdrop, z, mr.left, mr.top, mw, mh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, (LONG_PTR)s_backdrop);
        } else if (s_backdrop) {
            SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, 0);
            DestroyWindow(s_backdrop); s_backdrop = NULL;
        }
        SetWindowLongPtrA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, z, mr.left + (mw - w) / 2, mr.top + (mh - h) / 2, w, h,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return;
    }
    if (s_backdrop) {
        SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, 0);
        DestroyWindow(s_backdrop); s_backdrop = NULL;
    }
    LONG cw = (LONG)s_win_w, ch = (LONG)s_win_h;
    if (cw < 1280 || ch < 720) { cw = 1280; ch = 720; }
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    for (;;) {   /* fit the frame inside the work area */
        RECT t = {0, 0, cw, ch};
        AdjustWindowRect(&t, style & ~WS_VISIBLE, FALSE);
        if ((t.right - t.left) <= (wa.right - wa.left) && (t.bottom - t.top) <= (wa.bottom - wa.top)) break;
        cw = cw * 9 / 10; ch = ch * 9 / 10;
        if (cw < 320) break;
    }
    RECT wr = {0, 0, cw, ch};
    AdjustWindowRect(&wr, style & ~WS_VISIBLE, FALSE);
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    SetWindowLongPtrA(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, HWND_NOTOPMOST, wa.left + ((wa.right - wa.left) - ww) / 2,
                 wa.top + ((wa.bottom - wa.top) - wh) / 2, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK d3d12_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_RSX_SET_MODE:
        window_apply_mode(hwnd, (int)wp);
        return 0;
    case WM_SETCURSOR:
        if (s_hide_cursor && LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;
    case WM_CLOSE:
        s_d3d.window_closed = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (s_backdrop) { DestroyWindow(s_backdrop); s_backdrop = NULL; }
        PostQuitMessage(0);
        return 0;
    case WM_SYSKEYUP:
        if (wp == VK_F10) return 0;   /* the screenshot key: never the system menu on release */
        break;
    case WM_SYSKEYDOWN:
        /* Windows delivers F10 as a system key, even without Alt. */
        if (lighter_probe_key(wp, lp)) return 0;
        if (wp == VK_F10) {   /* F10: screenshot; never the system menu */
            if (!(lp & (1 << 30))) InterlockedExchange(&s_shot_request, 1);
            return 0;
        }
        if (wp == VK_RETURN && !(lp & (1 << 30))) {   /* Alt+Enter: window <-> fullscreen */
            window_apply_mode(hwnd, s_wmode == 0 ? 2 : 0);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if(wp==VK_F9&&!(lp&(1<<30))){mc_request();return 0;}
        if (lighter_probe_key(wp, lp)) return 0;
        /* Esc no longer quits: the window's X or Alt+F4 does. */
        if (wp == VK_INSERT && !(lp & (1 << 30)) && g_rsx_menu_hook) g_rsx_menu_hook();
        else if (!(lp & (1 << 30)) && g_rsx_key_hook) g_rsx_key_hook((unsigned)wp);

        if (wp == VK_F12 && !(lp & (1 << 30))) {   /* F12: rsxcap the next frame */
            extern volatile int g_rsx_cap_request;
            g_rsx_cap_request = 1;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND create_window(u32 width, u32 height, const char* title)
{
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = d3d12_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));   /* the title's icon, if it has one */
    wc.lpszClassName = "ps3recomp_d3d12";
    RegisterClassExA(&wc);

    /* Keep the display awake (see rsx_null_backend.c). */
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

    const char* wm = getenv("WINDOW_MODE");
    int mode = 0;
    if (wm && !_stricmp(wm, "borderless")) mode = 1;
    else if (wm && !_stricmp(wm, "fullscreen")) mode = 2;
    s_win_w = width; s_win_h = height;
    HWND hwnd = CreateWindowExA(0, "ps3recomp_d3d12", title ? title : "ps3recomp (D3D12)",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
                                NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hwnd) window_apply_mode(hwnd, mode);
    return hwnd;
}

/* Live options for a title's launcher; safe from any thread. */
void rsx_d3d12_set_window_mode(int mode)
{
    if (s_d3d.hwnd) PostMessageA(s_d3d.hwnd, WM_RSX_SET_MODE, (WPARAM)mode, 0);
}
void rsx_d3d12_close(void)
{
    if (s_d3d.hwnd) PostMessageA(s_d3d.hwnd, WM_CLOSE, 0, 0);
}
void* rsx_d3d12_window(void) { return s_d3d.hwnd; }
/* ---------------------------------------------------------------------------
 * D3D12 initialization
 * -----------------------------------------------------------------------*/

static int init_d3d12(u32 width, u32 height)
{
    HRESULT hr;

    /* Debug layer: on in _DEBUG (CRT debug) builds, or in any build when
     * D3D12_DBG is set (so a Release run can capture exact PSO/validation
     * errors). Keyed on _DEBUG, not NDEBUG: the optimized build replaces
     * CMAKE_C_FLAGS_RELEASE without /DNDEBUG, which left the layer validating
     * every call of every "release" run (~14% of the render thread). */
    if (
#ifdef _DEBUG
        1
#else
        getenv("D3D12_DBG")
#endif
    ) {
        ID3D12Debug* debug_controller = NULL;
        hr = D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug_controller);
        if (SUCCEEDED(hr) && debug_controller) {
            debug_controller->lpVtbl->EnableDebugLayer(debug_controller);
            debug_controller->lpVtbl->Release(debug_controller);
            printf("[D3D12] Debug layer enabled\n");
        } else {
            printf("[D3D12] Debug layer requested but unavailable (0x%08lX) -- "
                   "install 'Graphics Tools' optional feature\n", hr);
        }
    }

    /* Create DXGI factory */
    IDXGIFactory4* factory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&factory);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateDXGIFactory1 failed (0x%08lX)\n", hr);
        return -1;
    }

    /* DRED: when the GPU is lost (TDR), record which command-list operation
     * was running and which address faulted, so dred_report() can name the
     * culprit in an ordinary log. Must be set before the device exists.
     * CV_DRED=0 turns it off. */
    {
        const char* e = getenv("CV_DRED");
        if (!e || *e != '0') {
            ID3D12DeviceRemovedExtendedDataSettings* ds = NULL;
            if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12DeviceRemovedExtendedDataSettings,
                                                 (void**)&ds)) && ds) {
                ds->lpVtbl->SetAutoBreadcrumbsEnablement(ds, D3D12_DRED_ENABLEMENT_FORCED_ON);
                ds->lpVtbl->SetPageFaultEnablement(ds, D3D12_DRED_ENABLEMENT_FORCED_ON);
                ds->lpVtbl->Release(ds);
            }
        }
    }

    /* Create D3D12 device. On a dual-GPU laptop a NULL adapter usually lands on
     * the integrated GPU; explicitly pick the high-performance (discrete)
     * adapter via IDXGIFactory6. Set CELLMARK_IGPU to force the low-power one
     * (for A/B testing a suspected iGPU-driver stall). */
    {
        IDXGIFactory6* factory6 = NULL;
        if (SUCCEEDED(factory->lpVtbl->QueryInterface(
                factory, &IID_IDXGIFactory6, (void**)&factory6))) {
            DXGI_GPU_PREFERENCE pref = getenv("CELLMARK_IGPU")
                ? DXGI_GPU_PREFERENCE_MINIMUM_POWER
                : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
            IDXGIAdapter1* adapter = NULL;
            for (UINT ai = 0; factory6->lpVtbl->EnumAdapterByGpuPreference(
                     factory6, ai, pref, &IID_IDXGIAdapter1, (void**)&adapter)
                     != DXGI_ERROR_NOT_FOUND; ai++) {
                DXGI_ADAPTER_DESC1 ad;
                adapter->lpVtbl->GetDesc1(adapter, &ad);
                if (!(ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,
                        D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
                        (void**)&s_d3d.device))) {
                    printf("[D3D12] adapter: %ls (VRAM %llu MB)\n", ad.Description,
                           (unsigned long long)(ad.DedicatedVideoMemory >> 20));
                    adapter->lpVtbl->Release(adapter);
                    break;
                }
                adapter->lpVtbl->Release(adapter);
                adapter = NULL;
            }
            factory6->lpVtbl->Release(factory6);
        }
    }
    if (!s_d3d.device) {
        hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                               &IID_ID3D12Device, (void**)&s_d3d.device);
        if (FAILED(hr)) {
            printf("[D3D12] ERROR: D3D12CreateDevice failed (0x%08lX)\n", hr);
            factory->lpVtbl->Release(factory);
            return -1;
        }
        printf("[D3D12] Device created on default adapter (feature level 11.0)\n");
    }

    /* Create command queue */
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = s_d3d.device->lpVtbl->CreateCommandQueue(
        s_d3d.device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&s_d3d.cmd_queue);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateCommandQueue failed\n");
        factory->lpVtbl->Release(factory);
        return -1;
    }

    /* Create swap chain */
    DXGI_SWAP_CHAIN_DESC1 sc_desc = {0};
    sc_desc.Width = rs_dim(width, s_rs);
    sc_desc.Height = rs_dim(height, s_rs);
    { const char* e = getenv("DISPLAY_SCALE");
      s_display = !(e && *e == '0'); }
    if (s_display) {
        u32 cw = sc_desc.Width, ch = sc_desc.Height;
        window_client(&cw, &ch);
        sc_desc.Width = cw; sc_desc.Height = ch;
        s_disp_w = cw; s_disp_h = ch;
    }
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = FRAME_COUNT;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    /* VSYNC=1 waits for the display's refresh on every present. Off by
     * default, like RPCS3: the title is already paced by our own 60 Hz
     * vblank clock, and a second clock -- the monitor's, never exactly in
     * step with ours -- quantised frames to its refresh, so every so often
     * one 33 ms frame showed as ~17 ms followed by ~50 ms: a frametime line
     * that jumped everywhere, even in menus holding 30 fps. Without vsync the
     * frame goes out as soon as it is rendered (tearing allowed if the
     * system supports it). */
    { const char* e = getenv("VSYNC");
      s_vsync = (e && *e && *e != '0') ? 1 : 0; }
    if (!s_vsync) {
        IDXGIFactory5* f5 = NULL; BOOL tear = FALSE;
        if (SUCCEEDED(factory->lpVtbl->QueryInterface(factory, &IID_IDXGIFactory5, (void**)&f5))) {
            if (FAILED(f5->lpVtbl->CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                       &tear, sizeof tear))) tear = FALSE;
            f5->lpVtbl->Release(f5);
        }
        s_tearing = tear ? 1 : 0;
        if (s_tearing) sc_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    printf("[D3D12] present: vsync %s%s%c", s_vsync ? "ON (VSYNC=1)" : "OFF",
           (!s_vsync && s_tearing) ? ", tearing allowed" : "", 10);

    IDXGISwapChain1* swap_chain1 = NULL;
    if(rsx_fsr31_swapchain(s_d3d.device,s_d3d.cmd_queue,factory,s_d3d.hwnd,&sc_desc,(void**)&swap_chain1))hr=S_OK;
    else hr = factory->lpVtbl->CreateSwapChainForHwnd(
        factory, (IUnknown*)s_d3d.cmd_queue,
        s_d3d.hwnd, &sc_desc, NULL, NULL, &swap_chain1);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateSwapChainForHwnd failed (0x%08lX)\n", hr);
        factory->lpVtbl->Release(factory);
        return -1;
    }

    /* Disable Alt+Enter fullscreen toggle */
    factory->lpVtbl->MakeWindowAssociation(factory, s_d3d.hwnd, DXGI_MWA_NO_ALT_ENTER);
    factory->lpVtbl->Release(factory);

    /* Query SwapChain3 interface */
    hr = swap_chain1->lpVtbl->QueryInterface(
        swap_chain1, &IID_IDXGISwapChain3, (void**)&s_d3d.swap_chain);
    swap_chain1->lpVtbl->Release(swap_chain1);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: QueryInterface for SwapChain3 failed\n");
        return -1;
    }

    s_d3d.frame_index = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);

    /* Create RTV descriptor heap */
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {0};
    rtv_heap_desc.NumDescriptors = FRAME_COUNT;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
        s_d3d.device, &rtv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.rtv_heap);
    if (FAILED(hr)) return -1;

    s_d3d.rtv_descriptor_size = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    /* Create RTVs for each frame */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv_handle);

    if (s_display) {
        D3D12_DESCRIPTOR_HEAP_DESC dh = {0};
        dh.NumDescriptors = FRAME_COUNT; dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device, &dh, &IID_ID3D12DescriptorHeap,
                                                             (void**)&s_disp_rtv))
            || !display_bufs_get() || !game_rts_create()) return -1;
        printf("[D3D12] display %ux%u, game rendered at %ux%u and scaled into it\n",
               s_disp_w, s_disp_h, rs_dim(width, s_rs), rs_dim(height, s_rs));
    } else
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        hr = s_d3d.swap_chain->lpVtbl->GetBuffer(
            s_d3d.swap_chain, i, &IID_ID3D12Resource, (void**)&s_d3d.render_targets[i]);
        if (FAILED(hr)) return -1;

        s_d3d.device->lpVtbl->CreateRenderTargetView(
            s_d3d.device, s_d3d.render_targets[i], NULL, rtv_handle);
        rtv_handle.ptr += s_d3d.rtv_descriptor_size;
    }

    /* ---------------------------------------------------------------
     * Depth/stencil buffer
     * 24-bit depth + 8-bit stencil (DXGI_FORMAT_D24_UNORM_S8_UINT).
     * One shared depth texture across both frames â€” RSX games on PS3
     * typically use a single zeta surface.
     * ---------------------------------------------------------------*/
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {0};
        dsv_heap_desc.NumDescriptors = 1;
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
            s_d3d.device, &dsv_heap_desc, &IID_ID3D12DescriptorHeap,
            (void**)&s_d3d.dsv_heap);
        if (FAILED(hr)) {
            printf("[D3D12] DSV heap creation failed\n");
            return -1;
        }

        D3D12_HEAP_PROPERTIES heap_props = {0};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depth_desc = {0};
        depth_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        /* Sized to cover the LARGEST render target, not just the window:
         * D3D12 clips rasterization to the smallest bound attachment, and
         * this DSV is shared by every pass -- with a window-sized depth
         * buffer, draws into larger offscreen RTs (wave's 1920x1080 input
         * image) were silently cropped to the window rect. */
        depth_desc.Width              = rs_dim(2048, s_rs);
        depth_desc.Height             = rs_dim(2048, s_rs);
        depth_desc.DepthOrArraySize   = 1;
        depth_desc.MipLevels          = 1;
        /* Typeless so the same memory can also be read as R24 by the
         * zeta-as-texture conversion (titles sample their depth surface as an
         * A8R8G8B8 texture for fog and depth of field). */
        depth_desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
        depth_desc.SampleDesc.Count   = 1;
        depth_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depth_clear = {0};
        depth_clear.Format                       = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_clear.DepthStencil.Depth           = 1.0f;
        depth_clear.DepthStencil.Stencil         = 0;

        hr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &heap_props, D3D12_HEAP_FLAG_NONE,
            &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear,
            &IID_ID3D12Resource, (void**)&s_d3d.depth_buffer);
        if (FAILED(hr)) {
            printf("[D3D12] Depth buffer creation failed (0x%08lX)\n", hr);
            return -1;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {0};
        dsv_desc.Format         = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv_desc.ViewDimension  = D3D12_DSV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
        s_d3d.dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.dsv_heap, &dsv_handle);
        s_d3d.device->lpVtbl->CreateDepthStencilView(
            s_d3d.device, s_d3d.depth_buffer, &dsv_desc, dsv_handle);

        printf("[D3D12] Depth buffer created (%ux%u D24S8)\n", width, height);
    }

    /* Create command allocators and command list */
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        hr = s_d3d.device->lpVtbl->CreateCommandAllocator(
            s_d3d.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void**)&s_d3d.cmd_allocators[i]);
        if (FAILED(hr)) return -1;
    }

    hr = s_d3d.device->lpVtbl->CreateCommandList(
        s_d3d.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        s_d3d.cmd_allocators[0], NULL,
        &IID_ID3D12GraphicsCommandList, (void**)&s_d3d.cmd_list);
    if (FAILED(hr)) return -1;

    gc_install(s_d3d.cmd_list);   /* PS3_TIMELINE: [GPUCMD] counts */
    /* Close the command list (it starts in recording state) */
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);

    /* Create fence */
    hr = s_d3d.device->lpVtbl->CreateFence(
        s_d3d.device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void**)&s_d3d.fence);
    if (FAILED(hr)) return -1;

    s_d3d.fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    memset(s_d3d.fence_values, 0, sizeof(s_d3d.fence_values));

    /* ---------------------------------------------------------------
     * Create root signature with 16 root constants (one mat4 MVP at b0).
     * Visible only to the vertex shader â€” pixel shader doesn't need it.
     * ---------------------------------------------------------------*/
    {
        /* param 0: mat4 MVP as 16 root constants at b0 (vertex shader).
         * param 1: one SRV (t0) descriptor table for the atlas texture (pixel).
         * static sampler s0: linear clamp. The plain (untextured) PSO simply
         * never references t0/s0, so it ignores them. */
        D3D12_DESCRIPTOR_RANGE srv_range = {0};
        srv_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors     = 1;
        srv_range.BaseShaderRegister = 0;   /* t0 */
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER root_params[2] = {0};
        root_params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[0].Constants.ShaderRegister = 0;   /* b0 */
        root_params[0].Constants.RegisterSpace  = 0;
        root_params[0].Constants.Num32BitValues = 16;  /* mat4 */
        root_params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        root_params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[1].DescriptorTable.NumDescriptorRanges = 1;
        root_params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        root_params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samp = {0};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderRegister   = 0;   /* s0 */
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs_desc = {0};
        rs_desc.NumParameters     = 2;
        rs_desc.pParameters       = root_params;
        rs_desc.NumStaticSamplers = 1;
        rs_desc.pStaticSamplers   = &samp;
        rs_desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                              | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                              | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                              | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ID3DBlob* signature_blob = NULL;
        ID3DBlob* error_blob = NULL;
        hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                          &signature_blob, &error_blob);
        if (FAILED(hr)) {
            printf("[D3D12] Root signature serialization failed: %s\n",
                   error_blob ? (const char*)error_blob->lpVtbl->GetBufferPointer(error_blob) : "?");
            if (error_blob) error_blob->lpVtbl->Release(error_blob);
            return -1;
        }

        hr = s_d3d.device->lpVtbl->CreateRootSignature(
            s_d3d.device, 0,
            signature_blob->lpVtbl->GetBufferPointer(signature_blob),
            signature_blob->lpVtbl->GetBufferSize(signature_blob),
            &IID_ID3D12RootSignature, (void**)&s_d3d.root_signature);
        signature_blob->lpVtbl->Release(signature_blob);
        if (FAILED(hr)) {
            printf("[D3D12] Root signature creation failed\n");
            return -1;
        }
    }

    /* ---------------------------------------------------------------
     * Compile shaders and create PSO
     * ---------------------------------------------------------------*/
    {
        /* Basic vertex-colored shader.
         * The MVP matrix arrives via root constants as 4 vec4 columns
         * (PS3/OpenGL column-major convention). We multiply explicitly so
         * we don't depend on HLSL's matrix packing â€” matches PS3 semantics
         * `gl_Position = MVP * vec4(pos, 1.0)`. */
        static const char vs_hlsl[] =
            "cbuffer cb0 : register(b0) {\n"
            "    float4 mvp_col0;\n"
            "    float4 mvp_col1;\n"
            "    float4 mvp_col2;\n"
            "    float4 mvp_col3;\n"
            "};\n"
            "struct VSInput  { float3 pos : POSITION; float4 col : COLOR; };\n"
            "struct VSOutput { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
            "VSOutput main(VSInput i) {\n"
            "    VSOutput o;\n"
            "    float4 p = float4(i.pos, 1.0);\n"
            "    o.pos = mvp_col0 * p.x + mvp_col1 * p.y + mvp_col2 * p.z + mvp_col3 * p.w;\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n";
        static const char ps_hlsl[] =
            "struct PSInput { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
            "float4 main(PSInput i) : SV_TARGET { return i.col; }\n";

        ID3DBlob* vs_blob = NULL;
        ID3DBlob* ps_blob = NULL;
        ID3DBlob* err = NULL;

        hr = D3DCompile(vs_hlsl, sizeof(vs_hlsl) - 1, "vs_basic", NULL, NULL,
                        "main", "vs_5_0", 0, 0, &vs_blob, &err);
        if (FAILED(hr)) {
            printf("[D3D12] VS compile failed: %s\n",
                   err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "unknown");
            if (err) err->lpVtbl->Release(err);
        }

        hr = D3DCompile(ps_hlsl, sizeof(ps_hlsl) - 1, "ps_basic", NULL, NULL,
                        "main", "ps_5_0", 0, 0, &ps_blob, &err);
        if (FAILED(hr)) {
            printf("[D3D12] PS compile failed: %s\n",
                   err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "unknown");
            if (err) err->lpVtbl->Release(err);
        }

        if (vs_blob && ps_blob) {
            D3D12_INPUT_ELEMENT_DESC input_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
            pso_desc.pRootSignature = s_d3d.root_signature;
            pso_desc.VS.pShaderBytecode = vs_blob->lpVtbl->GetBufferPointer(vs_blob);
            pso_desc.VS.BytecodeLength = vs_blob->lpVtbl->GetBufferSize(vs_blob);
            pso_desc.PS.pShaderBytecode = ps_blob->lpVtbl->GetBufferPointer(ps_blob);
            pso_desc.PS.BytecodeLength = ps_blob->lpVtbl->GetBufferSize(ps_blob);
            pso_desc.InputLayout.pInputElementDescs = input_layout;
            pso_desc.InputLayout.NumElements = 3;
            pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
                D3D12_COLOR_WRITE_ENABLE_ALL;
            pso_desc.SampleMask = UINT_MAX;
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso_desc.NumRenderTargets = 1;
            pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            /* Depth test enabled, write enabled, LESS func.
             * Games with depth_test_enable=0 in RSX state can still render â€”
             * LESS just means new-z must be less than existing â€” but future
             * work should mirror RSX depth state into a PSO cache. */
            pso_desc.DepthStencilState.DepthEnable    = TRUE;
            pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            pso_desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            pso_desc.DepthStencilState.StencilEnable  = FALSE;
            pso_desc.SampleDesc.Count = 1;

            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc,
                &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state);
            if (SUCCEEDED(hr)) {
                s_d3d.pipeline_ready = 1;
                printf("[D3D12] Pipeline state created (triangle class)\n");
            } else {
                printf("[D3D12] PSO TRIANGLE creation failed (0x%08lX)\n", hr);
            }

            /* Line-class PSO â€” same shader, LINE topology type. */
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc,
                &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_lines);
            if (SUCCEEDED(hr)) printf("[D3D12] Pipeline state created (line class)\n");
            else printf("[D3D12] PSO LINE creation failed (0x%08lX)\n", hr);

            /* Point-class PSO. */
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc,
                &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_points);
            if (SUCCEEDED(hr)) printf("[D3D12] Pipeline state created (point class)\n");
            else printf("[D3D12] PSO POINT creation failed (0x%08lX)\n", hr);

            /* Flat UI geometry uses the same shader with alpha and no depth. */
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso_desc.DepthStencilState.DepthEnable = FALSE;
            pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc, &IID_ID3D12PipelineState,
                (void**)&s_d3d.pipeline_state_ui);
            if (FAILED(hr)) fprintf(stderr,"[ui-batch] PSO failed: 0x%08lX\n",hr);
            vs_blob->lpVtbl->Release(vs_blob);
            ps_blob->lpVtbl->Release(ps_blob);
        }

        /* -----------------------------------------------------------------
         * Textured triangle PSO (dbgfont / 2D atlas quads). Same MVP VS but
         * carrying UV; the PS samples the single-channel atlas as coverage
         * and modulates the vertex color, with straight alpha blending so
         * glyph edges composite over the framebuffer. Depth off (2D overlay).
         * ----------------------------------------------------------------*/
        {
            static const char vs_tex[] =
                "cbuffer cb0 : register(b0){float4 c0;float4 c1;float4 c2;float4 c3;};\n"
                "struct VSIn { float3 pos:POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
                "struct VSOut{ float4 pos:SV_POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
                "VSOut main(VSIn i){ VSOut o; float4 p=float4(i.pos,1.0);\n"
                "  o.pos=c0*p.x+c1*p.y+c2*p.z+c3*p.w; o.col=i.col; o.uv=i.uv; return o; }\n";
            static const char ps_tex[] =
                "Texture2D tex : register(t0);\n"
                "SamplerState smp : register(s0);\n"
                "struct PSIn{ float4 pos:SV_POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
                "float4 main(PSIn i):SV_TARGET{\n"
                /* dbgfont texcoords are compressed ~10x (U) / ~8x (V) vs. the
                 * atlas; scale to recover glyph cells. Exact per-glyph offset
                 * still being calibrated. */
                "  float2 uv2 = float2(i.uv.x*10.0, i.uv.y*8.0 + 0.59);\n"
                "  float cov = tex.Sample(smp, uv2).r;\n"
                "  return float4(i.col.rgb, i.col.a * cov); }\n";

            ID3DBlob *vtb = NULL, *ptb = NULL, *e2 = NULL;
            hr = D3DCompile(vs_tex, sizeof(vs_tex) - 1, "vs_tex", NULL, NULL, "main", "vs_5_0", 0, 0, &vtb, &e2);
            if (FAILED(hr)) printf("[D3D12] VS(tex) compile failed: %s\n", e2 ? (const char*)e2->lpVtbl->GetBufferPointer(e2) : "?");
            hr = D3DCompile(ps_tex, sizeof(ps_tex) - 1, "ps_tex", NULL, NULL, "main", "ps_5_0", 0, 0, &ptb, &e2);
            if (FAILED(hr)) printf("[D3D12] PS(tex) compile failed: %s\n", e2 ? (const char*)e2->lpVtbl->GetBufferPointer(e2) : "?");

            if (vtb && ptb) {
                D3D12_INPUT_ELEMENT_DESC il[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                };
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
                pd.pRootSignature = s_d3d.root_signature;
                pd.VS.pShaderBytecode = vtb->lpVtbl->GetBufferPointer(vtb);
                pd.VS.BytecodeLength  = vtb->lpVtbl->GetBufferSize(vtb);
                pd.PS.pShaderBytecode = ptb->lpVtbl->GetBufferPointer(ptb);
                pd.PS.BytecodeLength  = ptb->lpVtbl->GetBufferSize(ptb);
                pd.InputLayout.pInputElementDescs = il;
                pd.InputLayout.NumElements = 3;
                pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                pd.BlendState.RenderTarget[0].BlendEnable    = TRUE;
                pd.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
                pd.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
                pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                pd.SampleMask = UINT_MAX;
                pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pd.NumRenderTargets = 1;
                pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
                pd.DepthStencilState.DepthEnable = FALSE;
                pd.DepthStencilState.StencilEnable = FALSE;
                pd.SampleDesc.Count = 1;
                hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                    s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_tex);
                if (SUCCEEDED(hr)) printf("[D3D12] Pipeline state created (textured class)\n");
                else printf("[D3D12] PSO TEX creation failed (0x%08lX)\n", hr);
            }
            if (vtb) vtb->lpVtbl->Release(vtb);
            if (ptb) ptb->lpVtbl->Release(ptb);
        }

        /* SRV descriptor heap (shader-visible). Layout: slot 0 = legacy atlas
         * (dbgfont / textured 2D path), slots 1-4 = per-draw VP textures,
         * 5-20 = offscreen render targets (render-to-texture), rest spare.
         * All slots start as null SRVs so any 4-wide table window (the
         * root signature binds t0-t3) is valid on tier-1 hardware. */
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
            hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.NumDescriptors = SRV_HEAP_SIZE;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
                s_d3d.device, &hd, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.srv_heap);
            if (FAILED(hr)) printf("[D3D12] SRV heap creation failed (0x%08lX)\n", hr);
            else {
                s_d3d.srv_inc = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
                    s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                D3D12_SHADER_RESOURCE_VIEW_DESC nv = {0};
                nv.Format = DXGI_FORMAT_R8_UNORM;
                nv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                nv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                nv.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE hh;
                s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &hh);
                for (int _i = 0; _i < SRV_HEAP_SIZE; _i++) {
                    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, NULL, &nv, hh);
                    hh.ptr += s_d3d.srv_inc;
                }
            }
        }

        /* RTV heap for offscreen render targets (CPU-visible only). */
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
            hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            hd.NumDescriptors = MAX_OFF_RTS;
            hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
                s_d3d.device, &hd, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.rt_rtv_heap);
            if (FAILED(hr)) printf("[D3D12] offscreen RTV heap creation failed (0x%08lX)\n", hr);
        }
    }

    /* ---------------------------------------------------------------
     * Create dynamic vertex buffer (upload heap, 4MB)
     * ---------------------------------------------------------------*/
    {
        D3D12_HEAP_PROPERTIES heap_props = {0};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC buf_desc = {0};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = MAX_VERTICES * VERTEX_STRIDE;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &heap_props, D3D12_HEAP_FLAG_NONE,
            &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.vertex_buffer);
        if (SUCCEEDED(hr)) {
            D3D12_RANGE read_range = {0, 0};
            s_d3d.vertex_buffer->lpVtbl->Map(
                s_d3d.vertex_buffer, 0, &read_range, &s_d3d.vb_mapped);
            s_d3d.vb_view.BufferLocation =
                s_d3d.vertex_buffer->lpVtbl->GetGPUVirtualAddress(s_d3d.vertex_buffer);
            s_d3d.vb_view.StrideInBytes = VERTEX_STRIDE;
            s_d3d.vb_view.SizeInBytes = MAX_VERTICES * VERTEX_STRIDE;
            printf("[D3D12] Vertex buffer created (%u KB)\n",
                   (MAX_VERTICES * VERTEX_STRIDE) / 1024);
        }
    }

    /* ---------------------------------------------------------------
     * Real vertex-program path resources: root signature (CBV b0 for the
     * vp_c[] bank + SRV t0 + static sampler s0), a raw-float4 vertex buffer,
     * and the constant-bank buffer. The PSO itself is built lazily once the
     * game uploads its VP microcode (render_frame).
     * ---------------------------------------------------------------*/
    {
        /* 4-descriptor SRV table (t0-t3) so decompiled fragment programs can
         * sample up to 4 texture units; the hardcoded atlas/colour PSs use only
         * t0 and are unaffected. Matching 4 static samplers s0-s3. */
        D3D12_DESCRIPTOR_RANGE srv_range = {0};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = FP_TEX_UNITS;
        srv_range.BaseShaderRegister = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        /* t16-t19: vertex-texture units for TXL in transform programs. */
        D3D12_DESCRIPTOR_RANGE vtex_range = {0};
        vtex_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        vtex_range.NumDescriptors = 4;
        vtex_range.BaseShaderRegister = 16;
        vtex_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        if(vf_enabled()&&pose_compute_mode()&&!pc_init(&s_pose_compute,s_d3d.device,MAX_VERTICES,MAX_DRAWS)){
            s_pose_compute_failed=1;fprintf(stderr,"[POSE-COMPUTE] initialization failed; using CPU path\n");}
        D3D12_ROOT_PARAMETER rp[7] = {0};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   /* b0 = vp_c bank */
        rp[0].Descriptor.ShaderRegister = 0;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srv_range;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   /* b1 = FP texscale */
        rp[2].Descriptor.ShaderRegister = 1;
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[3].DescriptorTable.NumDescriptorRanges = 1;
        rp[3].DescriptorTable.pDescriptorRanges = &vtex_range;
        rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_STATIC_SAMPLER_DESC samp[FP_TEX_UNITS + 5] = {0};
        for (int _s = 0; _s < FP_TEX_UNITS; _s++) {
            samp[_s].Filter = tex_aniso() ? D3D12_FILTER_ANISOTROPIC : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samp[_s].MaxAnisotropy = tex_aniso();
            samp[_s].AddressU = samp[_s].AddressV = samp[_s].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samp[_s].ShaderRegister = (UINT)_s;
            samp[_s].MaxLOD = D3D12_FLOAT32_MAX;
            samp[_s].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        /* s0 is linear like s1-s7: guest programs sample unit 0 too, and a
         * point s0 drew Code Veronica's 256x256 lighter flame as blocks.
         * The dbgfont atlas PS, whose glyph cells bleed under linear
         * filtering, has its own point/clamp sampler at s(FP_TEX_UNITS + 4)
         * below. S0_POINT=1 restores the old point s0. */
        if (getenv("S0_POINT")) {
            samp[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            samp[0].AddressU = samp[0].AddressV = samp[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        }
        samp[FP_TEX_UNITS + 4].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp[FP_TEX_UNITS + 4].AddressU = samp[FP_TEX_UNITS + 4].AddressV =
            samp[FP_TEX_UNITS + 4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[FP_TEX_UNITS + 4].ShaderRegister = (UINT)(FP_TEX_UNITS + 4);
        samp[FP_TEX_UNITS + 4].MaxLOD = D3D12_FLOAT32_MAX;
        samp[FP_TEX_UNITS + 4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        /* s0-s3 again for the vertex stage: vertex textures are fetched
         * unfiltered. Same registers, disjoint visibility. */
        for (int _s = FP_TEX_UNITS; _s < FP_TEX_UNITS + 4; _s++) {
            samp[_s].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            samp[_s].AddressU = samp[_s].AddressV = samp[_s].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp[_s].ShaderRegister = (UINT)(_s - FP_TEX_UNITS);
            samp[_s].MaxLOD = D3D12_FLOAT32_MAX;
            samp[_s].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        }

        /* t24: raw records, descriptors and vertex bytes (RSX_GPU_VFETCH). */
        rp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rp[4].Descriptor.ShaderRegister = 24;
        rp[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        /* b2: the draw's first record. The draw starts at vertex 0 and adds
         * it, rather than relying on SV_VertexID including the start. */
        rp[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[5].Constants.ShaderRegister = 2;
        rp[5].Constants.Num32BitValues = 1;
        rp[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC rd = {0};
        rp[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[6].Descriptor.ShaderRegister=25;
        rp[6].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        rd.NumParameters = vf_enabled() ? (pose_compute_mode()?7:6) : 4; rd.pParameters = rp;
        rd.NumStaticSamplers = FP_TEX_UNITS + 5; rd.pStaticSamplers = samp;
        rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig = NULL; ID3DBlob* err = NULL;
        hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        if (SUCCEEDED(hr)) {
            s_d3d.device->lpVtbl->CreateRootSignature(s_d3d.device, 0,
                sig->lpVtbl->GetBufferPointer(sig), sig->lpVtbl->GetBufferSize(sig),
                &IID_ID3D12RootSignature, (void**)&s_d3d.vp_root_sig);
            sig->lpVtbl->Release(sig);
        } else if (err) { printf("[D3D12] VP root sig: %s\n", (const char*)err->lpVtbl->GetBufferPointer(err)); err->lpVtbl->Release(err); }

        /* raw float4 vertex buffer + constant bank (both UPLOAD, persistently mapped) */
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE nr = {0,0};

        bd.Width = (u64)MAX_VERTICES * 256 * vp_sets();  /* generic VP vertex: 16 float4 slots,
                                          * DOUBLE-buffered by frame parity so a
                                          * new frame's upload never overwrites
                                          * vertices the in-flight GPU frame is
                                          * still reading (was: torn/missing
                                          * triangles mixing stale+new verts) */
        /* Failure here leaves vp_vb_mapped NULL and every VP draw silently
         * declines to upload -- the scene just does not render, with no error
         * anywhere. Report the size too: this buffer is
         * MAX_VERTICES * 256 * 2 bytes and grows fast. */
        fprintf(stderr, "[VPVB] per-frame vertex buffer: %llu MB (%u verts, x%d parity)%c",
                (unsigned long long)(bd.Width >> 20), (unsigned)MAX_VERTICES, vp_sets(), 10);
        if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.vp_vb)))
            s_d3d.vp_vb->lpVtbl->Map(s_d3d.vp_vb, 0, &nr, &s_d3d.vp_vb_mapped);
        else
            fprintf(stderr, "[VPVB] ALLOCATION FAILED -- no VP geometry will render%c", 10);

        bd.Width = (u64)VP_CB_STRIDE * MAX_DRAWS * vp_sets();   /* per-draw constant slots, per parity */
        /* A failure here is silent and catastrophic: vp_record_cb returns early,
         * every draw loses its constants, and the whole scene transforms by
         * zeros -- indistinguishable from a broken vertex program. Say so. */
        fprintf(stderr, "[VPCB] per-draw constant buffer: %llu MB (%d draws)%c",
                (unsigned long long)(bd.Width >> 20), MAX_DRAWS, 10);
        if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.vp_cb)))
            s_d3d.vp_cb->lpVtbl->Map(s_d3d.vp_cb, 0, &nr, &s_d3d.vp_cb_mapped);

        bd.Width = (u64)VP_FPCB_STRIDE * MAX_DRAWS * vp_sets();  /* per-draw FP slots (b1), per parity */
        if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.vp_fpcb)))
            s_d3d.vp_fpcb->lpVtbl->Map(s_d3d.vp_fpcb, 0, &nr, &s_d3d.vp_fpcb_mapped);
        if (vf_enabled()) {
            bd.Width = vf_parity_bytes() * vp_sets();
            if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                    D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                    &IID_ID3D12Resource, (void**)&s_vf_buf)))
                s_vf_buf->lpVtbl->Map(s_vf_buf, 0, &nr, (void**)&s_vf_mapped);
            fprintf(stderr, "[VFETCH] raw vertex buffer: %llu MB %s%c",
                    (unsigned long long)(bd.Width >> 20), s_vf_mapped ? "ready" : "ALLOCATION FAILED", 10);
        }

        printf("[D3D12] VP path resources: rootsig=%p vb=%p cb=%p\n",
               (void*)s_d3d.vp_root_sig, (void*)s_d3d.vp_vb, (void*)s_d3d.vp_cb);
    }

    if(pose_compute_mode()&&!s_vf_mapped){s_pose_compute_failed=1;pc_release(&s_pose_compute);}
    pose_init_recording();

    printf("[D3D12] Initialization complete (%ux%u, %u buffers, pipeline=%s)\n",
           width, height, FRAME_COUNT,
           s_d3d.pipeline_ready ? "ready" : "NOT ready");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Frame sync helpers
 * -----------------------------------------------------------------------*/

static const char* dred_op_name(D3D12_AUTO_BREADCRUMB_OP op)
{
    switch (op) {
    case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:                   return "SetMarker";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:               return "DrawInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:        return "DrawIndexedInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:                    return "Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:            return "CopyBufferRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:           return "CopyTextureRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:                return "CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:          return "ResolveSubresource";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:       return "ClearRenderTargetView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:       return "ClearDepthStencilView";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:             return "ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:               return "ExecuteBundle";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT:                     return "Present";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:             return "ExecuteIndirect";
    default:                                                   return "op";
    }
}

/* Once per run, after the device is lost: the last operations of each
 * command list DRED saw (the first not-completed one is the culprit) and the
 * faulting GPU address, if any. */
static void dred_report(const char* where)
{
    static int done = 0;
    if (done || !s_d3d.device) return;
    HRESULT rr = s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device);
    if (rr == S_OK) return;
    done = 1;
    fprintf(stderr, "[DRED] GPU device lost (reason 0x%08lX), noticed at %s\n", (long)rr, where);
    ID3D12DeviceRemovedExtendedData* d = NULL;
    if (FAILED(s_d3d.device->lpVtbl->QueryInterface(s_d3d.device,
            &IID_ID3D12DeviceRemovedExtendedData, (void**)&d)) || !d) {
        fprintf(stderr, "[DRED] no DRED data (CV_DRED=0 or unsupported)\n");
        return;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc;
    if (SUCCEEDED(d->lpVtbl->GetAutoBreadcrumbsOutput(d, &bc))) {
        int ln = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE* n = bc.pHeadAutoBreadcrumbNode; n && ln < 16;
             n = n->pNext, ln++) {
            const UINT32 last = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0;
            if (last >= n->BreadcrumbCount) continue;        /* finished cleanly */
            UINT32 draws = 0;
            for (UINT32 i = 0; i < last; i++)
                if (n->pCommandHistory[i] == D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED ||
                    n->pCommandHistory[i] == D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED)
                    draws++;
            fprintf(stderr, "[DRED] list %p: %u of %u ops completed, %u draws completed; around the stop:\n",
                    (void*)n->pCommandList, last, n->BreadcrumbCount, draws);
            const UINT32 lo = last > 4 ? last - 4 : 0;
            const UINT32 hi = last + 3 < n->BreadcrumbCount ? last + 3 : n->BreadcrumbCount;
            for (UINT32 i = lo; i < hi; i++)
                fprintf(stderr, "[DRED]   #%u %s%s\n", i, dred_op_name(n->pCommandHistory[i]),
                        i == last ? "   <-- did not complete" : "");
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT pf;
    if (SUCCEEDED(d->lpVtbl->GetPageFaultAllocationOutput(d, &pf)) && pf.PageFaultVA) {
        fprintf(stderr, "[DRED] page fault at GPU VA 0x%llX\n", (unsigned long long)pf.PageFaultVA);
        for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadExistingAllocationNode; a; a = a->pNext)
            fprintf(stderr, "[DRED]   live allocation there: type %d\n", (int)a->AllocationType);
        for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadRecentFreedAllocationNode; a; a = a->pNext)
            fprintf(stderr, "[DRED]   recently freed allocation there: type %d\n", (int)a->AllocationType);
    }
    d->lpVtbl->Release(d);
    fflush(stderr);
}

/* Resources dropped while a frame is being recorded. The open command list,
 * and the frames still in flight, may reference them, so they are released
 * only once the frame that recorded them has completed on the GPU -- see
 * move_to_next_frame. Releasing at once made the GPU read freed memory and
 * hang (DRED: page fault in a recently freed resource) as soon as gameplay
 * started reusing texture slots. */
volatile int g_rsx_cap_request = 0;   /* set by F12, see the present path */

static ID3D12Resource** s_defer[FRAME_COUNT];
static u32 s_defer_n[FRAME_COUNT], s_defer_cap[FRAME_COUNT];

static void defer_release(ID3D12Resource* r)
{
    if (!r) return;
    const u32 fi = s_d3d.frame_index;
    if (s_defer_n[fi] == s_defer_cap[fi]) {
        u32 cap = s_defer_cap[fi] ? s_defer_cap[fi] * 2 : 64;
        ID3D12Resource** p = (ID3D12Resource**)realloc(s_defer[fi], cap * sizeof(*p));
        if (!p) { r->lpVtbl->Release(r); return; }   /* out of memory: old behaviour */
        s_defer[fi] = p; s_defer_cap[fi] = cap;
    }
    s_defer[fi][s_defer_n[fi]++] = r;
}

/* Set when resources are actually released: a texture window cached by
 * pointer could otherwise match a new resource that reused a freed one's
 * address and keep the old views (srv_commit). */
static int s_srv_cache_stale;
/* Bumped when the shared depth buffer is recreated (apply_res_scale), so the
 * views written once against it are written again. */
static u32 s_depth_gen;

static void release_deferred(u32 fi)
{
    if (s_defer_n[fi]) s_srv_cache_stale = 1;
    for (u32 i = 0; i < s_defer_n[fi]; i++)
        s_defer[fi][i]->lpVtbl->Release(s_defer[fi][i]);
    s_defer_n[fi] = 0;
}

static void wait_for_gpu(void)
{
    u32 fi = s_d3d.frame_index;
    s_d3d.fence_values[fi]++;
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_d3d.fence, s_d3d.fence_values[fi]);

    if (s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < s_d3d.fence_values[fi]) {
        s_d3d.fence->lpVtbl->SetEventOnCompletion(
            s_d3d.fence, s_d3d.fence_values[fi], s_d3d.fence_event);
        /* NEVER wait unbounded here: this runs on the vblank ticker, which also
         * drives the guest's vblank handlers, the FIFO drain, and the fence
         * publication. A device removal (TDR) leaves the D3D12 fence unsignaled
         * forever and an INFINITE wait silently froze the ENTIRE emulation --
         * observed on LBP as a boot that died the moment the 2048x2048
         * offscreen RT was created (last log line), every guest thread parked.
         * Degrade loudly instead: bounded waits + the removal reason, then
         * carry on (subsequent D3D calls fail visibly but the game keeps
         * running headless). */
        for (int tries = 0; tries < 3; tries++) {
            if (WaitForSingleObject(s_d3d.fence_event, 2000) != WAIT_TIMEOUT)
                return;
            HRESULT rr = s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device);
            printf("[D3D12] wait_for_gpu STUCK %ds: want %llu got %llu removed=0x%08lX\n",
                   2 * (tries + 1),
                   (unsigned long long)s_d3d.fence_values[fi],
                   (unsigned long long)s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence),
                   (long)rr);
            fflush(stdout);
            if (rr != 0 /* S_OK: device still alive, keep waiting a bit */) {
                dred_report("wait_for_gpu");
                break;
            }
        }
    }
}

/* RES_SCALE changed while running (a launcher's options menu). Applied at a
 * frame boundary, after Present: drain the GPU, resize the swapchain to the
 * new render size, rebuild the shared depth buffer, and drop every offscreen
 * target so each is recreated at the new scale on its next use. Whatever the
 * dropped targets held is lost, so one frame may show a glitch. */
static volatile float s_rs_pending;
void rsx_d3d12_set_res_scale(float f)
{
    if (f < 0.5f || f > 4.0f) f = 1.0f;
    s_rs_pending = f;
}

static void apply_res_scale(void)
{
    const float f = s_rs_pending;
    s_rs_pending = 0.0f;
    if (f == s_rs || !s_d3d.swap_chain) return;
    wait_for_gpu();
    for (int i = 0; i < MAX_OFF_RTS; i++) {
        OffRT* r = &s_d3d.off_rt[i];
        defer_release(r->res); r->res = NULL;
        defer_release(r->up);  r->up = NULL;
        defer_release(r->wb);  r->wb = NULL; r->wb_pending = 0;
        r->used = 0;
    }
    if (s_screen_copy) { defer_release(s_screen_copy); s_screen_copy = NULL; }
    for (u32 i = 0; i < FRAME_COUNT; i++)
        if (s_d3d.render_targets[i]) { s_d3d.render_targets[i]->lpVtbl->Release(s_d3d.render_targets[i]);
                                       s_d3d.render_targets[i] = NULL; }
    const float old = s_rs;
    s_rs = f;
    if (s_display) {
        if (!game_rts_create()) {   /* keep the old size */
            printf("[D3D12] RES_SCALE %.2f: render targets failed\n", f);
            for (u32 i = 0; i < FRAME_COUNT; i++)
                if (s_d3d.render_targets[i]) { s_d3d.render_targets[i]->lpVtbl->Release(s_d3d.render_targets[i]);
                                               s_d3d.render_targets[i] = NULL; }
            s_rs = old;
            game_rts_create();
        }
    } else {
    HRESULT hr = s_d3d.swap_chain->lpVtbl->ResizeBuffers(s_d3d.swap_chain, FRAME_COUNT,
        rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs), DXGI_FORMAT_R8G8B8A8_UNORM,
        s_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {   /* keep the old size */
        printf("[D3D12] RES_SCALE %.2f: ResizeBuffers failed (0x%08lX)\n", f, hr);
        s_rs = old;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv);
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        s_d3d.swap_chain->lpVtbl->GetBuffer(s_d3d.swap_chain, i, &IID_ID3D12Resource,
                                            (void**)&s_d3d.render_targets[i]);
        s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, s_d3d.render_targets[i], NULL, rtv);
        rtv.ptr += s_d3d.rtv_descriptor_size;
    }
    }
    /* depth: same description as at init, at the new scale */
    D3D12_RESOURCE_DESC dd; s_d3d.depth_buffer->lpVtbl->GetDesc(s_d3d.depth_buffer, &dd);
    dd.Width = rs_dim(2048, s_rs); dd.Height = rs_dim(2048, s_rs);
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE cv = {0};
    cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; cv.DepthStencil.Depth = 1.0f;
    ID3D12Resource* nd = NULL;
    if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE,
            &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, &IID_ID3D12Resource, (void**)&nd))) {
        defer_release(s_d3d.depth_buffer);
        s_d3d.depth_buffer = nd;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {0};
        dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE dh;
        s_d3d.dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.dsv_heap, &dh);
        s_d3d.device->lpVtbl->CreateDepthStencilView(s_d3d.device, nd, &dsv, dh);
        s_depth_gen++;
    }
    s_srv_cache_stale = 1;
    printf("[D3D12] RES_SCALE now %.2f: rendering %ux%u\n", s_rs,
           rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs));
}
static void move_to_next_frame(void)
{
    u64 current_fence = s_d3d.fence_values[s_d3d.frame_index];
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_d3d.fence, current_fence);

    s_d3d.frame_index = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);

    if (s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < s_d3d.fence_values[s_d3d.frame_index]) {
        s_d3d.fence->lpVtbl->SetEventOnCompletion(
            s_d3d.fence, s_d3d.fence_values[s_d3d.frame_index], s_d3d.fence_event);
        if (WaitForSingleObject(s_d3d.fence_event, 2000) == WAIT_TIMEOUT) {
            HRESULT rr = s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device);
            printf("[D3D12] FENCE STUCK 2s: want %llu got %llu removed=0x%08lX\n",
                   (unsigned long long)s_d3d.fence_values[s_d3d.frame_index],
                   (unsigned long long)s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence),
                   (long)rr);
            dred_report("move_to_next_frame");
        }
    }
    /* This slot's previous frame is done: what it dropped can go now. */
    release_deferred(s_d3d.frame_index);

    s_d3d.fence_values[s_d3d.frame_index] = current_fence + 1;
}

/* ---------------------------------------------------------------------------
 * Render a frame (clear + present)
 * -----------------------------------------------------------------------*/

/* Write the mapped readback buffer (R8G8B8A8, row pitch = readback_pitch) out
 * as a 24-bit bottom-up BMP. Debug-only. */
/* Name prefix for the next readback dump: lets the same writer emit the
 * backbuffer and the screen-copy texture under different filenames. */
static const char* s_dump_name = NULL;

static void dump_backbuffer_bmp(void)
{
    if (!s_d3d.readback_buf) return;
    void* mapped = NULL;
    D3D12_RANGE rr = {0, (SIZE_T)s_d3d.readback_pitch * rs_dim(s_d3d.height, s_rs)};
    if (FAILED(s_d3d.readback_buf->lpVtbl->Map(s_d3d.readback_buf, 0, &rr, &mapped)) || !mapped)
        return;

    static int idx = 0;
    char path[512];
    const char* dir = getenv("CELLMARK_DUMP_DIR");   /* default: current dir */
    snprintf(path, sizeof(path), "%s%s%s_%03d.bmp",
             dir ? dir : "", dir ? "/" : "", s_dump_name ? s_dump_name : "frame", idx++);
    FILE* f = fopen(path, "wb");
    if (f) {
        u32 w = rs_dim(s_d3d.width, s_rs), h = rs_dim(s_d3d.height, s_rs);
        u32 padded = (w * 3 + 3) & ~3u;
        u32 imgsz  = padded * h;
        u32 filesz = 54 + imgsz;
        unsigned char hdr[54] = {0};
        hdr[0] = 'B'; hdr[1] = 'M';
        hdr[2]=filesz&0xFF; hdr[3]=(filesz>>8)&0xFF; hdr[4]=(filesz>>16)&0xFF; hdr[5]=(filesz>>24)&0xFF;
        hdr[10]=54; hdr[14]=40;
        hdr[18]=w&0xFF; hdr[19]=(w>>8)&0xFF; hdr[20]=(w>>16)&0xFF; hdr[21]=(w>>24)&0xFF;
        hdr[22]=h&0xFF; hdr[23]=(h>>8)&0xFF; hdr[24]=(h>>16)&0xFF; hdr[25]=(h>>24)&0xFF;
        hdr[26]=1; hdr[28]=24;
        hdr[34]=imgsz&0xFF; hdr[35]=(imgsz>>8)&0xFF; hdr[36]=(imgsz>>16)&0xFF; hdr[37]=(imgsz>>24)&0xFF;
        fwrite(hdr, 1, 54, f);
        unsigned char* row = (unsigned char*)malloc(padded);
        if (row) {
            memset(row, 0, padded);
            for (int y = (int)h - 1; y >= 0; y--) {   /* BMP is bottom-up */
                unsigned char* src = (unsigned char*)mapped + (u32)y * s_d3d.readback_pitch;
                for (u32 x = 0; x < w; x++) {
                    row[x*3+0] = src[x*4+2]; /* B */
                    row[x*3+1] = src[x*4+1]; /* G */
                    row[x*3+2] = src[x*4+0]; /* R */
                }
                fwrite(row, 1, padded, f);
            }
            free(row);
        }
        fclose(f);
        printf("[D3D12] dumped %s (%ux%u) frame %llu\n", path, s_d3d.width, s_d3d.height,
               (unsigned long long)s_d3d.frame_count);
    }
    D3D12_RANGE wr = {0, 0};
    s_d3d.readback_buf->lpVtbl->Unmap(s_d3d.readback_buf, 0, &wr);
}

/* ---------------------------------------------------------------------------
 * RSX frame capture ("rsxcap") -- a purpose-built GPU debugger. RSX_CAP=start
 * [:count[:stride]] snapshots whole frames to <RSX_CAP_DIR>/frame_NN/: a text
 * manifest of every op (in submission order, with full surface/shader/texture
 * state) plus a BMP of the backbuffer and every offscreen colour RT used that
 * frame. One frame, captured atomically -- no cross-frame guessing.
 * -----------------------------------------------------------------------*/

/* Shared 24-bit BMP writer for readbacks. R8G8B8A8_UNORM straight; the half/
 * float RT formats are |v|-tonemapped so signed/HDR data is visible. */
static void cap_write_bmp(const char* path, const void* mapped, u32 w, u32 h,
                          u32 pitch, u32 dxgi)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    u32 rowb = (w * 3 + 3) & ~3u, datasz = rowb * h;
    u8 hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(u32*)(hdr+2) = 54 + datasz; *(u32*)(hdr+10) = 54; *(u32*)(hdr+14) = 40;
    *(int*)(hdr+18) = (int)w; *(int*)(hdr+22) = (int)h;
    *(u16*)(hdr+26) = 1; *(u16*)(hdr+28) = 24; *(u32*)(hdr+34) = datasz;
    fwrite(hdr, 1, 54, f);
    u8* line = (u8*)malloc(rowb);
    if (line) for (int y = (int)h - 1; y >= 0; y--) {   /* BMP is bottom-up */
        const u8* srow = (const u8*)mapped + (u64)y * pitch;
        memset(line, 0, rowb);
        for (u32 x = 0; x < w; x++) {
            float rv, gv, bv;
            if (dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const u16* hp = (const u16*)(srow + (u64)x * 8);
                float v[3];
                for (int c = 0; c < 3; c++) {
                    u16 hv = hp[c]; u32 s = (hv>>15)&1, e = (hv>>10)&0x1F, m = hv&0x3FF; float fv;
                    if (e == 0) fv = (float)m / 16777216.0f;
                    else { u32 fb = (s<<31)|((e-15+127)<<23)|(m<<13); memcpy(&fv,&fb,4); }
                    v[c] = fv;
                }
                rv = v[0]; gv = v[1]; bv = v[2];
            } else if (dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                const float* fp = (const float*)(srow + (u64)x * 16);
                rv = fp[0]; gv = fp[1]; bv = fp[2];
            } else if (dxgi == DXGI_FORMAT_R32_FLOAT) {
                /* Display scalar depth as grey, not as three bytes of its
                 * IEEE representation (which conceals its exponent). */
                float depth;
                memcpy(&depth, srow + (u64)x * 4, sizeof depth);
                rv = gv = bv = depth;
            } else {
                const u8* p = srow + (u64)x * 4;
                rv = p[0]/255.0f; gv = p[1]/255.0f; bv = p[2]/255.0f;
            }
            float ar = rv<0?-rv:rv, ag = gv<0?-gv:gv, ab = bv<0?-bv:bv;
            if (ar>1) ar=1; if (ag>1) ag=1; if (ab>1) ab=1;
            line[x*3+0] = (u8)(ab*255.0f);
            line[x*3+1] = (u8)(ag*255.0f);
            line[x*3+2] = (u8)(ar*255.0f);
        }
        fwrite(line, 1, rowb, f);
    }
    free(line);
    fclose(f);
}

/* Copy one GPU resource into a fresh readback buffer and write it as a BMP.
 * Self-contained: waits, resets the frame command list, copies, executes,
 * waits, maps. `prior` is the resource's current tracked state (restored after
 * the copy so the caller's state tracking stays valid). */
static void cap_readback_write(ID3D12Resource* res, u32 w, u32 h, u32 dxgi,
                               D3D12_RESOURCE_STATES prior, u32 fi, const char* path)
{
    if (!res || !w || !h) return;
    u32 bpp = (dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 :
              (dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;
    u32 pitch = (w * bpp + 255) & ~255u;

    ID3D12Resource* rb = NULL;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (u64)pitch * h; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&rb)) || !rb)
        return;

    wait_for_gpu();
    s_d3d.cmd_allocators[fi]->lpVtbl->Reset(s_d3d.cmd_allocators[fi]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[fi], NULL);

    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (prior != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        b.Transition.StateBefore = prior;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = (DXGI_FORMAT)dxgi;
    dst.PlacedFootprint.Footprint.Width = w; dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = pitch;
    src.pResource = res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
        /* TEXUP_DBG=1: texture uploads per second. A cache that misses every
         * draw re-uploads megabytes per frame, which is the kind of GPU work
         * that trips a 2-second TDR while looking like nothing in a draw count. */
        { static int td = -1; if (td < 0) td = getenv("TEXUP_DBG") ? 1 : 0;
          if (td) { static long long n = 0, bytes = 0; static clock_t t0 = 0;
              n++; bytes += (long long)pitch * h;
              const clock_t now = clock();
              if (!t0) t0 = now;
              if (now - t0 >= CLOCKS_PER_SEC) {
                  fprintf(stderr, "[TEXUP] %lld uploads/s, %.1f MB/s\n",
                          n, bytes / 1048576.0);
                  n = 0; bytes = 0; t0 = now; } } }

    if (prior != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = prior;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }

    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cl[] = { (ID3D12CommandList*)s_d3d.cmd_list };
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cl);
    wait_for_gpu();

    void* mp = NULL; D3D12_RANGE rr = {0, (SIZE_T)pitch * h};
    if (SUCCEEDED(rb->lpVtbl->Map(rb, 0, &rr, &mp)) && mp) {
        cap_write_bmp(path, mp, w, h, pitch, dxgi);
        /* This title's 3x2 shadow atlas packs two moments into RGBA8.
         * BMP drops alpha (the first moment's high byte): preserve exact
         * rows in a capture-only sidecar. No additional GPU copy. */
        if (dxgi == DXGI_FORMAT_R8G8B8A8_UNORM && w == 768 && h == 512) {
            char rawpath[600];
            snprintf(rawpath, sizeof rawpath, "%s.rgba8", path);
            FILE* raw = fopen(rawpath, "wb");
            if (raw) {
                for (u32 y = 0; y < h; y++)
                    fwrite((const u8*)mp + (size_t)y * pitch, 4, w, raw);
                fclose(raw);
            }
        }
        D3D12_RANGE wr = {0, 0};
        rb->lpVtbl->Unmap(rb, 0, &wr);
    }
    rb->lpVtbl->Release(rb);
}

/* F11: a screenshot of the finished frame -- after anti-aliasing, scaling
 * and sharpening, before the port's own overlays (performance panel, SRT,
 * notices), so the picture is the game alone. The copy rides in the frame's
 * command list; once it has run, a thread writes the PNG into "screenshots"
 * beside the exe and a notice says how it went (SCREENSHOT_TEXT, UTF-8
 * "<saved>|<failed>", gives the launcher's words). */
static ID3D12Resource* s_shot_rb;
static u32 s_shot_w, s_shot_h, s_shot_pitch;
typedef struct { u8* rgba; u32 w, h; } ShotJob;
static void shot_png_write(void* ctx, void* data, int size) { fwrite(data, 1, (size_t)size, (FILE*)ctx); }
static DWORD WINAPI shot_thread(LPVOID arg)
{
    ShotJob* j = (ShotJob*)arg;
    wchar_t dir[MAX_PATH], path[MAX_PATH + 64];
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    dir[n] = 0;
    wcscat_s(dir, MAX_PATH, L"screenshots");
    CreateDirectoryW(dir, NULL);
    SYSTEMTIME t; GetLocalTime(&t);
    int ok = 0;
    for (int k = 1; k < 100; k++) {
        if (k == 1) swprintf(path, MAX_PATH + 64, L"%ls\\%04u-%02u-%02u_%02u-%02u-%02u.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        else swprintf(path, MAX_PATH + 64, L"%ls\\%04u-%02u-%02u_%02u-%02u-%02u_%d.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, k);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) break;
    }
    FILE* f = _wfopen(path, L"wb");
    if (f) {
        ok = stbi_write_png_to_func(shot_png_write, f, (int)j->w, (int)j->h, 4, j->rgba, (int)j->w * 4) && !ferror(f);
        if (fclose(f)) ok = 0;
        if (!ok) DeleteFileW(path);
    }
    wchar_t words[160] = L"Screenshot saved|Could not save the screenshot";
    const char* e = (getenv)("SCREENSHOT_TEXT");
    if (e && *e) MultiByteToWideChar(CP_UTF8, 0, e, -1, words, 160);
    wchar_t* bar = wcschr(words, L'|');
    if (bar) *bar = 0;
    perf_overlay_toast(ok || !bar ? words : bar + 1, ok);
    fprintf(stderr, "[SHOT] %ls: %s\n", path, ok ? "saved" : "failed");
    free(j->rgba); free(j);
    return 0;
}
/* res is a R8G8B8A8 render target in RENDER_TARGET state. */
static void shot_record(ID3D12Resource* res, u32 w, u32 h)
{
    if (!s_shot_request || s_shot_rb || !res || !w || !h) return;
    InterlockedExchange(&s_shot_request, 0);
    const u32 pitch = (w * 4 + 255) & ~255u;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (u64)pitch * h; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_shot_rb)) || !s_shot_rb) {
        s_shot_rb = NULL;
        perf_overlay_toast(L"Screenshot failed", 0);
        return;
    }
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = s_shot_rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = w; dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = pitch;
    src.pResource = res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    s_shot_w = w; s_shot_h = h; s_shot_pitch = pitch;
}
/* After the frame was submitted: wait for that one frame, take the pixels
 * (alpha forced opaque) and hand them to the writer. */
static void shot_finish(void)
{
    if (!s_shot_rb) return;
    wait_for_gpu();
    const u32 w = s_shot_w, h = s_shot_h;
    void* mp = NULL; D3D12_RANGE rr = {0, (SIZE_T)s_shot_pitch * h};
    ShotJob* j = (ShotJob*)malloc(sizeof *j);
    u8* px = (u8*)malloc((size_t)w * h * 4);
    int ok = 0;
    if (j && px && SUCCEEDED(s_shot_rb->lpVtbl->Map(s_shot_rb, 0, &rr, &mp)) && mp) {
        for (u32 y = 0; y < h; y++) {
            u8* row = px + (size_t)y * w * 4;
            memcpy(row, (const u8*)mp + (size_t)y * s_shot_pitch, (size_t)w * 4);
            for (u32 x = 0; x < w; x++) row[x * 4 + 3] = 255;
        }
        D3D12_RANGE wr = {0, 0};
        s_shot_rb->lpVtbl->Unmap(s_shot_rb, 0, &wr);
        j->rgba = px; j->w = w; j->h = h;
        HANDLE t = CreateThread(NULL, 0, shot_thread, j, 0, NULL);
        if (t) { CloseHandle(t); ok = 1; }
    }
    s_shot_rb->lpVtbl->Release(s_shot_rb);
    s_shot_rb = NULL;
    if (!ok) { free(px); free(j); perf_overlay_toast(L"Screenshot failed", 0); }
}


#include "rsx_motion_capture.h"
/* A versioned little-endian diagnostic snapshot, copied only during F9.
 * Source positions and the actual CPU/VF recipe result share each vertex ID.
 * GPU-compute mode is explicitly flagged: final positions unavailable here. */
static void motion_capture_record(ID3D12Resource* image,u32 w,u32 h)
{
    if(!s_rb.present||!mc_wants())return;
    const RsxPoseHistory* hist=&s_pose_history;const RsxPoseSnapshot* cur=&hist->cur;
    const u32 nv=s_pose_on?s_rb.pose_nv:0,nd=s_pose_on&&hist->have_cur&&hist->reason!=RSX_POSE_INVALID&&cur->scene==s_rb.pose_scene?cur->nd:0;
    const u32 packed=nd?cur->nv:0,ops=s_dbg_last_draws<MAX_DRAWS?s_dbg_last_draws:MAX_DRAWS;const int compute=s_pose_on&&s_pose_compute.active;
    /* render_frame already reset s_rb.count; s_dbg_last_draws freezes THIS
     * completed batch's count before that reset. Otherwise every F9 silently
     * omitted its draw/FP records. Capture full banks too: c8+ may animate
     * local-space face parts even when their recorded xyz never change. */
    u8 seen_cb[MAX_DRAWS]={0},seen_vs[VP_VS_CACHE]={0};
    u32 banks=0,programs=0;size_t program_bytes=0;
    for(u32 op=0;op<ops;op++) {
        const D3D12DrawRecord* d=s_rb.draws+op;
        if(!d->is_vp||d->is_clear||d->is_copy)continue;
        if(d->vcb_slot<MAX_DRAWS&&!seen_cb[d->vcb_slot]){seen_cb[d->vcb_slot]=1;banks++;}
        if(d->vs_idx>=0&&d->vs_idx<s_d3d.vp_vs_n&&!seen_vs[d->vs_idx]&&s_d3d.vp_vs[d->vs_idx].hlsl){
            seen_vs[d->vs_idx]=1;programs++;program_bytes+=8+strlen(s_d3d.vp_vs[d->vs_idx].hlsl);
        }
    }
    size_t bytes=64+(size_t)nv*(sizeof(RsxPoseVertex)+3*sizeof(float))+
        (size_t)nd*(2*sizeof(RsxPoseDraw)+sizeof(RsxPoseTrack)+2*sizeof(int32_t)+3*32*sizeof(float))+
        (size_t)packed*6*sizeof(float)+(size_t)ops*336+16+
        (size_t)banks*(4+RSX_MAX_VERTEX_CONSTANTS*16)+program_bytes;
    unsigned char* blob=bytes<=64u*1024*1024?(unsigned char*)malloc(bytes):NULL;
    u32 stationary=0;for(u32 d=0;d<nd;d++)stationary+=hist->stationary_draws[d]!=0;
    u32 bad=0,jumps=0;float max_delta=0;
    if(blob){
        u32 head[16]={0x4d435031u,2,nv,nd,packed,(u32)(compute?2:s_pose_gpu_blend?1:0),s_rb.parity,s_rb.pose_scene,s_rb.pose_stamp,s_d3d.frame_count,
            sizeof(RsxPoseVertex),sizeof(RsxPoseDraw),sizeof(RsxPoseTrack),32,ops,336};
        memcpy(blob,head,64);unsigned char* at=blob+64;
        const RsxPoseVertex* original=nv?s_pose_vertices+(size_t)s_rb.parity*MAX_VERTICES:NULL;
        if(nv)memcpy(at,original,(size_t)nv*sizeof(*original));at+=(size_t)nv*sizeof(RsxPoseVertex);
        float* result=(float*)at;
        for(u32 v=0;v<nv;v++){
            float* out=result+(size_t)v*3;memcpy(out,original[v].xyz,12);
            if(!compute){
                if(vf_pose_enabled()&&s_vf_mapped){
                    const u8* rec=s_vf_mapped+(u64)s_rb.parity*vf_parity_bytes()+VF_POSE_OFF+(size_t)v*vf_pose_stride();rsx_pose_gpu_read(rec,out);
                }else if(s_d3d.vp_vb_mapped)memcpy(out,(const u8*)s_d3d.vp_vb_mapped+((size_t)s_rb.parity*MAX_VERTICES+v)*256,12);
            }
            if(original[v].stamp==s_rb.pose_stamp){
                float distance=0;for(int k=0;k<3;k++){float d=out[k]-original[v].xyz[k];distance+=d*d;if(!isfinite(out[k]))bad++;}
                if(isfinite(distance)){float d=sqrtf(distance);if(d>max_delta)max_delta=d;if(d>8)jumps++;}
            }
        }
        at+=(size_t)nv*3*sizeof(float);
#define MC_APPEND(ptr,n) do{size_t count_=(n);if(count_)memcpy(at,(ptr),count_);at+=count_;}while(0)
        for(u32 d=0;d<nd;d++){
            RsxPoseDraw captured=cur->draws[d];
            if(hist->stationary_draws[d])captured.flags|=RSX_POSE_CAPTURE_STATIONARY;
            MC_APPEND(&captured,sizeof captured);
        }
        MC_APPEND(s_pose_draws,(size_t)nd*sizeof(RsxPoseDraw));
        MC_APPEND(cur->tracks,(size_t)nd*sizeof(RsxPoseTrack));
        MC_APPEND(hist->vertex_sources,(size_t)nd*sizeof(int32_t));MC_APPEND(hist->camera_sources,(size_t)nd*sizeof(int32_t));
        MC_APPEND(cur->old_xyz,(size_t)packed*3*sizeof(float));MC_APPEND(cur->xyz,(size_t)packed*3*sizeof(float));
        MC_APPEND(cur->old_cameras,(size_t)nd*32*sizeof(float));MC_APPEND(cur->cameras,(size_t)nd*32*sizeof(float));
        for(u32 d=0;d<nd;d++){
            const u32 cb=s_pose_draws[d].cb;
            if(cb<MAX_DRAWS)memcpy(at,(const u8*)s_d3d.vp_cb_mapped+((size_t)s_rb.parity*MAX_DRAWS+cb)*VP_CB_STRIDE,32*sizeof(float));
            else memset(at,0,32*sizeof(float));at+=32*sizeof(float);
        }
        for(u32 op=0;op<ops;op++){
            const D3D12DrawRecord* d=s_rb.draws+op;u32 info[16]={op,fp_snap_code(d),d->vs_idx>=0&&d->vs_idx<s_d3d.vp_vs_n?s_d3d.vp_vs[d->vs_idx].hash:0,d->vcb_slot,d->rt_off,d->rt_w,d->rt_h,d->vp_w,d->vp_h,s_pose_accepted[op],d->blend,d->dtest,0,0,d->is_clear,d->is_copy};
            memset(at,0,320);const u8* snap=fp_snap_ptr(d);
            if(snap&&info[1]&&d->fp_bytes<=16384){
                u32 tail[4];memcpy(tail,snap+d->fp_bytes+16,16);
                if(tail[0]<=256){info[12]=tail[0]<16?tail[0]:16;info[13]=tail[1];memcpy(at+64,snap+d->fp_bytes+32,(size_t)info[12]*16);}
            }
            memcpy(at,info,64);
            u32 geometry[4]={d->vb_byte_offset/256,d->vertex_count,d->topology,d->is_vp};
            memcpy(at+320,geometry,16);at+=336;
        }
        u32 extra[4]={0x3256434du,banks,programs,RSX_MAX_VERTEX_CONSTANTS};
        MC_APPEND(extra,sizeof extra);
        for(u32 cb=0;cb<MAX_DRAWS;cb++)if(seen_cb[cb]){
            MC_APPEND(&cb,4);
            MC_APPEND((const u8*)s_d3d.vp_cb_mapped+((size_t)s_rb.parity*MAX_DRAWS+cb)*VP_CB_STRIDE,RSX_MAX_VERTEX_CONSTANTS*16);
        }
        for(int vs=0;vs<s_d3d.vp_vs_n;vs++)if(seen_vs[vs]){
            const VPVSEntry* shader=s_d3d.vp_vs+vs;u32 program[2]={shader->hash,(u32)strlen(shader->hlsl)};
            MC_APPEND(program,sizeof program);MC_APPEND(shader->hlsl,program[1]);
        }
#undef MC_APPEND
        if((size_t)(at-blob)!=bytes){free(blob);blob=NULL;}
    }
    LARGE_INTEGER tick,freq;QueryPerformanceCounter(&tick);QueryPerformanceFrequency(&freq);
    char json[4096];snprintf(json,sizeof json,
        "{\"pose_schema\":2,\"stationary_draws\":%u,\"vertex_identity\":\"uv_source_index\",\"captured_ops\":%u,\"captured_vp_banks\":%u,\"captured_vp_programs\":%u,\"frame\":%u,\"qpc\":%llu,\"qpc_frequency\":%llu,\"width\":%u,\"height\":%u,\"scene\":%u,\"stamp\":%u,\"draws\":%u,\"pose_enabled\":%d,\"reason\":%d,\"alpha\":%.6f,\"matched\":%u,\"moving\":%u,\"coherent\":%u,\"coherence_conflicts\":%u,\"seams\":%u,\"seam_conflicts\":%u,\"seam_components\":%u,\"seam_veto_vertices\":%u,\"shadow_draws\":%u,\"compute_mode\":%d,\"final_positions_available\":%s,\"nonfinite_components\":%u,\"displacements_over_8\":%u,\"max_displacement\":%.6f,\"pose_bytes\":%zu,\"fsr_requested\":%d,\"image\":\"real_render_before_port_overlays\"}\n",
        stationary,ops,banks,programs,s_d3d.frame_count,(unsigned long long)tick.QuadPart,(unsigned long long)freq.QuadPart,w,h,s_rb.pose_scene,s_rb.pose_stamp,s_dbg_last_draws,s_pose_on,hist->reason,hist->alpha,hist->matched,hist->moving,hist->coherent,hist->coherence_conflicts,hist->seams,hist->seam_conflicts,hist->seam_components,hist->seam_veto_vertices,s_pose_shadow_draws,compute,blob&&!compute?"true":"false",bad,jumps,max_delta,blob?bytes:0,rsx_fsr31_requested());
    if(!mc_record(s_d3d.device,s_d3d.cmd_list,image,w,h,json,blob,blob?bytes:0))free(blob);
}

/* Optional, bounded GPU instrumentation for the image investigation. */
static ID3D12QueryHeap* s_cv_query_heap;
static ID3D12Resource* s_cv_query_readback;
static unsigned char s_cv_query_valid[MAX_DRAWS];
static int s_cv_query_active;

/* rsxcap step snapshots: on the frame being captured, a copy of each
 * offscreen target at the moment the draw list leaves it. The end-of-frame
 * dump shows only what survived; these show where an image was lost. */
typedef struct { ID3D12Resource* res; u32 op, off, w, h, dxgi; } CapStep;
#define CAP_MAX_STEPS 128
static CapStep s_cap_steps[CAP_MAX_STEPS];
static int s_cap_nsteps = 0;
static int s_cap_steps_on = 0;      /* armed for the frame being rendered */
static u64 s_rsxcap_next = 0;       /* RSX_CAP: frame of the next capture (0 = none) */
static int s_autocap_arm = 0;       /* CV_AUTOCAP fires after the next frame */
static void cap_steps_free(void)
{
    for (int i = 0; i < s_cap_nsteps; i++)
        if (s_cap_steps[i].res) defer_release(s_cap_steps[i].res);
    s_cap_nsteps = 0;
}

static void rsx_capture_frame(u32 fi, u32 ndraws, u32 capidx)
{
    if(pose_compute_mode()&&s_pose_compute_capture_ready&&s_pose_compute.capture){
        wait_for_gpu();D3D12_RANGE range={0,(SIZE_T)s_rb.pose_nv*16};void* mapped=NULL;
        if(SUCCEEDED(s_pose_compute.capture->lpVtbl->Map(s_pose_compute.capture,0,&range,&mapped)))s_pose_compute_capture=(const u8*)mapped;
    }
    char dir[300]; const char* base = getenv("RSX_CAP_DIR");
    if (!base || !*base) base = "rsxcap";
    CreateDirectoryA(base, NULL);
    snprintf(dir, sizeof dir, "%s/frame_%02u", base, capidx);
    CreateDirectoryA(dir, NULL);
    char path[512];

    /* Manifest: every op in submission order with full state. */
    snprintf(path, sizeof path, "%s/manifest.txt", dir);
    FILE* mf = fopen(path, "w");
    if (mf) {
        fprintf(mf, "frame_count=%llu draws=%u backbuffer=%ux%u\n",
                (unsigned long long)s_d3d.frame_count, ndraws, s_d3d.width, s_d3d.height);
        fprintf(mf, "buffer_bank=%u step_count=%d step_phase=before_op\n",
                (u32)s_rb.parity, s_cap_nsteps);
        if(s_pose_on) {
            fprintf(mf,"pose_version=11 scene=0x%08X matched=%u coherent=%u seams=%u seam_conflicts=%u shadow_draws=%u\n",
                s_rb.pose_scene,s_pose_history.matched,s_pose_history.coherent,s_pose_history.seams,s_pose_history.seam_conflicts,s_pose_shadow_draws);
            /* F12 only: confirm the actual uploaded light projection against
             * its untouched CPU snapshot, even when no pose was applied. */
            u32 shown=0,last_cb=~0u;
            for(u32 i=0;i<s_rb.count&&i<MAX_DRAWS&&shown<8;i++) {
                const D3D12DrawRecord* d=&s_rb.draws[i];
                if(!d->is_vp||d->is_clear||d->is_copy||d->vcb_slot>=MAX_DRAWS||d->vcb_slot==last_cb||
                   d->vs_idx<0||d->vs_idx>=s_d3d.vp_vs_n||!s_d3d.vp_vs[d->vs_idx].uses_c03||
                   !(rsx_pose_fragment_flags(fp_snap_code(d))&RSX_POSE_CURRENT_LIGHT_CAMERA))continue;
                const float* original=s_pose_cameras+((size_t)s_rb.parity*MAX_DRAWS+d->vcb_slot)*32+28;
                const float* uploaded=(const float*)((const u8*)s_d3d.vp_cb_mapped+((size_t)s_rb.parity*MAX_DRAWS+d->vcb_slot)*VP_CB_STRIDE)+28;
                fprintf(mf,"pose_shadow op=%u cb=%u vs=%d current_c7=(%g %g %g %g) uploaded_c7=(%g %g %g %g)\n",i,d->vcb_slot,d->vs_idx,
                    original[0],original[1],original[2],original[3],uploaded[0],uploaded[1],uploaded[2],uploaded[3]);
                shown++;last_cb=d->vcb_slot;
            }
        }
        for (int i = 0; i < MAX_OFF_RTS; i++)
            if (s_d3d.off_rt[i].res && s_d3d.off_rt[i].used)
                fprintf(mf, "offrt[%d] off=0x%08X %ux%u dxgi=%u\n", i, s_d3d.off_rt[i].off,
                        s_d3d.off_rt[i].w, s_d3d.off_rt[i].h, s_d3d.off_rt[i].dxgi);
        fprintf(mf, "--- ops ---\n");
        for (u32 d = 0; d < ndraws && d < MAX_DRAWS; d++) {
            const D3D12DrawRecord* r = &s_rb.draws[d];
            if(r->is_copy) {fprintf(mf,"op%03u COPY src=%08X (%u,%u) dst=%08X (%u,%u) %ux%u\n",d,r->copy_src,r->copy_sx,r->copy_sy,r->rt_off,r->copy_dx,r->copy_dy,r->copy_w,r->copy_h);continue;}
            if (r->is_clear)
                fprintf(mf, "op%03u CLEAR rt=0x%08X mrt=0x%X/0x%X/0x%X cc=(%.3f,%.3f,%.3f,%.3f) zeta=0x%08X\n",
                        d, r->rt_off, r->rt_mrt[0], r->rt_mrt[1], r->rt_mrt[2],
                        r->cc[0], r->cc[1], r->cc[2], r->cc[3], r->zeta_off);
            else {
                fprintf(mf, "op%03u DRAW  rt=0x%08X mrt=0x%X/0x%X/0x%X vp=%u,%u,%ux%u fp=0x%08X vs=%d "
                            "n=%u cmask=%X blend=%d bk=0x%X t0=0x%08X t1=0x%08X t2=0x%08X t3=0x%08X"
                            " zeta=0x%08X ztest=%d zfunc=0x%X srgb=%u gamma=0x%X/0x%X\n",
                        d, r->rt_off, r->rt_mrt[0], r->rt_mrt[1], r->rt_mrt[2],
                        r->vp_x, r->vp_y, r->vp_w, r->vp_h,
                        r->fp_addr, r->vs_idx, r->vertex_count, r->cmask, r->blend, r->blend_key,
                        r->tex[0].raw, r->tex[1].raw, r->tex[2].raw, r->tex[3].raw,
                        r->zeta_off, r->dtest, r->dfunc, (r->cull >> 9) & 1u,
                        (r->tex[0].addr >> 20) & 0xFu, (r->tex[1].addr >> 20) & 0xFu);
                const u8* fp_ucode = fp_snap_ptr(r);
                if (fp_ucode) {
                    u32 fp_hdr[4];
                    memcpy(fp_hdr, fp_ucode - 16, sizeof fp_hdr);
                    fprintf(mf, "      fp_snapshot bytes=%u code=%08X source=fp_%08X_%08X_c%X_e%d.hlsl\n",
                            fp_hdr[2], fp_hdr[1], r->fp_addr, fp_hdr[1], dr_cube_mask(r), r->fp_exp32);
                }
                fprintf(mf, "      surf tgt=0x%X offA=0x%08X offB=0x%08X clip=%ux%u%c",
                        r->s_tgt, r->s_offa, r->s_offb, r->s_clipw, r->s_cliph, 10);
                for (int u = 0; u < FP_TEX_UNITS; u++)
                    if (r->tex[u].set)
                        fprintf(mf, "      t%d off=0x%08X fmt=0x%02X (%s%s) %ux%u remap=0x%04X cube=%d mips=%u addr=0x%08X\n", u,
                                r->tex[u].off, r->tex[u].fmt,
                                (r->tex[u].fmt & 0x20) ? "LN" : "SZ",
                                (r->tex[u].fmt & 0x40) ? ",NR" : "",
                                r->tex[u].w, r->tex[u].h, r->tex[u].ctrl1 & 0xFFFF,
                                r->tex[u].cube, r->tex[u].mips, r->tex[u].addr);
            }
        }
        fprintf(mf, "--- step snapshots (state before the indicated op) ---\n");
        for (int i = 0; i < s_cap_nsteps; i++) {
            const CapStep* c = &s_cap_steps[i];
            fprintf(mf, "step_op%04u_%08X.bmp before_op=%u rt=0x%08X %ux%u\n",
                    c->op, c->off, c->op, c->off, c->w, c->h);
        }
        /* Per-draw VP constant banks for the first few offscreen-RT geometry
         * draws: c[0..3] is the projection row-major for most CG VPs; also scan
         * for the largest non-zero slot so a mis-placed MVP is obvious. Records
         * live in the frozen render batch's bank, exactly as bound by the GPU.
         * Only the recording bank (s_d3d.vp_parity) flips on submission;
         * s_rb.parity already names the frame being captured. */
        if (s_d3d.vp_cb_mapped) {
            u32 par = (u32)s_rb.parity;
            fprintf(mf, "--- vp constants (first offscreen geometry draws) ---\n");
            int shown = 0;
            for (u32 d = 0; d < ndraws && d < MAX_DRAWS && shown < 4; d++) {
                const D3D12DrawRecord* r = &s_rb.draws[d];
                if (r->is_clear || !r->rt_off) continue;
                const float* c = (const float*)((const char*)s_d3d.vp_cb_mapped
                    + ((u64)par * MAX_DRAWS + r->vcb_slot) * VP_CB_STRIDE);
                int last_nz = -1; float maxabs = 0.0f;
                for (int i = 0; i < RSX_MAX_VERTEX_CONSTANTS * 4; i++) {
                    float a = c[i] < 0 ? -c[i] : c[i];
                    if (a > 1e-9f) last_nz = i;
                    if (a > maxabs) maxabs = a;
                }
                fprintf(mf, "op%03u vs=%d lastNZslot=%d maxabs=%.3f\n",
                        d, r->vs_idx, last_nz / 4, maxabs);
                if (shown == 0) {   /* full non-zero slot dump for the 1st draw */
                    for (int s = 0; s <= last_nz / 4; s++) {
                        const float* v = c + s * 4;
                        if ((v[0]||v[1]||v[2]||v[3]))
                            fprintf(mf, "   c[%3d] = %9.4f %9.4f %9.4f %9.4f\n",
                                    s, v[0], v[1], v[2], v[3]);
                    }
                }
                shown++;
            }
        }
        /* fp_k[] per draw, exactly as uploaded to b1 (constants are re-read
         * from the ucode at record time, so this is what each draw shaded
         * with), up to the last non-zero slot. Paired with the fp_*.hlsl
         * files below, a pass can be evaluated by hand from the capture. */
        if (s_d3d.vp_fpcb_mapped) {
            u32 par = (u32)s_rb.parity;
            fprintf(mf, "--- fp constants per draw (fp_k[i] = x y z w) ---\n");
            for (u32 d = 0; d < ndraws && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* r = &s_rb.draws[d];
                if (r->is_clear || r->is_copy || !r->is_vp || !r->fp_addr) continue;
                const float* k = (const float*)((const char*)s_d3d.vp_fpcb_mapped
                    + ((u64)par * MAX_DRAWS + r->cb_slot) * VP_FPCB_STRIDE) + FP_K_BASE;
                int last = -1;
                for (int i = 0; i < FP_MAX_K * 4; i++) if (k[i] > 1e-9f || k[i] < -1e-9f) last = i / 4;
                fprintf(mf, "op%03u fp=0x%08X", d, r->fp_addr);
                /* Include the probe's reserved lane even though unit 7 is
                 * intentionally unbound; otherwise its mode is invisible. */
                if (s_lighter_probe) fprintf(mf, " lighter_probe=%s",
                    k[-FP_K_BASE + 7*4+3] == -1234.0f ? "BYPASSED" : "NORMAL");
                /* b1 includes packed-RT interpretation and wrap/gamma modes. */
                for (int u = 0; u < FP_TEX_UNITS; u++) if (r->tex[u].set) {
                    const float* t = k - FP_K_BASE + u * 4;
                    fprintf(mf, " texaddr%d=(%g %g %g %g)", u, t[0], t[1], t[2], t[3]);
                }
                for (int i = 0; i <= last && i < FP_MAX_K; i++)
                    fprintf(mf, " k%d=(%g %g %g %g)", i, k[i*4], k[i*4+1], k[i*4+2], k[i*4+3]);
                fputc('\n', mf);
            }
        }
        /* The first vertices of the first draw of each vertex program, as
         * fetched and converted for the GPU (16 float4 attributes each). A
         * mesh decoded from an unsupported format shows up here as NaN/huge
         * values long before anyone can see why it is missing on screen. */
        if (s_d3d.vp_vb_mapped) {
            u32 par = (u32)s_rb.parity;
            fprintf(mf, "--- first vertices per vertex program (attrs with any non-zero lane) ---\n");
            int seen_vs[64]; int nvs = 0;
            for (u32 d = 0; d < ndraws && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* r = &s_rb.draws[d];
                if (r->is_clear || r->is_copy || !r->is_vp || !r->vertex_count) continue;
                int dup = 0;
                for (int i = 0; i < nvs; i++) if (seen_vs[i] == r->vs_idx) dup = 1;
                if (dup || nvs >= 64) continue;
                seen_vs[nvs++] = r->vs_idx;
                fprintf(mf, "op%03u vs=%d n=%u rt=0x%08X\n", d, r->vs_idx, r->vertex_count, r->rt_off);
                /* This draw's whole constant bank and its program's source,
                 * so the transform can be checked by hand. */
                if (s_d3d.vp_cb_mapped) {
                    const float* c = (const float*)((const char*)s_d3d.vp_cb_mapped
                        + ((u64)par * MAX_DRAWS + r->vcb_slot) * VP_CB_STRIDE);
                    for (int sl = 0; sl < RSX_MAX_VERTEX_CONSTANTS; sl++) {
                        const float* q = c + sl * 4;
                        if (q[0] || q[1] || q[2] || q[3])
                            fprintf(mf, "   c[%3d] = %g %g %g %g\n", sl, q[0], q[1], q[2], q[3]);
                    }
                }
                if (r->vs_idx >= 0 && r->vs_idx < s_d3d.vp_vs_n && s_d3d.vp_vs[r->vs_idx].hlsl) {
                    char vpath[512];
                    snprintf(vpath, sizeof vpath, "%s/vs_%02d.hlsl", dir, r->vs_idx);
                    FILE* vf = fopen(vpath, "w");
                    if (vf) { fprintf(vf, "/* first op%03u */\n%s", d, s_d3d.vp_vs[r->vs_idx].hlsl); fclose(vf); }
                }
                for (u32 v = 0; v < 3 && v < r->vertex_count; v++) {
                    float pulled[16][4];
                    const float* a = (const float*)((const char*)s_d3d.vp_vb_mapped
                        + (u64)par * MAX_VERTICES * 256u + r->vb_byte_offset + (u64)v * 256u);
                    if(s_vf_mapped){vf_capture_vertex(par,r->vb_byte_offset/256+v,pulled);a=&pulled[0][0];}
                    fprintf(mf, "   v%u:", v);
                    for (int at = 0; at < 16; at++) {
                        const float* q = a + at * 4;
                        if (q[0] || q[1] || q[2] || q[3])
                            fprintf(mf, " a%d=(%g %g %g %g)", at, q[0], q[1], q[2], q[3]);
                    }
                    fputc('\n', mf);
                }
            }
        }
        fclose(mf);
    }

    /* Every distinct (address, code) pair: the game can reuse one address for
     * a shadow filter and later a room material within the same frame. */
    {
        extern uint8_t* vm_base;
        extern u32 cellGcmResolveLocated(int local, u32 offset);
        extern int rsx_fp_decompile_buffered_ex(const u8*, u32, u32, u32, char*, u32, u32*);
        extern u32 rsx_fp_code_hash(const u8*, u32);
        char* fph = NULL;
        u32 fph_cap = 0;
        struct { u32 addr, code, cube; int exp32; } seen[512]; int ns = 0;
        for (u32 d = 0; d < ndraws && d < MAX_DRAWS && vm_base; d++) {
            const D3D12DrawRecord* r = &s_rb.draws[d];
            if (r->is_clear || r->is_copy || !r->fp_addr) continue;
            u32 off = cellGcmResolveLocated((r->fp_addr & 3u) == 1, r->fp_addr & ~3u);
            if (off == 0xFFFFFFFFu) continue;
            const u8* ucode = fp_snap_ptr(r) ? fp_snap_ptr(r) : vm_base + off;
            const u32 bytes = r->fp_bytes ? r->fp_bytes : fp_guest_program_size(ucode, NULL);
            if (!bytes) continue;
            const u32 code = rsx_fp_code_hash(ucode, bytes);
            const u32 cube = dr_cube_mask(r);
            int dup = 0;
            for (int i = 0; i < ns; i++)
                if (seen[i].addr == r->fp_addr && seen[i].code == code &&
                    seen[i].cube == cube && seen[i].exp32 == r->fp_exp32) dup = 1;
            if (dup || ns >= 512) continue;
            seen[ns].addr = r->fp_addr; seen[ns].code = code;
            seen[ns].cube = cube; seen[ns++].exp32 = r->fp_exp32;
            u32 nk = 0;
            if (rsx_fp_decompile_alloc(ucode, bytes, r->fp_exp32 ? 0x40u : 0u, cube,
                                       1, &fph, &fph_cap, &nk) <= 0) continue;
            snprintf(path, sizeof path, "%s/fp_%08X_%08X_c%X_e%d.hlsl", dir, r->fp_addr, code, cube, r->fp_exp32);
            FILE* f = fopen(path, "w");
            if (f) { fprintf(f, "/* raw translation: first op%03u, %u constants, cube=0x%X, exp32=%d (fp_constants[i] == fp_k[i]); backend sampling helpers not included */\n%s",
                             d, nk, cube, r->fp_exp32, fph);
                     /* Raw ucode, 16 bytes (one instruction or constant) per line. */
                     const u8* uc = ucode;
                     fprintf(f, "/* raw ucode:\n");
                     u32 rawlen = bytes;
                     for (u32 _b = 0; _b < rawlen; _b += 16) {
                         for (int _k = 0; _k < 16; _k++) fprintf(f, "%02X", uc[_b + _k]);
                         fprintf(f, "%c", 10);
                     }
                     fprintf(f, "*/\n");
                     fclose(f); }
        }
        free(fph);
    }

    /* Diagnostic-only snapshots of the actual guest video planes. */
    if (getenv("CV_VIDEO_CAPTURE")) {
        extern uint8_t* vm_base;
        for (u32 d=0; d<ndraws && d<MAX_DRAWS; ++d) {
            const D3D12DrawRecord* r=&s_rb.draws[d];
            if(r->is_clear || r->is_copy) continue;
            if(s_d3d.vp_cb_mapped && s_d3d.vp_vb_mapped && r->is_vp) {
                u32 par=(u32)s_rb.parity;
                snprintf(path,sizeof path,"%s/op%03u_cb.bin",dir,d);FILE* f=fopen(path,"wb");
                if(f){fwrite((char*)s_d3d.vp_cb_mapped+((u64)par*MAX_DRAWS+r->vcb_slot)*VP_CB_STRIDE,1,VP_CB_STRIDE,f);fclose(f);}
                snprintf(path,sizeof path,"%s/op%03u_vb.bin",dir,d);f=fopen(path,"wb");
                if(f){if(s_vf_mapped){for(u32 v=0;v<r->vertex_count;v++){float pulled[16][4];vf_capture_vertex(par,r->vb_byte_offset/256+v,pulled);fwrite(pulled,256,1,f);}}
                    else fwrite((char*)s_d3d.vp_vb_mapped+(u64)par*MAX_VERTICES*256+r->vb_byte_offset,256,r->vertex_count,f);fclose(f);}
            }
            if(!r->tex[0].set || r->tex[0].w!=1280 || r->tex[0].h!=640) continue;
            for(int u=0;u<3;++u) {
                if(!r->tex[u].set || (r->tex[u].fmt&0x9F)!=0x81) continue;
                snprintf(path,sizeof path,"%s/op%03u_plane%d.pgm",dir,d,u);
                FILE* pf=fopen(path,"wb");
                if(pf) { fprintf(pf,"P5\n%u %u\n255\n",r->tex[u].w,r->tex[u].h);
                    fwrite(vm_base+r->tex[u].off,1,(size_t)r->tex[u].w*r->tex[u].h,pf);fclose(pf); }
            }
        }
    }

    /* Backbuffer (in PRESENT state at capture time) + every used offscreen RT. */
    snprintf(path, sizeof path, "%s/backbuffer.bmp", dir);
    cap_readback_write(s_d3d.render_targets[fi], rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs),
                       DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_PRESENT, fi, path);
    if (s_cv_query_active) {
        D3D12_QUERY_DATA_PIPELINE_STATISTICS* stats=NULL;
        D3D12_RANGE range={0, MAX_DRAWS*sizeof(*stats)};
        if (SUCCEEDED(s_cv_query_readback->lpVtbl->Map(s_cv_query_readback,0,&range,(void**)&stats))) {
            snprintf(path,sizeof path,"%s/gpu-statistics.txt",dir);
            FILE* f=fopen(path,"w");
            if(f) {
                for(u32 d=0;d<ndraws && d<MAX_DRAWS;++d) if(s_cv_query_valid[d])
                    fprintf(f,"op%03u IA=%llu primitives=%llu VS=%llu clip_in=%llu clip_out=%llu PS=%llu\n",d,
                        (unsigned long long)stats[d].IAVertices,(unsigned long long)stats[d].IAPrimitives,
                        (unsigned long long)stats[d].VSInvocations,(unsigned long long)stats[d].CInvocations,
                        (unsigned long long)stats[d].CPrimitives,(unsigned long long)stats[d].PSInvocations);
                fclose(f);
            }
            D3D12_RANGE written={0,0}; s_cv_query_readback->lpVtbl->Unmap(s_cv_query_readback,0,&written);
        }
    }

    {   /* Every 3D method the title has written, with its last value. */
        extern u32 g_rsx_method_val[0x800];
        extern unsigned char g_rsx_method_seen[0x800];
        snprintf(path, sizeof path, "%s/methods.txt", dir);
        FILE* f = fopen(path, "w");
        if (f) {
            for (int m = 0; m < 0x800; m++)
                if (g_rsx_method_seen[m])
                    fprintf(f, "0x%04X = 0x%08X%c", m << 2, g_rsx_method_val[m], 10);
            fclose(f);
        }
    }
    for (int i = 0; i < s_cap_nsteps; i++) {
        const CapStep* c = &s_cap_steps[i];
        snprintf(path, sizeof path, "%s/step_op%04u_%08X.bmp", dir, c->op, c->off);
        cap_readback_write(c->res, c->w, c->h, c->dxgi, D3D12_RESOURCE_STATE_COPY_DEST, fi, path);
    }
    cap_steps_free();
    for (int i = 0; i < MAX_OFF_RTS; i++) {
        OffRT* r = &s_d3d.off_rt[i];
        if (!r->res || !r->used) continue;
        snprintf(path, sizeof path, "%s/rt_%08X.bmp", dir, r->off);
        cap_readback_write(r->res, r->pw, r->ph, r->dxgi, r->st, fi, path);
    }
    printf("[RSXCAP] frame %llu -> %s (%u ops)\n",
           (unsigned long long)s_d3d.frame_count, dir, ndraws);
    if(s_pose_compute_capture){D3D12_RANGE nr={0,0};s_pose_compute.capture->lpVtbl->Unmap(s_pose_compute.capture,0,&nr);s_pose_compute_capture=NULL;}

}

/* On-disk shader cache. FXC takes tens of milliseconds per translated
 * program, and every new area brings dozens -- the stutter when walking into
 * a room. Bytecode is stored under shader_cache/ keyed by a 64-bit hash of
 * the exact HLSL text and target, so a program compiles once ever; any change
 * to the translator changes the text and so the key. SHADER_CACHE=0 turns it
 * off. Safe from the record and render threads (distinct files; temp file
 * then rename). */
static volatile LONG s_shc_hits = 0, s_shc_miss = 0;
/* HITCHDBG=1: where a long frame's time went. */
static volatile LONG s_hd_vs_us = 0, s_hd_vs_n = 0, s_hd_fpi_us = 0, s_hd_fpi_n = 0, s_hd_wait_us = 0;

/* Focused opt-in timings, independent of per-draw PERF instrumentation. */
static int cv_cutscene_diag(void)
{
    static int enabled = -1;
    if (enabled < 0) { const char* e = getenv("CV_CUTSCENE_DIAG"); enabled = e ? atoi(e) != 0 : 0; }
    return enabled;
}
static void cv_cutscene_slow(const char* stage, double start)
{
    const double ms = (perf_now() - start) * 1000.0;
    static volatile LONG lines = 0;
    if (ms >= 2.0 && InterlockedIncrement(&lines) <= 2000)
        fprintf(stderr, "[CV-DIAG] tick=%llu tid=%lu stage=%s ms=%.3f\n",
                (unsigned long long)GetTickCount64(), (unsigned long)GetCurrentThreadId(), stage, ms);
}


/* Fixed for the entire process: never switch one stage to FXC after another
 * has become DXIL. InitOnce also prevents races between shader workers. */
static INIT_ONCE s_shader_once = INIT_ONCE_STATIC_INIT;
static int s_shader_dxc = 0;
static char s_shader_cache_dir[128] = "shader_cache";
static BOOL CALLBACK shader_compiler_init(PINIT_ONCE once, PVOID arg, PVOID* context)
{
    (void)once; (void)arg; (void)context;
    if (s_lighter_probe)
        snprintf(s_shader_cache_dir, sizeof s_shader_cache_dir, "shader_cache/lighter-probe1");
    const char* requested = getenv("SHADER_COMPILER");
    if (!requested || _stricmp(requested, "dxc")) {
        fprintf(stderr, "[SHADER] compiler: FXC (default)%c", 10);
        return TRUE;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL model = { D3D_SHADER_MODEL_6_0 };
    if (!s_d3d.device || FAILED(s_d3d.device->lpVtbl->CheckFeatureSupport(s_d3d.device,
            D3D12_FEATURE_SHADER_MODEL, &model, sizeof model)) || model.HighestShaderModel < D3D_SHADER_MODEL_6_0) {
        fprintf(stderr, "[SHADER] DXC unavailable: Shader Model 6 unsupported; using FXC for this session%c", 10);
        return TRUE;
    }
    /* Check compiler/validator before any guest stage is compiled. Missing
     * DLLs or an unusable installation fall back as a whole, never per-stage. */
    static const char* probes[] = {
        "float4 main(float4 p:POSITION):SV_Position{return p;}",
        "float4 main():SV_Target{return float4(1,0,1,1);}"
    };
    for (int i = 0; i < 2; i++) {
        void* object = NULL; size_t size = 0; char* error = NULL;
        int result = rsx_dxc_compile(probes[i], strlen(probes[i]), i ? "ps_6_0" : "vs_6_0", &object, &size, &error);
        free(object);
        if (result || !size) {
            fprintf(stderr, "[SHADER] DXC initialization failed (%d): %.400s; using FXC for this session%c",
                    result, error ? error : "no diagnostic", 10);
            free(error); return TRUE;
        }
        free(error);
    }
    s_shader_dxc = 1;
    snprintf(s_shader_cache_dir, sizeof s_shader_cache_dir, "shader_cache/%s", rsx_dxc_cache_tag());
    fprintf(stderr, "[SHADER] compiler: DXC EXPERIMENTAL (SM6, strict arithmetic); cache=%s; shadow artifacts may remain%c",
            s_shader_cache_dir, 10);
    return TRUE;
}
static int shader_uses_dxc(void)
{
    InitOnceExecuteOnce(&s_shader_once, shader_compiler_init, NULL, NULL);
    return s_shader_dxc;
}
static const char* shader_cache_dir(void)
{
    shader_uses_dxc();
    /* RSX_GPU_VFETCH programs and pipelines (no input layout) live apart. */
    if (vf_enabled()) {
        static char dir[160]; static volatile LONG made;
        if (!made) {
            const char* suffix=pose_compute_mode()?"vfetch-pose-compute42":(vf_pose_enabled()?"vfetch-pose1":"vfetch");
            if (!strcmp(s_shader_cache_dir, "shader_cache")) snprintf(dir, sizeof dir, "shader_cache/%s",suffix);
            else snprintf(dir, sizeof dir, "%s-%s", s_shader_cache_dir,suffix);
            InterlockedExchange(&made, 1);
        }
        return dir;
    }
    return s_shader_cache_dir;
}

/* Only these baseline guest stages can pair with translated guest stages.
 * Preserve their original uncached FXC route when DXC is not selected. */
static HRESULT shader_compile_cached(const char*, size_t, const char*, const char*, ID3DBlob**, ID3DBlob**);
static HRESULT shader_compile_guest(const char* src, size_t len, const char* name,
    const char* target, ID3DBlob** out, ID3DBlob** error)
{
    if (shader_uses_dxc()) return shader_compile_cached(src, len, name, target, out, error);
    return D3DCompile(src, len, name, NULL, NULL, "main", target, 0, 0, out, error);
}

static HRESULT shader_compile_cached(const char* src, size_t len, const char* name,
                                     const char* target, ID3DBlob** out, ID3DBlob** err)
{
    const int dxc = shader_uses_dxc();
    char sm6_target[16];
    if (dxc) { snprintf(sm6_target, sizeof sm6_target, "%.2s_6_0", target); target = sm6_target; }
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("SHADER_CACHE");
        on = (e && e[0] == '0') ? 0 : 1;
        if (on) { CreateDirectoryA("shader_cache", NULL); CreateDirectoryA(shader_cache_dir(), NULL); }
    }
    *out = NULL; if (err) *err = NULL;
    char path[256] = "";
    if (on) {
        unsigned long long h = 14695981039346656037ull;   /* FNV-1a 64 */
        for (size_t i = 0; i < len; i++) { h ^= (u8)src[i]; h *= 1099511628211ull; }
        for (const char* t = target; *t; t++) { h ^= (u8)*t; h *= 1099511628211ull; }
        snprintf(path, sizeof path, "%s/%s_%016llX.cso", shader_cache_dir(), target, h);
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            ID3DBlob* b = NULL;
            if (n > 32 && SUCCEEDED(D3DCreateBlob((SIZE_T)n, &b)) &&
                fread(b->lpVtbl->GetBufferPointer(b), 1, (size_t)n, f) == (size_t)n &&
                !memcmp(b->lpVtbl->GetBufferPointer(b), "DXBC", 4)) {
                fclose(f);
                InterlockedIncrement(&s_shc_hits);
                *out = b;
                return S_OK;
            }
            if (b) b->lpVtbl->Release(b);
            fclose(f);   /* short or corrupt: recompile and overwrite */
        }
    }
    HRESULT hr;
    if (dxc) {
        void* object = NULL; size_t size = 0; char* error = NULL;
        const int result = rsx_dxc_compile(src, len, target, &object, &size, &error);
        hr = !result && size ? D3DCreateBlob(size, out) : E_FAIL;
        if (SUCCEEDED(hr)) memcpy((*out)->lpVtbl->GetBufferPointer(*out), object, size);
        if (FAILED(hr)) {
            static LONG failures = 0;
            if (InterlockedIncrement(&failures) <= 16)
                fprintf(stderr, "[SHADER] DXC failed on %s: %.600s (restart with normal launcher for FXC)%c",
                        name, error ? error : "no diagnostic", 10);
            if (err && error && SUCCEEDED(D3DCreateBlob(strlen(error) + 1, err)))
                memcpy((*err)->lpVtbl->GetBufferPointer(*err), error, strlen(error) + 1);
        }
        free(object); free(error);
    } else {
        hr = D3DCompile(src, len, name, NULL, NULL, "main", target, 0, 0, out, err);
    }
    if (on && SUCCEEDED(hr) && *out) {
        InterlockedIncrement(&s_shc_miss);
        char tmp[280];
        snprintf(tmp, sizeof tmp, "%s.%lu.tmp", path, (unsigned long)GetCurrentThreadId());
        FILE* f = fopen(tmp, "wb");
        if (f) {
            size_t n = (*out)->lpVtbl->GetBufferSize(*out);
            int ok = fwrite((*out)->lpVtbl->GetBufferPointer(*out), 1, n, f) == n;
            ok = (fclose(f) == 0) && ok;
            if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp);
        }
    }
    return hr;
}

/* Decompile the captured RSX vertex program to HLSL and build the VP PSO
 * (decompiled VS + atlas alpha-test PS). One-shot per program. */
static void compile_vp(void)
{
    extern int rsx_vp_decompile(const uint8_t*, u32, char*, u32);
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st || st->vp_ucode_bytes < 16 || !s_d3d.vp_root_sig) return;

    static char hlsl[64 * 1024];
    { extern void rsx_vp_set_branch_base(u32); rsx_vp_set_branch_base(0); }
    int ni = rsx_vp_decompile(st->vp_ucode, st->vp_ucode_bytes, hlsl, sizeof hlsl);
    /* Does this VP read vp_c[0..3]? Gates the garbage-projection fallback:
     * programs keeping their MVP elsewhere (gcm/cube: c[256..259]) must not
     * have c[0..3] stomped nor the viewport z-lane overridden. */
    s_d3d.vp_uses_c03 = (strstr(hlsl, "vp_c[0]") || strstr(hlsl, "vp_c[1]") ||
                         strstr(hlsl, "vp_c[2]") || strstr(hlsl, "vp_c[3]")) ? 1 : 0;
    if (ni > 0 && vf_enabled() && !vf_wrap_vs(hlsl, sizeof hlsl)) ni = -1;
    if (ni <= 0) { printf("[VP] decompile failed (%d)\n", ni); s_d3d.vp_compiled_bytes = st->vp_ucode_bytes; return; }
    if (getenv("VP_DUMP")) {
        FILE* vf = fopen("vp_dump.hlsl", "w");
        if (vf) { fwrite(hlsl, 1, strlen(hlsl), vf); fclose(vf);
                  printf("[VP] dumped vp_dump.hlsl (%d instrs)\n", ni); }
        printf("[VPRAW] first 3 instrs (%u ucode bytes):\n", st->vp_ucode_bytes);
        for (u32 _q = 0; _q < 48 && _q < st->vp_ucode_bytes; _q += 16)
            printf("[VPRAW]  d0=%02X%02X%02X%02X d1=%02X%02X%02X%02X d2=%02X%02X%02X%02X d3=%02X%02X%02X%02X\n",
                st->vp_ucode[_q+0],st->vp_ucode[_q+1],st->vp_ucode[_q+2],st->vp_ucode[_q+3],
                st->vp_ucode[_q+4],st->vp_ucode[_q+5],st->vp_ucode[_q+6],st->vp_ucode[_q+7],
                st->vp_ucode[_q+8],st->vp_ucode[_q+9],st->vp_ucode[_q+10],st->vp_ucode[_q+11],
                st->vp_ucode[_q+12],st->vp_ucode[_q+13],st->vp_ucode[_q+14],st->vp_ucode[_q+15]);
    }

    /* Pixel shader mirrors dbgfont's FP: sample the atlas coverage at TEXCOORD0
     * (VP output o[7]), alpha-test at 0.5, output the vertex color (o[1]). */
    static const char ps[] =
        "Texture2D tex : register(t0); SamplerState smp : register(s12);\n"   /* point: s(FP_TEX_UNITS + 4) */
        "struct PSIn{ float4 pos:SV_Position; float4 col0:COLOR0; float4 col1:COLOR1; float4 fog:FOG;\n"
        "  float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;\n"
        "  float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7; };\n"
        "float4 main(PSIn i):SV_TARGET{ float cov = tex.Sample(smp, i.t0.xy).r;\n"
        "  if (cov <= 0.5) discard; return float4(i.col0.rgb, 1); }\n";

    ID3DBlob *vb=NULL,*pb=NULL,*e=NULL;
    HRESULT hr = shader_compile_guest(hlsl, strlen(hlsl), "vp", "vs_5_0", &vb, &e);
    if (FAILED(hr)) { printf("[VP] VS compile FAIL: %s\n", e?(const char*)e->lpVtbl->GetBufferPointer(e):"?"); if(e)e->lpVtbl->Release(e); s_d3d.vp_compiled_bytes=st->vp_ucode_bytes; return; }
    hr = shader_compile_guest(ps, sizeof(ps)-1, "vpps", "ps_5_0", &pb, &e);
    if (FAILED(hr)) { printf("[VP] PS compile FAIL: %s\n", e?(const char*)e->lpVtbl->GetBufferPointer(e):"?"); if(e)e->lpVtbl->Release(e); if(vb)vb->lpVtbl->Release(vb); s_d3d.vp_compiled_bytes=st->vp_ucode_bytes; return; }

    /* The decompiled VS declares inputs a0:ATTR0 .. a15:ATTR15 and the HLSL
     * compiler keeps whichever the program body reads. D3D12 requires EVERY VS
     * input to have a matching input-layout element (by semantic name+index),
     * so declare all 16 ATTR slots. (The old single "POSITION" element matched
     * NO VS input once the decompiler switched to ATTR semantics -> PSO failed
     * with E_INVALIDARG for every VP, blanking cellmark's text and vkcube.)
     * The VP path uploads only attrib0 to vp_vb (one float4/vertex), so every
     * slot reads that same float4 at offset 0; attributes other than position
     * therefore alias attrib0 for now (colours/uv wrong until the VP path
     * uploads multiple attributes), but geometry is correct and the PSO is
     * valid. */
    D3D12_INPUT_ELEMENT_DESC il[16];
    for (int _e = 0; _e < 16; _e++) {
        il[_e].SemanticName = "ATTR";
        il[_e].SemanticIndex = _e;
        il[_e].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        il[_e].InputSlot = 0;
        /* 256-byte generic VP vertex: every ATTRi is a float4 slot at i*16
         * (read_vp_vertex converts each enabled RSX attrib by type; disabled
         * slots hold (0,0,0,1)). Covers tiny3d (a0/a3/a8), SDK gcm samples
         * (a0/a1/a2), dbgfont -- no aliasing. */
        il[_e].AlignedByteOffset = (UINT)(_e * 16);
        il[_e].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        il[_e].InstanceDataStepRate = 0;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = s_d3d.vp_root_sig;
    pd.VS.pShaderBytecode = vb->lpVtbl->GetBufferPointer(vb); pd.VS.BytecodeLength = vb->lpVtbl->GetBufferSize(vb);
    pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb); pd.PS.BytecodeLength = pb->lpVtbl->GetBufferSize(pb);
    pd.InputLayout.pInputElementDescs = il; pd.InputLayout.NumElements = vf_enabled() ? 0 : 16;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    /* Depth test ON (LESS_EQUAL, matching the guest's rsxtiny_DepthTestFunc 515 /
     * CELL_GCM_LEQUAL). Without it the cube's faces drew in submission order and
     * back faces overwrote the front -> you saw through the near face to the
     * darker interior. The depth buffer is bound + cleared to 1.0 each frame. */
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleDesc.Count = 1;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_vp);

    /* Second PSO: same VS but a COLOUR-only PS (no texture sample, no alpha
     * discard) for untextured 3D geometry (vkcube's cube). The atlas PS above
     * samples a texture and discards where coverage<=0.5, which blanks any draw
     * with no atlas bound. */
    {
        static const char ps_col[] =
            "struct PSIn{ float4 pos:SV_Position; float4 col0:COLOR0; float4 col1:COLOR1; float4 fog:FOG;\n"
            "  float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;\n"
            "  float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7; };\n"
            "float4 main(PSIn i):SV_TARGET{ return float4(i.col0.rgb, 1); }\n";
        ID3DBlob *pcb=NULL,*ec=NULL;
        if (SUCCEEDED(shader_compile_guest(ps_col, sizeof(ps_col)-1, "vppsc",
                                           "ps_5_0", &pcb, &ec)) && pcb) {
            pd.PS.pShaderBytecode = pcb->lpVtbl->GetBufferPointer(pcb);
            pd.PS.BytecodeLength  = pcb->lpVtbl->GetBufferSize(pcb);
            HRESULT hr2 = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pd, &IID_ID3D12PipelineState,
                (void**)&s_d3d.pipeline_state_vp_color);
            printf(SUCCEEDED(hr2) ? "[VP] colour pipeline ready\n"
                                  : "[VP] colour PSO FAIL (0x%08lX)\n", hr2);
            pcb->lpVtbl->Release(pcb);
        }
        if (ec) ec->lpVtbl->Release(ec);
    }
    /* Keep the VS bytecode for guest-FP PSO builds (vp_get_fp_pso); bump the
     * generation so cached FP PSOs built against the old VS are rebuilt. */
    if (s_d3d.vp_vs_blob) s_d3d.vp_vs_blob->lpVtbl->Release(s_d3d.vp_vs_blob);
    s_d3d.vp_vs_blob = vb;
    s_d3d.vp_gen++;
    pb->lpVtbl->Release(pb);
    s_d3d.vp_compiled_bytes = st->vp_ucode_bytes;
    if (SUCCEEDED(hr)) { s_d3d.vp_ready = 1; printf("[VP] pipeline ready (%d instrs)\n", ni); }
    else {
        printf("[VP] PSO creation FAIL (0x%08lX)\n", hr);
        /* Drain the debug layer's message queue to get the EXACT validation
         * reason (E_INVALIDARG is otherwise opaque). One-shot. */
        ID3D12InfoQueue* iq = NULL;
        if (SUCCEEDED(s_d3d.device->lpVtbl->QueryInterface(
                s_d3d.device, &IID_ID3D12InfoQueue, (void**)&iq)) && iq) {
            UINT64 n = iq->lpVtbl->GetNumStoredMessages(iq);
            for (UINT64 mi = 0; mi < n; mi++) {
                SIZE_T len = 0;
                iq->lpVtbl->GetMessage(iq, mi, NULL, &len);
                D3D12_MESSAGE* m = (D3D12_MESSAGE*)malloc(len);
                if (m && SUCCEEDED(iq->lpVtbl->GetMessage(iq, mi, m, &len)))
                    printf("[VP][DBG] %s\n", m->pDescription);
                free(m);
            }
            iq->lpVtbl->Release(iq);
        }
    }
}

/* Build (or fetch) a PSO pairing the current decompiled VS with the guest's
 * FRAGMENT program at fp_addr: read the FP ucode from guest memory, decompile
 * to HLSL (rsx_fp_decompiler), compile, and cache. This replaces the two
 * hardcoded pixel shaders for draws whose FP we can translate -- e.g.
 * gcm/cube's plasma FP `c = tex2D(t0, uv).x; out = (c, 0, c, 1)`. */
#include "rsx_fp_decompiler.h"

/* Hash + compile the CURRENT rsx_state vertex program into the VS cache;
 * returns the cache slot (or -1). Called at draw-record time so each draw
 * carries the VP that was loaded when it was submitted. */
static u32 vp_hash_ucode(const u8* p, u32 n)
{
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h ? h : 1u;
}

/* ---------------------------------------------------------------------------
 * Vertex textures. NV40 vertex texture units read 2D float textures only;
 * TXL fetches them unfiltered at LOD 0. Without them a transform program's
 * TXL decompiles to zero -- Code Veronica fetches each room piece's world
 * matrix rows that way, so the whole room collapsed to the origin.
 * -----------------------------------------------------------------------*/
static int vtex_format(u32 fmt, DXGI_FORMAT* dxgi, u32* bpt, u32* comp)
{
    switch (fmt & 0x9F) {
    case 0x9A: *dxgi = DXGI_FORMAT_R16G16B16A16_FLOAT; *bpt = 8;  *comp = 2; return 1;
    case 0x9B: *dxgi = DXGI_FORMAT_R32G32B32A32_FLOAT; *bpt = 16; *comp = 4; return 1;
    case 0x9C: *dxgi = DXGI_FORMAT_R32_FLOAT;          *bpt = 4;  *comp = 4; return 1;
    case 0x9F: *dxgi = DXGI_FORMAT_R16G16_FLOAT;       *bpt = 4;  *comp = 2; return 1;
    default:   return 0;
    }
}

/* Units that are enabled, 2D, non-cube and in a format we can bind. */
static u32 vtex_mask_of(const rsx_state* st)
{
    u32 mask = 0;
    if (!st || getenv("VTEX_OFF")) return 0;
    for (u32 u = 0; u < RSX_MAX_VERTEX_TEXTURES; u++) {
        const rsx_texture_state* t = &st->vertex_textures[u];
        if (!(t->control0 >> 31)) continue;
        if (((t->format >> 4) & 0xF) != 2 || ((t->format >> 2) & 1)) continue;
        DXGI_FORMAT d; u32 b, c;
        u32 w = t->image_rect >> 16, h = t->image_rect & 0xFFFF;
        if (!vtex_format((t->format >> 8) & 0xFF, &d, &b, &c) || !w || !h || w > 4096 || h > 4096) {
            static int n = 0; if (n++ < 8)
                fprintf(stderr, "[VTEX] unit %u unsupported fmt=0x%08X %ux%u%c", u, t->format, w, h, 10);
            continue;
        }
        mask |= 1u << u;
    }
    return mask;
}

/* Per-frame copy of every fragment program's ucode, taken when the draw is
 * recorded. Code Veronica streams its fragment programs through a small ring
 * in local memory and rewrites a slot as soon as the draw that used it is in
 * the FIFO: by the time render_frame built the pipelines, most slots held a
 * LATER program, so draws were shaded by somebody else's program (or by a
 * half-written one that decompiled to nothing and painted the scene black).
 * Identical bytes at the same address reuse the previous copy. */
static u8*  s_fp_arena = NULL;
static u32  s_fp_arena_used = 0;
#define FP_ARENA_CAP  (8u << 20)
#define FP_ARENA_PAD  16u
#define FP_SNAP_MAGIC 0x50534E46u   /* "FNSP": header before each snapshot */
typedef struct { u32 addr, len, snap; } FPSnap;
static FPSnap s_fp_snaps[64];
static u32 s_fp_nsnap = 0;

/* The first guest page is already readable for a resolved shader address.
 * Small programs finish there without a host query. Before extending the
 * scan, verify each additional readable committed region and never cross the
 * guest address space or the common safety ceiling. */
static u32 fp_guest_program_size(const u8* src, u32* constants)
{
    extern uint8_t* vm_base;
    if (constants) *constants = 0;
    if (!src || !vm_base || (uintptr_t)src < (uintptr_t)vm_base) return 0;
    const u64 ea = (u64)((uintptr_t)src - (uintptr_t)vm_base);
    if (ea >= 0x100000000ull) return 0;
    const u64 remaining = 0x100000000ull - ea;
    const u32 limit = remaining < RSX_FP_PROGRAM_LIMIT ? (u32)remaining : RSX_FP_PROGRAM_LIMIT;
    u32 bound = 4096u - ((u32)ea & 4095u);
    if (bound > limit) bound = limit;
    /* Guest pages are committed on first touch and never decommitted, so
     * one answer per 4 KB page holds for the whole run. The query per draw
     * was ~10% of the FIFO thread in heavy rooms. */
    static volatile LONG s_fp_readable[(1u << 20) / 32];
    for (;;) {
        u32 len = rsx_fp_program_info(src, bound, constants);
        if (len) return len;
        if (bound == limit) break;
        MEMORY_BASIC_INFORMATION mb;
        const u8* next = src + bound;
        { u32 page = (u32)((ea + bound) >> 12), run = 0;
          while (page < (1u << 20) && (s_fp_readable[page >> 5] & (1u << (page & 31))) &&
                 run < limit - bound) { run += 4096u - (u32)((ea + bound + run) & 4095u); page++; }
          if (run) { bound += run > limit - bound ? limit - bound : run; continue; } }
        if (!VirtualQuery(next, &mb, sizeof mb) || mb.State != MEM_COMMIT ||
            (mb.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(mb.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) break;
        const size_t readable = (const u8*)mb.BaseAddress + mb.RegionSize - next;
        if (!readable) break;
        { const u64 first = (ea + bound) >> 12, end = ((u64)((const u8*)mb.BaseAddress + mb.RegionSize - vm_base)) >> 12;
          for (u64 pg = first; pg < end && pg < (1u << 20) && pg < first + 4096; pg++)
              InterlockedOr(&s_fp_readable[pg >> 5], (LONG)(1u << (pg & 31))); }
        bound += readable > limit - bound ? limit - bound : (u32)readable;
    }
    { static int warnings = 0; if (warnings++ < 16)
        fprintf(stderr, "[FP] incomplete program at guest 0x%08X (readable scan %u, safety limit %u bytes); rejected%c",
                (u32)ea, bound, RSX_FP_PROGRAM_LIMIT, 10); }
    return 0;
}

static void fp_snapshot(D3D12DrawRecord* dr)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    dr->fp_snap = 0;
    dr->fp_bytes = 0;
    if (!dr->fp_addr || !vm_base) return;
    u32 ea = cellGcmResolveLocated((dr->fp_addr & 0x3u) == 1, dr->fp_addr & ~0x3u);
    if (ea == 0xFFFFFFFFu) return;
    const u8* src = vm_base + ea;
    /* The same bytes as a snapshot already taken at this address this
     * frame: the same program, so the same length and per-program data.
     * Parsing it again was most of this function, ~9000 times a frame. */
    { static int off = -1; if (off < 0) off = getenv("FP_NOSNAP") ? 1 : 0;
      if (!off)
          for (u32 i = 0; i < s_fp_nsnap; i++) {
              const FPSnap* q = &s_fp_snaps[i];
              if (q->addr == dr->fp_addr && !memcmp(s_fp_arena + q->snap - 1, src, q->len)) {
                  dr->fp_bytes = q->len; dr->fp_snap = q->snap; return;
              }
          } }
    u32 constants = 0;
    u32 len = fp_guest_program_size(src, &constants);
    if (!len) return;
    dr->fp_bytes = len;
    { static int off = -1; if (off < 0) off = getenv("FP_NOSNAP") ? 1 : 0; if (off) return; }
    for (u32 i = 0; i < s_fp_nsnap; i++) {
        const FPSnap* q = &s_fp_snaps[i];
        if (q->addr == dr->fp_addr && q->len == len &&
            !memcmp(s_fp_arena + q->snap - 1, src, len)) { dr->fp_snap = q->snap; return; }
    }
    if (!s_fp_arena) {
        s_fp_arena = (u8*)calloc(1, FP_ARENA_CAP + FP_ARENA_PAD);
        if (!s_fp_arena) return;
    }
    /* [16-byte header: magic, code hash, size][ucode][16 zero bytes]
     * [16: inline constant count, texture-unit mask][constants]. The header
     * lets vp_get_fp_pso take the hash computed once here instead of
     * re-hashing the program for every pipeline lookup (2x per draw); the
     * tail gives vp_record_cb and the prepass what they derived per draw. */
    static float k_tmp[FP_MAX_K * 4];
    const int nk = rsx_fp_extract_consts(src, len, k_tmp, FP_MAX_K);
    if (s_fp_arena_used + 16 + len + 16 + 16 + (u32)nk * 16u > FP_ARENA_CAP) {
        static int w = 0;
        if (w++ < 4) fprintf(stderr, "[D3D12] WARNING: FP snapshot arena full%c", 10);
        return;
    }
    u32 at = s_fp_arena_used + 16;
    memcpy(s_fp_arena + at, src, len);
    memset(s_fp_arena + at + len, 0, 16);   /* stop marker room */
    { u32 hdr[4] = { FP_SNAP_MAGIC, rsx_fp_code_hash(s_fp_arena + at, len), len, constants };
      memcpy(s_fp_arena + at - 16, hdr, 16); }
    { u32 tail[4] = { (u32)nk, rsx_fp_tex_used_mask(src, len), 0, 0 };
      memcpy(s_fp_arena + at + len + 16, tail, 16);
      memcpy(s_fp_arena + at + len + 32, k_tmp, (size_t)nk * 16u); }
    s_fp_arena_used = (at + len + 32 + (u32)nk * 16u + 15u) & ~15u;
    dr->fp_snap = at + 1;
    /* Newest first; the table is a small most-recent window. */
    u32 n = s_fp_nsnap < 64 ? s_fp_nsnap : 63;
    memmove(&s_fp_snaps[1], &s_fp_snaps[0], sizeof(FPSnap) * n);
    s_fp_snaps[0].addr = dr->fp_addr; s_fp_snaps[0].len = len; s_fp_snaps[0].snap = at + 1;
    s_fp_nsnap = n + 1;
}

static const u8* fp_snap_ptr(const D3D12DrawRecord* dr)
{
    return (dr->fp_snap && s_rb.fp_arena) ? s_rb.fp_arena + dr->fp_snap - 1 : NULL;
}
static u32 fp_snap_code_in(const D3D12DrawRecord* dr,const u8* arena)
{
    if(dr->fp_snap<17||!arena)return 0;
    const u8* snap=arena+dr->fp_snap-1;
    u32 header[4];memcpy(header,snap-16,sizeof header);
    return header[0]==FP_SNAP_MAGIC&&header[2]==dr->fp_bytes?header[1]:0;
}
static u32 fp_snap_code(const D3D12DrawRecord* dr)
{
    return fp_snap_code_in(dr,s_rb.fp_arena);
}

/* Per-frame copy of every vertex texture's texels (guest byte order, packed
 * rows), taken when the draw is recorded. Reset with the draw list. A copy is
 * only made when the guest bytes differ from the last one taken at that
 * address: Code Veronica binds the same 1 MB matrix texture for ~50 draws a
 * frame, and copying it per draw ran the game at 0.3 fps. */
static u8*  s_vtex_arena = NULL;
static u32  s_vtex_arena_used = 0, s_vtex_arena_cap = 0;
typedef struct { u32 ea, len, data_off, hash, epoch; } VTexSnap;
/* FIFO drain pass the snapshot was last validated in (cellGcmSys.c). Every
 * draw of one pass was queued before that pass read `put`, and a title may
 * not rewrite data the RSX has yet to read -- so once a snapshot is checked
 * in a pass, it holds for the rest of the pass. Checking per draw cost a
 * GetWriteWatch over the whole range (Code Veronica's matrix texture: 1 MB,
 * 256 pages) on every draw that bound it; with the lighter's cube shadow
 * that was most of the FIFO drain. VTEX_EPOCH=0 restores the per-draw check. */
extern volatile u32 g_gcm_drain_epoch;
static VTexSnap s_vtex_snap[16];
static u32 s_vtex_nsnap = 0;

/* Guest-memory write tracking (vm_base is reserved MEM_WRITE_WATCH by the
 * host). 0 = no page of [ea, ea+len) was written since the last reset, so a
 * snapshot of it is still exact; 1 = maybe written (or tracking unavailable). */
static int s_ww_ok = -1;
static u64 s_vtex_cmp_bytes, s_vtex_copy_bytes, s_vtex_skip;
static int vm_range_written(u32 ea, u32 len)
{
    extern uint8_t* vm_base;
    if (s_ww_ok == 0 || !len) return 1;
    PVOID a[1]; ULONG_PTR n = 1; ULONG g;
    if (GetWriteWatch(0, vm_base + ea, len, a, &n, &g) != 0) { s_ww_ok = 0; return 1; }
    s_ww_ok = 1;
    return n != 0;
}
static void vm_range_watch_reset(u32 ea, u32 len)
{
    extern uint8_t* vm_base;
    if (s_ww_ok == 1 && len) ResetWriteWatch(vm_base + ea, len);
}

static void vtex_record(D3D12DrawRecord* dr, const rsx_state* st)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    dr->vtex_mask = 0;
    u32 mask = vtex_mask_of(st);
    for (u32 u = 0; u < RSX_MAX_VERTEX_TEXTURES && mask && vm_base; u++) {
        if (!((mask >> u) & 1u)) continue;
        const rsx_texture_state* t = &st->vertex_textures[u];
        DXGI_FORMAT d; u32 bpt, comp;
        u32 fmt = (t->format >> 8) & 0xFF;
        vtex_format(fmt, &d, &bpt, &comp);
        u32 w = t->image_rect >> 16, h = t->image_rect & 0xFFFF;
        u32 row = w * bpt, pitch = t->control3 & 0xFFFFF;
        if (!pitch) pitch = row;
        if (pitch < row) continue;
        u32 ea = cellGcmResolveLocated((t->format & 3) != 2, t->offset);
        if (ea == 0xFFFFFFFFu) continue;
        u32 len = row * h;
        u32 span = (h - 1) * pitch + row;   /* guest bytes the texels occupy */
        const u8* src = vm_base + ea;
        /* Same bytes as the latest copy of this address? Reuse it. Pages that
         * nobody wrote since that copy cannot differ, so skip the compare. */
        int reuse = -1;
        for (int i = (int)s_vtex_nsnap - 1; i >= 0 && reuse == -1; i--) {
            VTexSnap* q = &s_vtex_snap[i];
            if (q->ea != ea || q->len != len) continue;
            { static int ep = -1; if (ep < 0) { const char* e = getenv("VTEX_EPOCH"); ep = (e && *e == '0') ? 0 : 1; }
              if (ep && q->epoch == g_gcm_drain_epoch) { reuse = i; s_vtex_skip++; break; } }
            if (!vm_range_written(ea, span)) { reuse = i; s_vtex_skip++; q->epoch = g_gcm_drain_epoch; break; }
            vm_range_watch_reset(ea, span);   /* before reading: later writes stay visible */
            const u8* a = s_vtex_arena + q->data_off;
            int same = 1;
            s_vtex_cmp_bytes += len;
            if (pitch == row) same = !memcmp(a, src, len);
            else for (u32 y = 0; y < h && same; y++)
                same = !memcmp(a + (size_t)y * row, src + (size_t)y * pitch, row);
            reuse = same ? i : -2;   /* only the latest copy counts */
            if (same) q->epoch = g_gcm_drain_epoch;
        }
        if (reuse >= 0) {
            dr->vtx[u].fmt = fmt; dr->vtx[u].w = w; dr->vtx[u].h = h;
            dr->vtx[u].row_bytes = row; dr->vtx[u].data_off = s_vtex_snap[reuse].data_off;
            dr->vtx[u].hash = s_vtex_snap[reuse].hash;
            dr->vtex_mask |= 1u << u;
            continue;
        }
        if (s_vtex_arena_used + len > s_vtex_arena_cap) {
            u32 cap = s_vtex_arena_cap ? s_vtex_arena_cap * 2 : (1u << 20);
            while (cap < s_vtex_arena_used + len) cap *= 2;
            u8* p = (u8*)realloc(s_vtex_arena, cap);
            if (!p) continue;
            s_vtex_arena = p; s_vtex_arena_cap = cap;
        }
        u8* dst = s_vtex_arena + s_vtex_arena_used;
        if (reuse == -1) vm_range_watch_reset(ea, span);   /* first copy of this range */
        s_vtex_copy_bytes += len;
        if (pitch == row) memcpy(dst, src, len);
        else for (u32 y = 0; y < h; y++) memcpy(dst + (size_t)y * row, src + (size_t)y * pitch, row);
        u64 hh = 1469598103934665603ull;   /* FNV-1a over 8-byte words */
        for (u32 i = 0; i + 8 <= len; i += 8) { u64 v; memcpy(&v, dst + i, 8); hh ^= v; hh *= 1099511628211ull; }
        u32 hash = (u32)(hh ^ (hh >> 32)) ^ fmt ^ (w << 16) ^ h;
        if (s_vtex_nsnap == 16) { memmove(s_vtex_snap, s_vtex_snap + 1, 15 * sizeof(VTexSnap)); s_vtex_nsnap = 15; }
        s_vtex_snap[s_vtex_nsnap].ea = ea; s_vtex_snap[s_vtex_nsnap].len = len;
        s_vtex_snap[s_vtex_nsnap].data_off = s_vtex_arena_used; s_vtex_snap[s_vtex_nsnap].hash = hash;
        s_vtex_snap[s_vtex_nsnap].epoch = g_gcm_drain_epoch;
        s_vtex_nsnap++;
        dr->vtx[u].fmt = fmt; dr->vtx[u].w = w; dr->vtx[u].h = h;
        dr->vtx[u].hash = hash;
        dr->vtx[u].row_bytes = row; dr->vtx[u].data_off = s_vtex_arena_used;
        s_vtex_arena_used += (len + 15u) & ~15u;
        dr->vtex_mask |= 1u << u;
        { static int n = 0; if (n++ < 4)
            fprintf(stderr, "[VTEX] unit %u off=0x%08X fmt=0x%02X %ux%u pitch=%u%c",
                    u, t->offset, fmt, w, h, pitch, 10); }
    }
}

static void render_wait_idle(void);   /* render thread, see render_frame() */

/* Background VS compile. A first-time vertex program took FXC 30-170 ms on
 * the thread recording the guest's commands -- and a room brings a dozen at
 * once, which held the game for over half a second. The slot is published
 * with vs = NULL and pending set; draws that need it are left out (or, if
 * urgent, waited for) until the blob lands. */
/* Shader preload (pl_*, after vp_get_fp_pso): a vertex program listed from an
 * earlier session, built on a preload thread. */
typedef struct PlVs {
    u32 hash, vstart, vtex_mask, len;
    const u8* ucode;
    ID3DBlob* vs;                /* the built program; NULL until ready or if it failed */
    char* hlsl;
    int uses_c03;
    volatile LONG state;         /* 0 queued, 1 ready, 2 failed */
} PlVs;
static const PlVs* pl_vs_ready(u32 hash);
static void pl_record_vs(u32 hash, u32 vstart, u32 vtex_mask, const u8* ucode, u32 len);
static void pl_record_fp(const VPFPEntry* key, const u8* ucode, u32 len);

/* RSX_GPU_VFETCH: turn the decompiled VS (inputs a0..a15 : ATTRi) into one
 * that pulls its inputs. Record k = (guest vertex index, descriptor); each
 * descriptor attribute is (base, stride, format, frequency, constant), and
 * the bytes are the guest's own, big-endian. Same decoding as
 * rsx_fetch_vertex_planned. Returns 0 if the program does not have the
 * expected entry point. */
static int vf_wrap_vs(char* hlsl, size_t cap)
{
    static const char entry[] = "VSOutput main(VSInput input) {";
    static const char renamed[] = "VSOutput vf_main(VSInput input) {";
    static const char pull[] =
        "ByteAddressBuffer vf_buf : register(t24);\n"
        "uint vf_ld(uint a) { uint b = a & ~3u; uint s = (a & 3u) * 8u; uint w = vf_buf.Load(b);\n"
        "    if (s != 0u) w = (w >> s) | (vf_buf.Load(b + 4u) << (32u - s)); return w; }\n"
        "uint vf_bs32(uint w) { return (w >> 24) | ((w >> 8) & 0xFF00u) | ((w << 8) & 0xFF0000u) | (w << 24); }\n"
        "uint vf_bs16(uint w) { return ((w & 0xFFu) << 8) | ((w >> 8) & 0xFFu); }\n"
        "float vf_comp(uint t, uint a, uint k) {\n"
        "    if (t == 3u) return f16tof32(vf_bs16(vf_ld(a + 2u * k)));\n"
        "    if (t == 4u) return float(vf_ld(a + k) & 0xFFu) / 255.0;\n"
        "    if (t == 7u) return float(vf_ld(a + k) & 0xFFu);\n"
        "    if (t == 1u) return float(asint(vf_bs16(vf_ld(a + 2u * k)) << 16) >> 16) / 32767.0;\n"
        "    if (t == 5u) return float(asint(vf_bs16(vf_ld(a + 2u * k)) << 16) >> 16);\n"
        "    if (t == 15u) return asfloat(vf_ld(a + 4u * k));\n"
        "    return asfloat(vf_bs32(vf_ld(a + 4u * k)));\n"
        "}\n"
        "float4 vf_fetch(uint d, uint i, uint vi) {\n"
        "    uint da = VF_DESC_OFF_U + d * 512u + i * 32u;\n"
        "    uint4 h = vf_buf.Load4(da); float4 o = asfloat(vf_buf.Load4(da + 16u));\n"
        "    if ((h.z & 0x80000000u) == 0u) return o;\n"
        "    uint t = h.z & 15u, n = (h.z >> 4) & 7u;\n"
        "    uint ei = h.w != 0u ? (((h.z >> 7) & 1u) != 0u ? vi % h.w : vi / h.w) : vi;\n"
        "    uint a = h.x + ei * h.y;\n"
        "    if (t == 6u) { uint v = vf_bs32(vf_ld(a));\n"
        "        return float4(float(asint(v << 21) >> 21) / 1023.0, float(asint(v << 10) >> 21) / 1023.0,\n"
        "                      float(asint(v) >> 22) / 511.0, 1.0); }\n"
        "    [unroll] for (uint k = 0u; k < 4u; k++) if (k < n) o[k] = vf_comp(t, a, k);\n"
        "    return o;\n"
        "}\n"
        "#if VF_POSE_ENABLED\n" RSX_POSE_GPU_HLSL "#endif\n";
    static const char tail[] =
        "cbuffer VFDraw : register(b2) { uint vf_first; };\n"
        "VSOutput main(uint vf_vid : SV_VertexID) {\n"
        "    uint2 rec = vf_buf.Load2((vf_first + vf_vid) * 8u);\n"
        "    VSInput input;\n"
        "    input.a0 = vf_fetch(rec.y, 0u, rec.x);   input.a1 = vf_fetch(rec.y, 1u, rec.x);\n"
        "    input.a2 = vf_fetch(rec.y, 2u, rec.x);   input.a3 = vf_fetch(rec.y, 3u, rec.x);\n"
        "    input.a4 = vf_fetch(rec.y, 4u, rec.x);   input.a5 = vf_fetch(rec.y, 5u, rec.x);\n"
        "    input.a6 = vf_fetch(rec.y, 6u, rec.x);   input.a7 = vf_fetch(rec.y, 7u, rec.x);\n"
        "    input.a8 = vf_fetch(rec.y, 8u, rec.x);   input.a9 = vf_fetch(rec.y, 9u, rec.x);\n"
        "    input.a10 = vf_fetch(rec.y, 10u, rec.x); input.a11 = vf_fetch(rec.y, 11u, rec.x);\n"
        "    input.a12 = vf_fetch(rec.y, 12u, rec.x); input.a13 = vf_fetch(rec.y, 13u, rec.x);\n"
        "    input.a14 = vf_fetch(rec.y, 14u, rec.x); input.a15 = vf_fetch(rec.y, 15u, rec.x);\n"
        "#if VF_POSE_ENABLED\n"
        "    input.a0=vf_pose_position(vf_first+vf_vid,input.a0);\n"
        "#endif\n"
        "    return vf_main(input);\n"
        "}\n";
    char* at = strstr(hlsl, entry);
    if (!at || strstr(at + 1, entry)) return 0;
    char define[320];
    snprintf(define, sizeof define, "#define VF_DESC_OFF_U %lluu\n#define VF_POSE_OFF_U %lluu\n#define VF_POSE_ENABLED %d\n#define VF_POSE_STRIDE_U %uu\n#define VF_POSE_COMPUTE_ENABLED %d\n", (unsigned long long)VF_DESC_OFF,(unsigned long long)VF_POSE_OFF,vf_pose_enabled(),vf_pose_stride(),pose_compute_mode());
    const size_t len = strlen(hlsl), head = (size_t)(at - hlsl), rest = len - head - (sizeof entry - 1);
    const size_t need = len + strlen(define) + sizeof pull + sizeof renamed + sizeof tail;
    if (need > cap) return 0;
    char* out = (char*)malloc(need);
    if (!out) return 0;
    size_t o = 0;
    memcpy(out + o, hlsl, head); o += head;
    memcpy(out + o, define, strlen(define)); o += strlen(define);
    memcpy(out + o, pull, sizeof pull - 1); o += sizeof pull - 1;
    memcpy(out + o, renamed, sizeof renamed - 1); o += sizeof renamed - 1;
    memcpy(out + o, at + sizeof entry - 1, rest); o += rest;
    memcpy(out + o, tail, sizeof tail); o += sizeof tail - 1;
    memcpy(hlsl, out, o + 1);
    free(out);
    if (getenv("VF_DUMP")) { static volatile LONG n; if (InterlockedIncrement(&n) <= 3) {
        char pn[64]; snprintf(pn, sizeof pn, "vf_vs_%ld.hlsl", (long)n);
        FILE* f = fopen(pn, "w"); if (f) { fputs(hlsl, f); fclose(f); } } }
    return 1;
}

typedef struct VsJob { int slot; u32 hash; char* hlsl; } VsJob;
static VOID CALLBACK vs_compile_job(PTP_CALLBACK_INSTANCE inst, PVOID ctx)
{
    (void)inst;
    VsJob* j = (VsJob*)ctx;
    ID3DBlob* vb = NULL; ID3DBlob* e = NULL;
    const double t0 = perf_now();
    HRESULT hr = shader_compile_cached(j->hlsl, strlen(j->hlsl), "guest_vp2", "vs_5_0", &vb, &e);
    InterlockedAdd(&s_hd_vs_us, (LONG)((perf_now() - t0) * 1e6)); InterlockedIncrement(&s_hd_vs_n);
    if (e) e->lpVtbl->Release(e);
    if (FAILED(hr) || !vb) {
        static int _e = 0; if (_e++ < 4) printf("[VP] per-draw VS compile FAIL (hash=0x%08X)%c", j->hash, 10);
        vb = NULL;
    }
    VPVSEntry* v = &s_d3d.vp_vs[j->slot];
    /* The slot cannot be replaced while pending (see the LRU pick). A failed
     * compile leaves vs NULL: its draws fall back to the primary program, as
     * an inline failure always did (-1). */
    if (vb) InterlockedExchangePointer((PVOID volatile*)&v->vs, vb);
    InterlockedExchange(&v->pending, 0);
    free(j->hlsl); free(j);
}

static int vp_get_vs(const rsx_state* st)
{
    extern int rsx_vp_decompile_ex(const uint8_t*, u32, u32, char*, u32);
    extern u32 rsx_vp_program_size_instrs(const u8*, u32);
    extern void rsx_vp_set_branch_base(u32);
    if (!st || st->vp_ucode_bytes < 16) return -1;
    const u32 vtex_mask = vtex_mask_of(st);
    /* Start at the instruction SET_TRANSFORM_PROGRAM_START selects, not at 0.
     * The microcode store holds every resident program -- this title keeps its
     * scene, fluid, caustics and droplet programs in it at once -- so
     * decompiling from 0 ran the scene's transform for every draw. That is why
     * the fluid's 114300 vertices rasterized nothing: they were being pushed
     * through the wrong program.
     * VP_START_OFF=1 restores the old always-from-zero behaviour. */
    u32 vstart = 0;
    { static int off = -1; if (off < 0) off = getenv("VP_START_OFF") ? 1 : 0;
      if (!off) vstart = st->transform_program_start * 16u; }
    if (vstart >= st->vp_ucode_bytes) vstart = 0;
    const u8* vuc = st->vp_ucode + vstart;
    u32 vlen = st->vp_ucode_bytes - vstart;
    /* Up to the program's END only: the decompiler stops there, and hashing
     * the rest of the store keyed the program on whatever other programs sat
     * after it -- Code Veronica's outdoor areas built a "new" VS (and every
     * FP pipeline with it) each time a later slot changed, and the scene
     * skipped frames while they compiled. */
    /* V34: defer the END scan until the existing generation/start/texture
     * cache misses. Native mode and CV_VP_SCAN_DEFER=0 keep reference order. */
    static int defer_scan=-1;
    if(defer_scan<0){const char* fps=getenv("CV_FPS_TEST_60"),*off=getenv("CV_VP_SCAN_DEFER");
        defer_scan=fps&&!strcmp(fps,"1")&&!(off&&!strcmp(off,"0"));}
    if(!defer_scan){u32 ni=rsx_vp_program_size_instrs(vuc,vlen);if(ni)vlen=ni*16;}
    /* Same store generation, start and texture units as the last lookup: same
     * program. Hashing up to 16 KB of ucode on every draw was a measurable
     * slice of Code Veronica's 2300-draw frames. */
    static const rsx_state* l_st; static u32 l_gen, l_start, l_mask; static int l_idx = -1;
    if (l_idx >= 0 && l_idx < s_d3d.vp_vs_n && l_st == st && l_gen == st->vp_ucode_gen &&
        l_start == vstart && l_mask == vtex_mask) {
        s_d3d.vp_vs[l_idx].last_used = s_d3d.frame_count;
        return l_idx;
    }
    if(defer_scan){u32 ni=rsx_vp_program_size_instrs(vuc,vlen);if(ni)vlen=ni*16;}
    /* The bound vertex-texture units change the compiled program (they
     * declare textures), so they are part of its identity. */
    /* The start slot too: branch targets are absolute slots, so the same
     * program loaded at another slot decompiles to different HLSL. */
    u32 hash = vp_hash_ucode(vuc, vlen) ^ (vtex_mask * 0x9E3779B1u) ^ ((vstart / 16u + 1u) * 0x85EBCA6Bu);
    if (!hash) hash = 1u;
    for (int i = 0; i < s_d3d.vp_vs_n; i++)
        if (s_d3d.vp_vs[i].hash == hash) {
            l_st = st; l_gen = st->vp_ucode_gen; l_start = vstart; l_mask = vtex_mask; l_idx = i;
            s_d3d.vp_vs[i].last_used = s_d3d.frame_count;
            return i;
        }

    /* Built by the shader preload: no decompile, no compile, no pending. */
    const PlVs* pre = pl_vs_ready(hash);
    static char hlsl[262144];
    int ni = 0;
    if (pre) goto have_program;
    rsx_vp_set_branch_base(vstart / 16u);   /* targets are absolute slots */
    ni = rsx_vp_decompile_ex(vuc, vlen, vtex_mask, hlsl, sizeof hlsl);
    if (ni <= 0) return -1;
    if (vf_enabled() && !vf_wrap_vs(hlsl, sizeof hlsl)) return -1;
    pl_record_vs(hash, vstart, vtex_mask, vuc, vlen);
    if (getenv("CV_VIDEO_CAPTURE")) { char pn[80];snprintf(pn,sizeof pn,"vs_%08X.hlsl",hash);FILE* f=fopen(pn,"w");if(f){fprintf(f,"/* slot=%d */\n%s",s_d3d.vp_vs_n,hlsl);fclose(f);} }
    if (getenv("VP_DUMP")) { static int _d=0; if (_d++ < 4) {
        FILE* f = fopen("vp2_dump.hlsl", _d==1 ? "w" : "a");
        if (f) { fprintf(f, "/* per-draw VS hash pending, %d instrs */%s%s", ni, hlsl, "\n"); fclose(f); } } }
    /* RSX_DBG_VP=<hex tex0 offset>: write the vertex program belonging to the draws
     * that bind that texture. vp2_dump.hlsl holds whichever four programs were
     * compiled first, which need not include the one under investigation -- and
     * reading the wrong program sends you chasing the wrong constants. */
    { static const char* dp = (const char*)1; static u32 wantp = 0; static int done = 0;
      if (dp == (const char*)1) { dp = getenv("RSX_DBG_VP");
                                  wantp = dp ? (u32)strtoul(dp, NULL, 16) : 0; }
      if (wantp && !done && s_d3d.cur_texs[0].raw == wantp) {
          done = 1;
          FILE* f = fopen("duck_vp.hlsl", "w");
          if (f) { fprintf(f, "/* duck VP, %d instrs, tex0=0x%X */%s", ni, wantp, hlsl);
                   fclose(f); }
          fprintf(stderr, "[DUCKVP] wrote duck_vp.hlsl (%d instrs)%c", ni, 10);
      } }
    /* VP_BYPASS=1: replace the guest transform with a direct map of attribute 0
     * into clip space. If geometry appears with this on, everything from the
     * input layout through raster/depth/present is sound and the fault is the
     * transform (constants or the decompiled program); if it stays blank, the
     * fault is upstream of the shader -- the attribute binding itself. Patched
     * in place, space-padded to the original length so offsets stay valid. */
    /* VP_W_ONE=1: keep the guest transform but force the clip-space w to 1.
     * Different from VP_BYPASS, which replaces the whole position: this tests
     * only the hypothesis that w is wrong. This title's VP ends
     *   { float4 _v = (float4)((vp_c[467]).xxxx); o[0].w = _v.w; }
     * with c467 = (-1, 1, 0, 0), so w = -1 and the clipper discards every
     * vertex. Patched in place, space-padded so offsets stay valid. */
    if (getenv("VP_W_ONE")) {
        static const char anchor[] = "o[0].w = _v.w;";
        static const char repl[]   = "o[0].w = 1.0;";
        char* at = hlsl;
        while ((at = strstr(at, anchor)) != NULL) {
            size_t n = sizeof(anchor) - 1, m = sizeof(repl) - 1;
            memcpy(at, repl, m);
            memset(at + m, ' ', n - m);
            at += n;
        }
    }
    if (getenv("VP_BYPASS")) {
        static const char anchor[] =
            "Out.pos = float4(_p.xyz * vp_posscale.xyz + _p.w * vp_posoffset.xyz, _p.w);";
        static const char repl[] = "Out.pos = float4(v[0].xy * 0.5, 0.5, 1.0);";
        char* at = hlsl;
        while ((at = strstr(at, anchor)) != NULL) {
            size_t n = sizeof(anchor) - 1, m = sizeof(repl) - 1;
            memcpy(at, repl, m);
            memset(at + m, ' ', n - m);
            at += n;
        }
    }
have_program: ;
    int slot;
    if (s_d3d.vp_vs_n < VP_VS_CACHE) slot = s_d3d.vp_vs_n++;
    else {  /* replace the least recently used slot, in place */
        slot = 0;
        while (slot < VP_VS_CACHE - 1 && s_d3d.vp_vs[slot].pending) slot++;
        for (int i = slot + 1; i < VP_VS_CACHE; i++)
            if (!s_d3d.vp_vs[i].pending && s_d3d.vp_vs[i].last_used < s_d3d.vp_vs[slot].last_used) slot = i;
        render_wait_idle();   /* the render thread may still read the old program */
        { static int _w=0; if (_w++<3) printf("[VP] VS cache full (%d): replacing the least recently used program%c", VP_VS_CACHE, 10); }
        if (s_d3d.vp_vs[slot].vs) s_d3d.vp_vs[slot].vs->lpVtbl->Release(s_d3d.vp_vs[slot].vs);
        free(s_d3d.vp_vs[slot].hlsl);
        memset(&s_d3d.vp_vs[slot], 0, sizeof s_d3d.vp_vs[slot]);
        l_idx = -1;   /* the fast path may name the slot being replaced */
    }
    s_d3d.vp_vs[slot].last_used = s_d3d.frame_count;
    { static u32 built = 0;   /* how many distinct programs a session needs */
      if ((++built & 255u) == 0) printf("[VP] %u vertex programs built (%d cached, frame %llu)%c",
                                        built, s_d3d.vp_vs_n, (unsigned long long)s_d3d.frame_count, 10); }
    s_d3d.vp_vs[slot].hash = hash;
    s_d3d.vp_vs[slot].vs   = NULL;
    if (pre) {
        pre->vs->lpVtbl->AddRef(pre->vs);
        s_d3d.vp_vs[slot].vs = pre->vs;
        s_d3d.vp_vs[slot].hlsl = _strdup(pre->hlsl);
        s_d3d.vp_vs[slot].pose_inputs_known = rsx_pose_shader_inputs_known(pre->hlsl);
        s_d3d.vp_vs[slot].uses_c03 = pre->uses_c03;
        s_d3d.vp_vs[slot].pending = 0;
        goto have_slot;
    }
    s_d3d.vp_vs[slot].hlsl = _strdup(hlsl);
    s_d3d.vp_vs[slot].pose_inputs_known = rsx_pose_shader_inputs_known(hlsl);
    { VsJob* j = (VsJob*)calloc(1, sizeof *j);
      s_d3d.vp_vs[slot].pending = 1;
      if (j) { j->slot = slot; j->hash = hash; j->hlsl = _strdup(hlsl); }
      if (!j || !j->hlsl) { if (j) free(j); s_d3d.vp_vs[slot].pending = 0; }
      else if (!TrySubmitThreadpoolCallback(vs_compile_job, j, NULL))
          vs_compile_job(NULL, j);   /* no pool: compile here */
    }
    s_d3d.vp_vs[slot].uses_c03 =
        (strstr(hlsl, "vp_c[0]") || strstr(hlsl, "vp_c[1]") ||
         strstr(hlsl, "vp_c[2]") || strstr(hlsl, "vp_c[3]")) ? 1 : 0;
    { static int _n=0; if (_n++<6) printf("[VP] per-draw VS cached (hash=0x%08X, %d instrs, slot %d)\n", hash, ni, slot); }
have_slot:
    /* Build the base VP pipeline here if it does not exist yet. render_frame's
     * trigger reads s_d3d.current_rsx_state, which by frame end no longer has
     * the microcode -- so for a title that only ever compiles per-draw VS the
     * base pipeline was never built, s_d3d.vp_ready stayed 0, and the VP draw
     * pass it gates dropped every recorded is_vp draw. Here we are at RECORD
     * time with a live state that definitely has microcode. */
    if (!s_d3d.vp_ready && st && st->vp_ucode_bytes >= 16) {
        const rsx_state* prev = s_d3d.current_rsx_state;
        s_d3d.current_rsx_state = st;
        compile_vp();
        s_d3d.current_rsx_state = prev;
    }
    l_st = st; l_gen = st->vp_ucode_gen; l_start = vstart; l_mask = vtex_mask; l_idx = slot;
    return slot;
}

/* --- Guest blend state -> packed PSO key ----------------------------------
 * RSX blend factors/equation carry GL enums; each SFACTOR/DFACTOR/EQUATION
 * word packs (alpha<<16)|colour. The key packs D3D12 enums so the PSO cache
 * distinguishes real blend modes: bit0 enable, [5:1] srcC, [10:6] dstC,
 * [15:11] srcA, [20:16] dstA, [23:21] opC, [26:24] opA. DeferredShading's
 * light accumulation is additive (ONE,ONE) -- the old hardcoded straight-
 * alpha PSO turned each light pass into a screen-blanking flash. */
static u8 gl_blend_factor_d3d(u32 f, int is_alpha)
{
    switch (f & 0xFFFF) {
    case 0x0000: return D3D12_BLEND_ZERO;
    case 0x0001: return D3D12_BLEND_ONE;
    case 0x0300: return is_alpha ? D3D12_BLEND_SRC_ALPHA     : D3D12_BLEND_SRC_COLOR;
    case 0x0301: return is_alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    case 0x0302: return D3D12_BLEND_SRC_ALPHA;
    case 0x0303: return D3D12_BLEND_INV_SRC_ALPHA;
    case 0x0304: return D3D12_BLEND_DEST_ALPHA;
    case 0x0305: return D3D12_BLEND_INV_DEST_ALPHA;
    case 0x0306: return is_alpha ? D3D12_BLEND_DEST_ALPHA     : D3D12_BLEND_DEST_COLOR;
    case 0x0307: return is_alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
    case 0x0308: return D3D12_BLEND_SRC_ALPHA_SAT;
    case 0x8001: return D3D12_BLEND_BLEND_FACTOR;       /* CONSTANT_COLOR   */
    case 0x8002: return D3D12_BLEND_INV_BLEND_FACTOR;
    case 0x8003: return D3D12_BLEND_BLEND_FACTOR;       /* CONSTANT_ALPHA ~ */
    case 0x8004: return D3D12_BLEND_INV_BLEND_FACTOR;
    default:     return D3D12_BLEND_ONE;
    }
}
static u8 gl_blend_op_d3d(u32 e)
{
    switch (e & 0xFFFF) {
    case 0x8007: return D3D12_BLEND_OP_MIN;
    case 0x8008: return D3D12_BLEND_OP_MAX;
    case 0x800A: return D3D12_BLEND_OP_SUBTRACT;
    case 0x800B: return D3D12_BLEND_OP_REV_SUBTRACT;
    default:     return D3D12_BLEND_OP_ADD;   /* 0x8006 FUNC_ADD / unset */
    }
}
/* Pack the guest's face-culling state for the PSO key.
 * bit0 = cull enabled, bit1 = cull FRONT (else BACK), bit2 = front face is CCW.
 * RSX/GL enums: CULL_FACE FRONT=0x0404 BACK=0x0405 FRONT_AND_BACK=0x0408;
 * FRONT_FACE CW=0x0900 CCW=0x0901. rsx_commands seeds cull_face/front_face with
 * plain 1/0 before any register arrives, so treat those defaults as BACK/CW. */
static u32 rsx_cull_key(const rsx_state* st)
{
    if (!st) return 0;
    /* The winding is kept with culling off too: it still decides which side
     * the fragment program's FACING input calls the front. Code Veronica
     * draws its characters double-sided and its lighter shader flips the
     * normal of back faces; with the winding dropped, every face turned to
     * the flame tested as a back face and came out dark while backs lit up.
     * FACE_LEGACY=1 drops it again. */
    static int legacy = -1;
    if (legacy < 0) legacy = getenv("FACE_LEGACY") ? 1 : 0;
    u32 k = (!legacy && st->front_face == 0x0901u) ? 4u : 0u;            /* CCW */
    if (!st->cull_face_enable) return k;
    k |= 1u;
    if (st->cull_face == 0x0404u || st->cull_face == 0x0408u) k |= 2u;  /* FRONT */
    if (st->front_face == 0x0901u) k |= 4u;
    return k;
}

/* Guest depth state, packed into the same PSO key word as culling:
 * bit12 = present, bit13 = test enabled, bit14 = depth writes, bits16-18 =
 * compare function (GL NEVER..ALWAYS as 0..7). Every guest-FP pipeline used
 * to test LESS_EQUAL and write depth whatever the title asked for. Code
 * Veronica's inventory draws its panels with depth writes off; written, they
 * hid half of the 3D item models drawn behind them in the item boxes (the
 * diagonal cut through the lighter). DEPTH_LEGACY=1 restores the old state. */
static u32 rsx_depth_key(const rsx_state* st)
{
    static int legacy = -1;
    if (legacy < 0) legacy = getenv("DEPTH_LEGACY") ? 1 : 0;
    if (!st || legacy) return 0;
    u32 f = (st->depth_func >= 0x200u && st->depth_func <= 0x207u) ? st->depth_func - 0x200u : 3u;
    return 0x1000u | (st->depth_test_enable ? 0x2000u : 0u) |
           (st->depth_mask ? 0x4000u : 0u) | (f << 16);
}

/* Guest alpha test, in the same PSO key word: bit20 = the draw tests alpha.
 * Only the enable is keyed; func and ref reach the shader through
 * rsx_alpha_k (vp_record_cb), so a title that animates the reference does
 * not multiply pipelines. Keyed rather than tested in every program because
 * a discard anywhere in a shader costs that pipeline its early depth test.
 * Code Veronica's rm_1100 trees are opaque quads cut by the alpha test
 * on their DXT5 alpha: untested, they wrote depth over the whole quad and the
 * fog pass that reads depth drew dark green rectangles. NO_ALPHATEST=1 turns
 * it off. */
static u32 rsx_alpha_key(const rsx_state* st)
{
    static int off = -1;
    if (off < 0) off = getenv("NO_ALPHATEST") ? 1 : 0;
    if (!st || off || !st->alpha_test_enable) return 0;
    return (st->alpha_func == 0x0207u) ? 0 : 0x100000u;   /* ALWAYS: nothing to test */
}

static u32 rsx_blend_key(const rsx_state* st, int enable)
{
    { static int _bd = -1; if (_bd < 0) _bd = getenv("BLENDDBG") ? 1 : 0;
      if (_bd) { static u32 seen[32]; static int ns=0;
        u32 sk = (enable?0x80000000u:0u) | (st ? (st->blend_sfactor & 0xFFFFu) : 0u);
        int f=0; for (int k2=0;k2<ns;k2++) if (seen[k2]==sk) f=1;
        if (!f && ns<32) { seen[ns++]=sk;
            fprintf(stderr, "[BLEND] enable=%d sfactor=0x%X dfactor=0x%X%c", enable,
                    st?st->blend_sfactor:0, st?st->blend_dfactor:0, 10); } } }
    if (!enable) return 0;
    /* Factors never programmed: keep the legacy straight-alpha behaviour
     * (dbgfont-style text enables blending without setting factors). */
    if (!st || (st->blend_sfactor == 0 && st->blend_dfactor == 0))
        return 1u
             | ((u32)D3D12_BLEND_SRC_ALPHA     << 1)
             | ((u32)D3D12_BLEND_INV_SRC_ALPHA << 6)
             | ((u32)D3D12_BLEND_ONE           << 11)
             | ((u32)D3D12_BLEND_INV_SRC_ALPHA << 16)
             | ((u32)D3D12_BLEND_OP_ADD << 21) | ((u32)D3D12_BLEND_OP_ADD << 24);
    return 1u
         | ((u32)gl_blend_factor_d3d(st->blend_sfactor,       0) << 1)
         | ((u32)gl_blend_factor_d3d(st->blend_dfactor,       0) << 6)
         | ((u32)gl_blend_factor_d3d(st->blend_sfactor >> 16, 1) << 11)
         | ((u32)gl_blend_factor_d3d(st->blend_dfactor >> 16, 1) << 16)
         | ((u32)gl_blend_op_d3d(st->blend_equation)       << 21)
         | ((u32)gl_blend_op_d3d(st->blend_equation >> 16) << 24);
}

/* Bound colour target count for a draw record: 1 (target A) + the contiguous
 * prefix of MRT B/C/D offsets. */
static int dr_num_rts(const D3D12DrawRecord* dr)
{
    int n = 1;
    while (n < 4 && dr->rt_mrt[n - 1]) n++;
    return n;
}

/* Replace every occurrence of `find` with `repl` inside a NUL-terminated buffer,
 * shifting the tail. Used to retarget generated HLSL (2D sampler -> cube). */
static void hlsl_replace_all(char* buf, size_t cap, const char* find, const char* repl)
{
    size_t fl = strlen(find), rl = strlen(repl);
    if (!fl) return;
    char* at = buf;
    while ((at = strstr(at, find)) != NULL) {
        size_t used = strlen(buf) + 1;
        if (used + rl - fl > cap) return;
        memmove(at + rl, at + fl, used - (size_t)(at - buf) - fl);
        memcpy(at, repl, rl);
        at += rl;
    }
}

/* Which of a draw's texture units are cube textures, as a 4-bit mask. */
static u32 dr_cube_mask(const D3D12DrawRecord* dr)
{
    /* ON by default (RSX_CUBE_TEX=0 to disable). This was off while the cube path
     * cost 2262 PSO misses per 20 frames and 0.38 fps -- both caused by inline
     * fragment-program constants baking into the shader source. With those
     * constants hoisted into b1 the cube path costs 0 PSO misses, and with the
     * per-face stride fixed (each face is a whole mip pyramid, not one mip-0
     * image) all six faces decode as clean environment art. */
    static int en = -1;
    if (en < 0) { const char* e = getenv("RSX_CUBE_TEX"); en = e ? atoi(e) : 1; }
    if (!en) return 0;
    u32 m = 0;
    for (int u = 0; u < FP_TEX_UNITS; u++) if (dr->tex[u].set && dr->tex[u].cube) m |= 1u << u;
    return m;
}

/* Cube textures are bound as 6-slice Texture2DArray views and sampled face
 * by face in the shader (rsx_cube), as RSX filters them; RSX_CUBE_SEAMLESS=1
 * goes back to D3D's TextureCube, which filters across face seams. */
static int rsx_cube_as_array(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RSX_CUBE_SEAMLESS") ? 0 : 1;
    return on;
}

/* Set by vp_get_fp_pso when the program cannot be translated and the draw
 * must be dropped rather than drawn with the fallback colour shader. */
static int s_fp_skip_draw = 0;
static int s_fp_result_index = -1;
typedef struct { VPFPEntry key; int index,valid; } PreparedFp;
static PreparedFp s_prepared_fp[MAX_DRAWS];

/* Open-addressed index over s_d3d.vp_fp: slot -> entry index + 1 (0 = empty).
 * The key is every field the cache matches on (fp_addr is not one: programs
 * are identified by content). */
#define FP_INDEX_SIZE 65536u   /* power of two, > 2 x VP_FP_CACHE */
static int s_fp_index[FP_INDEX_SIZE];
static u32 fp_key_hash(const VPFPEntry* e)
{
    u32 h = 2166136261u;
    /* The VS by content (vs_hash; 0 = the primary VS, which gen covers), not
     * by cache slot: a slot can be replaced, and keying on it re-keyed every
     * cached pipeline at once -- the whole scene skipped a few frames while
     * they rebuilt. */
    const u32 f[10] = { e->vs_hash, e->gen, e->blend, (u32)e->nrt, e->rtfmt,
                        (u32)e->exp32, e->ucode_hash, e->cmask, e->cull, e->cube_mask };
    for (int i = 0; i < 10; i++) { h ^= f[i]; h *= 16777619u; h ^= h >> 15; }
    return h;
}
static int fp_key_eq(const VPFPEntry* a, const VPFPEntry* b)
{
    return a->ucode_hash == b->ucode_hash &&
           a->vs_hash == b->vs_hash && a->gen == b->gen && a->blend == b->blend &&
           a->nrt == b->nrt && a->rtfmt == b->rtfmt && a->exp32 == b->exp32 &&
           a->cmask == b->cmask && a->cull == b->cull && a->cube_mask == b->cube_mask;
}
static int fp_index_find(const VPFPEntry* key)
{
    for (u32 i = fp_key_hash(key) & (FP_INDEX_SIZE - 1), n = 0; n < FP_INDEX_SIZE;
         n++, i = (i + 1) & (FP_INDEX_SIZE - 1)) {
        int v = s_fp_index[i];
        if (!v) return -1;
        if (fp_key_eq(&s_d3d.vp_fp[v - 1], key)) return v - 1;
    }
    return -1;
}
/* Reuse the prepass lookup, but fetch the object from the LIVE cache entry.
 * No raw PSO pointer survives an eviction. A moved/replaced entry, generation
 * change, pending VS or different VS content takes the original lookup path.
 * Only immutable captured FP programs are eligible. */
static ID3D12PipelineState* fp_prepared_ready(u32 draw,const D3D12DrawRecord* dr) {
    if(!prep_reuse_on())return NULL;
    const PreparedFp* p=&s_prepared_fp[draw];
    if(!p->valid||p->index<0||p->index>=s_d3d.vp_fp_n||p->key.gen!=s_d3d.vp_gen)return NULL;
    const VPFPEntry* e=&s_d3d.vp_fp[p->index];
    if(e->pending||e->skip||!e->pso||!fp_key_eq(&p->key,e))return NULL;
    u32 hash=0;int have_vs=0;
    if(dr->vs_idx>=0&&dr->vs_idx<s_d3d.vp_vs_n) {
        VPVSEntry* v=&s_d3d.vp_vs[dr->vs_idx];if(v->pending)return NULL;
        if(InterlockedCompareExchangePointer((PVOID volatile*)&v->vs,NULL,NULL)){hash=v->hash;have_vs=1;}
    }
    if(!have_vs&&!s_d3d.vp_vs_blob)return NULL;
    if(hash!=p->key.vs_hash)return NULL;
    s_fp_skip_draw=0;s_prep_pipelines++;return e->pso;
}
static void fp_index_insert(int idx)
{
    for (u32 i = fp_key_hash(&s_d3d.vp_fp[idx]) & (FP_INDEX_SIZE - 1), n = 0; n < FP_INDEX_SIZE;
         n++, i = (i + 1) & (FP_INDEX_SIZE - 1))
        if (!s_fp_index[i]) { s_fp_index[i] = idx + 1; return; }
}
static void fp_index_rebuild(void)
{
    memset(s_fp_index, 0, sizeof s_fp_index);
    for (int i = 0; i < s_d3d.vp_fp_n; i++) fp_index_insert(i);
}

/* ---------------------------------------------------------------------------
 * Pipeline library: PSOs persisted to shader_cache/pipelines.bin. With the
 * bytecode already on disk, what is left of a first sighting is the driver
 * building the pipeline (several ms each, dozens on entering an area). A
 * stored pipeline loads instead. Keyed by a hash of the shader bytecode and
 * every fixed-function field of the desc. PSO_LIBRARY=0 disables it.
 * -------------------------------------------------------------------------*/
static ID3D12PipelineLibrary* s_plib = NULL;
static void* s_plib_blob = NULL;           /* must outlive s_plib */
static SRWLOCK s_plib_lock = SRWLOCK_INIT;
static volatile LONG s_plib_dirty = 0, s_plib_loaded = 0;
static ULONGLONG s_plib_saved_at = 0;
static int s_plib_state = 0;               /* 0 untried, 1 ready, -1 off */

static void plib_init(void)
{
    if (s_plib_state) return;
    s_plib_state = -1;
    { const char* e = getenv("PSO_LIBRARY"); if (e && e[0] == '0') return; }
    ID3D12Device1* d1 = NULL;
    if (FAILED(s_d3d.device->lpVtbl->QueryInterface(s_d3d.device, &IID_ID3D12Device1, (void**)&d1)) || !d1)
        return;
    CreateDirectoryA("shader_cache", NULL);
    CreateDirectoryA(shader_cache_dir(), NULL);
    char path[256]; snprintf(path, sizeof path, "%s/pipelines.bin", shader_cache_dir());
    FILE* f = fopen(path, "rb");
    size_t n = 0;
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz > 0 && (s_plib_blob = malloc((size_t)sz)) != NULL &&
            fread(s_plib_blob, 1, (size_t)sz, f) == (size_t)sz) n = (size_t)sz;
        fclose(f);
    }
    HRESULT hr = n ? d1->lpVtbl->CreatePipelineLibrary(d1, s_plib_blob, n,
                                                        &IID_ID3D12PipelineLibrary, (void**)&s_plib)
                   : E_FAIL;
    if (FAILED(hr)) {   /* none yet, or written by another driver/GPU: start empty */
        if (n) fprintf(stderr, "[PSOLIB] stored library rejected (0x%08lX), starting a new one%c", hr, 10);
        free(s_plib_blob); s_plib_blob = NULL; n = 0;
        hr = d1->lpVtbl->CreatePipelineLibrary(d1, NULL, 0, &IID_ID3D12PipelineLibrary, (void**)&s_plib);
    }
    d1->lpVtbl->Release(d1);
    if (FAILED(hr) || !s_plib) { s_plib = NULL; return; }
    s_plib_state = 1;
    s_plib_saved_at = GetTickCount64();
    fprintf(stderr, "[PSOLIB] pipeline library ready (%zu KB loaded)%c", n / 1024, 10);
}

static void plib_save(void)
{
    if (s_plib_state != 1 || !s_plib_dirty) return;
    AcquireSRWLockExclusive(&s_plib_lock);
    SIZE_T n = s_plib->lpVtbl->GetSerializedSize(s_plib);
    void* buf = n ? malloc(n) : NULL;
    HRESULT hr = buf ? s_plib->lpVtbl->Serialize(s_plib, buf, n) : E_FAIL;
    InterlockedExchange(&s_plib_dirty, 0);
    ReleaseSRWLockExclusive(&s_plib_lock);
    if (SUCCEEDED(hr)) {
        char path[256], tmp[280];
        snprintf(path, sizeof path, "%s/pipelines.bin", shader_cache_dir());
        snprintf(tmp, sizeof tmp, "%s.tmp", path);
        FILE* f = fopen(tmp, "wb");
        if (f) {
            int ok = fwrite(buf, 1, n, f) == n;
            ok = (fclose(f) == 0) && ok;
            if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp);
        }
    }
    free(buf);
    s_plib_saved_at = GetTickCount64();
}

/* Save at most every 10 s while new pipelines keep arriving: the game is
 * usually closed with the window's X, so shutdown cannot be relied on. */
static void plib_maybe_save(void)
{
    if (s_plib_state == 1 && s_plib_dirty && GetTickCount64() - s_plib_saved_at >= 10000)
        plib_save();
}

static HRESULT pso_create_cached_impl(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pd, ID3D12PipelineState** out)
{
    *out = NULL;
    if (s_plib_state == 1) {
        unsigned long long h = 14695981039346656037ull;
#define PH(p, n) do { const u8* _q = (const u8*)(p); for (size_t _i = 0; _i < (size_t)(n); _i++) { h ^= _q[_i]; h *= 1099511628211ull; } } while (0)
        PH(pd->VS.pShaderBytecode, pd->VS.BytecodeLength);
        PH(pd->PS.pShaderBytecode, pd->PS.BytecodeLength);
        PH(&pd->BlendState, sizeof pd->BlendState);
        PH(&pd->SampleMask, sizeof pd->SampleMask);
        PH(&pd->RasterizerState, sizeof pd->RasterizerState);
        PH(&pd->DepthStencilState, sizeof pd->DepthStencilState);
        PH(&pd->PrimitiveTopologyType, sizeof pd->PrimitiveTopologyType);
        PH(&pd->NumRenderTargets, sizeof pd->NumRenderTargets);
        PH(pd->RTVFormats, sizeof pd->RTVFormats);
        PH(&pd->DSVFormat, sizeof pd->DSVFormat);
        PH(&pd->InputLayout.NumElements, sizeof pd->InputLayout.NumElements);
        /* The samplers live in the root signature, which the stored desc
         * must match; off keeps the names already stored. */
        { const UINT an = tex_aniso(); if (an) PH(&an, sizeof an); }
#undef PH
        WCHAR name[40];
        swprintf(name, 40, L"fp_%016llX", h);
        AcquireSRWLockExclusive(&s_plib_lock);
        HRESULT hr = s_plib->lpVtbl->LoadGraphicsPipeline(s_plib, name, pd,
                                                           &IID_ID3D12PipelineState, (void**)out);
        ReleaseSRWLockExclusive(&s_plib_lock);
        if (SUCCEEDED(hr) && *out) { InterlockedIncrement(&s_plib_loaded); return hr; }
        *out = NULL;
        hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, pd,
                                                                &IID_ID3D12PipelineState, (void**)out);
        if (SUCCEEDED(hr) && *out) {
            AcquireSRWLockExclusive(&s_plib_lock);
            if (SUCCEEDED(s_plib->lpVtbl->StorePipeline(s_plib, name, *out)))
                InterlockedIncrement(&s_plib_dirty);
            ReleaseSRWLockExclusive(&s_plib_lock);
        }
        return hr;
    }
    return s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, pd,
                                                              &IID_ID3D12PipelineState, (void**)out);
}

static HRESULT pso_create_cached(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pd, ID3D12PipelineState** out)
{
    if (!cv_cutscene_diag()) return pso_create_cached_impl(pd, out);
    const double start = perf_now();
    HRESULT hr = pso_create_cached_impl(pd, out);
    cv_cutscene_slow("pipeline-load-create-store", start);
    return hr;
}

/* ---------------------------------------------------------------------------
 * Background pipeline builds. A miss used to decompile, compile and create
 * the PSO inline on the render thread -- the stutter on entering an area.
 * Now the key is cached as pending, the draws that need it are skipped for
 * the few frames the build takes (the object pops in, as with RPCS3's async
 * shaders), and worker threads do the work. FP_ASYNC=0 builds inline;
 * FP_ASYNC_THREADS=<n> sets the worker count (default 2).
 * -------------------------------------------------------------------------*/
enum { FPB_OK = 0, FPB_FAIL = 1, FPB_SKIP = 2 };
typedef struct FpJob {
    struct FpJob* next;
    VPFPEntry key;              /* full cache key */
    ID3DBlob* vsb;              /* AddRef'd: the VS cache may evict its slot */
    u32 off;
    int status;
    ID3D12PipelineState* pso;   /* result */
    u8* ucode;                 /* exact program copy; arena is recycled */
    u32 ucode_bytes;
    char* hlsl;                /* grows only while this pipeline is built */
    u32 hlsl_cap;
    int urgent;                /* a draw that must not be left out: the frame waits */
} FpJob;

static void fp_job_free(FpJob* j)
{
    if (j->vsb) j->vsb->lpVtbl->Release(j->vsb);
    free(j->ucode);
    free(j->hlsl);
    free(j);
}

static volatile LONG s_fp_async_skips = 0, s_fp_async_built = 0;
static volatile LONG s_fp_build_us = 0;  /* worker build time, for PERF */
static LONG s_fp_drawn_skips = 0;   /* draws really left out (render thread), for PERF */
static volatile LONG s_fp_inflight = 0;
static volatile LONG s_fp_urgent_inflight = 0;   /* urgent builds the frame waits for */                /* submitted, not yet built */
static SRWLOCK s_fpq_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_fpq_cv = CONDITION_VARIABLE_INIT;
static CONDITION_VARIABLE s_fpq_done_cv = CONDITION_VARIABLE_INIT;
static FpJob* s_fpq_head = NULL; static FpJob* s_fpq_tail = NULL;   /* to build */
static FpJob* s_fpq_done = NULL;                                     /* built */
static int s_fp_async = -1;

static int fp_build_pso(FpJob* j);

static DWORD WINAPI fp_worker(LPVOID p)
{
    (void)p;
    for (;;) {
        AcquireSRWLockExclusive(&s_fpq_lock);
        while (!s_fpq_head) SleepConditionVariableSRW(&s_fpq_cv, &s_fpq_lock, INFINITE, 0);
        FpJob* j = s_fpq_head;
        s_fpq_head = j->next; if (!s_fpq_head) s_fpq_tail = NULL;
        ReleaseSRWLockExclusive(&s_fpq_lock);
        /* The frame is waiting on an urgent build: compete for the CPU at
         * the game's own priority rather than below it. */
        if (j->urgent) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        { const double t0 = perf_now();
          j->status = fp_build_pso(j);
          InterlockedAdd(&s_fp_build_us, (LONG)((perf_now() - t0) * 1e6)); }
        if (j->urgent) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        InterlockedIncrement(&s_fp_async_built);
        AcquireSRWLockExclusive(&s_fpq_lock);
        j->next = s_fpq_done; s_fpq_done = j;
        InterlockedDecrement(&s_fp_inflight);
        if (j->urgent) InterlockedDecrement(&s_fp_urgent_inflight);
        ReleaseSRWLockExclusive(&s_fpq_lock);
        WakeAllConditionVariable(&s_fpq_done_cv);
    }
}

static int fp_async_on(void)
{
    if (s_fp_async >= 0) return s_fp_async;
    s_fp_async = 0;
    { const char* e = getenv("FP_ASYNC"); if (e && e[0] == '0') return 0; }
    /* Half the logical cores, 2..6: the game's own threads need the rest. */
    SYSTEM_INFO si; GetSystemInfo(&si);
    int nt = (int)si.dwNumberOfProcessors / 2;
    if (nt < 2) nt = 2; if (nt > 6) nt = 6;
    { const char* e = getenv("FP_ASYNC_THREADS"); if (e && atoi(e) > 0) nt = atoi(e); }
    if (nt > 8) nt = 8;
    int started = 0;
    for (int i = 0; i < nt; i++) {
        HANDLE th = CreateThread(NULL, 0, fp_worker, NULL, 0, NULL);
        if (!th) continue;
        SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);
        ps3_host_thread_name(th, "shader:worker %d", i);
        CloseHandle(th);
        started++;
    }
    s_fp_async = started > 0;
    if (started) fprintf(stderr, "[FP] background pipeline builds on (%d workers)%c", started, 10);
    return s_fp_async;
}

static void fp_async_submit(FpJob* j)
{
    AcquireSRWLockExclusive(&s_fpq_lock);
    if (j->urgent) {             /* ahead of the queue: the frame is waiting */
        j->next = s_fpq_head; s_fpq_head = j;
        if (!s_fpq_tail) s_fpq_tail = j;
        InterlockedIncrement(&s_fp_urgent_inflight);
    } else {
        j->next = NULL;
        if (s_fpq_tail) s_fpq_tail->next = j; else s_fpq_head = j;
        s_fpq_tail = j;
    }
    InterlockedIncrement(&s_fp_inflight);
    ReleaseSRWLockExclusive(&s_fpq_lock);
    WakeConditionVariable(&s_fpq_cv);
}

/* Cache insert (evicting the oldest when full). pending: a worker is
 * building it; skip: the program cannot be translated, drop its draws. */
static void fp_cache_insert(const VPFPEntry* key, ID3D12PipelineState* pso, int pending, int skip)
{
    if (s_d3d.vp_fp_n >= VP_FP_CACHE) {
        { static int _w = 0; if (_w++ < 3) printf("[FP] pipeline cache full (%d): evicting the oldest%c", VP_FP_CACHE, 10); }
        if (s_d3d.vp_fp[0].pso) s_d3d.vp_fp[0].pso->lpVtbl->Release(s_d3d.vp_fp[0].pso);
        memmove(&s_d3d.vp_fp[0], &s_d3d.vp_fp[1], sizeof(VPFPEntry) * (VP_FP_CACHE - 1));
        s_d3d.vp_fp_n = VP_FP_CACHE - 1;
        fp_index_rebuild();   /* every entry moved down one slot */
    }
    VPFPEntry* ne = &s_d3d.vp_fp[s_d3d.vp_fp_n];
    *ne = *key;
    ne->pso = pso; ne->pending = (u8)pending; ne->skip = (u8)skip;
    fp_index_insert(s_d3d.vp_fp_n);
    s_d3d.vp_fp_n++;
}

/* After the prepass has queued every pipeline this frame is missing: give
 * the workers a short, bounded wait. With bytecode and pipelines mostly on
 * disk a build takes a few ms, so most finish in time and are drawn this
 * frame instead of popping in a frame or two late. The bound keeps a burst
 * of first-time compiles from turning back into a stall.
 * FP_ASYNC_WAIT_MS=<ms> (default 8; 0 = never wait). */
static int fp_async_wait_ms(void)
{
    static int budget = -1;
    if (budget < 0) { const char* e = getenv("FP_ASYNC_WAIT_MS"); budget = e ? atoi(e) : 4; }
    return budget;
}
static void fp_async_wait_for(double lim)
{
    if (s_fp_async <= 0 || !s_fp_inflight || lim <= 0.0) return;
    const double t0 = perf_now();
    AcquireSRWLockExclusive(&s_fpq_lock);
    while (s_fp_inflight > 0 && perf_now() - t0 < lim)
        SleepConditionVariableSRW(&s_fpq_done_cv, &s_fpq_lock, 1, 0);   /* woken per finished build */
    ReleaseSRWLockExclusive(&s_fpq_lock);
    InterlockedAdd(&s_hd_wait_us, (LONG)((perf_now() - t0) * 1e6));
}

/* The urgent builds this frame queued: composites and full-screen passes,
 * which would black out the frame if left out. They used to be built inline,
 * one after another -- ten first-time programs held the frame for a second.
 * On the workers they build in parallel; the frame waits for all of them. */
static void fp_async_wait_urgent(void)
{
    if (s_fp_async <= 0 || !s_fp_urgent_inflight) return;
    const double t0 = perf_now();
    AcquireSRWLockExclusive(&s_fpq_lock);
    while (s_fp_urgent_inflight > 0 && perf_now() - t0 < 2.0)
        SleepConditionVariableSRW(&s_fpq_done_cv, &s_fpq_lock, 1, 0);
    ReleaseSRWLockExclusive(&s_fpq_lock);
    InterlockedAdd(&s_hd_wait_us, (LONG)((perf_now() - t0) * 1e6));
}

/* Render thread, frame start: hand finished builds to their cache entries. */
static void fp_async_drain(void)
{
    if (s_fp_async <= 0) return;
    AcquireSRWLockExclusive(&s_fpq_lock);
    FpJob* j = s_fpq_done; s_fpq_done = NULL;
    ReleaseSRWLockExclusive(&s_fpq_lock);
    while (j) {
        FpJob* nx = j->next;
        int idx = fp_index_find(&j->key);
        if (j->status == FPB_OK) pl_record_fp(&j->key, j->ucode, j->ucode_bytes);
        if (idx >= 0 && s_d3d.vp_fp[idx].pending) {
            VPFPEntry* e = &s_d3d.vp_fp[idx];
            e->pso = j->pso; e->pending = 0; e->skip = (j->status == FPB_SKIP);
        } else if (j->pso) {
            j->pso->lpVtbl->Release(j->pso);   /* evicted while building */
        }
        fp_job_free(j);
        j = nx;
    }
}

/* Build one guest-FP pipeline: decompile, compile (disk-cached), create the
 * PSO (pipeline-library cached). Touches no cache state, so it runs on the
 * shader workers as well as inline. */
static int fp_build_pso(FpJob* j)
{
    const u32 fp_addr = j->key.fp_addr; const int vs_idx = j->key.vs_idx;
    const u32 uhash = j->key.ucode_hash; const u32 blend = j->key.blend;
    const int nrt = j->key.nrt; const DXGI_FORMAT rtfmt = (DXGI_FORMAT)j->key.rtfmt;
    const int exp32 = j->key.exp32; u32 cmask = j->key.cmask;
    const u32 cull = j->key.cull; const u32 cube_mask = j->key.cube_mask;
    const u8* ucode = j->ucode; const u32 off = j->off; ID3DBlob* vsb = j->vsb;
    (void)off; (void)uhash; (void)vs_idx;
    /* fp_exp32 snapshots SET_SHADER_CONTROL bit 6 for this draw. Convert
     * the boolean back to the control bit expected by the decompiler.
     * AUTO mistakes fp32 temporaries for exports when final color is h0. */
    /* Guest texture address modes, emulated in the shader. The static
     * samplers are fixed (s0 clamps), so a title that mirrors a 9-slice panel
     * with negative coordinates -- Code Veronica's message window -- lost the
     * mirrored half. b1[0..3].zw carry each unit's S/T mode (vp_record_cb). */
    static const char k_addr_prelude[] =
        /* b1 layout as vp_record_cb writes it: texaddr[FP_TEX_UNITS] (.z = S
         * mode | gamma << 4), alpha test, then the program's inline constants
         * re-read from guest ucode per draw. */
        "cbuffer RsxTexAddr : register(b1) { float4 rsx_texaddr[8]; float4 rsx_alpha_k; float4 fp_k[128]; };\n"
        "float rsx_addr1(float c, float m, float e) {\n"
        /* Wrap: the static samplers already wrap. frac() here broke the
         * derivatives at every seam, which with mip levels (RSX_TEX_MIPS)
         * picked the smallest level along a line through the texture. */
        "    if (m == 1.0) return c;\n"
        "    if (m == 2.0) { float t = frac(c * 0.5) * 2.0; return t > 1.0 ? 2.0 - t : t; }\n"
        "    if (m == 6.0 || m == 8.0) return clamp(abs(c), e, 1.0 - e);\n"
        "    if (m == 3.0 || m == 4.0 || m == 5.0 || m == 7.0) return clamp(c, e, 1.0 - e);\n"
        "    return c;\n"
        "}\n"
        "#define RSX_ADDR(u, c) float2(rsx_addr1((c).x, (float)((uint)rsx_texaddr[u].z & 15u), rsx_texaddr[u].x), rsx_addr1((c).y, rsx_texaddr[u].w, rsx_texaddr[u].y))\n"
        /* TEXTURE_ADDRESS gamma bits: the unit's channels are sRGB and the
         * hardware linearises them on fetch (RPCS3 _process_texel). */
        "float4 rsx_texgamma(uint u, float4 c) {\n"
        /* .z bit 7: a Y16_X16 fetch of an A8R8G8B8 render target. On RSX both
         * name the same bytes, so each 32-bit texel A R G B reads as the two
         * 16-bit values A:R and G:B. Code Veronica's lighter keeps a variance
         * shadow map that way (distance in A:R, distance squared in G:B);
         * sampled as plain colour every lit pixel tested as shadowed and the
         * flame never lit faces. */
        "    if (u < 8u && (((uint)rsx_texaddr[min(u, 7u)].z) & 128u)) {\n"
        "        float4 b = round(saturate(c) * 255.0);\n"
        "        return float4((b.a * 256.0 + b.r) / 65535.0, (b.g * 256.0 + b.b) / 65535.0, 0.0, 1.0);\n"
        "    }\n"
        "    uint m = u < 8u ? (((uint)rsx_texaddr[min(u, 7u)].z) >> 4) & 7u : 0u;\n"
        /* Keep the expensive decode inside a uniform branch. An early return
         * alone is flattened by FXC after inlining: log/exp then run even for
         * linear textures and their results are discarded. No derivatives or
         * texture reads move across this branch; gamma-enabled math is intact. */
        "    [branch] if (m != 0u) {\n"
        "    float3 s = saturate(c.rgb);\n"
        "    float3 l = s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);\n"
        "    if (m & 1u) c.r = l.r; if (m & 2u) c.g = l.g; if (m & 4u) c.b = l.b;\n"
        "    }\n"
        "    return c;\n"
        "}\n"
        "#define RSX_TEXOUT(u, x) rsx_texgamma(u, x)\n"
        /* Return logical filtered values directly. Re-encoding and rounding
         * the interpolated moments to UINT16 introduced discontinuities at
         * carries (e.g. 255 -> 256) and lost sub-LSB variance precision. */
        RSX_PACKED16_FILTER_HLSL
        "#define RSX_S2D_OUT(u, x) (x)\n"
        "float4 rsx_s2d(Texture2D t, SamplerState s, uint u, float2 c) {\n"
        "    if (u < 8u && (((uint)rsx_texaddr[min(u, 7u)].z) & 128u))\n"
        "        return float4(rsx_filter_pair16(t, c), 0.0, 1.0);\n"
        "    return rsx_texgamma(u, t.Sample(s, c));\n"
        "}\n"
        /* Cube textures are bound as 6-slice arrays and sampled one face at a
         * time, filtered within that face (clamped at its edge) as RSX does.
         * D3D12's own cube sampling filters across face seams; Code Veronica's
         * lighter reads its shadow atlas through two cube look-up tables
         * (which tile, and where in it), and blending two faces' entries at a
         * seam gave positions in no tile at all -- thin dark lines along the
         * seams. Face selection and orientation are the D3D/GL cube rules. */
        "float3 rsx_cubeuv(float3 d) {\n"
        "    float3 a = abs(d); float sc, tc, ma, f;\n"
        "    if (a.x >= a.y && a.x >= a.z) { ma = a.x; f = d.x >= 0.0 ? 0.0 : 1.0; sc = d.x >= 0.0 ? -d.z : d.z; tc = -d.y; }\n"
        "    else if (a.y >= a.z)          { ma = a.y; f = d.y >= 0.0 ? 2.0 : 3.0; sc = d.x; tc = d.y >= 0.0 ? d.z : -d.z; }\n"
        "    else                          { ma = a.z; f = d.z >= 0.0 ? 4.0 : 5.0; sc = d.z >= 0.0 ? d.x : -d.x; tc = -d.y; }\n"
        "    ma = ma > 0.0 ? ma : 1.0;\n"
        "    return float3(sc / ma * 0.5 + 0.5, tc / ma * 0.5 + 0.5, f);\n"
        "}\n"
        "float3 rsx_cubeclamp(Texture2DArray t, float3 c) {\n"
        "    uint W, H, N; t.GetDimensions(W, H, N);\n"
        "    float2 e = 0.5 / float2(W, H);\n"
        "    return float3(clamp(c.xy, e, 1.0 - e), c.z);\n"
        "}\n"
        /* .z bit 8 (vp_record_cb): a Y16_X16 cube look-up table whose texels
         * hold i/(N-1), edge to edge -- Code Veronica's lighter maps a light
         * direction to its place in the shadow atlas through one. Sampled at
         * texel centres the answer is off by (u-0.5)/(N-1): up to two atlas
         * texels near a face edge, the opposite way on the next face, and the
         * jump at every seam drew a thin dark line across lit walls. Map the
         * coordinate onto the stored grid so the table returns u itself. */
        "float4 rsx_cube(Texture2DArray t, SamplerState s, uint u, float3 d) {\n"
        "    if (u < 8u && (((uint)rsx_texaddr[min(u, 7u)].z) & 256u)) {\n"
        "        uint W, H, N; t.GetDimensions(W, H, N);\n"
        "        float3 c = rsx_cubeuv(d); float2 n = float2(W, H);\n"
        "        c.xy = (saturate(c.xy) * (n - 1.0) + 0.5) / n;\n"
        "        return t.Sample(s, c);\n"
        "    }\n"
        "    return t.Sample(s, rsx_cubeclamp(t, rsx_cubeuv(d)));\n"
        "}\n"
        "float4 rsx_cube_b(Texture2DArray t, SamplerState s, float3 d, float b) {\n"
        "    return t.SampleBias(s, rsx_cubeclamp(t, rsx_cubeuv(d)), b);\n"
        "}\n"
        "float4 rsx_cube_l(Texture2DArray t, SamplerState s, float3 d, float l) {\n"
        "    return t.SampleLevel(s, rsx_cubeclamp(t, rsx_cubeuv(d)), l);\n"
        "}\n";
    const size_t k_pre = sizeof k_addr_prelude - 1;
    /* Constants must come from fp_k[], not literals: the pipeline is keyed on
     * the code hash alone, so a literal baked from the first draw's constants
     * was reused by every later draw of the program. Code Veronica patches its
     * light parameters into one program per object, and every object was lit
     * with the first one's (zero) lights -- the whole scene rendered black. */
    u32 nk = 0;
    /* The cube units go to the decompiler: it declares them TextureCube and
     * samples them with the 3-component direction. The text retargeting
     * below looked for declarations the decompiler no longer emits, so every
     * cube was sampled as a 2D texture -- with a cube view bound, Code
     * Veronica's water reflection read one flat bluish colour. */
    int n = rsx_fp_decompile_alloc(ucode, j->ucode_bytes, exp32 ? 0x40u : 0u, cube_mask,
                                  1, &j->hlsl, &j->hlsl_cap, &nk);
    const int buffered = n > 0 && nk <= FP_MAX_K;
    if (!buffered)
        n = rsx_fp_decompile_alloc(ucode, j->ucode_bytes, exp32 ? 0x40u : 0u, cube_mask,
                                   0, &j->hlsl, &j->hlsl_cap, NULL);
    if (n <= 0) { static int _e=0; if(_e++<16)
        fprintf(stderr, "[FP] decompile fail (fp=0x%08X, program=%u bytes, HLSL capacity=%u, result=%d)%c",
                fp_addr, j->ucode_bytes, j->hlsl_cap, n, 10); return FPB_FAIL; }
    /* Reserve the prelude and room for post-translation edits. WPOS is the
     * only normal edit that can expand at every instruction; count its
     * occurrences rather than assuming a fixed amount of slack suffices. */
    size_t needed = strlen(j->hlsl) + k_pre + 8192u;
    const char* wpos = "float4(input.position.xyz, 1.0 / input.position.w)";
    for (const char* p = j->hlsl; (p = strstr(p, wpos)) != NULL; p += strlen(wpos))
        needed += 128u;
    if (needed > RSX_FP_HLSL_LIMIT) return FPB_FAIL;
    u32 cap = j->hlsl_cap;
    while (cap < needed) cap = cap > RSX_FP_HLSL_LIMIT / 2 ? RSX_FP_HLSL_LIMIT : cap * 2;
    if (cap != j->hlsl_cap) {
        char* grown = (char*)realloc(j->hlsl, cap);
        if (!grown) return FPB_FAIL;
        j->hlsl = grown; j->hlsl_cap = cap;
    }
    char* hlsl = j->hlsl;
    const u32 hlsl_cap = j->hlsl_cap;
    memmove(hlsl + k_pre, hlsl, strlen(hlsl) + 1);
    if (buffered) {
        char* body = hlsl + k_pre;
        char* cb = strstr(body, "cbuffer PSConstants");
        char* cbe = cb ? strstr(cb, "};\n") : NULL;
        if (cbe) memmove(cb, cbe + 3, strlen(cbe + 3) + 1);
        /* fp_constants[ -> fp_k[ (shorter, so compact in place) */
        char* w = body; const char* r = body;
        while (*r) {
            if (!strncmp(r, "fp_constants[", 13)) { memcpy(w, "fp_k[", 5); w += 5; r += 13; }
            else *w++ = *r++;
        }
        *w = '\0';
    }
    /* An opcode the decompiler does not know produces a program that writes
     * nothing and exports zero. Code Veronica's full-screen grade pass
     * (draw ~1465, rewrites the scene RT in place) came out that way and
     * blacked out the whole gameplay image. Log the raw ucode so the opcode
     * can be implemented, and tell the caller to skip the draw: an ungraded
     * scene beats a black one. */
    if (strstr(hlsl + k_pre, "unhandled FP opcode")) {
        static int _w = 0;
        if (_w++ < 16) {
            const u8* uc = ucode;
            fprintf(stderr, "[FP] UNHANDLED opcode in fp=0x%08X instrs=%d (draw skipped) raw:", fp_addr, n);
            for (u32 _b = 0; _b < 128 && _b < j->ucode_bytes; _b++) fprintf(stderr, "%s%02X", (_b & 15) ? "" : " ", uc[_b]);
            fprintf(stderr, "%c", 10);
            const char* t = strstr(hlsl + k_pre, "/* TODO");
            if (t) { const char* e = strchr(t, 10); fprintf(stderr, "[FP]   %.*s%c", e ? (int)(e - t) : 60, t, 10); }
        }
        if (!getenv("FP_KEEP_UNHANDLED")) return FPB_SKIP;
    }
    /* A program that already declares b1 keeps pass-through coordinates. */
    if (strstr(hlsl + k_pre, "register(b1)")) memmove(hlsl, hlsl + k_pre, strlen(hlsl + k_pre) + 1);
    else {
        memcpy(hlsl, k_addr_prelude, k_pre);
        if(!precomputed_tex_edges_on())
            hlsl_replace_all(hlsl,hlsl_cap,"float rsx_addr1(float c, float m, float e) {",
                "float rsx_addr1(float c, float m, float n) { float e=n>0.0?0.5/n:0.0;");
        /* Declare the full fp_k[] only where a program needs it, so the
         * text -- and the on-disk shader cache -- of every other program
         * stays as it was. Same length: patched in place. */
        if (nk > 128) { char* d = strstr(hlsl, "fp_k[128]"); if (d) memcpy(d, "fp_k[512]", 9); }
    }
    /* sRGB colour output (cull bit 0x200, SET_SHADER_PACKER): encode every
     * export the way RSX does on write. Programs that write H-register
     * outputs come out in the PSOutput form; Code Veronica's film-grain pass
     * is one, and without the encode it left the scene in linear light
     * (mean 5.4 -> 0.4) and the LUT that follows turned it into a negative. */
#define RSX_SRGB_FN "float3 rsx_srgb(float3 c) { c = saturate(c); return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; } "
    if ((cull & 0x200u) && strstr(hlsl, "float4 main(PSInput input) : SV_TARGET")) {
        hlsl_replace_all(hlsl, hlsl_cap, "float4 main(PSInput input) : SV_TARGET",
            RSX_SRGB_FN "float4 main(PSInput input) : SV_TARGET");
        hlsl_replace_all(hlsl, hlsl_cap, "return (_o == _o) ? _o : (float4)0;",
            "_o = (_o == _o) ? _o : (float4)0; _o.rgb = rsx_srgb(_o.rgb); return _o;");
    } else if ((cull & 0x200u) && strstr(hlsl, "PSOutput main(PSInput input)")) {
        char enc[256] = "";
        for (int t = 0; t < 4; t++) {
            char decl[32], line[64];
            snprintf(decl, sizeof decl, "float4 t%d : SV_TARGET%d", t, t);
            if (!strstr(hlsl, decl)) continue;
            snprintf(line, sizeof line, "_out.t%d.rgb = rsx_srgb(_out.t%d.rgb); ", t, t);
            strncat(enc, line, sizeof enc - strlen(enc) - 1);
        }
        strncat(enc, "return _out;", sizeof enc - strlen(enc) - 1);
        hlsl_replace_all(hlsl, hlsl_cap, "PSOutput main(PSInput input)",
            RSX_SRGB_FN "PSOutput main(PSInput input)");
        hlsl_replace_all(hlsl, hlsl_cap, "return _out;", enc);
    }
#undef RSX_SRGB_FN
    /* Guest alpha test (cull bit 0x100000, rsx_alpha_key): D3D12 has no fixed
     * one, so the program discards what fails rsx_alpha_k (x enable, y ref,
     * z func - 0x200) on target 0's alpha, compared as RPCS3 does. The edit
     * that used to do this lived in the decompiler this one replaced. */
    if ((cull & 0x100000u) && strstr(hlsl, "float4 rsx_alpha_k;")) {
        const int mrt = strstr(hlsl, "PSOutput main(PSInput input)") != NULL;
        char* ret = NULL;
        for (char* p = strstr(hlsl, "return "); p; p = strstr(p + 1, "return "))
            if (!strncmp(p + 7, mrt ? "_out;" : "_o;", mrt ? 5 : 3) ||
                (!mrt && !strncmp(p + 7, "(_o == _o)", 10)))
                ret = p;
        if (ret) {
            char ins[600];
            const int len = snprintf(ins, sizeof ins,
                "{ float _ra = %s, _rr = rsx_alpha_k.y; uint _rf = (uint)rsx_alpha_k.z; "
                "if (rsx_alpha_k.x != 0.0 && !(_rf == 7u || (_rf == 1u && _ra < _rr) || "
                "(_rf == 2u && _ra == _rr) || (_rf == 3u && _ra <= _rr) || (_rf == 4u && _ra > _rr) || "
                "(_rf == 5u && _ra != _rr) || (_rf == 6u && _ra >= _rr))) discard; } ",
                mrt ? "_out.t0.a" : "((_o.a == _o.a) ? _o.a : 0.0)");
            const size_t tail = strlen(ret) + 1;
            if (len > 0 && (size_t)(ret - hlsl) + (size_t)len + tail <= hlsl_cap) {
                memmove(ret + len, ret, tail);
                memcpy(ret, ins, (size_t)len);
            }
        }
    }
    if (getenv("CV_VIDEO_CAPTURE")) { char pn[100];snprintf(pn,sizeof pn,"ps_%08X_%08X.hlsl",fp_addr,uhash);FILE* f=fopen(pn,"w");if(f){fprintf(f,"/* vs_idx=%d exports32=%d */\n%s",vs_idx,exp32,hlsl);fclose(f);} }
    /* Explicit diagnostic only: isolate interpolation and RT sampling. */
    if (vs_idx == 3 && getenv("CV_COMPOSITE_PROBE")) {
        char* ret = strstr(hlsl, "return (_o == _o) ? _o : (float4)0;");
        if (ret) {
            const char* mode=getenv("CV_COMPOSITE_PROBE");
            snprintf(ret,hlsl_cap-(size_t)(ret-hlsl),
                "%s\n}\n",atoi(mode)==1 ?
                "return float4(input.tc0.xyz,1.0);" :
                "return float4(rsx_tex[0].Sample(rsx_samp[0],input.tc1.xy).rgb,1.0);");
        }
    }
    /* FP_LIST=1: every program the title actually compiles, with its size. A
     * fragment program that hangs the GPU shows up here as an implausible
     * instruction count long before it shows up as a TDR. */
    { static int fl = -1; if (fl < 0) fl = getenv("FP_LIST") ? 1 : 0;
      if (fl) fprintf(stderr, "[FPLIST] fp=0x%08X instrs=%d hlsl=%u bytes%c",
                      fp_addr, n, (unsigned)strlen(hlsl), 10); }
    if (getenv("FP_DUMP")) { static int _d=0; if (_d++ < 4) {
        FILE* f = fopen("fp_dump.hlsl", _d==1 ? "w" : "a");
        if (f) {
            const u8* uc = ucode;
            fprintf(f, "/* fp_addr=0x%08X vmoff=0x%08X raw:", fp_addr, off);
            for (u32 _b = 0; _b < 32 && _b < j->ucode_bytes; _b++) fprintf(f, " %02X", uc[_b]);
            fprintf(f, " */\n%s\n", hlsl); fclose(f);
        } } }

    /* FP_PICK=<hex fp_addr>: write just that program, whatever order it compiles
     * in. fp_dump.hlsl keeps only the first four, same trap as vp2_dump.hlsl. */
    { static const char* fpk = (const char*)1; static u32 wantf = 0; static int done = 0;
      if (fpk == (const char*)1) { fpk = getenv("FP_PICK");
                                   wantf = fpk ? (u32)strtoul(fpk, NULL, 16) : 0; }
      if (wantf && !done && fp_addr == wantf) { done = 1;
          FILE* f = fopen("duck_fp.hlsl", "w");
          if (f) { fprintf(f, "/* fp_addr=0x%08X, %d instrs */%s", fp_addr, n, hlsl);
                   fclose(f); }
          fprintf(stderr, "[DUCKFP] wrote duck_fp.hlsl (fp=0x%08X, %d instrs)%c",
                  fp_addr, n, 10);
      } }

    /* Debug FP_ONE=<hex fp_addr>: force that program's colour output to
     * all-ones (e.g. wave's colour-detect -> full mask -> island borders
     * everywhere -> the water sim must visibly radiate if it works). */
    { const char* f1 = getenv("FP_ONE");
      if (f1 && (u32)strtoul(f1, NULL, 16) == fp_addr) {
          /* Match the assignment, not one exact spelling of it: the final
           * register is r[N] for some programs and h[N] for others, and the old
           * fixed "= r[0];" anchor silently did nothing for every h[] shader --
           * so this switch appeared to have no effect and was useless for
           * identifying which program draws what. Rewrite the expression with a
           * proper length-changing splice. */
          char* rp = strstr(hlsl, "_po.c0 = ");
          if (rp) {
              char* semi = strchr(rp, ';');
              if (semi) {
                  static const char rep[] = "_po.c0 = (1.0).xxxx";
                  size_t oldlen = (size_t)(semi - rp), newlen = sizeof(rep) - 1;
                  size_t tail = strlen(semi) + 1;
                  if ((rp - hlsl) + newlen + tail < hlsl_cap) {
                      memmove(rp + newlen, semi, tail);
                      memcpy(rp, rep, newlen);
                  }
                  (void)oldlen;
              }
          }
      } }
    /* FP_TEX=1: make every program output its unit-0 texture sample verbatim.
     * Separates "the texture is wrong" from "the shading tints it" in one run,
     * without having to identify which program draws which surface first. */
    /* FP_SHOW=<hlsl expr>: replace a program's colour output with an arbitrary
     * expression (e.g. FP_SHOW=h[1]) so an intermediate register can be looked
     * at directly. Restrict with FP_TEX_FP=<hex>. Reading a 69-instruction
     * shader to guess which term carries a bad value is slower and less certain
     * than displaying the terms. */
    /* FP_KILL=<hex fp_addr>: make that program discard every fragment, so what
     * it covers becomes visible. Distinguishes "this surface shades black" from
     * "this surface is correct and something else is missing behind it". */
    { const char* kf = getenv("FP_KILL");
      if (kf && (u32)strtoul(kf, NULL, 16) == fp_addr) {
          char* rp = strstr(hlsl, "PSOut _po;");
          if (rp) {
              const char* ins = "discard; ";
              size_t nl = strlen(ins), tail = strlen(rp) + 1;
              if ((size_t)(rp - hlsl) + nl + tail < hlsl_cap) {
                  memmove(rp + nl, rp, tail);
                  memcpy(rp, ins, nl);
                  fprintf(stderr, "[FPKILL] fp=0x%X discards%c", fp_addr, 10);
              }
          }
      } }
    /* 2D fetches through rsx_s2d (see the prelude): Y16_X16 reads of colour
     * targets are filtered as 16-bit values. RSX_S2D_OFF=1 keeps plain Sample. */
    { static int off = -1; if (off < 0) off = getenv("RSX_S2D_OFF") ? 1 : 0;
      if (!off)
          for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
              char f[64], r[80];
              snprintf(f, sizeof f, "rsx_tex[%d].Sample(rsx_samp[%d], ", _u, _u);
              snprintf(r, sizeof r, "rsx_s2d(rsx_tex[%d], rsx_samp[%d], %d, ", _u, _u, _u);
              hlsl_replace_all(hlsl, hlsl_cap, f, r);
              snprintf(f, sizeof f, "rsx_tex%d.Sample(rsx_samp[%d], RSX_ADDR(", _u, _u);
              snprintf(r, sizeof r, "rsx_s2d(rsx_tex%d, rsx_samp[%d], %d, RSX_ADDR(", _u, _u, _u);
              hlsl_replace_all(hlsl, hlsl_cap, f, r);
              /* rsx_s2d already returns logical channels, including gamma on
               * ordinary textures. Skip the outer decode for this fetch only;
               * cube, bias/LOD and non-wrapped fetches keep RSX_TEXOUT. */
              snprintf(f, sizeof f, "RSX_TEXOUT(%d, rsx_s2d(", _u);
              snprintf(r, sizeof r, "RSX_S2D_OUT(%d, rsx_s2d(", _u);
              hlsl_replace_all(hlsl, hlsl_cap, f, r);
          } }
    /* Cube units: 6-slice arrays sampled face by face (rsx_cube, see the
     * prelude). The SRVs match (srv_write_ex / the slot view). */
    if (rsx_cube_as_array())
        for (int _u = 0; _u < FP_TEX_UNITS; _u++)
            if (cube_mask & (1u << _u)) {
                char f[64], r[80];
                snprintf(f, sizeof f, "TextureCube rsx_tex%d ", _u);
                snprintf(r, sizeof r, "Texture2DArray rsx_tex%d ", _u);
                hlsl_replace_all(hlsl, hlsl_cap, f, r);
                snprintf(f, sizeof f, "rsx_tex%d.Sample(rsx_samp[%d], RSX_CUBEDIR(", _u, _u);
                snprintf(r, sizeof r, "rsx_cube(rsx_tex%d, rsx_samp[%d], %d, RSX_CUBEDIR(", _u, _u, _u);
                hlsl_replace_all(hlsl, hlsl_cap, f, r);
                snprintf(f, sizeof f, "rsx_tex%d.SampleBias(rsx_samp[%d], RSX_CUBEDIR(", _u, _u);
                snprintf(r, sizeof r, "rsx_cube_b(rsx_tex%d, rsx_samp[%d], RSX_CUBEDIR(", _u, _u);
                hlsl_replace_all(hlsl, hlsl_cap, f, r);
                snprintf(f, sizeof f, "rsx_tex%d.SampleLevel(rsx_samp[%d], RSX_CUBEDIR(", _u, _u);
                snprintf(r, sizeof r, "rsx_cube_l(rsx_tex%d, rsx_samp[%d], RSX_CUBEDIR(", _u, _u);
                hlsl_replace_all(hlsl, hlsl_cap, f, r);
            }
    /* RSX_CUBE_FLIP=x|y|z|xz|...: mirror the cube lookup direction on those
     * axes (an orientation test for Code Veronica's water reflection). */
    { const char* cf = getenv("RSX_CUBE_FLIP");
      if (cf && *cf) {
          char repl[96];
          snprintf(repl, sizeof repl, "#define RSX_CUBEDIR(v) ((v) * float3(%s, %s, %s))",
                   strchr(cf, 'x') ? "-1.0" : "1.0", strchr(cf, 'y') ? "-1.0" : "1.0",
                   strchr(cf, 'z') ? "-1.0" : "1.0");
          hlsl_replace_all(hlsl, hlsl_cap, "#define RSX_CUBEDIR(v) (v)", repl);
      } }
    { const char* sh = getenv("FP_SHOW");
      const char* only = getenv("FP_TEX_FP");
      const char* code = getenv("FP_SHOW_CODE");
      if (sh && (!only || (u32)strtoul(only, NULL, 16) == fp_addr) &&
          (!code || (u32)strtoul(code, NULL, 16) == uhash)) {
          /* Buffered guest FPs now export through _o, not the legacy _po.
           * Select by code as well as address: the guest relocates shaders
           * as it loads a room, so an address-only probe can hit another pass. */
          char* out = strstr(hlsl, "float4 _o = ");
          if (out) {
              char* semi = strchr(out, ';');
              if (semi) {
                  char rep[1024];
                  int len = snprintf(rep, sizeof rep, "float4 _o = float4((%s).xyz, 1.0)", sh);
                  if (len > 0 && len < (int)sizeof rep &&
                      (size_t)(out - hlsl) + (size_t)len + strlen(semi) + 1 < hlsl_cap) {
                      memmove(out + len, semi, strlen(semi) + 1);
                      memcpy(out, rep, (size_t)len);
                      fprintf(stderr, "[FPSHOW] fp=0x%X code=0x%X buffered output probe enabled%c",
                              fp_addr, uhash, 10);
                  }
              }
          }
          char* rp = strstr(hlsl, "_po.c0 = ");
          if (rp) { char* semi = strchr(rp, ';');
            if (semi) {
                char rep[256];
                snprintf(rep, sizeof rep, "_po.c0 = float4((%s).xyz, 1.0)", sh);
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                if ((size_t)(rp - hlsl) + newlen + tail < hlsl_cap) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                    fprintf(stderr, "[FPSHOW] fp=0x%X output replaced with %s%c",
                            fp_addr, sh, 10);
                }
            } }
      } }
    /* RES_SCALE: SV_Position is in host pixels; WPOS must stay in guest ones.
     * Always, not only when the game started scaled: the scale can change
     * while running (the options menu), and a program compiled at 1x then
     * read the lighter's screen-space light buffer at the wrong place. At 1x
     * rsx_alpha_k.w is 0 and the factor 1. */
    hlsl_replace_all(hlsl, hlsl_cap,
        "float4(input.position.xyz, 1.0 / input.position.w)",
        "float4(input.position.xy * (rsx_alpha_k.w > 0.0 ? rsx_alpha_k.w : 1.0), input.position.z, 1.0 / input.position.w)");
    /* (Cube units are declared and sampled by the decompiler, which gets the
     * mask, and turned into face-by-face array reads above.) */
    if (cube_mask) {
        static int _n = 0; if (_n++ < 4)
            fprintf(stderr, "[CUBE] fp=0x%X compiled with cube units mask 0x%X%c",
                    fp_addr, cube_mask, 10);
    }
    /* FP_IDCOLOR=1: give every fragment program a distinct flat colour derived
     * from its address. One frame then shows which program paints which surface,
     * instead of one run per candidate to test them by elimination. */
    if (getenv("FP_IDCOLOR")) {
        char* rp = strstr(hlsl, "_po.c0 = ");
        if (rp) {
            char* semi = strchr(rp, ';');
            if (semi) {
                /* Mix properly: every fp address in a title tends to share low
                 * bits (they all end 0x01/0x81 here), so a single multiply left
                 * one channel constant across all programs and made distinct
                 * shaders look like the same colour. */
                u32 hsh = fp_addr * 2654435761u;
                hsh ^= hsh >> 13; hsh *= 0x5BD1E995u; hsh ^= hsh >> 15;
                /* Force the top bit on in each channel: a hash that happens to
                 * land near black is indistinguishable from "this surface was
                 * never painted", which is the exact question the mode exists
                 * to answer. */
                float cr = (float)(((hsh >> 16) & 0x7F) | 0x80) / 255.0f;
                float cg = (float)(((hsh >>  8) & 0x7F) | 0x80) / 255.0f;
                float cb = (float)(((hsh      ) & 0x7F) | 0x80) / 255.0f;
                char rep[128];
                snprintf(rep, sizeof rep, "_po.c0 = float4(%.3f,%.3f,%.3f,1.0)", cr, cg, cb);
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                if ((size_t)(rp - hlsl) + newlen + tail < hlsl_cap) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                    fprintf(stderr, "[FPID] fp=0x%X -> rgb(%.0f,%.0f,%.0f)%c",
                            fp_addr, cr*255, cg*255, cb*255, 10);
                }
            }
        }
    }
    /* FP_TEX=<unit>: output that unit's sample verbatim. FP_TEX_FP=<hex> limits
     * it to one program, so one surface can be inspected without repainting the
     * whole scene. FP_TEX_UV=1 samples with the same texcoord the program uses
     * for that unit rather than tc0. */
    if (getenv("FP_TEX") && (!getenv("FP_TEX_FP") ||
        (u32)strtoul(getenv("FP_TEX_FP"), NULL, 16) == fp_addr)) {
        int _un = atoi(getenv("FP_TEX"));
        if (_un < 0 || _un > 3) _un = 0;
        int ret_form = 0;
        char* rp = strstr(hlsl, "_po.c0 = ");
        if (!rp) {
            rp = strstr(hlsl, "return (_o == _o) ? _o : (float4)0;");
            ret_form = rp != NULL;
        }
        if (rp) {
            char* semi = strchr(rp, ';');
            if (semi) {
                char rep[160];
                /* FP_TEX_UV=1: sample at a fixed centre UV instead of the
                 * interpolated texcoord. If the texture is populated but the
                 * texcoords never arrive, the normal path samples texel (0,0)
                 * -- usually black -- and looks exactly like an empty texture.
                 * This tells the two apart. */
                if (ret_form) {
                    if (getenv("FP_TEX_UV"))
                        snprintf(rep, sizeof rep,
                                 "return rsx_tex[%d].Sample(rsx_samp[%d], float2(0.5, 0.5))",
                                 _un, _un);
                    else
                        snprintf(rep, sizeof rep,
                                 "return rsx_tex[%d].Sample(rsx_samp[%d], input.tc%d.xy)",
                                 _un, _un, _un);
                } else if (getenv("FP_TEX_UV"))
                    snprintf(rep, sizeof rep,
                             "_po.c0 = rsx_tex%d.Sample(rsx_samp%d, float2(0.5, 0.5))",
                             _un, _un);
                else
                    snprintf(rep, sizeof rep,
                             "_po.c0 = rsx_tex%d.Sample(rsx_samp%d, input.tc0.xy * rsx_texscale[%d].xy)",
                             _un, _un, _un);
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                fprintf(stderr, "[FP_TEX] program 0x%X rewritten to sample unit %d%c",
                        fp_addr, _un, 10);
                if ((size_t)(rp - hlsl) + newlen + tail < hlsl_cap) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                }
            }
        }
    }
    /* FP_SOLID=1: replace the fragment output in every guest shader with a
     * visible colour. Supports both decompiler output forms (PSOut assignment
     * and direct return); distinguishes failed drawing from black input data. */
    if (getenv("FP_SOLID")) {
        char* rp = strstr(hlsl, "_po.c0 = ");
        int ret_form = 0;
        if (!rp) {
            rp = strstr(hlsl, "return (_o == _o) ? _o : (float4)0;");
            ret_form = rp != NULL;
        }
        if (rp) {
            char* semi = strchr(rp, ';');
            if (semi) {
                const char* rep = ret_form
                    ? "return float4(1.0, 0.0, 1.0, 1.0)"
                    : "_po.c0 = float4(1.0, 0.0, 1.0, 1.0)";
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                if ((size_t)(rp - hlsl) + newlen + tail < hlsl_cap) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                    fprintf(stderr, "[FPSOLID] program 0x%X output forced magenta%c",
                            fp_addr, 10);
                }
            }
        }
    }
    /* FP_FORCE=1: replace the translated body with solid magenta -- isolates
     * geometry/transform problems from texture/blend problems. */
    if (getenv("FP_FORCE")) {
        char* rp = strstr(hlsl, "return r[0];");
        if (rp) memcpy(rp, "return float4(1,0,1,1)", 23);
    }
    if (s_lighter_probe && lighter_shadow_code(uhash)) {
        /* These programs export the light's visibility/attenuation in r0.
         * Override only in the explicit probe. Runtime uniform avoids PSO
         * recompilation on F10; normal launch does not inject this expression. */
        hlsl_replace_all(hlsl, hlsl_cap, "float4 _o0 = r[0];",
            "float4 _o0 = rsx_texaddr[7].w == -1234.0 ? float4(1,1,1,1) : r[0];");
        hlsl_replace_all(hlsl, hlsl_cap, "float4 _o = r[0];",
            "float4 _o = rsx_texaddr[7].w == -1234.0 ? float4(1,1,1,1) : r[0];");
        fprintf(stderr, "[LIGHTER] diagnostic visibility shader code=%08X\n", uhash);
    }
    ID3DBlob* pb = NULL; ID3DBlob* e = NULL;
    HRESULT hr = shader_compile_cached(hlsl, strlen(hlsl), "guest_fp", "ps_5_0", &pb, &e);
    if (FAILED(hr) || !pb) {
        static int _e2=0; if (_e2++<16)
            printf("[FP] PS compile FAIL (fp=0x%08X): %s\n", fp_addr,
                   e ? (const char*)e->lpVtbl->GetBufferPointer(e) : "?");
        /* Keep the failing source: the error names a line of THIS text. */
        { static int _dumped = 0;
          if (_dumped < 8) {
              char pn[64]; snprintf(pn, sizeof pn, "fpfail_%d_%08X.hlsl", _dumped, uhash);
              FILE* f = fopen(pn, "w");
              if (f) { fprintf(f, "/* fp=0x%08X: %s */\n%s", fp_addr,
                               e ? (const char*)e->lpVtbl->GetBufferPointer(e) : "?", hlsl);
                       fclose(f); _dumped++; } } }
        if (e) e->lpVtbl->Release(e);
        return FPB_FAIL;   /* cached as a failure by the caller */
    }
    if (e) e->lpVtbl->Release(e);

    D3D12_INPUT_ELEMENT_DESC il[16];
    for (int _i = 0; _i < 16; _i++) {
        il[_i].SemanticName = "ATTR"; il[_i].SemanticIndex = _i;
        il[_i].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; il[_i].InputSlot = 0;
        il[_i].AlignedByteOffset = (UINT)(_i * 16);
        il[_i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        il[_i].InstanceDataStepRate = 0;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = s_d3d.vp_root_sig;
    pd.VS.pShaderBytecode = vsb->lpVtbl->GetBufferPointer(vsb);
    pd.VS.BytecodeLength  = vsb->lpVtbl->GetBufferSize(vsb);
    pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb);
    pd.PS.BytecodeLength  = pb->lpVtbl->GetBufferSize(pb);
    pd.InputLayout.pInputElementDescs = il; pd.InputLayout.NumElements = vf_enabled() ? 0 : 16;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    /* Near/far clipping, as RSX does by default (SET_ZMIN_MAX_CONTROL, which
     * Code Veronica never writes). The zero-initialised desc left it OFF, so
     * geometry nearer than the near plane was clamped and drawn instead of
     * clipped: in the prison cutscene a wall behind the camera covered two
     * thirds of the screen. vp_posscale/offset already map z to D3D's [0, w].
     * DEPTH_CLIP=0 restores the old behaviour. */
    /* The guest VS now clips -w <= z <= w itself (SV_ClipDistance0, as RSX
     * does), so D3D's depth clip stays off here: its 0 <= z' plane removed
     * geometry RSX keeps with its depth clamped to 0. RSX_ZCLIP=d3d restores
     * D3D's clip for comparison. */
    { static int dc = -1;
      if (dc < 0) { const char* e = getenv("DEPTH_CLIP"); const char* z = getenv("RSX_ZCLIP");
                    dc = (e && *e == '0') ? 0 : (z && !strcmp(z, "d3d")) ? 1 : 0; }
      pd.RasterizerState.DepthClipEnable = dc ? TRUE : FALSE; }
    /* Guest face culling. CULL_OFF=1 forces double-sided, the old
     * unconditional behaviour -- geometry that vanishes under culling is a
     * winding/front-face problem, not a missing draw. */
    { static int _co = -1; if (_co < 0) _co = getenv("CULL_OFF") ? 1 : 0;
      pd.RasterizerState.CullMode = (!_co && (cull & 1u))
          ? ((cull & 2u) ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK)
          : D3D12_CULL_MODE_NONE;
      /* CULL_FLIP=1: invert the front-face convention. Our VP path may hand
       * D3D the opposite winding from the guest's, in which case honouring
       * CULL_FACE culls exactly the faces it should keep. */
      static int _cfl = -1; if (_cfl < 0) _cfl = getenv("CULL_FLIP") ? 1 : 0;
      pd.RasterizerState.FrontCounterClockwise =
          (((cull & 4u) ? 1 : 0) ^ _cfl) ? TRUE : FALSE; }
    /* Blend per the guest's packed key (see rsx_blend_key): dbgfont text needs
     * straight alpha; demosaic's effect passes blend OFF; DeferredShading's
     * light accumulation is additive ONE,ONE. */
    pd.BlendState.RenderTarget[0].BlendEnable    = (blend & 1) ? TRUE : FALSE;
    pd.BlendState.RenderTarget[0].SrcBlend       = (blend & 1) ? (D3D12_BLEND)((blend >> 1)  & 0x1F) : D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlend      = (blend & 1) ? (D3D12_BLEND)((blend >> 6)  & 0x1F) : D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOp        = (blend & 1) ? (D3D12_BLEND_OP)((blend >> 21) & 0x7) : D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].SrcBlendAlpha  = (blend & 1) ? (D3D12_BLEND)((blend >> 11) & 0x1F) : D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlendAlpha = (blend & 1) ? (D3D12_BLEND)((blend >> 16) & 0x1F) : D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOpAlpha   = (blend & 1) ? (D3D12_BLEND_OP)((blend >> 24) & 0x7) : D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    /* cull bit 8 marks a line draw (see the call site): lines need a LINE
     * pipeline, and face culling means nothing for them. */
    pd.PrimitiveTopologyType = (cull & 0x100u) ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                               : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (cull & 0x100u) pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.NumRenderTargets = (UINT)nrt;
    for (int _r = 0; _r < nrt; _r++) {
        pd.RTVFormats[_r] = rtfmt;
        /* Zero-init leaves RenderTargetWriteMask 0 on secondary targets --
         * every MRT-B write would be masked off. Mirror RT0's blend state. */
        pd.BlendState.RenderTarget[_r] = pd.BlendState.RenderTarget[0];
    }
    /* Guest colour write mask (RGBA nibble, already D3D-ordered). cmask is 0xF
     * by default (unset colour_mask register decodes to all-on); a literal 0
     * therefore means the guest EXPLICITLY masked every channel -- a depth-only
     * pass (e.g. DeferredShading's shadow-map generation). Honour it: forcing 0
     * back to 0xF splatters the depth pass's fragment colour onto the target. */
    /* CMASK_FORCE=1: ignore the guest colour mask (write RGBA on every draw).
     * A mis-decoded mask makes geometry rasterize correctly and write nothing,
     * which is indistinguishable from "the draw never happened". */
    { static int _cf = -1; if (_cf < 0) _cf = getenv("CMASK_FORCE") ? 1 : 0;
      if (_cf) cmask = 0xF; }
    for (int _r = 0; _r < nrt; _r++)
        pd.BlendState.RenderTarget[_r].RenderTargetWriteMask = (UINT8)(cmask & 0xF);
    pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    if (cull & 0x1000u) {   /* guest depth state (rsx_depth_key) */
        pd.DepthStencilState.DepthEnable = (cull & 0x2000u) ? TRUE : FALSE;
        pd.DepthStencilState.DepthWriteMask = (cull & 0x4000u) ? D3D12_DEPTH_WRITE_MASK_ALL
                                                                : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc = (D3D12_COMPARISON_FUNC)(((cull >> 16) & 7u) + 1u);
    }
    pd.DepthStencilState.StencilEnable = FALSE;
    /* DEPTH_OFF=1: drop the depth test for guest-FP draws, so submission order
     * alone decides what is on top. Paired with DRAW_LAST_TEX this puts one
     * object in front of everything and answers "is it merely occluded?" --
     * which reordering alone cannot, because a relocated draw still fails the
     * depth test against whatever is already in the buffer. Diagnostic. */
    { static int doff = -1;
      if (doff < 0) { const char* e = getenv("DEPTH_OFF"); doff = e ? atoi(e) : 0; }
      if (doff) { pd.DepthStencilState.DepthEnable = FALSE;
                  pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; } }
    pd.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = NULL;
    hr = pso_create_cached(&pd, &pso);
    pb->lpVtbl->Release(pb);
    if (FAILED(hr)) {
        static int _e3=0; if (_e3++<16) printf("[FP] PSO FAIL (fp=0x%08X, 0x%08lX)\n", fp_addr, hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED) dred_report("fragment PSO creation");
        return FPB_FAIL;
    }
    { static int _ok=0; if (_ok++<32) printf("[FP] guest FP pipeline ready (fp=0x%08X)\n", fp_addr); }
    j->pso = pso;
    return FPB_OK;
}

/* Draws whose pipeline is built inline even with background builds on: the
 * final composite onto the display and full-screen passes (one quad, no
 * depth test -- colour grade, exposure, bloom). Skipping one of those for a
 * frame changes the whole picture, which showed as a brief screen flicker;
 * a missing object for a frame is barely visible. */
static int dr_fp_urgent(const D3D12DrawRecord* dr)
{
    return dr->rt_off == 0 || (!dr->dtest && dr->vertex_count <= 6);
}

static ID3D12PipelineState* vp_get_fp_pso(int vs_idx, u32 fp_addr, u32 blend, int nrt,
                                          DXGI_FORMAT rtfmt, int exp32, u32 cmask, u32 cull,
                                          u32 cube_mask, const u8* snap, int urgent)
{
    s_fp_skip_draw = 0;s_fp_result_index=-1;
    if (nrt < 1) nrt = 1; if (nrt > 4) nrt = 4;
    if (rtfmt == 0) rtfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);
    if (!fp_addr || !vm_base) return NULL;
    { static int _off=-1; if(_off<0)_off=getenv("FP_OFF")?1:0; if(_off) return NULL; }
    /* Resolve the VS: the draw's cached per-VP blob, else the primary. */
    ID3DBlob* vsb = NULL; u32 vs_hash = 0;
    if (vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n) {
        VPVSEntry* v = &s_d3d.vp_vs[vs_idx];
        if (v->pending) {   /* its VS is still compiling */
            if (!urgent) { s_fp_skip_draw = 1; return NULL; }
            const double t0 = perf_now();
            while (v->pending && perf_now() - t0 < 2.0) Sleep(1);
            InterlockedAdd(&s_hd_wait_us, (LONG)((perf_now() - t0) * 1e6));
        }
        vsb = (ID3DBlob*)InterlockedCompareExchangePointer((PVOID volatile*)&v->vs, NULL, NULL);
        vs_hash = v->hash;
    }
    if (!vsb) { vsb = s_d3d.vp_vs_blob; vs_idx = -1; vs_hash = 0; }
    if (!vsb) return NULL;

    /* SET_SHADER_PROGRAM low bits = location+1 (same as textures): 1 = LOCAL
     * (VRAM), 2 = MAIN (gcm/cube emits 0x00B90001 for its VRAM-resident FP). */
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    u32 off = cellGcmResolveLocated((fp_addr & 0x3u) == 1, fp_addr & ~0x3u);
    if (off == 0xFFFFFFFFu) return NULL;
    /* The ucode as it was when the draw was recorded (see fp_snapshot). */
    const u8* ucode = snap ? snap : vm_base + off;

    /* Content hash: inline constants are patched in place per frame (wave's
     * stamp), so identity is the BYTES, not the address. */
    /* Hash the CODE, not the constants: with fp_k[] hoisted into the per-draw
     * constant buffer the compiled shader is invariant under constant changes,
     * so hashing them made the pipeline key move every draw. */
    u32 uhash;
    u32 snap_hdr[4] = { 0 };
    if (snap) memcpy(snap_hdr, snap - 16, 16);
    u32 constants = 0;
    const u32 ucode_bytes = snap && snap_hdr[0] == FP_SNAP_MAGIC
        ? (constants = snap_hdr[3], snap_hdr[2]) : fp_guest_program_size(ucode, &constants);
    if (!ucode_bytes || ucode_bytes > RSX_FP_PROGRAM_LIMIT) {
        s_fp_skip_draw = 1;
        return NULL;
    }
    /* Programs exceeding b1's existing constant capacity use literals. Their
     * cache identity must include the values, or a larger program would reuse
     * the first draw's lights. Buffered programs retain the fast code hash. */
    const int literal_key = constants > FP_MAX_K ||
        (getenv("FP_CONSTBUF") && getenv("FP_CONSTBUF")[0] == '0');
    if (constants > FP_MAX_K) { static int n = 0; if (n++ < 30)
        printf("[FPK] fp=0x%08X has %u constants (> %d): keyed on its values%c", fp_addr, constants, FP_MAX_K, 10); }
    if (snap && snap_hdr[0] == FP_SNAP_MAGIC &&
        !literal_key) {
        uhash = snap_hdr[1];            /* hashed once, when the draw was recorded */
    } else {
        u32 usz = ucode_bytes;
        s_perf_pso_hashbytes += (int)usz;
        if (literal_key) {
            uhash = 2166136261u;
            const u8* up = ucode;
            for (u32 i = 0; i < usz; i++) { uhash ^= up[i]; uhash *= 16777619u; }
        } else {
            uhash = rsx_fp_code_hash(ucode, ucode_bytes);
        }
    }

    s_perf_pso_calls++;
    /* Keyed by content, not address: Code Veronica streams its programs
     * through a ring, so the same shader lands at a new address every frame
     * and an address key recompiled it each time (PSO creation was 60-90%
     * of the frame, 0.3-4 fps in gameplay). */
    /* Hash index over the cache: the linear scan walked up to 4096 entries
     * twice per draw (~4600 lookups a Code Veronica frame). */
    {
        /* Only the fields fp_key_hash/fp_key_eq read are set: zeroing the
         * whole entry took most of this function's time, twice per draw. */
        VPFPEntry key;
        key.vs_idx = vs_idx; key.vs_hash = vs_hash; key.gen = s_d3d.vp_gen;
        key.blend = blend; key.nrt = nrt; key.rtfmt = (u32)rtfmt; key.exp32 = exp32;
        key.ucode_hash = uhash; key.cmask = cmask; key.cull = cull; key.cube_mask = cube_mask;
        /* Consecutive draws mostly share one pipeline (~100 changes in a
         * 7000-draw frame): the last ready answer, per calling thread, before
         * probing the index. The entry is re-checked, as eviction may have
         * reused the slot. */
        static __declspec(thread) int last_hit = -1;
        static __declspec(thread) VPFPEntry last_key;
        if (last_hit >= 0 && last_hit < s_d3d.vp_fp_n && fp_key_eq(&last_key, &key) &&
            fp_key_eq(&s_d3d.vp_fp[last_hit], &key) && !s_d3d.vp_fp[last_hit].pending &&
            !s_d3d.vp_fp[last_hit].skip && s_d3d.vp_fp[last_hit].pso)
            { s_fp_result_index=last_hit;return s_d3d.vp_fp[last_hit].pso; }
        int hit = fp_index_find(&key);
        if (hit >= 0) {
            last_hit = hit; last_key = key;s_fp_result_index=hit;
            const VPFPEntry* he = &s_d3d.vp_fp[hit];
            if (he->pending) { s_fp_skip_draw = 1; InterlockedIncrement(&s_fp_async_skips); }
            else if (he->skip) s_fp_skip_draw = 1;
            return he->pso;
        }
    }
    s_perf_pso_miss++;      /* falls through to a full decompile + D3DCompile */
    /* PSOMISSDBG=1: on a miss, name the key field that differs from an existing
     * entry for the same program. "the cache misses" is not actionable; "it
     * misses on ucode_hash" is. */
    /* HASHDBG=1: how many DISTINCT ucode hashes each program produces. Inline
     * constants are patched into the shader bytes, so a program whose constants
     * animate yields a new hash -- and a new pipeline -- every time. */
    { static int hd = -1;
      if (hd < 0) { const char* e = getenv("HASHDBG"); hd = e ? atoi(e) : 0; }
      if (hd) { static u32 fps[32]; static u32 cnt[32]; static int nf = 0;
        int idx = -1;
        for (int i = 0; i < nf; i++) if (fps[i] == fp_addr) idx = i;
        if (idx < 0 && nf < 32) { idx = nf++; fps[idx] = fp_addr; cnt[idx] = 0; }
        if (idx >= 0) { cnt[idx]++;
          if ((cnt[idx] % 200) == 0)
            fprintf(stderr, "[HASHDBG] fp=0x%X has produced %u distinct compiles%c",
                    fp_addr, cnt[idx], 10); } } }
    /* PSOMISSDBG=1: every miss (up to 400), with each key field that differs
     * from the nearest cached entry of the same program, old -> new. A miss
     * with no same-program entry is a first compile and says so. */
    { static int md = -1;
      if (md < 0) { const char* e = getenv("PSOMISSDBG"); md = e ? atoi(e) : 0; }
      if (md) { static int n = 0;
        VPFPEntry* best = NULL; int best_d = 99;
        for (int i = 0; i < s_d3d.vp_fp_n; i++) {
            VPFPEntry* q = &s_d3d.vp_fp[i];
            if (q->fp_addr != fp_addr) continue;
            int d = (q->vs_idx != vs_idx) + (q->vs_hash != vs_hash) + (q->gen != s_d3d.vp_gen) +
                    (q->blend != blend) + (q->nrt != nrt) + (q->rtfmt != (u32)rtfmt) +
                    (q->exp32 != exp32) + (q->ucode_hash != uhash) + (q->cmask != cmask) +
                    (q->cull != cull) + (q->cube_mask != cube_mask);
            if (d < best_d) { best_d = d; best = q; }
        }
        if (n++ < 400) {
            if (!best) fprintf(stderr, "[PSOMISS] fp=0x%08X first compile (cache %d/%d)%c",
                               fp_addr, s_d3d.vp_fp_n, VP_FP_CACHE, 10);
            else {
                char why[512]; int w = 0; why[0] = 0;
#define PM(cond, fmt, a, b) if (cond && w < 440) w += snprintf(why + w, sizeof why - w, fmt, a, b)
                PM(best->vs_idx != vs_idx,        " vs_idx %d->%d", best->vs_idx, vs_idx);
                PM(best->vs_hash != vs_hash,      " vs_hash %08X->%08X", best->vs_hash, vs_hash);
                PM(best->gen != s_d3d.vp_gen,     " gen %u->%u", (unsigned)best->gen, (unsigned)s_d3d.vp_gen);
                PM(best->blend != blend,          " blend %08X->%08X", best->blend, blend);
                PM(best->nrt != nrt,              " nrt %d->%d", best->nrt, nrt);
                PM(best->rtfmt != (u32)rtfmt,     " rtfmt %u->%u", best->rtfmt, (u32)rtfmt);
                PM(best->exp32 != exp32,          " exp32 %d->%d", best->exp32, exp32);
                PM(best->ucode_hash != uhash,     " ucode %08X->%08X", best->ucode_hash, uhash);
                PM(best->cmask != cmask,          " cmask %X->%X", best->cmask, cmask);
                PM(best->cull != cull,            " cull %X->%X", best->cull, cull);
                PM(best->cube_mask != cube_mask,  " cube %X->%X", best->cube_mask, cube_mask);
#undef PM
                fprintf(stderr, "[PSOMISS] fp=0x%08X differs:%s (cache %d/%d)%c",
                        fp_addr, why, s_d3d.vp_fp_n, VP_FP_CACHE, 10);
            }
        } } }
    /* CUBEKEYDBG=1: which (program, cube mask) pairs are being compiled. If one
     * program shows several masks, the mask is unstable and is the reason the
     * pipeline cache thrashes with cube support on. */
    { static int ck = -1;
      if (ck < 0) { const char* e = getenv("CUBEKEYDBG"); ck = e ? atoi(e) : 0; }
      if (ck) { static u32 seen[64][2]; static int ns = 0; int known = 0;
        for (int i = 0; i < ns; i++)
            if (seen[i][0] == fp_addr && seen[i][1] == cube_mask) known = 1;
        if (!known && ns < 64) { seen[ns][0] = fp_addr; seen[ns][1] = cube_mask; ns++;
            fprintf(stderr, "[CUBEKEY] fp=0x%X cube_mask=0x%X (distinct pairs=%d)%c",
                    fp_addr, cube_mask, ns, 10); } } }
    VPFPEntry key;
    memset(&key, 0, sizeof key);
    key.fp_addr = fp_addr; key.vs_idx = vs_idx; key.vs_hash = vs_hash; key.gen = s_d3d.vp_gen;
    key.blend = blend; key.nrt = nrt; key.rtfmt = (u32)rtfmt; key.exp32 = exp32;
    key.ucode_hash = uhash; key.cmask = cmask; key.cull = cull; key.cube_mask = cube_mask;
    FpJob* j = (FpJob*)calloc(1, sizeof *j);
    if (!j) return NULL;
    j->next = NULL; j->key = key; j->off = off; j->pso = NULL;
    /* Only the program itself: the snapshot arena may end right after it. */
    j->ucode = (u8*)malloc(ucode_bytes);
    if (!j->ucode) { fp_job_free(j); return NULL; }
    j->ucode_bytes = ucode_bytes;
    memcpy(j->ucode, ucode, ucode_bytes);
    j->vsb = vsb; vsb->lpVtbl->AddRef(vsb);
    /* Background build: mark the key pending, skip this draw, and let a
     * worker build it. The finished pipeline is picked up by fp_async_drain
     * at the start of a later frame. */
    if (fp_async_on()) {
        j->urgent = urgent;
        fp_cache_insert(&key, NULL, 1, 0);
        fp_async_submit(j);
        s_fp_skip_draw = 1;
        InterlockedIncrement(&s_fp_async_skips);
        return NULL;
    }
    const double _hf0 = perf_now();
    int st = fp_build_pso(j);
    InterlockedAdd(&s_hd_fpi_us, (LONG)((perf_now() - _hf0) * 1e6)); InterlockedIncrement(&s_hd_fpi_n);
    ID3D12PipelineState* pso = j->pso;
    if (st == FPB_OK) pl_record_fp(&key, j->ucode, j->ucode_bytes);
    fp_job_free(j);
    fp_cache_insert(&key, pso, 0, st == FPB_SKIP);s_fp_result_index=s_d3d.vp_fp_n-1;
    if (st == FPB_SKIP) s_fp_skip_draw = 1;
    return pso;
}

/* ---------------------------------------------------------------------------
 * Shader preload. Every pipeline built is listed in pipelines.list, beside
 * pipelines.bin: the guest programs it came from (the fragment program's
 * ucode; the vertex program's ucode, start slot and vertex-texture units) and
 * its fixed-function key. At the next start, preload threads rebuild the
 * listed pipelines from the disk caches -- bytecode from the .cso files, the
 * PSO from pipelines.bin -- before the game asks for them, so an area seen in
 * an earlier session draws without a pipeline skipped or waited for.
 * The list holds guest programs, not translated text: a newer translator
 * makes its own HLSL (and .cso) from them, never runs a stale one.
 * SHADER_PRELOAD=bg (default) loads while the game runs, with a note in the
 * corner; wait holds the game at its start until done; 0 loads nothing (the
 * list is still kept). SHADER_PRELOAD_PARALLEL=0 loads on one thread.
 * -------------------------------------------------------------------------*/
#define PL_MAGIC   0x4C505643u            /* "CVPL" */
#define PL_VERSION 1u
#define PL_MAX_VS  2048
#define PL_MAX_FP  (VP_FP_CACHE / 2)      /* the other half stays free for new pipelines */
enum { PL_REC_VS = 1, PL_REC_FP = 2 };
typedef struct { VPFPEntry key; const u8* ucode; u32 len; } PlFp;
typedef struct PlDone { struct PlDone* next; VPFPEntry key; ID3D12PipelineState* pso; int skip; } PlDone;

static INIT_ONCE s_pl_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK s_pl_lock = SRWLOCK_INIT;   /* the known set, the output buffer, the done list */
static int s_pl_on = 0;                    /* the list is kept (the shader cache is on) */
static int s_pl_mode = 0;                  /* 0 no preload, 1 in the background, 2 the game waits */
static char s_pl_path[280];
static u8* s_pl_file = NULL;               /* the list as read; the records point into it */
static size_t s_pl_good = 0;               /* its bytes up to the first bad record */
static int s_pl_rewrite = 0;               /* missing, another format or a cut-off tail: write anew */
static PlVs* s_pl_vs = NULL; static int s_pl_nvs = 0;
static PlFp* s_pl_fp = NULL; static int s_pl_nfp = 0;
static int* s_pl_vs_index = NULL; static u32 s_pl_vs_index_cap = 0;   /* by hash: s_pl_vs index + 1 */
static u64* s_pl_known = NULL; static u32 s_pl_known_cap = 0, s_pl_known_n = 0;
static u8* s_pl_out = NULL; static size_t s_pl_out_n = 0, s_pl_out_cap = 0;
static ULONGLONG s_pl_flushed_at = 0;
static int s_pl_started = 0, s_pl_reported = 0;
static volatile ULONGLONG s_pl_finished_at = 0;   /* GetTickCount64 when it ended */
static volatile LONG s_pl_next_vs = 0, s_pl_next_fp = 0, s_pl_done_vs = 0, s_pl_done_fp = 0;
static volatile LONG s_pl_threads_left = 0, s_pl_built = 0;
static PlDone* s_pl_done = NULL;
static double s_pl_t0 = 0.0;

static u32 pl_rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
static u64 pl_mix(u64 h, u32 v) { h ^= v; h *= 1099511628211ull; return h; }
static u64 pl_vs_id(u32 hash) { const u64 h = pl_mix(14695981039346656037ull ^ 'V', hash); return h ? h : 1; }
static u64 pl_fp_id(const VPFPEntry* k)
{
    const u32 f[9] = { k->vs_hash, k->blend, (u32)k->nrt, k->rtfmt, (u32)k->exp32,
                       k->ucode_hash, k->cmask, k->cull, k->cube_mask };
    u64 h = 14695981039346656037ull ^ 'F';
    for (int i = 0; i < 9; i++) h = pl_mix(h, f[i]);
    return h ? h : 1;
}
static u32 pl_slot(u64 id, u32 cap) { return (u32)((id * 0x9E3779B97F4A7C15ull) >> 40) & (cap - 1); }
/* 1 if id was not known yet. s_pl_lock held (or the list still loading). */
static int pl_known_add(u64 id)
{
    if ((s_pl_known_n + 1) * 2 > s_pl_known_cap) {
        const u32 cap = s_pl_known_cap ? s_pl_known_cap * 2 : 4096;
        u64* t = (u64*)calloc(cap, sizeof *t);
        if (!t) return 0;
        for (u32 i = 0; i < s_pl_known_cap; i++)
            if (s_pl_known[i]) {
                u32 j = pl_slot(s_pl_known[i], cap);
                while (t[j]) j = (j + 1) & (cap - 1);
                t[j] = s_pl_known[i];
            }
        free(s_pl_known); s_pl_known = t; s_pl_known_cap = cap;
    }
    u32 j = pl_slot(id, s_pl_known_cap);
    while (s_pl_known[j]) { if (s_pl_known[j] == id) return 0; j = (j + 1) & (s_pl_known_cap - 1); }
    s_pl_known[j] = id; s_pl_known_n++;
    return 1;
}
static u32 pl_sum(u32 h, const u8* p, size_t n) { for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
/* One record: type, payload size, payload checksum, then the payload (head
 * words, then ucode). s_pl_lock held. */
static void pl_put_record(u32 type, const u32* head, int nhead, const u8* ucode, u32 len)
{
    const u32 plen = (u32)nhead * 4u + len;
    const size_t need = s_pl_out_n + 12u + plen;
    if (need > s_pl_out_cap) {
        size_t cap = s_pl_out_cap ? s_pl_out_cap : 65536;
        while (cap < need) cap *= 2;
        u8* t = (u8*)realloc(s_pl_out, cap);
        if (!t) return;
        s_pl_out = t; s_pl_out_cap = cap;
    }
    const u32 sum = pl_sum(pl_sum(2166136261u, (const u8*)head, (size_t)nhead * 4u), ucode, len);
    u8* o = s_pl_out + s_pl_out_n;
    memcpy(o, &type, 4); memcpy(o + 4, &plen, 4); memcpy(o + 8, &sum, 4);
    memcpy(o + 12, head, (size_t)nhead * 4u); memcpy(o + 12 + nhead * 4, ucode, len);
    s_pl_out_n = need;
}

static BOOL CALLBACK pl_load(PINIT_ONCE once, PVOID arg, PVOID* ctx)
{
    (void)once; (void)arg; (void)ctx;
    { const char* e = getenv("SHADER_CACHE"); if (e && e[0] == '0') return TRUE; }
    CreateDirectoryA("shader_cache", NULL);
    CreateDirectoryA(shader_cache_dir(), NULL);
    snprintf(s_pl_path, sizeof s_pl_path, "%s/pipelines.list", shader_cache_dir());
    s_pl_on = 1;
    { const char* m = getenv("SHADER_PRELOAD");
      s_pl_mode = (m && m[0] == '0') ? 0 : (m && !_stricmp(m, "wait")) ? 2 : 1; }
    /* Vertex-program edits for diagnosis that the list does not carry. */
    if (getenv("VP_W_ONE") || getenv("VP_BYPASS")) s_pl_mode = 0;
    FILE* f = fopen(s_pl_path, "rb");
    size_t sz = 0;
    if (f) {
        fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0 && (s_pl_file = (u8*)malloc((size_t)n)) != NULL &&
            fread(s_pl_file, 1, (size_t)n, f) == (size_t)n) sz = (size_t)n;
        fclose(f);
    }
    const u8* p = s_pl_file;
    if (sz < 8 || pl_rd32(p) != PL_MAGIC || pl_rd32(p + 4) != PL_VERSION) {
        s_pl_rewrite = 1;   /* none yet, or another format: start a new list */
        return TRUE;
    }
    int cap_vs = 0, cap_fp = 0;
    size_t at = 8;
    while (at + 12 <= sz) {
        const u32 type = pl_rd32(p + at), plen = pl_rd32(p + at + 4);
        if (plen > sz - at - 12 || pl_sum(2166136261u, p + at + 12, plen) != pl_rd32(p + at + 8)) break;
        const u8* q = p + at + 12;
        if (type == PL_REC_VS && plen >= 16 && 16u + pl_rd32(q + 12) == plen) {
            if (pl_known_add(pl_vs_id(pl_rd32(q))) && s_pl_nvs < PL_MAX_VS) {
                if (s_pl_nvs == cap_vs) {
                    cap_vs = cap_vs ? cap_vs * 2 : 256;
                    PlVs* t = (PlVs*)realloc(s_pl_vs, (size_t)cap_vs * sizeof *t);
                    if (!t) break;
                    s_pl_vs = t;
                }
                PlVs* v = &s_pl_vs[s_pl_nvs++];
                memset(v, 0, sizeof *v);
                v->hash = pl_rd32(q); v->vstart = pl_rd32(q + 4); v->vtex_mask = pl_rd32(q + 8);
                v->len = pl_rd32(q + 12); v->ucode = q + 16;
            }
        } else if (type == PL_REC_FP && plen >= 44 && 44u + pl_rd32(q + 40) == plen) {
            PlFp r;
            memset(&r, 0, sizeof r);
            r.key.vs_hash = pl_rd32(q); r.key.blend = pl_rd32(q + 4); r.key.nrt = (int)pl_rd32(q + 8);
            r.key.rtfmt = pl_rd32(q + 12); r.key.exp32 = (int)pl_rd32(q + 16); r.key.ucode_hash = pl_rd32(q + 20);
            r.key.cmask = pl_rd32(q + 24); r.key.cull = pl_rd32(q + 28); r.key.cube_mask = pl_rd32(q + 32);
            r.key.fp_addr = pl_rd32(q + 36); r.key.vs_idx = -1;
            r.len = pl_rd32(q + 40); r.ucode = q + 44;
            if (pl_known_add(pl_fp_id(&r.key)) && s_pl_nfp < PL_MAX_FP) {
                if (s_pl_nfp == cap_fp) {
                    cap_fp = cap_fp ? cap_fp * 2 : 1024;
                    PlFp* t = (PlFp*)realloc(s_pl_fp, (size_t)cap_fp * sizeof *t);
                    if (!t) break;
                    s_pl_fp = t;
                }
                s_pl_fp[s_pl_nfp++] = r;
            }
        }
        at += 12u + plen;
    }
    s_pl_good = at;
    if (at != sz) s_pl_rewrite = 1;   /* cut off mid-record (the game was closed while writing) */
    s_pl_vs_index_cap = 16;
    while (s_pl_vs_index_cap < (u32)s_pl_nvs * 2u) s_pl_vs_index_cap *= 2;
    s_pl_vs_index = (int*)calloc(s_pl_vs_index_cap, sizeof *s_pl_vs_index);
    if (!s_pl_vs_index) s_pl_vs_index_cap = 0;
    for (int i = 0; i < s_pl_nvs && s_pl_vs_index; i++) {
        u32 j = (s_pl_vs[i].hash * 0x9E3779B1u) & (s_pl_vs_index_cap - 1);
        while (s_pl_vs_index[j]) j = (j + 1) & (s_pl_vs_index_cap - 1);
        s_pl_vs_index[j] = i + 1;
    }
    fprintf(stderr, "[PRELOAD] %s: %d vertex programs, %d pipelines listed%c", s_pl_path, s_pl_nvs, s_pl_nfp, 10);
    return TRUE;
}
static int pl_ready(void)
{
    InitOnceExecuteOnce(&s_pl_once, pl_load, NULL, NULL);
    return s_pl_on;
}
static PlVs* pl_vs_find(u32 hash)
{
    if (!s_pl_vs_index_cap) return NULL;
    for (u32 j = (hash * 0x9E3779B1u) & (s_pl_vs_index_cap - 1);; j = (j + 1) & (s_pl_vs_index_cap - 1)) {
        const int v = s_pl_vs_index[j];
        if (!v) return NULL;
        if (s_pl_vs[v - 1].hash == hash) return &s_pl_vs[v - 1];
    }
}
static const PlVs* pl_vs_ready(u32 hash)
{
    if (!pl_ready()) return NULL;
    const PlVs* v = pl_vs_find(hash);
    return v && v->state == 1 && v->vs ? v : NULL;
}
static void pl_record_vs(u32 hash, u32 vstart, u32 vtex_mask, const u8* ucode, u32 len)
{
    if (!ucode || !len || !pl_ready()) return;
    const u32 head[4] = { hash, vstart, vtex_mask, len };
    AcquireSRWLockExclusive(&s_pl_lock);
    if (pl_known_add(pl_vs_id(hash))) pl_put_record(PL_REC_VS, head, 4, ucode, len);
    ReleaseSRWLockExclusive(&s_pl_lock);
}
/* The primary vertex program (vs_hash 0) is not listed: it is not rebuilt
 * from a listed program, and the game builds it at once anyway. */
static void pl_record_fp(const VPFPEntry* k, const u8* ucode, u32 len)
{
    if (!k->vs_hash || !ucode || !len || !pl_ready()) return;
    const u32 head[11] = { k->vs_hash, k->blend, (u32)k->nrt, k->rtfmt, (u32)k->exp32, k->ucode_hash,
                           k->cmask, k->cull, k->cube_mask, k->fp_addr, len };
    AcquireSRWLockExclusive(&s_pl_lock);
    if (pl_known_add(pl_fp_id(k))) pl_put_record(PL_REC_FP, head, 11, ucode, len);
    ReleaseSRWLockExclusive(&s_pl_lock);
}
/* New records reach the file within a second: the game is usually closed
 * with the window's X. */
static void pl_flush(void)
{
    const ULONGLONG now = GetTickCount64();
    if (now - s_pl_flushed_at < 1000) return;
    s_pl_flushed_at = now;
    AcquireSRWLockExclusive(&s_pl_lock);
    u8* buf = s_pl_out; const size_t n = s_pl_out_n;
    const int rewrite = s_pl_rewrite;
    if (!n && !rewrite) { ReleaseSRWLockExclusive(&s_pl_lock); return; }
    s_pl_out = NULL; s_pl_out_n = s_pl_out_cap = 0; s_pl_rewrite = 0;
    ReleaseSRWLockExclusive(&s_pl_lock);
    if (rewrite) {   /* the header, what was good of the old list, then the new records */
        char tmp[300];
        snprintf(tmp, sizeof tmp, "%s.tmp", s_pl_path);
        FILE* f = fopen(tmp, "wb");
        if (f) {
            int ok;
            if (s_pl_good >= 8) ok = fwrite(s_pl_file, 1, s_pl_good, f) == s_pl_good;
            else { const u32 hdr[2] = { PL_MAGIC, PL_VERSION }; ok = fwrite(hdr, 1, 8, f) == 8; }
            ok = ok && (!n || fwrite(buf, 1, n, f) == n);
            ok = (fclose(f) == 0) && ok;
            if (!ok || !MoveFileExA(tmp, s_pl_path, MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp);
        }
    } else {
        FILE* f = fopen(s_pl_path, "ab");
        if (f) { fwrite(buf, 1, n, f); fclose(f); }
    }
    free(buf);
}

static void pl_build_vs(PlVs* v)
{
    extern int rsx_vp_decompile_ex(const uint8_t*, u32, u32, char*, u32);
    extern void rsx_vp_set_branch_base(u32);
    enum { CAP = 262144 };
    char* hlsl = (char*)malloc(CAP);
    int ok = 0;
    /* The same identity vp_get_vs gives the program, or it is not this one. */
    u32 hash = vp_hash_ucode(v->ucode, v->len) ^ (v->vtex_mask * 0x9E3779B1u) ^ ((v->vstart / 16u + 1u) * 0x85EBCA6Bu);
    if (!hash) hash = 1u;
    if (hlsl && hash == v->hash) {
        rsx_vp_set_branch_base(v->vstart / 16u);
        if (rsx_vp_decompile_ex(v->ucode, v->len, v->vtex_mask, hlsl, CAP) > 0 &&
            (!vf_enabled() || vf_wrap_vs(hlsl, CAP))) {
            ID3DBlob* b = NULL; ID3DBlob* e = NULL;
            const HRESULT hr = shader_compile_cached(hlsl, strlen(hlsl), "guest_vp2", "vs_5_0", &b, &e);
            if (e) e->lpVtbl->Release(e);
            if (SUCCEEDED(hr) && b) {
                v->uses_c03 = (strstr(hlsl, "vp_c[0]") || strstr(hlsl, "vp_c[1]") ||
                               strstr(hlsl, "vp_c[2]") || strstr(hlsl, "vp_c[3]")) ? 1 : 0;
                char* kept = (char*)realloc(hlsl, strlen(hlsl) + 1);
                v->hlsl = kept ? kept : hlsl; hlsl = NULL;
                v->vs = b; ok = 1;
            }
        }
    }
    free(hlsl);
    InterlockedExchange(&v->state, ok ? 1 : 2);
}

static DWORD WINAPI pl_worker(LPVOID arg)
{
    (void)arg;
    for (;;) {   /* the vertex programs first: the pipelines are built with them */
        const LONG i = InterlockedIncrement(&s_pl_next_vs) - 1;
        if (i >= s_pl_nvs) break;
        pl_build_vs(&s_pl_vs[i]);
        InterlockedIncrement(&s_pl_done_vs);
    }
    while (s_pl_done_vs < s_pl_nvs) Sleep(1);
    FpJob j;
    memset(&j, 0, sizeof j);   /* one job, its HLSL buffer reused */
    for (;;) {
        const LONG i = InterlockedIncrement(&s_pl_next_fp) - 1;
        if (i >= s_pl_nfp) break;
        const PlFp* r = &s_pl_fp[i];
        const PlVs* v = pl_vs_find(r->key.vs_hash);
        if (v && v->state == 1 && v->vs) {
            j.key = r->key; j.ucode = (u8*)r->ucode; j.ucode_bytes = r->len;
            j.vsb = v->vs; j.pso = NULL;
            const int st = fp_build_pso(&j);
            PlDone* d = (st == FPB_OK && j.pso) || st == FPB_SKIP ? (PlDone*)malloc(sizeof *d) : NULL;
            if (d) {
                d->key = r->key; d->pso = j.pso; d->skip = st == FPB_SKIP;
                AcquireSRWLockExclusive(&s_pl_lock);
                d->next = s_pl_done; s_pl_done = d;
                ReleaseSRWLockExclusive(&s_pl_lock);
                InterlockedIncrement(&s_pl_built);
            } else if (j.pso) j.pso->lpVtbl->Release(j.pso);
        }
        InterlockedIncrement(&s_pl_done_fp);
    }
    free(j.hlsl);
    InterlockedDecrement(&s_pl_threads_left);
    return 0;
}

/* Render thread: hand the preloaded pipelines to the cache. */
static void pl_drain(void)
{
    AcquireSRWLockExclusive(&s_pl_lock);
    PlDone* d = s_pl_done; s_pl_done = NULL;
    ReleaseSRWLockExclusive(&s_pl_lock);
    while (d) {
        PlDone* nx = d->next;
        VPFPEntry key = d->key;
        key.gen = s_d3d.vp_gen;
        const int idx = fp_index_find(&key);
        if (idx < 0) fp_cache_insert(&key, d->pso, 0, d->skip);
        else if (s_d3d.vp_fp[idx].pending) {   /* the game asked first: its own build is dropped */
            VPFPEntry* e = &s_d3d.vp_fp[idx];
            e->pso = d->pso; e->pending = 0; e->skip = (u8)d->skip;
        } else if (d->pso) d->pso->lpVtbl->Release(d->pso);
        free(d);
        d = nx;
    }
}

/* Frame start, render thread. The preload starts once the base vertex
 * pipeline exists: the cache keys its pipelines with that generation. */
static void pl_tick(void)
{
    if (!pl_ready()) return;
    pl_flush();
    if (!s_pl_started && s_pl_mode && (s_pl_nvs || s_pl_nfp) && s_d3d.vp_ready && s_d3d.vp_root_sig) {
        s_pl_started = 1;
        SYSTEM_INFO si; GetSystemInfo(&si);
        const int cores = (int)si.dwNumberOfProcessors;
        int nt = s_pl_mode == 2 ? cores - 1 : cores / 4;   /* waiting, the game needs no CPU */
        if (nt < 1) nt = 1; if (nt > 12) nt = 12;
        { const char* e = getenv("SHADER_PRELOAD_PARALLEL"); if (e && e[0] == '0') nt = 1; }
        s_pl_t0 = perf_now();
        for (int i = 0; i < nt; i++) {
            InterlockedIncrement(&s_pl_threads_left);
            HANDLE th = CreateThread(NULL, 0, pl_worker, NULL, 0, NULL);
            if (!th) { InterlockedDecrement(&s_pl_threads_left); continue; }
            SetThreadPriority(th, s_pl_mode == 2 ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_LOWEST);
            ps3_host_thread_name(th, "shader:preload %d", i);
            CloseHandle(th);
        }
        fprintf(stderr, "[PRELOAD] loading %d vertex programs and %d pipelines on %d thread(s)%s%c",
                s_pl_nvs, s_pl_nfp, nt, s_pl_mode == 2 ? ", the game waits" : "", 10);
    }
    if (s_pl_mode == 2 && s_pl_threads_left > 0) {   /* hold the frame, a little at a time: the note stays drawn */
        const double t0 = perf_now();
        while (s_pl_threads_left > 0 && perf_now() - t0 < 0.1) Sleep(2);
    }
    pl_drain();
    if (s_pl_started && !s_pl_reported && s_pl_threads_left <= 0) {
        s_pl_reported = 1;
        s_pl_finished_at = GetTickCount64();
        fprintf(stderr, "[PRELOAD] %ld pipelines ready in %.1f s (%d listed)%c",
                (long)s_pl_built, perf_now() - s_pl_t0, s_pl_nfp, 10);
    }
}

/* For the loading note (rsx_perf_overlay.c): 0 not started (or nothing to
 * load), 1 loading, 2 finished at *finished_ms (GetTickCount64). */
int rsx_shader_preload_state(int* done, int* total, unsigned long long* finished_ms)
{
    if (done) *done = (int)(s_pl_done_vs + s_pl_done_fp);
    if (total) *total = s_pl_nvs + s_pl_nfp;
    if (finished_ms) *finished_ms = s_pl_finished_at;
    if (!s_pl_started) return 0;
    return s_pl_threads_left > 0 || !s_pl_finished_at ? 1 : 2;
}

/* Upload the guest texture for a VP draw into a per-frame texture slot
 * (re-uploaded every frame: gcm/cube's plasma animates in guest memory).
 * Returns the slot index (SRV at heap 1+slot) or -1. Must run while the
 * command list is open, before the draw passes. */
/* NV4097 TEXTURE_CONTROL1 component remap -> D3D12 Shader4ComponentMapping.
 * The crossbar decode itself is RSX semantics and lives in
 * rsx_texture_layout.c; all that is left here is packing the four selectors
 * into D3D12's word. Its force-zero/force-one encodings are 4 and 5, which is
 * what RSX_REMAP_ZERO/ONE already are, so the selectors pass straight through.
 *
 * out[] arrives in the crossbar's field order A,R,G,B; D3D12 wants
 * destR | destG<<3 | destB<<6 | destA<<9 | valid<<12. */
static u32 rsx_remap_to_d3d(u32 c1, u32 basef)
{
    u8 out[4];
    rsx_texture_component_remap(c1, basef, out);
    /* TEX_REMAP_ID=<n>: override the derived crossbar with a fixed mapping, to
     * test channel order directly. The resource holds the guest's A8R8G8B8
     * bytes straight through, so its components are (R=A, G=R, B=G, A=B) and
     * the shader needs destR=1, destG=2, destB=3, destA=0 to see real RGBA.
     *   1 = identity   2 = rotate (the ARGB fix)   3 = BGRA swap */
    { static int fixed = -1;
      if (fixed < 0) { const char* e = getenv("TEX_REMAP_ID"); fixed = e ? atoi(e) : 0; }
      if (fixed == 1) return 0u | (1u<<3) | (2u<<6) | (3u<<9) | (1u<<12);
      if (fixed == 2) return 1u | (2u<<3) | (3u<<6) | (0u<<9) | (1u<<12);
      if (fixed == 3) return 2u | (1u<<3) | (0u<<6) | (3u<<9) | (1u<<12); }
    /* D3D12 mapping: destR | destG<<3 | destB<<6 | destA<<9 | valid bit */
    { static int _rd = -1; if (_rd < 0) _rd = getenv("REMAPDBG") ? 1 : 0;
      if (_rd) { static u32 seen[32]; static int ns=0;
        u32 k = (basef << 16) | (c1 & 0xFFFFu); int f=0;
        for (int i2=0;i2<ns;i2++) if (seen[i2]==k) f=1;
        if (!f && ns<32) { seen[ns++]=k;
            fprintf(stderr, "[REMAP] basef=0x%02X c1=0x%04X -> destR=%u destG=%u destB=%u destA=%u%c",
                    basef, c1 & 0xFFFFu, out[1], out[2], out[3], out[0], 10); } } }
    return out[1] | (out[2] << 3) | (out[3] << 6) | (out[0] << 9) | (1u << 12);
}

/* Morton/Z-order texel offset for RSX swizzled textures (LN bit clear).
 * Interleaves x (even bit positions) and y (odd) until the smaller dimension's
 * bits run out, then the larger dimension's remaining bits ride above -- the
 * NV40 layout (matches RPCS3 convert_linear_swizzle). POT dims only, which is
 * what the hardware requires for swizzled textures. */

/* Sparse FNV-1a over a texture's source bytes -- enough to notice an animated
 * surface changing, cheap enough to run on every bind. Kept a function rather
 * than a statement-expression macro so the file still compiles under MSVC;
 * ps3recomp builds with clang-cl, but titles like Tokyo Jungle drive the build
 * through the Visual Studio generator. */
static u32 tex_csum(const u8* base, u32 nbytes)
{
    u32 h = 2166136261u;
    u32 step = nbytes > 4096u ? nbytes / 1024u : 4u;
    for (u32 i = 0; i + 3 < nbytes; i += step) {
        u32 w32; memcpy(&w32, base + i, sizeof w32);
        h ^= w32; h *= 16777619u;
    }
    return h;
}

/* ---------------------------------------------------------------------------
 * Texture mods (rsx_texture_pack.h). A replacement is uploaded once, as RGBA8
 * with its mip chain, and kept by hash while it is in use; past
 * TEXTURE_VRAM_MB (default 3072) the ones unused the longest are dropped and
 * read again if they come back, so a pack far larger than video memory works.
 * While a replacement is still decoding, its texture draws with its own image.
 * Moddable: B8, A8R8G8B8, G8B8 and DXT1/3/5 -- the formats drawn as such.
 * -------------------------------------------------------------------------*/
typedef struct { u64 hash; int state; ID3D12Resource* res; u64 bytes; u64 last; } RepEntry;
enum { REP_NEW = 0, REP_NONE, REP_LOADING, REP_READY, REP_DROPPED };
#define REP_CAP 65536u
static RepEntry* s_rep;
static u32 s_rep_n;
static u64 s_rep_bytes, s_rep_budget;

static int texrep_eligible(u32 basef)
{
    return basef == 0x81 || basef == 0x85 || basef == 0x8B || (basef >= 0x86 && basef <= 0x88);
}
static RepEntry* rep_find(u64 h, int add)
{
    if (!s_rep) {
        if (!add || (s_rep = (RepEntry*)calloc(REP_CAP, sizeof *s_rep)) == NULL) return NULL;
    }
    for (u32 i = (u32)(h >> 20) & (REP_CAP - 1), n = 0; n < REP_CAP; n++, i = (i + 1) & (REP_CAP - 1)) {
        if (s_rep[i].hash == h) return &s_rep[i];
        if (!s_rep[i].hash) {
            if (!add || s_rep_n * 10u > REP_CAP * 7u) return NULL;
            s_rep[i].hash = h; s_rep[i].state = REP_NEW; s_rep_n++;
            return &s_rep[i];
        }
    }
    return NULL;
}
static ID3D12Resource* rep_upload(const texpack_image* im)
{
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = im->w; td.Height = im->h; td.DepthOrArraySize = 1;
    td.MipLevels = (UINT16)im->levels; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Resource* res = NULL; ID3D12Resource* up = NULL;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&res)))
        return NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[16]; UINT rows[16]; UINT64 rowsz[16], total = 0;
    s_d3d.device->lpVtbl->GetCopyableFootprints(s_d3d.device, &td, 0, im->levels, 0, fp, rows, rowsz, &total);
    D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    u8* m = NULL; D3D12_RANGE nr = {0, 0};
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&up)) ||
        FAILED(up->lpVtbl->Map(up, 0, &nr, (void**)&m)) || !m) {
        if (up) up->lpVtbl->Release(up);
        res->lpVtbl->Release(res);
        return NULL;
    }
    for (u32 l = 0; l < im->levels; l++) {
        const u32 lw = fp[l].Footprint.Width;
        for (u32 y = 0; y < rows[l]; y++)
            memcpy(m + fp[l].Offset + (u64)y * fp[l].Footprint.RowPitch,
                   im->data + im->level_off[l] + (size_t)y * lw * 4, (size_t)lw * 4);
    }
    up->lpVtbl->Unmap(up, 0, NULL);
    for (u32 l = 0; l < im->levels; l++) {
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        dst.pResource = res; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = l;
        src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp[l];
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    }
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    defer_release(up);   /* once the GPU has copied it */
    return res;
}
static void rep_drop(RepEntry* e)
{
    defer_release(e->res); e->res = NULL;
    s_rep_bytes -= e->bytes; e->bytes = 0;
    e->state = REP_DROPPED;
}
/* Render thread, with the command list open: upload what finished decoding
 * (up to 48 MB a frame, so a burst does not stall one), and keep within the
 * budget. Switched off, the replacements stay loaded (only the budget lets
 * them go), so switching back with the hotkey is immediate. */
static void texrep_frame(void)
{
    if (!s_rep_budget) {
        const char* e = getenv("TEXTURE_VRAM_MB");
        const u64 mb = e && atoi(e) > 0 ? (u64)atoi(e) : 3072u;
        s_rep_budget = mb << 20;
    }
    uint64_t h; texpack_image* im = NULL; int r;
    u64 used = 0;
    while (used < (48ull << 20) && (r = texpack_next(&h, &im)) != 0) {
        RepEntry* e = rep_find(h, 0);
        if (r > 0 && e && e->state == REP_LOADING) {
            ID3D12Resource* res = rep_upload(im);
            if (res) { e->res = res; e->state = REP_READY; e->bytes = im->bytes; e->last = s_tex_frame;
                       s_rep_bytes += im->bytes; used += im->bytes; }
            else e->state = REP_NONE;
        } else if (e && e->state == REP_LOADING) e->state = r < 0 ? REP_NONE : REP_DROPPED;
        texpack_free(im); im = NULL;
    }
    if (!s_rep || s_rep_bytes <= s_rep_budget) return;
    /* Over budget: let go of the least recently used, and whatever went unused
     * within half a second of it -- never one this frame or the last used. */
    u64 oldest = ~0ull;
    for (u32 i = 0; i < REP_CAP; i++)
        if (s_rep[i].state == REP_READY && s_rep[i].last + 1 < s_tex_frame && s_rep[i].last < oldest) oldest = s_rep[i].last;
    if (oldest == ~0ull) return;
    for (u32 i = 0; i < REP_CAP; i++)
        if (s_rep[i].state == REP_READY && s_rep[i].last + 1 < s_tex_frame && s_rep[i].last <= oldest + 30)
            rep_drop(&s_rep[i]);
}
/* TEXTURE_DUMP: the decoded image (map0, rows pitch bytes apart, in the
 * upload layout) as RGBA8 with the channels the shader sees, so a PNG edited
 * and put back goes through the same remap. */
static void tex_dump_image(u64 hash, const u8* map0, u32 pitch, u32 w, u32 h, u32 basef)
{
    u8* rgba = (u8*)malloc((size_t)w * h * 4);
    if (!rgba) return;
    const char* name = basef == 0x85 ? "ARGB" : basef == 0x8B ? "G8B8" : basef == 0x81 ? "B8"
                     : basef == 0x86 ? "DXT1" : basef == 0x87 ? "DXT3" : "DXT5";
    if (basef >= 0x86 && basef <= 0x88) texpack_decode_bc((int)(basef - 0x85), map0, pitch, w, h, rgba);
    else
        for (u32 y = 0; y < h; y++) {
            const u8* s = map0 + (u64)y * pitch;
            u8* d = rgba + (size_t)y * w * 4;
            for (u32 x = 0; x < w; x++, d += 4) {
                if (basef == 0x85) memcpy(d, s + x * 4, 4);
                else if (basef == 0x8B) { d[0] = s[x * 2]; d[1] = s[x * 2 + 1]; d[2] = 0; d[3] = 255; }
                else { d[0] = d[1] = d[2] = s[x]; d[3] = 255; }
            }
        }
    texpack_dump(hash, w, h, name, rgba);
}
/* A texture uploaded while dumping was off: written the first time it is
 * bound after dumping is switched on, from its upload buffer. */
static void texdump_for_slot(int ts)
{
    VPTexSlot* t = &s_d3d.vp_tex[ts];
    if (!t->thash_can || !t->up || !texpack_dump_on()) return;
    const uint32_t generation = texpack_dump_generation();
    if (t->dump_seen && t->dump_generation == generation) return;
    t->dump_seen = 1; t->dump_generation = generation;
    if (!t->thash_ok) {
        extern uint8_t* vm_base;
        if (!vm_base) return;
        t->thash = texpack_hash(vm_base + t->off, t->thash_bytes, t->w, t->h, t->fmt & 0x9F);
        t->thash_ok = 1;
    }
    if (!texpack_dump_wanted(t->thash)) return;
    rsx_tex_layout tl;
    rsx_texture_layout(t->fmt, t->w, t->h, &tl);
    const u32 pitch = (tl.dst_row_bytes + 255) & ~255u;
    D3D12_RANGE rr = { 0, (SIZE_T)pitch * (tl.compressed ? tl.rows : t->h) }, nr = { 0, 0 };
    void* m = NULL;
    if (SUCCEEDED(t->up->lpVtbl->Map(t->up, 0, &rr, &m)) && m) {
        tex_dump_image(t->thash, (const u8*)m, pitch, t->w, t->h, t->fmt & 0x9F);
        t->up->lpVtbl->Unmap(t->up, 0, &nr);
    }
}
/* The replacement to draw a texture slot with, if one is loaded; asks for it
 * the first time the texture is seen. */
static ID3D12Resource* texrep_for_slot(int ts)
{
    VPTexSlot* t = &s_d3d.vp_tex[ts];
    if (!texpack_replace_on()) return NULL;
    if (!t->thash_ok) {   /* uploaded while replacement was off (the hotkey turned it on since) */
        extern uint8_t* vm_base;
        if (!t->thash_can || !vm_base) return NULL;
        t->thash = texpack_hash(vm_base + t->off, t->thash_bytes, t->w, t->h, t->fmt & 0x9F);
        t->thash_ok = 1;
    }
    RepEntry* e = rep_find(t->thash, 0);
    if (!e) {
        const int has = texpack_lookup(t->thash);
        if (has < 0 || !(e = rep_find(t->thash, 1))) return NULL;   /* the folder is still being listed */
        e->state = has ? REP_LOADING : REP_NONE;
        if (has) texpack_request(t->thash);
        return NULL;
    }
    if (e->state == REP_DROPPED) { e->state = REP_LOADING; texpack_request(e->hash); return NULL; }
    if (e->state != REP_READY) return NULL;
    e->last = s_tex_frame;
    return e->res;
}

/* PERF: a texture upload over 2 ms names itself -- which texture, and whether
 * it was new to the cache or a cached copy whose guest bytes changed. */
static int s_tex_why;   /* 0 = not cached (new or evicted), 1 = changed bytes */
#define TEX_HINT_N 4096
static short s_tex_hint[TEX_HINT_N];   /* slot + 1; 0 = none */
static int vp_upload_tex_slot_impl(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips);
static int vp_upload_tex_slot(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips)
{
    if (cv_cutscene_diag() && !perf_on()) {
        const double start = perf_now();
        int result = vp_upload_tex_slot_impl(off, w, h, fmt, cube, mips);
        cv_cutscene_slow("texture-lookup-upload", start);
        return result;
    }
    if (!perf_on()) return vp_upload_tex_slot_impl(off, w, h, fmt, cube, mips);
    double t0 = perf_now();
    s_tex_why = 0;
    int r = vp_upload_tex_slot_impl(off, w, h, fmt, cube, mips);
    double el = perf_now() - t0;
    if (el > 0.002) {
        static int n = 0;
        if (n++ < 300)
            fprintf(stderr, "[TEX-SLOW] %.1fms off=0x%08X %ux%u fmt=0x%02X cube=%d mips=%u %s%c",
                    el * 1000.0, off, w, h, fmt, cube, mips,
                    s_tex_why ? "CHANGED (guest bytes differ)" : "NOT CACHED (new or evicted)", 10);
    }
    return r;
}
static int vp_upload_tex_slot_impl(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips)
{
    /* TEX_BUDGET=<n>: cap texture uploads per frame. Off by default -- it was
     * tried against the TDR and does not help, because PERF shows the upload
     * path costing 0.01s for 775 calls. Kept because it is the right lever if a
     * title ever does hit an upload-bound frame. */
    { static int budget = -1;
      if (budget < 0) { const char* e = getenv("TEX_BUDGET"); budget = e ? atoi(e) : 0; }
      if (budget > 0) {
          static u64 last_frame = ~0ull; static int used = 0;
          if (last_frame != (u64)s_d3d.frame_count) { last_frame = (u64)s_d3d.frame_count; used = 0; }
          if (++used > budget) return -1;   /* try again next frame */
      } }
    extern uint8_t* vm_base;

    if (!off || !w || !h || !vm_base || !s_d3d.srv_heap) return -1;
    /* Format classes (base = fmt & 0x9F). The LBP loading screen uses:
     * 0x85 A8R8G8B8 (swizzled UI art), 0x8B G8B8 (the 1024x2048 linear FONT
     * atlas -- without it no text renders at all), 0x86/87/88 DXT1/23/45
     * (512x512 detail/LUT layers bound at t1/t3 on every draw). */
    /* Format class, row sizes and swizzling are RSX semantics: rsx_texture_
     * layout.c owns them so the Metal backend can read a guest texture without
     * re-deriving any of it. Only the DXGI mapping below is ours. */
    rsx_tex_layout tl;
    rsx_texture_layout(fmt, w, h, &tl);
    u32 basef = fmt & 0x9F;              /* still needed for the SRV remap */
    int argb = (tl.fmt == RSX_TEXFMT_R8G8B8A8);
    int dxt  = tl.compressed;
    /* Compressed rows are counted in blocks, so keep bpp at 1 for the byte
     * arithmetic the diagnostics below do. */
    u32 bpp = tl.compressed ? 1u : tl.bytes_per_texel;
    /* The DECODED image's format, which is not always the guest's: the packed
     * 16-bit colour formats arrive here unpacked to RGBA8, and DEPTH24_D8 as a
     * float. rsx_texture_layout.h says which class each format byte lands in. */
    DXGI_FORMAT dxfmt = (tl.fmt == RSX_TEXFMT_R8G8B8A8) ? DXGI_FORMAT_R8G8B8A8_UNORM
                      : (tl.fmt == RSX_TEXFMT_R8G8)     ? DXGI_FORMAT_R8G8_UNORM
                      : (tl.fmt == RSX_TEXFMT_BC1)      ? DXGI_FORMAT_BC1_UNORM
                      : (tl.fmt == RSX_TEXFMT_BC2)      ? DXGI_FORMAT_BC2_UNORM
                      : (tl.fmt == RSX_TEXFMT_BC3)      ? DXGI_FORMAT_BC3_UNORM
                      : (tl.fmt == RSX_TEXFMT_R16)      ? DXGI_FORMAT_R16_UNORM
                      : (tl.fmt == RSX_TEXFMT_R16G16)   ? DXGI_FORMAT_R16G16_UNORM
                      : (tl.fmt == RSX_TEXFMT_R16G16F)  ? DXGI_FORMAT_R16G16_FLOAT
                      : (tl.fmt == RSX_TEXFMT_R16G16B16A16F) ? DXGI_FORMAT_R16G16B16A16_FLOAT
                      : (tl.fmt == RSX_TEXFMT_R32F)     ? DXGI_FORMAT_R32_FLOAT
                      : (tl.fmt == RSX_TEXFMT_R32G32B32A32F) ? DXGI_FORMAT_R32G32B32A32_FLOAT
                      : DXGI_FORMAT_R8_UNORM;
    /* DXT data is stored as linear 4x4 block rows (compressed formats are
     * never Morton-swizzled on RSX). Row of blocks = (w/4)*blocksize. */
    u32 blkrow  = dxt ? tl.row_bytes : 0;
    u32 blkrows = dxt ? tl.rows      : 0;
    const u32 key_off = off;         /* lookup key: the offset as bound */
    /* Sparse checksum of a texture's source bytes -- enough to notice an
     * animated surface changing, cheap enough to run on every bind. */
    /* (portable helper tex_csum() -- see above; a statement-expression macro
     * here was a GCC/Clang extension MSVC rejects.) */
    #define TEX_CSUM(base, nbytes) tex_csum((base), (nbytes))
    int slot = -1, freeslot = -1, emptyslot = -1;
    /* Where this texture sat last time: checked first, so a lookup is one
     * compare instead of a walk over every slot (13k lookups a frame). */
    const u32 hk = ((key_off >> 7) ^ (key_off >> 17) ^ (w << 3) ^ h ^ fmt) & (TEX_HINT_N - 1);
    const int hint = s_tex_hint[hk] - 1;
    for (int k = -1; k < VP_TEX_SLOTS; k++) {
        const int i = (k < 0) ? hint : k;
        if (i < 0 || (k >= 0 && i == hint)) continue;
        VPTexSlot* c = &s_d3d.vp_tex[i];
        if (c->res && c->key == key_off && c->w == w && c->h == h && c->fmt == fmt
            && c->cube == cube) {
            s_tex_hint[hk] = (short)(i + 1);
            if (c->used) return i;                /* already bound this frame */
            if (c->ui_res && c->ui_scale != s_rs) { slot = i; break; }
            c->stamp = s_tex_frame;
            { static int nocache = -1;            /* TEX_NOCACHE=1: always re-upload */
              if (nocache < 0) { const char* e = getenv("TEX_NOCACHE");
                                 nocache = e ? atoi(e) : 0; }
              if (nocache) { slot = i; break; } }
            /* Held from an earlier frame: re-upload only if the guest bytes
             * changed. Most of this scene's textures are static, and converting
             * every one of them every frame was ~70% of the frame's CPU time. */
            u32 nb = dxt ? (blkrow * blkrows) : (w * h * bpp);
            u32 cs = TEX_CSUM(vm_base + c->off, nb);
            if (cs == c->csum && !rsx_tex_take_dirty(c->off)) { c->used = 1; return i; }
            s_tex_why = 1;
            slot = i; break;                      /* stale: fall through and redo */
        }
        /* Evict an empty slot first, then the one unused the longest -- not
         * merely the first not yet used THIS frame, which threw out textures
         * the frame was about to bind again while empty slots sat unused. */
        if (!c->used) {
            if (!c->res) { if (emptyslot < 0) emptyslot = i; }
            else if (freeslot < 0 || c->stamp < s_d3d.vp_tex[freeslot].stamp) freeslot = i;
        }
    }
    if (emptyslot >= 0) freeslot = emptyslot;
    const int restale = slot >= 0;   /* the same texture, its bytes changed */
    /* TEX_OFF_BIAS=1: sample the image one whole level-0 BELOW the bound offset.
     * Measured on the Rubber Ducky demo: at the bind offset every texture reads
     * as all-zero, while [off - w*h*bpp, off) holds the image (3800-4160 of 4298
     * sampled bytes non-zero, for four different textures at three sizes). So
     * the offset the guest programs points PAST level 0 rather than at it. */
    /* TEX_OFF_BIAS: 1 = one level-0 below the bound offset, 2 = one full MIP
     * CHAIN above it. The measured upload-to-bind deltas on the Rubber Ducky
     * demo are 0x2AAB00 for a 2MB level 0 and 0x555580 for a 4MB one -- both
     * exactly the mipmap pyramid total (L0 * 4/3), which is what the guest
     * reserves per texture. */
    { static int bias = -1;
      if (bias < 0) { const char* e = getenv("TEX_OFF_BIAS"); bias = e ? atoi(e) : 0; }
      if (bias == 1) { u32 sz = w * h * bpp; if (off > sz) off -= sz; }
      else if (bias >= 2) {
          u32 chain = 0;
          for (u32 mw = w, mh = h; mw && mh; mw >>= 1, mh >>= 1) {
              chain += mw * mh * bpp;
              if (mw == 1 && mh == 1) break;
          }
          /* bias 3: only shift when the bound offset really is empty, so a
           * texture that IS bound correctly keeps its own data. Applying the
           * chain unconditionally fixes the mis-bound majority but breaks any
           * correctly-bound minority, which shows up as objects going black. */
          int shift = 1;
          if (bias >= 3) {
              u32 nz = 0, sz = w * h * bpp;
              for (u32 i = 0; i < sz && i < 0x20000u; i += 97) if (vm_base[off + i]) nz++;
              shift = (nz == 0);
          }
          if (shift) off += chain;
      } }
    /* TEX_REMAP=1: if the bound offset holds nothing, look the image up by size
     * in the VRAM upload registry (cellGcmSys). For a title whose upload address
     * and SET_TEXTURE_OFFSET disagree this puts the real bytes under the sampler
     * without guessing a delta. */
    { static int remap = -1;
      if (remap < 0) { const char* e = getenv("TEX_REMAP"); remap = e ? atoi(e) : 0; }
      if (remap) {
        u32 nz = 0, sz = w * h * bpp;
        for (u32 i = 0; i < sz && i < 0x20000u; i += 97) if (vm_base[off + i]) nz++;
        if (!nz) {
            extern u32 rsx_find_vram_upload(u32);
            u32 alt = rsx_find_vram_upload(sz);
            if (alt) {
                static int _n = 0;
                if (_n++ < 8) fprintf(stderr, "[TEXREMAP] 0x%08X (%ux%u, %u bytes) -> 0x%08X%c",
                                      off, w, h, sz, alt, 10);
                off = alt;
            }
        } } }
    /* A stale cache entry (slot >= 0) re-uses its own slot; otherwise take a
     * free one. Without this the stale path fell through to freeslot and either
     * duplicated the texture into a second slot or bailed when none was free. */
    if (slot < 0 && freeslot < 0) {               /* out of slots this frame */
        static u64 last = ~0ull; static int n = 0;
        if (last != s_tex_frame && n < 40) { last = s_tex_frame; n++;
            printf("[TEX] out of texture slots (%d) in frame %llu: the draw goes untextured%c",
                   VP_TEX_SLOTS, (unsigned long long)s_tex_frame, 10); fflush(stdout); }
        return -1;
    }
    /* TEX_WATCH=1: the textures of Code Veronica's water (512x512 A8R8G8B8
     * normal map, 256x256 DXT1 reflection, 128x128 DXT5 detail): is their
     * source populated when they are uploaded, and with what? */
    { static int on = -1, n = 0;
      if (on < 0) { const char* e = getenv("TEX_WATCH"); on = e ? atoi(e) : 0; }
      const u32 bf = fmt & 0x9F;
      if (on && n < 80 && ((w == 512 && h == 512 && bf == 0x85) || (w == 256 && h == 256 && bf == 0x86) ||
                           (w == 128 && h == 128 && bf == 0x88))) {
        n++;
        u32 nz = 0, tot = 0, sz = w * h * bpp;
        for (u32 i = 0; i < sz; i += 61) { tot++; if (vm_base[off + i]) nz++; }
        const u8* p = vm_base + off;
        fprintf(stderr, "[TEXWATCH] off=0x%08X %ux%u fmt=0x%02X stale=%d nonzero %u/%u first %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X%c",
                off, w, h, fmt, restale, nz, tot, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15], 10);
      } }
    /* TEX_DUMP=<hex offset>: write the guest bytes of that texture to
     * texdump_<off>_<n>.bin each time it is (re)uploaded, the first 8 times. */
    { static int on = -1, n = 0; static u32 want = 0;
      if (on < 0) { const char* e = getenv("TEX_DUMP"); on = e ? 1 : 0;
                    if (e) want = (u32)strtoul(e, NULL, 16) & 0x0FFFFFFFu; }
      if (on && n < 8 && (off & 0x0FFFFFFFu) == want) {
        u32 nb = dxt ? (blkrow * blkrows) : (w * h * bpp);
        if (cube) {   /* all six faces, each a whole mip pyramid (see the face stride below) */
            u32 total = 0;
            for (u32 lw = w, lh = h, l = 0; l < (mips ? mips : 1u); l++) {
                total += dxt ? (((lw + 3) / 4) * (tl.fmt == RSX_TEXFMT_BC1 ? 8u : 16u) * ((lh + 3) / 4))
                             : (lw * lh * bpp);
                if (lw > 1) lw >>= 1;
                if (lh > 1) lh >>= 1;
            }
            nb = 6u * ((total + 127u) & ~127u);
        }
        char fn[96]; snprintf(fn, sizeof fn, "texdump_%08X_%d.bin", off, n);
        FILE* f = fopen(fn, "wb");
        if (f) { for (u32 o = 0; o < nb; o += 4096u)
                     if (fwrite(vm_base + off + o, 1, (nb - o < 4096u) ? nb - o : 4096u, f) == 0) break;
                 fclose(f); }
        fprintf(stderr, "[TEXDUMP] %s %ux%u fmt=0x%02X bytes=%u cube=%d mips=%u stale=%d frame=%llu%c", fn, w, h,
                fmt, nb, cube, mips, restale, (unsigned long long)s_tex_frame, 10);
        n++;
      } }
    /* TEX_SRCDBG=<N>: is the guest memory this slot uploads FROM actually
     * populated? An empty source and a broken sampler both render flat. */
    { static int cap = -1, n = 0;
      if (cap < 0) { const char* e = getenv("TEX_SRCDBG"); cap = e ? atoi(e) : 0; }
      if (cap && n < cap) { n++;
        u32 nz = 0, tot = 0;
        for (u32 i = 0; i < w * h * bpp && i < 0x40000u; i += 61) { tot++; if (vm_base[off + i]) nz++; }
        /* Several of this title's textures are bound at EXACTLY the end address
         * of an upload, so also sample the block immediately BELOW the bind
         * offset: if that holds the image, the bind is one whole texture high. */
        u32 nzb = 0, totb = 0;
        { u32 sz = w * h * bpp; u32 base = (off > sz) ? off - sz : 0;
          for (u32 i = 0; i < sz && i < 0x40000u; i += 61) { totb++; if (vm_base[base + i]) nzb++; } }
        long nearest = 0; u32 found = 0;
        if (!nz) {   /* empty: where IS the data? scan out from the bind offset */
            for (long d = 0x1000; d <= 0x1000000 && !found; d += 0x1000) {
                for (int s = 0; s < 2 && !found; s++) {
                    long a = (long)off + (s ? -d : d);
                    if (a < 0x1000) continue;
                    u32 c = 0;
                    for (u32 i = 0; i < 0x1000u; i += 61) if (vm_base[a + i]) c++;
                    if (c > 8) { nearest = (s ? -d : d); found = c; }
                }
            }
        }
        fprintf(stderr, "[TEXSRC] off=0x%08X %ux%u fmt=0x%02X bpp=%u nonzero=%u/%u"
                        "%s%+ld (page nonzero=%u)\n",
                off, w, h, fmt, bpp, nz, tot,
                nz ? "" : "  nearest data at ", nz ? 0L : nearest, found);
        fprintf(stderr, "[TEXSRC]    block BELOW bind (off-%u): nonzero=%u/%u%c",
                w * h * bpp, nzb, totb, 10);
        /* Walk DOWN in 4KB steps to the first populated page whose predecessor
         * is empty: that is where this texture's data actually begins, and the
         * delta from the bound offset is the rule we are missing. */
        { u32 step = 0x1000, prev_pop = 0; long start_delta = 0;
          for (long d = 0; d >= -(long)(w * h * bpp) * 2; d -= step) {
              u32 a = (u32)((long)off + d); u32 pop = 0;
              if (a < 0x1000) break;
              for (u32 i = 0; i < step; i += 53) if (vm_base[a + i]) pop++;
              if (!pop && prev_pop) { start_delta = d + step; break; }
              prev_pop = pop;
          }
          fprintf(stderr, "[TEXSRC]    data starts at off%+ld (level0=%u bytes)%c",
                  start_delta, w * h * bpp, 10); } } }
    if (slot < 0) slot = freeslot;
    VPTexSlot* t = &s_d3d.vp_tex[slot];
    /* The upload buffer's rows hold the DECODED image, so they are sized from
     * dst_row_bytes -- w * bpp is the source row, which is narrower for any
     * format rsx_texture_decode unpacks. */
    u32 pitch = (tl.dst_row_bytes + 255) & ~255u;
    int fresh = 0;

    /* Also recreate when the CUBE-ness changes: a slot holding a 2D texture of
     * the same dimensions would otherwise be reused with 1 array slice while the
     * cube path copies 6 subresources and writes a 6x upload buffer -- which
     * crashes. */
    /* RSX_TEX_MIPS=1: upload the guest's mip chain too (2D swizzled or
     * compressed textures; their levels follow level 0 back to back). Without
     * it every texture is sampled at full size at any distance: Code
     * Veronica's water ripples (a 512x512 normal map seen far off) turn to
     * per-pixel noise and its reflection loses its shape. */
    enum { TEX_MAX_LEV = 14 };
    int nlev = 1;
    { static int on = -1;
      if (on < 0) { const char* e = getenv("RSX_TEX_MIPS"); on = (e && *e == '1') ? 1 : 0; }
      if (on && !cube && mips > 1 && (dxt || tl.swizzled)) {
          int full = 1; for (u32 m = (w > h ? w : h); m > 1; m >>= 1) full++;
          nlev = (int)mips < full ? (int)mips : full;
          if (nlev > TEX_MAX_LEV) nlev = TEX_MAX_LEV;
      } }
    if (t->res && (t->w != w || t->h != h || t->fmt != fmt || t->cube != cube || t->nlev != nlev)) {
        defer_release(t->res); t->res = NULL;
        defer_release(t->up);  t->up = NULL;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT mip_fp[TEX_MAX_LEV];
    UINT64 mip_total = 0;
    if (nlev > 1) {
        D3D12_RESOURCE_DESC md = {0};
        md.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        md.Width = w; md.Height = h; md.DepthOrArraySize = 1; md.MipLevels = (UINT16)nlev;
        md.Format = dxfmt; md.SampleDesc.Count = 1; md.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        s_d3d.device->lpVtbl->GetCopyableFootprints(s_d3d.device, &md, 0, (UINT)nlev, 0,
                                                    mip_fp, NULL, NULL, &mip_total);
        /* Level 0 must keep the layout the decode below writes. */
        if (mip_fp[0].Offset != 0 || mip_fp[0].Footprint.RowPitch != pitch) nlev = 1;
        if (t->res && t->nlev != nlev) {
            defer_release(t->res); t->res = NULL;
            defer_release(t->up);  t->up = NULL;
        }
    }
    if (!t->res) {
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h;
        td.DepthOrArraySize = cube ? 6 : 1;   /* cube = 6 array slices */
        td.MipLevels = (UINT16)nlev;
        td.Format = dxfmt; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                &IID_ID3D12Resource, (void**)&t->res)))
            return -1;
        D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = (u64)pitch * (dxt ? blkrows : h) * (cube ? 6 : 1);
        if (nlev > 1 && mip_total > bd.Width) bd.Width = mip_total;
        bd.Height = 1; bd.DepthOrArraySize = 1;
        bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&t->up))) {
            t->res->lpVtbl->Release(t->res); t->res = NULL; return -1;
        }
        fresh = 1;
    }

    /* A title may rewrite the image in place first (rsx_tex_hook.h). */
    if (g_rsx_tex_source_hook && !cube)
        g_rsx_tex_source_hook(vm_base + off, dxt ? blkrow * blkrows : w * h * bpp, off);

    /* Texture mods know a texture by its bytes. One whose bytes changed again
     * within a second (a video, an animated surface) is left alone: its every
     * frame would be a new texture. */
    t->thash_ok = 0;
    t->thash_can = !cube && texrep_eligible(basef) && !(restale && s_tex_frame - t->upload_frame < 60);
    t->thash_bytes = dxt ? blkrow * blkrows : w * h * bpp;
    if (t->thash_can && (texpack_replace_on() || texpack_dump_on())) {
        t->thash = texpack_hash(vm_base + off, t->thash_bytes, w, h, basef);
        t->thash_ok = 1;
    }

    void* mapped = NULL; D3D12_RANGE nr = {0,0};
    if (FAILED(t->up->lpVtbl->Map(t->up, 0, &nr, &mapped)) || !mapped) return -1;
    { static int _tp=0; if (getenv("RTT_DUMP") && _tp++ < 6) {
        const u8* sp = vm_base + off;
        fprintf(stderr, "[TEXUP] off=0x%X fmt=0x%X row0:", off, fmt);
        for (int _b=0;_b<8;_b++) fprintf(stderr, " %02X", sp[_b]);
        fprintf(stderr, "  row240:");
        for (int _b=0;_b<8;_b++) fprintf(stderr, " %02X", sp[240*w*bpp+_b]);
        fprintf(stderr, "  mid:");
        for (int _b=0;_b<8;_b++) fprintf(stderr, " %02X", sp[240*w*bpp+320*bpp+_b]);
        fprintf(stderr, "%s", "\n");
    } }
    /* Swizzled (LN bit 0x20 clear): texels live in Morton/Z-order, not rows.
     * Uploading them as linear rows produced the diagonal-stripe garbage on
     * LBP's loading screen (every texture there is 0x85 swizzled). POT dims
     * only -- the hardware requires that for swizzled textures anyway. */
    int swz = tl.swizzled;
    /* Cube textures store their 6 faces consecutively. Convert each into its own
     * slice of the upload buffer; the conversion below is unchanged and simply
     * runs once per face with off/mapped rebased. */
    const u32 _face_rows  = dxt ? blkrows : h;
    /* The face stride is NOT one mip-0 image: RSX stores each cube face as its
     * own complete mip pyramid, so face f starts a whole pyramid (128-byte
     * aligned) in. Assuming mip-0-sized strides made face 1 land inside face 0's
     * mip chain -- which is exactly what the dumps showed, every face after the
     * first a progressively smaller copy of the first. */
    u32 _face_bytes = dxt ? (blkrow * blkrows) : (w * h * bpp);
    if (cube) {
        u32 total = 0;
        for (u32 lw = w, lh = h, l = 0; l < (mips ? mips : 1u); l++) {
            total += dxt ? (((lw + 3) / 4) * (bpp == 8 ? 16u : 8u) * ((lh + 3) / 4))
                         : (lw * lh * bpp);
            if (lw > 1) lw >>= 1;
            if (lh > 1) lh >>= 1;
        }
        _face_bytes = (total + 127u) & ~127u;
    }
    const u32 _nfaces     = cube ? 6u : 1u;
    const u32 _off0 = off; u8* const _map0 = (u8*)mapped;
    for (u32 _f = 0; _f < _nfaces; _f++) {
    off    = _off0 + _f * _face_bytes;
    if (cube && getenv("CUBEDBG")) { u32 _s=0; const u8* _p = vm_base + off;
        for (u32 _i = 0; _i < _face_bytes; _i += 97) _s = _s*131 + _p[_i];
        fprintf(stderr, "[CUBEFACE] f=%u off=0x%X bytes=%u bpp=%u swz=%d csum=%08X%c",
                _f, off, _face_bytes, bpp, swz, _s, 10); }
    mapped = _map0 + (u64)_f * pitch * _face_rows;
    /* Pixel conversion -- deswizzle, channel order, block copy -- is RSX
     * semantics and lives in rsx_texture_layout.c, so a second backend gets
     * it by calling this rather than porting the loops again. */
    rsx_texture_decode(mapped, pitch, vm_base + off, w, h, &tl,
                       rsx_texture_argb_is_rgba());

    /* The diagnostics below only apply to the linear 8-bit case (Bink video
     * planes); they used to sit inside that branch of the conversion. */
    if (!tl.compressed && tl.bytes_per_texel == 1 && !tl.swizzled) {
        /* MOVIE_FIND=1: when a 640x360 movie plane uploads ZERO, scan a wide
         * guest window for the REAL frame (a 640-wide region with content) so
         * we learn where the video decode actually wrote vs where the texture
         * points -- ends the frame-buffer-address guessing. */
        if (getenv("MOVIE_FIND") && w == 640 && h == 360) {
            extern uint8_t* vm_base;
            const u8* here = vm_base + off;
            u32 hz = 0; for (u32 i=0;i<w*h;i+=137) if (here[i]) { hz=1; break; }
            static int _mf = 0;
            if (!hz && _mf++ < 3) {
                fprintf(stderr, "[movie-find] bound plane ea=0x%X is ZERO; full scan...\n", off);
                /* Scan FULL VRAM + main heap in 0x8000 steps for a 640x360
                 * content block -- NO early cap (the previous found<6 stopped in
                 * the low-VRAM display buffers before ever reaching the movie
                 * region ~0x40E80000). Report the near-plane hits explicitly so
                 * ring-desync (frame present at a DIFFERENT slot) is separable
                 * from decode-never-ran (region entirely zero). */
                struct { u32 lo, hi; const char* tag; } rng[] = {
                    {0x40000000u, 0x41000000u, "VRAM"}, {0x00100000u, 0x10000000u, "MAIN"},
                    {0x48000000u, 0x4A000000u, "BINK"} };  /* Bink working set (handle ~0x4849E760, SPU I/O 0x4847/0x4945) */
                u32 plane_lo = (off > 0x400000u) ? off - 0x400000u : 0;
                u32 plane_hi = off + 0x400000u;
                int found = 0, nearc = 0;
                for (int r = 0; r < (int)(sizeof(rng)/sizeof(rng[0])); r++) {
                    u32 best_a = 0, best_nz = 0; u32 best_span = 0;
                    for (u32 a = rng[r].lo; a + w*h < rng[r].hi; a += 0x8000) {
                        const u8* p = vm_base + a;
                        u32 nz = 0; u8 mn2 = 255, mx2 = 0;
                        for (u32 i = 0; i < w*h; i += 257) { u8 v = p[i]; if (v) nz++; if (v<mn2) mn2=v; if (v>mx2) mx2=v; }
                        u32 span = mx2 - mn2;
                        if (nz > best_nz) { best_nz = nz; best_a = a; best_span = span; }
                        if (nz > 400 && span > 60) {  /* looks like image content */
                            int isnear = (a >= plane_lo && a <= plane_hi);
                            if (isnear) nearc++;
                            if (found < 40)
                                fprintf(stderr, "[movie-find]   CONTENT @0x%08X (%s) nz=%u span=%u%s\n",
                                        a, rng[r].tag, nz, span, isnear ? " <== NEAR bound plane" : "");
                            found++;
                        }
                    }
                    /* Always report the densest block in this range even below the
                     * content threshold -- the MM intro fades in from black, so the
                     * real first frames are low-span and would otherwise be invisible. */
                    fprintf(stderr, "[movie-find]   %s best @0x%08X nz=%u span=%u%s\n",
                            rng[r].tag, best_a, best_nz, best_span,
                            (best_a >= plane_lo && best_a <= plane_hi) ? " <== NEAR bound plane" : "");
                    /* Dump the densest block as a 640x360 grayscale BMP so the
                     * "is this the decoded movie frame" question is answered by
                     * eye. One dump per range per process. */
                    if (best_nz > 400 && best_span > 40) {
                        static int _bd[4] = {0,0,0,0};
                        if (r < 4 && !_bd[r]) { _bd[r] = 1;
                            const u8* sp = vm_base + best_a;
                            char pn[128];
                            snprintf(pn, sizeof(pn), "binkscan_%s_%08X.bmp", rng[r].tag, best_a);
                            FILE* f = fopen(pn, "wb");
                            if (f) {
                                u32 rowb = w*3, rowp = (rowb+3)&~3u, fsz = 54 + rowp*h;
                                u8 hd[54] = {'B','M'};
                                hd[2]=(u8)fsz; hd[3]=(u8)(fsz>>8); hd[4]=(u8)(fsz>>16); hd[5]=(u8)(fsz>>24);
                                hd[10]=54; hd[14]=40;
                                hd[18]=(u8)w; hd[19]=(u8)(w>>8); hd[22]=(u8)h; hd[23]=(u8)(h>>8);
                                hd[26]=1; hd[28]=24;
                                fwrite(hd,1,54,f);
                                for (int y=(int)h-1; y>=0; y--) {
                                    const u8* srow = sp + (u64)y*w;
                                    for (u32 x=0;x<w;x++){ u8 px[3]; px[0]=px[1]=px[2]=srow[x]; fwrite(px,1,3,f); }
                                    { u8 z[3]={0,0,0}; fwrite(z,1,rowp-rowb,f); }
                                }
                                fclose(f);
                                fprintf(stderr, "[movie-find]   dumped %s\n", pn);
                            }
                        }
                    }
                }
                fprintf(stderr, "[movie-find]   total=%d near-plane=%d (window 0x%X..0x%X)\n",
                        found, nearc, plane_lo, plane_hi);
                if (!found) fprintf(stderr, "[movie-find]   no 640x360 content block (>thresh) found anywhere\n");
            }
        }
        /* TEX_SAVE=1: also dump wide B8 uploads (Bink video planes are B8 --
         * the Y plane IS the movie frame in grayscale) + a content stat, so
         * "is the decoder producing pixels" is answerable by looking at a BMP. */
        if (getenv("TEX_SAVE") && w >= 256) {
            static int _bs = 0;
            const u8* sp = vm_base + off;
            u32 nz = 0; u8 mn = 255, mx = 0;
            for (u32 i = 0; i < w * h; i += 17) {
                u8 v = sp[i]; if (v) nz++; if (v < mn) mn = v; if (v > mx) mx = v;
            }
            fprintf(stderr, "[TEXB8] off=0x%X %ux%u nz=%u/%u min=%u max=%u\n",
                    off, w, h, nz, (w * h) / 17, mn, mx);
            if (_bs < 8 && mx > mn) { _bs++;
                char pn[128];
                snprintf(pn, sizeof(pn), "texb8_%08X_%ux%u_%d.bmp", off, w, h, _bs);
                FILE* f = fopen(pn, "wb");
                if (f) {
                    u32 rowb = w * 3, rowp = (rowb + 3) & ~3u;
                    u32 fsz = 54 + rowp * h;
                    u8 hd[54] = {'B','M'};
                    hd[2]=(u8)fsz; hd[3]=(u8)(fsz>>8); hd[4]=(u8)(fsz>>16); hd[5]=(u8)(fsz>>24);
                    hd[10]=54; hd[14]=40;
                    hd[18]=(u8)w; hd[19]=(u8)(w>>8); hd[22]=(u8)h; hd[23]=(u8)(h>>8);
                    hd[26]=1; hd[28]=24;
                    fwrite(hd, 1, 54, f);
                    for (int y = (int)h - 1; y >= 0; y--) {
                        const u8* srow = sp + (u64)y * w;
                        for (u32 x = 0; x < w; x++) {
                            u8 px[3]; px[0]=px[1]=px[2]=srow[x];
                            fwrite(px, 1, 3, f);
                        }
                        { u8 z[3] = {0,0,0}; fwrite(z, 1, rowp - rowb, f); }
                    }
                    fclose(f);
                    fprintf(stderr, "[TEX_SAVE] %s\n", pn);
                }
            }
        }
    }
    /* TEX_SAVE=1: dump the first few converted ARGB uploads as BMPs (rgb +
     * alpha channel separately) -- ground-truth for "is the guest texture
     * wrong or is the sampling wrong" questions (wave's hue palette). */
    if (argb && _f + 1 == _nfaces && getenv("TEX_SAVE")) { static int _ts = 0; if (_ts < 8) { _ts++;
        /* For a cube, write every face: the point of the dump is to check the
         * assumed 6-face layout against the game's cubemap art. */
        for (u32 _face = 0; _face < _nfaces; _face++)
        for (int pass = 0; pass < 2; pass++) {
            char pn[128];
            if (_nfaces > 1)
                snprintf(pn, sizeof(pn), "cube_%08X_%ux%u_f%u_%s.bmp", _off0, w, h,
                         _face, pass ? "a" : "rgb");
            else
                snprintf(pn, sizeof(pn), "tex_%08X_%ux%u_%s.bmp", off, w, h,
                     pass ? "a" : "rgb");
            FILE* f = fopen(pn, "wb");
            if (f) {
                u32 rowb = w * 3, rowp = (rowb + 3) & ~3u;
                u32 fsz = 54 + rowp * h;
                u8 hd[54] = {'B','M'};
                hd[2]=(u8)fsz; hd[3]=(u8)(fsz>>8); hd[4]=(u8)(fsz>>16); hd[5]=(u8)(fsz>>24);
                hd[10]=54; hd[14]=40;
                hd[18]=(u8)w; hd[19]=(u8)(w>>8); hd[22]=(u8)h; hd[23]=(u8)(h>>8);
                hd[26]=1; hd[28]=24;
                fwrite(hd, 1, 54, f);
                for (int y = (int)h - 1; y >= 0; y--) {
                    const u8* srow = _map0 + (u64)_face * pitch * _face_rows
                                     + (u64)y * pitch;
                    for (u32 x = 0; x < w; x++) {
                        u8 px[3];
                        if (pass) { px[0]=px[1]=px[2]=srow[x*4+3]; }
                        else { px[0]=srow[x*4+2]; px[1]=srow[x*4+1]; px[2]=srow[x*4+0]; }
                        fwrite(px, 1, 3, f);
                    }
                    { u8 z[3] = {0,0,0}; fwrite(z, 1, rowp - rowb, f); }
                }
                fclose(f);
                fprintf(stderr, "[TEX_SAVE] %s\n", pn);
            }
        }
    } }
    /* TEX_AVGDBG=1: one line per distinct bound texture offset with its average
     * source colour. duck.tga is overwhelmingly yellow (avg ~243,191,23), so this
     * says whether the duck's image is bound at all -- and if it is, the duck is
     * being drawn somewhere and the question is where, not whether. */
    { static int avgd = -1;
      if (avgd < 0) { const char* e = getenv("TEX_AVGDBG"); avgd = e ? atoi(e) : 0; }
      if (avgd && !dxt && bpp == 4) {
        static u32 seen[64]; static int nseen = 0;
        int known = 0;
        for (int i2 = 0; i2 < nseen; i2++) if (seen[i2] == off) { known = 1; break; }
        if (!known && nseen < 64) {
            seen[nseen++] = off;
            /* Report the source as R,G,B,A -- the layout TEX_RGBA uploads (PSGL
             * writes GL_RGBA/UNSIGNED_INT_8_8_8_8, so bytes land R,G,B,A). Alpha
             * matters as much as colour here: a blended surface whose alpha is a
             * flat 255 renders opaque no matter how correct the blend state is. */
            u64 sr = 0, sg = 0, sb = 0, sa = 0; u32 cnt = 0, amin = 255, amax = 0;
            for (u32 i2 = 0; i2 + 3 < w * h * 4u; i2 += 4 * 37) {
                const u8* q = vm_base + off + i2;
                sr += q[0]; sg += q[1]; sb += q[2]; sa += q[3]; cnt++;
                if (q[3] < amin) amin = q[3];
                if (q[3] > amax) amax = q[3];
            }
            if (cnt) fprintf(stderr, "[TEXAVG] raw=0x%08X %4ux%-4u fmt=0x%02X"
                                     " avgRGBA=(%3u,%3u,%3u,%3u) alpha[%u..%u]%c",
                             key_off, w, h, fmt, (u32)(sr/cnt), (u32)(sg/cnt),
                             (u32)(sb/cnt), (u32)(sa/cnt), amin, amax, 10);
        }
      } }
    /* Content-identify the duck's texture so filters can say "duck". */
    if (!dxt && bpp == 4 && w == 512 && h == 512 && s_duck_raw != key_off) {
        u64 ar = 0, ag = 0, ab = 0; u32 n = 0;
        for (u32 i2 = 0; i2 + 3 < w * h * 4u; i2 += 4 * 211) {
            const u8* q = vm_base + off + i2;
            ar += q[0]; ag += q[1]; ab += q[2]; n++;
        }
        if (n && ar/n > 200 && ag/n > 140 && ab/n < 90) {
            s_duck_off = key_off;
            { static u32 last = 0; if (last != key_off) { last = key_off;
                fprintf(stderr, "[DUCKTEX] resolved=0x%08X identified by content%c", key_off, 10); } }
        }
    }
    /* TEX_HILITE=1: paint the duck's texture pure red. duck.tga is overwhelmingly
     * yellow (avg 243,191,23); with the R,G,B,A source order that is bytes
     * s[0] high, s[1] mid, s[2] low. Flooding it with an unmistakable colour
     * answers "where on screen is the duck drawn" directly, which neither the
     * vertex data nor a pixel count can. Diagnostic only. */
    { static int hil = -1;
      if (hil < 0) { const char* e = getenv("TEX_HILITE"); hil = e ? atoi(e) : 0; }
      if (hil && !dxt && bpp == 4 && w == 512 && h == 512) {
        u64 sr = 0, sg = 0, sb = 0; u32 cnt = 0;
        for (u32 i2 = 0; i2 + 3 < w * h * 4u; i2 += 4 * 37) {
            const u8* q = vm_base + off + i2;
            sr += q[0]; sg += q[1]; sb += q[2]; cnt++;
        }
        if (cnt) {
            u32 ar = (u32)(sr/cnt), ag = (u32)(sg/cnt), ab = (u32)(sb/cnt);
            if (ar > 200 && ag > 140 && ab < 90) {
                for (u32 y = 0; y < h; y++) {
                    u8* drow = (u8*)mapped + (u64)y * pitch;
                    for (u32 x = 0; x < w; x++) {
                        drow[x*4+0] = 0xFF; drow[x*4+1] = 0x00;
                        drow[x*4+2] = 0x00; drow[x*4+3] = 0xFF;
                    }
                }
                { static int _n = 0; if (_n++ < 4)
                    fprintf(stderr, "[TEXHILITE] duck texture off=0x%08X avg=(%u,%u,%u) -> red%c",
                            off, ar, ag, ab, 10); }
            }
        }
      } }
    /* TEX_MARKEMPTY=1: paint any texture whose guest source is entirely zero a
     * flat magenta instead of uploading the empty bytes. An unresolved texture
     * and a legitimately black surface are indistinguishable on screen, so this
     * says WHICH geometry is missing its image -- diagnostic only. */
    { static int mark = -1;
      if (mark < 0) { const char* e = getenv("TEX_MARKEMPTY"); mark = e ? atoi(e) : 0; }
      if (mark && !dxt) {
        u32 nz = 0, sz = w * h * bpp;
        for (u32 i2 = 0; i2 < sz && i2 < 0x40000u; i2 += 61) if (vm_base[off + i2]) nz++;
        if (!nz) {
            for (u32 y = 0; y < h; y++) {
                u8* drow = (u8*)mapped + (u64)y * pitch;
                for (u32 x = 0; x < w; x++) {
                    /* Pure RED. Magenta collided with this title's wall tint,
                     * so the marker was indistinguishable from real geometry. */
                    drow[x*4+0] = 0xFF; drow[x*4+1] = 0x00;
                    drow[x*4+2] = 0x00; drow[x*4+3] = 0xFF;
                }
            }
            { static int _n = 0; if (_n++ < 8)
                fprintf(stderr, "[TEXEMPTY] off=0x%08X %ux%u fmt=0x%02X -> magenta%c",
                        off, w, h, fmt, 10); }
        }
      } }
    }   /* end per-face conversion */
    off = _off0; mapped = _map0;
    /* Mip levels 1.. (RSX_TEX_MIPS): each follows the previous one in guest
     * memory, at its own size, in the same layout (swizzled or blocks). */
    if (nlev > 1) {
        u32 src = off;
        for (int l = 0; l < nlev; l++) {
            u32 lw = w >> l, lh = h >> l;
            if (!lw) lw = 1;
            if (!lh) lh = 1;
            rsx_tex_layout tll;
            rsx_texture_layout(fmt, lw, lh, &tll);
            if (l > 0)
                rsx_texture_decode(_map0 + mip_fp[l].Offset, mip_fp[l].Footprint.RowPitch,
                                   vm_base + src, lw, lh, &tll, rsx_texture_argb_is_rgba());
            src += tll.compressed ? tll.row_bytes * tll.rows : lw * lh * tll.bytes_per_texel;
        }
    }
    /* TEX_DUMP (see above): also the decoded upload buffer, all faces, as the
     * GPU will copy it (texdump_<off>_up.bin). */
    { static int nu = 0; const char* e = getenv("TEX_DUMP");
      if (e && nu < 2 && (off & 0x0FFFFFFFu) == ((u32)strtoul(e, NULL, 16) & 0x0FFFFFFFu)) {
        char fn[96]; snprintf(fn, sizeof fn, "texdump_%08X_up%d.bin", off, nu++);
        FILE* f = fopen(fn, "wb");
        if (f) { fwrite(_map0, 1, (size_t)pitch * _face_rows * _nfaces, f); fclose(f); }
        fprintf(stderr, "[TEXDUMP] %s pitch=%u rows=%u faces=%u dxfmt=%d slot=%d%c", fn, pitch, _face_rows,
                _nfaces, (int)dxfmt, slot, 10);
      } }
    /* TEXTURE_DUMP: written now, or at its next bind if dumping is switched on later. */
    t->dump_seen = 0;
    if (t->thash_ok && texpack_dump_on()) {
        t->dump_seen = 1;
        if (texpack_dump_wanted(t->thash)) tex_dump_image(t->thash, _map0, pitch, w, h, basef);
    }
    t->up->lpVtbl->Unmap(t->up, 0, NULL);

    if (!fresh) {   /* reused resource: PSR -> COPY_DEST first */
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t->res;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = t->res; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = t->up;  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format   = dxfmt;
    src.PlacedFootprint.Footprint.Width    = w;
    src.PlacedFootprint.Footprint.Height   = h;
    src.PlacedFootprint.Footprint.Depth    = 1;
    src.PlacedFootprint.Footprint.RowPitch = pitch;
    for (u32 _f = 0; _f < _nfaces; _f++) {
        dst.SubresourceIndex        = _f;      /* cube face = array slice */
        src.PlacedFootprint.Offset  = (u64)_f * pitch * _face_rows;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    }
    for (int l = 1; l < nlev; l++) {
        dst.SubresourceIndex   = (UINT)l;
        src.PlacedFootprint    = mip_fp[l];
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    }
    {
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t->res;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }
    /* SRV at heap slot 1+slot */
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = dxfmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    /* RSX B8 replicates the byte into all four channels (dbgfont's FP reads
     * coverage from .w); DXGI R8 defaults to (r,0,0,1), so swizzle (R,R,R,R)
     * = encoded 0x1000 (component 0 in all lanes + always-set bit). ARGB8
     * keeps the identity mapping. */
    sv.Shader4ComponentMapping = (basef == 0x81) ? 0x1000 : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = (UINT)nlev;
    D3D12_CPU_DESCRIPTOR_HANDLE sh;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
    sh.ptr += (u64)(1 + slot) * s_d3d.srv_inc;
    if (cube && rsx_cube_as_array()) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Texture2DArray.MostDetailedMip = 0;
        sv.Texture2DArray.MipLevels = 1;
        sv.Texture2DArray.FirstArraySlice = 0;
        sv.Texture2DArray.ArraySize = 6;
        sv.Texture2DArray.PlaneSlice = 0;
        sv.Texture2DArray.ResourceMinLODClamp = 0.0f;
    } else if (cube) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = 1;
        sv.TextureCube.MostDetailedMip = 0;
        sv.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    /* The fixed per-slot view (1+slot) only exists below RT_SRV_BASE: the
     * slots above it share the heap with the RT and per-draw windows. Draws
     * bind their textures through their own windows (srv_commit), so the
     * extra slots need no fixed view. */
    if (1 + slot < RT_SRV_BASE)
        s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, t->res, &sv, sh);

    off = _off0;   /* the face loop left it on the last cube face */
    t->off = off; t->key = key_off; t->w = w; t->h = h; t->fmt = fmt;
    t->cube = cube; t->used = 1; t->stamp = s_tex_frame; t->upload_frame = s_tex_frame;
    t->nlev = nlev;
    /* Native UI replacement is independent of user texture packs. Keep the
     * guest texture and its key/dimensions intact; only its sampled resource
     * changes. Query once per upload, never once per draw. */
    {
        texpack_image* image = NULL;
        u64 id = 0;
        if (!cube && g_rsx_tex_ui_hook)
            id = g_rsx_tex_ui_hook(vm_base + off, t->thash_bytes, w, h,
                s_rs, t->ui_id, &image);
        if (id != t->ui_id || (!id && t->ui_res)) {
            if (t->ui_res) defer_release(t->ui_res);
            t->ui_res = NULL; t->ui_id = 0;
            if (id && image && image->data && image->levels == 1 &&
                image->w > 0 && image->h > 0 && image->w <= 8192 && image->h <= 8192 &&
                image->bytes == (size_t)image->w * image->h * 4u && image->bytes <= (64u << 20)) {
                t->ui_res = rep_upload(image);
                if (t->ui_res) {
                    t->ui_id = id;
                    fprintf(stderr, "[UI-HD] %ux%u -> %ux%u, render scale %.2f, source 0x%08X%c",
                        w,h,image->w,image->h,s_rs,off,10);
                }
            }
        }
        if (image) texpack_free(image);
        t->ui_scale = s_rs;
    }
    s_tex_hint[hk] = (short)(t - s_d3d.vp_tex + 1);
    { u32 nb = dxt ? (blkrow * blkrows) : (w * h * bpp);
      t->csum = TEX_CSUM(vm_base + off, nb); }
    #undef TEX_CSUM
    return slot;
}

/* ---------------------------------------------------------------------------
 * Render-to-texture: offscreen RT pool.
 *
 * Draws/clears targeting a non-display surface (SET_SURFACE_COLOR_xOFFSET
 * not registered via cellGcmSetDisplayBuffer) render into a pooled RGBA8
 * texture keyed by the surface's resolved vm offset; a later draw binding a
 * texture at that offset samples the RT directly instead of guest memory.
 * RGBA8 for every RT (even half-float guest surfaces) keeps the existing
 * PSO/RTV formats -- values clamp to [0,1], which demosaic's RGB data fits.
 * -----------------------------------------------------------------------*/

/* Snapshot the VP constant bank + viewport epilogue for one draw into its
 * vp_cb slot. Runs at record time so every draw keeps the constants that
 * were live when the guest issued it. */
static void vp_record_cb(u32 slot, int vs_idx, const D3D12DrawRecord* dr)
{
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!s_d3d.vp_cb_mapped || !st || slot >= MAX_DRAWS) return;
    /* FP texcoord scale (b1): 1/size for UNnormalized textures (fmt bit
     * 0x40 -- wave samples everything in texel space), 1.0 otherwise. */
    if (s_d3d.vp_fpcb_mapped) {
        float* ts = (float*)((char*)s_d3d.vp_fpcb_mapped
            + ((u64)s_d3d.vp_parity * MAX_DRAWS + slot) * VP_FPCB_STRIDE);
        for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
            /* .xy: half-texel edge distances, computed once per draw instead
             * of dividing by texture dimensions in every shaded pixel. This
             * preserves the guest-size clamp (including scaled render targets).
             * RSX_ADDR's clamp modes stop at the edge texel centres. The static samplers wrap, and a linear fetch at
             * exactly 0.0 blended the first texel with the last: Code
             * Veronica's 1024x1 colour LUT turned every black pixel into
             * (LUT[0] + LUT[1023]) / 2, the bright speckle over the cell. */
            float sx = 0.0f, sy = 0.0f;
            if (dr && dr->tex[_u].set) {
                sx = (float)dr->tex[_u].w;
                sy = (float)dr->tex[_u].h;
            }
            ts[_u*4+0] = precomputed_tex_edges_on() ? (sx > 0.0f ? 0.5f / sx : 0.0f) : sx;
            ts[_u*4+1] = precomputed_tex_edges_on() ? (sy > 0.0f ? 0.5f / sy : 0.0f) : sy;
            ts[_u*4+2] = 0.0f; ts[_u*4+3] = 0.0f;
            /* .zw: the guest's S/T address modes, applied in the shader by
             * RSX_ADDR (the static samplers cannot follow them per draw). */
            if (dr && dr->tex[_u].set) {
                ts[_u*4+2] = (float)(dr->tex[_u].addr & 0xFu);
                ts[_u*4+3] = (float)((dr->tex[_u].addr >> 8) & 0xFu);
            }
        }
        /* rsx_alphatest (b1[4]): x=enable, y=ref/255, z=func-0x200. D3D12
         * has no fixed alpha test; guest FPs discard on it dynamically
         * (wave's colour wheel is a disc via alpha test -- without it the
         * palette drew as an opaque square). */
        /* rsx_texaddr[u].z bits 4..7: the unit's texture gamma (sRGB decode)
         * bits, from TEXTURE_ADDRESS[20:23] (R,G,B,A). */
        if (dr && !getenv("NO_TEXGAMMA"))
            for (int _u = 0; _u < FP_TEX_UNITS; _u++)
                if (dr->tex[_u].set) {
                    /* Colour formats only (A1R5G5B5..DXT5). Single-channel
                     * planes (B8 = 0x81, G8B8 = 0x8B) hold luma/chroma or
                     * data: decoding them broke the FMV YUV->RGB maths. */
                    u32 bf = dr->tex[_u].fmt & 0x9Fu;
                    if (bf >= 0x82 && bf <= 0x88)
                        ts[_u*4+2] = (float)((dr->tex[_u].addr & 0xFu) |
                                             (((dr->tex[_u].addr >> 20) & 0xFu) << 4));
                }
        /* rsx_texaddr[u].z bit 8: an edge-to-edge Y16_X16 cube look-up table
         * (rsx_cube). RSX_CUBE_LUT_RAW=1 samples it at texel centres instead. */
        { static int raw = -1; if (raw < 0) raw = getenv("RSX_CUBE_LUT_RAW") ? 1 : 0;
          if (dr && !raw)
              for (int _u = 0; _u < FP_TEX_UNITS; _u++)
                  if (dr->tex[_u].set && dr->tex[_u].cube && (dr->tex[_u].fmt & 0x9Fu) == 0x95u &&
                      dr->tex[_u].w > 1 && dr->tex[_u].w <= 64 && dr->tex[_u].w == dr->tex[_u].h)
                      ts[_u*4+2] = (float)((u32)ts[_u*4+2] | 256u);
        }
        /* TEX_NOGAMMA_FP=<code hex>:<unit>,...: drop the sRGB decode of those
         * units for the program with that code hash (an experiment: RPCS3
         * reads Code Veronica's water normal map raw). */
        { static const char* ng = (const char*)1; static u32 ncode = 0, nmask = 0;
          if (ng == (const char*)1) {
              ng = getenv("TEX_NOGAMMA_FP");
              if (ng) { char* e = NULL; ncode = (u32)strtoul(ng, &e, 16);
                        while (e && *e) { long i = strtol(e + 1, &e, 10);
                                          if (i >= 0 && i < 16) nmask |= 1u << i; } }
          }
          if (ng && dr && dr->fp_addr && dr->fp_bytes) {
              extern uint8_t* vm_base;
              extern u32 cellGcmResolveLocated(int, u32);
              extern u32 rsx_fp_code_hash(const u8*, u32);
              u32 fo = cellGcmResolveLocated((dr->fp_addr & 0x3u) == 1, dr->fp_addr & ~0x3u);
              if (vm_base && fo != 0xFFFFFFFFu && rsx_fp_code_hash(vm_base + fo, dr->fp_bytes) == ncode)
                  for (int _u = 0; _u < FP_TEX_UNITS && _u < 8; _u++)
                      if (nmask & (1u << _u))
                          ts[_u*4+2] = (float)((u32)ts[_u*4+2] & 0x8Fu);
          } }
        if (dr && !getenv("NO_ALPHATEST")) {
            { static int _at = 0;
              if (((dr->alpha_ctl >> 16) & 1u) && _at++ < 4)
                  printf("[ALPHATEST] enable func=%u ref=%u\n",
                         (dr->alpha_ctl >> 8) & 0xFFu, dr->alpha_ctl & 0xFFu); }
            ts[FP_ALPHA_BASE+0] = (float)((dr->alpha_ctl >> 16) & 1u);
            ts[FP_ALPHA_BASE+1] = (float)(dr->alpha_ctl & 0xFFu) / 255.0f;
            ts[FP_ALPHA_BASE+2] = (float)((dr->alpha_ctl >> 8) & 0x07u);
            ts[FP_ALPHA_BASE+3] = 0.0f;
        } else {
            ts[FP_ALPHA_BASE+0] = 0.0f; ts[FP_ALPHA_BASE+1] = 0.0f;
            ts[FP_ALPHA_BASE+2] = 7.0f; ts[FP_ALPHA_BASE+3] = 0.0f;
        }
        /* rsx_alpha_k.w: 1/RES_SCALE of this draw's target, which WPOS.xy is
         * divided by (fp_post_res_scale) so programs see guest pixels. */
        if (dr && s_rs != 1.0f) {
            float sc = dr->rt_off ? rs_for(dr->rt_w ? dr->rt_w : s_d3d.width,
                                           dr->rt_h ? dr->rt_h : s_d3d.height) : s_rs;
            ts[FP_ALPHA_BASE+3] = 1.0f / sc;
        }

        /* Record phase: fp_snap_ptr() belongs to the frozen render batch,
         * not this recording arena. Do not inspect it here: the same offset
         * can name another program in the previous frame, causing flicker.
         * The shader-side code whitelist already limits who reads this lane. */
        if (s_lighter_probe && dr && dr->rt_off == 0x01C30000u && !dr->tex[7].set) {
            ts[7*4+3] = InterlockedCompareExchange(&s_lighter_shadow_bypass, 0, 0) ? -1234.0f : 0.0f;
        }
        /* fp_k[]: the program's inline fragment constants, re-read from the
         * guest ucode every draw. The decompiler emits fp_k[i] lookups instead
         * of baking these in as literals, so the compiled shader no longer
         * changes when the title re-patches them -- which is what forced the
         * pipeline cache to key on a hash of the ucode bytes. */
        if (dr && dr->fp_addr) {
            extern uint8_t* vm_base;
            extern u32 cellGcmResolveLocated(int, u32);
            u32 foff = cellGcmResolveLocated((dr->fp_addr & 0x3u) == 1,
                                             dr->fp_addr & ~0x3u);
            if (dr->fp_snap && s_fp_arena) {   /* extracted once, with the snapshot */
                const u8* tail = s_fp_arena + dr->fp_snap - 1 + dr->fp_bytes + 16;
                u32 nk; memcpy(&nk, tail, 4);
                if (nk > FP_MAX_K) nk = FP_MAX_K;
                memcpy(&ts[FP_K_BASE], tail + 16, (size_t)nk * 16u);
            } else if (vm_base && foff != 0xFFFFFFFFu)
                rsx_fp_extract_consts(vm_base + foff, dr->fp_bytes, &ts[FP_K_BASE], FP_MAX_K);   /* fp_k[] */
            /* FP_ZERO_K=<code hex>:<i>,<j>,...: zero those fp_k[] entries of the
             * program with that code hash (an experiment: RPCS3 draws Code
             * Veronica's water with its normal-map constants at zero). */
            { static const char* zk = (const char*)1; static u32 zcode = 0; static u32 zmask[4];
              if (zk == (const char*)1) {
                  zk = getenv("FP_ZERO_K");
                  if (zk) { char* e = NULL; zcode = (u32)strtoul(zk, &e, 16);
                            while (e && *e) { long i = strtol(e + 1, &e, 10);
                                              if (i >= 0 && i < 128) zmask[i >> 5] |= 1u << (i & 31); } }
              }
              if (zk && vm_base && foff != 0xFFFFFFFFu && dr->fp_bytes) {
                  extern u32 rsx_fp_code_hash(const u8*, u32);
                  if (rsx_fp_code_hash(vm_base + foff, dr->fp_bytes) == zcode)
                      for (int i = 0; i < 128 && i < FP_MAX_K; i++)
                          if (zmask[i >> 5] & (1u << (i & 31)))
                              memset(&ts[FP_K_BASE + i * 4], 0, 4 * sizeof(float));
              } }
            /* FPK_DBG=1: the fragment constants as the shader will see them.
             * A program whose only output is fp_k[0].xxxx renders black if
             * those constants never arrive, which looks identical from the
             * outside to geometry that never rasterised. */
            { static int fk = -1; if (fk < 0) fk = getenv("FPK_DBG") ? 1 : 0;
              static int n = 0;
              if (fk && n < 12 && dr->fp_addr != 0x00EC2B02u) { n++;
                  fprintf(stderr, "[FPK] tex0=0x%08X fp=0x%08X k0=(%g %g %g %g)"
                                  " k1=(%g %g %g %g)%c",
                          dr->tex[0].raw, dr->fp_addr,
                          ts[FP_K_BASE+0], ts[FP_K_BASE+1], ts[FP_K_BASE+2], ts[FP_K_BASE+3],
                          ts[FP_K_BASE+4], ts[FP_K_BASE+5], ts[FP_K_BASE+6], ts[FP_K_BASE+7], 10);
                  const u32 raw = dr->fp_addr & ~0x3u;
                  const u32 ea_main  = cellGcmResolveLocated(0, raw);
                  const u32 ea_local = cellGcmResolveLocated(1, raw);
                  fprintf(stderr, "[FPK]   ucode main@0x%08X:", ea_main);
                  for (int b = 0; b < 48; b++) fprintf(stderr, "%s%02X", (b%16)?" ":"  |", vm_base[ea_main + b]);
                  fputc(10, stderr); }
              /* distinct fragment programs seen -- if every draw shares one,
               * SET_SHADER_PROGRAM is not being tracked per draw. */
              if (fk) { static u32 seen[16]; static int ns = 0; int f = 0;
                  for (int q = 0; q < ns; q++) if (seen[q] == dr->fp_addr) f = 1;
                  if (!f && ns < 16) { seen[ns++] = dr->fp_addr;
                      fprintf(stderr, "[FPK] distinct fp #%d = 0x%08X%c", ns, dr->fp_addr, 10); } } }
        }
    }
    /* b0: the vertex-constant bank plus the viewport-derived vp_posscale /
     * vp_posoffset words. Built from the CPU-side state and written once --
     * the upload heap is write-combined, so reading it back (the projection
     * check used to read c[0] there) costs far more than the copy. When no
     * constant changed since the previous draw of this batch and the derived
     * words match, the draw points at that draw's bank instead of copying
     * 7.5 KB again (Code Veronica's lighter shadow passes: ~6600 draws). */
    D3D12DrawRecord* mdr = (D3D12DrawRecord*)dr;
    if (mdr) mdr->vcb_slot = slot;
    float vpx[8];
    const float* vs_ = st->viewport_scale;
    const float* vo_ = st->viewport_offset;
    vpx[0] = vpx[1] = vpx[3] = 1.0f;
    vpx[4] = vpx[5] = vpx[7] = 0.0f;
    if (vs_[2] != 0.0f) { vpx[2] = vs_[2]; vpx[6] = vo_[2]; }
    else                { vpx[2] = 1.0f;   vpx[6] = 0.0f;   }
    /* DBG_ZOOM=<k> with DBG_CENTER="cx,cy": debug camera in clip space
     * (pos.xy = _p.xy * k + _p.w * (-c * k)). DBG_LOCK follows a locked point. */
    { static int z = -1; static float k = 1.0f, cx = 0.0f, cy = 0.0f;
      if (z < 0) { const char* e = getenv("DBG_ZOOM");
                   k = e ? (float)atof(e) : 1.0f;
                   z = (e && k > 0.0f) ? 1 : 0;
                   const char* ce = getenv("DBG_CENTER");
                   if (ce) { double a = 0, b = 0; sscanf(ce, "%lf,%lf", &a, &b);
                             cx = (float)a; cy = (float)b; } }
      if (z) { float ux = cx, uy = cy;
               static int lock = -1;
               if (lock < 0) { const char* le = getenv("DBG_LOCK"); lock = le ? atoi(le) : 0; }
               if (lock && s_lock_valid) { ux = s_lock_x; uy = s_lock_y; }
               vpx[0] = k; vpx[1] = k; vpx[4] = -ux * k; vpx[5] = -uy * k; } }

    /* Garbage-projection fallback (vkcube): c0 outside (0, 8) with a VP that
     * reads c0-c3 gets a fixed perspective instead. */
    int uses_c03 = (vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n)
                       ? s_d3d.vp_vs[vs_idx].uses_c03 : s_d3d.vp_uses_c03;
    float c00 = st->vertex_constants[0][0];
    int garbage = !(c00 > 0.0f && c00 < 8.0f);
    static int s_nofix = -1, s_forcefix = -1;
    if (s_nofix < 0)    s_nofix = getenv("VP_NOFIXPROJ") ? 1 : 0;
    if (s_forcefix < 0) s_forcefix = getenv("VP_FIXPROJ") ? 1 : 0;
    if (s_nofix) garbage = 0;
    const int fix = (garbage && uses_c03) || s_forcefix;
    if (fix) { vpx[2] = 1.0f; vpx[6] = 0.0f; }

    /* VP_MVP / RSX_DBG_CB print from the slot: always write it when they are on. */
    static int dbg = -1;
    if (dbg < 0) dbg = (getenv("VP_MVP") || getenv("RSX_DBG_CB") || getenv("VCB_NODEDUP")) ? 1 : 0;
    static u32 l_gen, l_slot, l_par, l_batch; static int l_ok = 0; static float l_vpx[8];
    if (!fix && !dbg && l_ok && l_batch == s_vcb_batch && l_par == (u32)s_d3d.vp_parity && l_slot < slot &&
        st->vc_gen == l_gen && !memcmp(vpx, l_vpx, sizeof vpx)) {
        if (mdr) mdr->vcb_slot = l_slot;
        s_vcb_reused++;
        return;
    }
    char* dst = (char*)s_d3d.vp_cb_mapped
        + ((u64)s_d3d.vp_parity * MAX_DRAWS + slot) * VP_CB_STRIDE;
    memcpy(dst, st->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16);
    if(s_pose_on)memcpy(s_pose_cameras+((size_t)s_d3d.vp_parity*MAX_DRAWS+slot)*32,st->vertex_constants,32*sizeof(float));
    memcpy(dst + RSX_MAX_VERTEX_CONSTANTS * 16, vpx, sizeof vpx);
    if (fix) {
        { static int _fb = 0; if (_fb++ < 6)
            printf("[VPFB] fallback proj on draw slot %u (c00=%g vs_idx=%d)\n",
                   slot, c00, vs_idx); }
        float c[16] = {0};
        c[0]=1.358f; c[5]=2.414f; c[11]=1.0f;
        c[10]=1.0f/99.0f; c[14]=-1.0f/99.0f;
        memcpy(dst, c, sizeof c);
    }
    l_ok = !fix; l_gen = st->vc_gen; l_slot = slot; l_par = (u32)s_d3d.vp_parity; l_batch = s_vcb_batch;
    memcpy(l_vpx, vpx, sizeof vpx);
    s_vcb_copied++;
    if (!dbg) return;
    /* VP_MVP=<N>: the constant bank as the shader will see it, for the first N
     * draws. A snapshot taken from a stale rsx_state looks exactly like a broken
     * vertex program from the outside -- both give zero fragments. */
    { static int cap = -1, seen = 0;
      if (cap < 0) { const char* e = getenv("VP_MVP"); cap = e ? atoi(e) : 0; }
      if (cap && seen < cap) { seen++;
        const float* c = (const float*)dst;
        int last_nz = -1;
        for (int i = 0; i < RSX_MAX_VERTEX_CONSTANTS * 4; i++)
            if (c[i] > 1e-9f || c[i] < -1e-9f) last_nz = i / 4;
        fprintf(stderr, "[VPMVP] slot=%u lastNZ=c%d  c260=(%g %g %g %g) c467=(%g %g %g %g)\n",
                slot, last_nz,
                c[260*4+0], c[260*4+1], c[260*4+2], c[260*4+3],
                c[467*4+0], c[467*4+1], c[467*4+2], c[467*4+3]); } }
    { static const char* dc = (const char*)1; static u32 wantc = 0; static int n = 0;
      if (dc == (const char*)1) { dc = getenv("RSX_DBG_CB");
                                  wantc = dc ? (u32)strtoul(dc, NULL, 16) : 0; }
      if (wantc && dr && dr->tex[0].raw == wantc && n < 4) { n++;
          const float* c = (const float*)dst;
          fprintf(stderr, "[DUCKCB] slot=%u vs_idx=%d posscale=(%g %g %g) posoffset=(%g %g %g)%c",
                  slot, vs_idx, vpx[0], vpx[1], vpx[2], vpx[4], vpx[5], vpx[6], 10);
          for (int r = 256; r <= 259; r++)
              fprintf(stderr, "[DUCKCB]   c%d=(%g %g %g %g)%c", r,
                      c[r*4+0], c[r*4+1], c[r*4+2], c[r*4+3], 10);
      } }
}

extern int cellGcmOffsetIsDisplay(u32 offset);

/* Record an unscaled local-memory blit from a GPU-rendered surface.
 * Keeping it in FIFO order avoids copying the stale CPU shadow before draws. */
int rsx_d3d12_record_blit(u32 src,u32 sp,u32 dst,u32 dp,u32 w,u32 h)
{
    if(!s_d3d.initialized || !sp || !dp || sp%4 || dp%4 || !w || !h || s_d3d.draw_count>=MAX_DRAWS) return 0;
    u32 sb=src,sw=0,sh=0,sfmt=0,db=dst,dw=dp/4,dh=h;
    for(u32 i=0;i<s_d3d.draw_count;i++) { const D3D12DrawRecord* r=&s_d3d.draws[i];
        if(r->rt_off && (r->rt_fmt&0x1F)==8 && src>=r->rt_off && (u64)(src-r->rt_off)<(u64)sp*r->rt_h && r->rt_w==sp/4)
            {sb=r->rt_off;sw=r->rt_w;sh=r->rt_h;sfmt=r->rt_fmt;}
        if(r->rt_off && (r->rt_fmt&0x1F)==8 && dst>=r->rt_off && (u64)(dst-r->rt_off)<(u64)dp*r->rt_h && r->rt_w==dw)
            {db=r->rt_off;dh=r->rt_h;}
    }
    if(!sw)for(int i=0;i<MAX_OFF_RTS;i++){const OffRT* r=&s_d3d.off_rt[i];
        if(r->res && r->dxgi==DXGI_FORMAT_R8G8B8A8_UNORM && r->w==sp/4 && src>=r->off && (u64)(src-r->off)<(u64)sp*r->h)
            {sb=r->off;sw=r->w;sh=r->h;sfmt=8;break;}}
    /* Reinterpreting a destination at another pitch/format needs a byte-wise
     * resolve; do not replace an existing render target with incompatible dims. */
    for(u32 i=0;i<s_d3d.draw_count;i++){const D3D12DrawRecord* r=&s_d3d.draws[i];
        if(r->rt_off==db && (r->rt_w!=dw || r->rt_h!=dh || (r->rt_fmt&0x1F)!=8))return 0;}
    for(int i=0;i<MAX_OFF_RTS;i++){const OffRT* r=&s_d3d.off_rt[i];
        if(r->res && r->off==db && (r->w!=dw || r->h!=dh || r->dxgi!=DXGI_FORMAT_R8G8B8A8_UNORM))return 0;}
    if(!sw || src%4 || dst%4)return 0;
    u32 sx=((src-sb)%sp)/4,sy=(src-sb)/sp,dx=((dst-db)%dp)/4,dy=(dst-db)/dp;
    if((u64)sx+w>sw || (u64)sy+h>sh || (u64)dx+w>dw || (u64)dy+h>dh || sb==db)return 0;
    D3D12DrawRecord* r=&s_d3d.draws[s_d3d.draw_count++];memset(r,0,sizeof(*r));
    r->is_vp=1;r->is_copy=1;r->rt_off=db;r->rt_w=dw;r->rt_h=dh;r->rt_fmt=sfmt;
    r->copy_src=sb;r->copy_sx=sx;r->copy_sy=sy;r->copy_dx=dx;r->copy_dy=dy;r->copy_w=w;r->copy_h=h;
    return 1;
}

/* Which offscreen surface (if any) does the current RSX state render to?
 * Returns 0 for a display buffer (backbuffer), else the surface's RAW RSX
 * offset. RTs are keyed by raw offset -- a texture bound at the same raw
 * offset is the same buffer (surface and texture registers share the offset
 * space; location bits differ but one title doesn't alias local vs main at
 * one offset), which sidesteps guessing the surface's context DMA location. */
static u32 current_rt_off(u32* out_w, u32* out_h, u32 out_mrt[3])
{
    extern int cellGcmOffsetIsDisplay(u32 offset);
    const rsx_state* st = s_d3d.current_rsx_state;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (out_mrt) out_mrt[0] = out_mrt[1] = out_mrt[2] = 0;
    if (!st) return 0;
    /* SET_SURFACE_COLOR_TARGET: 1 = A, 2 = B, 0x13 = MRT1 (A+B),
     * 0x17 = MRT2 (A+B+C), 0x1F = MRT3 (A+B+C+D). */
    int sel = (st->color_target == 2) ? 1 : 0;
    u32 raw = st->surface_color_offset[sel];
    if (out_mrt) {
        if (st->color_target >= 0x13) out_mrt[0] = st->surface_color_offset[1];
        if (st->color_target >= 0x17) out_mrt[1] = st->surface_color_offset[2];
        if (st->color_target >= 0x1F) out_mrt[2] = st->surface_color_offset[3];
    }
    { static int _rs = -1; if (_rs < 0) _rs = getenv("RT_SEQDBG") ? 1 : 0;
      static u32 _last = 0; static int _n2 = 0;
      if (_rs && st->surface_color_offset[0] != _last && _n2 < 400000) {
          _last = st->surface_color_offset[0]; _n2++;
          fprintf(stderr, "[RTSEQ] #%d -> 0x%X%c", _n2, _last, 10); } }
    { static int _ro = -1; if (_ro < 0) _ro = getenv("RT_OFFDBG") ? 1 : 0;
      static u32 _seen[32]; static int _ns = 0;
      if (_ro) { u32 _k = st->surface_color_offset[0] ^ (st->color_target << 24)
                        ^ (st->surface_clip_w << 8) ^ st->surface_format;
        int _f = 0; for (int _i = 0; _i < _ns; _i++) if (_seen[_i] == _k) _f = 1;
        if (!_f && _ns < 32) { _seen[_ns++] = _k;
        fprintf(stderr, "[RTOFF] fmt=0x%X tgt=0x%X off[0]=0x%X off[1]=0x%X zeta=0x%X clip=%ux%u disp=%d -> 0x%X\n",
                st->surface_format, st->color_target, st->surface_color_offset[0], st->surface_color_offset[1],
                st->surface_zeta_offset, st->surface_clip_w, st->surface_clip_h,
                cellGcmOffsetIsDisplay(raw), cellGcmOffsetIsDisplay(raw) ? 0 : raw); } } }
    if (cellGcmOffsetIsDisplay(raw)) return 0;
    /* RT_DISPLAY_BY_SIZE=1: also treat a single-target surface whose clip
     * exactly matches the display resolution as the backbuffer.
     *
     * cellGcmSetDisplayBuffer only registers the buffers the FLIP may point at;
     * a guest is free to render into a different surface of the same size and
     * flip to it later (PSGL does). Matching offsets alone then classifies every
     * draw as offscreen, has_display stays 0, and render_frame() -- which is
     * what draws ALL recorded geometry -- is never called. The backbuffer shows
     * only the clear, which looks exactly like "nothing rasterizes". */
    { static int _bs = -1; if (_bs < 0) _bs = getenv("RT_DISPLAY_BY_SIZE") ? 1 : 0;
      if (_bs && st->color_target < 0x13 &&
          st->surface_clip_w == s_d3d.width && st->surface_clip_h == s_d3d.height)
          return 0; }
    /* Surface clip dims when sane; else the window size. Any size works --
     * passes draw normalized full-surface quads -- this only picks resolution. */
    u32 w = st->surface_clip_w, h = st->surface_clip_h;
    /* Strip surfaces are real: Code Veronica renders a 1024x1 colour lookup
     * table. Treating every clip under 16 as "unset" made it a 1280x720
     * target, and the table's texture then no longer matched its surface. */
    if (w < 1 || w > 2048 || h < 1 || h > 2048) { w = 0; h = 0; }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return raw;
}

/* RSX surface colour format (SET_SURFACE_FORMAT bits [4:0]) -> DXGI. Float
 * targets matter: wave's water height maps store SIGNED values (F_W16..),
 * demosaic's differential planes likewise -- RGBA8 clamps them to zero. */
static DXGI_FORMAT rsx_surface_dxgi(u32 fmt)
{
    switch (fmt & 0x1F) {
    case 0x0B: return DXGI_FORMAT_R16G16B16A16_FLOAT; /* F_W16Z16Y16X16 */
    case 0x0C: return DXGI_FORMAT_R32G32B32A32_FLOAT; /* F_W32Z32Y32X32 */
    case 0x0D: return DXGI_FORMAT_R32_FLOAT;          /* F_X32          */
    default:   return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

/* A surface address can be bound at several sizes in one frame: Code
 * Veronica's lighter frame lays depth through 0x011A4000 at 1280x720 and
 * later reuses it for 320x180 and 640x360 bloom passes. One resource per
 * address was recreated at each size and kept only the last, so the
 * full-screen pass covered a quarter of the screen. Slots are keyed on
 * address AND size; a lookup by address alone prefers a slot this frame
 * uses. */
static int off_rt_find(u32 off)
{
    if (!off) return -1;
    int any = -1;
    for (int i = 0; i < MAX_OFF_RTS; i++)
        if (s_d3d.off_rt[i].res && s_d3d.off_rt[i].off == off) {
            if (s_d3d.off_rt[i].used) return i;
            if (any < 0) any = i;
        }
    return any;
}

/* The slot for this address at exactly w x h, else any slot at the address. */
static int off_rt_find_sz(u32 off, u32 w, u32 h)
{
    if (!off) return -1;
    if (!w) w = s_d3d.width;
    if (!h) h = s_d3d.height;
    /* Preserve exact-size priority, then first-used/first-any fallback,
     * with no cache retained across table changes. */
    int any=-1,used=-1;
    for(int i=0;i<MAX_OFF_RTS;i++) {
        if(!s_d3d.off_rt[i].res||s_d3d.off_rt[i].off!=off)continue;
        if(s_d3d.off_rt[i].w==w&&s_d3d.off_rt[i].h==h)return i;
        if(any<0)any=i;
        if(used<0&&s_d3d.off_rt[i].used)used=i;
    }
    return used>=0?used:any;
}

/* Ensure an RT resource exists for this surface; (re)creates the RTV at
 * rt_rtv_heap[i] and the SRV at srv_heap[RT_SRV_BASE+i]. */
static int off_rt_get(u32 off, u32 w, u32 h, u32 rsx_fmt)
{
    if (!off || !s_d3d.rt_rtv_heap || !s_d3d.srv_heap) return -1;
    if (!w) w = s_d3d.width;
    if (!h) h = s_d3d.height;
    DXGI_FORMAT want_fmt = rsx_surface_dxgi(rsx_fmt);
    /* RT_FORCE_RGBA8=1: allocate every offscreen target as R8G8B8A8 rather than
     * its native format. A title whose final composite reads a half-float
     * surface cannot be shown with a plain CopyTextureRegion, and giving it an
     * 8-bit target costs HDR precision but makes the composite a same-format
     * copy. A stopgap for looking at the frame, not a rendering fix. */
    { static int f8 = -1;
      if (f8 < 0) { const char* e = getenv("RT_FORCE_RGBA8"); f8 = e ? atoi(e) : 0; }
      if (f8) want_fmt = DXGI_FORMAT_R8G8B8A8_UNORM; }
    int slot = off_rt_find_sz(off, w, h);
    u32 dbg_evicted = 0; int dbg_why = 0;   /* CV_SMALLFRAME log: 0 new, 1 resize, 2 evict */
    if (slot >= 0 && (s_d3d.off_rt[slot].w != w || s_d3d.off_rt[slot].h != h)) {
        /* Same address at another size: give this size its own slot, unless
         * that slot was not used yet this frame (a surface that changed size
         * for good -- reuse it rather than leak one per size). */
        if (s_d3d.off_rt[slot].used) slot = -1;
    }
    if (slot >= 0) {
        OffRT* r = &s_d3d.off_rt[slot];
        if (r->w == w && r->h == h && r->dxgi == (u32)want_fmt) { r->used = 1; return slot; }
        /* The frame in flight still references this resource -- releasing it here
         * is "was deleted prior to closing the command list" (D3D12 message 921),
         * and every draw recorded against it silently lands nowhere while the
         * replacement reads back empty. Let the GPU finish first. Recreations are
         * rare (a surface changing size), so the stall costs nothing. */
        wait_for_gpu();
        dbg_why = 1;
        defer_release(r->res); r->res = NULL;   /* dims/format changed */
        defer_release(r->up);  r->up = NULL;
        defer_release(r->wb);  r->wb = NULL; r->wb_pending = 0;
    } else {
        for (int i = 0; i < MAX_OFF_RTS; i++)
            if (!s_d3d.off_rt[i].res) { slot = i; break; }
        if (slot < 0) {   /* evict an entry not used this frame */
            for (int i = 0; i < MAX_OFF_RTS; i++)
                if (!s_d3d.off_rt[i].used) { slot = i; break; }
            if (slot < 0) return -1;
            wait_for_gpu();   /* same hazard as above: the frame may still use it */
            dbg_why = 2; dbg_evicted = s_d3d.off_rt[slot].off;
            defer_release(s_d3d.off_rt[slot].res);
            s_d3d.off_rt[slot].res = NULL;
            defer_release(s_d3d.off_rt[slot].up);
            s_d3d.off_rt[slot].up = NULL;
            defer_release(s_d3d.off_rt[slot].wb);
            s_d3d.off_rt[slot].wb = NULL; s_d3d.off_rt[slot].wb_pending = 0;
        }
    }
    OffRT* r = &s_d3d.off_rt[slot];
    const float sc = rs_for(w, h);
    const u32 pw = rs_dim(w, sc), ph = rs_dim(h, sc);
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = pw; td.Height = ph; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = want_fmt; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv = {0};
    cv.Format = td.Format;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, &cv,
            &IID_ID3D12Resource, (void**)&r->res)))
        return -1;
    r->off = off; r->w = w; r->h = h; r->dxgi = (u32)want_fmt;
    r->sc = sc; r->pw = pw; r->ph = ph;
    r->st = D3D12_RESOURCE_STATE_COPY_DEST;
    r->used = 1;
    /* A surface coming back after it was evicted or resized starts black, not
     * from guest memory: the GPU drew it, and guest memory only follows the
     * small surfaces off_rt_writeback_record copies back. Code Veronica's room
     * load evicts its frame copy at 0x00A9C000; the display pass samples it
     * before the new frame is copied in, and the stale bytes there flashed on
     * screen as a rainbow of stripes and dots for one frame. */
    int zero_init = 0;
    { static u32 seen[256]; static int nseen = 0;
      int known = 0;
      for (int i = 0; i < nseen; i++) if (seen[i] == off) { known = 1; break; }
      if (!known) { if (nseen < 256) seen[nseen++] = off; }
      else if (want_fmt != DXGI_FORMAT_R8G8B8A8_UNORM || (u64)w * h > 16384u || sc != 1.0f)
          zero_init = 1; }
    { static int lg = -1; if (lg < 0) lg = getenv("CV_SMALLFRAME") ? 1 : 0;
      static int n = 0;
      if (lg && s_gameplay_seen && n++ < 400) {
          extern uint8_t* vm_base; extern u32 cellGcmResolveOffset(u32);
          u32 src = cellGcmResolveOffset(off), nz = 0;
          if (vm_base && src != 0xFFFFFFFFu)
              for (u32 i = 0; i < w * h * 4u; i += 4093) if (vm_base[src + i]) nz++;
          fprintf(stderr, "[RTNEW] t=%llu off=0x%08X %ux%u dxgi=%u slot=%d %s 0x%08X guest_nonzero=%u%s%c",
                  (unsigned long long)GetTickCount64(), off, w, h, (u32)want_fmt, slot,
                  dbg_why == 2 ? "evicted" : dbg_why == 1 ? "resized" : "new", dbg_evicted, nz,
                  zero_init ? " -> black" : "", 10);
          s_rtnew_dump = 3;
      } }

    /* Populate the new RT from guest memory: titles CPU-initialise their
     * render-target buffers (wave fills the height fields with texels of
     * (0,0,0, 1.0f) -- the .w is 'inverse of mass'; from an all-zero GPU
     * resource the water simulation can never boot). Guest data is
     * big-endian and tightly packed. */
    {
        extern uint8_t* vm_base;
        extern u32 cellGcmResolveOffset(u32);
        u32 bpp = (want_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8u :
                  (want_fmt == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16u :
                  (want_fmt == DXGI_FORMAT_R32_FLOAT) ? 4u : 4u;
        u32 pitch = (pw * bpp + 255) & ~255u;
        u32 src = cellGcmResolveOffset(off);
        if (vm_base && src != 0xFFFFFFFFu) {
            D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd = {0};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = (u64)pitch * ph; bd.Height = 1; bd.DepthOrArraySize = 1;
            bd.MipLevels = 1; bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                    &IID_ID3D12Resource, (void**)&r->up))) {
                void* mp = NULL; D3D12_RANGE nr = {0,0};
                if (SUCCEEDED(r->up->lpVtbl->Map(r->up, 0, &nr, &mp)) && mp) {
                    const u8* sp = vm_base + src;
                    if (zero_init) memset(mp, 0, (size_t)pitch * ph);
                    else if (sc != 1.0f) {   /* RES_SCALE: nearest-neighbour upscale */
                        for (u32 y = 0; y < ph; y++) {
                            const u8* srow = sp + (u64)(u32)((float)y / sc) * w * bpp;
                            u8* drow = (u8*)mp + (u64)y * pitch;
                            for (u32 x = 0; x < pw; x++) {
                                const u8* sx = srow + (u64)(u32)((float)x / sc) * bpp;
                                u8* dx = drow + (u64)x * bpp;
                                if (want_fmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
                                    dx[0] = sx[1]; dx[1] = sx[2]; dx[2] = sx[3]; dx[3] = sx[0];
                                } else if (want_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                                    for (u32 k = 0; k < bpp; k += 2) { dx[k] = sx[k+1]; dx[k+1] = sx[k]; }
                                } else {
                                    for (u32 k = 0; k < bpp; k += 4) {
                                        dx[k] = sx[k+3]; dx[k+1] = sx[k+2]; dx[k+2] = sx[k+1]; dx[k+3] = sx[k]; }
                                }
                            }
                        }
                    } else
                    for (u32 y = 0; y < h; y++) {
                        const u8* srow = sp + (u64)y * w * bpp;
                        u8* drow = (u8*)mp + (u64)y * pitch;
                        if (want_fmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
                            /* guest A8R8G8B8 (bytes A,R,G,B) -> R,G,B,A */
                            for (u32 x = 0; x < w; x++) {
                                drow[x*4+0] = srow[x*4+1];
                                drow[x*4+1] = srow[x*4+2];
                                drow[x*4+2] = srow[x*4+3];
                                drow[x*4+3] = srow[x*4+0];
                            }
                        } else if (want_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                            for (u32 x = 0; x < w * 4; x++) {   /* u16 halves, BE */
                                drow[x*2+0] = srow[x*2+1];
                                drow[x*2+1] = srow[x*2+0];
                            }
                        } else {                                 /* u32 floats, BE */
                            u32 nw = (w * bpp) / 4;
                            for (u32 x = 0; x < nw; x++) {
                                drow[x*4+0] = srow[x*4+3];
                                drow[x*4+1] = srow[x*4+2];
                                drow[x*4+2] = srow[x*4+1];
                                drow[x*4+3] = srow[x*4+0];
                            }
                        }
                    }
                    r->up->lpVtbl->Unmap(r->up, 0, NULL);
                    D3D12_TEXTURE_COPY_LOCATION cdst = {0}, csrc = {0};
                    cdst.pResource = r->res;
                    cdst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    csrc.pResource = r->up;
                    csrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    csrc.PlacedFootprint.Footprint.Format   = want_fmt;
                    csrc.PlacedFootprint.Footprint.Width    = pw;
                    csrc.PlacedFootprint.Footprint.Height   = ph;
                    csrc.PlacedFootprint.Footprint.Depth    = 1;
                    csrc.PlacedFootprint.Footprint.RowPitch = pitch;
                    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &cdst, 0, 0, 0, &csrc, NULL);
                }
            }
        }
        {
            D3D12_RESOURCE_BARRIER b = {0};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = r->res;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
            r->st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rh;
    s_d3d.rt_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rt_rtv_heap, &rh);
    rh.ptr += (u64)slot * s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, r->res, NULL, rh);

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = td.Format;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sh;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
    sh.ptr += (u64)(RT_SRV_BASE + slot) * s_d3d.srv_inc;
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, r->res, &sv, sh);

    static int _log = 0;
    if (_log++ < 8)
        printf("[D3D12] offscreen RT %d: off=0x%X %ux%u (render-to-texture)\n",
               slot, off, w, h);
    return slot;
}

static D3D12_CPU_DESCRIPTOR_HANDLE off_rt_rtv(int slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rh;
    s_d3d.rt_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rt_rtv_heap, &rh);
    rh.ptr += (u64)slot * s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return rh;
}

/* Write a texture SRV (or a null SRV when res == NULL) at an absolute SRV
 * heap slot. Used to fill per-draw t0-t3 descriptor windows. */
static void srv_write_ex(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt,
                         UINT mapping, int cube)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = mapping;
    sv.Texture2D.MipLevels = (UINT)-1;   /* every level: a texture mod's mip chain (others have one) */
    if (cube && rsx_cube_as_array()) {   /* matches the Texture2DArray the HLSL declares */
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Texture2DArray.MostDetailedMip = 0;
        sv.Texture2DArray.MipLevels = 1;
        sv.Texture2DArray.FirstArraySlice = 0;
        sv.Texture2DArray.ArraySize = 6;
        sv.Texture2DArray.PlaneSlice = 0;
        sv.Texture2DArray.ResourceMinLODClamp = 0.0f;
    } else if (cube) {   /* must match the TextureCube declaration in the HLSL */
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = 1;
        sv.TextureCube.MostDetailedMip = 0;
        sv.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &h);
    h.ptr += (u64)heap_slot * s_d3d.srv_inc;
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, res, &sv, h);
}

static void srv_write(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping)
{
    srv_write_ex(heap_slot, res, fmt, mapping, 0);
}

/* A draw's texture window, collected first and written only when it differs
 * from the previous draw's: consecutive draws mostly bind the same textures
 * (the lighter's six shadow faces redraw the room with identical ones), and
 * writing all eight views per draw was ~9 ms a frame, spiking past 40 ms.
 * A draw that may sample its own target keeps a window of its own -- the
 * feedback copy rewrites it at bind time. SRV_NOREUSE=1 writes every draw. */
typedef struct { ID3D12Resource* res; u32 fmt, map, cube, pad; } SrvReq;
static u32 s_srv_win[MAX_DRAWS];
static SrvReq s_srv_last[FP_TEX_UNITS];
static int s_srv_last_win = -1;
static u64 s_srv_reused = 0, s_srv_written = 0;
static void srv_req_ex(SrvReq* r, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping, int cube)
{
    r->res = res; r->fmt = (u32)fmt; r->map = mapping; r->cube = (u32)cube; r->pad = 0;
}
static void srv_req(SrvReq* r, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping)
{
    srv_req_ex(r, res, fmt, mapping, 0);
}
static void srv_commit(u32 d, const SrvReq* rq, int own_window)
{
    static int off = -1;
    if (off < 0) off = getenv("SRV_NOREUSE") ? 1 : 0;
    if (s_srv_cache_stale) { s_srv_last_win = -1; s_srv_cache_stale = 0; }
    if (!off && !own_window && s_srv_last_win >= 0 &&
        memcmp(rq, s_srv_last, sizeof s_srv_last) == 0) {
        s_srv_win[d] = (u32)s_srv_last_win;
        s_srv_reused++;
        return;
    }
    for (int u = 0; u < FP_TEX_UNITS; u++)
        srv_write_ex(DRAW_SRV_BASE + d * FP_TEX_UNITS + (u32)u, rq[u].res,
                     (DXGI_FORMAT)rq[u].fmt, rq[u].map, (int)rq[u].cube);
    s_srv_win[d] = d;
    s_srv_written++;
    if (own_window) { s_srv_last_win = -1; return; }
    memcpy(s_srv_last, rq, sizeof s_srv_last);
    s_srv_last_win = (int)d;
}

static void off_rt_transition(int slot, D3D12_RESOURCE_STATES to)
{
    OffRT* r = &s_d3d.off_rt[slot];
    if (r->st == to) return;
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = r->res;
    b.Transition.StateBefore = r->st;
    b.Transition.StateAfter  = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    r->st = to;
}

/* A draw that samples the target it renders to. RSX allows it (each pixel
 * reads its own old value); D3D12 does not -- the SRV of a resource in
 * RENDER_TARGET state reads garbage, usually zero. Code Veronica grades the
 * whole gameplay scene that way (it reads and rewrites 0x01528000), which
 * turned every frame black. Copy the target aside first and sample the copy. */
static ID3D12Resource* s_fb_tex = NULL;
static u32 s_fb_w = 0, s_fb_h = 0, s_fb_fmt = 0;
static D3D12_RESOURCE_STATES s_fb_st = D3D12_RESOURCE_STATE_COPY_DEST;

static ID3D12Resource* off_rt_feedback_copy(int slot)
{
    OffRT* r = &s_d3d.off_rt[slot];
    if (!r->res) return NULL;
    if (!s_fb_tex || s_fb_w != r->pw || s_fb_h != r->ph || s_fb_fmt != r->dxgi) {
        if (s_fb_tex) { defer_release(s_fb_tex); s_fb_tex = NULL; }
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = r->pw; td.Height = r->ph; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = (DXGI_FORMAT)r->dxgi; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                &IID_ID3D12Resource, (void**)&s_fb_tex)))
            return NULL;
        s_fb_w = r->pw; s_fb_h = r->ph; s_fb_fmt = r->dxgi;
        s_fb_st = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    D3D12_RESOURCE_STATES back = r->st;
    off_rt_transition(slot, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (s_fb_st != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = s_fb_tex;
        b.Transition.StateBefore = s_fb_st;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }
    s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list, s_fb_tex, r->res);
    {
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = s_fb_tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
        s_fb_st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    off_rt_transition(slot, back);
    return s_fb_tex;
}

/* Copy target `slot` aside as the state after op `op` - 1 (rsxcap steps). */
static void cap_step_snapshot(int slot, u32 op)
{
    if (!s_cap_steps_on || s_cap_nsteps >= CAP_MAX_STEPS) return;
    OffRT* r = &s_d3d.off_rt[slot];
    if (!r->res || r->w < 64) return;
    CapStep* c = &s_cap_steps[s_cap_nsteps];
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = r->pw; td.Height = r->ph; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = (DXGI_FORMAT)r->dxgi; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&c->res)))
        return;
    D3D12_RESOURCE_STATES back = r->st;
    off_rt_transition(slot, D3D12_RESOURCE_STATE_COPY_SOURCE);
    s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list, c->res, r->res);
    off_rt_transition(slot, back);
    c->op = op; c->off = r->off; c->w = r->pw; c->h = r->ph; c->dxgi = r->dxgi;
    s_cap_nsteps++;
}

/* Small offscreen targets written back to guest memory. RSX renders into
 * memory the CPU can read; this backend keeps targets on the GPU. Code
 * Veronica measures scene brightness by reducing it to a 40x22 target that the
 * CPU reads to pick the exposure curve -- reading zeros, it never brightened
 * the dark rooms and gameplay stayed black. Copies are recorded at the end of
 * the frame and written to guest memory once the GPU is idle (next frame). */
static void off_rt_writeback_record(void)
{
    { static int off = -1; if (off < 0) off = getenv("RT_NOWRITEBACK") ? 1 : 0; if (off) return; }
    for (int i = 0; i < MAX_OFF_RTS; i++) {
        OffRT* r = &s_d3d.off_rt[i];
        r->wb_pending = 0;
        if (!r->res || !r->used || r->dxgi != (u32)DXGI_FORMAT_R8G8B8A8_UNORM) continue;
        if ((u64)r->w * r->h > 16384u || r->sc != 1.0f) continue;
        u32 pitch = (r->w * 4 + 255) & ~255u;
        if (!r->wb) {
            D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd = {0};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = (u64)pitch * r->h; rd.Height = 1; rd.DepthOrArraySize = 1;
            rd.MipLevels = 1; rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&r->wb)))
                { r->wb = NULL; continue; }
        }
        D3D12_RESOURCE_STATES back = r->st;
        off_rt_transition(i, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        dst.pResource = r->wb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = r->w; dst.PlacedFootprint.Footprint.Height = r->h;
        dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = pitch;
        src.pResource = r->res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
        off_rt_transition(i, back);
        r->wb_pending = 1;
    }
}

static void off_rt_writeback_flush(void)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);
    for (int i = 0; i < MAX_OFF_RTS; i++) {
        OffRT* r = &s_d3d.off_rt[i];
        if (!r->wb_pending || !r->wb || !vm_base) continue;
        r->wb_pending = 0;
        u32 ea = cellGcmResolveOffset(r->off);
        if (ea == 0xFFFFFFFFu) continue;
        u32 pitch = (r->w * 4 + 255) & ~255u;
        u32 gp = r->gpitch ? r->gpitch : r->w * 4;
        if (gp < r->w * 4) continue;
        u8* mp = NULL; D3D12_RANGE rr = {0, (SIZE_T)pitch * r->h};
        if (FAILED(r->wb->lpVtbl->Map(r->wb, 0, &rr, (void**)&mp)) || !mp) continue;
        for (u32 y = 0; y < r->h; y++) {
            const u8* s = mp + (u64)y * pitch;
            u8* d = vm_base + ea + (u64)y * gp;
            for (u32 x = 0; x < r->w; x++) {   /* R,G,B,A -> guest A,R,G,B */
                d[x*4+0] = s[x*4+3]; d[x*4+1] = s[x*4+0];
                d[x*4+2] = s[x*4+1]; d[x*4+3] = s[x*4+2];
            }
        }
        D3D12_RANGE wr = {0, 0};
        r->wb->lpVtbl->Unmap(r->wb, 0, &wr);
        { static int n = 0; if (n++ < 8)
            fprintf(stderr, "[RTWB] wrote back 0x%08X %ux%u pitch=%u%c", r->off, r->w, r->h, gp, 10); }
    }
}

/* GPU copies of snapshotted vertex textures, keyed by content. A room piece's
 * matrix texture is the same frame to frame, so this mostly hits. */
typedef struct { u32 hash, fmt, w, h; ID3D12Resource* tex; u64 last_frame; } VTexEntry;
#define VTEX_CACHE 256
static VTexEntry s_vtex_cache[VTEX_CACHE];
/* Render frames, counted on the render thread (s_d3d.frame_count is reset
 * every second for the FPS readout, so it cannot age cache entries). */
static u64 s_vtex_frame = 1;
typedef struct { ID3D12Resource* res[4]; DXGI_FORMAT format[4]; } VTexWindowKey;
typedef struct { VTexWindowKey key; u64 frame; u32 draw; } VTexWindow;
static VTexWindow s_vtex_windows[1024];
static u32 s_vtex_win[MAX_DRAWS];
/* One upload buffer for every vertex-texture copy, reused each frame: the
 * render frame starts with the GPU idle, so last frame's copies are done.
 * Creating a 1 MB texture plus a 1 MB upload buffer per changed matrix
 * snapshot cost several ms each (Code Veronica's lighter scene: ~130 draws
 * over 3 ms in one run). A changed snapshot now overwrites the oldest cached
 * texture of the same shape that this frame does not use. VTEX_NOPOOL=1
 * restores create-per-copy. */
static ID3D12Resource* s_vtex_up = NULL;
static u8*  s_vtex_up_map = NULL;
static u64  s_vtex_up_cap = 0, s_vtex_up_used = 0, s_vtex_up_frame = 0;

static ID3D12Resource* vtex_get(const D3D12DrawRecord* dr, u32 u)
{
    DXGI_FORMAT dxgi; u32 bpt, comp;
    if (!vtex_format(dr->vtx[u].fmt, &dxgi, &bpt, &comp)) return NULL;
    const u8* data = s_rb.vtex_arena + dr->vtx[u].data_off;
    const u32 w = dr->vtx[u].w, h = dr->vtx[u].h, row = dr->vtx[u].row_bytes;
    const u32 hash = dr->vtx[u].hash;
    static int nopool = -1;
    if (nopool < 0) nopool = getenv("VTEX_NOPOOL") ? 1 : 0;
    const u64 now = s_vtex_frame;
    int free_i = -1, old_i = -1, same_i = -1;
    for (int i = 0; i < VTEX_CACHE; i++) {
        VTexEntry* e = &s_vtex_cache[i];
        if (!e->tex) { if (free_i < 0) free_i = i; continue; }
        if (e->hash == hash && e->fmt == dr->vtx[u].fmt && e->w == w && e->h == h) {
            e->last_frame = now;
            return e->tex;
        }
        if (e->last_frame == now) continue;          /* bound this frame: keep */
        if (old_i < 0 || e->last_frame < s_vtex_cache[old_i].last_frame) old_i = i;
        /* oldest entry of the same shape: its texture takes the new contents */
        if (!nopool && e->fmt == dr->vtx[u].fmt && e->w == w && e->h == h &&
            (same_i < 0 || e->last_frame < s_vtex_cache[same_i].last_frame)) same_i = i;
    }
    int slot = same_i >= 0 ? same_i : free_i >= 0 ? free_i : old_i;
    if (slot < 0) return NULL;                       /* every entry bound this frame */
    VTexEntry* e = &s_vtex_cache[slot];
    ID3D12Resource* tex = NULL;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = dxgi; td.SampleDesc.Count = 1;
    if (same_i >= 0) {
        tex = e->tex;
        D3D12_RESOURCE_BARRIER b0 = {0};
        b0.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b0.Transition.pResource = tex;
        b0.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b0.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b0.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b0);
    } else {
        if (e->tex) { defer_release(e->tex); e->tex = NULL; }
        D3D12_HEAP_PROPERTIES hp0 = {0}; hp0.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp0,
                D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                &IID_ID3D12Resource, (void**)&tex))) return NULL;
    }
    D3D12_HEAP_PROPERTIES hp = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 total = 0;
    s_d3d.device->lpVtbl->GetCopyableFootprints(s_d3d.device, &td, 0, 1, 0, &fp, NULL, NULL, &total);
    ID3D12Resource* up = NULL;
    u8* m = NULL; D3D12_RANGE nr = {0, 0};
    if (!nopool) {
        if (s_vtex_up_frame != now) { s_vtex_up_frame = now; s_vtex_up_used = 0; }
        u64 at = (s_vtex_up_used + 511u) & ~(u64)511u;   /* placement alignment */
        if (!s_vtex_up || at + total > s_vtex_up_cap) {
            u64 cap = s_vtex_up_cap ? s_vtex_up_cap * 2 : ((u64)16u << 20);
            while (cap < total) cap *= 2;
            if (s_vtex_up) { defer_release(s_vtex_up); s_vtex_up = NULL; s_vtex_up_map = NULL; }
            hp.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd = {0};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = cap; bd.Height = 1;
            bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                    D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                    &IID_ID3D12Resource, (void**)&s_vtex_up)) ||
                FAILED(s_vtex_up->lpVtbl->Map(s_vtex_up, 0, &nr, (void**)&s_vtex_up_map))) {
                if (s_vtex_up) { s_vtex_up->lpVtbl->Release(s_vtex_up); s_vtex_up = NULL; }
                s_vtex_up_cap = 0;
                if (same_i < 0) tex->lpVtbl->Release(tex);
                else e->tex = NULL, tex->lpVtbl->Release(tex);
                return NULL;
            }
            s_vtex_up_cap = cap; at = 0;
        }
        fp.Offset = at;
        s_vtex_up_used = at + total;
        m = s_vtex_up_map;
        up = s_vtex_up;
    } else {
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&up))) { tex->lpVtbl->Release(tex); return NULL; }
        if (FAILED(up->lpVtbl->Map(up, 0, &nr, (void**)&m))) {
            up->lpVtbl->Release(up); tex->lpVtbl->Release(tex); return NULL;
        }
    }
    /* Guest components are big-endian; swap each into place. */
    for (u32 y = 0; y < h; y++) {
        u8* drow = m + fp.Offset + (u64)y * fp.Footprint.RowPitch;
        const u8* srow = data + (size_t)y * row;
        if (comp == 4) for (u32 x = 0; x < row; x += 4) {
            u32 v; memcpy(&v, srow + x, 4); v = _byteswap_ulong(v); memcpy(drow + x, &v, 4); }
        else for (u32 x = 0; x < row; x += 2) { drow[x] = srow[x + 1]; drow[x + 1] = srow[x]; }
    }
    if (nopool) up->lpVtbl->Unmap(up, 0, NULL);
    D3D12_TEXTURE_COPY_LOCATION dl = {0}, sl = {0};
    dl.pResource = tex; dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sl.pResource = up;  sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; sl.PlacedFootprint = fp;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dl, 0, 0, 0, &sl, NULL);
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    if (nopool) defer_release(up);   /* the copy above reads it; freed once the GPU is past */
    e->hash = hash; e->fmt = dr->vtx[u].fmt; e->w = w; e->h = h;
    e->tex = tex; e->last_frame = now;
    return tex;
}

/* Fill draw d's t16-t19 window. Units without a texture get a null SRV of a
 * float format, which the TXL reads as zero, as before. */
static void vtex_fill_window(u32 d, const D3D12DrawRecord* dr)
{
    if(!prep_reuse_on()) {
        s_vtex_win[d]=d;s_prep_vtex_written++;
        for(u32 u=0;u<4;u++) {
            ID3D12Resource* tex=((dr->vtex_mask>>u)&1u)?vtex_get(dr,u):NULL;
            DXGI_FORMAT dxgi=DXGI_FORMAT_R32G32B32A32_FLOAT;u32 bpt,comp;
            if(tex)vtex_format(dr->vtx[u].fmt,&dxgi,&bpt,&comp);
            srv_write(VTEX_SRV_BASE+d*4+u,tex,dxgi,D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        }
        return;
    }
    VTexWindowKey key={0};u32 hash=2166136261u;
    for(u32 u=0;u<4;u++) {
        key.res[u]=((dr->vtex_mask>>u)&1u)?vtex_get(dr,u):NULL;
        key.format[u]=DXGI_FORMAT_R32G32B32A32_FLOAT;u32 bpt,comp;
        if(key.res[u])vtex_format(dr->vtx[u].fmt,&key.format[u],&bpt,&comp);
        uintptr_t address=(uintptr_t)key.res[u];
        hash=(hash^(u32)(address>>4)^(u32)(address>>32)^(u32)key.format[u])*16777619u;
    }
    hash^=hash>>16;hash*=0x85EBCA6Bu;hash^=hash>>13;
    VTexWindow* window=&s_vtex_windows[hash&1023u];
    if(prep_reuse_on()&&window->frame==s_vtex_frame&&!memcmp(&window->key,&key,sizeof key)) {
        s_vtex_win[d]=window->draw;s_prep_vtex_reused++;return;
    }
    for(u32 u=0;u<4;u++)srv_write(VTEX_SRV_BASE+d*4+u,key.res[u],key.format[u],D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    s_vtex_win[d]=d;s_prep_vtex_written++;
    if(prep_reuse_on()){window->key=key;window->frame=s_vtex_frame;window->draw=d;}
}

/* ---------------------------------------------------------------------------
 * Zeta as texture. A title can bind its depth surface's offset as an A8R8G8B8
 * texture and rebuild depth from the bytes (Code Veronica's fog and depth of
 * field: floor(c*255+.5) . (1/256, 1/65536, 1/2^24) after a remap that puts the
 * high byte in R). Our depth lives only in the shared D24S8 buffer, which is
 * cleared on every target switch, and the guest memory there was never
 * written -- those passes read garbage and washed the screen out.
 *
 * zconv_run copies the depth, just after the last draw that used it and before
 * the switch that clears it, into an RGBA8 surface registered at the zeta
 * offset. Guest bytes d[23:16] d[15:8] d[7:0] stencil represent A,R,G,B;
 * store their logical RGBA order, as the colour texture decoder does, then
 * apply the draw's remap. The common 0xAA93 remap restores high,mid,low in RGB
 * for a dot with (1/256,1/65536,1/16777216).
 * -----------------------------------------------------------------------*/
static ID3D12PipelineState* s_zconv_pso = NULL;
static int  s_zconv_failed = 0;
static u32  s_zview_off = 0;      /* zeta offset sampled as a texture this frame */
static int  s_zview_slot = -1;    /* off_rt slot holding the converted bytes */
static u32  s_zconv_at = 0xFFFFFFFFu;  /* op index to run the conversion before */
/* Later reconversions: the depth changes between two reads. Code Veronica's
 * flooded room reads it for the water's depth copy, then draws the water
 * (writing depth), then fogs the scene from it; converted once, the fog saw
 * no water and washed it out, brightest near the camera. */
enum { ZCONV_MAX_AT = 8 };
static u32  s_zconv_more[ZCONV_MAX_AT];
static int  s_zconv_nmore = 0;

static int zconv_init(void)
{
    if (s_zconv_pso) return 1;
    if (s_zconv_failed || !s_d3d.vp_root_sig) return 0;
    static const char hlsl[] =
        "struct V { float4 p : SV_Position; };\n"
        "V vs(uint id : SV_VertexID) {\n"
        "    V o; float2 t = float2((id << 1) & 2, id & 2);\n"
        "    o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
        "Texture2D<float> z : register(t0);\n"
        "float4 ps(V i) : SV_Target {\n"
        "    uint d = (uint)round(saturate(z.Load(int3(i.p.xy, 0))) * 16777215.0);\n"
        /* The view's remap uses logical {A,R,G,B}, as uploaded colour
         * textures do. D24S8 bytes {high,mid,low,stencil} therefore have to
         * become RGBA {mid,low,stencil,high}. Putting high in R and zero in A
         * made the guest's 0xAA93 depth swizzle read {0,high,mid}: depth/256,
         * so Code Veronica's water always calculated zero thickness/alpha. */
        "    return float4((d >> 8) & 255, d & 255, 0, (d >> 16) & 255) / 255.0; }\n";
    ID3DBlob *vb = NULL, *pb = NULL, *e = NULL;
    HRESULT hr = D3DCompile(hlsl, sizeof hlsl - 1, "zconv", NULL, NULL, "vs", "vs_5_0", 0, 0, &vb, &e);
    if (e) { e->lpVtbl->Release(e); e = NULL; }
    if (SUCCEEDED(hr)) hr = D3DCompile(hlsl, sizeof hlsl - 1, "zconv", NULL, NULL, "ps", "ps_5_0", 0, 0, &pb, &e);
    if (e) { printf("[ZCONV] compile: %s\n", (const char*)e->lpVtbl->GetBufferPointer(e)); e->lpVtbl->Release(e); }
    if (FAILED(hr) || !vb || !pb) {
        if (vb) vb->lpVtbl->Release(vb);
        if (pb) pb->lpVtbl->Release(pb);
        s_zconv_failed = 1; return 0;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = s_d3d.vp_root_sig;
    pd.VS.pShaderBytecode = vb->lpVtbl->GetBufferPointer(vb);
    pd.VS.BytecodeLength  = vb->lpVtbl->GetBufferSize(vb);
    pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb);
    pd.PS.BytecodeLength  = pb->lpVtbl->GetBufferSize(pb);
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&s_zconv_pso);
    vb->lpVtbl->Release(vb); pb->lpVtbl->Release(pb);
    if (FAILED(hr)) { printf("[ZCONV] PSO FAIL (0x%08lX)\n", hr); s_zconv_failed = 1; return 0; }
    /* Written here and again whenever the depth buffer is recreated (zconv_run). */
    srv_write(ZCONV_SRV, s_d3d.depth_buffer, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
              D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    for (u32 i = 1; i < FP_TEX_UNITS; i++)
        srv_write(ZCONV_SRV + i, NULL, DXGI_FORMAT_R8G8B8A8_UNORM,
                  D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    printf("[ZCONV] depth-as-texture conversion ready\n");
    return 1;
}

/* Find the first op that samples a zeta surface written earlier in the frame,
 * register an RGBA8 surface at that offset, and note where to convert: right
 * after the last op that used the depth, before anything can clear it. */
static void zconv_plan(void)
{
    s_zview_off = 0; s_zview_slot = -1; s_zconv_at = 0xFFFFFFFFu; s_zconv_nmore = 0;
    if (getenv("ZCONV_OFF")) return;
    static int once = -1;   /* ZCONV_ONCE=1: the old single conversion */
    if (once < 0) once = getenv("ZCONV_ONCE") ? 1 : 0;
    int zdirty = 0, zlast = -1;
    /* One pass: the zeta surfaces drawn with so far and the last op that used
     * each. Rescanning every earlier op for every bound texture was
     * O(draws^2 x units) -- ~300M compares a frame in Code Veronica's lighter
     * scene (6500 draws), most of the render thread's time. */
    enum { ZSEEN_MAX = 16 };
    u32 zs_off[ZSEEN_MAX]; int zs_last[ZSEEN_MAX]; int nzs = 0;
    for (u32 d = 0; d < s_rb.count && d < MAX_DRAWS; d++) {
        const D3D12DrawRecord* dr = &s_rb.draws[d];
        if (!dr->is_vp || dr->is_clear || dr->is_copy) continue;
        if (s_zview_off) {   /* after the first conversion: reconvert when the depth changed */
            if (once) return;
            int reads = 0;
            for (int u = 0; u < 4; u++)
                if (dr->tex[u].set && dr->tex[u].raw == s_zview_off) reads = 1;
            if (reads && zdirty && zlast >= 0 && s_zconv_nmore < ZCONV_MAX_AT) {
                s_zconv_more[s_zconv_nmore++] = (u32)zlast + 1;
                zdirty = 0;
                { static int n = 0; if (n++ < 3)
                    fprintf(stderr, "[ZCONV] zeta 0x%08X changed before op%u: converting again before op%u%c",
                            s_zview_off, d, (u32)zlast + 1, 10); }
            }
            if (dr->zeta_off == s_zview_off && !reads) { zdirty = 1; zlast = (int)d; }
            continue;
        }
        for (int u = 0; u < 4 && !s_zview_off && nzs; u++) {
            if (!dr->tex[u].set || !dr->tex[u].raw) continue;
            u32 z = dr->tex[u].raw;
            int last = -1;
            for (int k = 0; k < nzs; k++) if (zs_off[k] == z) { last = zs_last[k]; break; }
            if (last < 0) continue;
            int slot = off_rt_get(z, dr->tex[u].w, dr->tex[u].h, 8);
            if (slot < 0 || !zconv_init()) return;
            s_zview_off = z; s_zview_slot = slot; s_zconv_at = (u32)last + 1;
            { static int n = 0; if (n++ < 3)
                fprintf(stderr, "[ZCONV] zeta 0x%08X sampled at op%u (%ux%u); converting before op%u%c",
                        z, d, dr->tex[u].w, dr->tex[u].h, s_zconv_at, 10); }
        }
        if (s_zview_off) continue;
        if (dr->zeta_off) {   /* this op now counts as a use for later ones */
            int k = 0;
            while (k < nzs && zs_off[k] != dr->zeta_off) k++;
            if (k == nzs && nzs < ZSEEN_MAX) zs_off[nzs++] = dr->zeta_off;
            if (k < nzs) zs_last[k] = (int)d;
        }
    }
}

static void zconv_run(D3D12_GPU_DESCRIPTOR_HANDLE gh_base)
{
    if (s_zview_slot < 0 || !s_zconv_pso) return;
    { static u32 seen;   /* the depth buffer was recreated: refresh its view */
      if (seen != s_depth_gen) {
          srv_write(ZCONV_SRV, s_d3d.depth_buffer, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
          seen = s_depth_gen;
      } }
    OffRT* r = &s_d3d.off_rt[s_zview_slot];
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = s_d3d.depth_buffer;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    off_rt_transition(s_zview_slot, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rh = off_rt_rtv(s_zview_slot);
    s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, 1, &rh, FALSE, NULL);
    D3D12_VIEWPORT vp = {0, 0, (float)r->w, (float)r->h, 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, (LONG)r->w, (LONG)r->h};
    rs_set_vp(r->sc, &vp, &sc);
    s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, s_zconv_pso);
    D3D12_GPU_DESCRIPTOR_HANDLE gh = gh_base;
    gh.ptr += (u64)ZCONV_SRV * s_d3d.srv_inc;
    s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
    s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, 3, 1, 0, 0);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
}

/* Set by the present entry point: 1 = this batch ends with a swapchain
 * Present, 0 = execute the recorded draws only. An OFFSCREEN-only batch (a
 * render-to-texture pass) must still run -- it is what fills the texture a
 * later batch samples -- but presenting it would show a half-built frame. */
static int s_present_this_frame = 1;   /* recording side; frozen into s_rb.present */

/* Resolve a DRAW_*_TEX value: a hex raw offset, or the literal "duck" for the
 * content-identified duck texture. */
static u32 draw_filter_tex(const char* e)
{
    if (!e) return 0;
    if (e[0] == 'd' && e[1] == 'u') return s_duck_raw;
    return (u32)strtoul(e, NULL, 16);
}

/* Copy the current backbuffer into s_screen_copy. Called mid-frame the moment
 * the reduced-viewport passes finish -- this title renders its reflection into
 * the TOP-LEFT REGION of the render target and the main scene then overwrites
 * it, so a snapshot taken at end of frame contains the scene, not the
 * reflection. Also called at end of frame as a fallback for frames with no
 * reduced-viewport pass. */
static void screen_copy_capture(u32 fi)
{
    static int en = -1;
    if (en < 0) { const char* e = getenv("SCREEN_AS_TEX"); en = e ? atoi(e) : 0; }
    if (!en || !s_d3d.device) return;
            if (!s_screen_copy) {
                D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC td = {0};
                td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                td.Width = rs_dim(s_d3d.width, s_rs); td.Height = rs_dim(s_d3d.height, s_rs);
                td.DepthOrArraySize = 1; td.MipLevels = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
                td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                        s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL,
                        &IID_ID3D12Resource, (void**)&s_screen_copy)))
                    s_screen_copy = NULL;
            }
            if (s_screen_copy) {
                D3D12_RESOURCE_BARRIER bb[2] = {0};
                bb[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bb[0].Transition.pResource   = s_d3d.render_targets[fi];
                bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
                bb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                bb[1] = bb[0];
                bb[1].Transition.pResource   = s_screen_copy;
                bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);

                s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list,
                    s_screen_copy, s_d3d.render_targets[fi]);

                bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);

                /* SCREENCOPY_DUMP=1: stage the captured image into the readback
                 * buffer so it can be written out and LOOKED AT. Claiming a
                 * capture happened is not the same as showing what is in it. */
                { static int sd = -1;
                  if (sd < 0) { const char* e = getenv("SCREENCOPY_DUMP"); sd = e ? atoi(e) : 0; }
                  if (sd && s_d3d.readback_buf) {
                      D3D12_RESOURCE_BARRIER t = {0};
                      t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                      t.Transition.pResource   = s_screen_copy;
                      t.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                      t.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
                      t.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                      s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &t);

                      D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
                      cd.pResource = s_d3d.readback_buf;
                      cd.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                      cd.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
                      cd.PlacedFootprint.Footprint.Width    = rs_dim(s_d3d.width, s_rs);
                      cd.PlacedFootprint.Footprint.Height   = rs_dim(s_d3d.height, s_rs);
                      cd.PlacedFootprint.Footprint.Depth    = 1;
                      cd.PlacedFootprint.Footprint.RowPitch = s_d3d.readback_pitch;
                      cs.pResource = s_screen_copy;
                      cs.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                      s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &cd, 0, 0, 0, &cs, NULL);

                      t.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                      t.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                      s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &t);
                      s_sc_dump_pending = 1;
                  } }
            }

}

/* rsx_topology -> D3D_PRIMITIVE_TOPOLOGY. Spelled out rather than relying on
 * the two enums happening to agree: the draw record carries the neutral value
 * so a second backend can read it, and this is the one place it becomes a D3D
 * one. */
static D3D12_PRIMITIVE_TOPOLOGY topo_to_d3d(u32 t)
{
    switch (t) {
    case RSX_TOPOLOGY_POINTS:         return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RSX_TOPOLOGY_LINES:          return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case RSX_TOPOLOGY_LINE_STRIP:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RSX_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case RSX_TOPOLOGY_TRIANGLES:      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    default:                          return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }
}

/* GCM_GUEST_FB=<off>,<w>,<h>,<pitch> -- present the guest display buffer.
 *
 * A title that composites its final image with the RSX 2D engine assembles it
 * in GUEST memory: nv3089_blit is a CPU copy and never touches D3D12, and the
 * draw pass below only runs when a recorded draw targets the display surface,
 * which for such a title never happens. The frame is finished, in the wrong
 * memory, and the swapchain shows only the clear. Copy it across.
 *
 * Off unless the offset and geometry are given, so nothing else changes. */
/* GPU-side composite of an offscreen RT onto the backbuffer.
 *
 * A title that finishes its frame with the RSX 2D engine blits a rendered
 * surface into the display buffer. That surface lives in guest RAM, which the
 * D3D12 path never writes -- the scene is on the GPU -- so the CPU copy in
 * nv3089_blit faithfully moves zeros. The backend already holds the same
 * surface as a texture, so do the composite here instead. Triggered from
 * cellGcmSys under GCM_COMPOSITE_RT. */
static u32 s_composite_src = 0;
static int s_last_drawn_slot = -1;
static u32 s_last_drawn_off  = 0;

void rsx_d3d12_composite_from_offscreen(u32 raw_off) { s_composite_src = raw_off; }

/* ---------------------------------------------------------------------------
 * Post-process anti-aliasing on the finished frame, just before Present.
 * AA_MODE / rsx_d3d12_set_aa: 0 off, 1 FXAA 3.11 (quality preset), 2 SMAA 1x
 * (Ultra preset; third_party/smaa, MIT). Runs at the render resolution, on the
 * GPU only, and can change between frames. The backbuffer is copied aside and
 * the passes write the result back into it.
 * -------------------------------------------------------------------------*/
#include "AreaTex.h"
#include "SearchTex.h"
#include "smaa_hlsl.h"

static volatile int s_aa_mode = -1;          /* -1: read AA_MODE once */
void rsx_d3d12_set_aa(int mode) { s_aa_mode = (mode >= 0 && mode <= 2) ? mode : 0; }
static volatile int s_color_brightness;
static volatile int s_color_contrast = 100;
static volatile int s_color_saturation = 100;
static volatile int s_color_env_loaded;
void rsx_d3d12_set_color_adjust(int brightness, int contrast)
{
    if (brightness < -80) brightness = -80;
    if (brightness > 80) brightness = 80;
    if (contrast < 50) contrast = 50;
    if (contrast > 180) contrast = 180;
    s_color_brightness = brightness;
    s_color_contrast = contrast;
    s_color_env_loaded = 1;
}
void rsx_d3d12_set_saturation(int saturation)
{
    if (saturation < 0) saturation = 0;
    if (saturation > 200) saturation = 200;
    s_color_saturation = saturation;
    s_color_env_loaded = 1;
}
static void color_adjust_load_env(void)
{
    if (s_color_env_loaded) return;
    const char* b = getenv("CV_BRIGHTNESS");
    const char* c = getenv("CV_CONTRAST");
    const char* s = getenv("CV_SATURATION");
    if (b) s_color_brightness = atoi(b);
    if (c) s_color_contrast = atoi(c);
    if (s_color_brightness < -80) s_color_brightness = -80;
    if (s_color_brightness > 80) s_color_brightness = 80;
    if (s_color_contrast < 50) s_color_contrast = 50;
    if (s_color_contrast > 180) s_color_contrast = 180;
    if (s) s_color_saturation = atoi(s);
    if (s_color_saturation < 0) s_color_saturation = 0;
    if (s_color_saturation > 200) s_color_saturation = 200;
    s_color_env_loaded = 1;
}

static ID3D12RootSignature* s_aa_rs;
static ID3D12PipelineState *s_aa_fxaa, *s_aa_edge, *s_aa_weight, *s_aa_blend, *s_aa_show, *s_aa_adjust;
static int s_aa_failed[3];
static ID3D12DescriptorHeap* s_aa_rtv;       /* 0 = edges, 1 = blend weights */
static ID3D12Resource *s_aa_color, *s_aa_edges, *s_aa_weights;
static ID3D12Resource *s_aa_area, *s_aa_search, *s_aa_up;
static u32 s_aa_w, s_aa_h;
static int s_aa_luts_ready;

static const char k_aa_common[] =
    "cbuffer C : register(b0) { float4 rt; float4 colorAdjust; float4 colorExtra; };\n" /* rt, brightness, contrast, saturation */
    "SamplerState LinearSampler : register(s0);\n"
    "SamplerState PointSampler : register(s1);\n"
    "Texture2D colorTex  : register(t0);\n"
    "Texture2D edgesTex  : register(t1);\n"
    "Texture2D areaTex   : register(t2);\n"
    "Texture2D searchTex : register(t3);\n"
    "Texture2D blendTex  : register(t4);\n"
    "float3 adjust_color(float3 c) { c=saturate((c-0.5)*colorAdjust.y+0.5+colorAdjust.x); float y=dot(c,float3(0.299,0.587,0.114)); return saturate(y+(c-y)*colorExtra.x); }\n"
    "void fs_tri(uint id, out float4 p, out float2 uv) {\n"
    "    uv = float2((id << 1) & 2, id & 2);\n"
    "    p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }\n";

/* FXAA 3.11, PC quality path (Timothy Lottes), luma computed from the colour. */
static const char k_aa_fxaa[] =
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { V o; fs_tri(id, o.p, o.uv); return o; }\n"
    "float L(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n"
    "float LT(float2 p) { return L(colorTex.SampleLevel(LinearSampler, p, 0).rgb); }\n"
    "float LO(float2 p, int2 o) { return L(colorTex.SampleLevel(LinearSampler, p, 0, o).rgb); }\n"
    "static const float kStep[12] = { 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0 };\n"
    "float4 ps(V i) : SV_Target {\n"
    "    const float subpix = 0.75, edgeTh = 0.166, edgeThMin = 0.0833;\n"
    "    float2 rcpf = rt.xy; float2 posM = i.uv;\n"
    "    float4 rgbM = colorTex.SampleLevel(LinearSampler, posM, 0);\n"
    "    float lumaM = L(rgbM.rgb);\n"
    "    float lumaS = LO(posM, int2(0, 1)), lumaE = LO(posM, int2(1, 0));\n"
    "    float lumaN = LO(posM, int2(0, -1)), lumaW = LO(posM, int2(-1, 0));\n"
    "    float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));\n"
    "    float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));\n"
    "    float range = rangeMax - rangeMin;\n"
    "    if (range < max(edgeThMin, rangeMax * edgeTh)) return float4(adjust_color(rgbM.rgb), 1);\n"
    "    float lumaNW = LO(posM, int2(-1, -1)), lumaSE = LO(posM, int2(1, 1));\n"
    "    float lumaNE = LO(posM, int2(1, -1)), lumaSW = LO(posM, int2(-1, 1));\n"
    "    float lumaNS = lumaN + lumaS, lumaWE = lumaW + lumaE;\n"
    "    float subpixRcpRange = 1.0 / range, subpixNSWE = lumaNS + lumaWE;\n"
    "    float edgeHorz1 = -2.0 * lumaM + lumaNS, edgeVert1 = -2.0 * lumaM + lumaWE;\n"
    "    float lumaNESE = lumaNE + lumaSE, lumaNWNE = lumaNW + lumaNE;\n"
    "    float edgeHorz2 = -2.0 * lumaE + lumaNESE, edgeVert2 = -2.0 * lumaN + lumaNWNE;\n"
    "    float lumaNWSW = lumaNW + lumaSW, lumaSWSE = lumaSW + lumaSE;\n"
    "    float edgeHorz4 = abs(edgeHorz1) * 2.0 + abs(edgeHorz2);\n"
    "    float edgeVert4 = abs(edgeVert1) * 2.0 + abs(edgeVert2);\n"
    "    float edgeHorz3 = -2.0 * lumaW + lumaNWSW, edgeVert3 = -2.0 * lumaS + lumaSWSE;\n"
    "    float edgeHorz = abs(edgeHorz3) + edgeHorz4, edgeVert = abs(edgeVert3) + edgeVert4;\n"
    "    float subpixNWSWNESE = lumaNWSW + lumaNESE;\n"
    "    float lengthSign = rcpf.x;\n"
    "    bool horzSpan = edgeHorz >= edgeVert;\n"
    "    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;\n"
    "    if (!horzSpan) { lumaN = lumaW; lumaS = lumaE; } else lengthSign = rcpf.y;\n"
    "    float subpixB = subpixA * (1.0 / 12.0) - lumaM;\n"
    "    float gradientN = lumaN - lumaM, gradientS = lumaS - lumaM;\n"
    "    float lumaNN = lumaN + lumaM, lumaSS = lumaS + lumaM;\n"
    "    bool pairN = abs(gradientN) >= abs(gradientS);\n"
    "    float gradient = max(abs(gradientN), abs(gradientS));\n"
    "    if (pairN) lengthSign = -lengthSign;\n"
    "    float subpixC = saturate(abs(subpixB) * subpixRcpRange);\n"
    "    float2 posB = posM;\n"
    "    float2 offNP = horzSpan ? float2(rcpf.x, 0) : float2(0, rcpf.y);\n"
    "    if (!horzSpan) posB.x += lengthSign * 0.5; else posB.y += lengthSign * 0.5;\n"
    "    float2 posN = posB - offNP * kStep[0], posP = posB + offNP * kStep[0];\n"
    "    float subpixD = -2.0 * subpixC + 3.0, subpixE = subpixC * subpixC;\n"
    "    float lumaEndN = LT(posN), lumaEndP = LT(posP);\n"
    "    if (!pairN) lumaNN = lumaSS;\n"
    "    float gradientScaled = gradient * 0.25;\n"
    "    float lumaMM = lumaM - lumaNN * 0.5;\n"
    "    float subpixF = subpixD * subpixE;\n"
    "    bool lumaMLTZero = lumaMM < 0.0;\n"
    "    lumaEndN -= lumaNN * 0.5; lumaEndP -= lumaNN * 0.5;\n"
    "    bool doneN = abs(lumaEndN) >= gradientScaled, doneP = abs(lumaEndP) >= gradientScaled;\n"
    "    if (!doneN) posN -= offNP * kStep[1];\n"
    "    if (!doneP) posP += offNP * kStep[1];\n"
    "    [loop] for (int s = 2; s < 12 && (!doneN || !doneP); s++) {\n"
    "        if (!doneN) lumaEndN = LT(posN) - lumaNN * 0.5;\n"
    "        if (!doneP) lumaEndP = LT(posP) - lumaNN * 0.5;\n"
    "        doneN = abs(lumaEndN) >= gradientScaled; doneP = abs(lumaEndP) >= gradientScaled;\n"
    "        if (!doneN) posN -= offNP * kStep[s];\n"
    "        if (!doneP) posP += offNP * kStep[s];\n"
    "    }\n"
    "    float dstN = horzSpan ? posM.x - posN.x : posM.y - posN.y;\n"
    "    float dstP = horzSpan ? posP.x - posM.x : posP.y - posM.y;\n"
    "    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero, goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;\n"
    "    float spanLengthRcp = 1.0 / (dstP + dstN);\n"
    "    bool directionN = dstN < dstP;\n"
    "    float dst = min(dstN, dstP);\n"
    /* only where the search found that end of the edge: on a long straight edge the
     * unfinished end reads ~0, its sign is noise, and following it shifted each row
     * differently (a comb along vertical text stems) */
    "    bool goodSpan = directionN ? (goodSpanN && doneN) : (goodSpanP && doneP);\n"
    "    float subpixG = subpixF * subpixF;\n"
    "    float pixelOffset = dst * (-spanLengthRcp) + 0.5;\n"
    "    float subpixH = subpixG * subpix;\n"
    "    float pixelOffsetSubpix = max(goodSpan ? pixelOffset : 0.0, subpixH);\n"
    "    if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign; else posM.y += pixelOffsetSubpix * lengthSign;\n"
    "    return float4(adjust_color(colorTex.SampleLevel(LinearSampler, posM, 0).rgb), 1); }\n";

static const char k_aa_adjust[] =
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { V o; fs_tri(id, o.p, o.uv); return o; }\n"
    "float4 ps(V i) : SV_Target { float4 c=colorTex.SampleLevel(LinearSampler,i.uv,0); return float4(adjust_color(c.rgb),c.a); }\n";

/* SMAA: the library compiled with our own sampler and texture bindings
 * (SMAA_CUSTOM_SL), then the three passes. */
static const char k_aa_smaa_pre[] =
    "#define SMAA_RT_METRICS rt\n"
    "#define SMAA_CUSTOM_SL 1\n"
    "#define SMAA_PRESET_ULTRA 1\n"   /* threshold 0.05: the title is dark and High (0.1) missed most edges */
    "#define SMAATexture2D(tex) Texture2D tex\n"
    "#define SMAATexturePass2D(tex) tex\n"
    "#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)\n"
    "#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)\n"
    "#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)\n"
    "#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)\n"
    "#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)\n"
    "#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)\n"
    "#define SMAA_FLATTEN [flatten]\n"
    "#define SMAA_BRANCH [branch]\n"
    "#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)\n";
static const char k_aa_smaa_post[] =
    "struct VE { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 o0 : TEXCOORD1;\n"
    "            float4 o1 : TEXCOORD2; float4 o2 : TEXCOORD3; float2 pix : TEXCOORD4; };\n"
    "VE vs_edge(uint id : SV_VertexID) { VE o = (VE)0; fs_tri(id, o.p, o.uv);\n"
    "    float4 off[3]; SMAAEdgeDetectionVS(o.uv, off); o.o0 = off[0]; o.o1 = off[1]; o.o2 = off[2]; return o; }\n"
    "float4 ps_edge(VE i) : SV_Target { float4 off[3] = { i.o0, i.o1, i.o2 };\n"
    "    return float4(SMAALumaEdgeDetectionPS(i.uv, off, colorTex), 0, 0); }\n"
    "VE vs_weight(uint id : SV_VertexID) { VE o = (VE)0; fs_tri(id, o.p, o.uv);\n"
    "    float4 off[3]; SMAABlendingWeightCalculationVS(o.uv, o.pix, off);\n"
    "    o.o0 = off[0]; o.o1 = off[1]; o.o2 = off[2]; return o; }\n"
    "float4 ps_weight(VE i) : SV_Target { float4 off[3] = { i.o0, i.o1, i.o2 };\n"
    "    return SMAABlendingWeightCalculationPS(i.uv, i.pix, off, edgesTex, areaTex, searchTex, float4(0, 0, 0, 0)); }\n"
    "VE vs_blend(uint id : SV_VertexID) { VE o = (VE)0; fs_tri(id, o.p, o.uv);\n"
    "    SMAANeighborhoodBlendingVS(o.uv, o.o0); return o; }\n"
    "float4 ps_blend(VE i) : SV_Target {\n"
    "    return float4(adjust_color(SMAANeighborhoodBlendingPS(i.uv, i.o0, colorTex, blendTex).rgb), 1); }\n"
    /* AA_SHOW=1: edges in red/green, blend weights in blue (diagnostics) */
    "float4 ps_show(VE i) : SV_Target { float2 e = edgesTex.SampleLevel(PointSampler, i.uv, 0).rg;\n"
    "    float4 w = blendTex.SampleLevel(PointSampler, i.uv, 0);\n"
    "    return float4(e, saturate(dot(w, float4(1, 1, 1, 1)) * 2.0), 1); }\n";

static int aa_init_rs(void)
{
    if (s_aa_rs) return 1;
    D3D12_DESCRIPTOR_RANGE range = {0};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 5;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER rp[2] = {0};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants.Num32BitValues = 12;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC ss[2] = {0};
    for (int i = 0; i < 2; i++) {
        ss[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss[i].MaxLOD = D3D12_FLOAT32_MAX;
        ss[i].ShaderRegister = (UINT)i;
        ss[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC desc = {0};
    desc.NumParameters = 2; desc.pParameters = rp;
    desc.NumStaticSamplers = 2; desc.pStaticSamplers = ss;
    ID3DBlob *sig = NULL, *err = NULL;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (err) err->lpVtbl->Release(err);
    if (SUCCEEDED(hr))
        hr = s_d3d.device->lpVtbl->CreateRootSignature(s_d3d.device, 0, sig->lpVtbl->GetBufferPointer(sig),
                 sig->lpVtbl->GetBufferSize(sig), &IID_ID3D12RootSignature, (void**)&s_aa_rs);
    if (sig) sig->lpVtbl->Release(sig);
    if (FAILED(hr)) { printf("[AA] root signature failed (0x%08lX)\n", hr); return 0; }
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 2;
    if (FAILED(s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device, &hd, &IID_ID3D12DescriptorHeap,
                                                         (void**)&s_aa_rtv))) return 0;
    return 1;
}

static ID3D12PipelineState* aa_pso(const char* src, size_t len, const char* vs, const char* ps)
{
    ID3DBlob *vb = NULL, *pb = NULL, *e = NULL;
    HRESULT hr = D3DCompile(src, len, "aa", NULL, NULL, vs, "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vb, &e);
    if (e) { if (FAILED(hr)) printf("[AA] %s: %s\n", vs, (const char*)e->lpVtbl->GetBufferPointer(e));
             e->lpVtbl->Release(e); e = NULL; }
    if (SUCCEEDED(hr))
        hr = D3DCompile(src, len, "aa", NULL, NULL, ps, "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pb, &e);
    if (e) { if (FAILED(hr)) printf("[AA] %s: %s\n", ps, (const char*)e->lpVtbl->GetBufferPointer(e));
             e->lpVtbl->Release(e); }
    ID3D12PipelineState* pso = NULL;
    if (SUCCEEDED(hr)) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
        pd.pRootSignature = s_aa_rs;
        pd.VS.pShaderBytecode = vb->lpVtbl->GetBufferPointer(vb); pd.VS.BytecodeLength = vb->lpVtbl->GetBufferSize(vb);
        pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb); pd.PS.BytecodeLength = pb->lpVtbl->GetBufferSize(pb);
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.SampleDesc.Count = 1;
        hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, &pd, &IID_ID3D12PipelineState,
                                                               (void**)&pso);
        if (FAILED(hr)) { printf("[AA] PSO %s failed (0x%08lX)\n", ps, hr); pso = NULL; }
    }
    if (vb) vb->lpVtbl->Release(vb);
    if (pb) pb->lpVtbl->Release(pb);
    return pso;
}

/* Builds what the mode needs on its first use (shader compile: one hitch). */
static int aa_ready(int mode)
{
    if (s_aa_failed[mode] || !aa_init_rs()) return 0;
    if (mode == 1) {
        if (s_aa_fxaa) return 1;
        size_t n = sizeof k_aa_common + sizeof k_aa_fxaa;
        char* src = (char*)malloc(n);
        if (!src) return 0;
        snprintf(src, n, "%s%s", k_aa_common, k_aa_fxaa);
        s_aa_fxaa = aa_pso(src, strlen(src), "vs", "ps");
        free(src);
        if (!s_aa_fxaa) { s_aa_failed[1] = 1; return 0; }
        printf("[AA] FXAA ready\n");
        return 1;
    }
    if (s_aa_blend) return 1;
    size_t n = sizeof k_aa_common + sizeof k_aa_smaa_pre + sizeof k_smaa_hlsl + sizeof k_aa_smaa_post + 4;
    char* src = (char*)malloc(n);
    if (!src) return 0;
    snprintf(src, n, "%s%s%s\n%s", k_aa_common, k_aa_smaa_pre, k_smaa_hlsl, k_aa_smaa_post);
    const size_t len = strlen(src);
    s_aa_edge   = aa_pso(src, len, "vs_edge",   "ps_edge");
    s_aa_weight = aa_pso(src, len, "vs_weight", "ps_weight");
    s_aa_blend  = aa_pso(src, len, "vs_blend",  "ps_blend");
    if (getenv("AA_SHOW")) s_aa_show = aa_pso(src, len, "vs_blend", "ps_show");
    free(src);
    if (!s_aa_edge || !s_aa_weight || !s_aa_blend) { s_aa_failed[2] = 1; s_aa_blend = NULL; return 0; }
    printf("[AA] SMAA ready\n");
    return 1;
}

static int aa_adjust_ready(void)
{
    if (s_aa_adjust) return 1;
    if (!aa_init_rs()) return 0;
    const size_t n = sizeof k_aa_common + sizeof k_aa_adjust;
    char* src = (char*)malloc(n);
    if (!src) return 0;
    snprintf(src, n, "%s%s", k_aa_common, k_aa_adjust);
    s_aa_adjust = aa_pso(src, strlen(src), "vs", "ps");
    free(src);
    if (!s_aa_adjust) return 0;
    printf("[PC_SETTINGS] color adjustment ready\n");
    return 1;
}

static ID3D12Resource* aa_tex(u32 w, u32 h, DXGI_FORMAT fmt, int rt, D3D12_RESOURCE_STATES st)
{
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    td.Flags = rt ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    D3D12_CLEAR_VALUE cv = {0}; cv.Format = fmt;
    ID3D12Resource* r = NULL;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            st, rt ? &cv : NULL, &IID_ID3D12Resource, (void**)&r))) return NULL;
    return r;
}

static void aa_barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER br = {0};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = r;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    br.Transition.StateBefore = a; br.Transition.StateAfter = b;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &br);
}

/* SMAA's two lookup tables, uploaded once through the frame's command list. */
static void aa_upload_luts(void)
{
    if (s_aa_luts_ready || s_aa_up) return;
    const u32 ap = (AREATEX_PITCH + 255) & ~255u, sp = (SEARCHTEX_PITCH + 255) & ~255u;
    const u64 aoff = 0, soff = ((u64)ap * AREATEX_HEIGHT + 511) & ~511ull;
    const u64 total = soff + (u64)sp * SEARCHTEX_HEIGHT;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&s_aa_up))) {
        s_aa_up = NULL; s_aa_failed[2] = 1; return;
    }
    u8* m = NULL; D3D12_RANGE nr = {0, 0};
    if (FAILED(s_aa_up->lpVtbl->Map(s_aa_up, 0, &nr, (void**)&m)) || !m) { s_aa_failed[2] = 1; return; }
    for (u32 y = 0; y < AREATEX_HEIGHT; y++)
        memcpy(m + aoff + (u64)y * ap, areaTexBytes + (size_t)y * AREATEX_PITCH, AREATEX_PITCH);
    for (u32 y = 0; y < SEARCHTEX_HEIGHT; y++)
        memcpy(m + soff + (u64)y * sp, searchTexBytes + (size_t)y * SEARCHTEX_PITCH, SEARCHTEX_PITCH);
    s_aa_up->lpVtbl->Unmap(s_aa_up, 0, NULL);
    s_aa_area   = aa_tex(AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    s_aa_search = aa_tex(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!s_aa_area || !s_aa_search) { s_aa_failed[2] = 1; return; }
    D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
    src.pResource = s_aa_up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = aoff;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8_UNORM;
    src.PlacedFootprint.Footprint.Width = AREATEX_WIDTH; src.PlacedFootprint.Footprint.Height = AREATEX_HEIGHT;
    src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = ap;
    dst.pResource = s_aa_area; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    src.PlacedFootprint.Offset = soff;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    src.PlacedFootprint.Footprint.Width = SEARCHTEX_WIDTH; src.PlacedFootprint.Footprint.Height = SEARCHTEX_HEIGHT;
    src.PlacedFootprint.Footprint.RowPitch = sp;
    dst.pResource = s_aa_search;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    aa_barrier(s_aa_area, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    aa_barrier(s_aa_search, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    srv_write(AA_SRV_BASE + 2, s_aa_area, DXGI_FORMAT_R8G8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    srv_write(AA_SRV_BASE + 3, s_aa_search, DXGI_FORMAT_R8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    s_aa_luts_ready = 1;
}

/* Frame-sized targets, recreated when the render size changes. */
static int aa_targets(u32 w, u32 h, int with_smaa)
{
    if (s_aa_color && s_aa_w == w && s_aa_h == h) {
        if (!with_smaa || (s_aa_edges && s_aa_weights)) return 1;
    } else {
        defer_release(s_aa_color); defer_release(s_aa_edges); defer_release(s_aa_weights);
        s_aa_color = aa_tex(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        s_aa_edges = s_aa_weights = NULL;
        if (!s_aa_color) { s_aa_w = s_aa_h = 0; return 0; }
        s_aa_w = w; s_aa_h = h;
        srv_write(AA_SRV_BASE + 0, s_aa_color, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    }
    if (!with_smaa) return 1;
    s_aa_edges = aa_tex(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
    s_aa_weights = aa_tex(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (!s_aa_edges || !s_aa_weights) {
        defer_release(s_aa_edges); defer_release(s_aa_weights);
        s_aa_edges = s_aa_weights = NULL;
        return 0;
    }
    srv_write(AA_SRV_BASE + 1, s_aa_edges,   DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    srv_write(AA_SRV_BASE + 4, s_aa_weights, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    D3D12_CPU_DESCRIPTOR_HANDLE h0;
    s_aa_rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_aa_rtv, &h0);
    const UINT inc = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, s_aa_edges, NULL, h0);
    h0.ptr += inc;
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, s_aa_weights, NULL, h0);
    return 1;
}

static void aa_draw(ID3D12PipelineState* pso, D3D12_CPU_DESCRIPTOR_HANDLE rtv, int clear)
{
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    if (clear) { const float z[4] = {0, 0, 0, 0}; cl->lpVtbl->ClearRenderTargetView(cl, rtv, z, 0, NULL); }
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &rtv, FALSE, NULL);
    cl->lpVtbl->SetPipelineState(cl, pso);
    cl->lpVtbl->DrawInstanced(cl, 3, 1, 0, 0);
}

/* fi's backbuffer is in RENDER_TARGET state on entry and on exit. */
static void aa_apply(u32 fi)
{
    if (s_aa_mode < 0) {
        const char* e = getenv("AA_MODE");
        s_aa_mode = !e ? 0 : (!_stricmp(e, "fxaa") || !strcmp(e, "1")) ? 1
                  : (!_stricmp(e, "smaa") || !strcmp(e, "2")) ? 2 : 0;
    }
    color_adjust_load_env();
    int mode = s_aa_mode;
    const int adjust = s_color_brightness != 0 || s_color_contrast != 100 || s_color_saturation != 100;
    if (mode <= 0 && !adjust) return;
    if (mode > 0 && !aa_ready(mode)) {
        if (!adjust) return;
        mode = 0;
    }
    if (mode == 0 && !aa_adjust_ready()) return;
    const u32 w = rs_dim(s_d3d.width, s_rs), h = rs_dim(s_d3d.height, s_rs);
    if (!aa_targets(w, h, mode == 2)) return;
    if (mode == 2) { aa_upload_luts(); if (!s_aa_luts_ready) return; }
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    ID3D12Resource* bb = s_d3d.render_targets[fi];

    /* the finished frame -> colour texture */
    aa_barrier(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    aa_barrier(s_aa_color, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->lpVtbl->CopyResource(cl, s_aa_color, bb);
    aa_barrier(s_aa_color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    aa_barrier(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    cl->lpVtbl->SetDescriptorHeaps(cl, 1, &s_d3d.srv_heap);
    cl->lpVtbl->SetGraphicsRootSignature(cl, s_aa_rs);
    /* HLSL cbuffer packs three float4s: rt[0..3], colorAdjust[4..7],
       colorExtra[8..11]. Saturation belongs at constant 8 (colorExtra.x). */
    const float metrics[12] = { 1.0f / (float)w, 1.0f / (float)h, (float)w, (float)h,
        (float)s_color_brightness / 200.0f, (float)s_color_contrast / 100.0f,
        0.0f, 0.0f, (float)s_color_saturation / 100.0f, 0.0f, 0.0f, 0.0f };
    cl->lpVtbl->SetGraphicsRoot32BitConstants(cl, 0, 12, metrics, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE gh;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
    gh.ptr += (u64)AA_SRV_BASE * s_d3d.srv_inc;
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh);
    D3D12_VIEWPORT vp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, (LONG)w, (LONG)h};
    { static int split = -1;   /* AA_SPLIT=1: right half only, to compare in one frame */
      if (split < 0) split = getenv("AA_SPLIT") ? 1 : 0;
      if (split) sc.left = (LONG)(w / 2); }
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &sc);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &bb_rtv);
    bb_rtv.ptr += (u64)fi * s_d3d.rtv_descriptor_size;
    if (mode == 0) {
        aa_draw(s_aa_adjust, bb_rtv, 0);
        return;
    }
    if (mode == 1) {
        aa_draw(s_aa_fxaa, bb_rtv, 0);
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE e_rtv, w_rtv;
    s_aa_rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_aa_rtv, &e_rtv);
    w_rtv = e_rtv;
    w_rtv.ptr += s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    aa_draw(s_aa_edge, e_rtv, 1);
    aa_barrier(s_aa_edges, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    aa_draw(s_aa_weight, w_rtv, 1);
    aa_barrier(s_aa_weights, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    aa_draw(s_aa_show ? s_aa_show : s_aa_blend, bb_rtv, 0);
    aa_barrier(s_aa_edges, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    aa_barrier(s_aa_weights, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

/* Display pass: the game target of frame fi, scaled into the window-sized
 * swap chain buffer, then the overlays at the window's pixels. Both buffers
 * are in PRESENT on entry and on exit. */
static ID3D12PipelineState* s_disp_pso;
static int s_disp_failed;
static const char k_disp_hlsl[] =
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs_up(uint id : SV_VertexID) { V o; fs_tri(id, o.p, o.uv); return o; }\n"
    "float4 ps_up(V i) : SV_Target { return float4(colorTex.SampleLevel(LinearSampler, i.uv, 0).rgb, 1); }\n";

/* ---------------------------------------------------------------------------
 * AMD FidelityFX Super Resolution 1.0: EASU (edge-adaptive upscale) and RCAS
 * (robust contrast-adaptive sharpening), on the game image only -- after AA,
 * before the port's overlays. Float32 port of ffx_fsr1.h (FsrEasuF, FsrRcasF
 * with FSR_RCAS_DENOISE), from the FidelityFX SDK:
 *
 *   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * CV_SHARPNESS=0..100 (rsx_d3d12_set_sharpness): RCAS strength, 0 = off (the
 * image is exactly the plain path's), 100 = FSR's maximum (0 stops).
 * CV_FSR1_UPSCALE=1 (rsx_d3d12_set_fsr1_upscale): EASU in place of the
 * bilinear stretch when the render size is below the window's.
 * CV_SHARPNESS_SPLIT=1: left half without FSR, to compare in one frame.
 * Constants (the [AA] root signature's 12): EASU rt = con0, colorAdjust.xy =
 * 1 / input size (con1..3 follow from it); RCAS colorExtra = (strength,
 * last pixel x, last pixel y).
 * -------------------------------------------------------------------------*/
static volatile int s_sharp = -1;   /* -1: read CV_SHARPNESS once */
static volatile int s_easu = -1;    /* -1: read CV_FSR1_UPSCALE once */
void rsx_d3d12_set_sharpness(int pct) { s_sharp = pct < 0 ? 0 : pct > 100 ? 100 : pct; }
void rsx_d3d12_set_fsr1_upscale(int on) { s_easu = on ? 1 : 0; }

static const char k_fsr_hlsl[] =
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs_fsr(uint id : SV_VertexID) { V o; fs_tri(id, o.p, o.uv); return o; }\n"
    "float LumA(float r, float g, float b) { return b * 0.5 + (r * 0.5 + g); }\n"
    "float4 LumA4(float4 r, float4 g, float4 b) { return b * 0.5 + (r * 0.5 + g); }\n"
    /* EASU */
    "void EasuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len, float lob, float clp, float3 c) {\n"
    "    float2 v = float2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;\n"
    "    float d2 = min(dot(v, v), clp);\n"
    "    float wB = 0.4 * d2 - 1.0, wA = lob * d2 - 1.0;\n"
    "    wB *= wB; wA *= wA;\n"
    "    wB = 1.5625 * wB - 0.5625;\n"
    "    float w = wB * wA; aC += c * w; aW += w; }\n"
    "void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {\n"
    "    float lenX = rcp(max(max(abs(lD - lC), abs(lC - lB)), 1.0 / 65536.0));\n"
    "    float dirX = lD - lB; dir.x += dirX * w;\n"
    "    lenX = saturate(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;\n"
    "    float lenY = rcp(max(max(abs(lE - lC), abs(lC - lA)), 1.0 / 65536.0));\n"
    "    float dirY = lE - lA; dir.y += dirY * w;\n"
    "    lenY = saturate(abs(dirY) * lenY); lenY *= lenY; len += lenY * w; }\n"
    "float4 ps_easu(V i) : SV_Target {\n"
    "    float2 pp = floor(i.p.xy) * rt.xy + rt.zw;\n"
    "    float2 fp = floor(pp); pp -= fp;\n"
    /* 12 taps:  b c / e f g h / i j k l / n o, four gathers */
    "    float2 ri = colorAdjust.xy;\n"
    "    float2 p0 = (fp + float2(1, -1)) * ri;\n"
    "    float2 p1 = p0 + float2(-1, 2) * ri, p2 = p0 + float2(1, 2) * ri, p3 = p0 + float2(0, 4) * ri;\n"
    "    float4 bczzR = colorTex.GatherRed(LinearSampler, p0), bczzG = colorTex.GatherGreen(LinearSampler, p0), bczzB = colorTex.GatherBlue(LinearSampler, p0);\n"
    "    float4 ijfeR = colorTex.GatherRed(LinearSampler, p1), ijfeG = colorTex.GatherGreen(LinearSampler, p1), ijfeB = colorTex.GatherBlue(LinearSampler, p1);\n"
    "    float4 klhgR = colorTex.GatherRed(LinearSampler, p2), klhgG = colorTex.GatherGreen(LinearSampler, p2), klhgB = colorTex.GatherBlue(LinearSampler, p2);\n"
    "    float4 zzonR = colorTex.GatherRed(LinearSampler, p3), zzonG = colorTex.GatherGreen(LinearSampler, p3), zzonB = colorTex.GatherBlue(LinearSampler, p3);\n"
    "    float4 bczzL = LumA4(bczzR, bczzG, bczzB), ijfeL = LumA4(ijfeR, ijfeG, ijfeB);\n"
    "    float4 klhgL = LumA4(klhgR, klhgG, klhgB), zzonL = LumA4(zzonR, zzonG, zzonB);\n"
    "    float bL = bczzL.x, cL = bczzL.y, iL = ijfeL.x, jL = ijfeL.y, fL = ijfeL.z, eL = ijfeL.w;\n"
    "    float kL = klhgL.x, lL = klhgL.y, hL = klhgL.z, gL = klhgL.w, oL = zzonL.z, nL = zzonL.w;\n"
    "    float2 dir = 0; float len = 0;\n"
    "    EasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);\n"
    "    EasuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);\n"
    "    EasuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);\n"
    "    EasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);\n"
    "    float dirR = dot(dir, dir); bool zro = dirR < 1.0 / 32768.0;\n"
    "    dirR = zro ? 1.0 : rsqrt(max(dirR, 1.0 / 32768.0));\n"
    "    dir.x = zro ? 1.0 : dir.x; dir *= dirR;\n"
    "    len = len * 0.5; len *= len;\n"
    "    float stretch = dot(dir, dir) * rcp(max(abs(dir.x), abs(dir.y)));\n"
    "    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;\n"
    "    float clp = rcp(lob);\n"
    "    float3 cf = float3(ijfeR.z, ijfeG.z, ijfeB.z), cg = float3(klhgR.w, klhgG.w, klhgB.w);\n"
    "    float3 cj = float3(ijfeR.y, ijfeG.y, ijfeB.y), ck = float3(klhgR.x, klhgG.x, klhgB.x);\n"
    "    float3 min4 = min(min(cf, cg), min(cj, ck)), max4 = max(max(cf, cg), max(cj, ck));\n"
    "    float3 aC = 0; float aW = 0;\n"
    "    EasuTap(aC, aW, float2( 0, -1) - pp, dir, len2, lob, clp, float3(bczzR.x, bczzG.x, bczzB.x));\n"
    "    EasuTap(aC, aW, float2( 1, -1) - pp, dir, len2, lob, clp, float3(bczzR.y, bczzG.y, bczzB.y));\n"
    "    EasuTap(aC, aW, float2(-1,  1) - pp, dir, len2, lob, clp, float3(ijfeR.x, ijfeG.x, ijfeB.x));\n"
    "    EasuTap(aC, aW, float2( 0,  1) - pp, dir, len2, lob, clp, cj);\n"
    "    EasuTap(aC, aW, float2( 0,  0) - pp, dir, len2, lob, clp, cf);\n"
    "    EasuTap(aC, aW, float2(-1,  0) - pp, dir, len2, lob, clp, float3(ijfeR.w, ijfeG.w, ijfeB.w));\n"
    "    EasuTap(aC, aW, float2( 1,  1) - pp, dir, len2, lob, clp, ck);\n"
    "    EasuTap(aC, aW, float2( 2,  1) - pp, dir, len2, lob, clp, float3(klhgR.y, klhgG.y, klhgB.y));\n"
    "    EasuTap(aC, aW, float2( 2,  0) - pp, dir, len2, lob, clp, float3(klhgR.z, klhgG.z, klhgB.z));\n"
    "    EasuTap(aC, aW, float2( 1,  0) - pp, dir, len2, lob, clp, cg);\n"
    "    EasuTap(aC, aW, float2( 1,  2) - pp, dir, len2, lob, clp, float3(zzonR.z, zzonG.z, zzonB.z));\n"
    "    EasuTap(aC, aW, float2( 0,  2) - pp, dir, len2, lob, clp, float3(zzonR.w, zzonG.w, zzonB.w));\n"
    "    return float4(min(max4, max(min4, aC * rcp(aW))), 1); }\n"
    /* RCAS: a cross of 5 texels at the output's own resolution */
    "float3 RcasLoad(int2 p) { return colorTex.Load(int3(clamp(p, int2(0, 0), int2(colorExtra.yz)), 0)).rgb; }\n"
    "float4 ps_rcas(V i) : SV_Target {\n"
    "    int2 ip = int2(i.p.xy);\n"
    "    float3 b = RcasLoad(ip + int2(0, -1)), d = RcasLoad(ip + int2(-1, 0)), e = RcasLoad(ip);\n"
    "    float3 f = RcasLoad(ip + int2(1, 0)), h = RcasLoad(ip + int2(0, 1));\n"
    "    float bL = LumA(b.r, b.g, b.b), dL = LumA(d.r, d.g, d.b), eL = LumA(e.r, e.g, e.b);\n"
    "    float fL = LumA(f.r, f.g, f.b), hL = LumA(h.r, h.g, h.b);\n"
    "    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;\n"
    "    float rng = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);\n"
    "    nz = saturate(abs(nz) * rcp(max(rng, 1.0 / 65536.0)));\n"
    "    nz = -0.5 * nz + 1.0;\n"
    "    float3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));\n"
    "    float3 hitMin = min(mn4, e) * rcp(max(4.0 * mx4, 1.0 / 65536.0));\n"
    "    float3 hitMax = (1.0 - max(mx4, e)) * rcp(min(4.0 * mn4 - 4.0, -1.0 / 65536.0));\n"
    "    float3 lobe3 = max(-hitMin, hitMax);\n"
    "    float lobe = max(-(0.25 - 1.0 / 16.0), min(max(max(lobe3.r, lobe3.g), lobe3.b), 0.0)) * colorExtra.x;\n"
    "    lobe *= nz;\n"
    "    return float4((lobe * (b + d + h + f) + e) * rcp(4.0 * lobe + 1.0), 1); }\n";

static ID3D12PipelineState *s_fsr_easu, *s_fsr_rcas;
static int s_fsr_failed;
static ID3D12DescriptorHeap* s_fsr_rtv;      /* the intermediate's RTV */
static ID3D12Resource* s_fsr_tmp;            /* between EASU and RCAS; PIXEL_SHADER_RESOURCE at rest */
static u32 s_fsr_w, s_fsr_h;

static void fsr_load_env(void)
{
    if (s_sharp < 0) { const char* e = getenv("CV_SHARPNESS"); rsx_d3d12_set_sharpness(e ? atoi(e) : 0); }
    if (s_easu < 0) { const char* e = getenv("CV_FSR1_UPSCALE"); s_easu = (e && *e == '1') ? 1 : 0; }
}

static int fsr_ready(void)
{
    if (s_fsr_rcas) return 1;
    if (s_fsr_failed || !aa_init_rs()) return 0;
    const size_t n = sizeof k_aa_common + sizeof k_fsr_hlsl;
    char* src = (char*)malloc(n);
    if (!src) return 0;
    snprintf(src, n, "%s%s", k_aa_common, k_fsr_hlsl);
    s_fsr_easu = aa_pso(src, strlen(src), "vs_fsr", "ps_easu");
    s_fsr_rcas = aa_pso(src, strlen(src), "vs_fsr", "ps_rcas");
    free(src);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 1;
    if (!s_fsr_easu || !s_fsr_rcas
        || FAILED(s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device, &hd, &IID_ID3D12DescriptorHeap,
                                                            (void**)&s_fsr_rtv))) {
        s_fsr_failed = 1; s_fsr_rcas = NULL;
        printf("[FSR1] unavailable\n");
        return 0;
    }
    printf("[FSR1] EASU + RCAS ready\n");
    return 1;
}

/* The intermediate at w x h; its view also at AA_SRV_BASE + 5 (backbuffer path). */
static int fsr_tmp(u32 w, u32 h)
{
    if (s_fsr_tmp && s_fsr_w == w && s_fsr_h == h) return 1;
    defer_release(s_fsr_tmp);
    s_fsr_tmp = aa_tex(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!s_fsr_tmp) { s_fsr_w = s_fsr_h = 0; return 0; }
    s_fsr_w = w; s_fsr_h = h;
    D3D12_CPU_DESCRIPTOR_HANDLE h0;
    s_fsr_rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_fsr_rtv, &h0);
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, s_fsr_tmp, NULL, h0);
    srv_write(AA_SRV_BASE + 5, s_fsr_tmp, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    return 1;
}

static D3D12_CPU_DESCRIPTOR_HANDLE fsr_tmp_rtv(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h0;
    s_fsr_rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_fsr_rtv, &h0);
    return h0;
}

/* One full-screen pass reading the view at srv_slot into rtv (w x h). */
static void fsr_draw(ID3D12PipelineState* pso, u32 srv_slot, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                     u32 w, u32 h, const float k[12], u32 x0)
{
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    cl->lpVtbl->SetDescriptorHeaps(cl, 1, &s_d3d.srv_heap);
    cl->lpVtbl->SetGraphicsRootSignature(cl, s_aa_rs);
    cl->lpVtbl->SetGraphicsRoot32BitConstants(cl, 0, 12, k, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE gh;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
    gh.ptr += (u64)srv_slot * s_d3d.srv_inc;
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh);
    D3D12_VIEWPORT vp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
    D3D12_RECT sc = {(LONG)x0, 0, (LONG)w, (LONG)h};
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &sc);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    aa_draw(pso, rtv, 0);
}

static float fsr_strength(int pct) { return exp2f(-2.0f * (float)(100 - pct) / 100.0f); }

/* Display path: the game image (a shader resource, its view at slot) into
 * out_rtv (ow x oh) through EASU and/or RCAS; slot + 1 is free for the
 * intermediate. Returns 0 to leave the plain bilinear scale to the caller. */
static int fsr_display(u32 slot, D3D12_CPU_DESCRIPTOR_HANDLE out_rtv, u32 ow, u32 oh,
                       ID3D12PipelineState* bilinear)
{
    fsr_load_env();
    const int sharp = s_sharp;
    const u32 gw = rs_dim(s_d3d.width, s_rs), gh = rs_dim(s_d3d.height, s_rs);
    const int up = s_easu > 0 && gw <= ow && gh <= oh && (gw < ow || gh < oh);
    if ((!sharp && !up) || !gw || !gh || !fsr_ready()) return 0;
    const float con = fsr_strength(sharp);
    /* CV_SHARPNESS_SPLIT=1: the plain scale on the left half, FSR on the right */
    static int split = -1;
    if (split < 0) split = getenv("CV_SHARPNESS_SPLIT") ? 1 : 0;
    const u32 x0 = (split && bilinear) ? ow / 2 : 0;
    const float kz[12] = {0};
    if (x0) fsr_draw(bilinear, slot, out_rtv, ow, oh, kz, 0);
    if (up) {
        const float sx = (float)gw / (float)ow, sy = (float)gh / (float)oh;
        const float ke[12] = { sx, sy, 0.5f * sx - 0.5f, 0.5f * sy - 0.5f,
                               1.0f / (float)gw, 1.0f / (float)gh, (float)gw, (float)gh, 0, 0, 0, 0 };
        if (!sharp) { fsr_draw(s_fsr_easu, slot, out_rtv, ow, oh, ke, x0); return 1; }
        if (!fsr_tmp(ow, oh)) return 0;
        aa_barrier(s_fsr_tmp, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        fsr_draw(s_fsr_easu, slot, fsr_tmp_rtv(), ow, oh, ke, 0);
        aa_barrier(s_fsr_tmp, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        srv_write(slot + 1, s_fsr_tmp, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        const float kr[12] = { 0, 0, 0, 0, 0, 0, 0, 0, con, (float)(ow - 1), (float)(oh - 1), 0 };
        fsr_draw(s_fsr_rcas, slot + 1, out_rtv, ow, oh, kr, x0);
        return 1;
    }
    /* RCAS alone, at the window's size (where it was made to run): if the
     * sizes differ, the usual bilinear scale goes first, into the intermediate */
    const float kr[12] = { 0, 0, 0, 0, 0, 0, 0, 0, con, (float)(ow - 1), (float)(oh - 1), 0 };
    if (gw == ow && gh == oh) { fsr_draw(s_fsr_rcas, slot, out_rtv, ow, oh, kr, x0); return 1; }
    if (!bilinear || !fsr_tmp(ow, oh)) return 0;
    aa_barrier(s_fsr_tmp, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    fsr_draw(bilinear, slot, fsr_tmp_rtv(), ow, oh, kz, 0);
    aa_barrier(s_fsr_tmp, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    srv_write(slot + 1, s_fsr_tmp, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    fsr_draw(s_fsr_rcas, slot + 1, out_rtv, ow, oh, kr, x0);
    return 1;
}

/* DISPLAY_SCALE=0: RCAS in place on fi's backbuffer (RENDER_TARGET on entry
 * and on exit), after AA and before the overlays. */
static void fsr_backbuffer(u32 fi)
{
    fsr_load_env();
    const int sharp = s_sharp;
    if (!sharp || !fsr_ready()) return;
    const u32 w = rs_dim(s_d3d.width, s_rs), h = rs_dim(s_d3d.height, s_rs);
    if (!w || !h || !fsr_tmp(w, h)) return;
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    ID3D12Resource* bb = s_d3d.render_targets[fi];
    aa_barrier(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    aa_barrier(s_fsr_tmp, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->lpVtbl->CopyResource(cl, s_fsr_tmp, bb);
    aa_barrier(s_fsr_tmp, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    aa_barrier(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &bb_rtv);
    bb_rtv.ptr += (u64)fi * s_d3d.rtv_descriptor_size;
    const float kr[12] = { 0, 0, 0, 0, 0, 0, 0, 0, fsr_strength(sharp), (float)(w - 1), (float)(h - 1), 0 };
    fsr_draw(s_fsr_rcas, AA_SRV_BASE + 5, bb_rtv, w, h, kr, 0);
}

static void display_resize(u32 w, u32 h)
{
    wait_for_gpu();
    rsx_fsr31_wait();
    for (u32 i = 0; i < FRAME_COUNT; i++)
        if (s_disp_bufs[i]) { s_disp_bufs[i]->lpVtbl->Release(s_disp_bufs[i]); s_disp_bufs[i] = NULL; }
    HRESULT hr = s_d3d.swap_chain->lpVtbl->ResizeBuffers(s_d3d.swap_chain, FRAME_COUNT, w, h,
        DXGI_FORMAT_R8G8B8A8_UNORM, s_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (SUCCEEDED(hr)) { s_disp_w = w; s_disp_h = h; }
    else printf("[D3D12] display resize to %ux%u failed (0x%08lX)\n", w, h, hr);
    display_bufs_get();
}

static void display_present(u32 fi)
{
    u32 w = s_disp_w, h = s_disp_h;
    if (window_client(&w, &h) && (w != s_disp_w || h != s_disp_h)) display_resize(w, h);
    w = s_disp_w; h = s_disp_h;
    const u32 di = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);
    ID3D12Resource *game = s_d3d.render_targets[fi], *out = s_disp_bufs[di];
    if (!game || !out) return;
    if (!s_disp_pso && !s_disp_failed) {
        size_t n = sizeof k_aa_common + sizeof k_disp_hlsl;
        char* src = (char*)malloc(n);
        if (src && aa_init_rs()) {
            snprintf(src, n, "%s%s", k_aa_common, k_disp_hlsl);
            s_disp_pso = aa_pso(src, strlen(src), "vs_up", "ps_up");
        }
        free(src);
        if (!s_disp_pso) { s_disp_failed = 1; printf("[D3D12] display pass unavailable: game copied unscaled\n"); }
    }
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    s_disp_rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_disp_rtv, &rtv);
    rtv.ptr += (u64)di * s_d3d.rtv_descriptor_size;
    aa_barrier(out, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (s_disp_pso) {
        aa_barrier(game, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const u32 slot = DISPLAY_SRV_BASE + fi * 5;
        srv_write(slot, game, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        cl->lpVtbl->SetDescriptorHeaps(cl, 1, &s_d3d.srv_heap);
        cl->lpVtbl->SetGraphicsRootSignature(cl, s_aa_rs);
        const float k[12] = {0};
        cl->lpVtbl->SetGraphicsRoot32BitConstants(cl, 0, 12, k, 0);
        D3D12_GPU_DESCRIPTOR_HANDLE gh;
        s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
        gh.ptr += (u64)slot * s_d3d.srv_inc;
        cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh);
        D3D12_VIEWPORT vp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        D3D12_RECT sc = {0, 0, (LONG)w, (LONG)h};
        cl->lpVtbl->RSSetViewports(cl, 1, &vp);
        cl->lpVtbl->RSSetScissorRects(cl, 1, &sc);
        cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (!fsr_display(slot, rtv, w, h, s_disp_pso)) aa_draw(s_disp_pso, rtv, 0);
        aa_barrier(game, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PRESENT);
    } else {   /* no shader: the game image in the corner, unscaled */
        const float z[4] = {0, 0, 0, 1};
        cl->lpVtbl->ClearRenderTargetView(cl, rtv, z, 0, NULL);
        aa_barrier(out, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        aa_barrier(game, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION sl = {0}, dl = {0};
        sl.pResource = game; dl.pResource = out;
        sl.Type = dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX box = {0, 0, 0, rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs), 1};
        if (box.right > w) box.right = w;
        if (box.bottom > h) box.bottom = h;
        cl->lpVtbl->CopyTextureRegion(cl, &dl, 0, 0, 0, &sl, &box);
        aa_barrier(game, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        aa_barrier(out, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    if(s_fsr_ready)rsx_fsr31_hudless(cl,out,D3D12_RESOURCE_STATE_RENDER_TARGET);
    motion_capture_record(out,w,h);
    shot_record(out, w, h);   /* the shown image, before the port's overlays */
    s_qb_display = 1; s_qb_rtv = rtv; s_qb_w = w; s_qb_h = h;
    perf_overlay_draw(fi);
    if(s_fsr_ready)rsx_fsr31_present_color(cl,out,D3D12_RESOURCE_STATE_RENDER_TARGET);
    s_qb_display = 0;
    aa_barrier(out, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}

static void composite_present(u32 fi)
{
    u32 want = s_composite_src;
    s_composite_src = 0;
    if (!want) return;
    /* want == 2 means "whatever this frame's draws targeted" -- the one thing
     * guaranteed to hold geometry, and immune to the offset collision above. */
    int i;
    if (want == 2u) { i = s_last_drawn_slot; want = s_last_drawn_off; }
    else            { i = off_rt_find(want); }
    if (i < 0 || !s_d3d.off_rt[i].res) return;
    OffRT* r = &s_d3d.off_rt[i];
    /* Same-format copy only; a half-float source would need a converting draw. */
    if (r->dxgi != DXGI_FORMAT_R8G8B8A8_UNORM) {
        static int w1 = 0;
        if (w1++ < 2)
            fprintf(stderr, "[COMPOSITE] RT 0x%08X is dxgi=%u, not R8G8B8A8 -- needs a draw%c",
                    want, r->dxgi, 10);
        return;
    }
    u32 cw = r->w < s_d3d.width  ? r->w : s_d3d.width;
    u32 ch = r->h < s_d3d.height ? r->h : s_d3d.height;
    if (!cw || !ch) return;
    { static int n = 0;
      if (n++ < 3) fprintf(stderr, "[COMPOSITE] RT 0x%08X %ux%u -> backbuffer%c",
                           want, cw, ch, 10); }

    D3D12_RESOURCE_BARRIER b[2] = {0};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource   = r->res;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[0].Transition.StateBefore = r->st;
    b[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1] = b[0];
    b[1].Transition.pResource   = s_d3d.render_targets[fi];
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, b);

    D3D12_TEXTURE_COPY_LOCATION dl = {0}, sl = {0};
    dl.pResource = s_d3d.render_targets[fi];
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dl.SubresourceIndex = 0;
    sl.pResource = r->res;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sl.SubresourceIndex = 0;
    D3D12_BOX box = {0};
    box.left = 0; box.top = 0; box.front = 0;
    box.right = rs_dim(cw, r->sc); box.bottom = rs_dim(ch, r->sc); box.back = 1;
    if (box.right > rs_dim(s_d3d.width, s_rs))   box.right  = rs_dim(s_d3d.width, s_rs);
    if (box.bottom > rs_dim(s_d3d.height, s_rs)) box.bottom = rs_dim(s_d3d.height, s_rs);
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dl, 0, 0, 0, &sl, &box);

    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.StateAfter  = r->st;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, b);
}


static void guest_fb_present(u32 fi)
{
    static int inited = 0;
    static u32 off, gw, gh, gpitch, up_pitch;
    static ID3D12Resource* up = NULL;
    if (!inited) {
        inited = 1;
        const char* e = getenv("GCM_GUEST_FB");
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (!e || sscanf(e, "%i,%u,%u,%u", &a, &b, &c, &d) != 4 || !b || !c) return;
        off = a; gw = b; gh = c; gpitch = d;
        up_pitch = (gw * 4u + 255u) & ~255u;      /* D3D12 wants 256-byte rows */
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width  = (u64)up_pitch * gh; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&up)))
            up = NULL;
        fprintf(stderr, "[GUESTFB] presenting guest 0x%08X %ux%u pitch %u -> backbuffer%c",
                off, gw, gh, gpitch, 10);
    }
    if (!up) return;
    { extern u8* vm_base;
      if (!vm_base) return;
      /* RSX local memory is mapped at guest 0xC0000000. */
      const u8* srow = vm_base + 0xC0000000u + off;
      void* m = NULL; D3D12_RANGE nr = {0, 0};
      if (FAILED(up->lpVtbl->Map(up, 0, &nr, &m)) || !m) return;
      for (u32 y = 0; y < gh; y++) {
          const u8* s = srow + (size_t)y * gpitch;
          u8* dp = (u8*)m + (size_t)y * up_pitch;
          for (u32 x = 0; x < gw; x++) {   /* guest B,G,R,A -> backbuffer R,G,B,A */
              dp[x*4+0] = s[x*4+2]; dp[x*4+1] = s[x*4+1];
              dp[x*4+2] = s[x*4+0]; dp[x*4+3] = 0xFF;
          }
      }
      D3D12_RANGE wr = {0, (SIZE_T)up_pitch * gh};
      up->lpVtbl->Unmap(up, 0, &wr); }

    D3D12_RESOURCE_BARRIER gb = {0};
    gb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    gb.Transition.pResource   = s_d3d.render_targets[fi];
    gb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    gb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    gb.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &gb);

    D3D12_TEXTURE_COPY_LOCATION dl = {0}, sl = {0};
    dl.pResource = s_d3d.render_targets[fi];
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dl.SubresourceIndex = 0;
    sl.pResource = up; sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sl.PlacedFootprint.Offset = 0;
    sl.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    sl.PlacedFootprint.Footprint.Width    = gw;
    sl.PlacedFootprint.Footprint.Height   = gh;
    sl.PlacedFootprint.Footprint.Depth    = 1;
    sl.PlacedFootprint.Footprint.RowPitch = up_pitch;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dl, 0, 0, 0, &sl, NULL);

    gb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    gb.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &gb);
}


/* PS3_TIMELINE: what the GPU is asked to do per frame. The command list's
 * method table is replaced, in this process only, by a copy whose chosen
 * entries count and forward; no call site changes. Reported every 5 s as
 * [GPUCMD]. A GPU that idles between small draws is usually told to stop:
 * barriers, copies, target and pipeline changes. */
enum { GC_DRAW, GC_BARRIER_CALLS, GC_BARRIERS, GC_COPY_TEX, GC_COPY_BUF, GC_COPY_RES, GC_RT_BIND,
       GC_PSO, GC_CLEAR_RT, GC_CLEAR_DS, GC_VIEWPORT, GC_N };
static volatile LONG64 s_gc[GC_N];
static ID3D12GraphicsCommandListVtbl s_gc_vtbl, s_gc_orig;
static void STDMETHODCALLTYPE gc_DrawInstanced(ID3D12GraphicsCommandList* t, UINT a, UINT b, UINT c, UINT d)
{ s_gc[GC_DRAW]++; s_gc_orig.DrawInstanced(t, a, b, c, d); }
static void STDMETHODCALLTYPE gc_ResourceBarrier(ID3D12GraphicsCommandList* t, UINT n, const D3D12_RESOURCE_BARRIER* b)
{ s_gc[GC_BARRIER_CALLS]++; s_gc[GC_BARRIERS] += n; s_gc_orig.ResourceBarrier(t, n, b); }
static void STDMETHODCALLTYPE gc_CopyTextureRegion(ID3D12GraphicsCommandList* t, const D3D12_TEXTURE_COPY_LOCATION* d,
    UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* sr, const D3D12_BOX* box)
{ s_gc[GC_COPY_TEX]++; s_gc_orig.CopyTextureRegion(t, d, x, y, z, sr, box); }
static void STDMETHODCALLTYPE gc_CopyBufferRegion(ID3D12GraphicsCommandList* t, ID3D12Resource* d, UINT64 doff,
    ID3D12Resource* sr, UINT64 soff, UINT64 n)
{ s_gc[GC_COPY_BUF]++; s_gc_orig.CopyBufferRegion(t, d, doff, sr, soff, n); }
static void STDMETHODCALLTYPE gc_CopyResource(ID3D12GraphicsCommandList* t, ID3D12Resource* d, ID3D12Resource* sr)
{ s_gc[GC_COPY_RES]++; s_gc_orig.CopyResource(t, d, sr); }
static void STDMETHODCALLTYPE gc_OMSetRenderTargets(ID3D12GraphicsCommandList* t, UINT n,
    const D3D12_CPU_DESCRIPTOR_HANDLE* rt, BOOL single, const D3D12_CPU_DESCRIPTOR_HANDLE* ds)
{ s_gc[GC_RT_BIND]++; s_gc_orig.OMSetRenderTargets(t, n, rt, single, ds); }
static void STDMETHODCALLTYPE gc_SetPipelineState(ID3D12GraphicsCommandList* t, ID3D12PipelineState* ps)
{ s_gc[GC_PSO]++; s_gc_orig.SetPipelineState(t, ps); }
static void STDMETHODCALLTYPE gc_ClearRenderTargetView(ID3D12GraphicsCommandList* t, D3D12_CPU_DESCRIPTOR_HANDLE h,
    const FLOAT c[4], UINT n, const D3D12_RECT* r)
{ s_gc[GC_CLEAR_RT]++; s_gc_orig.ClearRenderTargetView(t, h, c, n, r); }
static void STDMETHODCALLTYPE gc_ClearDepthStencilView(ID3D12GraphicsCommandList* t, D3D12_CPU_DESCRIPTOR_HANDLE h,
    D3D12_CLEAR_FLAGS f, FLOAT dp, UINT8 st, UINT n, const D3D12_RECT* r)
{ s_gc[GC_CLEAR_DS]++; s_gc_orig.ClearDepthStencilView(t, h, f, dp, st, n, r); }
static void STDMETHODCALLTYPE gc_RSSetViewports(ID3D12GraphicsCommandList* t, UINT n, const D3D12_VIEWPORT* v)
{ s_gc[GC_VIEWPORT]++; s_gc_orig.RSSetViewports(t, n, v); }
static int gc_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_TIMELINE"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}
static void gc_install(ID3D12GraphicsCommandList* cl)
{
    /* The runtime sets the list's table again on Reset: done after each. */
    if (!gc_on() || !cl || cl->lpVtbl == &s_gc_vtbl) return;
    static int said;
    s_gc_orig = *cl->lpVtbl; s_gc_vtbl = s_gc_orig;
    s_gc_vtbl.DrawInstanced = gc_DrawInstanced;
    s_gc_vtbl.ResourceBarrier = gc_ResourceBarrier;
    s_gc_vtbl.CopyTextureRegion = gc_CopyTextureRegion;
    s_gc_vtbl.CopyBufferRegion = gc_CopyBufferRegion;
    s_gc_vtbl.CopyResource = gc_CopyResource;
    s_gc_vtbl.OMSetRenderTargets = gc_OMSetRenderTargets;
    s_gc_vtbl.SetPipelineState = gc_SetPipelineState;
    s_gc_vtbl.ClearRenderTargetView = gc_ClearRenderTargetView;
    s_gc_vtbl.ClearDepthStencilView = gc_ClearDepthStencilView;
    s_gc_vtbl.RSSetViewports = gc_RSSetViewports;
    cl->lpVtbl = &s_gc_vtbl;
    if (!said++) fprintf(stderr, "[GPUCMD] counting command-list calls (PS3_TIMELINE)%c", 10);
}
/* GPU time of a frame: timestamps at the start and the end of its command
 * lists, read back after the next frame's GPU wait. */
static ID3D12QueryHeap* s_ts_heap;
static ID3D12Resource* s_ts_rb;
static int s_ts_have[FRAME_COUNT];
static double s_ts_ms_acc; static u32 s_ts_n;
/* CPU clock (QPC) at the first submit and at the end of recording, per slot,
 * to place the GPU's own start and end on the same clock. */
static LONGLONG s_ts_cpu_exec[FRAME_COUNT], s_ts_cpu_end[FRAME_COUNT];
static double s_ts_lat_acc, s_ts_tail_acc;
static LONGLONG ts_qpc(void) { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static void ts_note_exec(u32 fi) { if (gc_on() && !s_ts_cpu_exec[fi]) s_ts_cpu_exec[fi] = ts_qpc(); }
static void ts_begin(u32 fi)
{
    if (!gc_on()) return;
    if (!s_ts_heap) {
        D3D12_QUERY_HEAP_DESC qd = {0}; qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = 2 * FRAME_COUNT;
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {0}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 16 * FRAME_COUNT;
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateQueryHeap(s_d3d.device, &qd, &IID_ID3D12QueryHeap, (void**)&s_ts_heap)) ||
            FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_ts_rb))) { s_ts_heap = NULL; return; }
    }
    /* The previous use of this slot has finished: the frame began with a GPU wait. */
    if (s_ts_have[fi]) {
        UINT64* t = NULL; D3D12_RANGE r = { fi * 16u, fi * 16u + 16u };
        if (SUCCEEDED(s_ts_rb->lpVtbl->Map(s_ts_rb, 0, &r, (void**)&t)) && t) {
            UINT64 freq = 0; s_d3d.cmd_queue->lpVtbl->GetTimestampFrequency(s_d3d.cmd_queue, &freq);
            const UINT64 a = t[fi * 2], b = t[fi * 2 + 1];
            UINT64 g0 = 0, c0 = 0; LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
            if (freq && b > a && SUCCEEDED(s_d3d.cmd_queue->lpVtbl->GetClockCalibration(s_d3d.cmd_queue, &g0, &c0))) {
                s_ts_ms_acc += (double)(b - a) * 1000.0 / (double)freq; s_ts_n++;
                /* GPU stamps on the CPU clock, in ms relative to the calibration point. */
                const double ga = ((double)(INT64)(a - g0) / (double)freq) * 1000.0;
                const double gb = ((double)(INT64)(b - g0) / (double)freq) * 1000.0;
                const double ce = ((double)(s_ts_cpu_exec[fi] - (LONGLONG)c0) / (double)qf.QuadPart) * 1000.0;
                const double cr = ((double)(s_ts_cpu_end[fi] - (LONGLONG)c0) / (double)qf.QuadPart) * 1000.0;
                s_ts_lat_acc += ga - ce; s_ts_tail_acc += gb - cr;
            }
            D3D12_RANGE w = { 0, 0 }; s_ts_rb->lpVtbl->Unmap(s_ts_rb, 0, &w);
        }
        s_ts_have[fi] = 0;
    }
    s_ts_cpu_exec[fi] = 0;
    s_d3d.cmd_list->lpVtbl->EndQuery(s_d3d.cmd_list, s_ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, fi * 2);
}
static void ts_end(u32 fi)
{
    if (!gc_on() || !s_ts_heap) return;
    s_d3d.cmd_list->lpVtbl->EndQuery(s_d3d.cmd_list, s_ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, fi * 2 + 1);
    s_d3d.cmd_list->lpVtbl->ResolveQueryData(s_d3d.cmd_list, s_ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, fi * 2, 2, s_ts_rb, fi * 16u);
    s_ts_have[fi] = 1;
    s_ts_cpu_end[fi] = ts_qpc();
    ts_note_exec(fi);
}
static void gc_frame(void)
{
    if (!gc_on()) return;
    static ULONGLONG t0; static u32 frames;
    const ULONGLONG now = GetTickCount64();
    if (!t0) t0 = now;
    frames++;
    if (now - t0 < 5000) return;
    LONG64 v[GC_N];
    for (int i = 0; i < GC_N; i++) v[i] = InterlockedExchange64(&s_gc[i], 0);
    const double f = frames ? (double)frames : 1.0;
    fprintf(stderr, "[GPUCMD] per frame: draws %.0f | barriers %.0f in %.0f calls | copies tex %.0f buf %.0f res %.0f"
                    " | rt-binds %.0f | pso %.0f | clears rt %.0f ds %.0f | viewports %.0f%c",
            v[GC_DRAW] / f, v[GC_BARRIERS] / f, v[GC_BARRIER_CALLS] / f, v[GC_COPY_TEX] / f, v[GC_COPY_BUF] / f,
            v[GC_COPY_RES] / f, v[GC_RT_BIND] / f, v[GC_PSO] / f, v[GC_CLEAR_RT] / f, v[GC_CLEAR_DS] / f,
            v[GC_VIEWPORT] / f, 10);
    if (s_ts_n) fprintf(stderr, "[GPUCMD] GPU per frame: busy span %.2f ms | starts %.2f ms after the first submit"
                                " | ends %.2f ms after recording ended%c",
                        s_ts_ms_acc / s_ts_n, s_ts_lat_acc / s_ts_n, s_ts_tail_acc / s_ts_n, 10);
    s_ts_ms_acc = 0; s_ts_n = 0; s_ts_lat_acc = 0; s_ts_tail_acc = 0;
    t0 = now; frames = 0;
}

#include "rsx_frame_gpu_probe.h"
enum {FP_ENTRY,FP_POSE,FP_BUILD,FP_SUBMIT,FP_READBACK,FP_PRESENT,FP_FENCE,FP_BOOKKEEP,FP_TAIL,FP_STAGES};
static struct {
    int active;double start,mark,last_start,interval,prepass,stage[FP_STAGES];u64 thread_start;
} s_frame_profile;
static RsxFrameGpuProbe s_frame_gpu;
static int s_frame_gpu_init;
static int s_split_used;
enum {FD_SETUP,FD_SHADOW,FD_OFFSCREEN,FD_MAIN,FD_TRANSFER,FD_POST,FD_DEPTH,FD_LEGACY};
static struct {int active,tag;double mark,cpu[RSX_FRAME_GPU_TAGS],totals[RSX_FRAME_GPU_TAGS];u32 samples;} s_frame_detail;
static int frame_detail_on(void) {
    static int on=-1;if(on<0){const char* e=getenv("CV_FRAME_DETAIL");on=e&&*e=='1';}return on;
}
static void frame_detail_scope(int tag,u32 target,u32 width,u32 height) {
    if(!s_frame_detail.active||!s_frame_gpu.active)return;
    rsx_frame_gpu_scope(&s_frame_gpu,s_d3d.cmd_list,(u32)tag,target,width,height);
    if(tag==s_frame_detail.tag)return;
    double now=pose_time();s_frame_detail.cpu[s_frame_detail.tag]+=now-s_frame_detail.mark;
    s_frame_detail.mark=now;s_frame_detail.tag=tag;
}
static void frame_detail_tag(int tag) {frame_detail_scope(tag,0,0,0);}
static void frame_detail_end_record(void) {
    if(!s_frame_detail.active)return;
    double now=pose_time();s_frame_detail.cpu[s_frame_detail.tag]+=now-s_frame_detail.mark;
    if(s_frame_gpu.active&&!s_cap_steps_on){for(int i=0;i<RSX_FRAME_GPU_TAGS;i++)s_frame_detail.totals[i]+=s_frame_detail.cpu[i];s_frame_detail.samples++;}
    s_frame_detail.active=0;
}

static int frame_profile_on(void) {
    static int on=-1;
    if(on<0) {
        const char *e=getenv("CV_FRAME_PROFILE"),*fps=getenv("CV_FPS_TEST_60"),*interp=getenv("CV_RENDER_INTERPOLATION");
        on=e&&!strcmp(e,"1")&&fps&&!strcmp(fps,"1")&&interp&&!strcmp(interp,"1");
        if(on)fprintf(stderr,"[FRAME-PROFILE] v38 sampled GPU timestamps (1/30 presents), optional classified ranges; no extra waits\n");
    }
    return on;
}
static int producer_profile_on(void) {
    static int on=-1;
    if(on<0){const char* e=getenv("CV_PRODUCER_PROFILE");on=e&&!strcmp(e,"1");}
    return on;
}
static u64 frame_cpu_ticks(int process) {
    FILETIME created,exited,kernel,user;BOOL ok=process?GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user):GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
    if(!ok)return 0;return (((u64)kernel.dwHighDateTime<<32)|kernel.dwLowDateTime)+(((u64)user.dwHighDateTime<<32)|user.dwLowDateTime);
}
static void frame_profile_begin(void) {
    s_frame_profile.active=frame_profile_on()&&s_rb.present;
    if(!s_frame_profile.active)return;
    s_frame_profile.thread_start=frame_detail_on()?frame_cpu_ticks(0):0;
    double now=pose_time();s_frame_profile.start=s_frame_profile.mark=now;
    s_frame_profile.interval=s_frame_profile.last_start?now-s_frame_profile.last_start:0;
    s_frame_profile.last_start=now;s_frame_profile.prepass=0;
    memset(s_frame_profile.stage,0,sizeof s_frame_profile.stage);
}
static void frame_profile_mark(int stage) {
    if(!s_frame_profile.active)return;
    double now=pose_time();s_frame_profile.stage[stage]+=now-s_frame_profile.mark;s_frame_profile.mark=now;
}
static void frame_profile_gpu_begin(u32 fi) {
    if(!frame_profile_on())return;
    if(!s_frame_gpu_init&&s_frame_profile.active) {
        s_frame_gpu_init=rsx_frame_gpu_init(&s_frame_gpu,s_d3d.device,s_d3d.cmd_queue,FRAME_COUNT)?1:-1;
        if(s_frame_gpu_init<0)fprintf(stderr,"[FRAME-PROFILE] GPU timestamps unavailable; CPU timings retained\n");
    }
    if(s_frame_gpu_init!=1)return;
    /* Called only after the existing wait_for_gpu; no new waits/readbacks. */
    rsx_frame_gpu_collect(&s_frame_gpu,s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence));
    static u32 presents;int sample=s_frame_profile.active&&++presents%30==0;
    rsx_frame_gpu_begin_frame(&s_frame_gpu,fi,sample&&!s_cap_steps_on);
    rsx_frame_gpu_begin_list(&s_frame_gpu,s_d3d.cmd_list);
    rsx_frame_gpu_context(&s_frame_gpu,s_d3d.width,s_d3d.height,s_rs);
    s_frame_detail.active=frame_detail_on()&&s_frame_gpu.active;s_frame_detail.tag=FD_SETUP;
    if(s_frame_detail.active){memset(s_frame_detail.cpu,0,sizeof s_frame_detail.cpu);s_frame_detail.mark=pose_time();}
}
static void frame_profile_finish(void) {
    if(!s_frame_profile.active)return;
    frame_profile_mark(FP_TAIL);s_frame_profile.active=0;
    /* F12 intentionally stalls. Exclude it and the following start interval. */
    if(s_cap_steps_on){s_frame_profile.last_start=0;return;}
    static double sums[FP_STAGES],prepass,interval,producer,backpressure,last_report,ops,parts,pipelines,vtex_reused,vtex_written,batch_groups,batch_saved;
    static u32 frames,intervals,thread_frames;static double thread_cpu;static u64 process_ticks;
    static double producer_cpu_sum,producer_wall_sum;static u32 producer_valid,producer_invalid;
    if(producer_profile_on()) {
        if(s_rb.profile_producer_valid){producer_cpu_sum+=s_rb.profile_producer_cpu;producer_wall_sum+=s_rb.profile_producer;producer_valid++;}
        else producer_invalid++;
    }
    if(s_frame_profile.thread_start){u64 end=frame_cpu_ticks(0);if(end>=s_frame_profile.thread_start){thread_cpu+=(double)(end-s_frame_profile.thread_start)*1e-7;thread_frames++;}}
    double now=pose_time();if(!last_report)last_report=now;
    for(int i=0;i<FP_STAGES;i++)sums[i]+=s_frame_profile.stage[i];
    prepass+=s_frame_profile.prepass;producer+=s_rb.profile_producer;backpressure+=s_rb.profile_backpressure;
    /* render_frame_impl clears s_rb.count before returning. This snapshot
     * retains the submitted operation count, including filtered batches. */
    ops+=s_dbg_last_draws;parts+=s_split_used+1;frames++;
    batch_groups+=s_shadow_batch_groups;batch_saved+=s_shadow_batch_saved;
    pipelines+=s_prep_pipelines;vtex_reused+=s_prep_vtex_reused;vtex_written+=s_prep_vtex_written;
    if(s_frame_profile.interval>0){interval+=s_frame_profile.interval;intervals++;}
    if(now-last_report<5)return;
    double scale=1000.0/frames,total=0;for(int i=0;i<FP_STAGES;i++)total+=sums[i];
    fprintf(stderr,"[FRAME-PERF] %u presents interval_ms=%.3f render_wall=%.3f entry_wait=%.3f pose=%.3f build=%.3f (prepass=%.3f) submit=%.3f readback_diag=%.3f present=%.3f fence=%.3f bookkeeping=%.3f tail_wait=%.3f producer_span=%.3f backpressure=%.3f ops=%.0f parts=%.1f\n",
        frames,intervals?interval*1000.0/intervals:0,total*scale,sums[FP_ENTRY]*scale,sums[FP_POSE]*scale,
        sums[FP_BUILD]*scale,prepass*scale,sums[FP_SUBMIT]*scale,sums[FP_READBACK]*scale,sums[FP_PRESENT]*scale,
        sums[FP_FENCE]*scale,sums[FP_BOOKKEEP]*scale,sums[FP_TAIL]*scale,producer*scale,backpressure*scale,ops/frames,parts/frames);
    if(producer_profile_on()) {
        double unit=producer_valid?1000.0/producer_valid:0;
        double remainder=producer_wall_sum-producer_cpu_sum;
        fprintf(stderr,"[PRODUCER-PERF] valid_spans=%u invalid_spans=%u wall_ms=%.3f issuing_thread_cpu_ms=%.3f wall_minus_cpu_ms=%.3f (same issuing thread, release-to-ready only; excludes wait-render; remainder includes waits/scheduling, not pure idle; overlaps renderer)\n",
            producer_valid,producer_invalid,producer_wall_sum*unit,producer_cpu_sum*unit,(remainder>0?remainder:0)*unit);
        producer_cpu_sum=producer_wall_sum=0;producer_valid=producer_invalid=0;
    }
    fprintf(stderr,"[FRAME-PREP] ready_pipeline_reuse=%.1f vtex_windows_reused=%.1f vtex_windows_written=%.1f (counts/present)\n",pipelines/frames,vtex_reused/frames,vtex_written/frames);
    fprintf(stderr,"[PREPASS-REPEAT] ready_reused=%.1f (per present; exact options/program, live PSO/VS guards; CV_PREP_REPEAT=0 disables)\n",(double)s_prepass_repeat_reused/frames);
    s_prepass_repeat_reused=0;
    fprintf(stderr,"[SHADOW-BATCH] groups=%.1f draws_saved=%.1f (per present; DXC, identical state/constants, original triangles retained)\n",batch_groups/frames,batch_saved/frames);
    if(s_frame_gpu.samples)fprintf(stderr,"[FRAME-GPU] samples=%u lists_ms=%.3f span_ms=%.3f gaps_ms=%.3f invalid=%u (sampled direct lists; overlaps CPU; not added to FRAME-PERF)\n",
        s_frame_gpu.samples,s_frame_gpu.lists_ms/s_frame_gpu.samples,s_frame_gpu.span_ms/s_frame_gpu.samples,
        s_frame_gpu.gaps_ms/s_frame_gpu.samples,s_frame_gpu.invalid);
    else fprintf(stderr,"[FRAME-GPU] samples=0 status=%s\n",s_frame_gpu_init==1?"pending":"unavailable");
    if(frame_detail_on()) {
        u64 process_now=frame_cpu_ticks(1);
        fprintf(stderr,"[CPU-THREAD] renderer_cpu_ms=%.3f renderer_samples=%u whole_process_cpu_ms_per_present=%.3f process_window_ready=%d (process includes concurrent threads; not added to frame wall time)\n",
            thread_frames?thread_cpu*1000.0/thread_frames:0,thread_frames,process_ticks&&process_now>=process_ticks?(double)(process_now-process_ticks)*.0001/frames:0,process_ticks!=0);
        process_ticks=process_now;thread_cpu=0;thread_frames=0;
        float scale=0;memcpy(&scale,&s_frame_gpu.result_scale_bits,4);
        fprintf(stderr,"[FRAME-CONTEXT] window=%ux%u display=%ux%u scale=%.3f sampled_base=%ux%u sampled_scale=%.3f mixed_resolution=%u dropped_samples=%u\n",
            s_d3d.width,s_d3d.height,s_disp_w,s_disp_h,(double)s_rs,s_frame_gpu.result_width,s_frame_gpu.result_height,(double)scale,s_frame_gpu.context_mixed,s_frame_gpu.overflow);
        if(s_frame_gpu.samples)fprintf(stderr,"[GPU-RANGES] samples=%u setup_upload=%.3f shadow_shader=%.3f other_offscreen=%.3f main_target=%.3f copy_clear=%.3f post_display=%.3f depth_convert=%.3f legacy=%.3f (ms/sample; classified ranges include barriers; overlap CPU)\n",
            s_frame_gpu.samples,s_frame_gpu.tag_ms[0]/s_frame_gpu.samples,s_frame_gpu.tag_ms[1]/s_frame_gpu.samples,s_frame_gpu.tag_ms[2]/s_frame_gpu.samples,s_frame_gpu.tag_ms[3]/s_frame_gpu.samples,s_frame_gpu.tag_ms[4]/s_frame_gpu.samples,s_frame_gpu.tag_ms[5]/s_frame_gpu.samples,s_frame_gpu.tag_ms[6]/s_frame_gpu.samples,s_frame_gpu.tag_ms[7]/s_frame_gpu.samples);
        if(s_frame_detail.samples){double unit=1000.0/s_frame_detail.samples;
            fprintf(stderr,"[CPU-RECORD-RANGES] samples=%u setup_upload=%.3f shadow_shader=%.3f other_offscreen=%.3f main_target=%.3f copy_clear=%.3f post_display=%.3f depth_convert=%.3f legacy=%.3f (ms/sample; subset of build, includes prepass; excludes pose and fence waits)\n",
                s_frame_detail.samples,s_frame_detail.totals[0]*unit,s_frame_detail.totals[1]*unit,s_frame_detail.totals[2]*unit,s_frame_detail.totals[3]*unit,s_frame_detail.totals[4]*unit,s_frame_detail.totals[5]*unit,s_frame_detail.totals[6]*unit,s_frame_detail.totals[7]*unit);}
        if(s_frame_gpu.samples) {
            u8 shown[RSX_FRAME_GPU_SURFACES]={0};
            for(u32 rank=0;rank<8&&rank<s_frame_gpu.surface_count;rank++) {
                int best=-1;for(u32 i=0;i<s_frame_gpu.surface_count;i++)if(!shown[i]&&(best<0||s_frame_gpu.surfaces[i].ms>s_frame_gpu.surfaces[best].ms))best=(int)i;
                if(best<0)break;shown[best]=1;const RsxFrameGpuSurface* surface=&s_frame_gpu.surfaces[best];
                fprintf(stderr,"[GPU-TARGET] rank=%u offset=0x%08X host_size=%ux%u category=%u gpu_ms=%.3f ranges=%u samples=%u\n",rank+1,surface->key.offset,surface->key.width,surface->key.height,surface->key.tag,surface->ms/s_frame_gpu.samples,surface->ranges,s_frame_gpu.samples);
            }
            fprintf(stderr,"[GPU-TARGET-OTHER] ms=%.3f distinct_targets=%u overflow_ranges=%u (unattributed/over-limit; target timings are a subset of GPU-RANGES)\n",s_frame_gpu.surface_other_ms/s_frame_gpu.samples,s_frame_gpu.surface_count,s_frame_gpu.surface_overflow);
        }
        memset(s_frame_detail.totals,0,sizeof s_frame_detail.totals);s_frame_detail.samples=0;
    }
    s_frame_gpu.surface_count=s_frame_gpu.surface_overflow=0;s_frame_gpu.surface_other_ms=0;
    memset(s_frame_gpu.tag_ms,0,sizeof s_frame_gpu.tag_ms);s_frame_gpu.context_mixed=s_frame_gpu.overflow=0;
    memset(sums,0,sizeof sums);prepass=interval=producer=backpressure=ops=parts=pipelines=vtex_reused=vtex_written=batch_groups=batch_saved=0;frames=intervals=0;last_report=now;
    s_frame_gpu.samples=s_frame_gpu.invalid=0;s_frame_gpu.lists_ms=s_frame_gpu.span_ms=s_frame_gpu.gaps_ms=0;
}

/* RSX_SUBMIT_SPLIT=<N> (default 1024, 0 = off): hand the frame to the GPU
 * every N draws instead of once at the end. Recording a heavy frame took
 * ~12 ms and the GPU ~10 ms, one after the other; split, the GPU draws the
 * first part while the rest is recorded. Each part gets its own allocator,
 * reset at the next use of this frame index, after the frame's GPU wait. */
#define SPLIT_ALLOCS 32
static ID3D12CommandAllocator* s_split_alloc[FRAME_COUNT][SPLIT_ALLOCS];
static int submit_split_every(void)
{
    static int n = -1;
    if (n < 0) { const char* e = getenv("RSX_SUBMIT_SPLIT"); n = e ? atoi(e) : 1024; if (n < 0) n = 0; }
    return n;
}
/* Submit what is recorded and reopen the list on a fresh allocator. Returns
 * 0 (nothing changed) when no allocator is left for this frame. */
static int submit_split(u32 fi)
{
    if (s_split_used >= SPLIT_ALLOCS) return 0;
    ID3D12CommandAllocator** a = &s_split_alloc[fi][s_split_used];
    if (!*a && FAILED(s_d3d.device->lpVtbl->CreateCommandAllocator(s_d3d.device,
            D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)a))) { *a = NULL; return 0; }
    s_split_used++;
    ts_note_exec(fi);
    rsx_frame_gpu_end_list(&s_frame_gpu,s_d3d.cmd_list);
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cl[] = { (ID3D12CommandList*)s_d3d.cmd_list };
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cl);
    (*a)->lpVtbl->Reset(*a);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, *a, NULL);
    gc_install(s_d3d.cmd_list);
    rsx_frame_gpu_begin_list(&s_frame_gpu,s_d3d.cmd_list);
    return 1;
}

/* The prepass of one range of draws: textures uploaded, the draw's SRV
 * window filled, its pipeline looked up or queued. Each draw needs only its
 * own, so with RSX_SUBMIT_SPLIT the draw loop prepares one part, draws it and
 * hands it to the GPU before preparing the next: done all at once first, the
 * prepass held the GPU idle for most of a heavy frame. */
static double s_pp_t; static const D3D12DrawRecord* s_pp_prev;

static void prepass_range(u32 from, u32 to)
{
    const int repeat_on=prepass_repeat_on();
    int previous_draw=-1;RsxPreparedRepeatKey previous_key={0};const u8* previous_code=NULL;
    frame_detail_tag(FD_SETUP);
    double frame_profile_start=s_frame_profile.active?pose_time():0;
    double _pd_t = s_pp_t; const D3D12DrawRecord* _pd_prev = s_pp_prev;
    for (u32 _d = from; _d < to && _d < s_rb.count && _d < MAX_DRAWS; _d++) {
        D3D12DrawRecord* dr = &s_rb.draws[_d];
        s_srv_win[_d] = _d;s_prepared_fp[_d].valid=0;
        /* PERF: one draw's prepass over 3 ms (time to the next draw) names it. */
        if (perf_on()) {
            double now = perf_now();
            if (_pd_prev && now - _pd_t > 0.003) {
                static int n = 0;
                if (n++ < 300)
                    fprintf(stderr, "[PREPASS-SLOW] %.1fms draw %u fp=0x%X vtex=0x%X rt=0x%08X"
                            " tex0=%ux%u fmt 0x%02X tex1=%ux%u verts=%u%c",
                            (now - _pd_t) * 1000.0, _d - 1, _pd_prev->fp_addr, _pd_prev->vtex_mask,
                            _pd_prev->rt_off, _pd_prev->tex[0].w, _pd_prev->tex[0].h,
                            _pd_prev->tex[0].fmt, _pd_prev->tex[1].w, _pd_prev->tex[1].h,
                            _pd_prev->vertex_count, 10);
            }
            _pd_t = now; _pd_prev = dr;
        }
        if (!dr->is_vp || dr->is_clear || dr->is_copy) { previous_draw=-1;continue; }
        /* Debug: RTT_VIEWRT=<hex raw offset> makes display draws sample that
         * offscreen RT at t0 (the composite blit then shows it fullscreen). */
        { static const char* vr = (const char*)1;     /* hoisted: this runs per DRAW */
          if (vr == (const char*)1) vr = getenv("RTT_VIEWRT");
          if (vr && dr->rt_off == 0) {
              dr->tex[0].raw = (u32)strtoul(vr, NULL, 16);
              dr->tex[0].off = 0;
              dr->tex[0].set = 1;
          } }
        /* SCREENTEX=1: report draws that sample a DISPLAY-SIZED texture. On RSX
         * a title renders a reflection/refraction into a surface and then
         * samples it; our backend renders into a D3D backbuffer, so the guest
         * memory that sampler reads is never written and the texture comes back
         * empty. Finding which geometry does it is the first step to feeding it
         * the rendered image instead. */
        /* EMPTYTEX=1: which geometry binds a texture whose source is all zero.
         * An unresolved texture renders untextured, so this names the surfaces
         * affected instead of leaving it to guesswork. */
        { static int ed = -1;
          if (ed < 0) { const char* e = getenv("EMPTYTEX"); ed = e ? atoi(e) : 0; }
          extern uint8_t* vm_base;
          if (ed && vm_base) for (int _u = 0; _u < 4; _u++) {
              if (!dr->tex[_u].set || !dr->tex[_u].off) continue;
              u32 nz = 0, sz = dr->tex[_u].w * dr->tex[_u].h * 4u;
              for (u32 i2 = 0; i2 < sz && i2 < 0x20000u; i2 += 997)
                  if (vm_base[dr->tex[_u].off + i2]) { nz = 1; break; }
              if (!nz) {
                  static u32 seen[24]; static int ns = 0; int known = 0;
                  for (int k = 0; k < ns; k++) if (seen[k] == dr->fp_addr) known = 1;
                  if (!known && ns < 24) { seen[ns++] = dr->fp_addr;
                      /* Scan a window around the resolved address: if the pass
                       * that fills this surface ran but landed elsewhere, the
                       * data is nearby; if the whole window is zero it was never
                       * produced. */
                      /* Same offset through BOTH context DMAs: a texture the
                       * guest put in main memory but that we resolve as LOCAL
                       * reads as untouched VRAM, i.e. all zeros. */
                      { extern u32 cellGcmResolveLocated(int, u32);
                        u32 la = cellGcmResolveLocated(1, dr->tex[_u].raw);
                        u32 ma = cellGcmResolveLocated(0, dr->tex[_u].raw);
                        u32 ln = 0, mn = 0;
                        for (u32 i4 = 0; i4 < 0x20000u; i4 += 997) {
                            if (vm_base[la + i4]) ln++;
                            if (vm_base[ma + i4]) mn++;
                        }
                        fprintf(stderr, "[TEXLOC] raw=0x%08X LOCAL@0x%08X nz=%u  MAIN@0x%08X nz=%u%c",
                                dr->tex[_u].raw, la, ln, ma, mn, 10); }
                      { u32 base = dr->tex[_u].off & ~0xFFFFFu;
                        for (u32 w = 0; w < 0x400000u; w += 0x100000u) {
                            u32 nzc = 0;
                            for (u32 i3 = 0; i3 < 0x100000u; i3 += 1021)
                                if (vm_base[base + w + i3]) nzc++;
                            fprintf(stderr, "[EMPTYSCAN] 0x%08X..+1MB nonzero %u/1027%c",
                                    base + w, nzc, 10);
                        } }
                      fprintf(stderr, "[EMPTYTEX] fp=0x%X unit=%d raw=0x%08X res=0x%08X %ux%u"
                                      " fmt=0x%02X verts=%u vp=%u,%u %ux%u%c",
                              dr->fp_addr, _u, dr->tex[_u].raw, dr->tex[_u].off, dr->tex[_u].w,
                              dr->tex[_u].h, dr->tex[_u].fmt, dr->vertex_count,
                              dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h, 10); }
              }
          } }
        { static int sd = -1;
          if (sd < 0) { const char* e = getenv("SCREENTEX"); sd = e ? atoi(e) : 0; }
          if (sd) for (int _u = 0; _u < 4; _u++)
              if (dr->tex[_u].set && dr->tex[_u].w == s_d3d.width &&
                  dr->tex[_u].h == s_d3d.height) {
                  static u32 seen[16]; static int ns = 0; int known = 0;
                  for (int k = 0; k < ns; k++) if (seen[k] == dr->fp_addr) known = 1;
                  if (!known && ns < 16) { seen[ns++] = dr->fp_addr;
                      fprintf(stderr, "[SCREENTEX] unit=%d raw=0x%08X %ux%u fmt=0x%02X"
                                      " fp=0x%X verts=%u vp=%u,%u %ux%u%c",
                              _u, dr->tex[_u].raw, dr->tex[_u].w, dr->tex[_u].h,
                              dr->tex[_u].fmt, dr->fp_addr, dr->vertex_count,
                              dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h, 10); }
              } }
        if (dr->vtex_mask) vtex_fill_window(_d, dr);
        /* Fill this draw's t0-t3 SRV window (DRAW_SRV_BASE + d*4): each unit
         * resolves to an offscreen RT (sampled directly), an uploaded guest
         * texture, or a null SRV. */
        double _sv0 = perf_on() ? perf_now() : 0.0;
        SrvReq _rq[FP_TEX_UNITS]; int _fb_risk = 0;
        memset(_rq, 0, sizeof _rq);
        /* Units the program never samples get no view. They still hold the
         * game's last binding -- Code Veronica's lighter shadow passes leave
         * their own target on unit 3 -- and binding it made every one of those
         * thousands of draws copy the 768x512 target aside as feedback.
         * FP_TEXUSED=0 binds every set unit as before. */
        u32 _used = 0xFFFFu;
        { static int en = -1;
          if (en < 0) { const char* e = getenv("FP_TEXUSED"); en = e ? atoi(e) : 1; }
          if (en && dr->fp_addr && dr->fp_snap && s_rb.fp_arena)   /* taken with the snapshot */
              memcpy(&_used, s_rb.fp_arena + dr->fp_snap - 1 + dr->fp_bytes + 16 + 4, 4);
          else if (en && dr->fp_addr) {
              const u8* uc = fp_snap_ptr(dr);
              if (!uc) {
                  extern uint8_t* vm_base;
                  u32 fo = cellGcmResolveLocated((dr->fp_addr & 3u) == 1, dr->fp_addr & ~3u);
                  if (fo != 0xFFFFFFFFu) uc = vm_base + fo;
              }
              if (uc) {
                  u32 bytes = dr->fp_bytes ? dr->fp_bytes : fp_guest_program_size(uc, NULL);
                  if (bytes) _used = rsx_fp_tex_used_mask(uc, bytes);
              }
          } }
        for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
            dr->tex_rt[_u] = -1;
            if (!((_used >> _u) & 1u)) {
                srv_req(&_rq[_u], NULL, DXGI_FORMAT_R8G8B8A8_UNORM,
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                continue;
            }
            /* Display-sized sampler source -> the rendered frame. */
            if (dr->tex[_u].set && s_screen_copy &&
                dr->tex[_u].w == s_d3d.width && dr->tex[_u].h == s_d3d.height) {
                static int en = -1;
                if (en < 0) { const char* e = getenv("SCREEN_AS_TEX"); en = e ? atoi(e) : 0; }
                if (en) {
                    { static int _n = 0; if (_n++ < 3)
                        fprintf(stderr, "[SCREENTEX] bound frame copy at unit %d for"
                                        " fp=0x%X (%ux%u)%c", _u, dr->fp_addr,
                                dr->tex[_u].w, dr->tex[_u].h, 10); }
                    srv_req(&_rq[_u], s_screen_copy, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                    continue;
                }
            }
            /* A sampler whose guest buffer was never written, at the size of a
             * reduced-viewport pass we captured: that pass IS its producer. */
            if (dr->tex[_u].set && dr->tex[_u].off && s_subvp_n) {
                int hit = -1;
                for (int i = 0; i < s_subvp_n; i++)
                    if (s_subvp[i].res && s_subvp[i].w == dr->tex[_u].w &&
                        s_subvp[i].h == dr->tex[_u].h) { hit = i; break; }
                if (hit >= 0) {
                    extern uint8_t* vm_base;
                    u32 nz = 0, sz = dr->tex[_u].w * dr->tex[_u].h * 4u;
                    for (u32 i5 = 0; i5 < sz && i5 < 0x20000u; i5 += 997)
                        if (vm_base[dr->tex[_u].off + i5]) { nz = 1; break; }
                    if (!nz) {
                        { static int _n = 0; if (getenv("SUBVP_DBG") && _n++ < 8)
                            fprintf(stderr, "[SUBVP] unit %d fp=0x%X <- captured %ux%u%c",
                                    _u, dr->fp_addr, dr->tex[_u].w, dr->tex[_u].h, 10); }
                        srv_req(&_rq[_u], s_subvp[hit].res, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                        continue;
                    }
                }
            }
            if (dr->tex[_u].set) {
                int rt = off_rt_find_sz(dr->tex[_u].raw, dr->tex[_u].w, dr->tex[_u].h);
                /* A strip-shaped texture over a full-size surface is not that
                 * surface. Code Veronica's colour grade reads a 1024x1 lookup
                 * table the CPU writes at 0x06FF0000, where it also clears a
                 * 1280x720 target to white; binding the target graded every
                 * pixel to white. Read such a texture from guest memory. */
                if (rt >= 0 && rt != s_zview_slot && dr->tex[_u].h <= 4 &&
                    s_d3d.off_rt[rt].h > 4 && !getenv("TEX_STRIP_AS_RT")) {
                    static int n = 0; if (n++ < 4)
                        fprintf(stderr, "[TEXRT] 0x%08X sampled as %ux%u, surface is %ux%u -- using guest memory%c",
                                dr->tex[_u].raw, dr->tex[_u].w, dr->tex[_u].h,
                                s_d3d.off_rt[rt].w, s_d3d.off_rt[rt].h, 10);
                    rt = -1;
                }
                if (rt >= 0) {
                    dr->tex_rt[_u] = rt;
                    /* .z bit 7: a Y16_X16 read of a colour target -- the shader
                     * pairs its bytes (see rsx_texgamma). An uploaded Y16_X16
                     * texture is already R16G16 and must not be. */
                    if ((dr->tex[_u].fmt & 0x9Fu) == 0x95u && rt != s_zview_slot &&
                        s_d3d.vp_fpcb_mapped) {
                        float* tsg = (float*)((char*)s_d3d.vp_fpcb_mapped
                            + ((u64)s_rb.parity * MAX_DRAWS + dr->cb_slot) * VP_FPCB_STRIDE);
                        tsg[_u*4+2] = (float)((dr->tex[_u].addr & 0xFu) | 128u);
                    }
                    if (s_d3d.off_rt[rt].off == dr->rt_off) _fb_risk = 1;   /* may need a feedback copy */
                    /* RT_NOGAMMA=1 (diagnostic): sample render targets without
                     * their gamma bits. Off by default: RPCS3 applies them to
                     * surfaces too, and Code Veronica's bloom downsample needs
                     * the decode (without it each pass re-encoded and the
                     * bloom chain blew the frame out). */
                    static int rt_nogamma = -1;
                    if (rt_nogamma < 0) rt_nogamma = getenv("RT_NOGAMMA") ? 1 : 0;
                    if (rt_nogamma && s_d3d.vp_fpcb_mapped) {
                        float* tsg = (float*)((char*)s_d3d.vp_fpcb_mapped
                            + ((u64)s_rb.parity * MAX_DRAWS + dr->cb_slot) * VP_FPCB_STRIDE);
                        tsg[_u*4+2] = (float)((u32)tsg[_u*4+2] & 0xFu);
                    }
                    /* The converted zeta holds A8R8G8B8 bytes in memory order,
                     * so it takes the draw's remap like an uploaded texture. */
                    srv_req(&_rq[_u], s_d3d.off_rt[rt].res, (DXGI_FORMAT)s_d3d.off_rt[rt].dxgi,
                              (rt == s_zview_slot)
                                  ? rsx_remap_to_d3d(dr->tex[_u].ctrl1, 0x85)
                                  : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                    continue;
                }
                u32 _bf = dr->tex[_u].fmt & 0x9F;
                /* 0x95 Y16_X16: Code Veronica's lighter looks up which shadow
                 * face and where in it through two small Y16_X16 cube maps;
                 * left unbound, every pixel read one spot of the shadow map. */
                if (dr->tex[_u].off &&
                    (_bf == 0x81 || _bf == 0x85 || _bf == 0x8B || _bf == 0x95 || _bf == 0x9C ||
                     (_bf >= 0x86 && _bf <= 0x88))) {
                    double _tt = perf_on() ? perf_now() : 0.0;
                    int _cube = dr->tex[_u].cube && (dr_cube_mask(dr) & (1u << _u));
                    int ts = vp_upload_tex_slot(dr->tex[_u].off, dr->tex[_u].w,
                                                dr->tex[_u].h, dr->tex[_u].fmt,
                                                _cube, dr->tex[_u].mips);
                    if (perf_on()) { s_perf_tex += perf_now() - _tt; s_perf_ntex++;
                        s_perf_texbytes += (u64)dr->tex[_u].w * dr->tex[_u].h * 4u; }
                    /* Both forms of the offset are in hand here; the filters
                     * compare the BOUND one. */
                    if (s_duck_off && dr->tex[_u].off == s_duck_off)
                        s_duck_raw = dr->tex[_u].raw;
                    if (ts >= 0) {
                        DXGI_FORMAT sf =
                            (_bf == 0x85) ? DXGI_FORMAT_R8G8B8A8_UNORM :
                            (_bf == 0x8B) ? DXGI_FORMAT_R8G8_UNORM :
                            (_bf == 0x95) ? DXGI_FORMAT_R16G16_UNORM :
                            (_bf == 0x9C) ? DXGI_FORMAT_R32_FLOAT :
                            (_bf == 0x86) ? DXGI_FORMAT_BC1_UNORM :
                            (_bf == 0x87) ? DXGI_FORMAT_BC2_UNORM :
                            (_bf == 0x88) ? DXGI_FORMAT_BC3_UNORM :
                                            DXGI_FORMAT_R8_UNORM;
                        /* A texture mod's image, once loaded: RGBA8 with the
                         * same channels, so the guest's remap still applies. */
                        texdump_for_slot(ts);
                        ID3D12Resource* _tr = s_d3d.vp_tex[ts].ui_res;
                        if (!_tr) _tr = texrep_for_slot(ts);
                        if (_tr) sf = DXGI_FORMAT_R8G8B8A8_UNORM;
                        else _tr = s_d3d.vp_tex[ts].res;
                        { static int nb = 0; const char* e = getenv("TEX_DUMP");
                          if (e && nb < 6 && ((dr->tex[_u].off & 0x0FFFFFFFu) ==
                                              ((u32)strtoul(e, NULL, 16) & 0x0FFFFFFFu))) { nb++;
                              fprintf(stderr, "[TEXBIND] unit=%d fp=0x%08X ts=%d cube=%d(st %d mask 0x%X) slotcube=%d "
                                              "fmt=0x%02X ctrl1=0x%08X map=0x%X res=%p rep=%d%c",
                                      _u, dr->fp_addr, ts, _cube, dr->tex[_u].cube, dr_cube_mask(dr),
                                      s_d3d.vp_tex[ts].cube, dr->tex[_u].fmt, dr->tex[_u].ctrl1,
                                      rsx_remap_to_d3d(dr->tex[_u].ctrl1, _bf), (void*)_tr,
                                      _tr != s_d3d.vp_tex[ts].res, 10); } }
                        srv_req_ex(&_rq[_u], _tr, sf,
                                  (_bf == 0x81) ? 0x1000
                                                : rsx_remap_to_d3d(dr->tex[_u].ctrl1, _bf),
                                  _cube);
                        continue;
                    }
                }
            }
            /* TEX_BINDDBG=<N>: a unit that ends up with a NULL SRV. The draw is
             * still recorded, lit and transformed -- it just samples nothing,
             * which renders as a solid black object. */
            { static int cap = -1, n = 0;
              if (cap < 0) { const char* e = getenv("TEX_BINDDBG"); cap = e ? atoi(e) : 0; }
              if (cap && n < cap && dr->tex[_u].set) { n++;
                fprintf(stderr, "[TEXBIND] null SRV unit=%d raw=0x%X off=0x%X %ux%u fmt=0x%02X%c",
                        _u, dr->tex[_u].raw, dr->tex[_u].off, dr->tex[_u].w,
                        dr->tex[_u].h, dr->tex[_u].fmt, 10); } }
            srv_req(&_rq[_u], NULL, DXGI_FORMAT_R8G8B8A8_UNORM,
                      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        }
        srv_commit((u32)_d, _rq, _fb_risk);
        if (perf_on()) s_perf_srv += perf_now() - _sv0;
        double _ps0 = perf_on() ? perf_now() : 0.0;
        if (dr->fp_addr) {
            const u8* snap=fp_snap_ptr(dr);
            RsxPreparedRepeatKey key={(u32)dr->vs_idx,dr->blend_key,(u32)dr_num_rts(dr),
                dr->rt_off?rsx_surface_dxgi(dr->rt_fmt):DXGI_FORMAT_R8G8B8A8_UNORM,
                dr->fp_exp32,dr->cmask,dr->cull|((prep_reuse_on()&&dr->topology==RSX_TOPOLOGY_LINES)?0x100u:0u),
                dr_cube_mask(dr),dr->fp_bytes};
            const int frozen=dr->fp_snap&&s_rb.fp_arena&&snap;
            ID3D12PipelineState* prepared=NULL;
            if(repeat_on&&frozen&&previous_draw>=0&&
               rsx_prepared_repeat_match(&previous_key,&key,previous_code,snap)) {
                prepared=fp_prepared_ready((u32)previous_draw,dr);
                if(prepared){s_prep_pipelines--;s_prepass_repeat_reused++;
                    s_fp_result_index=s_prepared_fp[previous_draw].index;}
            }
            if(!prepared)prepared=vp_get_fp_pso(dr->vs_idx,dr->fp_addr,dr->blend_key,
                (int)key.nrt,key.rtfmt,dr->fp_exp32,dr->cmask,key.cull,key.cube_mask,snap,dr_fp_urgent(dr));
            if(prep_reuse_on()&&snap&&prepared&&!s_fp_skip_draw&&s_fp_result_index>=0) {
                PreparedFp* p=&s_prepared_fp[_d];p->index=s_fp_result_index;
                p->key=s_d3d.vp_fp[p->index];p->valid=1;
            }
            if(repeat_on&&frozen&&s_prepared_fp[_d].valid){previous_draw=(int)_d;previous_key=key;previous_code=snap;}
            else previous_draw=-1;
        } else previous_draw=-1;
        if (perf_on()) s_perf_pso += perf_now() - _ps0;
    }
    s_pp_t = _pd_t; s_pp_prev = _pd_prev;
    if(s_frame_profile.active)s_frame_profile.prepass+=pose_time()-frame_profile_start;
}

/* Undo exactly what pose_interpolate_batch wrote into this batch's upload
 * set: the same seen/changed tests select the same vertices and constant
 * banks, and the frozen snapshots hold their recorded values. */
static void pose_restore_batch(void)
{
    if((u32)s_rb.parity>=(u32)vp_sets()||!s_pose_written[s_rb.parity])return;
    s_pose_written[s_rb.parity]=0;
    const u32* seen_vertices=s_pose_seen_vertices_sets+(size_t)s_rb.parity*MAX_VERTICES;
    const u32* seen_cameras=s_pose_seen_cameras_sets+(size_t)s_rb.parity*MAX_DRAWS;
    const float* out_vertices=s_pose_out_vertices_sets+(size_t)s_rb.parity*MAX_VERTICES*3;
    const float* out_cameras=s_pose_out_cameras_sets+(size_t)s_rb.parity*MAX_DRAWS*32;
    const RsxPoseVertex* vertices=s_pose_vertices+(size_t)s_rb.parity*MAX_VERTICES;
    const float* cameras=s_pose_cameras+(size_t)s_rb.parity*MAX_DRAWS*32;
    u8* vb=(u8*)s_d3d.vp_vb_mapped+(size_t)s_rb.parity*MAX_VERTICES*256;
    u8* cb=(u8*)s_d3d.vp_cb_mapped+(size_t)s_rb.parity*MAX_DRAWS*VP_CB_STRIDE;
    for(u32 v=0;v<s_rb.pose_nv;v++) {
        if(seen_vertices[v]!=1||!memcmp(vertices[v].xyz,out_vertices+(size_t)v*3,3*sizeof(float)))continue;
        if(s_vf_mapped)rsx_pose_gpu_reset(s_vf_mapped+(u64)s_rb.parity*vf_parity_bytes()+VF_POSE_OFF+(size_t)v*vf_pose_stride());
        else memcpy(vb+(size_t)v*256,vertices[v].xyz,3*sizeof(float));
    }
    for(u32 c=0;c<MAX_DRAWS;c++)if(seen_cameras[c]==1&&memcmp(cameras+(size_t)c*32,out_cameras+(size_t)c*32,32*sizeof(float)))
        memcpy(cb+(size_t)c*VP_CB_STRIDE,cameras+(size_t)c*32,32*sizeof(float));
}
/* CV_FRAME_PROFILE: present-to-present pacing. A 60 FPS average can hide an
 * uneven cadence (e.g. 10/23 ms); report percentiles and the two replay legs. */
static void present_pace_note(void)
{
    static double last, reported, legs[2]; static u32 n, leg_n[2], short_n, long_n;
    static float ms[1024];
    if (!frame_profile_on()) return;
    const double now = pose_time();
    if (last > 0) {
        const double d = (now - last) * 1000.0;
        if (n < 1024) ms[n++] = (float)d;
        if (d < 12.0) short_n++; else if (d > 21.0) long_n++;
        legs[s_replay_pass] += d; leg_n[s_replay_pass]++;   /* 1: first->replay, 0: replay->next */
    }
    last = now;
    if (!reported) reported = now;
    if (now - reported < 5 || n < 8) return;
    for (u32 i = 1; i < n; i++) { float x = ms[i]; u32 j = i; while (j && ms[j-1] > x) { ms[j] = ms[j-1]; j--; } ms[j] = x; }
    fprintf(stderr, "[PRESENT-PACE] %u intervals p50=%.2f p90=%.2f p99=%.2f max=%.2f under12=%u over21=%u first_to_replay=%.2f replay_to_next=%.2f%c",
            n, ms[n/2], ms[n*9/10], ms[n*99/100], ms[n-1], short_n, long_n,
            leg_n[1] ? legs[1] / leg_n[1] : 0, leg_n[0] ? legs[0] / leg_n[0] : 0, 10);
    n = short_n = long_n = leg_n[0] = leg_n[1] = 0; legs[0] = legs[1] = 0; reported = now;
}
static int pose_overlap_on(void);
int rsx_d3d12_pose_replay_enabled(void);
static void render_frame_impl(void)
{
    { static int announced; if(!announced&&opt30_mode()>=0){
        fprintf(stderr,"[OPT30] package=%s prep=%d shadow=%d tex_edges=%d pose=%d fps_test60=0 (restart to switch)\n",
            opt30_mode()?"ON":"OFF",prep_reuse_on(),shadow_batch_on(),precomputed_tex_edges_on(),s_pose_on);announced=1;}}
    frame_profile_begin();s_prep_pipelines=s_prep_vtex_reused=s_prep_vtex_written=0;
    s_shadow_batch_groups=s_shadow_batch_saved=0;
    const int cv_diag = cv_cutscene_diag();
    const double cv_start = cv_diag ? perf_now() : 0.0;
    double cv_wait = 0.0, cv_present_start = 0.0, cv_present_ms = 0.0, cv_fence_start = 0.0;

    double _rf0 = perf_on() ? perf_now() : 0.0;
    u32 fi = s_d3d.frame_index;
    /* PERF: a render frame over 50 ms reports its own stage breakdown. */
    const double _h_tex = s_perf_tex, _h_srv = s_perf_srv, _h_pso = s_perf_pso,
                 _h_pre = s_perf_pre, _h_gpu = s_perf_gpu;
    const int _h_ntex = s_perf_ntex; double _h_misc = 0.0;

    /* Drain the GPU before touching shared upload resources (vp_vb vertices,
     * vp_cb constants, per-frame texture staging): the previous frame's draws
     * may still be reading them, and overwriting mid-flight tears geometry
     * (gcm/cube: triangles mixing stale and new vertices -> missing/sliver
     * polygons). These workloads are a few draws/frame, so full serialisation
     * costs little. */
    /* Its own parity only: may overlap the previous batch's GPU work. */
    const int pose_early = pose_overlap_on() && !s_replay_pass && !s_rb.pose_done;
    if (pose_early) { pose_interpolate_batch(); frame_profile_mark(FP_POSE); }
    { double _tw = perf_on() ? perf_now() : 0.0;
      const double cv_gpu_start = cv_diag ? perf_now() : 0.0;
      wait_for_gpu();
      if (cv_diag) cv_wait = (perf_now() - cv_gpu_start) * 1000.0;
      if (perf_on()) s_perf_gpu += perf_now() - _tw; }
    frame_profile_mark(FP_ENTRY);
    if (s_replay_pass) pose_restore_batch();   /* the GPU finished the interpolated draw */
    else if (!pose_early && !s_rb.pose_done) pose_interpolate_batch();
    s_fsr_ready=0;s_fsr_depth_at=0xFFFFFFFFu;
    if(s_rb.present&&rsx_fsr31_requested()) {
        s_fsr_ready=rsx_fsr31_begin(s_d3d.device,s_display?s_disp_w:rs_dim(s_d3d.width,s_rs),
            s_display?s_disp_h:rs_dim(s_d3d.height,s_rs),s_rb.pose_scene,
            s_pose_history.reason==RSX_POSE_CUT||s_pose_history.reason==RSX_POSE_LAYOUT);
        /* Snapshot the shared DSV after the final full-size camera geometry,
         * before fullscreen/postprocess/shadow work can overwrite its bank. */
        if(s_fsr_ready)for(u32 i=0;i<s_rb.count&&i<MAX_DRAWS;i++){
            const D3D12DrawRecord* d=&s_rb.draws[i];
            if(!d->is_vp||d->is_clear||d->is_copy||!d->dtest||d->vs_idx<0||d->vs_idx>=s_d3d.vp_vs_n||
               !s_d3d.vp_vs[d->vs_idx].uses_c03||d->rt_w!=s_d3d.width||d->rt_h!=s_d3d.height||
               (rsx_pose_fragment_flags(fp_snap_code(d))&RSX_POSE_CURRENT_LIGHT_CAMERA))continue;
            s_fsr_depth_at=i+1;s_fsr_depth_w=rs_dim(d->rt_w,s_rs);s_fsr_depth_h=rs_dim(d->rt_h,s_rs);
        }
    }
    frame_profile_mark(FP_POSE);
    { double _m0 = perf_on() ? perf_now() : 0.0;
    s_vtex_frame++;       /* ages the vertex-texture cache */
    s_tex_frame++;        /* ...and the texture-slot cache */
    plib_init();          /* first frame: open shader_cache/pipelines.bin */
    fp_async_drain();     /* pipelines the shader workers finished */
    plib_maybe_save();
    pl_tick();            /* the shader preload: start it, hand over what it built */
      if (perf_on()) _h_misc = perf_now() - _m0; }

    off_rt_writeback_flush();   /* GPU is idle: last frame's copies are ready */
    s_cv_query_active = 0;
    memset(s_cv_query_valid, 0, sizeof s_cv_query_valid);
    cap_steps_free();
    s_cap_steps_on = g_rsx_cap_request || s_autocap_arm ||
                     (s_rsxcap_next && s_d3d.frame_count >= s_rsxcap_next);
    if ((getenv("CV_GPU_STATS") && s_d3d.frame_count == 1800) || s_cap_steps_on) {
        if (!s_cv_query_heap) {
            D3D12_QUERY_HEAP_DESC qd = {0};
            qd.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
            qd.Count = MAX_DRAWS;
            HRESULT qhr = s_d3d.device->lpVtbl->CreateQueryHeap(s_d3d.device,
                &qd, &IID_ID3D12QueryHeap, (void**)&s_cv_query_heap);
            D3D12_HEAP_PROPERTIES hp = {0}; hp.Type=D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd = {0};
            rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width=(u64)MAX_DRAWS*sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
            rd.Height=1; rd.DepthOrArraySize=1; rd.MipLevels=1;
            rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            HRESULT rhr=s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,
                &hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,
                &IID_ID3D12Resource,(void**)&s_cv_query_readback);
            fprintf(stderr,"[CV_GPU_STATS] create query=%08X readback=%08X\n",(unsigned)qhr,(unsigned)rhr);
        }
        s_cv_query_active = s_cv_query_heap && s_cv_query_readback;
    }

    /* Compile the real vertex program once its microcode is captured, and keep
     * the constant bank uploaded for the VS. */
    /* The base VP pipeline gates the ENTIRE vertex-program draw pass below
     * (`if (s_d3d.vp_ready ...)`). The old trigger also required the microcode
     * size to differ from the last compile, so a title whose first captured
     * program has the same byte count as vp_compiled_bytes never built the base
     * pipeline -- and then every is_vp draw record was silently dropped: draws
     * recorded, constants uploaded, per-draw VS compiled and cached, and not one
     * of them submitted. While !vp_ready there is nothing to be stale against,
     * so only the "have microcode" test belongs here. */
    if (s_d3d.current_rsx_state && !s_d3d.vp_ready &&
        s_d3d.current_rsx_state->vp_ucode_bytes >= 16)
        compile_vp();
    /* Per-draw VP constants are snapshotted at record time (vp_record_cb). */

    /* Lazily create the readback buffer the first time a dump is requested. */
    if (s_d3d.dump_frames_left > 0 && !s_d3d.readback_buf) {
        s_d3d.readback_pitch = (rs_dim(s_d3d.width, s_rs) * 4 + 255) & ~255u;
        D3D12_HEAP_PROPERTIES hp = {0};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {0};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (u64)s_d3d.readback_pitch * rs_dim(s_d3d.height, s_rs);
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.readback_buf);
    }

    /* Reset command allocator and list */
    s_d3d.cmd_allocators[fi]->lpVtbl->Reset(s_d3d.cmd_allocators[fi]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[fi], NULL);
    s_split_used = 0;     /* the GPU wait above freed this index's part allocators */
    gc_install(s_d3d.cmd_list);
    gc_frame();
    ts_begin(fi);
    frame_profile_gpu_begin(fi);
    s_pose_compute_capture_ready=0;
    if(pose_compute_mode()&&s_pose_compute.ready)pc_record(&s_pose_compute,s_d3d.cmd_list,&s_pose_compute_plan,s_vf_buf,
        (u64)s_rb.parity*vf_parity_bytes()+VF_POSE_OFF,s_rb.pose_nv);
    texrep_frame();       /* texture mods: upload what finished decoding */

    /* Upload the bound font atlas into an R8_UNORM texture and create its SRV.
     * The atlas is a linear 8-bit coverage map, so a straight row copy (no
     * deswizzle) suffices. It is a DYNAMIC glyph cache -- the game rasterizes
     * new glyphs into it over time -- so re-upload whenever it is dirty (set on
     * every bind), not just once. Uploading only on first bind left every glyph
     * added after frame 1 sampling stale texels -> torn text. The backend is
     * synchronous (wait_for_gpu each frame), so recreating the resource per
     * dirty frame is safe. */
    if (s_d3d.tex_bound && (!s_d3d.tex_ready || s_d3d.tex_dirty) && s_d3d.srv_heap && s_d3d.pipeline_state_tex) {
        extern uint8_t* vm_base;
        u32 w = s_d3d.tex_w, h = s_d3d.tex_h;
        u32 pitch = (w + 255) & ~255u;   /* D3D12 requires 256-byte row pitch */

        if (s_d3d.tex_resource) { s_d3d.tex_resource->lpVtbl->Release(s_d3d.tex_resource); s_d3d.tex_resource = NULL; }
        if (s_d3d.tex_upload)   { s_d3d.tex_upload->lpVtbl->Release(s_d3d.tex_upload);     s_d3d.tex_upload = NULL; }

        D3D12_HEAP_PROPERTIES hp_def = {0}; hp_def.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        HRESULT thr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp_def, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.tex_resource);

        D3D12_HEAP_PROPERTIES hp_up = {0}; hp_up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ud = {0};
        ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width = (u64)pitch * h; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
        ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(thr))
            thr = s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp_up, D3D12_HEAP_FLAG_NONE, &ud,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.tex_upload);

        if (SUCCEEDED(thr) && vm_base) {
            void* mapped = NULL;
            D3D12_RANGE nr = {0, 0};
            if (SUCCEEDED(s_d3d.tex_upload->lpVtbl->Map(s_d3d.tex_upload, 0, &nr, &mapped)) && mapped) {
                const u8* srcbase = vm_base + s_d3d.tex_src_offset;
                for (u32 y = 0; y < h; y++)
                    memcpy((u8*)mapped + (u64)y * pitch, srcbase + (u64)y * w, w);
                s_d3d.tex_upload->lpVtbl->Unmap(s_d3d.tex_upload, 0, NULL);

                D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
                dst.pResource = s_d3d.tex_resource;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = 0;
                src.pResource = s_d3d.tex_upload;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = 0;
                src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
                src.PlacedFootprint.Footprint.Width    = w;
                src.PlacedFootprint.Footprint.Height   = h;
                src.PlacedFootprint.Footprint.Depth    = 1;
                src.PlacedFootprint.Footprint.RowPitch = pitch;
                s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

                D3D12_RESOURCE_BARRIER tb = {0};
                tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                tb.Transition.pResource   = s_d3d.tex_resource;
                tb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                tb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &tb);

                D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
                sv.Format = DXGI_FORMAT_R8_UNORM;
                sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sv.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE sh;
                s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
                s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, s_d3d.tex_resource, &sv, sh);

                s_d3d.tex_ready = 1;
                s_d3d.tex_dirty = 0;   /* content now in sync with guest atlas */
                { static int _au = 0; if (_au++ < 3)
                    printf("[D3D12] atlas uploaded (%ux%u R8) -> textured\n", w, h); }
            }
        }
    }

    /* Frames with no ops (init/boot presents) don't count against the cap --
     * PNG-decode-heavy titles burn hundreds of empty presents before the first
     * real draw. */
    if (getenv("RTT_DUMP") && s_rb.count > 0) { static int _f=0; int _cap = atoi(getenv("RTT_DUMP")); if (_cap < 2) _cap = 14; if (_f++ < _cap) {
        fprintf(stderr, "[RTT] frame %d: %u ops\n", _f, s_rb.count);
        for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++) {
            D3D12DrawRecord* r = &s_rb.draws[_d];
            fprintf(stderr, "[RTT]  op%02u %s rt=0x%X mrt=0x%X/0x%X/0x%X t0=0x%X fp=0x%X n=%u cmask=%X vp=%u,%u %ux%u blend=%d\n",
                _d, r->is_clear?"CLR ":(r->is_vp?"draw":"leg "), r->rt_off,
                r->rt_mrt[0], r->rt_mrt[1], r->rt_mrt[2],
                r->tex[0].raw, r->fp_addr, r->vertex_count,
                r->cmask, r->vp_x, r->vp_y, r->vp_w, r->vp_h, r->blend);
        }
    } }

    /* Debug: RTT_PASS=N shows pass N's output directly on screen (drops later
     * ops, retargets pass N to the backbuffer). */
    { const char* rp = getenv("RTT_PASS");
      if (rp) {
        int keep = atoi(rp), seen = 0; u32 cut = s_rb.count;
        int viewrt = getenv("RTT_VIEWRT") != NULL;
        for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++) {
            D3D12DrawRecord* r = &s_rb.draws[_d];
            if (r->is_clear || !r->is_vp) continue;
            if (seen == keep) {
                /* Alone: retarget op N to the backbuffer. With RTT_VIEWRT:
                 * keep op N intact and pull the display composite forward so
                 * the chosen RT is shown as of this point in the chain. */
                if (!viewrt) { r->rt_off = 0; r->rt_mrt[0] = r->rt_mrt[1] = r->rt_mrt[2] = 0; }
                cut = _d + 1;
                break;
            }
            seen++;
        }
        if (viewrt) {
            for (u32 _j = cut; _j < s_rb.count && _j < MAX_DRAWS; _j++) {
                D3D12DrawRecord* r = &s_rb.draws[_j];
                if (r->is_vp && !r->is_clear && r->rt_off == 0) {
                    if (cut < MAX_DRAWS) s_rb.draws[cut++] = *r;
                    break;
                }
            }
        }
        s_rb.count = (cut < s_rb.count) ? cut : s_rb.count;
      } }

    /* DRAW_LIMIT=<N>: keep only the first N recorded ops this frame. The duck's
     * draws are ops 00..~03 of every frame, so a small limit shows the ducks
     * alone -- separating "the ducks never rasterize" from "later geometry is
     * painted over them". Diagnostic only. */
    { static int lim = -1;
      if (lim < 0) { const char* e = getenv("DRAW_LIMIT"); lim = e ? atoi(e) : 0; }
      if (lim > 0 && s_rb.count > (u32)lim) s_rb.count = (u32)lim; }

    /* DRAW_SKIP_TEX=<hex raw offset>: drop every op binding that texture on
     * unit 0. The ducks sit behind the water's slab geometry, so removing the
     * occluder is the way to see whether the duck itself renders. Diagnostic. */
    { static const char* sk = (const char*)1; static u32 want = 0;
      if (sk == (const char*)1) sk = getenv("DRAW_SKIP_TEX");
      want = draw_filter_tex(sk);
      if (want) {
        u32 keep = 0;
        for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++) {
            if (!s_rb.draws[_d].is_clear && s_rb.draws[_d].tex[0].raw == want) continue;
            if (keep != _d) s_rb.draws[keep] = s_rb.draws[_d];
            keep++;
        }
        s_rb.count = keep;
      } }

    /* DRAW_KEEP_TEX=<hex raw offset>: the inverse -- keep only the ops binding
     * that texture (plus clears). Isolates one object from everything drawn over
     * it, which is what finally shows the duck on its own. Diagnostic. */
    { static const char* kp = (const char*)1; static u32 want = 0;
      if (kp == (const char*)1) kp = getenv("DRAW_KEEP_TEX");
      want = draw_filter_tex(kp);
      /* DRAW_KEEP_NOCLEAR=1: also drop the clears. Keeping them preserves their
       * ORIGINAL position in the batch, so a clear that originally ran after the
       * kept draws still runs after them -- and wipes the very geometry being
       * isolated. That makes an object that renders fine look like it renders
       * nothing. */
      static int noclr = -1;
      if (noclr < 0) { const char* e = getenv("DRAW_KEEP_NOCLEAR"); noclr = e ? atoi(e) : 0; }
      /* DRAW_KEEP_BLEND=0|1: further restrict DRAW_KEEP_TEX to draws with that
       * blend flag. A mesh drawn twice -- once opaque, once blended -- is two
       * different passes (body vs reflection shell) that a texture filter alone
       * cannot separate. */
      static int kblend = -2;
      if (kblend == -2) { const char* e = getenv("DRAW_KEEP_BLEND");
                          kblend = e ? atoi(e) : -1; }
      if (want) {
        /* Emit the CLEARS FIRST, then the kept draws. Preserving the original
         * order means a clear that ran after them still runs after them and
         * wipes the isolated geometry; dropping the clears entirely (noclr)
         * leaves stale DEPTH that rejects most of its fragments. Neither shows
         * the object properly -- clear, then draw, does. */
        static D3D12DrawRecord kept[MAX_DRAWS];
        u32 nk = 0;
        if (!noclr)
            for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++)
                if (s_rb.draws[_d].is_clear && nk < MAX_DRAWS) kept[nk++] = s_rb.draws[_d];
        for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++)
            if (!s_rb.draws[_d].is_clear && s_rb.draws[_d].tex[0].raw == want
                && (kblend < 0 || s_rb.draws[_d].blend == kblend)
                && nk < MAX_DRAWS)
                kept[nk++] = s_rb.draws[_d];
        for (u32 _k = 0; _k < nk; _k++) s_rb.draws[_k] = kept[_k];
        s_rb.count = nk;
      } }

    /* DRAW_LAST_TEX=<hex raw offset>: move the ops binding that texture to the
     * END of the batch so nothing is drawn over them. The duck's draws are ops
     * 00..03 of every frame and the tub wall is drawn afterwards across the same
     * screen area, so anything that defeats the depth sort hides the duck
     * completely. Reordering separates "occluded" from "not rendered". */
    { static const char* lt = (const char*)1; static u32 want = 0;
      if (lt == (const char*)1) lt = getenv("DRAW_LAST_TEX");
      want = draw_filter_tex(lt);
      if (want && s_rb.count > 1) {
        static D3D12DrawRecord moved[MAX_DRAWS];
        u32 nm = 0, keep = 0;
        for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++) {
            if (!s_rb.draws[_d].is_clear && s_rb.draws[_d].tex[0].raw == want) {
                if (nm < MAX_DRAWS) moved[nm++] = s_rb.draws[_d];
                continue;
            }
            if (keep != _d) s_rb.draws[keep] = s_rb.draws[_d];
            keep++;
        }
        for (u32 _m = 0; _m < nm && keep < MAX_DRAWS; _m++)
            s_rb.draws[keep++] = moved[_m];
        s_rb.count = keep;
      } }

    /* Render-to-texture pre-pass: make sure an offscreen RT resource exists for
     * every non-display surface targeted this frame (so draws binding it as a
     * texture can resolve to it below, whatever the op order). */
    for (int _i = 0; _i < MAX_OFF_RTS; _i++) s_d3d.off_rt[_i].used = 0;
    for (u32 _d = 0; _d < s_rb.count && _d < MAX_DRAWS; _d++) {
        D3D12DrawRecord* dr = &s_rb.draws[_d];
        if (dr->is_vp && dr->rt_off)
            off_rt_get(dr->rt_off, dr->rt_w, dr->rt_h, dr->rt_fmt);
        for (int _m = 0; _m < 3; _m++)
            if (dr->is_vp && dr->rt_mrt[_m])
                off_rt_get(dr->rt_mrt[_m], dr->rt_w, dr->rt_h, dr->rt_fmt);
    }
    zconv_plan();   /* after the RT pass: it adds a surface at the zeta offset */

    /* Per-frame VP textures + guest-FP pipelines: for each VP draw, upload the
     * texture it had bound at submit time into a slot (SRV heap 1+slot; plasma
     * animates so contents re-upload every frame) and pre-build its FP PSO.
     * A texture whose offset matches an offscreen RT samples the RT directly
     * (tex_slot 1000+idx) -- no guest-memory upload. */
    double _pre0 = perf_on() ? perf_now() : 0.0;
    for (int _i = 0; _i < VP_TEX_SLOTS; _i++) s_d3d.vp_tex[_i].used = 0;
    s_srv_last_win = -1;
    s_pp_t = perf_on() ? perf_now() : 0.0; s_pp_prev = NULL;
    /* Split frames prepare their first part here and the rest in the draw
     * loop, part by part; otherwise all of it now, as before. */
    const u32 pp_part = (s_cv_query_active || s_cap_steps_on) ? 0u : (u32)submit_split_every();
    u32 pp_done = pp_part ? pp_part : MAX_DRAWS;
    prepass_range(0, pp_done);

    /* Transition render target to RENDER_TARGET state */
    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = s_d3d.render_targets[fi];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

    /* Get RTV handle for current frame */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv_handle);
    rtv_handle.ptr += fi * s_d3d.rtv_descriptor_size;

    /* Get DSV handle (single depth buffer shared across frames) */
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    s_d3d.dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.dsv_heap, &dsv_handle);

    /* Pipelines the prepass just queued: wait briefly, then pick them up so
     * the draw loop below (which looks each one up again) can use them. */
    /* One budget for the frame, shared with the draw loop below (the prepass
     * keys some draws differently). A build from the disk caches takes about
     * a millisecond; a first-time compile takes 30-100, and waiting longer
     * for one only stretched the frame -- the stutter on every new shader. */
    double draw_wait_left = fp_async_wait_ms() / 1000.0;
    if (s_fp_inflight) {
        fp_async_wait_urgent();
        const double tw = perf_now();
        fp_async_wait_for(draw_wait_left);
        draw_wait_left -= perf_now() - tw;
        fp_async_drain();
    }
    if (perf_on()) s_perf_pre += perf_now() - _pre0;
    /* Set render target + depth */
    s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);

    /* Clear color and depth */
    s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
        s_d3d.cmd_list, rtv_handle, s_d3d.clear_color, 0, NULL);
    s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(
        s_d3d.cmd_list, dsv_handle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, NULL);

    /* Set viewport and scissor */
    D3D12_VIEWPORT viewport = {0, 0, (float)s_d3d.width, (float)s_d3d.height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)s_d3d.width, (LONG)s_d3d.height};
    rs_set_vp(s_rs, &viewport, &scissor);

    /* Bind pipeline state and push MVP if anything to draw */
    if (s_d3d.pipeline_ready && s_rb.count > 0) {
        s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list, s_d3d.root_signature);
        s_d3d.cmd_list->lpVtbl->IASetVertexBuffers(s_d3d.cmd_list, 0, 1, &s_d3d.vb_view);

        /* Make the atlas SRV heap current and bind its table (t0) for textured
         * draws. Safe to set even if no draw is textured. */
        if (s_d3d.tex_ready && s_d3d.srv_heap) {
            ID3D12DescriptorHeap* heaps[] = { s_d3d.srv_heap };
            s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list, 1, heaps);
            D3D12_GPU_DESCRIPTOR_HANDLE gh;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
        }

        /* Push the MVP matrix from RSX vertex constants slots 0..3.
         * If the game hasn't written any constants (e.g. placeholder data
         * already in clip space), fall back to identity. */
        float mvp[16];
        const rsx_state* st = s_d3d.current_rsx_state;
        int have_mvp = 0;
        if (st) {
            for (u32 r = 0; r < 4; r++) {
                for (u32 c = 0; c < 4; c++) {
                    float v = st->vertex_constants[r][c];
                    mvp[r * 4 + c] = v;
                    if (v != 0.0f) have_mvp = 1;
                }
            }
        }
        if (!have_mvp) {
            memset(mvp, 0, sizeof(mvp));
            mvp[0] = mvp[5] = mvp[10] = mvp[15] = 1.0f; /* identity */
        }
        s_d3d.cmd_list->lpVtbl->SetGraphicsRoot32BitConstants(
            s_d3d.cmd_list, 0 /*root param 0*/, 16, mvp, 0);

        /* D3D12_IQ=1: dump exactly what the GPU is about to see -- the MVP root
         * constants and the first uploaded host vertices. The legacy path draws
         * with no validation errors yet produces zero pixels, so the geometry
         * must be degenerate/off-screen post-VS. */
        { static int _vd = -1;
          if (_vd < 0) { const char* e = getenv("D3D12_IQ"); _vd = e ? 1 : 0; }
          static int _n = 0;
          if (_vd && _n < 3) {
            _n++;
            fprintf(stderr, "[D3D12-DBG] have_mvp=%d draw_count=%u vb_offset=%u\n",
                    have_mvp, s_rb.count, s_d3d.vb_offset);
            fprintf(stderr, "[D3D12-DBG] mvp rows: [%.2f %.2f %.2f %.2f][%.2f %.2f %.2f %.2f]"
                            "[%.2f %.2f %.2f %.2f][%.2f %.2f %.2f %.2f]\n",
                    mvp[0],mvp[1],mvp[2],mvp[3], mvp[4],mvp[5],mvp[6],mvp[7],
                    mvp[8],mvp[9],mvp[10],mvp[11], mvp[12],mvp[13],mvp[14],mvp[15]);
            if (s_d3d.vb_mapped) {
                /* host vertex = 9 floats (pos3 + col4 + uv2), VERTEX_STRIDE=36 */
                const float* bv = (const float*)s_d3d.vb_mapped;
                for (int k = 0; k < 3; k++) {
                    const float* v = bv + k * 9;
                    fprintf(stderr, "[D3D12-DBG]  hostvert[%d] pos=(%.3f,%.3f,%.3f) col=(%.2f,%.2f,%.2f,%.2f)\n",
                            k, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
                }
            }
            for (u32 d = 0; d < s_rb.count && d < 3; d++) {
                const D3D12DrawRecord* dr = &s_rb.draws[d];
                fprintf(stderr, "[D3D12-DBG]  draw[%u] is_vp=%d is_clear=%d topo=%u cnt=%u vbofs=%u startv=%u rt=%u\n",
                        d, dr->is_vp, dr->is_clear, dr->topology, dr->vertex_count,
                        dr->vb_byte_offset, dr->vb_byte_offset / VERTEX_STRIDE, dr->rt_off);
            }
            fflush(stderr);
          } }

        /* Replay each recorded draw with its own primitive topology and
         * the matching PSO class (triangle / line / point). The PSO class
         * must match the topology or D3D12 rejects the draw. */
        u32 last_topo = 0xFFFFFFFFu;
        ID3D12PipelineState* last_pso = NULL;
        u32 draws = s_rb.count;
        if (draws > MAX_DRAWS) draws = MAX_DRAWS;
        for (u32 d = 0; d < draws; d++) {
            const D3D12DrawRecord* dr = &s_rb.draws[d];
            if (dr->is_vp) continue; /* drawn by the VP pass below */
            frame_detail_tag(FD_LEGACY);

            /* Select PSO: textured triangles (dbgfont) use the atlas PSO;
             * otherwise pick by topology class. */
            ID3D12PipelineState* target_pso = s_d3d.pipeline_state; /* default triangle */
            if (dr->textured && s_d3d.tex_ready && s_d3d.pipeline_state_tex) {
                target_pso = s_d3d.pipeline_state_tex;
            } else if (dr->topology == RSX_TOPOLOGY_POINTS) {
                target_pso = s_d3d.pipeline_state_points
                             ? s_d3d.pipeline_state_points : s_d3d.pipeline_state;
            } else if (dr->topology == RSX_TOPOLOGY_LINES ||
                       dr->topology == RSX_TOPOLOGY_LINE_STRIP) {
                target_pso = s_d3d.pipeline_state_lines
                             ? s_d3d.pipeline_state_lines : s_d3d.pipeline_state;
            }
            if (target_pso != last_pso) {
                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, target_pso);
                last_pso = target_pso;
            }
            if (dr->topology != last_topo) {
                s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, topo_to_d3d(dr->topology));
                last_topo = dr->topology;
            }
            u32 start_vert = dr->vb_byte_offset / VERTEX_STRIDE;
            s_d3d.cmd_list->lpVtbl->DrawInstanced(
                s_d3d.cmd_list, dr->vertex_count, 1, start_vert, 0);
        }
    }

    frame_detail_tag(FD_SETUP);
    /* VP pass: real decompiled vertex program + atlas alpha-test PS. Feeds raw
     * float4 attrib0 from vp_vb and the vp_c[] constant bank. */
    if (s_d3d.vp_ready && s_rb.count > 0) {
        int any = 0;
        for (u32 d = 0; d < s_rb.count && d < MAX_DRAWS; d++)
            if (s_rb.draws[d].is_vp) { any = 1; break; }
        /* Textured geometry (dbgfont atlas) uses the sampling PS; untextured 3D
         * (vkcube) uses the colour-only PS. Fall back to whichever exists. */
        ID3D12PipelineState* vpso =
            (s_d3d.tex_ready && s_d3d.pipeline_state_vp) ? s_d3d.pipeline_state_vp
                                                         : s_d3d.pipeline_state_vp_color;
        if (!vpso) vpso = s_d3d.pipeline_state_vp;
        { static int cap = -1, n = 0;
          if (cap < 0) { const char* e = getenv("VP_SUBMIT"); cap = e ? atoi(e) : 0; }
          if (cap && n < cap) { n++;
            u32 nvp = 0, nclr = 0;
            for (u32 d2 = 0; d2 < s_rb.count && d2 < MAX_DRAWS; d2++) {
                if (s_rb.draws[d2].is_vp)    nvp++;
                if (s_rb.draws[d2].is_clear) nclr++;
            }
            fprintf(stderr, "[VPPASS] records=%u is_vp=%u clears=%u any=%d vpso=%p rootsig=%p\n",
                    s_rb.count, nvp, nclr, any,
                    (void*)vpso, (void*)s_d3d.vp_root_sig); } }
        if (any && vpso) {
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list, s_d3d.vp_root_sig);
            s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, vpso);
            s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootConstantBufferView(s_d3d.cmd_list, 0,
                s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb));
            if (s_vf_buf)   /* RSX_GPU_VFETCH: this batch's records, descriptors and bytes */
                s_d3d.cmd_list->lpVtbl->SetGraphicsRootShaderResourceView(s_d3d.cmd_list, 4,
                    s_vf_buf->lpVtbl->GetGPUVirtualAddress(s_vf_buf) + (u64)s_rb.parity * vf_parity_bytes());
                if(pose_compute_mode()&&s_pose_compute.ready)s_d3d.cmd_list->lpVtbl->SetGraphicsRootShaderResourceView(s_d3d.cmd_list,6,s_pose_compute.output->lpVtbl->GetGPUVirtualAddress(s_pose_compute.output)+s_pose_compute.final_offset);
            ID3D12DescriptorHeap* heaps[] = { s_d3d.srv_heap };
            s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list, 1, heaps);
            D3D12_GPU_DESCRIPTOR_HANDLE gh;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.BufferLocation = s_d3d.vp_vb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_vb)
                               + (u64)s_rb.parity * MAX_VERTICES * 256;
            vbv.SizeInBytes    = MAX_VERTICES * 256;
            vbv.StrideInBytes  = 256;   /* 16 float4 attrib slots */
            s_d3d.cmd_list->lpVtbl->IASetVertexBuffers(s_d3d.cmd_list, 0, 1, &vbv);
            D3D12_GPU_DESCRIPTOR_HANDLE gh_base;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh_base);
            int cur_rt = -1;                       /* target A: -1 = backbuffer */
            float cur_sc = s_rs;                   /* RES_SCALE of the bound target */
            float vp_last_w = 0, vp_last_h = 0;    /* for DRAWARGS */
            float dvp_x = 0, dvp_y = 0, dvp_w = 0, dvp_h = 0;  /* per-draw, for DRAWARGS */
            int cur_m[3] = {-1, -1, -1};           /* MRT B/C/D: -1 = unbound   */
            /* Last pipeline / viewport handed to the command list, so a run of
             * draws with the same state does not re-set it (most of a frame).
             * Anything else that touches them resets these to "unknown". */
            ID3D12PipelineState* last_pso = NULL;
            D3D12_VIEWPORT last_vp; int have_vp = 0;
            memset(&last_vp, 0, sizeof last_vp);
            double _rec0 = perf_on() ? perf_now() : 0.0;
            int seen_small_vp = 0, captured = 0;
            u32 cur_zeta = 0;   /* guest zeta surface the host depth buffer holds */
            const int split_n = (s_cv_query_active || s_cap_steps_on) ? 0 : submit_split_every();
            u32 split_last = 0;
            /* The per-draw bindings, skipped when the same as the previous
             * draw's (shared constant banks and texture windows); 0 = unknown.
             * The buffers' addresses are fixed: asked once, not twice a draw. */
            const D3D12_GPU_VIRTUAL_ADDRESS cb_va = s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb);
            const D3D12_GPU_VIRTUAL_ADDRESS fpcb_va = s_d3d.vp_fpcb
                ? s_d3d.vp_fpcb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_fpcb) : 0;
            u64 bound_srv = 0; D3D12_GPU_VIRTUAL_ADDRESS bound_cb0 = 0, bound_cb2 = 0;
            const int batch_shadow=shadow_batch_on()&&shader_uses_dxc()&&s_vf_buf&&
                !s_cap_steps_on&&!s_cv_query_active&&!cv_diag&&!getenv("FP_ZERO_K");
            for (u32 d = 0; d < s_rb.count && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* dr = &s_rb.draws[d];
                if (split_n && d - split_last >= (u32)split_n && submit_split(fi)) {
                    /* A reopened list has no state: what this pass set once
                     * above, again; the rest is rebound per draw. */
                    split_last = d;
                    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
                    cl->lpVtbl->SetGraphicsRootSignature(cl, s_d3d.vp_root_sig);
                    cl->lpVtbl->SetPipelineState(cl, vpso);
                    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cl->lpVtbl->SetGraphicsRootConstantBufferView(cl, 0,
                        s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb));
                    if (s_vf_buf)
                        cl->lpVtbl->SetGraphicsRootShaderResourceView(cl, 4,
                            s_vf_buf->lpVtbl->GetGPUVirtualAddress(s_vf_buf) + (u64)s_rb.parity * vf_parity_bytes());
                if(pose_compute_mode()&&s_pose_compute.ready)cl->lpVtbl->SetGraphicsRootShaderResourceView(cl,6,s_pose_compute.output->lpVtbl->GetGPUVirtualAddress(s_pose_compute.output)+s_pose_compute.final_offset);
                    ID3D12DescriptorHeap* heaps[] = { s_d3d.srv_heap };
                    cl->lpVtbl->SetDescriptorHeaps(cl, 1, heaps);
                    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh_base);
                    cl->lpVtbl->IASetVertexBuffers(cl, 0, 1, &vbv);
                    cur_rt = -2; for (int m = 0; m < 3; m++) cur_m[m] = -2;
                    last_pso = NULL; have_vp = 0;
                    bound_srv = 0; bound_cb0 = bound_cb2 = 0;
                }
                if (s_cap_steps_on) { bound_srv = 0; bound_cb0 = bound_cb2 = 0; }   /* captures draw between */
                if (d >= pp_done) {   /* this part's prepass, and its queued pipelines */
                    prepass_range(pp_done, pp_done + pp_part);
                    pp_done += pp_part;
                    if (s_fp_inflight) {
                        fp_async_wait_urgent();
                        const double tw = perf_now();
                        fp_async_wait_for(draw_wait_left);
                        draw_wait_left -= perf_now() - tw;
                        fp_async_drain();
                    }
                }
                if(s_fsr_ready&&d==s_fsr_depth_at){
                    rsx_fsr31_depth(s_d3d.cmd_list,s_d3d.depth_buffer,s_fsr_depth_w,s_fsr_depth_h);
                    ID3D12DescriptorHeap* fg_heaps[]={s_d3d.srv_heap};
                    s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list,1,fg_heaps);
                    last_pso=NULL;bound_srv=0;bound_cb0=bound_cb2=0;
                }
                int zc_now = (d == s_zconv_at);
                for (int k = 0; k < s_zconv_nmore && !zc_now; k++) zc_now = (d == s_zconv_more[k]);
                if (zc_now) {
                    frame_detail_tag(FD_DEPTH);
                    zconv_run(gh_base);
                    cur_rt = -2; for (int m = 0; m < 3; m++) cur_m[m] = -2;
                    last_pso = NULL; have_vp = 0;
                    bound_srv = 0; bound_cb0 = bound_cb2 = 0;
                }
                if (!dr->is_vp) continue;
                if(dr->is_copy) {
                    frame_detail_tag(FD_TRANSFER);
                    int si=off_rt_find(dr->copy_src),di=off_rt_find_sz(dr->rt_off,dr->rt_w,dr->rt_h);
                    if(si>=0 && di>=0 && s_d3d.off_rt[si].sc!=s_d3d.off_rt[di].sc) {
                        static int n=0; if(n++<3) fprintf(stderr,"[RES_SCALE] copy %08X -> %08X skipped: target scales differ%c",dr->copy_src,dr->rt_off,10);
                    } else if(si>=0 && di>=0) {
                        OffRT* sr=&s_d3d.off_rt[si];OffRT* ds=&s_d3d.off_rt[di];
                        off_rt_transition(si,D3D12_RESOURCE_STATE_COPY_SOURCE);off_rt_transition(di,D3D12_RESOURCE_STATE_COPY_DEST);
                        D3D12_TEXTURE_COPY_LOCATION sl={0},dl={0};sl.pResource=sr->res;dl.pResource=ds->res;
                        sl.Type=dl.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        const float cs=sr->sc;
                        D3D12_BOX box={rs_dim(dr->copy_sx,cs),rs_dim(dr->copy_sy,cs),0,rs_dim(dr->copy_sx+dr->copy_w,cs),rs_dim(dr->copy_sy+dr->copy_h,cs),1};
                        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list,&dl,rs_dim(dr->copy_dx,cs),rs_dim(dr->copy_dy,cs),0,&sl,&box);
                    } else if(si>=0 && cellGcmOffsetIsDisplay(dr->rt_off)) {
                        /* Blit of the finished frame into a display buffer (Code
                         * Veronica renders offscreen, then copies to the flip
                         * target): that buffer is the backbuffer here. */
                        OffRT* sr=&s_d3d.off_rt[si];
                        { static int n=0; if(n++<3) fprintf(stderr,"[DISPCOPY] %08X -> display %08X (%u,%u %ux%u) dxgi=%u bb=%ux%u%c",
                            dr->copy_src,dr->rt_off,dr->copy_dx,dr->copy_dy,dr->copy_w,dr->copy_h,sr->dxgi,s_d3d.width,s_d3d.height,10); }
                        if(sr->dxgi==DXGI_FORMAT_R8G8B8A8_UNORM && sr->sc==s_rs &&
                           dr->copy_dx+dr->copy_w<=s_d3d.width && dr->copy_dy+dr->copy_h<=s_d3d.height) {
                            off_rt_transition(si,D3D12_RESOURCE_STATE_COPY_SOURCE);
                            D3D12_RESOURCE_BARRIER bb={0};bb.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            bb.Transition.pResource=s_d3d.render_targets[fi];
                            bb.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            bb.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
                            bb.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
                            s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&bb);
                            D3D12_TEXTURE_COPY_LOCATION sl={0},dl={0};sl.pResource=sr->res;dl.pResource=s_d3d.render_targets[fi];
                            sl.Type=dl.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                            const float cs=s_rs;
                            D3D12_BOX box={rs_dim(dr->copy_sx,cs),rs_dim(dr->copy_sy,cs),0,rs_dim(dr->copy_sx+dr->copy_w,cs),rs_dim(dr->copy_sy+dr->copy_h,cs),1};
                            s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list,&dl,rs_dim(dr->copy_dx,cs),rs_dim(dr->copy_dy,cs),0,&sl,&box);
                            bb.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
                            bb.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
                            s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&bb);
                        }
                    } else { static int n=0; if(n++<3) fprintf(stderr,"[DISPCOPY] skipped %08X -> %08X si=%d di=%d disp=%d%c",
                            dr->copy_src,dr->rt_off,si,di,cellGcmOffsetIsDisplay(dr->rt_off),10); }
                    /* Force the next draw to restore target state after barriers. */
                    cur_rt=-2;for(int m=0;m<3;m++)cur_m[m]=-2;
                    continue;
                }
                /* The reduced-viewport passes (this title's reflection, drawn
                 * into a region of the same target) finish here -- snapshot
                 * before the full-screen scene overwrites them. */
                if (!dr->is_clear) {
                    if (dr->vp_w && dr->vp_w < s_d3d.width) {
                        seen_small_vp = 1;
                        subvp_note(dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h);
                    }
                    else if (seen_small_vp && !captured &&
                             dr->vp_w == s_d3d.width && dr->rt_off == 0) {
                        captured = 1;
                        screen_copy_capture(fi);
                        subvp_capture(fi);
                        last_pso = NULL; have_vp = 0;
                        s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(
                            s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);
                    }
                }
                /* Render-to-texture: retarget when this op's surfaces differ.
                 * Depth is a single shared buffer, so clear it per switch. */
                int want  = dr->rt_off  ? off_rt_find_sz(dr->rt_off, dr->rt_w, dr->rt_h)  : -1;
                if(s_frame_detail.active) {
                    int category=dr->is_clear?FD_TRANSFER:rsx_pose_fragment_flags(fp_snap_code(dr))?FD_SHADOW:want>=0?FD_OFFSCREEN:FD_MAIN;
                    float scale=want>=0?s_d3d.off_rt[want].sc:s_rs;
                    u32 width=want>=0?s_d3d.off_rt[want].w:s_d3d.width,height=want>=0?s_d3d.off_rt[want].h:s_d3d.height;
                    frame_detail_scope(category,dr->rt_off,rs_dim(width,scale),rs_dim(height,scale));
                }
                /* Remember where geometry actually went: off_rt_find() matches
                 * on offset alone and slots share offsets at different sizes,
                 * so the composite cannot just look the source up by address. */
                if (want >= 0) { s_last_drawn_slot = want; s_last_drawn_off = dr->rt_off;
                                 if (dr->s_pitch) s_d3d.off_rt[want].gpitch = dr->s_pitch; }
                int wantm[3];
                for (int _m = 0; _m < 3; _m++)
                    wantm[_m] = dr->rt_mrt[_m] ? off_rt_find_sz(dr->rt_mrt[_m], dr->rt_w, dr->rt_h) : -1;
                if (want != cur_rt || wantm[0] != cur_m[0] ||
                    wantm[1] != cur_m[1] || wantm[2] != cur_m[2]) {
                    D3D12_CPU_DESCRIPTOR_HANDLE rh[4];
                    UINT nrt = 1;
                    rh[0] = rtv_handle;
                    D3D12_VIEWPORT vp = {0, 0, (float)s_d3d.width, (float)s_d3d.height, 0.0f, 1.0f};
                    if (cur_rt >= 0) cap_step_snapshot(cur_rt, d);
                    if (want >= 0) {
                        off_rt_transition(want, D3D12_RESOURCE_STATE_RENDER_TARGET);
                        rh[0] = off_rt_rtv(want);
                        vp.Width  = (float)s_d3d.off_rt[want].w;
                        vp.Height = (float)s_d3d.off_rt[want].h;
                    }
                    for (int _m = 0; _m < 3 && wantm[_m] >= 0; _m++) {
                        off_rt_transition(wantm[_m], D3D12_RESOURCE_STATE_RENDER_TARGET);
                        rh[nrt++] = off_rt_rtv(wantm[_m]);
                    }
                    D3D12_RECT sc = {0, 0, (LONG)vp.Width, (LONG)vp.Height};
                    vp_last_w = vp.Width; vp_last_h = vp.Height;
                    s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, nrt, rh, FALSE, &dsv_handle);
                    cur_sc = (want >= 0) ? s_d3d.off_rt[want].sc : s_rs;
                    rs_set_vp(cur_sc, &vp, &sc);
                    have_vp = 0;   /* the per-draw rect below is re-applied at this scale */
                    /* One host depth buffer stands in for every guest zeta
                     * surface, so it is reset when a different zeta comes into
                     * use -- not on every colour-target switch. Clearing per
                     * switch wiped the scene's depth when Code Veronica went
                     * back to its main target after a 320x180 pass, and the
                     * lighter flame then drew through Claire. DEPTH_CLEAR_ON_SWITCH=1
                     * restores the old behaviour. */
                    { static int dcs = -1; if (dcs < 0) dcs = getenv("DEPTH_CLEAR_ON_SWITCH") ? 1 : 0;
                      if (dcs || (dr->zeta_off && dr->zeta_off != cur_zeta)) {
                          s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(s_d3d.cmd_list, dsv_handle,
                              D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, NULL);
                          if (dr->zeta_off) cur_zeta = dr->zeta_off;
                      } }
                    /* RT_CLEARDBG=2: magenta-clear the surface the DRAWS bind,
                     * at the moment they bind it. The guest's own clears may go
                     * to a different surface entirely (they do here: clears to
                     * 0xAB0000, draws to 0x1BE0000), so clearing on the clear
                     * path tests the wrong target. Anything not magenta
                     * afterwards was rasterised. */
                    { static int cd2 = -1;
                      if (cd2 < 0) { const char* e = getenv("RT_CLEARDBG"); cd2 = e ? atoi(e) : 0; }
                      if (cd2 >= 2 && want >= 0) {
                          const float mag[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
                              s_d3d.cmd_list, off_rt_rtv(want), mag, 0, NULL);
                      } }
                    /* VP_TESTTRI=1: our own triangle, through the game's exact
                     * pipeline -- same vpso, same root signature, same vertex
                     * buffer, same RTV just bound. Written at vertex 1000 so it
                     * cannot collide with the guest's data. Pair with
                     * VP_BYPASS=1 so the shader is a trivial passthrough.
                     *
                     * If this renders, the GPU does read vp_vb and the fault is
                     * in the guest draws' own state. If it does not, nothing
                     * this backend draws reaches a render target at all. */
                    { static int tt = -1;
                      if (tt < 0) { const char* e = getenv("VP_TESTTRI"); tt = e ? atoi(e) : 0; }
                      if (tt && want >= 0 && s_d3d.vp_vb_mapped) {
                          float* vtx = (float*)((char*)s_d3d.vp_vb_mapped
                              + (u64)s_rb.parity * MAX_VERTICES * 256u
                              + (u64)1000 * 256u);
                          static const float tri[3][4] = {
                              {-2.0f, -2.0f, 0.0f, 1.0f},
                              { 2.0f, -2.0f, 0.0f, 1.0f},
                              { 0.0f,  2.0f, 0.0f, 1.0f} };
                          for (int _v = 0; _v < 3; _v++) {
                              float* dstv = vtx + (size_t)_v * (256u / 4u);
                              dstv[0] = tri[_v][0]; dstv[1] = tri[_v][1];
                              dstv[2] = tri[_v][2]; dstv[3] = tri[_v][3];
                          }
                          s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, 3, 1, 1000, 0);
                          { static int _n = 0;
                            if (_n++ < 3) fprintf(stderr, "[TESTTRI] issued on rt=%d off=0x%08X%c",
                                                  want, s_d3d.off_rt[want].off, 10); }
                      } }
                    cur_rt = want;
                    for (int _m = 0; _m < 3; _m++) cur_m[_m] = wantm[_m];
                    have_vp = 0;   /* the switch above set its own viewport/scissor */
                    last_pso = NULL;   /* VP_TESTTRI may have drawn with another */
                    bound_srv = 0; bound_cb0 = bound_cb2 = 0;
                }
                /* rsxcap: the post chain rewrites one target over and over;
                 * photograph it before each of the last 40 ops. */
                if (s_cap_steps_on && cur_rt >= 0 && d + 40 >= s_rb.count &&
                    s_d3d.off_rt[cur_rt].w >= 640)
                    cap_step_snapshot(cur_rt, d);
                /* F12 also samples full-scene colour draws automatically.
                 * Shadow atlas viewports and depth-only/channel-mask passes
                 * are excluded, leaving room for the post-chain snapshots.
                 * This follows variable draw counts without a launch-time
                 * RSXCAP_STEP_RANGE and does no work outside capture frames. */
                if (s_cap_steps_on && cur_rt >= 0 && d + 40 < s_rb.count &&
                    !dr->is_clear && (dr->cmask & 7u) == 7u &&
                    dr->vp_w >= 640 && dr->vp_h >= 360 && (d & 31u) == 0)
                    cap_step_snapshot(cur_rt, d);
                /* RSXCAP_STEP_RANGE=<lo>,<hi>,<stride>: also photograph the
                 * bound target every <stride> ops in [lo, hi] -- finds the op
                 * that paints a region, by bisection over captures. */
                { static int rlo = -2, rhi = 0, rst = 1;
                  if (rlo == -2) { const char* e = getenv("RSXCAP_STEP_RANGE"); rlo = -1;
                      if (e && sscanf(e, "%d,%d,%d", &rlo, &rhi, &rst) < 2) rlo = -1;
                      if (rst < 1) rst = 1; }
                  if (s_cap_steps_on && rlo >= 0 && cur_rt >= 0 && (int)d >= rlo && (int)d <= rhi &&
                      ((int)d - rlo) % rst == 0)
                      cap_step_snapshot(cur_rt, d); }
                /* RSXCAP_STEP_VTX=<n>: photograph the target before and right
                 * after every full-screen draw of exactly n vertices (one draw
                 * by its mesh, whatever its op number this frame). */
                { static int vn = -2; static u32 next = ~0u;
                  if (vn == -2) { const char* e = getenv("RSXCAP_STEP_VTX"); vn = e ? atoi(e) : -1; }
                  if (s_cap_steps_on && vn > 0 && cur_rt >= 0) {
                      if (d == next) cap_step_snapshot(cur_rt, d);
                      if ((int)dr->vertex_count == vn && dr->vp_w >= 640) { cap_step_snapshot(cur_rt, d); next = d + 1; }
                  } }
                if (dr->is_clear) {
                    D3D12_CPU_DESCRIPTOR_HANDLE rh =
                        (cur_rt >= 0) ? off_rt_rtv(cur_rt) : rtv_handle;
                    /* Honour the CLEAR_SURFACE mask: a depth-only clear (0x3)
                     * must not wipe the colour target, and vice versa. A
                     * zero mask keeps the old colour-only behaviour. */
                    if (dr->clr_flags & 0x3u) {
                        D3D12_CLEAR_FLAGS zf = (D3D12_CLEAR_FLAGS)0;
                        if (dr->clr_flags & 1u) zf |= D3D12_CLEAR_FLAG_DEPTH;
                        if (dr->clr_flags & 2u) zf |= D3D12_CLEAR_FLAG_STENCIL;
                        s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(s_d3d.cmd_list, dsv_handle,
                            zf, dr->clr_depth, dr->clr_stencil, 0, NULL);
                        if (dr->zeta_off) cur_zeta = dr->zeta_off;
                    }
                    if (dr->clr_flags && !(dr->clr_flags & 0xF0u)) continue;
                    /* RT_CLEARDBG=1: clear offscreen targets to magenta rather
                     * than the guest colour. "0.000% nonzero" cannot tell an
                     * untouched surface from one written black, and that
                     * ambiguity invalidates every read of an offscreen target.
                     * If the surface comes back magenta the clears land and the
                     * draws are writing black; if it stays black, nothing
                     * reaches it at all. */
                    { static int cd = -1;
                      if (cd < 0) { const char* e = getenv("RT_CLEARDBG"); cd = e ? atoi(e) : 0; }
                      if (cd && cur_rt >= 0) {
                          const float mag[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, mag, 0, NULL);
                      } else
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, dr->cc, 0, NULL); }
                    for (int _m = 0; _m < 3; _m++)
                        if (cur_m[_m] >= 0) {
                            D3D12_CPU_DESCRIPTOR_HANDLE rhm = off_rt_rtv(cur_m[_m]);
                            s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rhm, dr->cc, 0, NULL);
                        }
                    continue;
                }
                /* Per-draw pipeline: prefer the guest's own compiled FP; fall
                 * back to the hardcoded atlas/colour PS pair. */
                ID3D12PipelineState* dpso =fp_prepared_ready(d,dr);
                if(!dpso)dpso =
                    dr->fp_addr ? vp_get_fp_pso(dr->vs_idx, dr->fp_addr, dr->blend_key,
                                                dr_num_rts(dr),
                                                dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                           : DXGI_FORMAT_R8G8B8A8_UNORM,
                                                dr->fp_exp32, dr->cmask,
                                                dr->cull | ((dr->topology == RSX_TOPOLOGY_LINES) ? 0x100u : 0u),
                                                dr_cube_mask(dr), fp_snap_ptr(dr), dr_fp_urgent(dr)) : NULL;
                /* Submitted just now, or still building: a build from the
                 * disk caches takes about a millisecond, so wait for it (out
                 * of the frame's budget) rather than drop the draw -- the
                 * character and props vanished for a frame or more each time
                 * a room's pipelines came back from disk. */
                if (!dpso && dr->fp_addr && s_fp_skip_draw &&
                    ((s_fp_inflight && (draw_wait_left > 0.0 || s_fp_urgent_inflight)) || s_fpq_done)) {
                    fp_async_wait_urgent();
                    const double tw = perf_now();
                    fp_async_wait_for(draw_wait_left);
                    draw_wait_left -= perf_now() - tw;
                    fp_async_drain();
                    dpso = vp_get_fp_pso(dr->vs_idx, dr->fp_addr, dr->blend_key, dr_num_rts(dr),
                                         dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                    : DXGI_FORMAT_R8G8B8A8_UNORM,
                                         dr->fp_exp32, dr->cmask,
                                         dr->cull | ((dr->topology == RSX_TOPOLOGY_LINES) ? 0x100u : 0u),
                                         dr_cube_mask(dr), fp_snap_ptr(dr), dr_fp_urgent(dr));
                }
                if (!dpso && dr->fp_addr && s_fp_skip_draw) { s_fp_drawn_skips++; continue; }
                { ID3D12PipelineState* use = dpso ? dpso : vpso;
                  if (use != last_pso) {
                      s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, use);
                      last_pso = use;
                  } }
                /* Per-draw viewport: the guest rect when sane, else the
                 * full target. Scissor tracks the same rect. */
                {
                    float tw = (cur_rt >= 0) ? (float)s_d3d.off_rt[cur_rt].w : (float)s_d3d.width;
                    float th = (cur_rt >= 0) ? (float)s_d3d.off_rt[cur_rt].h : (float)s_d3d.height;
                    D3D12_VIEWPORT dvp = {0, 0, tw, th, 0.0f, 1.0f};
                    if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
                        (float)(dr->vp_x + dr->vp_w) <= tw + 0.5f &&
                        (float)(dr->vp_y + dr->vp_h) <= th + 0.5f) {
                        dvp.TopLeftX = (float)dr->vp_x;
                        dvp.TopLeftY = (float)dr->vp_y;
                        dvp.Width    = (float)dr->vp_w;
                        dvp.Height   = (float)dr->vp_h;
                    }
                    D3D12_RECT dsc = {(LONG)dvp.TopLeftX, (LONG)dvp.TopLeftY,
                                      (LONG)(dvp.TopLeftX + dvp.Width),
                                      (LONG)(dvp.TopLeftY + dvp.Height)};
                    dvp_x = dvp.TopLeftX; dvp_y = dvp.TopLeftY;
                    dvp_w = dvp.Width;    dvp_h = dvp.Height;
                    if (!have_vp || memcmp(&dvp, &last_vp, sizeof dvp) != 0) {
                        rs_set_vp(cur_sc, &dvp, &dsc);
                        last_vp = dvp; have_vp = 1;
                    }
                }
                /* Per-draw textures: bind this draw's t0-t3 SRV window.
                 * Any sampled offscreen RT transitions to PSR first (never
                 * one of the currently-bound colour targets). */
                for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
                    int rt = dr->tex_rt[_u];
                    if (rt >= 0 && rt != cur_rt &&
                        rt != cur_m[0] && rt != cur_m[1] && rt != cur_m[2])
                        off_rt_transition(rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    else if (rt >= 0 && rt == cur_rt && rt != s_zview_slot &&
                             !getenv("RT_NOFEEDBACK")) {
                        /* Samples its own target: read a copy (see
                         * off_rt_feedback_copy). This draw's window slot is
                         * unique to it, so rewriting it here is safe. */
                        ID3D12Resource* fb = off_rt_feedback_copy(rt);
                        if (fb) {
                            srv_write(DRAW_SRV_BASE + d * FP_TEX_UNITS + _u, fb,
                                      (DXGI_FORMAT)s_d3d.off_rt[rt].dxgi,
                                      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                            static int n = 0;
                            if (n++ < 4) fprintf(stderr, "[RTFB] draw %u samples its own target 0x%08X: using a copy%c",
                                                 d, s_d3d.off_rt[rt].off, 10);
                        }
                    }
                }
                D3D12_GPU_DESCRIPTOR_HANDLE gh = gh_base;
                gh.ptr += (u64)(DRAW_SRV_BASE + s_srv_win[d] * FP_TEX_UNITS) * s_d3d.srv_inc;
                if (gh.ptr != bound_srv) {
                    s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
                    bound_srv = gh.ptr;
                }
                if (dr->vtex_mask) {   /* t16-t19 for the transform program's TXL */
                    D3D12_GPU_DESCRIPTOR_HANDLE vh = gh_base;
                    vh.ptr += (u64)(VTEX_SRV_BASE + s_vtex_win[d] * 4) * s_d3d.srv_inc;
                    s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 3, vh);
                }
                /* Per-draw constants: this draw's vp_cb + FP texscale slots. */
                { const D3D12_GPU_VIRTUAL_ADDRESS a0 = cb_va + ((u64)s_rb.parity * MAX_DRAWS + dr->vcb_slot) * VP_CB_STRIDE;
                  if (a0 != bound_cb0) {
                      s_d3d.cmd_list->lpVtbl->SetGraphicsRootConstantBufferView(s_d3d.cmd_list, 0, a0);
                      bound_cb0 = a0;
                  } }
                if (s_d3d.vp_fpcb) {
                    const D3D12_GPU_VIRTUAL_ADDRESS a2 = fpcb_va + ((u64)s_rb.parity * MAX_DRAWS + dr->cb_slot) * VP_FPCB_STRIDE;
                    if (a2 != bound_cb2) {
                        s_d3d.cmd_list->lpVtbl->SetGraphicsRootConstantBufferView(s_d3d.cmd_list, 2, a2);
                        bound_cb2 = a2;
                    }
                }
                /* DRAWARGS=<N>: the arguments actually handed to the GPU, plus
                 * the target they are aimed at. A zero vertex count, or a start
                 * vertex past the buffer, rasterises nothing while every other
                 * check upstream still says the draw is fine. */
                { static int dcap = -1, dn = 0;
                  if (dcap < 0) { const char* e = getenv("DRAWARGS"); dcap = e ? atoi(e) : 0; }
                  if (dcap && dn < dcap) { dn++;
                      D3D12_CPU_DESCRIPTOR_HANDLE _rh =
                          (cur_rt >= 0) ? off_rt_rtv(cur_rt) : rtv_handle;
                      fprintf(stderr, "[DRAWARGS] verts=%u start=%u rt=%d rec=0x%08X bound=0x%08X"
                              " tex0=0x%08X texrt=%d dvp=%.0f,%.0f %.0fx%.0f%c",
                              dr->vertex_count, dr->vb_byte_offset / 256, cur_rt,
                              dr->rt_off,
                              (cur_rt >= 0) ? s_d3d.off_rt[cur_rt].off : 0u,
                              dr->tex[0].raw, dr->tex_rt[0],
                              (double)dvp_x, (double)dvp_y,
                              (double)dvp_w, (double)dvp_h, 10); } }
                /* RT_REBIND=1: re-issue OMSetRenderTargets immediately before
                 * every draw instead of only when the target changes. A clear
                 * proves the RTV descriptor is good but not that the binding is
                 * still live at draw time -- ClearRenderTargetView writes
                 * through the descriptor and never consults the OM. If anything
                 * between the retarget and here drops the binding, this restores
                 * it. */
                { static int rb = -1;
                  if (rb < 0) { const char* e = getenv("RT_REBIND"); rb = e ? atoi(e) : 0; }
                  if (rb) {
                      D3D12_CPU_DESCRIPTOR_HANDLE brh[4];
                      UINT bn = 1;
                      brh[0] = (cur_rt >= 0) ? off_rt_rtv(cur_rt) : rtv_handle;
                      for (int _m = 0; _m < 3; _m++)
                          if (cur_m[_m] >= 0) brh[bn++] = off_rt_rtv(cur_m[_m]);
                      s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(
                          s_d3d.cmd_list, bn, brh, FALSE, &dsv_handle);
                  } }
                u32 batch_count=1,draw_vertices=dr->vertex_count;
                if(batch_shadow&&dpso&&dr->vcb_slot<MAX_DRAWS&&dr->cb_slot<MAX_DRAWS&&
                   dr->fp_snap&&s_rb.fp_arena&&dr->fp_bytes==240&&fp_snap_code(dr)==0x7EA4C4E7u) {
                    u32 used=0xFFFFu;
                    memcpy(&used,s_rb.fp_arena+dr->fp_snap-1+dr->fp_bytes+20,4);
                    u32 nk=0;
                    memcpy(&nk,s_rb.fp_arena+dr->fp_snap-1+dr->fp_bytes+16,4);
                    if(used==0&&nk==5) {
                    D3D12DrawRecord run=*dr;
                    /* Stay within the prepared range and command-list split.
                     * Never cross a scheduled depth-conversion operation. */
                    for(u32 next=d+1;next<s_rb.count&&next<MAX_DRAWS&&next<pp_done;next++) {
                        if(split_n&&next-split_last>=(u32)split_n)break;
                        int boundary=next==s_zconv_at||(s_fsr_ready&&next==s_fsr_depth_at);
                        for(int k=0;k<s_zconv_nmore&&!boundary;k++)boundary=next==s_zconv_more[k];
                        if(boundary)break;
                        const D3D12DrawRecord* nd=&s_rb.draws[next];
                        if(nd->cb_slot>=MAX_DRAWS||nd->vcb_slot>=MAX_DRAWS)break;
                        const u8* nu=fp_snap_ptr(nd);
                        if(!nu||nd->fp_bytes!=240)break;
                        u32 nnk=0;memcpy(&nnk,nu+nd->fp_bytes+16,4);
                        if(nnk!=5||!rsx_shadow_batch_pair(&run,nd,fp_snap_ptr(dr),nu,
                            fp_snap_ptr(dr)+dr->fp_bytes+32,nu+nd->fp_bytes+32))break;
                        if(fp_prepared_ready(next,nd)!=dpso)break;
                        run.vertex_count+=nd->vertex_count;batch_count++;
                    }
                    draw_vertices=run.vertex_count;
                    if(batch_count>1){s_shadow_batch_groups++;s_shadow_batch_saved+=batch_count-1;}
                    }
                }
                if (s_cv_query_active) s_d3d.cmd_list->lpVtbl->BeginQuery(
                    s_d3d.cmd_list,s_cv_query_heap,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,d);
                { const int ln = (dr->topology == RSX_TOPOLOGY_LINES);
                  if (ln) s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                  if (s_vf_buf) {
                      s_d3d.cmd_list->lpVtbl->SetGraphicsRoot32BitConstant(s_d3d.cmd_list, 5,
                          dr->vb_byte_offset / 256, 0);
                      s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, draw_vertices, 1, 0, 0);
                  } else
                  s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,
                      dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);
                  if (ln) s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); }
                if (s_cv_query_active) {
                    s_d3d.cmd_list->lpVtbl->EndQuery(s_d3d.cmd_list,s_cv_query_heap,
                        D3D12_QUERY_TYPE_PIPELINE_STATISTICS,d);
                    s_d3d.cmd_list->lpVtbl->ResolveQueryData(s_d3d.cmd_list,s_cv_query_heap,
                        D3D12_QUERY_TYPE_PIPELINE_STATISTICS,d,1,s_cv_query_readback,
                        (u64)d*sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
                    s_cv_query_valid[d]=1;
                }
                /* VP_SUBMIT=<N>: prove the VP pass actually reaches the GPU.
                 * "records exist" and "draws were submitted" are different
                 * claims, and every blank-output investigation conflates them. */
                { static int cap = -1, n = 0;
                  if (cap < 0) { const char* e = getenv("VP_SUBMIT"); cap = e ? atoi(e) : 0; }
                  if (cap && n < cap) { n++;
                    fprintf(stderr, "[VPSUBMIT] draw[%u] verts=%u pso=%s vp=%ux%u tex0=0x%X(set=%d) tex1=0x%X slot=%d fp=0x%X%c",
                            d, dr->vertex_count, dpso ? "guest-fp" : "fallback",
                            dr->vp_w, dr->vp_h, dr->tex[0].raw, dr->tex[0].set,
                            dr->tex[1].raw, dr->tex_slot, dr->fp_addr, 10);
                    fprintf(stderr, "            vs_idx=%d cb_slot=%u vbofs=%u cull=%u blend=%d%c",
                            dr->vs_idx, dr->cb_slot, dr->vb_byte_offset / 256,
                            dr->cull, dr->blend, 10); } }
                d+=batch_count-1;
            }
            if (perf_on()) s_perf_gpu += perf_now() - _rec0;   /* reuse: record time */
            /* Leave the backbuffer bound for the dump/present epilogue. */
            if (cur_rt >= 0) {
                s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);
                D3D12_VIEWPORT vp = {0, 0, (float)s_d3d.width, (float)s_d3d.height, 0.0f, 1.0f};
                D3D12_RECT sc = {0, 0, (LONG)s_d3d.width, (LONG)s_d3d.height};
                rs_set_vp(s_rs, &vp, &sc);
            }
        }
    }

    if (perf_on()) {
        double el = perf_now() - _rf0;
        s_perf_rf += el;
        if (el > 0.050) {
            static int hn = 0;
            if (hn++ < 200)
                fprintf(stderr, "[RENDER-HITCH] %.1fms draws=%u | gpu-wait %.1f pso-drain/save %.1f"
                        " prepass %.1f (tex %.1f in %d uploads, srv %.1f, pso %.1f) | rest %.1f%c",
                        el * 1000.0, s_rb.count, (s_perf_gpu - _h_gpu) * 1000.0, _h_misc * 1000.0,
                        (s_perf_pre - _h_pre) * 1000.0, (s_perf_tex - _h_tex) * 1000.0,
                        s_perf_ntex - _h_ntex, (s_perf_srv - _h_srv) * 1000.0,
                        (s_perf_pso - _h_pso) * 1000.0,
                        (el - (s_perf_gpu - _h_gpu) - _h_misc - (s_perf_pre - _h_pre)) * 1000.0, 10);
        }
    }
    /* MEMPEEK=<hex addr>:<n>: n big-endian floats at a guest address each frame,
     * with a min/max so a buffer of constants is distinguishable from real data. */
    { const char* mp = getenv("MEMPEEK");
      if (mp) { extern uint8_t* vm_base; static int _n = 0;
        u32 addr = 0; int cnt = 8; sscanf(mp, "%x:%d", &addr, &cnt);
        if (vm_base && addr && _n++ < 6) {
            float lo = 1e30f, hi = -1e30f; u32 nz = 0;
            for (int i = 0; i < cnt; i++) {
                const u8* q = vm_base + addr + i * 4;
                u32 bits = ((u32)q[0]<<24)|((u32)q[1]<<16)|((u32)q[2]<<8)|q[3];
                float f; memcpy(&f, &bits, 4);
                if (f == f && f < 1e30f && f > -1e30f) { if (f < lo) lo = f; if (f > hi) hi = f; }
                if (bits) nz++;
            }
            fprintf(stderr, "[MEMPEEK] 0x%08X n=%d nonzero=%u range[%g..%g]%c",
                    addr, cnt, nz, lo, hi, 10);
        } } }
    /* VRAMSCAN=1: which 64 KB blocks of guest memory change between frames.
     * "nothing writes the buffer we read" is only half an answer -- this says
     * where the producer IS writing, so the two can be matched up. */
    { static int vs = -1; if (vs < 0) vs = getenv("VRAMSCAN") ? 1 : 0;
      if (vs) { extern uint8_t* vm_base;
        static u32 prev[512]; static int have = 0; static int reported = 0;
        u32 base = 0xC6000000u; u32 nblk = 512;          /* 32 MB window */
        const char* bs = getenv("VRAMSCAN_BASE");
        if (bs) base = (u32)strtoul(bs, NULL, 0);
        if (vm_base && reported < 6) {
            u32 changed = 0; char list[512]; int ln = 0;
            for (u32 b = 0; b < nblk; b++) {
                const u8* p = vm_base + base + b * 0x10000u;
                u32 h = 2166136261u;
                for (u32 i = 0; i < 0x10000u; i += 61) h = (h ^ p[i]) * 16777619u;
                if (have && h != prev[b]) { changed++;
                    if (ln < 400) ln += snprintf(list + ln, sizeof(list) - ln,
                                                 " +0x%X", b * 0x10000u); }
                prev[b] = h;
            }
            if (have && changed) { reported++;
                fprintf(stderr, "[VRAMSCAN] base=0x%08X %u/512 blocks changed:%s%c",
                        base, changed, list, 10); }
            have = 1;
        } } }
    /* FRAME_BUDGET=1: geometry a frame asked for vs what the batch can hold.
     * Reported from render_frame so it works under RSX_ACCUM_FRAME too -- it
     * used to live in the clear-boundary present path, which accum bypasses, so
     * it silently produced nothing in exactly the configuration being used. */
    { static int fb = -1;
      if (fb < 0) { const char* e = getenv("FRAME_BUDGET"); fb = e ? atoi(e) : 0; }
      if (fb && s_req_draws) { static int n = 0; if (n++ < 40)
        fprintf(stderr, "[BUDGET] frame: requested %llu verts / %llu draws, "
                        "buffer holds %u verts, %llu draws truncated%c",
                (unsigned long long)s_req_verts, (unsigned long long)s_req_draws,
                MAX_VERTICES, (unsigned long long)s_drop_draws, 10); }
      s_req_verts = 0; s_req_draws = 0; s_drop_draws = 0; }
    /* Always on, silent while the frame rate is healthy: one line per 2 s
     * window in which fewer than 10 frames reached the screen, with how many
     * flips the title asked for meanwhile. The slow intro after New Game comes
     * and goes between runs, so the evidence has to be in every normal log. */
    { static double w0 = 0.0; static unsigned frames = 0; static unsigned long long req0 = 0;
      extern unsigned long long g_gcm_flip_requests;
      double now = perf_now();
      if (w0 == 0.0) { w0 = now; req0 = g_gcm_flip_requests; }
      frames++;
      if (now - w0 >= 2.0) {
          if (frames < 10)
              fprintf(stderr, "[SLOWFRAME] %.2f fps over %.1fs: %u presents, %llu flips requested%c",
                      frames / (now - w0), now - w0, frames,
                      g_gcm_flip_requests - req0, 10);
          w0 = now; frames = 0; req0 = g_gcm_flip_requests;
      } }
    if (perf_on()) {
        static double s_t_prev = 0.0; static int s_n = 0;
        double now = perf_now();
        if (s_t_prev > 0.0) s_perf_frame += now - s_t_prev;
        s_t_prev = now;
        /* Every 20 frames, or every 5 s when the title crawls below 4 fps. */
        if (++s_n >= 20 || s_perf_frame >= 5.0) {
            double fr = s_perf_frame > 0 ? s_perf_frame : 1;
            double nfr = (double)s_n; s_n = 0;
            fprintf(stderr, "[PERF] %.2f fps | tex %.2fs (%.0f%%, %d calls) | vtx %.2fs"
                            " (%.0f%%, %.0fk verts) | render_frame %.2fs (%.0f%%)"
                            " [gpuwait %.2fs %.0f%%]"
                            " | prepass %.2fs (%.0f%%) [srv %.2fs %.0f%% | pso %.2fs %.0f%% %d calls %d MISS %dKB hashed] | guest %.2fs (%.0f%%)%c",
                    nfr / fr,
                    s_perf_tex, 100.0 * s_perf_tex / fr, s_perf_ntex,
                    s_perf_vtx, 100.0 * s_perf_vtx / fr,
                    (double)s_perf_nverts / 1000.0,
                    s_perf_rf, 100.0 * s_perf_rf / fr,
                    s_perf_gpu, 100.0 * s_perf_gpu / fr,
                    s_perf_pre, 100.0 * s_perf_pre / fr,
                    s_perf_srv, 100.0 * s_perf_srv / fr,
                    s_perf_pso, 100.0 * s_perf_pso / fr,
                    s_perf_pso_calls, s_perf_pso_miss, s_perf_pso_hashbytes / 1024,
                    fr - s_perf_rf, 100.0 * (fr - s_perf_rf) / fr, 10);
            { extern double g_gcm_fifo_secs;
              fprintf(stderr, "[PERF-FIFO] drain %.2fs (%.0f%% of wall, includes catch-up presents)%c",
                      g_gcm_fifo_secs, 100.0 * g_gcm_fifo_secs / fr, 10);
              g_gcm_fifo_secs = 0.0;
              fprintf(stderr, "[PERF-VTEX] write-watch %s | unchanged (no compare) %llu | compared %.1f MB | copied %.1f MB%c",
                      s_ww_ok == 1 ? "on" : s_ww_ok == 0 ? "UNAVAILABLE" : "unused",
                      (unsigned long long)s_vtex_skip, s_vtex_cmp_bytes / 1048576.0,
                      s_vtex_copy_bytes / 1048576.0, 10);
              s_vtex_skip = 0; s_vtex_cmp_bytes = 0; s_vtex_copy_bytes = 0;
              fprintf(stderr, "[PERF-VCB] vertex constants: %u banks written, %u reused"
                              " | vertices: %llu converted, %llu reused"
                              " | texture windows: %llu written, %llu reused%c",
                      s_vcb_copied, s_vcb_reused, s_vmemo_miss, s_vmemo_hits,
                      s_srv_written, s_srv_reused, 10);
              s_vcb_copied = 0; s_vcb_reused = 0; s_vmemo_miss = 0; s_vmemo_hits = 0;
              s_srv_written = 0; s_srv_reused = 0;
              { LONG hh = InterlockedExchange(&s_shc_hits, 0), mm = InterlockedExchange(&s_shc_miss, 0);
                LONG pl = InterlockedExchange(&s_plib_loaded, 0);
                LONG ab = InterlockedExchange(&s_fp_async_built, 0), as = InterlockedExchange(&s_fp_async_skips, 0);
                if (hh || mm || pl || ab || as || s_fp_drawn_skips)
                    fprintf(stderr, "[PERF-SHADER] bytecode: %ld from disk, %ld compiled | pipelines: %ld"
                                    " from library | background: %ld built (%.1f ms each), %ld lookups pending, %ld draws left out%c",
                            (long)hh, (long)mm, (long)pl, (long)ab,
                            ab ? InterlockedExchange(&s_fp_build_us, 0) / 1000.0 / ab : 0.0, (long)as,
                            (long)s_fp_drawn_skips, 10);
                s_fp_drawn_skips = 0; } }
            /* PERF_THREADS=1 only: it snapshots every thread on the system
             * (Toolhelp) and queries each one's times -- ~50 ms, on the render
             * thread, every 20 frames. That stall held up the FIFO drain and
             * the vblank clock behind it, and was the regular spike in every
             * frametime graph taken with PERF=1. */
            { static int pt = -1; if (pt < 0) pt = getenv("PERF_THREADS") ? 1 : 0;
              if (pt) perf_thread_report(fr); }
            { extern void spu_coh_stats_report(void*); spu_coh_stats_report(stderr); }
            s_perf_frame = 0.0; s_perf_tex = 0.0; s_perf_vtx = 0.0; s_perf_rf = 0.0;
            s_perf_gpu = 0.0; s_perf_pre = 0.0; s_perf_srv = 0.0; s_perf_pso = 0.0;
            s_perf_pso_calls = 0; s_perf_pso_miss = 0; s_perf_pso_hashbytes = 0;
            s_perf_ntex = 0; s_perf_texbytes = 0; s_perf_nverts = 0;
        }
    }
    { static int dt = -1;
      if (dt < 0) { const char* e = getenv("RSX_DBG_TRACK"); dt = e ? atoi(e) : 0; }
      if (dt) { static int fr = 0; fr++;
        if (s_lock_n)
            fprintf(stderr, "[RSX_DBG_TRACK] frame %d: %u duck draws, ndc=(%.3f,%.3f)%c",
                    fr, s_lock_n, (float)(s_lock_sx/s_lock_n), (float)(s_lock_sy/s_lock_n), 10);
        else if (fr % 20 == 0)
            fprintf(stderr, "[RSX_DBG_TRACK] frame %d: NO duck draws%c", fr, 10);
      } }
    if (s_lock_n) {                       /* publish this frame's centroid */
        s_lock_x = (float)(s_lock_sx / s_lock_n);
        s_lock_y = (float)(s_lock_sy / s_lock_n);
        s_lock_valid = 1;
        s_lock_sx = s_lock_sy = 0.0; s_lock_n = 0;
    }
    /* CV_FRAMELOG=1: one line per batch -- what it drew, where, and whether it
     * was presented. A screen that flickers between two pictures is two kinds
     * of batch alternating; this names both. */
    { static int fl = -1; if (fl < 0) fl = getenv("CV_FRAMELOG") ? 1 : 0;
      static int lines = 0, armed = 0;   /* start at the first gameplay-sized batch */
      if (s_rb.count > 1000) armed = 1;
      if (fl && armed && lines < 600 && s_rb.count > 0) {
          u32 disp = 0, clr = 0, cpy = 0, cpy_disp = 0, off = 0;
          for (u32 i = 0; i < s_rb.count && i < MAX_DRAWS; i++) {
              const D3D12DrawRecord* r = &s_rb.draws[i];
              if (r->is_copy) { cpy++; if (cellGcmOffsetIsDisplay(r->rt_off)) cpy_disp++; }
              else if (r->is_clear) clr++;
              else if (!r->rt_off) disp++;
              else off++;
          }
          const D3D12DrawRecord* f0 = &s_rb.draws[0];
          fprintf(stderr, "[FRAME] #%llu draws=%u present=%d display=%u offscreen=%u clears=%u copies=%u(to display %u)"
                          " first: %s rt=0x%08X t0=0x%08X zconv=%d%c",
                  (unsigned long long)s_d3d.frame_count, s_rb.count, s_rb.present,
                  disp, off, clr, cpy, cpy_disp,
                  f0->is_copy ? "COPY" : f0->is_clear ? "CLEAR" : "DRAW", f0->rt_off, f0->tex[0].raw,
                  s_zconv_at != 0xFFFFFFFFu, 10);
          lines++;
      } }
    /* CV_SMALLFRAME=<N>: every presented batch of at most N records, record by
     * record -- the frames around a room load are a handful of draws, and the
     * one that shows garbage is among them. */
    { static int mx = -1; if (mx < 0) { const char* e = getenv("CV_SMALLFRAME"); mx = e ? atoi(e) : 0; }
      static int lines = 0;
      if (s_rb.count > 1000) s_gameplay_seen = 1;   /* menus and title before it are small too */
      if (mx > 0 && s_gameplay_seen && s_rb.present && s_rb.count > 0 && s_rb.count <= (u32)mx && lines < 8000) {
          fprintf(stderr, "[SMALLF] #%llu t=%llu records=%u%c", (unsigned long long)s_d3d.frame_count,
                  (unsigned long long)GetTickCount64(), s_rb.count, 10);
          lines++;
          for (u32 i = 0; i < s_rb.count && lines < 8000; i++, lines++) {
              const D3D12DrawRecord* r = &s_rb.draws[i];
              if (r->is_clear) { fprintf(stderr, "[SMALLF]   %u CLEAR rt=0x%08X flags=0x%X c=%.2f,%.2f,%.2f%c",
                                         i, r->rt_off, r->clr_flags, r->cc[0], r->cc[1], r->cc[2], 10); continue; }
              if (r->is_copy) { fprintf(stderr, "[SMALLF]   %u COPY 0x%08X -> rt=0x%08X %ux%u%c",
                                        i, r->copy_src, r->rt_off, r->copy_w, r->copy_h, 10); continue; }
              fprintf(stderr, "[SMALLF]   %u DRAW rt=0x%08X %ux%u vp=%u,%u %ux%u fp=0x%X n=%u blend=%d",
                      i, r->rt_off, r->rt_w, r->rt_h, r->vp_x, r->vp_y, r->vp_w, r->vp_h,
                      r->fp_addr, r->vertex_count, r->blend);
              for (int u = 0; u < FP_TEX_UNITS; u++)
                  if (r->tex[u].set)
                      fprintf(stderr, " t%d=0x%08X(off 0x%08X) %ux%u fmt=0x%02X rt=%d", u, r->tex[u].raw,
                              r->tex[u].off, r->tex[u].w, r->tex[u].h, r->tex[u].fmt, r->tex_rt[u]);
              fprintf(stderr, "%c", 10);
          }
      } }
    if (cv_cutscene_diag() && s_rb.count >= 12288) {
        static unsigned int reports = 0;
        if (reports++ < 120)
            fprintf(stderr, "[CV-CAPACITY] tick=%llu frame=%llu records=%u capacity=%u present=%d\n",
                    (unsigned long long)GetTickCount64(), (unsigned long long)(s_d3d.frame_count + 1),
                    s_rb.count, (unsigned int)MAX_DRAWS, s_rb.present);
    }
    if (s_rb.count >= MAX_DRAWS) {
        static int n = 0; if (n++ < 8)
            fprintf(stderr, "[D3D12] WARNING: frame hit MAX_DRAWS (%d) -- later draws were dropped%c", MAX_DRAWS, 10);
    }
    s_dbg_last_draws = s_rb.count;
    /* The recording-side resets (draw count, upload offsets, snapshot arenas,
     * parity) happen when render_frame() freezes the batch, not here: with
     * the render thread the drain is already recording the next frame. */
    s_rb.count = 0;

    /* Debug: RTT_SAVERT=<hex raw offset>[:frame] copies that offscreen RT
     * into a readback buffer this frame and writes rt_save.bmp (half-float
     * RTs are tonemapped |v| -> byte). */
    static ID3D12Resource* s_rtsave_buf = NULL;
    static u32 s_rtsave_state = 0;   /* 1 = copy queued this frame */
    static u32 s_rtsave_w, s_rtsave_h, s_rtsave_pitch, s_rtsave_dxgi;
    { const char* sv = getenv("RTT_SAVERT");
      static int _done = 0;
      static u32 _skip = 0xFFFFFFFFu;
      if (_skip == 0xFFFFFFFFu) { const char* sk = getenv("RTT_SAVERT_SKIP");
          _skip = sk ? (u32)strtoul(sk, NULL, 0) : 0; }   /* frame 60 is far too
          early for a title that takes a minute to reach its menu */
      /* RTT_SAVERT_AFTER=<seconds> triggers on wall time instead of a frame
       * count. This title runs its menu at a few frames a second, so a frame
       * number is not a usable way to say "once it has settled". */
      static u32 _after = 0xFFFFFFFFu;
      if (_after == 0xFFFFFFFFu) { const char* sa = getenv("RTT_SAVERT_AFTER");
          _after = sa ? (u32)strtoul(sa, NULL, 0) : 0; }
      static clock_t _t0 = 0;
      if (!_t0) _t0 = clock();
      const int _due = _after
          ? ((u32)((clock() - _t0) / CLOCKS_PER_SEC) >= _after)
          : (s_d3d.frame_count > 60 + _skip);
      { static int _p1 = 0;
        if (sv && _due && !_p1) { _p1 = 1;
            printf("[RTT_SAVERT] due: want=0x%lX found_rt=%d frame=%u\n",
                   strtoul(sv, NULL, 16), off_rt_find((u32)strtoul(sv, NULL, 16)),
                   (unsigned)s_d3d.frame_count); fflush(stdout); } }
      if (sv && !_done && _due) {
        int rt = off_rt_find((u32)strtoul(sv, NULL, 16));
        fprintf(stderr, "[RTT_SAVERT] rt=%d res=%p\n", rt, rt >= 0 ? (void*)s_d3d.off_rt[rt].res : NULL);
        if (rt >= 0 && s_d3d.off_rt[rt].res) {
            OffRT* r = &s_d3d.off_rt[rt];
            u32 bpp = (r->dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 :
                      (r->dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;
            u32 pitch = (r->w * bpp + 255) & ~255u;
            fprintf(stderr, "[RTT_SAVERT] rt=%d w=%u h=%u bpp=%u pitch=%u dxgi=%d bytes=%llu\n",
                    rt, r->w, r->h, bpp, pitch, (int)r->dxgi,
                    (unsigned long long)((u64)pitch * r->h));
            if (!s_rtsave_buf) {
                D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC rd = {0};
                rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                rd.Width = (u64)pitch * r->h; rd.Height = 1; rd.DepthOrArraySize = 1;
                rd.MipLevels = 1; rd.SampleDesc.Count = 1;
                rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                    &IID_ID3D12Resource, (void**)&s_rtsave_buf);
            }
            if (!s_rtsave_buf) fprintf(stderr, "[RTT_SAVERT] readback buffer alloc FAILED\n");
            if (s_rtsave_buf) {
                off_rt_transition(rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION cdst = {0}, csrc = {0};
                cdst.pResource = s_rtsave_buf;
                cdst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                cdst.PlacedFootprint.Footprint.Format   = (DXGI_FORMAT)r->dxgi;
                cdst.PlacedFootprint.Footprint.Width    = r->w;
                cdst.PlacedFootprint.Footprint.Height   = r->h;
                cdst.PlacedFootprint.Footprint.Depth    = 1;
                cdst.PlacedFootprint.Footprint.RowPitch = pitch;
                csrc.pResource = r->res;
                csrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &cdst, 0, 0, 0, &csrc, NULL);
                off_rt_transition(rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s_rtsave_state = 1;
                s_rtsave_w = r->w; s_rtsave_h = r->h;
                s_rtsave_pitch = pitch; s_rtsave_dxgi = r->dxgi;
                fprintf(stderr, "[RTT_SAVERT] copy queued: rt=%d %ux%u pitch=%u\n",
                        rt, r->w, r->h, pitch);
                _done = 1;
            }
        }
      } }

    /* CELLMARK_DUMP_MINDRAWS=<N>: only consider frames carrying at least N draw
     * records. Most render_frame calls come from the guest flip with a batch the
     * clear boundary already drained, so they present an empty backbuffer -- and
     * sampling those wastes every dump on a black image while the frames that do
     * hold the scene go uncaptured. */
    { static int mind = -1;
      if (mind < 0) { const char* e = getenv("CELLMARK_DUMP_MINDRAWS");
                      mind = e ? atoi(e) : 0; }
      /* draw_count is already reset by here; s_dbg_last_draws holds the
       * count this frame actually rendered. */
      if (mind > 0 && s_dbg_last_draws < (u32)mind) goto skip_dump_consider; }
    if (s_d3d.dump_skip_left > 0 && s_d3d.dump_frames_left > 0) s_d3d.dump_skip_left--;
skip_dump_consider: ;
    guest_fb_present(fi);
    frame_detail_tag(FD_POST);
    composite_present(fi);
    if (s_rb.present) ui_batch_draw(fi);
    if (s_rb.present) aa_apply(fi);   /* anti-aliasing on the finished frame */
    if (s_rb.present && !s_display) fsr_backbuffer(fi);   /* FSR 1 RCAS, after AA */
    if (s_rb.present && !s_display) motion_capture_record(s_d3d.render_targets[fi],rs_dim(s_d3d.width,s_rs),rs_dim(s_d3d.height,s_rs));
    if (s_rb.present && !s_display) shot_record(s_d3d.render_targets[fi], rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs));
    if(s_fsr_ready&&s_rb.present&&!s_display)rsx_fsr31_hudless(s_d3d.cmd_list,s_d3d.render_targets[fi],D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (s_rb.present && !s_display) {
        perf_overlay_draw(fi);
        if(s_fsr_ready)rsx_fsr31_present_color(s_d3d.cmd_list,s_d3d.render_targets[fi],D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    int dumping = (s_d3d.dump_frames_left > 0 && s_d3d.readback_buf
                   && s_d3d.dump_skip_left == 0);
    { static int mind2 = -1;
      if (mind2 < 0) { const char* e = getenv("CELLMARK_DUMP_MINDRAWS");
                       mind2 = e ? atoi(e) : 0; }
      if (mind2 > 0 && s_dbg_last_draws < (u32)mind2) dumping = 0; }
    /* CELLMARK_DUMP_MAXDRAWS=<N>: only frames of at most N records (loads). */
    { static int maxd = -1;
      if (maxd < 0) { const char* e = getenv("CELLMARK_DUMP_MAXDRAWS"); maxd = e ? atoi(e) : 0; }
      if (maxd > 0 && dumping && s_rb.present && s_rtnew_dump > 0) { s_rtnew_dump--;
          fprintf(stderr, "[RTNEW] dumping frame after RT creation (%u records)%c", s_dbg_last_draws, 10); }
      else if (maxd > 0 && (s_dbg_last_draws > (u32)maxd || !s_rb.present || !s_gameplay_seen)) dumping = 0; }
    /* CELLMARK_DUMP_EVERY=N: after each dump, skip N-1 frames -- samples the whole
     * run instead of one contiguous window, so a short burst of real content
     * can't fall between the dumped frames. */
    if (dumping) {
        static int s_every = -1;
        if (s_every < 0) { const char* e = getenv("CELLMARK_DUMP_EVERY");
                           s_every = e ? atoi(e) : 0; }
        if (s_every > 1) s_d3d.dump_skip_left = s_every - 1;
    }
    if (dumping) {
        /* RT -> COPY_SOURCE, copy into the readback buffer, then -> PRESENT. */
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        dst.pResource = s_d3d.readback_buf;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width    = rs_dim(s_d3d.width, s_rs);
        dst.PlacedFootprint.Footprint.Height   = rs_dim(s_d3d.height, s_rs);
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = s_d3d.readback_pitch;
        src.pResource = s_d3d.render_targets[fi];
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);
    } else {
        /* End-of-frame fallback snapshot: used when the frame never contained a
         * reduced-viewport pass to capture mid-frame. */
        screen_copy_capture(fi);
        /* Transition render target to PRESENT state */
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);
    }
    if(s_fsr_ready&&s_fsr_depth_at==s_rb.count)
        rsx_fsr31_depth(s_d3d.cmd_list,s_d3d.depth_buffer,s_fsr_depth_w,s_fsr_depth_h);
    if (s_rb.present && s_display) display_present(fi);
    if(s_rb.present){
        if(g_rsx_cap_request&&s_fsr_ready)rsx_fsr31_capture_next();
        int fg_menu=0;
        if(s_fsr_ready){
            AcquireSRWLockShared(&s_ui_lock);
            int ui_slot=rsx_ui_select_batch(s_ui_render_flip,s_ui_frame,s_ui_valid,UI_SLOTS,ui_reuse_age());
            fg_menu=ui_slot>=0&&s_ui_count[ui_slot]!=0;
            ReleaseSRWLockShared(&s_ui_lock);
        }
        rsx_fsr31_frame(s_d3d.cmd_list,s_d3d.swap_chain,
            s_fsr_ready&&s_fsr_depth_at!=0xFFFFFFFFu&&!s_cap_steps_on&&!fg_menu);
    }

    /* A replay draws the same batch again: its targets went back to guest
     * memory after the first draw; never write them over later guest data. */
    if (!s_replay_pass) off_rt_writeback_record();

    if(pose_compute_mode()&&s_pose_compute.ready&&s_cap_steps_on)
        s_pose_compute_capture_ready=pc_record_capture(&s_pose_compute,s_d3d.device,s_d3d.cmd_list,s_rb.pose_nv);
    ts_end(fi);
    frame_detail_end_record();
    frame_profile_mark(FP_BUILD);
    rsx_frame_gpu_end_list(&s_frame_gpu,s_d3d.cmd_list);
    rsx_frame_gpu_end_frame(&s_frame_gpu,s_d3d.cmd_list,s_d3d.fence_values[fi],s_cap_steps_on);
    /* Close and execute */
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cmd_lists[] = {(ID3D12CommandList*)s_d3d.cmd_list};
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cmd_lists);
    mc_submit(s_d3d.cmd_queue);
    shot_finish();
    frame_profile_mark(FP_SUBMIT);

    /* D3D12_IQ=1: drain the debug layer's message queue after submitting the
     * frame. The legacy (is_vp=0) DrawInstanced path silently produced ZERO
     * pixels in flOw's render injection while ClearRenderTargetView worked, and
     * every input (verts, offsets, layout, MVP, PSO, root sig, depth, cull) was
     * verified correct -- so the only remaining explanation is a validation
     * error the debug layer is swallowing. Off unless the env var is set. */
    { static int _iq = -1;
      if (_iq < 0) { const char* e = getenv("D3D12_IQ"); _iq = e ? 1 : 0; }
      if (_iq && s_d3d.device) {
        ID3D12InfoQueue* iq = NULL;
        if (SUCCEEDED(s_d3d.device->lpVtbl->QueryInterface(
                s_d3d.device, &IID_ID3D12InfoQueue, (void**)&iq)) && iq) {
            UINT64 n = iq->lpVtbl->GetNumStoredMessages(iq);
            static int _printed = 0;
            for (UINT64 mi = 0; mi < n && _printed < 80; mi++) {
                SIZE_T len = 0;
                iq->lpVtbl->GetMessage(iq, mi, NULL, &len);
                D3D12_MESSAGE* m = (D3D12_MESSAGE*)malloc(len);
                if (m && SUCCEEDED(iq->lpVtbl->GetMessage(iq, mi, m, &len))) {
                    fprintf(stderr, "[D3D12-IQ][sev=%d id=%d] %s\n",
                            (int)m->Severity, (int)m->ID, m->pDescription);
                    _printed++;
                }
                free(m);
            }
            if (n) iq->lpVtbl->ClearStoredMessages(iq);
            iq->lpVtbl->Release(iq);
            fflush(stderr);
        }
      } }

    if (s_sc_dump_pending) {
        s_sc_dump_pending = 0;
        wait_for_gpu();
        s_dump_name = "screencopy";
        dump_backbuffer_bmp();
        s_dump_name = NULL;
    }
    if (dumping) {
        wait_for_gpu();            /* ensure the copy finished before mapping */
        dump_backbuffer_bmp();
        s_d3d.dump_frames_left--;
    }

    if (s_rtsave_state) {
        fprintf(stderr, "[RTT_SAVERT] writing rt_save.bmp %ux%u\n", s_rtsave_w, s_rtsave_h);
        if (!dumping) { double _g = perf_on() ? perf_now() : 0.0;
                        wait_for_gpu();
                        if (perf_on()) s_perf_gpu += perf_now() - _g; }
        void* mp = NULL; D3D12_RANGE rr = {0, (SIZE_T)s_rtsave_pitch * s_rtsave_h};
        if (SUCCEEDED(s_rtsave_buf->lpVtbl->Map(s_rtsave_buf, 0, &rr, &mp)) && mp) {
            FILE* f = fopen("rt_save.bmp", "wb");
            if (f) {
                u32 w = s_rtsave_w, h = s_rtsave_h;
                u32 rowb = (w * 3 + 3) & ~3u;
                u32 datasz = rowb * h;
                u8 hdr[54] = {0};
                hdr[0]='B'; hdr[1]='M';
                *(u32*)(hdr+2) = 54 + datasz; *(u32*)(hdr+10) = 54;
                *(u32*)(hdr+14) = 40; *(int*)(hdr+18) = (int)w; *(int*)(hdr+22) = (int)h;
                *(u16*)(hdr+26) = 1; *(u16*)(hdr+28) = 24; *(u32*)(hdr+34) = datasz;
                fwrite(hdr, 1, 54, f);
                u8* line = (u8*)malloc(rowb);
                for (int y = (int)h - 1; y >= 0; y--) {
                    const u8* srow = (const u8*)mp + (u64)y * s_rtsave_pitch;
                    memset(line, 0, rowb);
                    for (u32 x = 0; x < w; x++) {
                        float rv, gv, bv;
                        if (s_rtsave_dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                            const u16* hp16 = (const u16*)(srow + (u64)x * 8);
                            /* crude half->float: sign|exp|mant */
                            float v[3];
                            for (int c2 = 0; c2 < 3; c2++) {
                                u16 hv = hp16[c2];
                                u32 sign = (hv >> 15) & 1, exp = (hv >> 10) & 0x1F, man = hv & 0x3FF;
                                float fv;
                                if (exp == 0) fv = (float)man / 16777216.0f;
                                else { u32 fb = (sign << 31) | ((exp - 15 + 127) << 23) | (man << 13);
                                       memcpy(&fv, &fb, 4); }
                                v[c2] = fv;
                            }
                            rv = v[0]; gv = v[1]; bv = v[2];
                        } else if (s_rtsave_dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                            const float* fp32 = (const float*)(srow + (u64)x * 16);
                            rv = fp32[0]; gv = fp32[1]; bv = fp32[2];
                        } else {
                            const u8* p8 = srow + (u64)x * 4;
                            rv = p8[0] / 255.0f; gv = p8[1] / 255.0f; bv = p8[2] / 255.0f;
                        }
                        /* RTT_SAVEA=1: show the alpha lane in RED (gates
                         * like wave's mask.w live there). */
                        if (getenv("RTT_SAVEA")) {
                            float av;
                            if (s_rtsave_dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                                const u16* hp16 = (const u16*)(srow + (u64)x * 8);
                                u16 hv = hp16[3];
                                u32 sign = (hv >> 15) & 1, exp = (hv >> 10) & 0x1F, man = hv & 0x3FF;
                                if (exp == 0) av = (float)man / 16777216.0f;
                                else { u32 fb = (sign << 31) | ((exp - 15 + 127) << 23) | (man << 13);
                                       memcpy(&av, &fb, 4); }
                            } else if (s_rtsave_dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                                av = ((const float*)(srow + (u64)x * 16))[3];
                            } else {
                                av = (srow + (u64)x * 4)[3] / 255.0f;
                            }
                            rv = av;
                        }
                        /* |v| tonemap so signed heights are visible */
                        float ar = rv < 0 ? -rv : rv, ag = gv < 0 ? -gv : gv, ab = bv < 0 ? -bv : bv;
                        if (ar > 1) ar = 1; if (ag > 1) ag = 1; if (ab > 1) ab = 1;
                        line[x*3+0] = (u8)(ab * 255.0f);
                        line[x*3+1] = (u8)(ag * 255.0f);
                        line[x*3+2] = (u8)(ar * 255.0f);
                    }
                    fwrite(line, 1, rowb, f);
                }
                free(line);
                fclose(f);
                printf("[D3D12] wrote rt_save.bmp (%ux%u dxgi=%u)\n", w, h, s_rtsave_dxgi);
            }
            s_rtsave_buf->lpVtbl->Unmap(s_rtsave_buf, 0, NULL);
        }
        s_rtsave_state = 0;
    }

    /* rsxcap: snapshot whole frames (backbuffer + every offscreen RT + a draw
     * manifest) at RSX_CAP=start[:count[:stride]] frame_counts. Runs before the
     * present transition would matter -- the backbuffer is in PRESENT here and
     * is restored to PRESENT after each readback. */
    /* F12 in the game window: capture the next frame that drew anything, to
     * rsxcap/frame_9N (RSX_CAP_DIR overrides the base). Lets a capture land
     * on a scene reached by hand, where a frame number cannot be predicted. */
    { static u32 s_key_caps = 0;
      /* CV_AUTOCAP=<draws>: also capture once, by itself, after 30
       * consecutive frames of at least that many draws (a 3D scene). */
      { static int thr = -1; static u32 run = 0; static int fired = 0;
        if (thr < 0) { const char* e = getenv("CV_AUTOCAP"); thr = e ? atoi(e) : 0; }
        if (thr > 0 && !fired) {
            run = (s_dbg_last_draws >= (u32)thr) ? run + 1 : 0;
            if (run >= 5 && s_cap_nsteps > 0) { fired = 1; g_rsx_cap_request = 1; }
            s_autocap_arm = !fired && (run >= 4);
        } }
      if (g_rsx_cap_request && s_dbg_last_draws > 0 && s_key_caps < 10 && (!pose_compute_mode()||s_pose_compute_capture_ready)) {
          g_rsx_cap_request = 0;
          rsx_capture_frame(fi, s_dbg_last_draws, 90 + s_key_caps++);
          fprintf(stderr, "[RSXCAP] F12 capture %u written (%u draws)%c",
                  s_key_caps, s_dbg_last_draws, 10);
      }
      /* FSR diagnostics can continue after the legacy real-frame cap limit.
       * Do not leave the request latched, which would suspend FG forever. */
      if(g_rsx_cap_request&&s_key_caps>=10&&rsx_fsr31_requested())g_rsx_cap_request=0;
    }
    { const char* cap = getenv("RSX_CAP");
      if (cap) {
        static int s_cap_parsed = 0;
        static u32 s_cap_start = 0, s_cap_count = 1, s_cap_stride = 1, s_cap_done = 0;
        if (!s_cap_parsed) {
            s_cap_parsed = 1;
            u32 a = 0, b = 1, c = 1;
            int n = sscanf(cap, "%u:%u:%u", &a, &b, &c);
            s_cap_start  = (n >= 1 && a) ? a : 200;
            s_cap_count  = (n >= 2 && b) ? b : 1;
            s_cap_stride = (n >= 3 && c) ? c : 1;
        }
        if (s_cap_done < s_cap_count) {
            u32 target = s_cap_start + s_cap_done * s_cap_stride;
            s_rsxcap_next = target;   /* arms the step photos for that frame */
            if (s_d3d.frame_count >= target && s_dbg_last_draws > 0) {
                rsx_capture_frame(fi, s_dbg_last_draws, s_cap_done);
                s_cap_done++;
                s_rsxcap_next = (s_cap_done < s_cap_count)
                              ? s_cap_start + s_cap_done * s_cap_stride : 0;
            }
        }
      } }

    if (cv_diag) cv_present_start = perf_now();
    frame_profile_mark(FP_READBACK);
    /* Present */
    if (s_rb.present)
        { extern void ps3_ft(char); ps3_ft('R');
          HRESULT _ph = s_vsync
              ? s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, 1, 0)
              : s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, 0,
                                                 s_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
          if (_ph == DXGI_ERROR_DEVICE_REMOVED || _ph == DXGI_ERROR_DEVICE_RESET) dred_report("Present");
          perf_overlay_frame(); present_pace_note(); }   /* skipped for an offscreen-only batch */

    if (cv_diag) cv_present_ms = (perf_now() - cv_present_start) * 1000.0;
    frame_profile_mark(FP_PRESENT);
    if (s_rs_pending != 0.0f) apply_res_scale();
    if (cv_diag) cv_fence_start = perf_now();
    move_to_next_frame();
    frame_profile_mark(FP_FENCE);
    if (cv_diag) {
        const double now = perf_now();
        static unsigned int reports = 0;
        if ((now - cv_start) >= 0.020 && reports++ < 1200)
            fprintf(stderr, "[CV-FRAME] tick=%llu frame=%llu total=%.3f entry-gpu=%.3f record-submit=%.3f present-overlay=%.3f end-fence=%.3f draws=%u\n",
                    (unsigned long long)GetTickCount64(), (unsigned long long)(s_d3d.frame_count + 1),
                    (now-cv_start)*1000.0, cv_wait,
                    (cv_present_start-cv_start)*1000.0-cv_wait, cv_present_ms,
                    (now-cv_fence_start)*1000.0, s_dbg_last_draws);
    }

    s_d3d.frame_count++;
    { static int on = -1; static double last = 0.0;
      if (on < 0) on = getenv("HITCHDBG") ? 1 : 0;
      if (on) {
          const double now = perf_now(), dt = last > 0.0 ? now - last : 0.0; last = now;
          const LONG vs = InterlockedExchange(&s_hd_vs_us, 0), vn = InterlockedExchange(&s_hd_vs_n, 0);
          const LONG fi = InterlockedExchange(&s_hd_fpi_us, 0), fn = InterlockedExchange(&s_hd_fpi_n, 0);
          const LONG wt = InterlockedExchange(&s_hd_wait_us, 0);
          { static double mx = 0.0; static int k = 0; if (dt > mx) mx = dt;
            if (++k == 300) { fprintf(stderr, "[HITCH] longest frame of the last 300: %.1f ms%c", mx * 1000.0, 10); k = 0; mx = 0.0; } }
          if (dt > 0.045)
              fprintf(stderr, "[HITCH] frame %llu took %.1f ms: VS compiles %ld (%.1f ms), inline FP builds %ld (%.1f ms),"
                     " waits %.1f ms, background in flight %ld%c",
                     (unsigned long long)s_d3d.frame_count, dt * 1000.0, (long)vn, vs / 1000.0,
                     (long)fn, fi / 1000.0, wt / 1000.0, (long)s_fp_inflight, 10);
      } }

    /* RPCS3-style titlebar stats, refreshed once a second: presented FPS,
     * draw count of the last frame, and the render (backbuffer) size. */
    {
        static ULONGLONG s_tt0 = 0;
        static u64 s_tframes = 0;
        s_tframes++;
        ULONGLONG tnow = GetTickCount64();
        if (s_tt0 == 0) s_tt0 = tnow;
        if (tnow - s_tt0 >= 1000 && s_d3d.hwnd) {
            extern char g_rsx_title_base[128];
            char tb[256];
            snprintf(tb, sizeof(tb), "%s | FPS: %.2f | draws: %u | %ux%u",
                     g_rsx_title_base,
                     s_tframes * 1000.0 / (double)(tnow - s_tt0),
                     s_dbg_last_draws, rs_dim(s_d3d.width, s_rs), rs_dim(s_d3d.height, s_rs));
            if (s_lighter_probe) {
                size_t used = strlen(tb);
                snprintf(tb + used, sizeof tb - used, " | F10 SOMBRA: %s",
                         InterlockedCompareExchange(&s_lighter_shadow_bypass, 0, 0) ? "SEM SOMBRA" : "NORMAL");
            }
            /* Posted, not SetWindowText: this runs on the render thread, and
             * SetWindowText on another thread's window waits for that thread
             * to pump. The window thread applies it in pump_messages. */
            AcquireSRWLockExclusive(&s_title_lock);
            memcpy(s_title_pending, tb, sizeof tb);
            s_title_dirty = 1;
            ReleaseSRWLockExclusive(&s_title_lock);
            s_tframes = 0;
            s_tt0 = tnow;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Render thread (see s_rb). render_frame() is what every present path calls,
 * on the thread that drains the FIFO.
 * -------------------------------------------------------------------------*/
static u8*    s_fp_arena_spare = NULL;
static u8*    s_vtex_arena_spare = NULL;
static u32    s_vtex_arena_spare_cap = 0;
static HANDLE s_rt_work = NULL, s_rt_idle = NULL, s_rt_thread = NULL;
static int    s_rt_async = -1;

/* Wait for the render thread to finish its batch -- CPU work AND the GPU
 * work it submitted, so both upload-stream halves are free again. While
 * waiting, keep servicing messages SENT to this thread's windows: the
 * render thread's SetWindowText (title FPS) and DXGI may send to the window
 * this thread owns, and blocking here without dispatching them deadlocks. */
static void render_wait_cpu_idle(void)
{
    if (!s_rt_idle) return;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &s_rt_idle, FALSE, INFINITE, QS_SENDMESSAGE);
        if (r == WAIT_OBJECT_0) return;
        MSG m; PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
}
static volatile LONG64 s_rt_fence_pending;
static void rt_fence_wait(u64 v);
static void render_wait_idle(void)
{
    render_wait_cpu_idle();
    if (s_rt_idle) rt_fence_wait((u64)s_rt_fence_pending);   /* the GPU part, see pose_overlap_on */
}

void ps3_tl_begin(int ev);   /* PS3_TIMELINE, cellGcmSys.c */
void ps3_tl_end(int ev);

/* 60 FPS CPU interpolation: the next batch's pose reads only its own parity
 * (CPU snapshots, vp_vb/vp_cb/vf halves), so it may run while the GPU still
 * draws this batch. The render thread then reports idle right after submit;
 * the GPU wait that frees this batch's halves moves to the producer, before it
 * records into them (render_frame), and to every other render_wait_idle. */
static ID3D12Fence* s_rt_fence;
static HANDLE s_rt_fence_event;
static u64 s_rt_fence_value;
/* s_rt_fence_pending (above render_wait_idle): last batch's fence; 0 = none.
 * s_rt_set_fence: the last batch drawn from each upload set (vp_sets). */
static volatile LONG64 s_rt_set_fence[3];
static int pose_overlap_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("CV_POSE_OVERLAP");
        on = s_pose_on && !pose_compute_mode() && !(e && !strcmp(e, "0"));
        if (on && (FAILED(s_d3d.device->lpVtbl->CreateFence(s_d3d.device, 0, D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void**)&s_rt_fence)) ||
                !(s_rt_fence_event = CreateEventA(NULL, FALSE, FALSE, NULL)))) on = 0;
        fprintf(stderr, "[POSE-OVERLAP] v1 %s; next pose computed while the GPU draws the current batch%c",
                on ? "on" : "off", 10);
    }
    return on;
}
/* Render thread, after the batch's last submit. 0: wait here as before. */
static int rt_fence_signal(void)
{
    if (!pose_overlap_on()) return 0;
    const u64 v = s_rt_fence_value + 1;
    if (FAILED(s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_rt_fence, v))) return 0;
    s_rt_fence_value = v;
    if ((u32)s_rb.parity < 3) InterlockedExchange64(&s_rt_set_fence[s_rb.parity], (LONG64)v);
    InterlockedExchange64(&s_rt_fence_pending, (LONG64)v);
    return 1;
}
/* Any thread but the render thread: wait until the GPU finished batch v. */
static void rt_fence_wait(u64 v)
{
    if (!v || !s_rt_fence || s_rt_fence->lpVtbl->GetCompletedValue(s_rt_fence) >= v) return;
    s_rt_fence->lpVtbl->SetEventOnCompletion(s_rt_fence, v, s_rt_fence_event);
    for (int tries = 0; tries < 3; tries++) {   /* bounded, as wait_for_gpu */
        if (WaitForSingleObject(s_rt_fence_event, 2000) != WAIT_TIMEOUT) return;
        HRESULT rr = s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device);
        printf("[D3D12] batch fence STUCK %ds: want %llu got %llu removed=0x%08lX\n", 2 * (tries + 1),
               (unsigned long long)v, (unsigned long long)s_rt_fence->lpVtbl->GetCompletedValue(s_rt_fence), (long)rr);
        fflush(stdout);
        if (rr != 0) { dred_report("rt_fence_wait"); break; }
    }
}

/* CV_POSE_REPLAY (60 FPS, CPU interpolation, 3 upload sets): the guest keeps
 * its native 30 Hz loop, one update and ONE draw per tick, instead of drawing
 * every tick twice. Each batch is presented twice: on arrival with the
 * interpolated pose (midpoint), then about half a guest frame later again
 * with its recorded positions. CV_POSE_REPLAY=0 restores the guest's own
 * second draw. FSR frame generation makes its own frames: no replay. */
int rsx_d3d12_pose_replay_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *r = getenv("CV_POSE_REPLAY"), *fg = getenv("CV_FSR_FRAME_GENERATION");
        on = vp_sets() == 3 && !(r && !strcmp(r, "0")) && !(fg && !strcmp(fg, "1"));
        fprintf(stderr, "[POSE-REPLAY] v1 %s; native 30 Hz guest draws, each batch presented interpolated then recorded%c",
                on ? "on" : "off", 10);
    }
    return on;
}
static double s_replay_cost = 0.008;
/* Present the same batch again half a native 30 Hz tick after its first
 * present. A fixed target, never the measured guest interval: the producer
 * waits for this thread, so a wait that followed a slower guest would slow
 * the guest further. Late (heavy frames): replay at once, never skip it. */
static void render_replay_batch(u32 count, double first_present)
{
    const double start = first_present + 0.5 / 30.0 - s_replay_cost;
    for (double now = pose_time(); now < start; now = pose_time()) {
        const double left = start - now;
        if (left > 0.002) Sleep((DWORD)((left - 0.0015) * 1000.0)); else YieldProcessor();
    }
    const double t0 = pose_time();
    s_rb.count = count;
    s_replay_pass = 1;
    render_frame_impl();
    s_replay_pass = 0;
    const double cost = pose_time() - t0;
    if (cost > 0 && cost < 0.05) s_replay_cost = s_replay_cost * 0.8 + cost * 0.2;
}

/* Replay pose on its own thread (CV_POSE_THREAD=0: the producer computes it,
 * as in v4). Same function, same inputs, same order, one batch at a time;
 * the job starts at once, so its clock reading moves by microseconds: the
 * result does not change. The producer no longer waits for it and goes straight on to
 * record the next batch; the render thread waits for the batch's job before
 * drawing it. The batch keeps its draw list, FP arena and upload set until the
 * producer's next handover, which already waits for the render thread. */
static SRWLOCK s_pt_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_pt_cv = CONDITION_VARIABLE_INIT;
static RenderBatch s_pt_batch;
static u64 s_pt_submitted, s_pt_done;
static int pose_thread_on(void);
static DWORD WINAPI pose_thread_main(LPVOID p)
{
    (void)p;
    for (;;) {
        AcquireSRWLockExclusive(&s_pt_lock);
        while (s_pt_done == s_pt_submitted) SleepConditionVariableSRW(&s_pt_cv, &s_pt_lock, INFINITE, 0);
        ReleaseSRWLockExclusive(&s_pt_lock);
        pose_interpolate_batch_for(&s_pt_batch);
        AcquireSRWLockExclusive(&s_pt_lock);
        s_pt_done++;
        WakeAllConditionVariable(&s_pt_cv);
        ReleaseSRWLockExclusive(&s_pt_lock);
    }
    return 0;
}
static int pose_thread_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("CV_POSE_THREAD");
        on = !(e && !strcmp(e, "0"));
        if (on) {
            HANDLE t = CreateThread(NULL, 4u << 20, pose_thread_main, NULL, 0, NULL);
            if (!t) on = 0;
            else {
                ps3_host_thread_name(t, "rsx_pose");
                const char* pe = getenv("RSX_RENDER_PRIO");   /* as rsx_render */
                if (!pe || *pe != '0') SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL);
            }
        }
        fprintf(stderr, "[POSE-THREAD] v1 %s; replay pose computed beside the recording of the next batch%c",
                on ? "on" : "off (CV_POSE_THREAD=0)", 10);
    }
    return on;
}
/* Producer: after the previous job, start this batch's pose. Returns its job. */
static u64 pose_thread_submit(const RenderBatch* b)
{
    AcquireSRWLockExclusive(&s_pt_lock);
    while (s_pt_done != s_pt_submitted) SleepConditionVariableSRW(&s_pt_cv, &s_pt_lock, INFINITE, 0);
    s_pt_batch = *b;
    const u64 seq = ++s_pt_submitted;
    WakeAllConditionVariable(&s_pt_cv);
    ReleaseSRWLockExclusive(&s_pt_lock);
    return seq;
}
/* Render thread: wait until job seq has written its pose. */
static void pose_thread_wait(u64 seq)
{
    static double waited, reported; static u32 jobs;
    const double t0 = pose_time();
    AcquireSRWLockExclusive(&s_pt_lock);
    while (s_pt_done < seq) SleepConditionVariableSRW(&s_pt_cv, &s_pt_lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&s_pt_lock);
    if (!s_pose_profile) return;
    const double t1 = pose_time();
    waited += t1 - t0; jobs++;
    if (!reported) reported = t1;
    if (t1 - reported >= 5) {
        fprintf(stderr, "[POSE-THREAD] %u batches render_wait_ms=%.3f (render thread waiting for the pose)%c",
                jobs, waited * 1000.0 / jobs, 10);
        waited = 0; jobs = 0; reported = t1;
    }
}

static DWORD WINAPI render_thread_main(LPVOID p)
{
    (void)p;
    for (;;) {
        WaitForSingleObject(s_rt_work, INFINITE);
        if (s_rb.pose_seq) pose_thread_wait(s_rb.pose_seq);
        const u32 count = s_rb.count;
        const int replay = s_rb.present && rsx_d3d12_pose_replay_enabled() && pose_overlap_on();
        ps3_tl_begin(4);
        render_frame_impl();
        if (replay) {
            frame_profile_mark(FP_BOOKKEEP);
            frame_profile_finish();
            render_replay_batch(count, pose_time());
        }
        ps3_tl_end(4);
        frame_profile_mark(FP_BOOKKEEP);
        ps3_tl_begin(5);
        if (!rt_fence_signal())
            wait_for_gpu();      /* this batch's upload halves are reusable */
        ps3_tl_end(5);
        frame_profile_finish();
        SetEvent(s_rt_idle);
    }
    return 0;
}

/* Bumped whenever the recording draw list starts over (render_frame). */
static unsigned s_draw_gen;

static void render_frame(void)
{
    static double profile_released;
    static u64 producer_released_cpu;static DWORD producer_released_tid;
    int profile=frame_profile_on();double profile_ready=profile?pose_time():0,profile_wait=0;
    int producer_detail=profile&&producer_profile_on();
    DWORD producer_tid=producer_detail?GetCurrentThreadId():0;
    u64 producer_ready_cpu=producer_detail?frame_cpu_ticks(0):0;
    if (s_rt_async < 0) {
        const char* e = getenv("RSX_RENDER_THREAD");
        s_rt_async = (e && *e == '0') ? 0 : 1;
        if (s_rt_async) {
            s_rt_work = CreateEventA(NULL, FALSE, FALSE, NULL);
            s_rt_idle = CreateEventA(NULL, TRUE, TRUE, NULL);
            s_rt_thread = CreateThread(NULL, 4u << 20, render_thread_main, NULL, 0, NULL);
            if (!s_rt_work || !s_rt_idle || !s_rt_thread) s_rt_async = 0;
            else {
                ps3_host_thread_name(s_rt_thread, "rsx_render");
                /* Above the guest's own threads: a render frame preempted by
                 * busy guest pollers stalled 20-50 ms at random draws (Code
                 * Veronica's lighter scene). RSX_RENDER_PRIO=0 leaves it normal. */
                const char* pe = getenv("RSX_RENDER_PRIO");
                if (!pe || *pe != '0') SetThreadPriority(s_rt_thread, THREAD_PRIORITY_ABOVE_NORMAL);
            }
            fprintf(stderr, "[D3D12] render thread %s%c", s_rt_async ? "ON" : "OFF (RSX_RENDER_THREAD=0)", 10);
        }
    }
    /* Replay: interpolate the finished recording batch here, while the render
     * thread may still draw the previous batch twice. The pose reads and
     * writes only this batch's own upload set, which no GPU work uses yet. */
    int pose_done = 0;
    u64 pose_seq = 0;
    u32 pose_scene = 0;
    /* F9 copies the drawn batch's pose history on the render thread. While
     * it records, let that batch finish before this pose replaces the
     * history, so the capture's old/current positions belong to it. */
    if (s_rt_async && (mc.request || mc.remaining)) render_wait_cpu_idle();
    if (s_rt_async && s_present_this_frame && s_pose_on && rsx_d3d12_pose_replay_enabled()) {
        vf_flush();
        RenderBatch next;
        memset(&next, 0, sizeof next);
        next.draws = s_d3d.draws;
        next.count = s_d3d.draw_count;
        next.parity = s_d3d.vp_parity;
        next.fp_arena = s_fp_arena;
        next.present = 1;
        next.pose_stamp = s_pose_stamp;
        next.pose_nv = s_d3d.vp_vb_offset / 256;
        next.pose_scene = pose_scene = (u32)InterlockedCompareExchange(&s_pose_scene, 0, 0);
        if (pose_thread_on()) pose_seq = pose_thread_submit(&next);
        else pose_interpolate_batch_for(&next);
        pose_done = 1;
    }
    /* Previous batch done on the CPU. With pose overlap the GPU may still
     * read the set recording moves to: wait for it after handing over. */
    u64 gpu_pending = 0;
    if (s_rt_async) {
        ps3_tl_begin(3); render_wait_cpu_idle(); ps3_tl_end(3);
    }
    if(profile)profile_wait=pose_time()-profile_ready;

    /* Freeze the recording batch for the renderer... */
    s_rb.draws      = s_d3d.draws;
    s_rb.count      = s_d3d.draw_count;   /* render_frame_impl clamps to MAX_DRAWS */
    s_rb.parity     = s_d3d.vp_parity;
    s_rb.fp_arena   = s_fp_arena;
    s_rb.vtex_arena = s_vtex_arena;
    s_rb.present    = s_present_this_frame;
    s_rb.pose_stamp=s_pose_stamp;s_rb.pose_nv=s_d3d.vp_vb_offset/256;
    s_rb.pose_scene=pose_done?pose_scene:(u32)InterlockedCompareExchange(&s_pose_scene,0,0);
    s_rb.pose_done=pose_done;s_rb.pose_seq=pose_seq;
    s_rb.profile_producer=profile&&profile_released?profile_ready-profile_released:0;
    s_rb.profile_backpressure=profile_wait;
    /* GetThreadTimes is cumulative for one host thread. Never subtract across
     * a first sample, counter failure, or an issuing-thread change. */
    s_rb.profile_producer_valid=producer_detail&&profile_released>0&&producer_tid==producer_released_tid&&producer_released_cpu&&producer_ready_cpu>=producer_released_cpu;
    s_rb.profile_producer_cpu=s_rb.profile_producer_valid?(double)(producer_ready_cpu-producer_released_cpu)*1e-7:0;
    if(++s_pose_stamp==0)s_pose_stamp=1;
    { extern unsigned cellGcm_flip_done_count(void);
      s_ui_render_flip = cellGcm_flip_done_count(); }   /* the flip that ended it */

    /* ...and point recording at the other buffers. */
    s_d3d.draws = (s_d3d.draws == s_draw_bufs[0]) ? s_draw_bufs[1] : s_draw_bufs[0];
    s_d3d.draw_count = 0;
    s_draw_gen++;   /* rsx_d3d12_backend_present's incremental display scan */
    s_d3d.vb_offset = 0;
    vf_flush(); vf_frame_reset();
    s_d3d.vp_vb_offset = 0;
    s_d3d.vp_parity = (s_d3d.vp_parity + 1) % vp_sets();
    /* The batch that last drew from this set (two sets: the one just
     * finished on the CPU; three: the one before it). */
    if (s_rt_async) gpu_pending = (u64)s_rt_set_fence[s_d3d.vp_parity];
    s_vcb_batch++;   /* constant banks of the old batch are not reusable */
    { u8* t = s_fp_arena; s_fp_arena = s_fp_arena_spare; s_fp_arena_spare = t; }
    s_fp_arena_used = 0; s_fp_nsnap = 0;
    { u8* t = s_vtex_arena; u32 c = s_vtex_arena_cap;
      s_vtex_arena = s_vtex_arena_spare; s_vtex_arena_cap = s_vtex_arena_spare_cap;
      s_vtex_arena_spare = t; s_vtex_arena_spare_cap = c; }
    s_vtex_arena_used = 0; s_vtex_nsnap = 0;

    if (s_rt_async) {
        ResetEvent(s_rt_idle);
        SetEvent(s_rt_work);
        ps3_tl_begin(3); rt_fence_wait(gpu_pending); ps3_tl_end(3);
    } else {
        render_frame_impl();
        frame_profile_mark(FP_BOOKKEEP);
        frame_profile_finish();
    }
    if(profile)profile_released=pose_time();
    if(producer_detail){producer_released_cpu=frame_cpu_ticks(0);producer_released_tid=producer_tid;}
    else {producer_released_cpu=0;producer_released_tid=0;}
}

/* ---------------------------------------------------------------------------
 * RSX backend callbacks
 * -----------------------------------------------------------------------*/

static int d3d12_init(void* ud, u32 width, u32 height)
{
    (void)ud;
    printf("[D3D12] Backend init(%ux%u)\n", width, height);
    return 0;
}

static void d3d12_shutdown(void* ud)
{
    (void)ud;
    printf("[D3D12] Backend shutdown\n");
}

static void d3d12_begin_frame(void* ud)
{
    (void)ud;
}

static void d3d12_end_frame(void* ud)
{
    (void)ud;
}

static u32 s_dbg_clears_since_present = 0;   /* CELLMARK_BLINKDBG */
static u32 s_clear_presents = 0;   /* presents issued at clear (frame boundary) */

/* FRAME_BUDGET=1: how much geometry a guest frame actually asks for, versus
 * what the per-frame vertex buffer can hold. A frame that overflows is silently
 * truncated -- the tail of the scene never renders -- which reads as "the object
 * is missing" rather than "the batch is too small". */

static int blink_dbg(void)
{
    static int v = -1;
    if (v < 0) v = getenv("CELLMARK_BLINKDBG") ? 1 : 0;
    return v;
}

static void d3d12_present(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    if (blink_dbg())
        printf("[PRESENT] draws=%u clears_since_last=%u\n",
               s_d3d.draw_count, s_dbg_clears_since_present);
    s_dbg_clears_since_present = 0;

    /* An accumulated batch with draws but NONE targeting a display buffer is
     * offscreen pass work only (demosaic flips once per effect pass): showing
     * it would strobe the bare backbuffer clear. Keep accumulating -- the
     * composite draw that targets the display presents the whole chain, in
     * order, in one command list. Empty batches still present (boot/idle). */
    int flip_has_display = (s_d3d.draw_count == 0);
    for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++)
        if (!s_d3d.draws[_i].is_clear && s_d3d.draws[_i].rt_off == 0) {
            flip_has_display = 1;
            break;
        }

    if (s_d3d.initialized && flip_has_display)
        render_frame();

    /* FPS tracking */
    ULONGLONG now = GetTickCount64();
    if (now - s_d3d.last_fps_time >= 1000) {
        s_d3d.fps = (u32)s_d3d.frame_count; /* rough estimate */
        s_d3d.last_fps_time = now;
        s_d3d.frame_count = 0;
    }
}

static void d3d12_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;

    /* Convert RSX ARGB u32 to float[4] RGBA */
    float cc[4];
    cc[0] = ((color >> 16) & 0xFF) / 255.0f; /* R */
    cc[1] = ((color >> 8) & 0xFF) / 255.0f;  /* G */
    cc[2] = (color & 0xFF) / 255.0f;          /* B */
    cc[3] = ((color >> 24) & 0xFF) / 255.0f;  /* A */
    /* CLEAR_RGB=r,g,b: override the guest clear colour. Black holes (geometry
     * that never rasterized) and black pixels a shader really wrote look
     * identical against a black clear; this separates them in one run. */
    { static int _cinit = 0; static float _co[3]; static int _con = 0;
      if (!_cinit) { _cinit = 1; const char* e = getenv("CLEAR_RGB");
          if (e && sscanf(e, "%f,%f,%f", &_co[0], &_co[1], &_co[2]) == 3) _con = 1; }
      if (_con) { cc[0] = _co[0]; cc[1] = _co[1]; cc[2] = _co[2]; } }

    u32 rt_w = 0, rt_h = 0, mrt[3] = {0, 0, 0};
    u32 rt = current_rt_off(&rt_w, &rt_h, mrt);

    /* An OFFSCREEN clear is just an ordered op in the current frame's pass
     * chain (demosaic clears each effect pass's surface) -- record it, don't
     * touch the frame boundary. */
    if (rt != 0) {
        if (s_d3d.draw_count < MAX_DRAWS) {
            D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count++];
            memset(dr, 0, sizeof(*dr));
            dr->is_vp = 1; dr->is_clear = 1; dr->tex_slot = -1;
            dr->rt_off = rt; memcpy(dr->rt_mrt, mrt, sizeof(mrt));
            dr->rt_w = rt_w; dr->rt_h = rt_h;
            if (s_d3d.current_rsx_state) dr->zeta_off = s_d3d.current_rsx_state->surface_zeta_offset;
            dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0;
            if (s_d3d.current_rsx_state) { dr->zeta_off = s_d3d.current_rsx_state->surface_zeta_offset; dr->dtest = s_d3d.current_rsx_state->depth_test_enable; dr->dfunc = s_d3d.current_rsx_state->depth_func; }
            memcpy(dr->cc, cc, sizeof(cc));
            dr->clr_flags = flags; dr->clr_depth = depth; dr->clr_stencil = stencil;
        }
        return;
    }

    memcpy(s_d3d.clear_color, cc, sizeof(cc));

    /* A DISPLAY clear marks the start of a new visible frame. If a completed
     * frame is still accumulated (the drain gulped across a frame boundary --
     * guaranteed at the FIFO ring wrap, where rest-of-frame-N + clear-N+1
     * arrive in one batch), PRESENT it now instead of discarding it. Only a
     * batch that actually contains DISPLAY draws is a completed frame: a
     * batch of offscreen pass work (render-to-texture) must keep accumulating
     * until its composite draw arrives, or the screen strobes intermediates. */
    int have_display_draws = 0;
    for (u32 i = 0; i < s_d3d.draw_count && i < MAX_DRAWS; i++)
        if (!s_d3d.draws[i].is_clear && s_d3d.draws[i].rt_off == 0) {
            have_display_draws = 1;
            break;
        }
    if (!have_display_draws)
        return;   /* keep accumulating the in-progress frame */

    /* A title that DOUBLE-BUFFERS the display (DeferredShading clears both
     * 0x0 and 0x440000 per frame, plus a HUD pass) issues several display
     * clears per real frame -- treating each as a boundary presented the
     * 1-2-draw intermediates, strobing black between the full 146-draw
     * frames. Anchor the boundary to the FLIP instead: only present once a
     * cellGcmSetFlip has landed since the last present. Titles that flip once
     * per frame (wave/cellmark/gcmcube) are unaffected -- their single
     * display clear still follows their single flip. Fall back to the old
     * clear-only heuristic for titles that never flip (fc stays 0). */
    static u32 s_frame_draws_max = 0;   /* running max frame size (typical full frame) */
    if (s_d3d.draw_count > s_frame_draws_max) s_frame_draws_max = s_d3d.draw_count;
    {
        extern unsigned cellGcm_flip_request_count(void);
        static unsigned s_last_present_flip = 0;
        unsigned fc = cellGcm_flip_request_count();
        if (fc != 0) {
            if (fc == s_last_present_flip)
                return;   /* no flip since last present -> not a real boundary */
            /* A double-buffered title issues several display clears per flip
             * (DeferredShading: clear back-buffer, HUD pass, ...). They can
             * arrive in either order, so a small batch here may be an
             * intermediate that precedes the full frame's clear. Don't present
             * (or consume the flip) until the batch is a substantial fraction
             * of a full frame -- keep accumulating so the complete frame lands
             * in one present. Gauged against the running MAX frame size (not
             * the last, which a leaked tiny batch would poison), so it
             * self-scales per title with no fixed threshold. */
            /* Two or more flips since the last present: a whole guest frame
             * went by, so this is a real boundary whatever its size. The max
             * never decayed either -- after Code Veronica's 10k-draw lighter
             * scene, its few-hundred-draw menus sat under max/4 and presented
             * once every 12k accumulated draws (0.2 fps, 120 flips per
             * present), fast or slow depending on what was played before. */
            if (s_frame_draws_max > 16 && s_d3d.draw_count < s_frame_draws_max / 4 &&
                fc - s_last_present_flip < 2)
                return;   /* intermediate: keep accumulating, flip stays pending */
            s_last_present_flip = fc;
            s_frame_draws_max -= s_frame_draws_max / 16;   /* follow the current scene */
            if (s_d3d.draw_count > s_frame_draws_max) s_frame_draws_max = s_d3d.draw_count;
        }
    }

    /* RSX_ACCUM_FRAME=1: never present at a clear boundary, so every draw in a
     * guest frame accumulates into one image. A title that clears several times
     * per frame (render-to-texture passes) otherwise gets presented mid-scene,
     * and each captured frame holds only a slice of the geometry -- the floor in
     * one, a wall in the next -- which makes a single model impossible to see. */
    { static int accum = -1;
      if (accum < 0) { const char* e = getenv("RSX_ACCUM_FRAME"); accum = e ? atoi(e) : 0; }
      if (accum) return; }
    if (s_d3d.initialized) {
        if (blink_dbg())
            printf("[CLEAR] presenting %u accumulated draws at frame boundary\n",
                   s_d3d.draw_count);
        render_frame();
        s_clear_presents++;
    }
    s_dbg_clears_since_present++;
    s_d3d.draw_count   = 0;
    s_d3d.vb_offset    = 0;
    vf_flush(); vf_frame_reset();
    s_d3d.vp_vb_offset = 0;
    s_vcb_batch++;
}

static void d3d12_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;
    /* Log only the first few; set_render_target is called every frame and
     * floods the log otherwise. */
    static int s_count = 0;
    if (s_count < 5) {
        printf("[D3D12] set_render_target(%ux%u)\n",
               state->surface_clip_w, state->surface_clip_h);
        s_count++;
    }
}

static void d3d12_set_viewport(void* ud, const rsx_state* state)
{
    (void)ud;
    /* TODO: update D3D12 viewport from RSX state */
    (void)state;
}

/* Our host vertex layout for the fallback path: position (xyz) + color (rgba)
 * + texcoord (uv), 36 bytes. (The real VP path feeds raw float4 attrib0.) */
typedef struct { float x, y, z; float r, g, b, a; float u, v; } BasicVertex;


/* Read one RSX vertex (by absolute vertex index) from guest memory into our
 * host layout. Position is attrib 0 (float3+), color is attrib 3 (ubyte4 or
 * float4); missing attribs default to opaque white. RSX stores each 32-bit
 * component big-endian, so every lane is byte-swapped. */
static void read_rsx_vertex(const rsx_state* state, u32 vindex, BasicVertex* out)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);

    out->x = out->y = out->z = 0.0f;
    out->r = out->g = out->b = out->a = 1.0f;
    out->u = out->v = 0.0f;
    if (!state || !vm_base) return;

    const rsx_vertex_attrib* pos = &state->vertex_attribs[0];
    if (pos->enabled && pos->type == 2 /* float */ && pos->size >= 2) {
        u8* src = vm_base + cellGcmResolveOffset(pos->offset + vindex * pos->stride);
        out->x = rsx_rd_bef(src);
        out->y = rsx_rd_bef(src + 4);
        if (pos->size >= 3) out->z = rsx_rd_bef(src + 8);

        /* dbgfont (and similar 2D overlays) store screen positions biased far
         * outside clip space; their vertex program folds them back to clip
         * space using transform constants we don't execute. When a position is
         * well outside NDC, recover the fractional part as a normalized [0,1]
         * screen coord and map it to NDC (screen Y-down -> clip Y-up). In this
         * layout the attribute is [posX, posY, U, V] (size 4), so the last two
         * components are the atlas texcoords, not depth.
         * TODO: execute the real vertex-program / RSX viewport transform so
         * this is not needed. */
        if (out->x > 2.0f || out->x < -2.0f || out->y > 2.0f || out->y < -2.0f) {
            float sx = out->x - floorf(out->x);
            float sy = out->y - floorf(out->y);
            out->x = sx * 2.0f - 1.0f;
            out->y = 1.0f - sy * 2.0f;
            out->z = 0.0f;
            if (pos->size >= 4) {
                out->u = rsx_rd_bef(src + 8);   /* U */
                out->v = rsx_rd_bef(src + 12);  /* V */
            }
        }
    }

    const rsx_vertex_attrib* col = &state->vertex_attribs[3];
    if (col->enabled && col->size >= 3) {
        u8* src = vm_base + cellGcmResolveOffset(col->offset + vindex * col->stride);
        if (col->type == 4 /* ubyte */) {
            out->r = src[0] / 255.0f;
            out->g = src[1] / 255.0f;
            out->b = src[2] / 255.0f;
            out->a = (col->size >= 4) ? src[3] / 255.0f : 1.0f;
        } else if (col->type == 2 /* float */) {
            u32 fr, fg, fb, fa;
            memcpy(&fr, src,     4); fr = ((fr>>24)&0xFF)|((fr>>8)&0xFF00)|((fr<<8)&0xFF0000)|((fr<<24)&0xFF000000);
            memcpy(&fg, src + 4, 4); fg = ((fg>>24)&0xFF)|((fg>>8)&0xFF00)|((fg<<8)&0xFF0000)|((fg<<24)&0xFF000000);
            memcpy(&fb, src + 8, 4); fb = ((fb>>24)&0xFF)|((fb>>8)&0xFF00)|((fb<<8)&0xFF0000)|((fb<<24)&0xFF000000);
            memcpy(&out->r, &fr, 4); memcpy(&out->g, &fg, 4); memcpy(&out->b, &fb, 4);
            if (col->size >= 4) {
                memcpy(&fa, src + 12, 4); fa = ((fa>>24)&0xFF)|((fa>>8)&0xFF00)|((fa<<8)&0xFF0000)|((fa<<24)&0xFF000000);
                memcpy(&out->a, &fa, 4);
            }
        }
    }
}

/* Upload `count` sequential vertices [first, first+count). Returns the count
 * actually written (clamped to the remaining per-frame buffer). */
static u32 upload_vertices_from_rsx(u32 first, u32 count)
{
    BasicVertex* verts = (BasicVertex*)((u8*)s_d3d.vb_mapped + s_d3d.vb_offset);
    u32 max_verts = (MAX_VERTICES * VERTEX_STRIDE - s_d3d.vb_offset) / sizeof(BasicVertex);
    if (count > max_verts) count = max_verts;
    const rsx_state* state = s_d3d.current_rsx_state;
    for (u32 i = 0; i < count; i++)
        read_rsx_vertex(state, first + i, &verts[i]);
    s_d3d.vb_offset += count * sizeof(BasicVertex);
    return count;
}

/* Upload RSX QUADS (prim 8) as a triangle list: each 4-vertex quad v0..v3
 * (perimeter winding) splits into triangles (v0,v1,v2) and (v0,v2,v3).
 * D3D12 has no quad topology, so this expansion is how quads render at all.
 * Returns the number of triangle-list vertices emitted (6 per quad). */
static u32 upload_quads_from_rsx(u32 first, u32 count)
{
    const rsx_state* state = s_d3d.current_rsx_state;
    u32 quads = count / 4;
    u32 max_verts = (MAX_VERTICES * VERTEX_STRIDE - s_d3d.vb_offset) / sizeof(BasicVertex);
    if (quads * 6 > max_verts) quads = max_verts / 6;
    BasicVertex* verts = (BasicVertex*)((u8*)s_d3d.vb_mapped + s_d3d.vb_offset);
    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        BasicVertex c[4];
        for (u32 k = 0; k < 4; k++)
            read_rsx_vertex(state, first + q * 4 + k, &c[k]);
        verts[o++] = c[0]; verts[o++] = c[1]; verts[o++] = c[2];
        verts[o++] = c[0]; verts[o++] = c[2]; verts[o++] = c[3];
    }
    s_d3d.vb_offset += o * sizeof(BasicVertex);
    return o;
}

/* Upload RSX QUADS as raw float4 attrib0 (byte-swapped) into vp_vb, expanded
 * to a triangle list (6 verts/quad). The decompiled vertex shader does the
 * transform. Returns emitted vertex count. */
/* Generic VP vertex: all 16 RSX vertex attributes, each converted to a float4
 * slot -- 256 bytes/vertex, input layout ATTRi @ i*16. Apps place attributes at
 * arbitrary indices (tiny3d: pos=a0 colour=a3 tex=a8; SDK gcm samples: pos=a0
 * colour=a1 tex=a2; dbgfont: a0..a2), so a hardcoded pos+colour pair can't
 * cover them: every enabled attrib is fetched from guest memory and converted
 * by its RSX type; disabled slots read (0,0,0,1). */
typedef struct { float v[4]; } VPSlot;
#define VP_VERT_STRIDE (16 * sizeof(VPSlot))   /* 256 */


void rsx_vtx_pos_dbg(const rsx_state* state, const float* v, u32 n);

/* Fill all 16 attribute slots for one vertex. The per-attribute work lives in
 * rsx_vertex_fetch.c so the Metal and null backends read guest vertices the
 * same way this one does, rather than each porting the logic again. */
/* Converted-vertex memo for one upload_*_vp call: strips and indexed meshes
 * expanded to triangle lists fetch each vertex 3-6 times, and the fetch (16
 * attributes, byte-swapped and format-converted) was the largest cost of
 * recording a draw. Direct-mapped by vertex index; s_vmemo_gen is bumped at
 * the start of every upload call so attribute state never leaks between
 * draws. Record thread only. VTX_NOMEMO=1 disables it. */
#define VMEMO_N 1024u
static VPSlot s_vmemo[VMEMO_N][16];
static u32 s_vmemo_key[VMEMO_N], s_vmemo_tag[VMEMO_N];
static u32 s_vmemo_gen = 1;
/* The draw's fetch plan (rsx_fetch_plan_build), rebuilt with the memo at the
 * start of every upload_*_vp call. VTX_NOPLAN=1 fetches attribute by
 * attribute as before. */
static rsx_fetch_plan s_vplan;
static const rsx_state* s_vplan_state = NULL;
static void vp_fetch_begin(const rsx_state* state)
{
    ++s_vmemo_gen;
    static int noplan = -1;
    if (noplan < 0) noplan = getenv("VTX_NOPLAN") ? 1 : 0;
    s_vplan_state = NULL;
    if (state && !noplan) { rsx_fetch_plan_build(state, &s_vplan); s_vplan_state = state; }
}
#define VP_FETCH_BEGIN() vp_fetch_begin(state)

/* RSX_GPU_VFETCH, record side. Each output vertex slot of vp_vb keeps its
 * place (draw merging and offsets are unchanged) but gets an 8-byte record
 * instead of 256 converted bytes. A draw's descriptor is opened by its first
 * vertex and filled by vf_flush once the draw call is done (the attribute
 * state cannot change inside one call): the vertex range it used is copied
 * once, raw. */
#define VF_RANGE_MAX (4u << 20)   /* larger ranges: converted here instead */
static int s_vf_cur = -1;
static const rsx_state* s_vf_state;
static u32 s_vf_vmin, s_vf_vmax, s_vf_rmin, s_vf_rmax;
static u8* vf_parity_base(void) { return s_vf_mapped + (u64)s_d3d.vp_parity * vf_parity_bytes(); }
static void pose_note_raw_vertex(const rsx_state* state,VPSlot* dst,u32 vi);
static u32 s_vf_nflush, s_vf_nfall, s_vf_nchecked, s_vf_maxdesc, s_vf_maxarena;
static void vf_frame_reset(void)
{
    if (s_vf_ndesc > s_vf_maxdesc) s_vf_maxdesc = s_vf_ndesc;
    if (s_vf_aused > s_vf_maxarena) s_vf_maxarena = s_vf_aused;
    static ULONGLONG t0; const ULONGLONG now = GetTickCount64();
    if (!t0) t0 = now;
    if (s_vf_mapped && now - t0 >= 5000) {
        if(frame_profile_on())fprintf(stderr, "[VFETCH] 5s: draws=%u converted_here=%u checked=%u max_desc=%u max_arena=%uKB%c",
                s_vf_nflush, s_vf_nfall, s_vf_nchecked, s_vf_maxdesc, s_vf_maxarena >> 10, 10);
        s_vf_nflush = s_vf_nfall = s_vf_nchecked = s_vf_maxdesc = s_vf_maxarena = 0; t0 = now;
    }
    s_vf_ndesc = 0; s_vf_aused = 0; s_vf_cur = -1;
}
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
static u32 vf_elem_bytes(u32 type, u32 n)
{
    switch (type) { case 1: case 3: case 5: return 2 * n; case 4: case 7: return n; case 6: return 4; default: return 4 * n; }
}
static void vf_copy_guest(u8* dst, int main_mem, u32 off, u32 len)
{
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    if (!main_mem) { memcpy(dst, vm_base + cellGcmResolveLocated(1, off), len); return; }
    while (len) {   /* IO pages need not be contiguous */
        const u32 in = off & 0xFFFFFu; u32 n = 0x100000u - in; if (n > len) n = len;
        memcpy(dst, vm_base + cellGcmResolveLocated(0, off & ~0xFFFFFu) + in, n);
        dst += n; off += n; len -= n;
    }
}
/* Too wide a vertex range for this draw's few vertices (or no room): convert
 * the vertices it uses here, as before, into the arena as host floats. */
static void vf_fallback(rsx_fetch_plan* plan, int cur, u32* desc)
{
    enum { HN = 1 << 18 };
    static u32 hkey[HN], hval[HN], hgen[HN], gen;
    if (++gen == 0) { memset(hgen, 0, sizeof hgen); gen = 1; }
    u8* base = vf_parity_base();
    u32* recs = (u32*)base;
    const u32 start = (s_vf_aused + 255u) & ~255u;
    u32 n = 0;
    for (u32 r = s_vf_rmin; r <= s_vf_rmax; r++) {
        u32* rec = recs + (u64)r * 2;
        if (rec[1] != (u32)cur) continue;
        const u32 vi = rec[0];
        u32 h = (vi * 2654435761u) & (HN - 1);
        while (hgen[h] == gen && hkey[h] != vi) h = (h + 1) & (HN - 1);
        if (hgen[h] != gen) {
            if (start + (n + 1) * 256u > VF_ARENA_BYTES) {
                static int w = 0; if (w++ < 4) fprintf(stderr, "[VFETCH] arena full%c", 10);
                break;
            }
            hgen[h] = gen; hkey[h] = vi; hval[h] = n;
            rsx_fetch_vertex_planned(plan, vi, (float (*)[4])(base + VF_ARENA_OFF + start + (u64)n * 256u));
            n++;
        }
        rec[0] = hval[h];
    }
    s_vf_aused = start + n * 256u;
    for (int i = 0; i < 16; i++) {
        desc[i * 8 + 0] = (u32)(VF_ARENA_OFF + start) + 16u * (u32)i;
        desc[i * 8 + 1] = 256u;
        desc[i * 8 + 2] = 0x80000000u | 15u | (4u << 4);
        desc[i * 8 + 3] = 0;
    }
}
/* VF_CHECK=<N>: decode N recorded vertices from the copied bytes the way
 * the shader does and compare with the CPU conversion. Diagnostic. */
static u32 vf_ld_host(const u8* b, u32 a) { u32 w; memcpy(&w, b + a, 4); return w; }
static u32 vf_bs32_host(u32 w) { return (w >> 24) | ((w >> 8) & 0xFF00u) | ((w << 8) & 0xFF0000u) | (w << 24); }
/* F12 only: decode the frozen raw bank, not live guest bytes. Include the
 * pose override actually read by the VS. No upload-heap reads per frame. */
static void vf_capture_vertex(u32 parity,u32 vertex,float out[16][4]) {
    const u8* base=s_vf_mapped+(u64)parity*vf_parity_bytes();
    const u32* rec=(const u32*)(base+(size_t)vertex*8);
    const u32* desc=(const u32*)(base+VF_DESC_OFF+(size_t)rec[1]*VF_DESC_SIZE);
    for(int i=0;i<16;i++) {
        const u32* h=desc+i*8;float* o=out[i];memcpy(o,h+4,16);
        if(!(h[2]&0x80000000u))continue;
        u32 vi=h[3]?((h[2]&0x80u)?rec[0]%h[3]:rec[0]/h[3]):rec[0];
        const u8* p=base+h[0]+vi*h[1];u32 type=h[2]&15u,n=(h[2]>>4)&7u;
        if(type==6) {u32 v=vf_bs32_host(vf_ld_host(p,0));
            o[0]=(float)((s32)(v<<21)>>21)/1023.f;o[1]=(float)((s32)(v<<10)>>21)/1023.f;
            o[2]=(float)((s32)v>>22)/511.f;o[3]=1;continue;}
        for(u32 k=0;k<n&&k<4;k++) {
            if(type==3)o[k]=rsx_rd_half_be(p+k*2);
            else if(type==4)o[k]=p[k]/255.f;
            else if(type==7)o[k]=(float)p[k];
            else if(type==1||type==5){s16 v=(s16)((p[k*2]<<8)|p[k*2+1]);o[k]=type==1?v/32767.f:(float)v;}
            else if(type==15)memcpy(o+k,p+k*4,4);
            else o[k]=rsx_rd_bef(p+k*4);
        }
    }
    if(vf_pose_enabled()) {
        const u8* pose=s_pose_compute_capture?s_pose_compute_capture+(size_t)vertex*16:base+VF_POSE_OFF+(size_t)vertex*vf_pose_stride();
        rsx_pose_gpu_read(pose,out[0]);
    }
}
static void vf_check(rsx_fetch_plan* plan, int cur, const u32* desc)
{
    static int cap = -1, done = 0, bad = 0;
    if (cap < 0) { const char* e = getenv("VF_CHECK"); cap = e ? atoi(e) : 0; }
    if (done >= cap) return;
    const u8* base = vf_parity_base();
    const u32* recs = (const u32*)base;
    for (u32 r = s_vf_rmin; r <= s_vf_rmax && done < cap; r++) {
        const u32* rec = recs + (u64)r * 2;
        if (rec[1] != (u32)cur) continue;
        done++; s_vf_nchecked++;
        const u32 vi = rec[0];
        float ref[16][4];
        rsx_fetch_vertex_planned(plan, vi, ref);
        for (int i = 0; i < 16; i++) {
            const u32* h = desc + i * 8;
            float o[4]; memcpy(o, h + 4, 16);
            if (h[2] & 0x80000000u) {
                const u32 t = h[2] & 15u, n = (h[2] >> 4) & 7u;
                const u32 ei = h[3] ? ((h[2] & 0x80u) ? vi % h[3] : vi / h[3]) : vi;
                const u32 a = h[0] + ei * h[1];
                for (u32 k = 0; k < n && k < 4; k++) {
                    u32 w;
                    if (t == 2u) { w = vf_bs32_host(vf_ld_host(base, a + 4 * k)); memcpy(&o[k], &w, 4); }
                    else if (t == 3u) { const u8* q = base + a + 2 * k; o[k] = rsx_rd_half_be(q); }
                    else if (t == 4u) o[k] = base[a + k] / 255.0f;
                    else if (t == 15u) { w = vf_ld_host(base, a + 4 * k); memcpy(&o[k], &w, 4); }
                    else o[k] = NAN;
                }
            }
            if (memcmp(o, ref[i], 16) && bad++ < 40)
                fprintf(stderr, "[VFCHECK] desc=%d vi=%u attr=%d type=%u n=%u stride=%u base=%08X -> (%g %g %g %g) want (%g %g %g %g)%c",
                        cur, vi, i, h[2] & 15u, (h[2] >> 4) & 7u, h[1], h[0], o[0], o[1], o[2], o[3],
                        ref[i][0], ref[i][1], ref[i][2], ref[i][3], 10);
        }
    }
}
static void vf_flush(void)
{
    if (s_vf_cur < 0) return;
    const int cur = s_vf_cur; s_vf_cur = -1;
    const rsx_state* st = s_vf_state;
    s_vf_nflush++;
    rsx_fetch_plan local;
    rsx_fetch_plan* plan = &s_vplan;
    if (s_vplan_state != st) { rsx_fetch_plan_build(st, &local); plan = &local; }
    u32* desc = (u32*)(vf_parity_base() + VF_DESC_OFF + (u64)cur * VF_DESC_SIZE);
    struct { int main_mem; u64 s, e; u32 arena; } seg[16];
    int nseg = 0, segof[16];
    for (int i = 0; i < 16; i++) {
        const rsx_attr_plan* a = &plan->a[i];
        memcpy(&desc[i * 8 + 4], a->cst, 16);
        desc[i * 8 + 0] = desc[i * 8 + 1] = desc[i * 8 + 2] = desc[i * 8 + 3] = 0;
        segof[i] = -1;
        if (!a->active) continue;
        u32 e0 = s_vf_vmin, e1 = s_vf_vmax;
        if (a->freq) { if (a->modulo) { e0 = 0; e1 = a->freq - 1; } else { e0 /= a->freq; e1 /= a->freq; } }
        const u64 rs = (u64)a->off0 + (u64)e0 * a->stride;
        const u64 re = (u64)a->off0 + (u64)e1 * a->stride + vf_elem_bytes(a->type, a->n);
        int k = 0;
        for (; k < nseg; k++)   /* interleaved arrays share one copy */
            if (seg[k].main_mem == a->main_mem && rs <= seg[k].e + 4096 && seg[k].s <= re + 4096) {
                if (rs < seg[k].s) seg[k].s = rs;
                if (re > seg[k].e) seg[k].e = re;
                break;
            }
        if (k == nseg) { seg[k].main_mem = a->main_mem; seg[k].s = rs; seg[k].e = re; nseg++; }
        segof[i] = k;
    }
    u64 total = 0;
    for (int k = 0; k < nseg; k++) total += seg[k].e - seg[k].s + 32;
    if (total > VF_RANGE_MAX || s_vf_aused + total > VF_ARENA_BYTES) { s_vf_nfall++; vf_fallback(plan, cur, desc); return; }
    u8* arena = vf_parity_base() + VF_ARENA_OFF;
    for (int k = 0; k < nseg; k++) {
        const u32 at = (s_vf_aused + 15u) & ~15u, len = (u32)(seg[k].e - seg[k].s);
        vf_copy_guest(arena + at, seg[k].main_mem, (u32)seg[k].s, len);
        seg[k].arena = (u32)VF_ARENA_OFF + at;
        s_vf_aused = at + len + 8;   /* vf_ld may read a word past the end */
    }
    for (int i = 0; i < 16; i++) {
        if (segof[i] < 0) continue;
        const rsx_attr_plan* a = &plan->a[i];
        desc[i * 8 + 0] = seg[segof[i]].arena + (a->off0 - (u32)seg[segof[i]].s);
        desc[i * 8 + 1] = a->stride;
        desc[i * 8 + 2] = 0x80000000u | (a->type & 15u) | ((a->n & 7u) << 4) | (a->modulo ? 0x80u : 0u);
        desc[i * 8 + 3] = a->freq;
    }
    vf_check(plan, cur, desc);
}

static void pose_note_vertex(const VPSlot* dst,const VPSlot* src,u32 source_index) {
    if(!s_pose_on)return;
    uintptr_t at=(uintptr_t)dst,base=(uintptr_t)s_d3d.vp_vb_mapped;
    if(at<base||at-base>=(size_t)MAX_VERTICES*vp_sets()*256||(at-base)%256)return;
    RsxPoseVertex* v=s_pose_vertices+(at-base)/256;
    rsx_pose_record_source_vertex(v,(const float (*)[4])src,s_pose_stamp,source_index);
}
static int pose_fetch_fast_on(void) {
    static int on=-1;
    if(on<0){const char* fps=getenv("CV_FPS_TEST_60"),*off=getenv("CV_POSE_FETCH_FAST");
        on=fps&&!strcmp(fps,"1")&&!(off&&!strcmp(off,"0"));}
    return on;
}
static void pose_note_raw_vertex(const rsx_state* state,VPSlot* dst,u32 vi) {
    const u32 h=(vi*2654435761u)>>22;VPSlot* c=s_vmemo[h];
    if(s_vmemo_tag[h]!=s_vmemo_gen||s_vmemo_key[h]!=vi) {
        if(s_vplan_state==state){
            if(pose_fetch_fast_on())rsx_fetch_pose_vertex(&s_vplan,vi,(float (*)[4])c);
            else rsx_fetch_vertex_selected(&s_vplan,vi,RSX_POSE_ATTRIBUTE_MASK,(float (*)[4])c);
        }
        else {rsx_fetch_attrib(state,0,vi,c[0].v);rsx_fetch_attrib(state,8,vi,c[8].v);rsx_fetch_attrib(state,15,vi,c[15].v);}
        s_vmemo_tag[h]=s_vmemo_gen;s_vmemo_key[h]=vi;
    }
    pose_note_vertex(dst,c,vi);
}
static void read_vp_vertex(const rsx_state* state, u32 vi, VPSlot* out16)
{
    if (s_vf_mapped) { vf_note(state, out16, vi); return; }
    static int nomemo = -1;
    if (nomemo < 0) nomemo = getenv("VTX_NOMEMO") ? 1 : 0;
    const u32 h = (vi * 2654435761u) >> 22;   /* 10 bits */
    VPSlot* c = s_vmemo[h];
    if (!nomemo && s_vmemo_tag[h] == s_vmemo_gen && s_vmemo_key[h] == vi) {
        s_vmemo_hits++;
        pose_note_vertex(out16,c,vi);
        memcpy(out16, c, 16 * sizeof(VPSlot));
        return;
    }
    s_vmemo_miss++;
    /* Convert into cached memory, then one sequential copy: out16 is the
     * write-combined upload heap, and fetch writes each slot twice (the
     * constant default, then the array value). */
    if (s_vplan_state == state)
        rsx_fetch_vertex_planned(&s_vplan, vi, (float (*)[4])c);
    else
        for (int i = 0; i < 16; i++)
            rsx_fetch_attrib(state, i, vi, c[i].v);
    { const rsx_vertex_attrib* a = &state->vertex_attribs[0];
      u32 n = a->size ? a->size : 4; if (n > 4) n = 4;
      rsx_vtx_pos_dbg(state, c[0].v, n); }
    s_vmemo_tag[h] = s_vmemo_gen; s_vmemo_key[h] = vi;
    pose_note_vertex(out16,c,vi);
    memcpy(out16, c, 16 * sizeof(VPSlot));
}

/* Place a vertex read into a local copy (quads) at its output slot. */
static void vp_put(VPSlot* dst, const VPSlot* src, const rsx_state* state,u32 source_index)
{
    if (s_vf_mapped) { vf_note(state, dst, source_index); return; }
    pose_note_vertex(dst, src,source_index);
    memcpy(dst, src, 16 * sizeof(VPSlot));
}

/* VTX_POS=<N>: print the first fetched position of the first N draws. A guest
 * vertex array that resolves to the wrong memory reads as garbage/denormals, and
 * that is indistinguishable from "the shader is wrong" without seeing the input. */
void rsx_vtx_pos_dbg(const rsx_state* state, const float* v, u32 n)
{
    static int cap = -1, seen = 0;
    if (cap < 0) { const char* e = getenv("VTX_POS"); cap = e ? atoi(e) : 0; }
    if (!cap || seen >= cap) return;
    seen++;
    const rsx_vertex_attrib* a = &state->vertex_attribs[0];
    fprintf(stderr, "[VTXPOS] a0 off=0x%X stride=%u size=%u type=%u -> (%g, %g, %g, %g)\n",
            a->offset, a->stride, a->size, a->type,
            v[0], v[1], v[2], v[3]);   /* all four: masking by `size` hid whether w survived */
}

static void vp_attrs_dbg(const rsx_state* state)
{
    if (!getenv("VP_ATTRS")) return;
    /* VP_ATTRS_FP=<hex shader_program>: dump only the draws that use one
     * fragment program. Without it the first six draws are whatever the frame
     * happens to start with, which is never the mesh being investigated. */
    { static const char* e = (const char*)1; static u32 want = 0;
      if (e == (const char*)1) { e = getenv("VP_ATTRS_FP");
          want = e ? (u32)strtoul(e, NULL, 0) : 0; }
      if (want && (!state || state->shader_program != want)) return; }
    /* Dedupe by the set of enabled attributes rather than printing the first
     * few draws: one fragment program can be used by meshes with different
     * vertex layouts, and a mesh missing an attribute takes the constant
     * register instead -- which is exactly the case worth seeing. */
    { u32 mask = 0;
      for (int i = 0; i < 16; i++) if (state->vertex_attribs[i].enabled) mask |= 1u << i;
      static u32 seen[16]; static int ns = 0;
      for (int i = 0; i < ns; i++) if (seen[i] == mask) return;
      if (ns >= 16) return;
      seen[ns++] = mask; }
    fprintf(stderr, "[VPATTR] --- fp=0x%X ---%c", state->shader_program, 10);
    fprintf(stderr, "[VPATTR] divider_op=0x%08X\n", state->frequency_divider_op);
    for (int i = 0; i < 16; i++) {
        const rsx_vertex_attrib* a = &state->vertex_attribs[i];
        if (a->enabled) fprintf(stderr, "[VPATTR] a%d off=0x%X stride=%u size=%u type=%u freq=%u fmt=0x%08X\n",
                                i, a->offset, a->stride, a->size, a->type, a->frequency, a->format);
    }
    /* VP_ATTRS_DUMP=<attr>:<n>: read n entries of that attribute straight out of
     * guest memory. Answers "did the guest write these values, or did our fetch
     * mangle them" without inferring it from the rendered image. */
    { const char* e = getenv("VP_ATTRS_DUMP"); if (!e) return;
      int ai = 0, cnt = 8; sscanf(e, "%d:%d", &ai, &cnt);
      const rsx_vertex_attrib* a = &state->vertex_attribs[ai];
      if (!a->enabled || !a->stride) return;
      extern uint8_t* vm_base;
      extern u32 cellGcmResolveLocated(int, u32);
      u32 nbad = 0, nzero = 0;
      for (int v = 0; v < cnt; v++) {
          u32 aoff = (a->offset & 0x7FFFFFFFu) + (u32)v * a->stride;
          const u8* q = vm_base + ((a->offset & 0x80000000u)
                        ? cellGcmResolveLocated(0, aoff) : cellGcmResolveLocated(1, aoff));
          /* Also read the same offset through the OTHER context-DMA. An array
           * the guest put in main memory but whose offset we resolve as LOCAL
           * reads as untouched VRAM, i.e. all zeros -- indistinguishable from
           * "the producer never ran". */
          if (v == 0) {
              u32 la = cellGcmResolveLocated(1, aoff), ma = cellGcmResolveLocated(0, aoff);
              const u8* lp = vm_base + la; const u8* mp = vm_base + ma;
              const u8* rp = vm_base + (a->offset & 0x7FFFFFFFu);   /* raw, unresolved */
              fprintf(stderr, "[VPDUMP] a%d off=0x%X  LOCAL@0x%X=(%g,%g,%g)  MAIN@0x%X=(%g,%g,%g)"
                              "  RAW@0x%X=(%g,%g,%g)%c",
                      ai, aoff, la, rsx_rd_bef(lp), rsx_rd_bef(lp+4), rsx_rd_bef(lp+8),
                      ma, rsx_rd_bef(mp), rsx_rd_bef(mp+4), rsx_rd_bef(mp+8),
                      (a->offset & 0x7FFFFFFFu), rsx_rd_bef(rp), rsx_rd_bef(rp+4), rsx_rd_bef(rp+8), 10);
          }
          u32 nc = a->size ? a->size : 3; if (nc > 3) nc = 3;
          float f[3] = {0,0,0};
          for (u32 k = 0; k < nc; k++) f[k] = rsx_rd_bef(q + k * 4);
          if (v < 12)
              fprintf(stderr, "[VPDUMP] a%d[%d] = (%g, %g, %g)%c", ai, v, f[0], f[1], f[2], 10);
          int allz = 1; for (u32 k = 0; k < nc; k++) if (f[k] != 0.f) allz = 0;
          if (allz) nzero++;
          else if (f[2] < -0.9f && f[0] > -0.1f && f[0] < 0.1f) nbad++;
      }
      fprintf(stderr, "[VPDUMP] a%d over %d verts: %u zero, %u near (0,0,-1)%c",
              ai, cnt, nzero, nbad, 10);
      /* How many entries differ from the first? An all-identical position array
       * is a degenerate mesh: it rasterizes nothing, which looks the same as
       * "the draw never happened". */
      { float f0[3]; u32 o0 = (a->offset & 0x7FFFFFFFu);
        const u8* q0 = vm_base + ((a->offset & 0x80000000u)
                       ? cellGcmResolveLocated(0, o0) : cellGcmResolveLocated(1, o0));
        for (int k = 0; k < 3; k++) f0[k] = rsx_rd_bef(q0 + k * 4);
        u32 diff = 0;
        for (int v = 1; v < cnt; v++) {
            u32 ao = o0 + (u32)v * a->stride;
            const u8* q = vm_base + ((a->offset & 0x80000000u)
                          ? cellGcmResolveLocated(0, ao) : cellGcmResolveLocated(1, ao));
            for (int k = 0; k < 3; k++)
                if (rsx_rd_bef(q + k * 4) != f0[k]) { diff++; break; }
        }
        fprintf(stderr, "[VPDUMP] a%d: %u/%d entries differ from entry 0 (%g,%g,%g)%c",
                ai, diff, cnt, f0[0], f0[1], f0[2], 10);
        /* Extent: an isosurface with little fluid is a speck, and a speck that
         * rasterizes nothing is correct, not a bug. */
        { float lo[3] = {1e30f,1e30f,1e30f}, hi[3] = {-1e30f,-1e30f,-1e30f};
          for (int v = 0; v < cnt; v++) {
              u32 ao = o0 + (u32)v * a->stride;
              const u8* q = vm_base + ((a->offset & 0x80000000u)
                            ? cellGcmResolveLocated(0, ao) : cellGcmResolveLocated(1, ao));
              for (int k = 0; k < 3; k++) { float f = rsx_rd_bef(q + k * 4);
                  if (f < lo[k]) lo[k] = f; if (f > hi[k]) hi[k] = f; } }
          u32 nnan = 0, nfin = 0;
          for (int v = 0; v < cnt; v++) {
              u32 ao = o0 + (u32)v * a->stride;
              const u8* q = vm_base + ((a->offset & 0x80000000u)
                            ? cellGcmResolveLocated(0, ao) : cellGcmResolveLocated(1, ao));
              int bad = 0;
              for (int k = 0; k < 3; k++) { float f = rsx_rd_bef(q + k * 4);
                  if (!(f == f) || f > 1e30f || f < -1e30f) bad = 1; }
              if (bad) nnan++; else nfin++;
          }
          fprintf(stderr, "[VPDUMP] a%d extent x[%g..%g] y[%g..%g] z[%g..%g]"
                          "  finite=%u nan/inf=%u%c",
                  ai, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], nfin, nnan, 10); } } }
}

static u32 upload_quads_vp(const rsx_state* state, u32 first, u32 count)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    vp_attrs_dbg(state);
    u32 quads = count / 4;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (quads * 6 > maxv) quads = maxv / 6;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, first + q*4 + k, c[k]);
        /* quad -> two triangles (perimeter winding) */
        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { vp_put(&out[o*16], c[idx[t]], state,first+q*4+(u32)idx[t]); o++; }
        if (getenv("VTX_DUMP")) { static int _n=0; if (_n++ < 4) {
            FILE* f = fopen("vtx_dump.txt", _n==1 ? "w" : "a");
            if (f) { for (u32 k = 0; k < 4; k++)
                fprintf(f, "q%02d v%u a0=(%.3f,%.3f,%.3f,%.3f) a8=(%.3f,%.3f)\n", _n, k,
                    c[k][0].v[0],c[k][0].v[1],c[k][0].v[2],c[k][0].v[3],
                    c[k][8].v[0],c[k][8].v[1]);
              fclose(f); } } }
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* Straight triangle-list upload through the VP path (gcm/cube's cube draws
 * TRIANGLES, prim 5 -- no expansion needed). */
static u32 upload_tris_vp(const rsx_state* state, u32 first, u32 count)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    s_req_verts += count; s_req_draws++;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    vp_attrs_dbg(state);
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (count > maxv) { s_drop_draws++;
        { static int _n = 0; if (getenv("VBFULL") && _n++ < 12)
            fprintf(stderr, "[VBFULL] fp=0x%X wanted %u verts, room for %u%c",
                    state->shader_program, count, maxv, 10); }
        count = maxv - (maxv % 3); }
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    { double _tv = perf_on() ? perf_now() : 0.0;
      for (u32 k = 0; k < count; k++)
          read_vp_vertex(state, first + k, &out[k*16]);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; } }
    /* IDXDBG=<hex shader_program>: vertex range for that program's draws. */
    { static const char* ie = (const char*)1; static u32 iw = 0;
      if (ie == (const char*)1) { ie = getenv("IDXDBG");
          iw = ie ? (u32)strtoul(ie, NULL, 16) : 0; }
      if (iw && state->shader_program == iw) { static int _n = 0; if (_n++ < 12)
          fprintf(stderr, "[IDXDBG] tris first=%u count=%u a0off=0x%X stride=%u%c",
                  first, count, state->vertex_attribs[0].offset,
                  state->vertex_attribs[0].stride, 10); } }
    /* RSX_DBG_VTX=<hex tex0 offset>: dump attribute-0 positions for the draws that
     * bind that texture. The duck's texture resolves and its 9960 draws all
     * target the backbuffer, yet none of its texels reach the screen -- so the
     * question is whether its vertices are where they should be. */
    { static const char* dv = (const char*)1; static u32 want = 0; static int n = 0;
      if (dv == (const char*)1) { dv = getenv("RSX_DBG_VTX");
                                  want = dv ? (u32)strtoul(dv, NULL, 16) : 0; }
      if (want && s_d3d.cur_texs[0].raw == want && n < 6) { n++;
        fprintf(stderr, "[DUCKVTX] first=%u count=%u", first, count);
        for (u32 k = 0; k < count && k < 4; k++)
            fprintf(stderr, "  v%u=(%.2f,%.2f,%.2f)", k,
                    out[k*16].v[0], out[k*16].v[1], out[k*16].v[2]);
        { int lo = -1, hi = -1, nz = 0;
          for (int r = 0; r < RSX_MAX_VERTEX_CONSTANTS; r++) {
              const float* m = state->vertex_constants[r];
              if (m[0] || m[1] || m[2] || m[3]) { if (lo < 0) lo = r; hi = r; nz++; }
          }
          fprintf(stderr, "  | constants: %d non-zero, slots %d..%d", nz, lo, hi); }
        fprintf(stderr, "%c", 10);
      } }
    if (getenv("VTX_DUMP") && state->surface_color_offset[0] == 0xCC0000) {
        static int _n=0; if (_n++ < 1) {
        FILE* f = fopen("vtx_dump.txt", "w");
        if (f) { fprintf(f, "G-buffer draw surf0=0x%X count=%u first=%u\n",
                         state->surface_color_offset[0], count, first);
          for (u32 k = 0; k < count && k < 30; k++)
            fprintf(f, "v%02u a0=(%.3f,%.3f,%.3f) a2=(%.3f,%.3f,%.3f) a9=(%.4f,%.4f,%.4f,%.4f)\n", k,
                out[k*16].v[0],out[k*16].v[1],out[k*16].v[2],
                out[k*16+2].v[0],out[k*16+2].v[1],out[k*16+2].v[2],
                out[k*16+9].v[0],out[k*16+9].v[1],out[k*16+9].v[2],out[k*16+9].v[3]);
          fclose(f); } } }
    s_d3d.vp_vb_offset += count * VP_VERT_STRIDE;
    return count;
}

/* Non-indexed TRIANGLE_STRIP (fan=0) / TRIANGLE_FAN (fan=1) through the VP
 * path, expanded to a triangle list. LBP's Bink movie draws its YUV quad as a
 * non-indexed 4-vertex strip (prim 6); without this it fell to the fixed-
 * function fallback with is_vp=0 (no vertex-program transform -> the quad
 * collapsed to a line) and textured=0 (the Y/U/V planes never sampled). */
/* t_begin: the strip's triangles before it were already emitted (a run joined
 * entry by entry appends only the new ones). */
static u32 upload_strip_vp_from(const rsx_state* state, u32 first, u32 count, int fan, u32 t_begin)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    if (count < 3 || t_begin >= count - 2) return 0;
    u32 tris = count - 2;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if ((tris - t_begin) * 3 > maxv) tris = t_begin + maxv / 3;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    for (u32 t = t_begin; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        /* strip winding alternates per triangle to keep facing consistent */
        u32 i1 = t + 1, i2 = t + 2;
        if (!fan && (t & 1)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
        read_vp_vertex(state, first + i0, &out[o*16]); o++;
        read_vp_vertex(state, first + i1, &out[o*16]); o++;
        read_vp_vertex(state, first + i2, &out[o*16]); o++;
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}
static u32 upload_strip_vp(const rsx_state* state, u32 first, u32 count, int fan)
{
    return upload_strip_vp_from(state, first, count, fan, 0);
}

/* Fetch index k from the guest index array (SET_INDEX_ARRAY_ADDRESS/_DMA:
 * dma [3:0] = location (0 local, 1 main), [7:4] = type (0 u32, 1 u16)).
 * Indices are big-endian in guest memory. */
static u32 read_guest_index(const rsx_state* st, u32 k)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    int local = ((st->index_array_dma & 0xF) == 0);
    u32 base = cellGcmResolveLocated(local, st->index_array_offset);
    const u8* p = vm_base + base;
    if (((st->index_array_dma >> 4) & 0xF) == 1) {
        p += (u64)k * 2;
        return ((u32)p[0] << 8) | p[1];
    }
    p += (u64)k * 4;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* Indexed variants of the VP-path uploads. */
static u32 upload_quads_vp_indexed(const rsx_state* state, u32 first, u32 count)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    u32 quads = count / 4;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (quads * 6 > maxv) quads = maxv / 6;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];u32 source_indices[4];
        for (u32 k = 0; k < 4; k++) {
            source_indices[k]=read_guest_index(state,first+q*4+k);
            read_vp_vertex(state,source_indices[k],c[k]);
        }
        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { vp_put(&out[o*16], c[idx[t]], state,source_indices[idx[t]]); o++; }
        if (getenv("VTX_DUMP")) { static int _n=0; if (_n++ < 6) {
            FILE* f = fopen("vtx_dump.txt", _n==1 ? "w" : "a");
            if (f) {
                const float* vs_ = state->viewport_scale;
                const float* vo_ = state->viewport_offset;
                fprintf(f, "iq%02d vp_scale=(%.1f,%.1f,%.3f) vp_off=(%.1f,%.1f,%.3f)\n", _n,
                        vs_[0], vs_[1], vs_[2], vo_[0], vo_[1], vo_[2]);
                for (int _c = 464; _c <= 467; _c++)
                    fprintf(f, "  c[%d]=(%.3f,%.3f,%.3f,%.3f)\n", _c,
                            state->vertex_constants[_c][0], state->vertex_constants[_c][1],
                            state->vertex_constants[_c][2], state->vertex_constants[_c][3]);
                for (u32 k = 0; k < 4; k++)
                    fprintf(f, "  v%u i%u a0=(%.3f,%.3f,%.3f,%.3f) a8=(%.3f,%.3f)\n", k,
                        read_guest_index(state, first + q*4 + k),
                        c[k][0].v[0],c[k][0].v[1],c[k][0].v[2],c[k][0].v[3],
                        c[k][8].v[0],c[k][8].v[1]);
                fclose(f);
            } } }
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

static u32 upload_tris_vp_indexed(const rsx_state* state, u32 first, u32 count)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    s_req_verts += count; s_req_draws++;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    vp_attrs_dbg(state);
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (count > maxv) { s_drop_draws++;
        { static int _n = 0; if (getenv("VBFULL") && _n++ < 12)
            fprintf(stderr, "[VBFULL] fp=0x%X wanted %u verts, room for %u%c",
                    state->shader_program, count, maxv, 10); }
        count = maxv - (maxv % 3); }
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    { double _tv = perf_on() ? perf_now() : 0.0;
      for (u32 k = 0; k < count; k++)
          read_vp_vertex(state, read_guest_index(state, first + k), &out[k*16]);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; } }
    /* IDXDBG=<hex shader_program>: index range for that program's draws. An
     * index past the vertex array reads unmapped guest memory as zero, which
     * shows up as a spike triangle with a zero texcoord, not as a missing draw. */
    { static const char* ie = (const char*)1; static u32 iw = 0;
      if (ie == (const char*)1) { ie = getenv("IDXDBG");
          iw = ie ? (u32)strtoul(ie, NULL, 16) : 0; }
      if (iw && state->shader_program == iw) { static int _n = 0; if (_n++ < 12) {
          u32 lo = 0xFFFFFFFFu, hi = 0;
          for (u32 k = 0; k < count; k++) { u32 ix = read_guest_index(state, first + k);
              if (ix < lo) lo = ix; if (ix > hi) hi = ix; }
          fprintf(stderr, "[IDXDBG] first=%u count=%u idx=[%u..%u] dma=0x%X a0off=0x%X stride=%u%c",
                  first, count, lo, hi, state->index_array_dma,
                  state->vertex_attribs[0].offset, state->vertex_attribs[0].stride, 10); } } }
    /* DBG_LOCK tracking: projected centroid of this mesh, updated every draw. */
    { static const char* dl = (const char*)1; static u32 wantl = 0; static int mvpb = 256;
      if (dl == (const char*)1) { dl = getenv("RSX_DBG_VTX");
                                  wantl = dl ? (u32)strtoul(dl, NULL, 16) : 0;
                                  const char* mb = getenv("MVP_BASE");
                                  if (mb) mvpb = atoi(mb); }
      /* RSX_DBG_TRACK=1 tracks the content-identified duck without needing its
       * offset passed in (VRAM offsets move between runs). Re-read every call:
       * s_duck_raw is filled by the texture upload, which happens AFTER the
       * first draws, so caching it once left the tracker watching offset 0. */
      { static int dtrk = -1;
        if (dtrk < 0) { const char* e = getenv("RSX_DBG_TRACK"); dtrk = e ? atoi(e) : 0; }
        if (dtrk && s_duck_raw) wantl = s_duck_raw; }
      /* RSX_DBG_PICK=<first>: track ONLY the draw with that starting index. The
       * mesh is one big vertex buffer sliced into 256-index chunks, so averaging
       * across all of them converges on the centre of the whole field -- which is
       * where there is nothing in particular. A single slice is a stable target
       * and can be magnified without drifting off it. */
      static int pick = -1;
      if (pick < 0) { const char* e = getenv("RSX_DBG_PICK"); pick = e ? atoi(e) : -2; }
      if (wantl && s_d3d.cur_texs[0].raw == wantl && count
          && (pick == -2 || (int)first == pick)) {
          double sx = 0, sy = 0, sz = 0;
          for (u32 k = 0; k < count; k++) {
              sx += out[k*16].v[0]; sy += out[k*16].v[1]; sz += out[k*16].v[2];
          }
          float v[4] = { (float)(sx/count), (float)(sy/count), (float)(sz/count), 1.0f };
          float clip[4];
          for (int r = 0; r < 4; r++) {
              const float* m = state->vertex_constants[mvpb + r];
              clip[r] = m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3];
          }
          if (clip[3] > 1e-6f) {
              float nx = clip[0]/clip[3], ny = clip[1]/clip[3];
              if (nx > -2.0f && nx < 2.0f && ny > -2.0f && ny < 2.0f) {
                  s_lock_sx += nx; s_lock_sy += ny; s_lock_n++;
                  if (!s_lock_valid) {        /* seed so frame 1 is usable */
                      s_lock_x = nx; s_lock_y = ny; s_lock_valid = 1;
                  }
              }
          }
      } }
    /* RSX_DBG_SCALE=<f>: multiply attribute-0 positions for the draws binding
     * RSX_DBG_VTX's texture. The duck's object-space bbox is ~0.2 units and its
     * draws rasterize only a few dozen pixels per frame -- sub-pixel. Growing it
     * about its own origin tests the rest of the chain (index buffer, texture,
     * per-draw transform): if a duck-shaped, duck-coloured object appears, only
     * the scale is wrong. Diagnostic. */
    { static const char* ds = (const char*)1; static float sc = 0.0f;
      if (ds == (const char*)1) { ds = getenv("RSX_DBG_SCALE");
                                  sc = ds ? (float)atof(ds) : 0.0f; }
      static const char* dv2 = (const char*)1; static u32 want2 = 0;
      if (dv2 == (const char*)1) { dv2 = getenv("RSX_DBG_VTX");
                                   want2 = dv2 ? (u32)strtoul(dv2, NULL, 16) : 0; }
      if (sc > 0.0f && want2 && s_d3d.cur_texs[0].raw == want2 && count) {
        /* Magnify about a FIXED point captured from the first tracked draw, not
         * about each chunk's own centre -- recentring per chunk would stack every
         * chunk of the mesh on top of the others. Combined with VP_BYPASS (which
         * maps attribute 0 straight to clip space) this renders the mesh's real
         * geometry and texture at a readable size. */
        /* RSX_DBG_RECENTER=1: recentre EVERY chunk on its own centroid, overlaying
         * them. If each chunk is one instance of the same mesh they align into a
         * single silhouette; if they are arbitrary slices of one big mesh they
         * smear. Either way the answer is visible at a glance. */
        static int recen = -1;
        if (recen < 0) { const char* e = getenv("RSX_DBG_RECENTER"); recen = e ? atoi(e) : 0; }
        static int have_c = 0; static float ox = 0, oy = 0, oz = 0;
        if (!have_c || recen) {
            double sx = 0, sy = 0, sz = 0;
            for (u32 k = 0; k < count; k++) {
                sx += out[k*16].v[0]; sy += out[k*16].v[1]; sz += out[k*16].v[2];
            }
            ox = (float)(sx/count); oy = (float)(sy/count); oz = (float)(sz/count);
            have_c = 1;
        }
        for (u32 k = 0; k < count; k++) {
            /* RSX_DBG_SHIFT="dx,dy": nudge after scaling. VP_BYPASS applies the
             * guest's posoffset, so a recentred mesh does not land at screen
             * centre -- without this the magnified geometry sits clipped against
             * the top edge. */
            static int shf = -1; static float dx = 0.0f, dy = 0.0f;
            if (shf < 0) { const char* e = getenv("RSX_DBG_SHIFT");
                           if (e) { double a=0,b=0; sscanf(e, "%lf,%lf", &a, &b);
                                    dx = (float)a; dy = (float)b; }
                           shf = 1; }
            out[k*16].v[0] = (out[k*16].v[0] - ox) * sc + dx;
            out[k*16].v[1] = (out[k*16].v[1] - oy) * sc + dy;
            /* Leave z alone: under VP_BYPASS the object z maps straight to clip
             * z, so scaling it walks the geometry out of the depth range and
             * everything clips away. */
            (void)oz;
        }
      } }
    /* RSX_DBG_VTX=<hex tex0 offset>: attribute-0 positions for the draws binding
     * that texture (see upload_tris_vp). The duck's meshes are indexed, so this
     * is the copy that actually fires for it. */
    { static const char* dv = (const char*)1; static u32 want = 0; static int n = 0;
      if (dv == (const char*)1) { dv = getenv("RSX_DBG_VTX");
                                  want = dv ? (u32)strtoul(dv, NULL, 16) : 0; }
      if (want && s_d3d.cur_texs[0].raw == want && n < 8) { n++;
        float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
        for (u32 k = 0; k < count; k++) {
            float x=out[k*16].v[0], y=out[k*16].v[1], z=out[k*16].v[2];
            if (x<mnx)mnx=x; if (y<mny)mny=y; if (z<mnz)mnz=z;
            if (x>mxx)mxx=x; if (y>mxy)mxy=y; if (z>mxz)mxz=z;
        }
        const rsx_vertex_attrib* _a3 = &state->vertex_attribs[3];
        fprintf(stderr, "[DUCKVTX] a3(en=%d type=%u size=%u stride=%u off=0x%08X freq=%u)"
                        " -> col0 v0=(%.3f %.3f %.3f %.3f)%c",
                _a3->enabled, _a3->type, _a3->size, _a3->stride, _a3->offset,
                _a3->frequency,
                out[3].v[0], out[3].v[1], out[3].v[2], out[3].v[3], 10);
        const rsx_vertex_attrib* _a0 = &state->vertex_attribs[0];
        fprintf(stderr, "[DUCKVTX] first=%u count=%u a0(type=%u size=%u stride=%u off=0x%08X)"
                        " obj-bbox x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f]%c",
                first, count, _a0->type, _a0->size, _a0->stride, _a0->offset,
                mnx,mxx, mny,mxy, mnz,mxz, 10);
        /* Project the object-space bbox corners through the first four transform
         * constants (the conventional MVP rows) and report NDC. Sub-pixel output
         * with correct vertices means the placement transform is the problem, and
         * the NDC extent says by how much. */
        { float cx=(mnx+mxx)*0.5f, cy=(mny+mxy)*0.5f, cz=(mnz+mxz)*0.5f;
          /* This title's Cg programs keep their MVP at c[256..259], not c[0..3]
           * (see the vp_uses_c03 note); projecting through c0 reported zeros. */
          static int MVP = -1;
          if (MVP < 0) { const char* e = getenv("MVP_BASE"); MVP = e ? atoi(e) : 256; }
          float v[4] = { cx, cy, cz, 1.0f };
          float clip[4];
          for (int r = 0; r < 4; r++) {
              const float* m = state->vertex_constants[MVP + r];
              clip[r] = m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3];
          }
          float ex[4];
          { float e[4] = { mxx-cx, mxy-cy, mxz-cz, 0.0f };
            for (int r = 0; r < 4; r++) {
                const float* m = state->vertex_constants[MVP + r];
                ex[r] = m[0]*e[0] + m[1]*e[1] + m[2]*e[2];
            } }
          { int lo = -1, hi = -1, nz = 0;
            for (int r = 0; r < RSX_MAX_VERTEX_CONSTANTS; r++) {
                const float* m = state->vertex_constants[r];
                if (m[0] || m[1] || m[2] || m[3]) { if (lo < 0) lo = r; hi = r; nz++; }
            }
            fprintf(stderr, "[DUCKVTX]   constants: %d non-zero, slots %d..%d;", nz, lo, hi);
            for (int r = (lo < 0 ? 0 : lo); r < (lo < 0 ? 0 : lo) + 4 && r < RSX_MAX_VERTEX_CONSTANTS; r++)
                fprintf(stderr, " c%d=(%.3f %.3f %.3f %.3f)", r,
                        state->vertex_constants[r][0], state->vertex_constants[r][1],
                        state->vertex_constants[r][2], state->vertex_constants[r][3]);
            fprintf(stderr, "%c", 10); }
          fprintf(stderr, "[DUCKVTX]   mvp0=(%.3f %.3f %.3f %.3f) clip=(%.3f %.3f %.3f w=%.3f)",
                  state->vertex_constants[MVP][0], state->vertex_constants[MVP][1],
                  state->vertex_constants[MVP][2], state->vertex_constants[MVP][3],
                  clip[0], clip[1], clip[2], clip[3]);
          if (clip[3] > 1e-6f)
              fprintf(stderr, " ndc=(%.4f %.4f) half-extent-px=(%.2f %.2f)",
                      clip[0]/clip[3], clip[1]/clip[3],
                      ex[0]/clip[3]*640.0f, ex[1]/clip[3]*360.0f);
          fprintf(stderr, "%c", 10); }
      } }
    /* IDX_DBG=<N>: the first indices and the position they resolve to. An index
     * buffer read with the wrong element size or base yields huge indices and
     * degenerate triangles -- an INDEXED mesh vanishes while non-indexed
     * geometry in the same scene renders fine. */
    { static int cap = -1, n = 0;
      if (cap < 0) { const char* e = getenv("IDX_DBG"); cap = e ? atoi(e) : 0; }
      if (cap && n < cap && count >= 3) { n++;
        extern u32 cellGcmResolveOffset(u32);
        const rsx_vertex_attrib* a0 = &state->vertex_attribs[0];
        u32 vbase = cellGcmResolveOffset(a0->offset & 0x7FFFFFFFu);
        u32 nz = 0; for (u32 q = 0; q < 0x1000u; q += 29) if (vm_base[vbase + q]) nz++;
        fprintf(stderr, "[IDX] idxoff=0x%X idx: %u %u %u | a0 off=0x%08X(->0x%08X nz=%u/142)"
                        " stride=%u size=%u type=%u  pos0=(%g %g %g)%c",
                state->index_array_offset,
                read_guest_index(state, first), read_guest_index(state, first+1),
                read_guest_index(state, first+2),
                a0->offset, vbase, nz, a0->stride, a0->size, a0->type,
                out[0].v[0], out[0].v[1], out[0].v[2], 10); } }
    s_d3d.vp_vb_offset += count * VP_VERT_STRIDE;
    return count;
}

/* Indexed TRIANGLE_STRIP / TRIANGLE_FAN, expanded CPU-side into a triangle
 * list (the whole VP path draws lists; CullMode is NONE so strip parity
 * winding doesn't matter). DeferredShading's spline tube meshes -- the
 * octopus tentacles -- are indexed strips. */
/* The open strip segment at the end of the last indexed strip entry: where it
 * starts, how many indices it has so far, and where the next entry of the
 * same BEGIN/END would start. */
static u32 s_carry_epoch, s_carry_next = ~0u, s_carry_start, s_carry_len;
/* cont: this entry continues the previous one's strip (a joined run), so its
 * open segment is carried even without primitive restart. */
static u32 upload_strip_vp_indexed(const rsx_state* state, u32 first, u32 count, int fan, int cont)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    if (count < 3 && !cont) return 0;
    u32 tris = count > 2 ? count - 2 : 0;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (tris * 3 > maxv) tris = maxv / 3;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    /* Primitive restart: the sentinel ends the strip/fan and the next index
     * starts a new one. Without this the sentinel is fetched as a vertex and
     * every quad of a restart-separated sprite batch is joined to its
     * neighbours by long slivers (Code Veronica's title menu). A 16-bit index
     * buffer ignores a sentinel that does not fit (RPCS3 restart_index_enabled). */
    int idx16 = ((state->index_array_dma >> 4) & 0xF) == 1;
    int restart = state->restart_index_enable &&
                  !(idx16 && state->restart_index > 0xFFFFu);
    u32 sentinel = state->restart_index;
    /* One BEGIN/END is one strip even when it arrives as several
     * DRAW_INDEX_ARRAY entries (each holds at most 256 indices). A segment
     * split across two entries continues here: restart from the unfinished
     * segment's first index and skip the triangles the previous entry already
     * emitted. Without this a glyph quad straddling the boundary loses one of
     * its two triangles. The carried indices hold no sentinel, so the scan
     * resumes at this entry instead of reading them again: a long strip sent
     * as hundreds of entries (Code Veronica's flooded rooms) stays linear. */
    u32 base = first, skip = 0, k0 = 0;
    if ((restart || cont) && s_carry_epoch == state->begin_epoch && s_carry_next == first &&
        s_carry_len && first - s_carry_start == s_carry_len) {
        base = s_carry_start;
        skip = s_carry_len > 2 ? s_carry_len - 2 : 0;
        k0 = s_carry_len;
    }
    u32 total = first + count - base;
    if (total < 3) { s_carry_epoch = state->begin_epoch; s_carry_next = first + count;
                     s_carry_start = base; s_carry_len = total; return 0; }
    if (total - 2 > tris) tris = (total - 2) * 3 <= maxv ? total - 2 : maxv / 3;
    u32 seg = 0, seg_len = k0;  /* current segment's first index (rel. base), length */
    for (u32 k = k0; k <= total && o + 3 <= tris * 3; k++) {
        u32 idx = k < total ? read_guest_index(state, base + k) : 0;
        if (k < total && !(restart && idx == sentinel)) { seg_len++; continue; }
        /* Segment [seg, seg + seg_len) closed by a sentinel or the end. */
        for (u32 t = (seg == 0 ? skip : 0); t + 2 < seg_len && o + 3 <= tris * 3; t++) {
            u32 i0 = fan ? 0 : t, i1 = t + 1, i2 = t + 2;
            /* Strip parity: every other triangle is wound the other way and
             * the hardware flips it back. Guest face culling reaches the PSO
             * now, so unflipped odd triangles were culled. */
            if (!fan && (t & 1)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
            read_vp_vertex(state, read_guest_index(state, base + seg + i0), &out[o*16]); o++;
            read_vp_vertex(state, read_guest_index(state, base + seg + i1), &out[o*16]); o++;
            read_vp_vertex(state, read_guest_index(state, base + seg + i2), &out[o*16]); o++;
        }
        if (k == total) {   /* the entry ended inside this segment: remember it */
            s_carry_start = base + seg; s_carry_len = seg_len;
        }
        seg = k + 1; seg_len = 0;
    }
    s_carry_epoch = state->begin_epoch; s_carry_next = first + count;
    /* VTX_WATCH=1: one line per large strip run (Code Veronica's water):
     * attribute 0's format and the range of positions and indices emitted. */
    { static int on = -1;
      if (on < 0) { const char* e = getenv("VTX_WATCH"); on = e ? atoi(e) : 0; }
      static u32 ep = ~0u, nv = 0, imin, imax, lines = 0; static float mn[3], mx[3];
      if (on && lines < 40) {
          if (ep != state->begin_epoch) {
              if (nv >= 100000) { lines++;
                  const rsx_vertex_attrib* a = &state->vertex_attribs[0];
                  fprintf(stderr, "[VTXWATCH] %u verts idx %u..%u pos x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f a0 type=%u size=%u stride=%u%c",
                          nv, imin, imax, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], a->type, a->size, a->stride, 10); }
              ep = state->begin_epoch; nv = 0; imin = ~0u; imax = 0;
              for (int c = 0; c < 3; c++) { mn[c] = 1e30f; mx[c] = -1e30f; }
          }
          for (u32 k = 0; k < o; k++)
              for (int c = 0; c < 3; c++) { float v = out[k*16].v[c]; if (v < mn[c]) mn[c] = v; if (v > mx[c]) mx[c] = v; }
          for (u32 k = 0; k < count; k++) { u32 ix = read_guest_index(state, first + k);
              if (!(restart && ix == sentinel)) { if (ix < imin) imin = ix; if (ix > imax) imax = ix; } }
          nv += o;
      } }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* Non-indexed LINE_STRIP through the VP path, expanded to a line list. */
static u32 upload_linestrip_vp(const rsx_state* state, u32 first, u32 count)
{
    VP_FETCH_BEGIN();
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped || count < 2) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    u32 out_n = (count - 1) * 2;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (out_n > maxv) { s_drop_draws++; return 0; }
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    for (u32 k = 0; k + 1 < count; k++) {
        read_vp_vertex(state, first + k,     &out[(2*k)   * 16]);
        read_vp_vertex(state, first + k + 1, &out[(2*k+1) * 16]);
    }
    s_d3d.vp_vb_offset += out_n * VP_VERT_STRIDE;
    return out_n;
}

static void d3d12_draw_arrays_impl(void* ud, u32 primitive, u32 first, u32 count);
static void d3d12_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    ps3_tl_begin(9); d3d12_draw_arrays_impl(ud, primitive, first, count); vf_flush(); ps3_tl_end(9);
}
static void d3d12_draw_arrays_impl(void* ud, u32 primitive, u32 first, u32 count)
{
    /* RSX_LIVE_FEED_DBG=1: pair with the [feed] method counts so "the FIFO
     * carries no draw batches" can be checked against "the backend is told
     * to draw" in the same run. */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("RSX_LIVE_FEED_DBG") ? 1 : 0;
      if (dbg) { static long long n = 0;
          if (((++n) % 20000) == 0)
              printf("[backend] arrays callbacks=%lld (prim=%u count=%u)\n", n, primitive, count), fflush(stdout); } }
    (void)ud;
    /* Log the first 20 calls in detail, then every 200000th to show liveness:
     * every 1000th was ~7 lines a frame in heavy rooms, on the FIFO thread. */
    static u64 s_total = 0;
    if (s_total < 20 || (frame_profile_on() && (s_total % 200000) == 0)) {
        printf("[D3D12] draw_arrays #%llu prim=%u first=%u count=%u\n",
               (unsigned long long)s_total, primitive, first, count);
    }
    s_total++;

    if (!s_d3d.pipeline_ready || !s_d3d.vb_mapped) return;
    if (count == 0 || count > MAX_VERTICES) return;

    /* QUADS (prim 8) have no D3D12 topology; expand to a triangle list.
     * dbgfont draws all text as quads. Prefer the real vertex-program path
     * (exact position + texcoord): upload raw float4 attrib0 into vp_vb and
     * mark the draw is_vp; render_frame compiles the VP and draws it. Fall
     * back to the frac-approximation textured path if VP resources are absent. */
    if (primitive == RSX_PRIMITIVE_QUADS) {
        /* Prefer the real vertex-program path whenever VP resources exist --
         * NOT only when a texture is bound. Requiring tex_bound routed vkcube's
         * UNtextured cube to the fixed-function fallback, which never applies the
         * VP's MVP transform, so the cube was drawn in object space and clipped
         * off-screen. Untextured VP draws now render via the colour PSO. */
        if (s_d3d.vp_vb_mapped && s_d3d.vp_root_sig) {
            u32 rec = s_d3d.vp_vb_offset;
            ps3_tl_begin(8);
            u32 emitted = upload_quads_vp(s_d3d.current_rsx_state, first, count);
            ps3_tl_end(8);
            if (emitted && s_d3d.draw_count < MAX_DRAWS) {
                D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
                dr->vb_byte_offset = rec;
                dr->vertex_count   = emitted;
                dr->topology       = RSX_TOPOLOGY_TRIANGLES;
                dr->textured       = s_d3d.tex_bound;
                dr->is_vp          = 1;
                dr->fp_addr = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->shader_program : 0;
                fp_snapshot(dr);
                dr->fp_exp32 = s_d3d.current_rsx_state ?
                    ((s_d3d.current_rsx_state->shader_control & 0x40) != 0) : 1;
                dr->cull = rsx_cull_key(s_d3d.current_rsx_state);
                if (s_d3d.current_rsx_state && s_d3d.current_rsx_state->shader_packer && !getenv("NO_SRGB_OUT")) dr->cull |= 0x200u;
                dr->cull |= rsx_depth_key(s_d3d.current_rsx_state);
                dr->cull |= rsx_alpha_key(s_d3d.current_rsx_state);
        dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_epoch : 0;
            dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_epoch : 0;
                dr->cmask = 0xF;
                if (s_d3d.current_rsx_state) {
                    u32 _cm = s_d3d.current_rsx_state->color_mask;
                    dr->cmask = ((_cm & 0x00010000) ? 1u : 0u)   /* R */
                              | ((_cm & 0x00000100) ? 2u : 0u)   /* G */
                              | ((_cm & 0x00000001) ? 4u : 0u)   /* B */
                              | ((_cm & 0x01000000) ? 8u : 0u);  /* A */
                }
                dr->alpha_ctl = 0;
                if (s_d3d.current_rsx_state)
                    dr->alpha_ctl = ((s_d3d.current_rsx_state->alpha_test_enable ? 1u : 0u) << 16)
                                  | ((s_d3d.current_rsx_state->alpha_func & 0xFFu) << 8)
                                  | (s_d3d.current_rsx_state->alpha_ref & 0xFFu);
                for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
                    dr->tex[_u].off = s_d3d.cur_texs[_u].off;
                    dr->tex[_u].raw = s_d3d.cur_texs[_u].raw;
                    dr->tex[_u].w   = s_d3d.cur_texs[_u].w;
                    dr->tex[_u].h   = s_d3d.cur_texs[_u].h;
                    dr->tex[_u].fmt = s_d3d.cur_texs[_u].fmt;
                    dr->tex[_u].ctrl1 = s_d3d.cur_texs[_u].ctrl1; dr->tex[_u].addr = s_d3d.cur_texs[_u].addr;
                    dr->tex[_u].cube  = s_d3d.cur_texs[_u].cube;
                    dr->tex[_u].mips  = s_d3d.cur_texs[_u].mips;
                    dr->tex[_u].set = s_d3d.cur_texs[_u].set;
                    dr->tex_rt[_u]  = -1;
                }
                dr->tex_slot = -1;
                dr->vs_idx = vp_get_vs(s_d3d.current_rsx_state);
                vtex_record(dr, s_d3d.current_rsx_state);
                dr->is_clear = 0; dr->is_copy = 0;
                dr->blend = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->blend_enable : 1;
                dr->blend_key = rsx_blend_key(s_d3d.current_rsx_state, dr->blend);
                if (getenv("BLENDDBG")) { static u32 seen[32]; static int ns=0;
                    u32 k = (dr->blend ? 0x80000000u : 0u) | dr->blend_key;
                    int f=0; for (int i=0;i<ns;i++) if (seen[i]==k) f=1;
                    if (!f && ns<32) { seen[ns++]=k;
                        fprintf(stderr, "[BLEND] enable=%d key=0x%X%c",
                                dr->blend, dr->blend_key, 10); } }
                dr->rt_off = current_rt_off(&dr->rt_w, &dr->rt_h, dr->rt_mrt);
            if (s_d3d.current_rsx_state) { const rsx_state* _ss = s_d3d.current_rsx_state;
                dr->s_tgt = _ss->color_target; dr->s_offa = _ss->surface_color_offset[0];
                dr->s_offb = _ss->surface_color_offset[1];
                dr->s_clipw = _ss->surface_clip_w; dr->s_cliph = _ss->surface_clip_h; dr->s_pitch = _ss->surface_color_pitch[0]; }
                dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0;
                if (s_d3d.current_rsx_state) { dr->zeta_off = s_d3d.current_rsx_state->surface_zeta_offset; dr->dtest = s_d3d.current_rsx_state->depth_test_enable; dr->dfunc = s_d3d.current_rsx_state->depth_func; }
                if (s_d3d.current_rsx_state) {
                    dr->vp_x = s_d3d.current_rsx_state->viewport_x;
                    dr->vp_y = s_d3d.current_rsx_state->viewport_y;
                    dr->vp_w = s_d3d.current_rsx_state->viewport_w;
                    dr->vp_h = s_d3d.current_rsx_state->viewport_h;
                } else { dr->vp_x = dr->vp_y = dr->vp_w = dr->vp_h = 0; }
                dr->cb_slot = s_d3d.draw_count;
                vp_record_cb(s_d3d.draw_count, dr->vs_idx, dr);
                s_d3d.draw_count++;
            }
            return;
        }
        u32 record_offset = s_d3d.vb_offset;
        u32 emitted = upload_quads_from_rsx(first, count);
        if (emitted == 0) return;
        if (s_d3d.draw_count < MAX_DRAWS) {
            s_d3d.draws[s_d3d.draw_count].vb_byte_offset = record_offset;
            s_d3d.draws[s_d3d.draw_count].vertex_count   = emitted;
            s_d3d.draws[s_d3d.draw_count].topology       = RSX_TOPOLOGY_TRIANGLES;
            s_d3d.draws[s_d3d.draw_count].textured       = s_d3d.tex_bound;
            s_d3d.draws[s_d3d.draw_count].is_vp          = 0;
            s_d3d.draws[s_d3d.draw_count].is_clear       = 0;
        s_d3d.draws[s_d3d.draw_count].is_copy = 0;
            s_d3d.draws[s_d3d.draw_count].rt_off         = 0;
            s_d3d.draw_count++;
        }
        return;
    }

    /* TRIANGLES (prim 5): same VP-path preference as quads -- the guest's
     * vertex program does the MVP transform (gcm/cube draws its cube this way);
     * the fixed-function fallback below applies no transform, so 3D geometry
     * ends up in object space (invisible/garbage). */
    /* Route TRIANGLES/STRIP/FAN through the VP path (sagemono: LBP draws
     * non-indexed strips/fans), but ONLY when the guest ACTUALLY HAS a vertex
     * program (flOw). The VP path transforms via the guest's VP microcode; with
     * no microcode loaded it transforms nothing and the draw silently produces
     * zero pixels (it also uploads to vp_vb, leaving the fixed-function vb empty).
     * Geometry already in clip space with no VP -- flOw's injected scene -- must
     * go down the fixed-function passthrough below instead. */
    /* LINES / LINE_STRIP too: Code Veronica draws its colour-grading lookup
     * table as three lines into a 1024x1 surface. On the fixed-function
     * fallback they went to the backbuffer untransformed, the table stayed
     * empty, and grading turned every pixel black. */
    const int is_line = (primitive == RSX_PRIMITIVE_LINES || primitive == RSX_PRIMITIVE_LINE_STRIP);
    if ((primitive == RSX_PRIMITIVE_TRIANGLES || primitive == RSX_PRIMITIVE_TRIANGLE_STRIP
         || primitive == RSX_PRIMITIVE_TRIANGLE_FAN || is_line) &&
        s_d3d.vp_vb_mapped && s_d3d.vp_root_sig &&
        s_d3d.current_rsx_state && s_d3d.current_rsx_state->vp_ucode_bytes >= 16) {
        /* A strip or fan continued by the next DRAW_ARRAYS entry of the same
         * BEGIN/END is ONE primitive on RSX. Expanding each entry on its own
         * lost the triangles that join them -- Code Veronica sends its
         * inventory icon quads as a 3-vertex strip plus one more vertex, and
         * only the first triangle of each icon was drawn (the diagonal cut).
         * Re-expand the whole run in place of the previous record. */
        if ((primitive == RSX_PRIMITIVE_TRIANGLE_STRIP || primitive == RSX_PRIMITIVE_TRIANGLE_FAN) &&
            s_d3d.merge_prev_draw == (int)primitive && s_d3d.draw_count > 0 &&
            s_d3d.merge_first_end == first) {
            D3D12DrawRecord* pv = &s_d3d.draws[s_d3d.draw_count - 1];
            u32 fpnow = s_d3d.current_rsx_state->shader_program;
            if (pv->is_vp && pv->fp_addr == fpnow &&
                pv->begin_epoch == s_d3d.current_rsx_state->begin_epoch &&
                pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE == s_d3d.vp_vb_offset) {
                u32 total = first + count - s_d3d.merge_strip_first;
                u32 done = first - s_d3d.merge_strip_first;   /* indices already expanded */
                ps3_tl_begin(8);
                if (done >= 3 && pv->vertex_count == (done - 2) * 3) {
                    /* Append only the new triangles: re-expanding the whole run
                     * for every entry was quadratic in its length. */
                    pv->vertex_count += upload_strip_vp_from(s_d3d.current_rsx_state, s_d3d.merge_strip_first,
                                                             total, primitive == RSX_PRIMITIVE_TRIANGLE_FAN, done - 2);
                } else {
                    s_d3d.vp_vb_offset = pv->vb_byte_offset;   /* drop the partial expansion */
                    pv->vertex_count = upload_strip_vp(s_d3d.current_rsx_state, s_d3d.merge_strip_first,
                                                       total, primitive == RSX_PRIMITIVE_TRIANGLE_FAN);
                }
                ps3_tl_end(8);
                s_d3d.merge_first_end = first + count;
                { static int n = 0; if (n++ < 4)
                    fprintf(stderr, "[STRIPJOIN] prim=%u joined %u vertices from %u%c",
                            primitive, total, s_d3d.merge_strip_first, 10); }
                return;
            }
        }
        u32 rec = s_d3d.vp_vb_offset;
        ps3_tl_begin(8);
        u32 emitted = (primitive == RSX_PRIMITIVE_LINES) ? upload_tris_vp(s_d3d.current_rsx_state, first, count & ~1u)
            : (primitive == RSX_PRIMITIVE_LINE_STRIP) ? upload_linestrip_vp(s_d3d.current_rsx_state, first, count)
            : (primitive == RSX_PRIMITIVE_TRIANGLES)
            ? upload_tris_vp(s_d3d.current_rsx_state, first, count)
            : upload_strip_vp(s_d3d.current_rsx_state, first, count, primitive == RSX_PRIMITIVE_TRIANGLE_FAN);
        ps3_tl_end(8);
        /* RSX splits one primitive stream across several DRAW_ARRAYS entries
         * inside a single BEGIN/END (each carries at most 256 vertices), and the
         * hardware concatenates them before assembling primitives. Recording a
         * draw per batch instead regroups the triangles: this title's tub mesh
         * arrives as 128 then 256,256,..., neither a multiple of 3, so every
         * batch after the first was assembled one vertex out of phase -- which
         * rendered as spike triangles with a zero texcoord along the tub rim.
         * The batches land contiguously in the vertex buffer, so extending the
         * previous record is all that is needed to restore the stream. */
        if (emitted && s_d3d.merge_prev_draw == 1 && s_d3d.draw_count > 0 &&
            s_d3d.merge_first_end == first) {
            D3D12DrawRecord* pv = &s_d3d.draws[s_d3d.draw_count - 1];
            u32 fpnow = s_d3d.current_rsx_state
                        ? s_d3d.current_rsx_state->shader_program : 0;
            if (pv->is_vp && pv->topology == RSX_TOPOLOGY_TRIANGLES &&
                pv->fp_addr == fpnow && primitive == RSX_PRIMITIVE_TRIANGLES &&
                pv->begin_epoch == (s_d3d.current_rsx_state
                                    ? s_d3d.current_rsx_state->begin_epoch : 0) &&
                pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE == rec) {
                pv->vertex_count += emitted;
                s_d3d.merge_first_end = first + count;
                return;
            }
        }
        if (emitted && s_d3d.draw_count < MAX_DRAWS) {
            D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
            dr->vb_byte_offset = rec;
            dr->vertex_count   = emitted;
            dr->topology       = is_line ? RSX_TOPOLOGY_LINES : RSX_TOPOLOGY_TRIANGLES;
            dr->textured       = s_d3d.tex_bound;
            dr->is_vp          = 1;
            dr->fp_addr = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->shader_program : 0;
            fp_snapshot(dr);
            dr->fp_exp32 = s_d3d.current_rsx_state ?
                ((s_d3d.current_rsx_state->shader_control & 0x40) != 0) : 1;
            dr->cull = rsx_cull_key(s_d3d.current_rsx_state);
            if (s_d3d.current_rsx_state && s_d3d.current_rsx_state->shader_packer && !getenv("NO_SRGB_OUT")) dr->cull |= 0x200u;
            dr->cull |= rsx_depth_key(s_d3d.current_rsx_state);
            dr->cull |= rsx_alpha_key(s_d3d.current_rsx_state);
        dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_epoch : 0;
            dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_epoch : 0;
            dr->cmask = 0xF;
            if (s_d3d.current_rsx_state) {
                u32 _cm = s_d3d.current_rsx_state->color_mask;
                dr->cmask = ((_cm & 0x00010000) ? 1u : 0u)   /* R */
                          | ((_cm & 0x00000100) ? 2u : 0u)   /* G */
                          | ((_cm & 0x00000001) ? 4u : 0u)   /* B */
                          | ((_cm & 0x01000000) ? 8u : 0u);  /* A */
            }
            dr->alpha_ctl = 0;
            if (s_d3d.current_rsx_state)
                dr->alpha_ctl = ((s_d3d.current_rsx_state->alpha_test_enable ? 1u : 0u) << 16)
                              | ((s_d3d.current_rsx_state->alpha_func & 0xFFu) << 8)
                              | (s_d3d.current_rsx_state->alpha_ref & 0xFFu);
            for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
                dr->tex[_u].off = s_d3d.cur_texs[_u].off;
                dr->tex[_u].raw = s_d3d.cur_texs[_u].raw;
                dr->tex[_u].w   = s_d3d.cur_texs[_u].w;
                dr->tex[_u].h   = s_d3d.cur_texs[_u].h;
                dr->tex[_u].fmt = s_d3d.cur_texs[_u].fmt;
                    dr->tex[_u].ctrl1 = s_d3d.cur_texs[_u].ctrl1; dr->tex[_u].addr = s_d3d.cur_texs[_u].addr;
                    dr->tex[_u].cube  = s_d3d.cur_texs[_u].cube;
                    dr->tex[_u].mips  = s_d3d.cur_texs[_u].mips;
                dr->tex[_u].set = s_d3d.cur_texs[_u].set;
                dr->tex_rt[_u]  = -1;
            }
            dr->tex_slot = -1;
            dr->vs_idx = vp_get_vs(s_d3d.current_rsx_state);
            vtex_record(dr, s_d3d.current_rsx_state);
            dr->is_clear = 0; dr->is_copy = 0;
            dr->blend = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->blend_enable : 1;
            dr->blend_key = rsx_blend_key(s_d3d.current_rsx_state, dr->blend);
            dr->rt_off = current_rt_off(&dr->rt_w, &dr->rt_h, dr->rt_mrt);
            if (s_d3d.current_rsx_state) { const rsx_state* _ss = s_d3d.current_rsx_state;
                dr->s_tgt = _ss->color_target; dr->s_offa = _ss->surface_color_offset[0];
                dr->s_offb = _ss->surface_color_offset[1];
                dr->s_clipw = _ss->surface_clip_w; dr->s_cliph = _ss->surface_clip_h; dr->s_pitch = _ss->surface_color_pitch[0]; }
            dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0;
            if (s_d3d.current_rsx_state) { dr->zeta_off = s_d3d.current_rsx_state->surface_zeta_offset; dr->dtest = s_d3d.current_rsx_state->depth_test_enable; dr->dfunc = s_d3d.current_rsx_state->depth_func; }
            if (s_d3d.current_rsx_state) {
                dr->vp_x = s_d3d.current_rsx_state->viewport_x;
                dr->vp_y = s_d3d.current_rsx_state->viewport_y;
                dr->vp_w = s_d3d.current_rsx_state->viewport_w;
                dr->vp_h = s_d3d.current_rsx_state->viewport_h;
            } else { dr->vp_x = dr->vp_y = dr->vp_w = dr->vp_h = 0; }
            dr->cb_slot = s_d3d.draw_count;
                vp_record_cb(s_d3d.draw_count, dr->vs_idx, dr);
            s_d3d.draw_count++;
            /* Anchor for merging the next DRAW_ARRAYS batch of this stream. */
            s_d3d.merge_prev_draw  = (primitive == RSX_PRIMITIVE_TRIANGLES) ? 1
                                   : (primitive == RSX_PRIMITIVE_TRIANGLE_STRIP ||
                                      primitive == RSX_PRIMITIVE_TRIANGLE_FAN) ? (int)primitive : 0;
            s_d3d.merge_first_end  = first + count;
            s_d3d.merge_strip_first = first;
        }
        return;
    }
    s_d3d.merge_prev_draw = 0;   /* any other primitive breaks the stream */

    u32 topo = (u32)rsx_primitive_topology(primitive);
    if (topo == RSX_TOPOLOGY_UNSUPPORTED) {
        /* Other primitives still needing index-buffer conversion
         * (line loops, triangle fans) â€” skip rather than draw wrong. */
        static int s_skipped_nontri = 0;
        if (s_skipped_nontri < 3) {
            printf("[D3D12] draw_arrays: skipping prim=%u (needs index conversion)\n",
                   primitive);
            s_skipped_nontri++;
        }
        return;
    }

    u32 record_offset = s_d3d.vb_offset;
    u32 actual_count  = upload_vertices_from_rsx(first, count);
    if (actual_count == 0) return;

    if (s_d3d.draw_count < MAX_DRAWS) {
        s_d3d.draws[s_d3d.draw_count].vb_byte_offset = record_offset;
        s_d3d.draws[s_d3d.draw_count].vertex_count   = actual_count;
        s_d3d.draws[s_d3d.draw_count].topology       = topo;
        s_d3d.draws[s_d3d.draw_count].textured       = 0;
        s_d3d.draws[s_d3d.draw_count].is_vp          = 0;
        s_d3d.draws[s_d3d.draw_count].is_clear       = 0;
        s_d3d.draws[s_d3d.draw_count].is_copy = 0;
        s_d3d.draws[s_d3d.draw_count].rt_off         = 0;
        s_d3d.draw_count++;
    }
}

static void d3d12_draw_indexed_impl(void* ud, u32 primitive, u32 first, u32 count);
static void d3d12_draw_indexed(void* ud, u32 primitive, u32 first, u32 count)
{
    ps3_tl_begin(9); d3d12_draw_indexed_impl(ud, primitive, first, count); vf_flush(); ps3_tl_end(9);
}
static void d3d12_draw_indexed_impl(void* ud, u32 primitive, u32 first, u32 count)
{
    /* RSX_LIVE_FEED_DBG=1: pair with the [feed] method counts so "the FIFO
     * carries no draw batches" can be checked against "the backend is told
     * to draw" in the same run. */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("RSX_LIVE_FEED_DBG") ? 1 : 0;
      if (dbg) { static long long n = 0;
          if (((++n) % 20000) == 0)
              printf("[backend] indexed callbacks=%lld (prim=%u count=%u)\n", n, primitive, count), fflush(stdout); } }
    (void)ud;
    static int log_count = 0;
    if (log_count < 8) {
        printf("[D3D12] draw_indexed(prim=%u, first=%u, count=%u)\n",
               primitive, first, count);
        log_count++;
    }
    if (!s_d3d.pipeline_ready) return;
    if (count == 0 || count > MAX_VERTICES) return;
    if (!s_d3d.vp_vb_mapped || !s_d3d.vp_root_sig) return;

    /* Expand through the VP path (indices resolved CPU-side): QUADS -> two
     * triangles per quad, TRIANGLES straight through, STRIP/FAN -> triangle
     * list. Other primitives are skipped rather than drawn wrong. */
    /* An indexed strip/fan continued by the next DRAW_INDEX_ARRAY entry of the
     * same BEGIN/END is one primitive. Code Veronica sends its inventory icon
     * quads as 3 indices + 1 index; expanded per entry, the lone index was
     * dropped and each icon lost its second triangle (the diagonal cut), and
     * longer split strips lost the triangles joining the entries. Re-expand
     * the whole run in place of the previous record. */
    /* The run may also START with an entry too short to make a triangle (1 or
     * 2 indices -- the File tab's frame edge arrives as 1 + 3): nothing was
     * recorded for it, so prepend it to this entry instead. */
    if ((primitive == RSX_PRIMITIVE_TRIANGLE_STRIP || primitive == RSX_PRIMITIVE_TRIANGLE_FAN) &&
        s_d3d.current_rsx_state && s_d3d.merge_prev_draw == (int)primitive + 0x200 &&
        s_d3d.merge_first_end == first &&
        s_d3d.merge_pending_epoch == s_d3d.current_rsx_state->begin_epoch) {
        count = first + count - s_d3d.merge_strip_first;
        first = s_d3d.merge_strip_first;
        s_d3d.merge_prev_draw = 0;
    }
    if ((primitive == RSX_PRIMITIVE_TRIANGLE_STRIP || primitive == RSX_PRIMITIVE_TRIANGLE_FAN) &&
        s_d3d.current_rsx_state && s_d3d.merge_prev_draw == (int)primitive + 0x100 &&
        s_d3d.draw_count > 0 && s_d3d.merge_first_end == first) {
        D3D12DrawRecord* pv = &s_d3d.draws[s_d3d.draw_count - 1];
        if (pv->is_vp && pv->fp_addr == s_d3d.current_rsx_state->shader_program &&
            pv->begin_epoch == s_d3d.current_rsx_state->begin_epoch &&
            pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE == s_d3d.vp_vb_offset) {
            u32 total = first + count - s_d3d.merge_strip_first;
            ps3_tl_begin(8);
            if (s_carry_epoch == s_d3d.current_rsx_state->begin_epoch && s_carry_next == first) {
                /* Append only this entry's triangles: re-expanding the whole
                 * run for every entry was quadratic in its length. */
                pv->vertex_count += upload_strip_vp_indexed(s_d3d.current_rsx_state, first, count,
                                                            primitive == RSX_PRIMITIVE_TRIANGLE_FAN, 1);
            } else {
                s_d3d.vp_vb_offset = pv->vb_byte_offset;   /* drop the partial expansion */
                u32 n = upload_strip_vp_indexed(s_d3d.current_rsx_state, s_d3d.merge_strip_first, total,
                                                primitive == RSX_PRIMITIVE_TRIANGLE_FAN, 0);
                if (n) pv->vertex_count = n;
                else   s_d3d.vp_vb_offset = pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE;
            }
            ps3_tl_end(8);
            s_d3d.merge_first_end = first + count;
            { static int k = 0; if (k++ < 4)
                fprintf(stderr, "[STRIPJOIN] indexed prim=%u joined %u indices from %u -> %u vertices%c",
                        primitive, total, s_d3d.merge_strip_first, pv->vertex_count, 10); }
            return;
        }
    }
    u32 emitted = 0;
    u32 rec = s_d3d.vp_vb_offset;
    ps3_tl_begin(8);
    if (primitive == RSX_PRIMITIVE_QUADS)             emitted = upload_quads_vp_indexed(s_d3d.current_rsx_state, first, count);
    else if (primitive == RSX_PRIMITIVE_TRIANGLES)    emitted = upload_tris_vp_indexed(s_d3d.current_rsx_state, first, count);
    else if (primitive == RSX_PRIMITIVE_TRIANGLE_STRIP) emitted = upload_strip_vp_indexed(s_d3d.current_rsx_state, first, count, 0, 0);
    else if (primitive == RSX_PRIMITIVE_TRIANGLE_FAN)   emitted = upload_strip_vp_indexed(s_d3d.current_rsx_state, first, count, 1, 0);
    else {
        ps3_tl_end(8);
        static int _skip = 0;
        if (_skip++ < 3)
            printf("[D3D12] draw_indexed: skipping prim=%u (not wired)\n", primitive);
        return;
    }
    ps3_tl_end(8);
    /* Same BEGIN/END batch concatenation as the non-indexed path: this title's
     * fluid arrives as ~3000 DRAW_INDEX_ARRAY batches of 256, and 256 is not a
     * multiple of 3, so every batch after the first assembled out of phase. */
    if (emitted && s_d3d.merge_prev_draw == 1 && s_d3d.draw_count > 0 &&
        s_d3d.merge_first_end == first && primitive == RSX_PRIMITIVE_TRIANGLES) {
        D3D12DrawRecord* pv = &s_d3d.draws[s_d3d.draw_count - 1];
        u32 fpnow = s_d3d.current_rsx_state
                    ? s_d3d.current_rsx_state->shader_program : 0;
        if (pv->is_vp && pv->topology == RSX_TOPOLOGY_TRIANGLES &&
            pv->fp_addr == fpnow &&
            pv->begin_epoch == (s_d3d.current_rsx_state
                                ? s_d3d.current_rsx_state->begin_epoch : 0) &&
            pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE == rec) {
            pv->vertex_count += emitted;
            s_d3d.merge_first_end = first + count;
            return;
        }
    }
    if (emitted && s_d3d.draw_count < MAX_DRAWS) {
        D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
        dr->vb_byte_offset = rec;
        dr->vertex_count   = emitted;
        dr->topology       = RSX_TOPOLOGY_TRIANGLES;
        dr->textured       = s_d3d.tex_bound;
        dr->is_vp          = 1;
        dr->fp_addr = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->shader_program : 0;
        fp_snapshot(dr);
        dr->fp_exp32 = s_d3d.current_rsx_state ?
            ((s_d3d.current_rsx_state->shader_control & 0x40) != 0) : 1;
        dr->cull = rsx_cull_key(s_d3d.current_rsx_state);
        if (s_d3d.current_rsx_state && s_d3d.current_rsx_state->shader_packer && !getenv("NO_SRGB_OUT")) dr->cull |= 0x200u;
        dr->cull |= rsx_depth_key(s_d3d.current_rsx_state);
        dr->cull |= rsx_alpha_key(s_d3d.current_rsx_state);
        dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_epoch : 0;
        dr->cmask = 0xF;
        if (s_d3d.current_rsx_state) {
            u32 _cm = s_d3d.current_rsx_state->color_mask;
            dr->cmask = ((_cm & 0x00010000) ? 1u : 0u)   /* R */
                      | ((_cm & 0x00000100) ? 2u : 0u)   /* G */
                      | ((_cm & 0x00000001) ? 4u : 0u)   /* B */
                      | ((_cm & 0x01000000) ? 8u : 0u);  /* A */
        }
        dr->alpha_ctl = 0;
        if (s_d3d.current_rsx_state)
            dr->alpha_ctl = ((s_d3d.current_rsx_state->alpha_test_enable ? 1u : 0u) << 16)
                          | ((s_d3d.current_rsx_state->alpha_func & 0xFFu) << 8)
                          | (s_d3d.current_rsx_state->alpha_ref & 0xFFu);
        for (int _u = 0; _u < FP_TEX_UNITS; _u++) {
            dr->tex[_u].off = s_d3d.cur_texs[_u].off;
            dr->tex[_u].raw = s_d3d.cur_texs[_u].raw;
            dr->tex[_u].w   = s_d3d.cur_texs[_u].w;
            dr->tex[_u].h   = s_d3d.cur_texs[_u].h;
            dr->tex[_u].fmt = s_d3d.cur_texs[_u].fmt;
                    dr->tex[_u].ctrl1 = s_d3d.cur_texs[_u].ctrl1; dr->tex[_u].addr = s_d3d.cur_texs[_u].addr;
                    dr->tex[_u].cube  = s_d3d.cur_texs[_u].cube;
                    dr->tex[_u].mips  = s_d3d.cur_texs[_u].mips;
            dr->tex[_u].set = s_d3d.cur_texs[_u].set;
            dr->tex_rt[_u]  = -1;
        }
        dr->tex_slot = -1;
        dr->vs_idx = vp_get_vs(s_d3d.current_rsx_state);
        vtex_record(dr, s_d3d.current_rsx_state);
        dr->is_clear = 0; dr->is_copy = 0;
        dr->blend = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->blend_enable : 1;
        dr->blend_key = rsx_blend_key(s_d3d.current_rsx_state, dr->blend);
        dr->rt_off = current_rt_off(&dr->rt_w, &dr->rt_h, dr->rt_mrt);
            if (s_d3d.current_rsx_state) { const rsx_state* _ss = s_d3d.current_rsx_state;
                dr->s_tgt = _ss->color_target; dr->s_offa = _ss->surface_color_offset[0];
                dr->s_offb = _ss->surface_color_offset[1];
                dr->s_clipw = _ss->surface_clip_w; dr->s_cliph = _ss->surface_clip_h; dr->s_pitch = _ss->surface_color_pitch[0]; }
        dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0;
        if (s_d3d.current_rsx_state) { dr->zeta_off = s_d3d.current_rsx_state->surface_zeta_offset; dr->dtest = s_d3d.current_rsx_state->depth_test_enable; dr->dfunc = s_d3d.current_rsx_state->depth_func; }
        if (s_d3d.current_rsx_state) {
            dr->vp_x = s_d3d.current_rsx_state->viewport_x;
            dr->vp_y = s_d3d.current_rsx_state->viewport_y;
            dr->vp_w = s_d3d.current_rsx_state->viewport_w;
            dr->vp_h = s_d3d.current_rsx_state->viewport_h;
        } else { dr->vp_x = dr->vp_y = dr->vp_w = dr->vp_h = 0; }
        dr->cb_slot = s_d3d.draw_count;
                vp_record_cb(s_d3d.draw_count, dr->vs_idx, dr);
        s_d3d.draw_count++;
        /* Anchor for merging the next batch of this indexed stream. */
        s_d3d.merge_prev_draw = (primitive == RSX_PRIMITIVE_TRIANGLES) ? 1
                              : (primitive == RSX_PRIMITIVE_TRIANGLE_STRIP ||
                                 primitive == RSX_PRIMITIVE_TRIANGLE_FAN) ? (int)primitive + 0x100 : 0;
        s_d3d.merge_first_end = first + count;
        s_d3d.merge_strip_first = first;
        return;
    }
    /* A strip/fan head with too few indices for a triangle: keep it pending
     * for the entry that continues it (see the top of this function). */
    if (!emitted && count < 3 && s_d3d.current_rsx_state &&
        (primitive == RSX_PRIMITIVE_TRIANGLE_STRIP || primitive == RSX_PRIMITIVE_TRIANGLE_FAN)) {
        s_d3d.merge_prev_draw = (int)primitive + 0x200;
        s_d3d.merge_strip_first = first;
        s_d3d.merge_first_end = first + count;
        s_d3d.merge_pending_epoch = s_d3d.current_rsx_state->begin_epoch;
    }
}

static void d3d12_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex)
{
    (void)ud;
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);

    u32 width  = (tex->image_rect >> 16) & 0xFFFF;
    u32 height = tex->image_rect & 0xFFFF;
    u32 format = (tex->format >> 8) & 0xFF;
    u32 offset = tex->offset;
    /* CUBEDBG=1: NV4097_SET_TEXTURE_FORMAT bit 2 is the cubemap flag, and the
     * line above throws it away with the rest of the low byte. Nothing in this
     * backend handles cube textures, so a cubemap bound for an environment
     * reflection is sampled as a plain 2D image -- which is what the chrome
     * faucet's black patches and banded escutcheons look like. */
    { static int cd = -1;
      if (cd < 0) { const char* e = getenv("CUBEDBG"); cd = e ? atoi(e) : 0; }
      if (cd && (tex->format & 4)) {
          static u32 seen[8]; static int ns = 0; int known = 0;
          for (int k = 0; k < ns; k++) if (seen[k] == offset) known = 1;
          if (!known && ns < 8) { seen[ns++] = offset;
              fprintf(stderr, "[CUBE] unit=%u offset=0x%08X %ux%u fmt=0x%02X"
                              " raw_format=0x%08X (cubemap bit SET)%c",
                      unit, offset, width, height, format, tex->format, 10); }
      } }

    static int log_count = 0;
    if (log_count < 10) {
        printf("[D3D12] bind_texture(unit=%u, offset=0x%X, fmt=0x%02X, %ux%u)\n",
               unit, offset, format, width, height);
        log_count++;
    }
    /* MOVIE_BIND=1: trace movie-plane binds (640x360 Y / 320x180 U/V) with the
     * resolved EA + a content probe -- used to diagnose the Bink frame-buffer
     * ring mismatch (the draw binds a cleared buffer 0x4D80 before the one the
     * decoder actually fills). */
    if (getenv("MOVIE_BIND") &&
        ((width == 640 && height == 360) || (width == 320 && height == 180))) {
        extern u32 cellGcmResolveLocated(int local, u32 offset);
        static int _mv = 0; if (_mv++ < 24) {
            /* Resolve the SAME raw offset both ways -- as LOCAL (VRAM) and as MAIN
             * (IO-mapped) -- and count nonzero bytes over the whole plane for each.
             * If the plane the game declares (loc bits fmt&3) is zero but the OTHER
             * pool has content, the decoder wrote to a different memory space than
             * our texture resolve picked (the local-vs-main mismatch). */
            u32 loc = tex->format & 3;               /* 1=LOCAL, 2=MAIN */
            u32 ea_loc  = cellGcmResolveLocated(1, offset);
            u32 ea_main = cellGcmResolveLocated(0, offset);
            u32 plane = width * height;
            u32 nz_loc = 0, nz_main = 0;
            if (vm_base) for (u32 i = 0; i < plane; i += 137) {
                if (vm_base[ea_loc  + i]) nz_loc++;
                if (vm_base[ea_main + i]) nz_main++;
            }
            fprintf(stderr, "[mv-bind] unit=%u fmt=0x%02X loc=%u %ux%u off=0x%X | LOCAL ea=0x%08X nz=%u | MAIN ea=0x%08X nz=%u\n",
                    unit, format, loc, width, height, offset,
                    ea_loc, nz_loc, ea_main, nz_main);
        }
    }

    if (!vm_base || width == 0 || height == 0) return;

    /* Record the currently-bound atlas so subsequent quad draws sample it. The
     * actual GPU upload happens in render_frame (we have no open command list
     * here). Only the 8-bit single-channel font atlas (B8, RSX fmt base 0x81 /
     * as-seen 0xA1 with the LN flag) is wired up so far; other formats fall
     * back to untextured. */
    u32 base_fmt = format & 0x9F;   /* strip LN(0x20)/UN(0x40) flags */
    /* VP path: record the latest bound texture (any supported format) so draws
     * can carry it per-draw. Location bits (format[1:0]): 1 = LOCAL, 2 = MAIN. */
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    if (unit < FP_TEX_UNITS &&
        (base_fmt == 0x81 /* B8 */ || base_fmt == 0x85 /* A8R8G8B8 */ ||
         base_fmt == 0x8B /* G8B8: LBP's font atlas */ ||
         (base_fmt >= 0x86 && base_fmt <= 0x88) /* DXT1/23/45 */ ||
         /* X32_FLOAT is also a fragment texture: Code Veronica samples its
          * converted scene depth for water thickness/transparency. Ignoring
          * this bind kept an unrelated old 1x1 texture on the unit, making
          * the water pass transparent even though its mesh rasterized. */
         base_fmt == 0x9C /* X32_FLOAT: converted depth / float RTT */ ||
         base_fmt == 0x9A /* W16Z16Y16X16 half-float: RTT intermediates */ ||
         base_fmt == 0x95 /* Y16_X16: an A8R8G8B8 target re-read as 16-bit pairs */)) {
        /* TEX_RESOLVE_AUTO=1: resolve through the page tables (local-page map
         * then IO table) instead of trusting the format's location bits. A
         * texture the guest built in main memory but tagged local resolves to
         * untouched VRAM and samples as all-zero -- geometry renders, flat. */
        { static int _ra = -1; if (_ra < 0) _ra = getenv("TEX_RESOLVE_AUTO") ? 1 : 0;
          extern u32 cellGcmResolveIO(u32);
          u32 _r = 0;
          if (_ra) _r = cellGcmResolveIO(offset);          /* IO table first */
          if (!_r) _r = cellGcmResolveLocated((tex->format & 3) == 1, offset);
          /* TEX_PROBE=1: the location bits are not always right. This title
           * binds its menu textures with a location that resolves into main
           * memory, where the bytes are all zero, while the same raw offset
           * resolved as LOCAL lands on real data -- so 100k menu draws sample
           * black and the menu never appears. When the chosen resolution is
           * empty over a sample and the other one is not, take the other. */
          { static int _tp = -1;
            if (_tp < 0) _tp = getenv("TEX_PROBE") ? 1 : 0;
            if (_tp && vm_base && _r) {
                u32 alt = cellGcmResolveLocated((tex->format & 3) != 1, offset);
                if (alt && alt != _r && alt < 0xE0000000u && _r < 0xE0000000u) {
                    u32 span = width * height / 2;
                    if (span > 4096) span = 4096;
                    u32 nz_r = 0, nz_a = 0;
                    for (u32 i = 0; i < span; i += 7) {
                        if (vm_base[_r  + i]) nz_r++;
                        if (vm_base[alt + i]) nz_a++;
                    }
                    if (nz_r == 0 && nz_a > 0) {
                        static u32 seen[32]; static int ns = 0; int known = 0;
                        for (int k = 0; k < ns; k++) if (seen[k] == offset) known = 1;
                        if (!known && ns < 32) { seen[ns++] = offset;
                            fprintf(stderr, "[TEX_PROBE] 0x%08X: %s empty, using %s"
                                            " (%u non-zero)%c", offset,
                                    ((tex->format & 3) == 1) ? "local" : "main",
                                    ((tex->format & 3) == 1) ? "main" : "local",
                                    nz_a, 10); }
                        _r = alt;
                    }
                }
            } }
          s_d3d.cur_texs[unit].off = _r; }
        s_d3d.cur_texs[unit].raw = offset;
        s_d3d.cur_texs[unit].w = width; s_d3d.cur_texs[unit].h = height;
        s_d3d.cur_texs[unit].fmt = format;   /* full byte: LN(0x20)/UN(0x40) kept */
        s_d3d.cur_texs[unit].ctrl1 = tex->control1;
        s_d3d.cur_texs[unit].addr  = tex->address;
        s_d3d.cur_texs[unit].cube  = (tex->format & 4) ? 1 : 0;
        s_d3d.cur_texs[unit].mips  = (tex->format >> 16) & 0xFFFFu;
        if (getenv("TEXFMTDBG")) { static u32 seen[64]; static int ns=0;
            u32 k = (unit<<24) | (((tex->format>>8) & 0xFFu)<<16) | (width & 0xFFFFu);
            int f=0; for (int i=0;i<ns;i++) if (seen[i]==k) f=1;
            if (!f && ns < 64) { seen[ns++]=k;
                fprintf(stderr, "[TEXFMT] unit=%u fmt=0x%02X %ux%u mips=%u cube=%d off=0x%X%c",
                        unit, (tex->format >> 8) & 0xFFu, width, height,
                        (tex->format>>16)&0xFFFFu, (tex->format&4)?1:0, offset, 10); } }
        s_d3d.cur_texs[unit].set = 1;
    }
    if (base_fmt == 0x81 /* B8 */) {
        s_d3d.tex_src_offset = cellGcmResolveLocated((tex->format & 3) == 1, offset);
        if (s_d3d.tex_w != width || s_d3d.tex_h != height) {
            /* dims changed -> resource must be (re)created in render_frame */
            s_d3d.tex_ready = 0;
        }
        s_d3d.tex_w     = width;
        s_d3d.tex_h     = height;
        s_d3d.tex_bound = 1;
        s_d3d.tex_dirty = 1;
    } else {
        s_d3d.tex_bound = 0;
    }
}

static void d3d12_set_vertex_attribs(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;

    /* Log enabled vertex attributes for debugging. RSX_VTXDBG=<N> raises the
     * cap: the fixed 5 were all consumed by boot-time setup, so the layouts the
     * real draws use were never printed. */
    static int log_count = 0, log_cap = -1;
    if (log_cap < 0) { const char* e = getenv("RSX_VTXDBG"); log_cap = e ? atoi(e) : 5; }
    if (log_count < log_cap) {
        printf("[D3D12] set_vertex_attribs:\n");
        for (int i = 0; i < 16; i++) {
            const rsx_vertex_attrib* a = &state->vertex_attribs[i];
            if (a->enabled) {
                const char* type_name = "?";
                switch (a->type) {
                case 1: type_name = "snorm16"; break;
                case 2: type_name = "float32"; break;
                case 3: type_name = "float16"; break;
                case 4: type_name = "ubyte"; break;
                case 5: type_name = "s16"; break;
                case 7: type_name = "ubyte256"; break;
                }
                printf("  attrib[%d]: %s x%u, stride=%u, offset=0x%X\n",
                       i, type_name, a->size, a->stride, a->offset);
            }
        }
        log_count++;
    }
}

static void d3d12_set_shader(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;

    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_shader: fp_addr=0x%08X, vp_load=%u, output_mask=0x%08X\n",
               state->fragment_program_addr, state->transform_program_load,
               state->vertex_attrib_output_mask);
        log_count++;
    }

    /* TODO: Look up or compile a PSO matching this shader combination.
     * For now we use the basic vertex-colored PSO for everything. */
}

static void d3d12_set_blend(void* ud, const rsx_state* state)
{
    (void)ud;
    /* TODO: modify PSO blend state or use dynamic state.
     * D3D12 requires PSO recreation for blend state changes,
     * so we'd need a PSO cache keyed by blend configuration. */
    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_blend(enable=%d, sfactor=0x%X, dfactor=0x%X)\n",
               state->blend_enable, state->blend_sfactor, state->blend_dfactor);
        log_count++;
    }
}

static void d3d12_set_depth_stencil(void* ud, const rsx_state* state)
{
    (void)ud;
    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_depth_stencil(depth=%d, stencil=%d, func=0x%X)\n",
               state->depth_test_enable, state->stencil_test_enable,
               state->depth_func);
        log_count++;
    }
}

/* ---------------------------------------------------------------------------
 * Backend registration
 * -----------------------------------------------------------------------*/

static rsx_backend s_d3d12_backend = {0};

/* ---------------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/

int rsx_d3d12_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_d3d, 0, sizeof(s_d3d));
    s_d3d.draws = s_draw_bufs[0];
    s_d3d.width = width;
    s_d3d.height = height;
    s_d3d.clear_color[0] = 0.0f;
    s_d3d.clear_color[1] = 0.0f;
    s_d3d.clear_color[2] = 0.1f;
    s_d3d.clear_color[3] = 1.0f;

    /* Debug: dump the first N presented frames to BMP if CELLMARK_DUMP is set
     * (its numeric value when > 1, else 24). */
    {
        const char* dv = getenv("CELLMARK_DUMP");
        int n = dv ? atoi(dv) : 0;
        s_d3d.dump_frames_left = dv ? (n > 1 ? n : 24) : 0;
        /* CELLMARK_DUMP_SKIP=N: ignore the first N presents before dumping.
         * The opening frames are the loading screen; the interesting content
         * only appears once the sim SPUs have advanced the scene. */
        { const char* sv = getenv("CELLMARK_DUMP_SKIP");
          s_d3d.dump_skip_left = sv ? atoi(sv) : 0; }
    }

    /* Create window */
    {
        extern char g_rsx_title_base[128];
        snprintf(g_rsx_title_base, sizeof(g_rsx_title_base), "%s",
                 title ? title : "ps3recomp");
    }
    { const char* e = getenv("RES_SCALE");
      float f = e ? (float)atof(e) : 1.0f;
      if (f >= 0.5f && f <= 4.0f && f != 1.0f) s_rs = f;   /* 0.667 = 480p */
      if (s_rs != 1.0f)
          printf("[D3D12] RES_SCALE %.2f: rendering %ux%u for a %ux%u guest\n",
                 s_rs, rs_dim(width, s_rs), rs_dim(height, s_rs), width, height); }
    { const char* probe = getenv("CV_LIGHTER_PROBE");
      s_lighter_probe = probe && !strcmp(probe, "1");
      InterlockedExchange(&s_lighter_shadow_bypass, 0);
      if (s_lighter_probe) fprintf(stderr, "[LIGHTER] isolation active: F10 toggles visibility, F12 captures; separate FXC cache\n"); }
    s_d3d.hwnd = create_window(rs_dim(width, s_rs), rs_dim(height, s_rs), title);
    if (!s_d3d.hwnd) {
        printf("[D3D12] ERROR: Window creation failed\n");
        return -1;
    }

    /* Initialize D3D12 */
    if (init_d3d12(width, height) != 0) {
        printf("[D3D12] ERROR: D3D12 initialization failed\n");
        return -1;
    }

    /* Set up backend callbacks */
    s_d3d12_backend.userdata          = &s_d3d;
    s_d3d12_backend.init              = d3d12_init;
    s_d3d12_backend.shutdown          = d3d12_shutdown;
    s_d3d12_backend.begin_frame       = d3d12_begin_frame;
    s_d3d12_backend.end_frame         = d3d12_end_frame;
    s_d3d12_backend.present           = d3d12_present;
    s_d3d12_backend.clear             = d3d12_clear;
    s_d3d12_backend.set_render_target = d3d12_set_render_target;
    s_d3d12_backend.set_viewport      = d3d12_set_viewport;
    s_d3d12_backend.set_blend         = d3d12_set_blend;
    s_d3d12_backend.set_depth_stencil = d3d12_set_depth_stencil;
    s_d3d12_backend.set_shader        = d3d12_set_shader;
    s_d3d12_backend.set_vertex_attribs = d3d12_set_vertex_attribs;
    s_d3d12_backend.draw_arrays       = d3d12_draw_arrays;
    s_d3d12_backend.draw_indexed      = d3d12_draw_indexed;
    s_d3d12_backend.bind_texture      = d3d12_bind_texture;

    rsx_set_backend(&s_d3d12_backend);

    s_d3d.initialized = 1;
    s_d3d.last_fps_time = GetTickCount64();

    printf("[D3D12] Backend ready: %ux%u\n", width, height);
    return 0;
}

void rsx_d3d12_backend_shutdown(void)
{
    if (!s_d3d.initialized) return;

    render_wait_idle();
    wait_for_gpu();
    plib_save();   /* pipelines built since the last periodic save */
    mc_shutdown();
    rsx_fsr31_shutdown();
    pc_release(&s_pose_compute);rsx_pose_compute_plan_destroy(&s_pose_compute_plan);
    rsx_frame_gpu_destroy(&s_frame_gpu);s_frame_gpu_init=0;

    /* Release D3D12 resources */
    if (s_d3d.vertex_buffer) {
        s_d3d.vertex_buffer->lpVtbl->Unmap(s_d3d.vertex_buffer, 0, NULL);
        s_d3d.vertex_buffer->lpVtbl->Release(s_d3d.vertex_buffer);
    }
    if (s_d3d.pipeline_state)        s_d3d.pipeline_state->lpVtbl->Release(s_d3d.pipeline_state);
    if (s_d3d.pipeline_state_lines)  s_d3d.pipeline_state_lines->lpVtbl->Release(s_d3d.pipeline_state_lines);
    if (s_d3d.pipeline_state_points) s_d3d.pipeline_state_points->lpVtbl->Release(s_d3d.pipeline_state_points);
    if (s_d3d.pipeline_state_ui) s_d3d.pipeline_state_ui->lpVtbl->Release(s_d3d.pipeline_state_ui);
    for(u32 i=0;i<FRAME_COUNT;i++) {
        if(s_ui_vbs.vb[i])s_ui_vbs.vb[i]->lpVtbl->Release(s_ui_vbs.vb[i]);
        if(s_ov_vbs.vb[i])s_ov_vbs.vb[i]->lpVtbl->Release(s_ov_vbs.vb[i]);
        if(s_srt_vbs.vb[i])s_srt_vbs.vb[i]->lpVtbl->Release(s_srt_vbs.vb[i]);
        if(s_spr_vbs.vb[i])s_spr_vbs.vb[i]->lpVtbl->Release(s_spr_vbs.vb[i]);
    }
    if (s_d3d.depth_buffer) s_d3d.depth_buffer->lpVtbl->Release(s_d3d.depth_buffer);
    if (s_d3d.dsv_heap)     s_d3d.dsv_heap->lpVtbl->Release(s_d3d.dsv_heap);
    if (s_d3d.root_signature) s_d3d.root_signature->lpVtbl->Release(s_d3d.root_signature);
    if (s_d3d.fence) s_d3d.fence->lpVtbl->Release(s_d3d.fence);
    if (s_d3d.fence_event) CloseHandle(s_d3d.fence_event);
    if (s_d3d.cmd_list) s_d3d.cmd_list->lpVtbl->Release(s_d3d.cmd_list);
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        if (s_d3d.cmd_allocators[i]) s_d3d.cmd_allocators[i]->lpVtbl->Release(s_d3d.cmd_allocators[i]);
        if (s_d3d.render_targets[i]) s_d3d.render_targets[i]->lpVtbl->Release(s_d3d.render_targets[i]);
        if (s_disp_bufs[i]) { s_disp_bufs[i]->lpVtbl->Release(s_disp_bufs[i]); s_disp_bufs[i] = NULL; }
    }
    if (s_disp_rtv) { s_disp_rtv->lpVtbl->Release(s_disp_rtv); s_disp_rtv = NULL; }
    if (s_d3d.rtv_heap) s_d3d.rtv_heap->lpVtbl->Release(s_d3d.rtv_heap);
    if (s_d3d.swap_chain) s_d3d.swap_chain->lpVtbl->Release(s_d3d.swap_chain);
    if (s_d3d.cmd_queue) s_d3d.cmd_queue->lpVtbl->Release(s_d3d.cmd_queue);
    if (s_d3d.device) s_d3d.device->lpVtbl->Release(s_d3d.device);

    if (s_d3d.hwnd) DestroyWindow(s_d3d.hwnd);

    rsx_set_backend(NULL);
    s_d3d.initialized = 0;

    printf("[D3D12] Backend shut down\n");
}

int rsx_d3d12_backend_pump_messages(void)
{
    MSG msg;
    if (s_title_dirty && s_d3d.hwnd) {
        char tb[256];
        AcquireSRWLockExclusive(&s_title_lock);
        memcpy(tb, s_title_pending, sizeof tb); s_title_dirty = 0;
        ReleaseSRWLockExclusive(&s_title_lock);
        SetWindowTextA(s_d3d.hwnd, tb);
    }
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return -1;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return s_d3d.window_closed ? -1 : 0;
}

void rsx_d3d12_backend_present(void)
{
    if (blink_dbg())
        printf("[PRESENT] draws=%u clears_since_last=%u clear_presents=%u\n",
               s_d3d.draw_count, s_dbg_clears_since_present, s_clear_presents);
    s_dbg_clears_since_present = 0;

    /* Once frame-boundary presents are active (d3d12_clear presents each
     * completed frame as the drain crosses into the next one), the ticker
     * present would only ever show the partially-accumulated NEXT frame --
     * that partial present right after the FIFO ring recycle was the visible
     * blink. Keep the ticker present solely as the boot-time fallback (before
     * the first framed clear arrives). */
    if (s_clear_presents > 0)
        return;

    /* Same display gate as d3d12_present: a batch of offscreen pass work only
     * (render-to-texture) keeps accumulating until its composite arrives.
     * Empty batches present only until the first real frame -- after that an
     * empty present is a flip/drain race and wipes the screen for a frame
     * (wave: black flashes and layout flicker between frames). */
    static int s_seen_content = 0;
    int has_display = (s_d3d.draw_count == 0 && !s_seen_content);
    /* Is any recorded draw on the display? The list only grows between
     * render_frame calls, so scan just what was added since the last call
     * (re-checking the last record, which can still be amended). Rescanning
     * every record each time walked ~3800 large records per frame in Code
     * Veronica's 3D cutscenes, a quarter of the frame_clock thread. */
    { static unsigned scan_gen = ~0u; static u32 scanned; static int found;
      if (scan_gen != s_draw_gen || s_d3d.draw_count < scanned) {
          scan_gen = s_draw_gen; scanned = 0; found = 0; }
      if (!found) {
          u32 end = s_d3d.draw_count < MAX_DRAWS ? s_d3d.draw_count : MAX_DRAWS;
          for (u32 _i = scanned ? scanned - 1 : 0; _i < end; _i++)
              if (!s_d3d.draws[_i].is_clear && s_d3d.draws[_i].rt_off == 0) { found = 1; break; }
          scanned = end;
      }
      if (found) has_display = 1; }
    /* The guest-framebuffer present needs the pass to run at all. */
    { static int gfb = -1;
      if (gfb < 0) { const char* e = getenv("GCM_GUEST_FB"); gfb = e ? 1 : 0; }
      if (gfb) has_display = 1; }
    if (s_composite_src) has_display = 1;
    if (has_display && s_d3d.draw_count > 0) s_seen_content = 1;

    /* VP_SUBMIT=<N>: has_display gates render_frame() entirely, so a batch whose
     * records all target an OFFSCREEN rt (rt_off != 0) presents without ever
     * running the draw pass -- the backbuffer then shows only the clear, which
     * reads as "nothing rasterizes" from every downstream check. */
    { static int cap = -1, n = 0;
      if (cap < 0) { const char* e = getenv("VP_SUBMIT"); cap = e ? atoi(e) : 0; }
      if (cap && n < cap && s_d3d.draw_count) { n++;
        u32 onscreen = 0, offscreen = 0, clears = 0;
        for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++) {
            if (s_d3d.draws[_i].is_clear) { clears++; continue; }
            if (s_d3d.draws[_i].rt_off) offscreen++; else onscreen++;
        }
        { /* Which surfaces the batch's draws actually target -- "offscreen=57"
           * does not say whether they all go to one RT or scatter. */
          u32 offs[8]; u32 cnt[8]; int nd = 0;
          for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++) {
              if (s_d3d.draws[_i].is_clear) continue;
              u32 o = s_d3d.draws[_i].rt_off; int f = -1;
              for (int _j = 0; _j < nd; _j++) if (offs[_j] == o) { f = _j; break; }
              if (f < 0) { if (nd < 8) { offs[nd] = o; cnt[nd] = 1; nd++; } }
              else cnt[f]++;
          }
          { u32 vp = 0, novp = 0;
            for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++) {
                if (s_d3d.draws[_i].is_clear) continue;
                if (s_d3d.draws[_i].is_vp) vp++; else novp++;
            }
            /* The execution loop skips anything without is_vp, so a batch full
             * of !is_vp records is counted, gated and then never drawn. */
            fprintf(stderr, "[PRESENTGATE] is_vp=%u not_vp=%u%c", vp, novp, 10); }
          fprintf(stderr, "[PRESENTGATE] targets:");
          for (int _j = 0; _j < nd; _j++)
              fprintf(stderr, " 0x%08X x%u", offs[_j], cnt[_j]);
          fprintf(stderr, "%c", 10); }
        fprintf(stderr, "[PRESENTGATE] records=%u onscreen=%u offscreen=%u clears=%u"
                        " has_display=%d seen_content=%d -> render_frame=%s\n",
                s_d3d.draw_count, onscreen, offscreen, clears, has_display,
                s_seen_content, (s_d3d.initialized && has_display) ? "YES" : "SKIPPED"); } }

    /* Execute the batch whenever it has draws. Gating the whole call on
     * has_display meant a render-to-texture pass -- every draw targeting an
     * offscreen surface -- was DISCARDED rather than deferred, so the texture
     * it produces was never written and whatever sampled it later read an
     * empty resource. Only the Present needs onscreen content. */
    if (s_d3d.initialized && (has_display || s_d3d.draw_count > 0)) {
        { extern void rsx_reset_upload_claims(void);
          static int remap = -1;
          if (remap < 0) { const char* e = getenv("TEX_REMAP"); remap = e ? atoi(e) : 0; }
          if (remap) rsx_reset_upload_claims(); }
        s_present_this_frame = has_display;
        render_frame();
        s_present_this_frame = 1;
    }
}

#else /* !_WIN32 */

#include <ps3emu/ps3types.h>   /* u32 (header includes above are inside the _WIN32 guard) */
#include <stdio.h>

/* Stub for non-Windows â€” D3D12 is Windows-only */
int rsx_d3d12_backend_init(u32 w, u32 h, const char* t)
{
    (void)w; (void)h; (void)t;
    printf("[D3D12] Not available on this platform (use Vulkan backend)\n");
    return -1;
}
void rsx_d3d12_backend_shutdown(void) {}
int rsx_d3d12_backend_pump_messages(void) { return 0; }
void rsx_d3d12_backend_present(void) {}

#endif /* _WIN32 */
