/*
 * ps3recomp - cellPamf HLE implementation
 *
 * Parses PAMF container headers to extract stream information.
 * The actual PAMF header format is parsed to provide correct stream
 * counts and codec parameters to cellDmux/cellVdec/cellAdec.
 */

#include "cellPamf.h"
#include "../../runtime/ppu/ppu_memory.h"   /* GUEST_PTR, vm_write*: guest EA -> host pointer */
#include <stdio.h>
#include <string.h>

/* PAMF 0040/0041: header/data sizes at 8/12 are in 2048-byte sectors.
 * Sequence info starts at 0x50; the first grouping period/group put their
 * count at 0x87 and their 48-byte stream descriptors at 0x88. */

#define PAMF_OFF_MAGIC            0x00
#define PAMF_OFF_VERSION          0x04
#define PAMF_OFF_DATA_OFFSET      0x08
#define PAMF_OFF_DATA_SIZE        0x0C
#define PAMF_OFF_STREAM_TABLE     0x10
#define PAMF_OFF_NUM_STREAMS      0x14
#define PAMF_OFF_PRESENT_START    0x58
#define PAMF_OFF_PRESENT_END      0x5E
#define PAMF_OFF_MUX_RATE         0x62
#define PAMF_OFF_EP_OFFSET        0x24
#define PAMF_OFF_EP_COUNT         0x28

/* Minimum valid header size (enough to read all fields above) */
#define PAMF_MIN_HEADER_SIZE      0x88

/* Stream descriptor size (fixed 24 bytes per descriptor) */
#define PAMF_STREAM_DESC_SIZE     48

/* Raw stream type values in the PAMF file (different from SDK constants!) */
#define PAMF_RAW_TYPE_AVC         0x1B
#define PAMF_RAW_TYPE_M2V         2
#define PAMF_RAW_TYPE_ATRAC3PLUS  0xDC
#define PAMF_RAW_TYPE_AC3         0x81
#define PAMF_RAW_TYPE_LPCM        0x80
#define PAMF_RAW_TYPE_USERDATA    0xDD

/* ---------------------------------------------------------------------------
 * Endian helpers (PAMF is big-endian)
 * -----------------------------------------------------------------------*/
static u32 be32(const void* p)
{
    const u8* b = (const u8*)p;
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}

static u64 be64(const void* p)
{
    const u8* b = (const u8*)p;
    return ((u64)be32(b) << 32) | be32(b + 4);
}

static u16 be16(const void* p)
{
    const u8* b = (const u8*)p;
    return ((u16)b[0] << 8) | b[1];
}

/* Header queries use 2048-byte sectors and write guest big-endian u64s.
 * See the PAMF 0040/0041 header format, not the host Reader struct. */
static s32 pamf_query_sizes(void* pamfAddr, u64 fileSize, u64* header, u64* data)
{
    if (!pamfAddr) return (s32)0x80610503;
    const u8* base = GUEST_PTR(pamfAddr, const u8*);
    if (be32(base) != CELL_PAMF_MAGIC) return (s32)0x80610504;
    u32 version = be32(base + 4);
    if (version != 0x30303430u && version != 0x30303431u) return (s32)0x80610505;
    *header = (u64)be32(base + 8) * 2048;
    *data = (u64)be32(base + 12) * 2048;
    if (!*header || (fileSize && *header + *data != fileSize)) return (s32)0x80610502;
    return CELL_OK;
}

s32 cellPamfGetStreamOffsetAndSize(void* pamfAddr, u64 fileSize, u64* offset, u64* size)
{
    u64 h, d;
    s32 ret = pamf_query_sizes(pamfAddr, fileSize, &h, &d);
    fprintf(stderr, "[cellPamf] GetStreamOffsetAndSize(addr=%08X file=%llu) ret=%08X\n",
        (u32)(uintptr_t)pamfAddr, (unsigned long long)fileSize, (u32)ret);
    if (ret) return ret;
    if (offset) vm_write64((u32)(uintptr_t)offset, h);
    if (size) vm_write64((u32)(uintptr_t)size, d);
    return CELL_OK;
}

s32 cellPamfGetHeaderSize(void* pamfAddr, u64 fileSize, u64* size)
{
    return cellPamfGetStreamOffsetAndSize(pamfAddr, fileSize, size, NULL);
}

