#include "spu_fault_snapshot.h"

#ifdef _WIN32
#include <windows.h>

extern uint8_t* vm_base;

enum { FAULT_EVENTS = 512 };
static struct {
    uint64_t seq, ms;
    DWORD thread;
    char phase[16];
    uint32_t taskset, task, detail, arg0, arg1, counter;
} fault_events[FAULT_EVENTS];
static uint64_t fault_event_seq;
static SRWLOCK fault_event_lock = SRWLOCK_INIT;
static volatile LONG fault_target_taskset;
static INIT_ONCE fault_event_once = INIT_ONCE_STATIC_INIT;
static int fault_events_enabled;

static BOOL CALLBACK fault_event_init(PINIT_ONCE once, PVOID param, PVOID* context)
{
    (void)once; (void)param; (void)context;
    const char* dir = getenv("SPU_FAULT_DIR");
    fault_events_enabled = dir && *dir;
    return TRUE;
}

static uint32_t fault_be32(const uint8_t* p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

void spu_fault_event(const char* phase, uint32_t ts, uint32_t task, int image, uint32_t detail)
{
    InitOnceExecuteOnce(&fault_event_once, fault_event_init, NULL, NULL);
    if (!fault_events_enabled || !ts || task >= 128) return;
    if (image == 10) InterlockedCompareExchange(&fault_target_taskset, (LONG)ts, 0);
    if ((uint32_t)fault_target_taskset != ts) return;
    uint8_t bytes[16]; SIZE_T got;
    uint32_t a0 = 0, a1 = 0, counter = 0xFFFFFFFFu;
    uint64_t ti = (uint64_t)ts + 0x80 + task * 48;
    if (vm_base && ti + 16 <= 0x100000000ULL &&
        ReadProcessMemory(GetCurrentProcess(), vm_base + ti, bytes, 16, &got) && got == 16) {
        a0 = fault_be32(bytes); a1 = fault_be32(bytes + 4);
        if (a1 >= 0x10000 && a1 <= 0xFFFFFFFCu &&
            ReadProcessMemory(GetCurrentProcess(), vm_base + a1, bytes, 4, &got) && got == 4)
            counter = fault_be32(bytes);
    }
    AcquireSRWLockExclusive(&fault_event_lock);
    uint64_t seq = ++fault_event_seq;
    unsigned i = (unsigned)((seq - 1) % FAULT_EVENTS);
    fault_events[i].seq = seq; fault_events[i].ms = GetTickCount64();
    fault_events[i].thread = GetCurrentThreadId();
    snprintf(fault_events[i].phase, sizeof(fault_events[i].phase), "%s", phase);
    fault_events[i].taskset = ts; fault_events[i].task = task; fault_events[i].detail = detail;
    fault_events[i].arg0 = a0; fault_events[i].arg1 = a1; fault_events[i].counter = counter;
    ReleaseSRWLockExclusive(&fault_event_lock);
}

static void fault_save_events(const char* path)
{
    char event_path[1100];
    snprintf(event_path, sizeof(event_path), "%s.events.txt", path);
    FILE* f = fopen(event_path, "w");
    if (!f) return;
    fprintf(f, "SPU task history (counter is a concurrent observation; FFFFFFFF = unreadable)\n");
    AcquireSRWLockShared(&fault_event_lock);
    uint64_t first = fault_event_seq > FAULT_EVENTS ? fault_event_seq - FAULT_EVENTS + 1 : 1;
    for (uint64_t seq = first; seq <= fault_event_seq; seq++) {
        unsigned i = (unsigned)((seq - 1) % FAULT_EVENTS);
        fprintf(f, "%llu ms=%llu host=%lu %-15s ts=%08X task=%u detail=%08X args=%08X,%08X counter=%08X\n",
            fault_events[i].seq, fault_events[i].ms, fault_events[i].thread, fault_events[i].phase,
            fault_events[i].taskset, fault_events[i].task, fault_events[i].detail,
            fault_events[i].arg0, fault_events[i].arg1, fault_events[i].counter);
    }
    ReleaseSRWLockShared(&fault_event_lock);
    fclose(f);
}

/* A single self-contained file: 16-byte magic, 16 LE u32 header words,
 * 128*4 LE u32 register words, 256KB raw (guest-endian) LS, then records
 * {LE u32 guest EA, LE u32 byte count, raw bytes} until EOF. Guest memory
 * is read through ReadProcessMemory: diagnostics must not commit a bogus
 * pointer or raise a new exception while investigating an existing fault. */
typedef struct {
    FILE* file;
    uint32_t seen[520];
    unsigned count;
    int ok;
} fault_writer;

static void fault_write(fault_writer* w, const void* p, size_t n)
{
    if (w->ok && fwrite(p, 1, n, w->file) != n) w->ok = 0;
}

static void fault_guest(fault_writer* w, uint32_t ea, uint32_t n)
{
    uint8_t bytes[0x4100];
    if (!vm_base || ea < 0x10000 || !n || n > sizeof(bytes)) return;
    if ((uint64_t)ea + n > 0x100000000ULL)
        n = (uint32_t)(0x100000000ULL - ea);
    for (unsigned i = 0; i < w->count; i++) if (w->seen[i] == ea) return;
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), vm_base + ea, bytes, n, &got)
            || got != n) return;
    if (w->count == sizeof(w->seen) / sizeof(w->seen[0])) return;
    w->seen[w->count++] = ea;
    fault_write(w, &ea, sizeof(ea));
    fault_write(w, &n, sizeof(n));
    fault_write(w, bytes, n);
}

