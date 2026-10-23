/*
 * ps3recomp - native decode for the MultiStream ATRAC3 plug-in
 *
 * Sony's MultiStream audio library (linked into titles such as Code Veronica)
 * decodes ATRAC3 sound effects on its SPU mixer through a firmware plug-in,
 * /dev_flash/sys/external/flashATRAC.pic. The runtime can serve a stand-in
 * plug-in instead (runtime/syscalls/sys_fs.c) whose decode entry the SPU
 * dispatcher routes here, so the title plays its ATRAC3 audio with the LGPL
 * decoder in third_party/at3_standalone and needs no firmware.
 *
 * The contract was recovered from the plug-in's decodeATRACPackets and checked
 * against it call by call, offline (tools/ms_atrac3_check.c) and in game:
 *
 *   r3 work  LS work area holding the frames ahead of the current one: frame j
 *            (0 = the next) of channel c at work + (j * channels + c) * size.
 *   r4 out   LS output; channel c at out + c * 0x1000, N + 4 big-endian int16.
 *   r6 N     window length in samples: 1024, or around 1186 for a voice the
 *            mixer pitches up.
 *   r7 strm  LS stream info: format word at +0x2D0 (4/5/6 = 96/152/192-byte
 *            frames per channel), channel count byte at +33, per-channel state
 *            slots at +0x368 + 4c.
 *   r8 ms    LS MultiStream info: reset flag at +0x818, window position P at
 *            +0x8D0 (a sample of the current 1024-sample frame; 1024 when no
 *            frame is current yet), decoder info at +0x8D4..+0x8E7.
 *   r9 chan  LS: state base EA at +0x7C; channel state = base + slot * 4992.
 *            (Sony keeps its decoder there; this decoder keys on that EA.)
 *
 * Each call writes the stream's samples [P, P + N) -- the rest of the current
 * frame and as many frames ahead as that needs -- plus 4 samples of linear
 * extrapolation for the mixer's resampler, then advances P by N / 2. When P
 * crosses the frame end the next frame becomes current (P -= 1024); only then
 * does Sony save its decoder state. Frames it decodes just to fill a window
 * are decoded again, from the saved state, once they become current -- so
 * here each frame ahead is decoded once and kept until it is reached. The new
 * P is returned. Sony's decoder has the opposite polarity to FFmpeg's; with the
 * sign flipped the two agree to within float rounding (a few LSB).
 */
#include "ms_atrac3.h"
#include "audio_gain.h"
#include "../../third_party/at3_standalone/at3_bridge.h"
#include <stdio.h>
#include <string.h>

#define FRAME_SAMPLES 1024
#define STATE_BYTES   4992u
#define AHEAD         3           /* frames decoded ahead of the current one */

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int16_t rd16(const uint8_t* p) { return (int16_t)((uint16_t)p[0] << 8 | p[1]); }
static void wr16(uint8_t* p, int16_t v) { p[0] = (uint8_t)((uint16_t)v >> 8); p[1] = (uint8_t)v; }

typedef struct { uint8_t in[192]; int16_t pcm[FRAME_SAMPLES]; } Ahead;

/* One channel stream, keyed by the EA of Sony's state for it. */
typedef struct {
    uint32_t ea;
    int frame;
    void* dec;
    int16_t cur[FRAME_SAMPLES];
    int n_ahead;
    Ahead ahead[AHEAD];
} Stream;

static Stream s_streams[64];
static volatile long s_lock;

static void lock(void)   { while (__atomic_exchange_n(&s_lock, 1, __ATOMIC_ACQUIRE)) { } }
static void unlock(void) { __atomic_store_n(&s_lock, 0, __ATOMIC_RELEASE); }

static void stream_reset(Stream* s)
{
    if (s->dec) at3_close(s->dec);
    s->dec = at3_open(1, s->frame);
    memset(s->cur, 0, sizeof s->cur);
    s->n_ahead = 0;
}

static Stream* stream_for(uint32_t ea, int frame, int reset)
{
    Stream* free_slot = NULL;
    for (int i = 0; i < 64; i++) {
        Stream* s = &s_streams[i];
        if (s->dec && s->ea == ea) {
            if (reset || s->frame != frame) { s->frame = frame; stream_reset(s); }
            return s;
        }
        if (!s->dec && !free_slot) free_slot = s;
    }
    if (!free_slot) free_slot = &s_streams[0];       /* table full: recycle */
    free_slot->ea = ea;
    free_slot->frame = frame;
    stream_reset(free_slot);
    return free_slot->dec ? free_slot : NULL;
}

static int16_t to_s16(float f)
{
    float v = -f * 32768.0f;                         /* Sony's polarity */
    v = v < 0 ? v - 0.5f : v + 0.5f;
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
}

