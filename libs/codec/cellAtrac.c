/*
 * ps3recomp - cellAtrac HLE
 *
 * All-data-on-memory decoding of ATRAC3plus RIFF files, with looping. The
 * title hands over the whole .at3 file (SetDataAndGetMemSize), creates a
 * decoder, and then pulls one frame of float PCM per cellAtracDecode.
 *
 * Sample positions. FFmpeg's decoder output is a linear stream of 2048-sample
 * frames. The file's first audible sample sits at index
 *     first = fact[1] + 0x170
 * (0x170 is ATRAC3plus's own decoder delay -- the same constant PPSSPP uses),
 * and fact[0] audible samples follow. `smpl` loop points count from fact[2]
 * (fact[1] when the chunk has no third word). Checked on The Simpsons Arcade
 * Game's three tracks: first + fact[0] lands exactly on the last frame
 * boundary of the data chunk, and each loop spans the whole track.
 *
 * Streaming. When SetDataAndGetMemSize gets less than the whole file, the
 * title's buffer is a ring the title refills: GetStreamDataInfo says where to
 * write, how much, and from which file offset; AddStreamData says it did. The
 * ring's contents are only guest-visible bookkeeping here -- every chunk is
 * copied out on AddStreamData into a host copy of the whole file, and the
 * decoder reads that copy. Arrival is tracked per frame, so a seek
 * (GetBufferInfoForResetting/ResetPlayPosition) can start a new run anywhere.
 * Once every frame has arrived the track reports itself as on memory and the
 * title stops streaming; loops then need no second buffer.
 *
 * ponytail: no plain ATRAC3.
 */

#include "cellAtrac.h"
#include "../../runtime/ppu/ppu_memory.h"
#include "../../third_party/at3_standalone/at3_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AT3P_SAMPLES   2048
#define AT3P_DELAY     0x170
#define ATRAC_MAX_SLOTS 16

typedef struct {
    u32 channels, block_align;
    u32 data_off, data_bytes;  /* the data chunk, relative to the file start */
    s32 first, end;            /* decoder-stream indices; end is exclusive   */
    s32 loop_start, loop_end;  /* same space, loop_end exclusive; -1 = none  */
} AtracTrackInfo;

typedef struct {
    u32 handle_ea;             /* 0 = free */
    const u8* file;
    AtracTrackInfo t;
    void* dec;
    s32 pos;                   /* next decoder-stream index to output */
    s32 loop_num;              /* -1 forever, 0 none, n more laps */
    s32 next_frame;            /* frame the decoder expects next */
    s32 pcm_frame;             /* frame held in pcm[], -1 = none */
    float pcm[2][AT3P_SAMPLES];
    /* Streaming (stream != 0). `host` holds the whole file as it arrives. */
    int stream;
    u8* host;
    u8* have;                  /* per frame: all of its bytes have arrived */
    u32 file_bytes;            /* data_off + data_bytes */
    u32 buf_ea, buf_bytes;     /* the title's ring */
    u32 ring_base;             /* file offset that maps to ring offset 0 */
    u32 run_start, rd;         /* current contiguous run [run_start, rd) */
    u32 missing;               /* frames not yet arrived */
} AtracSlot;

static AtracSlot s_slots[ATRAC_MAX_SLOTS];

