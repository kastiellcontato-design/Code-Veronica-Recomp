/* Taskset ownership and ready-task collection. Kept separate so the actual
 * scheduling code can be exercised without loading a game or starting SPUs. */
#include "spu_workload.h"
#include "../platform/win32_compat.h"
#include "../../libs/spurs/spurs_taskset.h"

/* Minimal taskset scheduling for tasks the PPU never created.
 *
 * SPU code can create tasks itself, or mark existing ones ready, and rely on
 * the taskset policy module to schedule them. Tasks created from the PPU are
 * dispatched by our cellSpursCreateTask, but nothing looked at the taskset
 * afterwards, so SPU-scheduled work never ran. Guitar Hero III's Havok task
 * hands the rest of each physics step to other tasks and EXITs, and the step
 * then waits forever.
 *
 * Runs when a task of the taskset returns (our runner returns on the EXIT
 * syscall; waiting tasks park instead). The returning task's ready, pending
 * and enabled bits are cleared, as the real policy does on exit, and every
 * other enabled task that is pending or ready and not already running here is
 * started from its ELF.
 *
 * Taskset layout: bitsets of 16 bytes at +0x10 ready, +0x20 pending,
 * +0x30 enabled (task 0 = MSB of byte 0); task_info[t] at +0x80 + 0x30*t with
 * the ELF EA at +0x14 and the context save EA at +0x1C. */
#define TS_TRACK 8
static struct { uint32_t ea; uint8_t running[16]; } s_ts_run[TS_TRACK];
static volatile LONG s_ts_lock;
static uint8_t* ts_running(uint32_t taskset_ea)
{
    for (int i = 0; i < TS_TRACK; i++) if (s_ts_run[i].ea == taskset_ea) return s_ts_run[i].running;
    for (int i = 0; i < TS_TRACK; i++) if (!s_ts_run[i].ea) { s_ts_run[i].ea = taskset_ea; return s_ts_run[i].running; }
    return NULL;
}
static void ts_lock(void)   { while (InterlockedExchange(&s_ts_lock, 1)) Sleep(0); }
static void ts_unlock(void) { InterlockedExchange(&s_ts_lock, 0); }

static void spu_taskset_mark(uint32_t taskset_ea, uint32_t t, int on)
{
    if (!taskset_ea || t >= 128) return;
    ts_lock();
    uint8_t* r = ts_running(taskset_ea);
    uint8_t m = (uint8_t)(0x80u >> (t & 7));
    if (r) { if (on) r[t / 8] |= m; else r[t / 8] &= (uint8_t)~m; }
    ts_unlock();
}

/* Reserve before CreateThread: a ready task can also be discovered by an
 * exiting sibling. Both paths must share this claim, not mark it only after
 * the new host thread starts, or they execute the same SPU DMA twice. */
static int spu_taskset_claim(uint32_t taskset_ea, uint32_t t)
{
    if (!taskset_ea || t >= 128) return 1;
    ts_lock();
    uint8_t* r = ts_running(taskset_ea);
    uint8_t m = (uint8_t)(0x80u >> (t & 7));
    int claimed = r && !(r[t / 8] & m);
    if (claimed) r[t / 8] |= m;
    ts_unlock();
    return claimed;
}

void spu_workload_task_exited(uint32_t taskset_ea, uint32_t done_task)
{
    extern uint8_t* vm_base;
    struct { uint32_t id, elf, ctx, size; } pending[128];
    uint32_t npending=0;
    ts_lock();
    uint8_t* ts = vm_base + taskset_ea;
    uint8_t* run = ts_running(taskset_ea);
    if (done_task < 128) {
        uint8_t m = (uint8_t)(0x80u >> (done_task & 7));
        ts[0x10 + done_task / 8] &= (uint8_t)~m;
        ts[0x20 + done_task / 8] &= (uint8_t)~m;
        ts[0x30 + done_task / 8] &= (uint8_t)~m;
        if (run) run[done_task / 8] &= (uint8_t)~m;
    }
    for (uint32_t t = 0; t < 128 && run; t++) {
        uint8_t m = (uint8_t)(0x80u >> (t & 7));
        if (!(ts[0x30 + t / 8] & m) || (run[t / 8] & m)) continue;
        if (!((ts[0x10 + t / 8] | ts[0x20 + t / 8]) & m)) continue;
        const uint8_t* ti = ts + 0x80 + 0x30 * t;
        uint32_t elf = ((uint32_t)ti[0x14] << 24) | ((uint32_t)ti[0x15] << 16) |
                       ((uint32_t)ti[0x16] << 8) | ti[0x17];
        uint32_t ctx = ((uint32_t)ti[0x1C] << 24) | ((uint32_t)ti[0x1D] << 16) |
                       ((uint32_t)ti[0x1E] << 8) | ti[0x1F];
        if (!elf) continue;
        size_t sz = spu_elf_image_size(vm_base + elf, 2u * 1024 * 1024);
        if (!sz) continue;
        /* Selection AND ownership must be one locked operation. Otherwise two
         * exiting siblings can collect the same task; if the first run ends
         * before the second dispatcher claims it, the second starts consumed
         * work again. A late claim does not protect a stale ready snapshot. */
        run[t / 8] |= m;
        ts[0x20 + t / 8] &= (uint8_t)~m;                 /* pending -> started */
        pending[npending].id=t; pending[npending].elf=elf;
        pending[npending].ctx=ctx; pending[npending].size=(uint32_t)sz;
        npending++;
    }
    ts_unlock();
    /* Ownership travels with the selected work across this unlocked handoff.
     * The common claimed dispatcher drops it if image/thread creation fails. */
    for (uint32_t i=0;i<npending;i++) {
        spu_workload_dispatch_task_claimed(vm_base+pending[i].elf, pending[i].size,
            pending[i].ctx, taskset_ea, pending[i].id);
    }
}

int spu_workload_task_preclaim(uint32_t taskset_ea, uint32_t taskid)
{
    return spu_taskset_claim(taskset_ea, taskid);
}

int spu_workload_task_reserve(uint32_t taskset_ea)
{
    if (!taskset_ea) return -1;
    int slot = -1;
    ts_lock();
    uint8_t* run = ts_running(taskset_ea);
    const uint8_t* enabled = vm_base + taskset_ea + CSTS_ENABLED;
    for (uint32_t t = 0; run && t < 128; t++) {
        uint8_t m = (uint8_t)(0x80u >> (t & 7));
        if (!((enabled[t / 8] | run[t / 8]) & m)) {
            run[t / 8] |= m; slot = (int)t; break;
        }
    }
    ts_unlock();
    return slot;
}

void spu_workload_task_publish(uint32_t taskset_ea, uint32_t taskid,
    uint64_t elf, uint64_t context, const uint32_t args[4], const uint32_t pattern[4])
{
    /* add_task updates whole BE words while task exit clears individual bytes.
     * They must share the same lock, or publication can restore an exited
     * task's enabled/ready bits from a stale read-modify-write. */
    ts_lock();
    spurs_taskset_add_task(taskset_ea, taskid, elf, context, args, pattern);
    ts_unlock();
}

void spu_workload_task_unclaim(uint32_t taskset_ea, uint32_t taskid)
{
    spu_taskset_mark(taskset_ea, taskid, 0);
}