/* Frame j ahead of the current one, decoding it if this is its first use. */
static const int16_t* frame_ahead(Stream* s, int j, const uint8_t* in)
{
    static int s_warned;
    if (j >= AHEAD) j = AHEAD - 1;
    if (j < s->n_ahead) {
        if (!s_warned && memcmp(s->ahead[j].in, in, (size_t)s->frame) != 0) {
            s_warned = 1;
            fprintf(stderr, "[ms-atrac3] the input for a frame changed after it was decoded ahead\n");
        }
        return s->ahead[j].pcm;
    }
    while (s->n_ahead <= j) {
        Ahead* a = &s->ahead[s->n_ahead];
        float pcm[FRAME_SAMPLES];
        memcpy(a->in, in, (size_t)s->frame);
        if (at3_decode(s->dec, pcm, NULL, in, s->frame) < 0) memset(pcm, 0, sizeof pcm);
        for (int i = 0; i < FRAME_SAMPLES; i++) a->pcm[i] = to_s16(pcm[i]);
        s->n_ahead++;
    }
    return s->ahead[j].pcm;
}

uint32_t ms_atrac3_decode(uint8_t* ls, const uint32_t r[8])
{
    const uint32_t mask = 0x3FFFF;
    const uint32_t work = r[0] & mask, out = r[1] & mask, strm = r[4] & mask;
    const uint32_t ms = r[5] & mask, chan = r[6] & mask;
    const int n = (int)r[3];
    const uint32_t fmt = rd32(ls + strm + 0x2D0);
    const int channels = ls[strm + 33];
    const int frame = fmt == 4 ? 96 : fmt == 5 ? 152 : fmt == 6 ? 192 : 0;
    const int reset = rd32(ls + ms + 0x818) != 0;
    const int p = (int)rd32(ls + ms + 0x8D0);
    const uint32_t base = rd32(ls + chan + 0x7C);
    const int advance = p + n / 2 >= FRAME_SAMPLES;

    /* Format 4 (66 kbps) takes a separate path in the plug-in that has not
     * been seen in a title yet; play silence rather than guess at it. */
    static int s_warned, s_said;
    const int supported = frame && fmt != 4 && channels >= 1 && channels <= 2 &&
                          n > 0 && n <= 2048 && p >= 0 && p <= FRAME_SAMPLES;
    if (!s_said) {
        s_said = 1;
        fprintf(stderr, "[ms-atrac3] decoding the MultiStream ATRAC3 plug-in natively (format %u, %d channel(s))\n",
                fmt, channels);
    }
    if (!supported && !s_warned) {
        s_warned = 1;
        fprintf(stderr, "[ms-atrac3] unsupported stream: format %u, %d channels, window %d, position %d -- silence\n",
                fmt, channels, n, p);
    }

    const int effects_volume=ps3_audio_gain_percent(1);
    lock();
    for (int c = 0; c < (channels > 0 ? channels : 1) && c < 2; c++) {
        uint8_t* o = ls + ((out + (uint32_t)c * 0x1000) & mask);
        if (!supported) {
            memset(o, 0, (size_t)(n > 0 && n <= 2048 ? n + 4 : 4) * 2);
            continue;
        }
        const uint32_t slot = rd32(ls + strm + 0x368 + 4u * (uint32_t)c);
        Stream* s = stream_for(base + slot * STATE_BYTES, frame, reset);
        for (int i = 0; i < n; i++) {
            const int at = p + i;
            int16_t v = 0;
            if (at < FRAME_SAMPLES) v = s ? s->cur[at] : 0;
            else if (s) {
                const int j = at / FRAME_SAMPLES - 1;
                const uint8_t* in = ls + ((work + (uint32_t)((j * channels + c) * frame)) & mask);
                v = frame_ahead(s, j, in)[at % FRAME_SAMPLES];
            }
            wr16(o + 2 * i, v);
        }
        /* 4 samples of linear extrapolation, in wrapping 16-bit arithmetic */
        int16_t a = n >= 2 ? rd16(o + 2 * (n - 2)) : 0, b = rd16(o + 2 * (n - 1));
        for (int i = 0; i < 4; i++) {
            int16_t x = (int16_t)(uint16_t)((uint16_t)b + (uint16_t)(b - a));
            wr16(o + 2 * (n + i), x);
            a = b; b = x;
        }
        // Scale only the delivered window, after extrapolation. Decoder state
        // and lookahead stay unscaled, so gain never compounds or resets audio.
        ps3_audio_gain_s16be(o,(unsigned)n+4,effects_volume);
        if (s && advance) {
            if (!s->n_ahead) {                      /* never reached in practice */
                const uint8_t* in = ls + ((work + (uint32_t)(c * frame)) & mask);
                frame_ahead(s, 0, in);
            }
            memcpy(s->cur, s->ahead[0].pcm, sizeof s->cur);
            memmove(&s->ahead[0], &s->ahead[1], sizeof(Ahead) * (AHEAD - 1));
            s->n_ahead--;
        }
    }
    unlock();

    if (reset) {
        wr32(ls + ms + 0x8D4, 2);
        wr32(ls + ms + 0x8D8, (uint32_t)frame);
    }
    if (advance) {
        wr32(ls + ms + 0x8E0, work);
        wr32(ls + ms + 0x8E4, (uint32_t)frame * 8);
    }
    const int np = advance ? p + n / 2 - FRAME_SAMPLES : p + n / 2;
    wr32(ls + ms + 0x8D0, (uint32_t)np);
    return (uint32_t)np;
}