static u32 le32(const u8* p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 le16(const u8* p) { return (u16)(p[0] | p[1] << 8); }

/* The ATRAC3plus subformat GUID of a WAVE_FORMAT_EXTENSIBLE fmt chunk. */
static const u8 k_at3p_guid[16] = { 0xBF, 0xAA, 0x23, 0xE9, 0x58, 0xCB, 0x71, 0x44,
                                    0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62 };

/* Parses a RIFF .at3 header. Returns 0 or a CELL_ATRAC_ERROR_*. */
int atrac_parse_riff(const u8* f, u32 bytes, AtracTrackInfo* t)
{
    memset(t, 0, sizeof(*t));
    t->loop_start = t->loop_end = -1;
    if (bytes < 12 || memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4))
        return (int)CELL_ATRAC_ERROR_UNKNOWN_FORMAT;
    u32 total = 0, off1 = 0, off2 = 0, has_fact = 0, lstart = 0, lend = 0, has_loop = 0;
    u32 o = 12;
    while (o + 8 <= bytes && !t->data_off) {
        const u8* c = f + o;
        const u32 cs = le32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            if (o + 8 + 40 > bytes || cs < 40 || le16(c + 8) != 0xFFFE ||
                memcmp(c + 8 + 24, k_at3p_guid, 16))
                return (int)CELL_ATRAC_ERROR_UNKNOWN_FORMAT;   /* ATRAC3plus only */
            t->channels = le16(c + 10);
            t->block_align = le16(c + 20);
        } else if (!memcmp(c, "fact", 4) && cs >= 8 && o + 16 <= bytes) {
            has_fact = 1;
            total = le32(c + 8);
            off1 = le32(c + 12);
            off2 = (cs >= 12 && o + 20 <= bytes) ? le32(c + 16) : off1;
        } else if (!memcmp(c, "smpl", 4) && cs >= 36 + 24 && o + 8 + 60 <= bytes &&
                   le32(c + 8 + 28)) {
            has_loop = 1;
            lstart = le32(c + 8 + 36 + 8);
            lend = le32(c + 8 + 36 + 12);
        } else if (!memcmp(c, "data", 4)) {
            t->data_off = o + 8;
            t->data_bytes = cs;
        }
        o += 8 + cs;
    }
    if (!t->data_off || !t->block_align || t->channels < 1 || t->channels > 2)
        return (int)CELL_ATRAC_ERROR_ILLEGAL_DATA;
    const s32 stream = (s32)(t->data_bytes / t->block_align) * AT3P_SAMPLES;
    t->first = (s32)off1 + AT3P_DELAY;
    t->end = has_fact ? t->first + (s32)total : stream;
    if (t->end > stream) t->end = stream;
    if (has_loop && lend >= lstart && lstart >= off2) {
        t->loop_start = t->first + (s32)(lstart - off2);
        t->loop_end = t->first + (s32)(lend - off2) + 1;
        if (t->loop_end > t->end) t->loop_end = t->end;
    }
    return 0;
}

static void slot_free(AtracSlot* s)
{
    if (s->dec) at3p_close(s->dec);
    free(s->host);
    free(s->have);
    memset(s, 0, sizeof(*s));
}

static u32 frame_count(const AtracSlot* s) { return s->t.data_bytes / s->t.block_align; }

/* Copies file bytes [from, to) out of the ring into the host copy and marks
 * every frame lying wholly inside the current run. */
static void stream_take(AtracSlot* s, u32 from, u32 to)
{
    if (to > s->file_bytes) to = s->file_bytes;
    for (u32 f = from; f < to; ) {
        const u32 ring = (f - s->ring_base) % s->buf_bytes;
        u32 n = s->buf_bytes - ring;
        if (n > to - f) n = to - f;
        memcpy(s->host + f, vm_base + s->buf_ea + ring, n);
        f += n;
    }
    const u32 ba = s->t.block_align, nf = frame_count(s);
    const u32 lo = s->run_start > s->t.data_off ? s->run_start - s->t.data_off : 0;
    for (u32 k = (lo + ba - 1) / ba; k < nf && s->t.data_off + (k + 1) * ba <= to; k++)
        if (!s->have[k]) { s->have[k] = 1; s->missing--; }
}

/* The file offset of the first frame at or after `f` that has not arrived,
 * or file_bytes when there is none from there to the end. */
static u32 next_missing(const AtracSlot* s, u32 f)
{
    const u32 ba = s->t.block_align, nf = frame_count(s);
    u32 k = f > s->t.data_off ? (f - s->t.data_off) / ba : 0;
    while (k < nf && s->have[k]) k++;
    return k < nf ? s->t.data_off + k * ba : s->file_bytes;
}

/* Frames ready to decode from the current position onwards. */
static s32 stream_remain(const AtracSlot* s)
{
    if (!s->missing)
        return s->t.loop_start >= 0 ? CELL_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY
                                    : CELL_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
    const u32 nf = frame_count(s);
    u32 k = (u32)(s->pos / AT3P_SAMPLES), n = 0;
    while (k < nf && s->have[k]) { k++; n++; }
    return (s32)n;
}