s32 cellPamfGetHeaderSize2(void* pamfAddr, u64 fileSize, u32 attribute, u64* size)
{
    u64 h, d;
    s32 ret = pamf_query_sizes(pamfAddr, fileSize, &h, &d);
    if (!ret && (attribute & 2)) {
        const u8* base = GUEST_PTR(pamfAddr, const u8*);
        u64 minimum = 0x54ull + be32(base + 0x50);
        if (minimum > h) ret = (s32)0x80610502;
        else h = minimum;
    }
    fprintf(stderr, "[cellPamf] GetHeaderSize2(addr=%08X file=%llu attr=%u) ret=%08X header=%llu\n",
        (u32)(uintptr_t)pamfAddr, (unsigned long long)fileSize, attribute,
        (u32)ret, ret ? 0ull : (unsigned long long)h);
    if (!ret && size) vm_write64((u32)(uintptr_t)size, h);
    return ret;
}

/* ---------------------------------------------------------------------------
 * Raw-type to SDK-type conversion
 * -----------------------------------------------------------------------*/
static s32 raw_type_to_sdk(u8 rawType)
{
    switch (rawType) {
    case PAMF_RAW_TYPE_AVC:        return CELL_PAMF_STREAM_TYPE_AVC;
    case PAMF_RAW_TYPE_M2V:        return CELL_PAMF_STREAM_TYPE_M2V;
    case PAMF_RAW_TYPE_ATRAC3PLUS: return CELL_PAMF_STREAM_TYPE_ATRAC3PLUS;
    case PAMF_RAW_TYPE_AC3:        return CELL_PAMF_STREAM_TYPE_AC3;
    case PAMF_RAW_TYPE_LPCM:       return CELL_PAMF_STREAM_TYPE_PAMF_LPCM;
    case PAMF_RAW_TYPE_USERDATA:   return CELL_PAMF_STREAM_TYPE_USER_DATA;
    default:                       return -1;
    }
}

/* ---------------------------------------------------------------------------
 * Reader state lives HOST-side, keyed by the guest EA.
 *
 * The title allocates a CellPamfReader and passes its address in. Our
 * CellPamfReader is a HOST-layout struct -- an 8-byte void* first member, 32
 * bytes total -- while the guest's is 28 bytes with a 4-byte pointer. So we
 * cannot simply translate the pointer and write through it: that stores four
 * bytes past the end of the caller's struct, and the pamfAddr field would hold
 * a 64-bit host pointer the guest could never use. Key off the EA instead and
 * never write the guest's copy; the title treats the reader as opaque.
 *
 * ponytail: linear scan over 8 slots. A title juggling more PAMF readers than
 * that wants a hash; nothing has needed even two.
 * -----------------------------------------------------------------------*/
#define PAMF_MAX_READERS 8
static struct { u32 ea; int in_use; CellPamfReader r; } s_readers[PAMF_MAX_READERS];

static CellPamfReader* pamf_host(CellPamfReader* guest_ea)
{
    u32 ea = (u32)(uintptr_t)guest_ea;
    if (!ea)
        return NULL;
    for (int i = 0; i < PAMF_MAX_READERS; i++)
        if (s_readers[i].in_use && s_readers[i].ea == ea)
            return &s_readers[i].r;
    for (int i = 0; i < PAMF_MAX_READERS; i++)
        if (!s_readers[i].in_use) {
            s_readers[i].in_use = 1;
            s_readers[i].ea = ea;
            memset(&s_readers[i].r, 0, sizeof(s_readers[i].r));
            return &s_readers[i].r;
        }
    return NULL;
}

/* --------------------------------------------------------------------------- 
 * Reader lifecycle
 * -----------------------------------------------------------------------*/