int spu_fault_snapshot(const spu_context* ctx, uint32_t taskset,
                       uint32_t task, uint32_t args)
{
    const char* dir = getenv("SPU_FAULT_DIR");
    if (!ctx || !ctx->ls || !dir || !*dir) return 0;
    static volatile LONG sequence;
    LONG seq = InterlockedIncrement(&sequence);
    if (seq > 2) return 0;
    char path[1024];
    int len = snprintf(path, sizeof(path), "%s/spu-%lu-%llu-%ld.spufault", dir,
        GetCurrentProcessId(), (unsigned long long)GetTickCount64(), seq);
    if (len < 0 || (size_t)len >= sizeof(path)) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[spu-fault] could not create snapshot: %s\n", path);
        return 0;
    }
    fault_writer w = {0}; w.file = f; w.ok = 1;
    const char magic[16] = "SPUFAULT-v1";
    uint32_t header[16] = {1, GetCurrentProcessId(), GetCurrentThreadId(),
        (uint32_t)ctx->image_id, ctx->pc, taskset, task, args,
        ctx->mfc_lsa, ctx->mfc_eah, ctx->mfc_eal, ctx->mfc_size,
        ctx->mfc_tag, ctx->mfc_tag_mask, ctx->mfc_tag_status, SPU_LS_SIZE};
    fault_write(&w, magic, sizeof(magic));
    fault_write(&w, header, sizeof(header));
    for (unsigned i = 0; i < 128; i++)
        fault_write(&w, ctx->gpr[i]._u32, 4 * sizeof(uint32_t));
    fault_write(&w, ctx->ls, SPU_LS_SIZE);
    /* Preserve the transfer's full source, then task metadata and pointer
     * neighborhoods. Hard bounds keep even corrupt descriptors under 450KB. */
    if (!ctx->mfc_eah)
        fault_guest(&w, ctx->mfc_eal, ctx->mfc_size <= 0x4000 ? ctx->mfc_size : 0x4000);
    if (taskset && taskset <= 0xFFFFE000u) fault_guest(&w, taskset, 0x1880);
    fault_guest(&w, args, 256);
    /* By-value entry argument is retained in the LS TaskInfo at 0x2780. */
    for (unsigned i = 0x2780; i < 0x2790; i += 4) {
        uint32_t ea = ((uint32_t)ctx->ls[i] << 24) | ((uint32_t)ctx->ls[i+1] << 16)
                    | ((uint32_t)ctx->ls[i+2] << 8) | ctx->ls[i+3];
        fault_guest(&w, ea, 256);
    }
    for (unsigned i = 0; i < 128; i++)
        for (unsigned j = 0; j < 4; j++) fault_guest(&w, ctx->gpr[i]._u32[j], 256);
    int ok = w.ok;
    if (fclose(f)) ok = 0;
    fault_save_events(path);
    fprintf(stderr, "[spu-fault] %s snapshot %s (img=%d pc=%05X task=%u, %u RAM regions)\n",
        ok ? "saved" : "incomplete", path, ctx->image_id, ctx->pc, task, w.count);
    fflush(stderr);
    return ok;
}
#else
void spu_fault_event(const char* phase, uint32_t taskset, uint32_t task, int image, uint32_t detail)
{
    (void)phase; (void)taskset; (void)task; (void)image; (void)detail;
}
int spu_fault_snapshot(const spu_context* ctx, uint32_t taskset,
                       uint32_t task, uint32_t args)
{
    (void)ctx; (void)taskset; (void)task; (void)args;
    return 0;
}
#endif