static AtracSlot* slot_of(u32 handle_ea)
{
    for (int i = 0; i < ATRAC_MAX_SLOTS; i++)
        if (s_slots[i].handle_ea == handle_ea) return &s_slots[i];
    return NULL;
}

/* Puts decoder-stream frame k in s->pcm, decoding forward from wherever the
 * decoder is. The decoder overlaps adjacent frames, so a jump backwards
 * restarts one frame early and discards that frame's output. */
static int decode_frame(AtracSlot* s, s32 k)
{
    if (s->pcm_frame == k) return 0;
    if (s->next_frame > k) {
        at3p_flush(s->dec);
        s->next_frame = k > 0 ? k - 1 : 0;
    }
    while (s->next_frame <= k) {
        const u32 at = s->t.data_off + (u32)s->next_frame * s->t.block_align;
        int n = 0;
        if (s->stream && !s->have[s->next_frame]) {
            /* Starved: the title has not delivered this frame yet. */
            memset(s->pcm, 0, sizeof(s->pcm));
        } else if (at3p_decode(s->dec, s->pcm[0], s->pcm[1], &n, s->file + at,
                        (int)s->t.block_align) < 0 || n != AT3P_SAMPLES) {
            /* A bad frame decodes as silence rather than ending the track. */
            memset(s->pcm, 0, sizeof(s->pcm));
        }
        s->next_frame++;
    }
    s->pcm_frame = k;
    return 0;
}

static int looping(const AtracSlot* s) { return s->t.loop_start >= 0 && s->loop_num != 0; }

s32 cellAtracSetDataAndGetMemSize(u32 pHandle, u32 pucBufferAddr, u32 uiReadByte,
                                  u32 uiBufferByte, u32 puiWorkMemByte)
{
    if (!pHandle || !pucBufferAddr) return (s32)CELL_ATRAC_ERROR_API_FAIL;
    AtracSlot* s = slot_of(pHandle);
    if (!s) s = slot_of(0);
    if (!s) return (s32)CELL_ATRAC_ERROR_API_FAIL;
    AtracTrackInfo t;
    const u8* file = vm_base + pucBufferAddr;
    int rc = atrac_parse_riff(file, uiReadByte, &t);
    if (rc) {
        fprintf(stderr, "[cellAtrac] SetData: not an ATRAC3plus file (0x%08X)\n", (u32)rc);
        return rc;
    }
    const u32 file_bytes = t.data_off + t.data_bytes;
    const int stream = uiReadByte < file_bytes;
    if (stream && (uiBufferByte < uiReadByte || uiBufferByte < t.block_align))
        return (s32)CELL_ATRAC_ERROR_READSIZE_OVER_BUFFER;
    slot_free(s);
    s->handle_ea = pHandle;
    s->file = file;
    s->t = t;
    if (stream) {
        const u32 nf = t.data_bytes / t.block_align;
        s->host = (u8*)malloc(file_bytes);
        s->have = (u8*)calloc(nf ? nf : 1, 1);
        if (!s->host || !s->have) { slot_free(s); return (s32)CELL_ATRAC_ERROR_API_FAIL; }
        s->stream = 1;
        s->file = s->host;
        s->file_bytes = file_bytes;
        s->buf_ea = pucBufferAddr;
        s->buf_bytes = uiBufferByte;
        s->missing = nf;
        s->rd = uiReadByte;
        stream_take(s, 0, uiReadByte);
    }
    s->pos = t.first;
    s->next_frame = 0;         /* start of the stream, like the console */
    s->pcm_frame = -1;
    (void)uiBufferByte;
    if (puiWorkMemByte) vm_write32(puiWorkMemByte, 0x1000);
    fprintf(stderr, "[cellAtrac] SetData handle=0x%08X ch=%u frames=%u first=%d end=%d loop=[%d,%d)%s\n",
            pHandle, t.channels, t.data_bytes / t.block_align, t.first, t.end,
            t.loop_start, t.loop_end, stream ? " streaming" : "");
    return CELL_OK;
}