s32 cellPamfReaderInitialize(CellPamfReader* reader, void* pamfAddr,
                              u32 pamfSize, u32 attribute)
{
    reader = pamf_host(reader);
    (void)attribute;

    printf("[cellPamf] ReaderInitialize(addr=%p, size=%u)\n", pamfAddr, pamfSize);

    if (!reader || !pamfAddr || pamfSize < PAMF_MIN_HEADER_SIZE)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    pamfAddr = GUEST_PTR(pamfAddr, void*);
    const u8* base = (const u8*)pamfAddr;

    /* Verify magic */
    u32 magic = be32(base + PAMF_OFF_MAGIC);
    if (magic != CELL_PAMF_MAGIC) {
        printf("[cellPamf] Invalid PAMF magic: 0x%08X\n", magic);
        return (s32)CELL_PAMF_ERROR_INVALID_HEADER;
    }

    reader->pamfAddr = pamfAddr;
    reader->pamfSize = pamfSize;
    reader->dataOffset = be32(base + PAMF_OFF_DATA_OFFSET) * 2048u;
    reader->dataSize = be32(base + PAMF_OFF_DATA_SIZE) * 2048u;
    reader->streamTableOffset = 0x88; /* first grouping period, first group */
    reader->numStreams = base[0x87];
    if (!base[0x6F] || !base[0x81] || !reader->numStreams ||
        0x88u + (u32)reader->numStreams * 48u > 0x54u + be32(base + 0x50))
        return (s32)0x80610502;
    reader->currentStream = 0;
    reader->currentEP = 0;

    u32 muxRate = be32(base + PAMF_OFF_MUX_RATE);

    printf("[cellPamf] PAMF v%08X: %u streams, dataOffset=0x%X, dataSize=%u, "
           "streamTableOff=0x%X, muxRate=%u\n",
           be32(base + PAMF_OFF_VERSION),
           reader->numStreams, reader->dataOffset, reader->dataSize,
           reader->streamTableOffset, muxRate);

    /* Log each stream descriptor */
    for (u32 i = 0; i < reader->numStreams; i++) {
        const u8* desc = base + reader->streamTableOffset + i * PAMF_STREAM_DESC_SIZE;
        u8 rawType = desc[0];
        u8 channel = (desc[4] == 0xBD ? desc[5] : desc[4]) & 15;
        s32 sdkType = raw_type_to_sdk(rawType);
        printf("[cellPamf]   stream[%u]: rawType=%u (sdk=%d) channel=%u\n",
               i, rawType, sdkType, channel);
    }

    return CELL_OK;
}

s32 cellPamfReaderGetPresentationStartTime(CellPamfReader* reader, u64* startTime)
{
    reader = pamf_host(reader);
    if (!reader || !startTime)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    const u8* base = (const u8*)reader->pamfAddr;
    vm_write64((u32)(uintptr_t)startTime, ((u64)be16(base + 0x56) << 32) | be32(base + PAMF_OFF_PRESENT_START));
    return CELL_OK;
}

s32 cellPamfReaderGetPresentationEndTime(CellPamfReader* reader, u64* endTime)
{
    reader = pamf_host(reader);
    if (!reader || !endTime)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    const u8* base = (const u8*)reader->pamfAddr;
    vm_write64((u32)(uintptr_t)endTime, ((u64)be16(base + 0x5C) << 32) | be32(base + PAMF_OFF_PRESENT_END));
    return CELL_OK;
}

u32 cellPamfReaderGetMuxRateBound(CellPamfReader* reader)
{
    reader = pamf_host(reader);
    if (!reader)
        return 0;

    const u8* base = (const u8*)reader->pamfAddr;
    return be32(base + PAMF_OFF_MUX_RATE);
}

/* ---------------------------------------------------------------------------
 * Stream queries
 * -----------------------------------------------------------------------*/

s32 cellPamfReaderGetNumberOfStreams(CellPamfReader* reader)
{
    reader = pamf_host(reader);
    if (!reader)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    return reader->numStreams;
}

/* Get pointer to raw stream descriptor bytes by absolute index */
static const u8* get_stream_desc_raw(CellPamfReader* reader, u32 index)
{
    if (!reader || index >= reader->numStreams)
        return NULL;

    const u8* base = (const u8*)reader->pamfAddr;
    return base + reader->streamTableOffset + index * PAMF_STREAM_DESC_SIZE;
}

/* Get SDK stream type for a given absolute stream index */
static s32 get_stream_sdk_type(CellPamfReader* reader, u32 index)
{
    const u8* desc = get_stream_desc_raw(reader, index);
    if (!desc) return -1;
    return raw_type_to_sdk(desc[0]);
}

s32 cellPamfReaderGetNumberOfSpecificStreams(CellPamfReader* reader, u8 streamType)
{
    reader = pamf_host(reader);
    if (!reader)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    s32 count = 0;
    for (u32 i = 0; i < reader->numStreams; i++) {
        if (get_stream_sdk_type(reader, i) == (s32)streamType)
            count++;
    }
    return count;
}

s32 cellPamfReaderSetStreamWithType(CellPamfReader* reader, u8 streamType, u32 streamIndex)
{
    reader = pamf_host(reader);
    if (!reader)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    u32 found = 0;
    for (u32 i = 0; i < reader->numStreams; i++) {
        if (get_stream_sdk_type(reader, i) == (s32)streamType) {
            if (found == streamIndex) {
                reader->currentStream = (u8)i;
                return CELL_OK;
            }
            found++;
        }
    }
    return (s32)CELL_PAMF_ERROR_STREAM_NOT_FOUND;
}

