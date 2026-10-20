#ifndef RSX_POSE_GPU_H
#define RSX_POSE_GPU_H
#include <stdint.h>
#include <string.h>
#define RSX_POSE_GPU_STRIDE 32u
#define RSX_POSE_GPU_CPU_STRIDE 16u
#define RSX_POSE_ATTRIBUTE_MASK ((1u<<0)|(1u<<8)|(1u<<15))
/* Per-output-vertex recipe, in fenced parity banks: old.xyz/mode,
 * current.xyz/alpha. Mode 1 retains the exact CPU override for comparison
 * and conservative fallbacks; mode 2 evaluates the original blend on GPU.
 * Neither path changes raw descriptors, guest memory or original a0.w. */
static inline void rsx_pose_gpu_reset(void* record) {
    uint32_t inactive=0;memcpy((uint8_t*)record+12,&inactive,4);
}
static inline void rsx_pose_gpu_write(void* record,const float xyz[3]) {
    uint32_t active=1;memcpy(record,xyz,12);memcpy((uint8_t*)record+12,&active,4);
}
static inline int rsx_pose_gpu_write_blend(void* record,const float old_xyz[3],const float current_xyz[3],float alpha) {
    uint32_t a;memcpy(&a,&alpha,4);
    if(a<0x3f000000u||a>0x3f800000u)return 0; /* finite alpha in [.5,1] */
    /* Shader float arithmetic may flush subnormals. Keep tiny/unknown values
     * on the exact CPU path, including cancellation close to zero. For
     * nonzero operands >=2^-63, differences/products cannot be subnormal. */
    for(int k=0;k<3;k++) {
        uint32_t x,y;memcpy(&x,old_xyz+k,4);memcpy(&y,current_xyz+k,4);
        x&=0x7fffffffu;y&=0x7fffffffu;
        if((x&&x<0x20000000u)||(y&&y<0x20000000u)||x>0x49800000u||y>0x49800000u)return 0;
    }
    uint32_t active=2;memcpy(record,old_xyz,12);memcpy((uint8_t*)record+12,&active,4);
    memcpy((uint8_t*)record+16,current_xyz,12);memcpy((uint8_t*)record+28,&alpha,4);return 1;
}
/* Diagnostics only: reconstruct what the shader sees for F12 captures. */
static inline int rsx_pose_gpu_read(const void* record,float xyz[3]) {
    uint32_t mode;memcpy(&mode,(const uint8_t*)record+12,4);
    if(mode==1){memcpy(xyz,record,12);return 1;}
    if(mode==2) {
        float a[3],b[3],t;memcpy(a,record,12);memcpy(b,(const uint8_t*)record+16,12);memcpy(&t,(const uint8_t*)record+28,4);
        for(int k=0;k<3;k++)xyz[k]=a[k]+(b[k]-a[k])*t;return 1;
    }
    return 0;
}
#define RSX_POSE_GPU_HLSL \
    "#if VF_POSE_COMPUTE_ENABLED\n" \
    "ByteAddressBuffer vf_pose_computed:register(t25);\n" \
    "float4 vf_pose_position(uint index,float4 original){uint4 p=vf_pose_computed.Load4(index*16u);return p.w==1u?float4(asfloat(p.xyz),original.w):original;}\n" \
    "#else\n" \
    "#ifndef VF_POSE_STRIDE_U\n#define VF_POSE_STRIDE_U 32u\n#endif\n" \
    "float4 vf_pose_position(uint index,float4 original) {\n" \
    " uint offset=VF_POSE_OFF_U+index*VF_POSE_STRIDE_U;\n" \
    " uint4 p=vf_buf.Load4(offset);\n" \
    " if(p.w==1u)return float4(asfloat(p.xyz),original.w);\n" \
    "#if VF_POSE_STRIDE_U >= 32u\n" \
    " if(p.w==2u){\n" \
    "  float4 b=asfloat(vf_buf.Load4(offset+16u));\n" \
    "  precise float3 delta=b.xyz-asfloat(p.xyz);\n" \
    "  precise float3 scaled=delta*b.w;\n" \
    "  precise float3 position=asfloat(p.xyz)+scaled;\n" \
    "  return float4(position,original.w);\n" \
    " }\n" \
    "#endif\n" \
    " return original;\n" \
    "}\n" \
    "#endif\n"
#endif