s32 cellAtracCreateDecoder(u32 pHandle, u32 pucWorkMem, u32 uiPpuThreadPriority,
                           u32 uiSpuThreadPriority)
{
    (void)pucWorkMem; (void)uiPpuThreadPriority; (void)uiSpuThreadPriority;
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    if (s->dec) return (s32)CELL_ATRAC_ERROR_DECODER_WAS_CREATED;
    int ba = (int)s->t.block_align;
    s->dec = at3p_open((int)s->t.channels, &ba);
    return s->dec ? CELL_OK : (s32)CELL_ATRAC_ERROR_API_FAIL;
}

s32 cellAtracCreateDecoderExt(u32 pHandle, u32 pucWorkMem, u32 uiPpuThreadPriority,
                              u32 pExtRes)
{
    (void)pExtRes;
    return cellAtracCreateDecoder(pHandle, pucWorkMem, uiPpuThreadPriority, 0);
}

s32 cellAtracDeleteDecoder(u32 pHandle)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    slot_free(s);
    return CELL_OK;
}

/* One call returns at most one frame's worth of samples per channel,
 * interleaved big-endian floats. */
s32 cellAtracDecode(u32 pHandle, u32 pfOutAddr, u32 puiSamples, u32 puiFinishflag,
                    u32 piRemainFrame)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    if (!s->dec) return (s32)CELL_ATRAC_ERROR_NO_DECODER;
    if (puiSamples) vm_write32(puiSamples, 0);
    if (s->pos >= (looping(s) ? s->t.loop_end : s->t.end)) {
        if (!looping(s)) {
            if (puiFinishflag) vm_write32(puiFinishflag, 1);
            return (s32)CELL_ATRAC_ERROR_ALLDATA_WAS_DECODED;
        }
        s->pos = s->t.loop_start;
        if (s->loop_num > 0) s->loop_num--;
    }
    const s32 limit = looping(s) ? s->t.loop_end : s->t.end;
    const s32 k = s->pos / AT3P_SAMPLES, at = s->pos - k * AT3P_SAMPLES;
    s32 n = AT3P_SAMPLES - at;
    if (n > limit - s->pos) n = limit - s->pos;
    decode_frame(s, k);
    const u32 ch = s->t.channels;
    for (s32 i = 0; i < n; i++)
        for (u32 c = 0; c < ch; c++) {
            u32 bits;
            memcpy(&bits, &s->pcm[c][at + i], 4);
            vm_write32(pfOutAddr + ((u32)i * ch + c) * 4, bits);
        }
    s->pos += n;
    if (puiSamples) vm_write32(puiSamples, (u32)n);
    if (puiFinishflag) vm_write32(puiFinishflag, !looping(s) && s->pos >= s->t.end);
    if (piRemainFrame)
        vm_write32(piRemainFrame, (u32)(s->stream ? stream_remain(s) : CELL_ATRAC_ALLDATA_IS_ON_MEMORY));
    return CELL_OK;
}

s32 cellAtracGetRemainFrame(u32 pHandle, u32 piRemainFrame)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    vm_write32(piRemainFrame, (u32)(s->stream ? stream_remain(s) : CELL_ATRAC_ALLDATA_IS_ON_MEMORY));
    return CELL_OK;
}

s32 cellAtracGetChannel(u32 pHandle, u32 puiChannel)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    vm_write32(puiChannel, s->t.channels);
    return CELL_OK;
}

s32 cellAtracGetMaxSample(u32 pHandle, u32 puiMaxSample)
{
    if (!slot_of(pHandle)) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    vm_write32(puiMaxSample, AT3P_SAMPLES);
    return CELL_OK;
}

/* The API counts samples from the first audible one. */
s32 cellAtracGetNextSample(u32 pHandle, u32 puiNextSample)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    const s32 k = s->pos / AT3P_SAMPLES;
    vm_write32(puiNextSample, (u32)(AT3P_SAMPLES - (s->pos - k * AT3P_SAMPLES)));
    return CELL_OK;
}