s32 cellPamfReaderSetStreamWithTypeAndChannel(CellPamfReader* reader, u8 streamType, u32 channel)
{
    /* Forward the GUEST pointer untouched: cellPamfReaderSetStreamWithType
     * resolves it itself, and pamf_host keys on the guest EA -- handing it an
     * already-resolved host pointer would key a second, empty slot. */
    return cellPamfReaderSetStreamWithType(reader, streamType, channel);
}

s32 cellPamfReaderSetStreamWithIndex(CellPamfReader* reader, u32 streamIndex)
{
    reader = pamf_host(reader);
    if (!reader)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    if (streamIndex >= reader->numStreams)
        return (s32)CELL_PAMF_ERROR_STREAM_NOT_FOUND;

    reader->currentStream = (u8)streamIndex;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Sample rate / channel lookup helpers for audio descriptors
 * -----------------------------------------------------------------------*/
static u32 audio_sample_rate(u8 code)
{
    switch (code) {
    case 1: return 44100;
    case 2: return 48000;
    default: return 48000;
    }
}

static u8 audio_channels(u8 code)
{
    switch (code) {
    case 1: return 1;
    case 2: return 2;
    case 6: return 6;
    case 8: return 8;
    default: return 2;
    }
}

static u8 lpcm_bits(u8 code)
{
    switch (code) {
    case 1: return 16;
    case 2: return 24;
    default: return 16;
    }
}

/* Stream metadata is serialized in the SDK's guest layout, big endian.
 * Never write through a guest EA as if it were a native host pointer. */
s32 cellPamfReaderGetStreamInfo(CellPamfReader* reader, void* info, u32 infoSize)
{
    reader = pamf_host(reader);
    if (!reader || !info) return (s32)CELL_PAMF_ERROR_INVALID_ARG;
    const u8* desc = get_stream_desc_raw(reader, reader->currentStream);
    if (!desc) return (s32)CELL_PAMF_ERROR_STREAM_NOT_FOUND;
    s32 type = raw_type_to_sdk(desc[0]);
    const u8* d = desc + 16;
    u32 ea = (u32)(uintptr_t)info;
    u32 need = type == CELL_PAMF_STREAM_TYPE_M2V ? 24 : type == CELL_PAMF_STREAM_TYPE_AVC ? 32 : 8;
    if (infoSize < need) return (s32)CELL_PAMF_ERROR_INVALID_ARG;
    memset(GUEST_PTR(info, void*), 0, need);
    if (type == CELL_PAMF_STREAM_TYPE_M2V || type == CELL_PAMF_STREAM_TYPE_AVC) {
        int m2v = type == CELL_PAMF_STREAM_TYPE_M2V;
        vm_write8(ea, m2v ? (d[0] == 0x44 ? 3 : d[0] == 0x48 ? 1 : 255) : d[0]);
        if (!m2v) vm_write8(ea + 1, d[1]);
        u32 f = m2v ? 1 : 2;
        vm_write8(ea + f, d[2] >> 7);
        vm_write8(ea + f + 1, (d[2] >> 6) & 1);
        vm_write8(ea + f + 2, (d[2] & 15) - (m2v ? 0 : 1));
        vm_write8(ea + f + 3, d[3]);
        u32 w = 6;
        if (d[3] == 255) { vm_write16(ea + w, be16(d + 4)); vm_write16(ea + w + 2, be16(d + 6)); }
        vm_write16(ea + 10, d[9] * 16u);
        vm_write16(ea + 12, d[11] * 16u);
        if (m2v) {
            vm_write16(ea + 14, be16(d + 12)); vm_write16(ea + 16, be16(d + 14));
        } else {
            for (u32 i=0; i<4; ++i) vm_write16(ea + 14 + i*2, be16(d + 12 + i*2));
        }
        u32 v = m2v ? 18 : 22;
        if (d[2] & 0x40) {
            vm_write8(ea + v, d[20] >> 5); vm_write8(ea + v+1, (d[20] >> 4)&1);
            for (u32 i=0; i<3; ++i) vm_write8(ea + v+2+i, d[21+i]);
        }
        if (!m2v) {
            vm_write8(ea+27,d[24]>>7); vm_write8(ea+28,(d[24]>>6)&1);
            vm_write8(ea+29,(d[24]>>4)&3); vm_write8(ea+30,d[24]&3); vm_write8(ea+31,d[25]);
        }
        fprintf(stderr,"[cellPamf] StreamInfo type=%d %ux%u\n",type,d[9]*16u,d[11]*16u);
    } else if (type >= 2 && type <= 4) {
        vm_write32(ea, d[3] & 15); vm_write8(ea + 4, d[2] & 15);
        if (type == CELL_PAMF_STREAM_TYPE_PAMF_LPCM) vm_write16(ea + 6, d[4] >> 6);
    } else return (s32)CELL_PAMF_ERROR_NOT_AVAILABLE;
    return CELL_OK;
}

s32 cellPamfReaderGetStreamTypeAndChannel(CellPamfReader* reader, u8* type, u8* channel)
{
    reader = pamf_host(reader);
    if (!reader) return (s32)CELL_PAMF_ERROR_INVALID_ARG;
    const u8* d = get_stream_desc_raw(reader, reader->currentStream);
    if (!d) return (s32)CELL_PAMF_ERROR_STREAM_NOT_FOUND;
    s32 t = raw_type_to_sdk(d[0]);
    if (t < 0) return (s32)CELL_PAMF_ERROR_STREAM_NOT_FOUND;
    if (type) vm_write8((u32)(uintptr_t)type, (u8)t);
    if (channel) vm_write8((u32)(uintptr_t)channel, (d[4] == 0xBD ? d[5] : d[4]) & 15);
    return CELL_OK;
}

s32 cellPamfReaderGetStreamIndex(CellPamfReader* reader)
{
    reader = pamf_host(reader);
    if (!reader)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    return reader->currentStream;
}

s32 cellPamfStreamTypeToEsFilterId(u8 streamType, u8 streamIndex,
                                     void* esFilterId)
{
    if (!esFilterId || streamIndex > 15) return (s32)CELL_PAMF_ERROR_INVALID_ARG;
    u32 major, minor = 0;
    switch (streamType) {
    case CELL_PAMF_STREAM_TYPE_AVC:
    case CELL_PAMF_STREAM_TYPE_M2V: major = 0xE0 | streamIndex; break;
    case CELL_PAMF_STREAM_TYPE_ATRAC3PLUS: major = 0xBD; minor = streamIndex; break;
    case CELL_PAMF_STREAM_TYPE_PAMF_LPCM: major = 0xBD; minor = 0x40 | streamIndex; break;
    case CELL_PAMF_STREAM_TYPE_AC3: major = 0xBD; minor = 0x30 | streamIndex; break;
    case CELL_PAMF_STREAM_TYPE_USER_DATA: major = 0xBD; minor = 0x20 | streamIndex; break;
    default: return (s32)CELL_PAMF_ERROR_INVALID_ARG;
    }
    u32 ea = (u32)(uintptr_t)esFilterId;
    vm_write32(ea, major); vm_write32(ea+4, minor);
    vm_write32(ea+8, streamType == CELL_PAMF_STREAM_TYPE_AVC); vm_write32(ea+12, 0);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Entry point navigation
 * -----------------------------------------------------------------------*/

s32 cellPamfReaderGetNumberOfEp(CellPamfReader* reader, s32* numEp)
{
    reader = pamf_host(reader);
    if (!reader || !numEp)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    const u8* base = (const u8*)reader->pamfAddr;
    vm_write32((u32)(uintptr_t)numEp, be32(base + PAMF_OFF_EP_COUNT));
    return CELL_OK;
}

s32 cellPamfReaderGetEp(CellPamfReader* reader, u32 epIndex, CellPamfEp* ep)
{
    reader = pamf_host(reader);
    if (!reader || !ep)
        return (s32)CELL_PAMF_ERROR_INVALID_ARG;

    const u8* base = (const u8*)reader->pamfAddr;
    u32 epCount = be32(base + PAMF_OFF_EP_COUNT);
    if (epIndex >= epCount)
        return (s32)CELL_PAMF_ERROR_EP_NOT_FOUND;

    /* EP table entries are at epOffset, each 8 bytes (pts + offset) */
    u32 epTableOff = be32(base + PAMF_OFF_EP_OFFSET);
    const u8* epData = base + epTableOff + epIndex * 8;
    vm_write32((u32)(uintptr_t)ep + 0, be32(epData));
    vm_write32((u32)(uintptr_t)ep + 4, be32(epData + 4));
    return CELL_OK;
}
