#ifndef RSX_POSE_COMPARE_H
#define RSX_POSE_COMPARE_H
#include <stdint.h>
#include <string.h>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define RSX_POSE_COMPARE_SSE2 1
#endif
/* Preserve the exhaustive check: sampled matching never replaces this loop.
 * changed is observed only if jump==0; a jump discards the temporal proposal. */
static inline int rsx_pose_compare_scalar(const float* a,const float* b,uint32_t n,int* changed) {
    for(uint32_t k=0;k<n;k++) {
        float distance=0;
        for(int v=0;v<3;v++){float d=a[(size_t)k*3+v]-b[(size_t)k*3+v];distance+=d*d;}
        if(distance>64.f)return 1;
        if(memcmp(a+(size_t)k*3,b+(size_t)k*3,12))*changed=1;
    }
    return 0;
}
static inline int rsx_pose_compare_vectors(const float* a,const float* b,uint32_t n,int* changed) {
#ifdef __clang__
#pragma clang fp contract(off)
#endif
    uint32_t k=0;
#ifdef RSX_POSE_COMPARE_SSE2
    for(;n-k>=4;k+=4) {
        // Exactly 12 floats: no fourth component read past the final vertex.
        const float* x=a+(size_t)k*3;const float* y=b+(size_t)k*3;
        __m128 a0=_mm_loadu_ps(x),a1=_mm_loadu_ps(x+4),a2=_mm_loadu_ps(x+8);
        __m128 b0=_mm_loadu_ps(y),b1=_mm_loadu_ps(y+4),b2=_mm_loadu_ps(y+8);
        __m128 d0=_mm_sub_ps(a0,b0),d1=_mm_sub_ps(a1,b1),d2=_mm_sub_ps(a2,b2);
        d0=_mm_mul_ps(d0,d0);d1=_mm_mul_ps(d1,d1);d2=_mm_mul_ps(d2,d2);
        __m128 xx01=_mm_shuffle_ps(d0,d0,_MM_SHUFFLE(3,0,3,0));
        __m128 xx23=_mm_shuffle_ps(d1,d2,_MM_SHUFFLE(1,1,2,2));
        __m128 xx=_mm_shuffle_ps(xx01,xx23,_MM_SHUFFLE(2,0,1,0));
        __m128 yy01=_mm_shuffle_ps(d0,d1,_MM_SHUFFLE(0,0,1,1));
        __m128 yy23=_mm_shuffle_ps(d1,d2,_MM_SHUFFLE(2,2,3,3));
        __m128 yy=_mm_shuffle_ps(yy01,yy23,_MM_SHUFFLE(2,0,2,0));
        __m128 zz01=_mm_shuffle_ps(d0,d1,_MM_SHUFFLE(1,1,2,2));
        __m128 zz23=_mm_shuffle_ps(d2,d2,_MM_SHUFFLE(3,0,3,0));
        __m128 zz=_mm_shuffle_ps(zz01,zz23,_MM_SHUFFLE(1,0,2,0));
        __m128 distance=_mm_add_ps(_mm_add_ps(xx,yy),zz);
        if(_mm_movemask_ps(_mm_cmpgt_ps(distance,_mm_set1_ps(64.f))))return 1;
        // A camera change or an earlier changed vertex already settles this
        // flag. Keep checking EVERY distance, but do not compare bits again.
        if(!*changed) {
            __m128i bits=_mm_or_si128(_mm_xor_si128(_mm_castps_si128(a0),_mm_castps_si128(b0)),
                _mm_or_si128(_mm_xor_si128(_mm_castps_si128(a1),_mm_castps_si128(b1)),_mm_xor_si128(_mm_castps_si128(a2),_mm_castps_si128(b2))));
            if(_mm_movemask_epi8(_mm_cmpeq_epi8(bits,_mm_setzero_si128()))!=65535)*changed=1;
        }
    }
#endif
    return rsx_pose_compare_scalar(a+(size_t)k*3,b+(size_t)k*3,n-k,changed);
}
#endif