s32 cellAtracGetSoundInfo(u32 pHandle, u32 piEndSample, u32 piLoopStartSample,
                          u32 piLoopEndSample)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    const AtracTrackInfo* t = &s->t;
    vm_write32(piEndSample, (u32)(t->end - t->first - 1));
    vm_write32(piLoopStartSample, (u32)(t->loop_start < 0 ? -1 : t->loop_start - t->first));
    vm_write32(piLoopEndSample, (u32)(t->loop_end < 0 ? -1 : t->loop_end - t->first - 1));
    return CELL_OK;
}

s32 cellAtracGetNextDecodePosition(u32 pHandle, u32 puiSamplePosition)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    if (s->pos >= s->t.end) return (s32)CELL_ATRAC_ERROR_ALLDATA_WAS_DECODED;
    vm_write32(puiSamplePosition, (u32)(s->pos - s->t.first));
    return CELL_OK;
}

s32 cellAtracGetBitrate(u32 pHandle, u32 puiBitrate)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    /* kbps: block_align bytes per 2048 samples at 48 kHz. */
    vm_write32(puiBitrate, s->t.block_align * 8u * 48000u / AT3P_SAMPLES / 1000u);
    return CELL_OK;
}

s32 cellAtracGetLoopInfo(u32 pHandle, u32 piLoopNum, u32 puiLoopStatus)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    vm_write32(piLoopNum, (u32)s->loop_num);
    vm_write32(puiLoopStatus, s->t.loop_start >= 0);
    return CELL_OK;
}

s32 cellAtracSetLoopNum(u32 pHandle, s32 iLoopNum)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    if (s->t.loop_start < 0 && iLoopNum != 0) return (s32)CELL_ATRAC_ERROR_UNSET_LOOP_NUM;
    s->loop_num = iLoopNum;
    return CELL_OK;
}

/* The file offset a seek to decoder-stream index p reads from: one frame
 * early, because decode_frame restarts the decoder one frame back. */
static u32 reset_read_pos(const AtracSlot* s, s32 p)
{
    const s32 k = p / AT3P_SAMPLES;
    return s->t.data_off + (u32)(k > 0 ? k - 1 : 0) * s->t.block_align;
}

s32 cellAtracResetPlayPosition(u32 pHandle, u32 uiSample, u32 uiWriteByte)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    const s32 p = s->t.first + (s32)uiSample;
    if (p >= s->t.end) return (s32)CELL_ATRAC_ERROR_ILLEGAL_SAMPLE;
    s->pos = p;
    if (s->stream && s->missing && uiWriteByte) {
        /* The title refilled the ring from its start, reading from the
         * offset GetBufferInfoForResetting gave it. */
        const u32 f = reset_read_pos(s, p);
        if (uiWriteByte > s->buf_bytes) uiWriteByte = s->buf_bytes;
        s->ring_base = f;
        s->run_start = f;
        s->rd = f + uiWriteByte;
        stream_take(s, f, s->rd);
    }
    return CELL_OK;
}

/* Where the title should put its next chunk. The ring holds the run from the
 * frame being decoded up to rd; the rest of it is free. */
static void stream_window(AtracSlot* s, u32* write_ea, u32* writable, u32* read_pos)
{
    *write_ea = s->buf_ea; *writable = 0; *read_pos = s->rd;
    if (!s->missing) return;
    const u32 rk = s->rd >= s->t.data_off ? (s->rd - s->t.data_off) / s->t.block_align : 0;
    if (s->rd >= s->file_bytes || rk >= frame_count(s) ||
        (s->rd >= s->t.data_off && s->have[rk])) {
        /* The run reached the end, or data that already arrived: start a new
         * run at the next gap, wrapping to the start of the file. */
        u32 f = next_missing(s, s->rd < s->file_bytes ? s->rd : 0);
        if (f >= s->file_bytes) f = next_missing(s, 0);
        s->ring_base = f; s->run_start = f; s->rd = f;
    }
    const u32 cur = s->t.data_off + (u32)(s->pos / AT3P_SAMPLES) * s->t.block_align;
    const u32 held = (s->rd > cur && cur >= s->run_start) ? s->rd - cur : 0;
    if (held >= s->buf_bytes) return;
    const u32 ring = (s->rd - s->ring_base) % s->buf_bytes;
    u32 n = s->buf_bytes - held;
    if (n > s->buf_bytes - ring) n = s->buf_bytes - ring;      /* contiguous */
    if (n > s->file_bytes - s->rd) n = s->file_bytes - s->rd;
    *write_ea = s->buf_ea + ring; *writable = n;
}

