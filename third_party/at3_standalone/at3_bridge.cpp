/* See at3_bridge.h. */
#include "at3_bridge.h"
#include "at3_decoders.h"

extern "C" void* at3p_open(int channels, int* block_align) { return atrac3p_alloc(channels, block_align); }
extern "C" void  at3p_close(void* dec) { atrac3p_free((ATRAC3PContext*)dec); }
extern "C" void  at3p_flush(void* dec) { atrac3p_flush_buffers((ATRAC3PContext*)dec); }
extern "C" int   at3p_decode(void* dec, float* left, float* right, int* samples,
                             const uint8_t* frame, int frame_bytes)
{
    float* out[2] = { left, right };
    return atrac3p_decode_frame((ATRAC3PContext*)dec, out, samples, frame, frame_bytes);
}

extern "C" void* at3_open(int channels, int block_align)
{
    /* WAV-style extradata: version 1, samples, coding mode 0 (no joint stereo),
     * its duplicate, frame factor 1, reserved. */
    uint8_t ed[14] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
    int ba = block_align * channels;
    return atrac3_alloc(channels, &ba, ed, (int)sizeof ed);
}
extern "C" void  at3_close(void* dec) { atrac3_free((ATRAC3Context*)dec); }
extern "C" int   at3_decode(void* dec, float* left, float* right, const uint8_t* frame, int frame_bytes)
{
    float* out[2] = { left, right };
    int n = 0;
    return atrac3_decode_frame((ATRAC3Context*)dec, out, &n, frame, frame_bytes);
}