s32 cellAtracGetStreamDataInfo(u32 pHandle, u32 ppucWritePointer, u32 puiWritableByte,
                               u32 puiReadPosition)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    u32 w = s->buf_ea, n = 0, r = s->t.data_off + s->t.data_bytes;
    if (s->stream) stream_window(s, &w, &n, &r);
    if (ppucWritePointer) vm_write32(ppucWritePointer, w);
    if (puiWritableByte)  vm_write32(puiWritableByte, n);
    if (puiReadPosition)  vm_write32(puiReadPosition, r);
    return CELL_OK;
}

s32 cellAtracAddStreamData(u32 pHandle, u32 uiAddByte)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    if (!s->stream) return (s32)CELL_ATRAC_ERROR_ALLDATA_IS_ONMEMORY;
    if (!uiAddByte) return CELL_OK;
    u32 w, n, r;
    stream_window(s, &w, &n, &r);
    if (uiAddByte > n) return (s32)CELL_ATRAC_ERROR_READSIZE_OVER_BUFFER;
    stream_take(s, s->rd, s->rd + uiAddByte);
    s->rd += uiAddByte;
    return CELL_OK;
}

s32 cellAtracGetVacantSize(u32 pHandle, u32 puiVacantSize)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    u32 w, n = 0, r;
    if (s->stream) stream_window(s, &w, &n, &r);
    vm_write32(puiVacantSize, n);
    return CELL_OK;
}

/* The whole file ends up in host memory, so a loop never needs the console's
 * second buffer for the data past its end. */
s32 cellAtracIsSecondBufferNeeded(u32 pHandle)
{
    return slot_of(pHandle) ? 0 : (s32)CELL_ATRAC_ERROR_UNSET_DATA;
}

s32 cellAtracGetSecondBufferInfo(u32 pHandle, u32 puiReadPosition, u32 puiDataByte)
{
    if (!slot_of(pHandle)) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    if (puiReadPosition) vm_write32(puiReadPosition, 0);
    if (puiDataByte)     vm_write32(puiDataByte, 0);
    return CELL_OK;
}

s32 cellAtracSetSecondBuffer(u32 pHandle, u32 pucSecondBufferAddr, u32 uiSecondBufferByte)
{
    (void)pucSecondBufferAddr; (void)uiSecondBufferByte;
    return slot_of(pHandle) ? CELL_OK : (s32)CELL_ATRAC_ERROR_UNSET_DATA;
}

/* CellAtracBufferInfo { pucWriteAddr, uiWritableByte, uiMinWriteByte,
 * uiReadPosition }: a seek refills the ring from its start. */
s32 cellAtracGetBufferInfoForResetting(u32 pHandle, u32 uiSample, u32 pBufferInfo)
{
    AtracSlot* s = slot_of(pHandle);
    if (!s) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    const s32 p = s->t.first + (s32)uiSample;
    if (p >= s->t.end) return (s32)CELL_ATRAC_ERROR_ILLEGAL_SAMPLE;
    u32 n = 0, f = s->t.data_off + s->t.data_bytes, min = 0;
    if (s->stream && s->missing) {
        f = reset_read_pos(s, p);
        n = s->buf_bytes;
        if (n > s->file_bytes - f) n = s->file_bytes - f;
        min = 2 * s->t.block_align < n ? 2 * s->t.block_align : n;
    }
    vm_write32(pBufferInfo + 0, s->stream ? s->buf_ea : (u32)(s->file - vm_base));
    vm_write32(pBufferInfo + 4, n);
    vm_write32(pBufferInfo + 8, min);
    vm_write32(pBufferInfo + 12, f);
    return CELL_OK;
}

s32 cellAtracGetInternalErrorInfo(u32 pHandle, u32 piResult)
{
    if (!slot_of(pHandle)) return (s32)CELL_ATRAC_ERROR_UNSET_DATA;
    vm_write32(piResult, 0);
    return CELL_OK;
}
