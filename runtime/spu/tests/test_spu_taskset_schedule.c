/* Drive the real taskset exit collector, without running game code/threads.
 * A competing creator tries to take the selected slot exactly at handoff. */
#include "../spu_workload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

uint8_t* vm_base;
static unsigned dispatched, stolen, bad;
enum { TS = 0x10000, ELF_EA = 0x20000 };
enum { PUB_TS = 0x14000 };
static HANDLE read_ready, exit_finished;
static int inject_publication;
static DWORD WINAPI finish_old_task(LPVOID unused)
{
    (void)unused;
    WaitForSingleObject(read_ready,INFINITE);
    spu_workload_task_exited(PUB_TS,0);
    SetEvent(exit_finished);
    return 0;
}

/* Simulate the builder's real 32-bit read/modify/write, with a deterministic
 * context switch between the read and write. The scheduler wrapper itself
 * is production code; only the builder's RMW is simulated here. */
void spurs_taskset_add_task(uint32_t ts,uint32_t id,uint64_t elf,uint64_t context,
    const uint32_t args[4],const uint32_t pattern[4])
{
    (void)elf; (void)context; (void)args; (void)pattern;
    uint32_t ea=ts+0x30+(id/32)*4;
    uint32_t old=((uint32_t)vm_base[ea]<<24)|((uint32_t)vm_base[ea+1]<<16)
                |((uint32_t)vm_base[ea+2]<<8)|vm_base[ea+3];
    if(inject_publication) {
        SetEvent(read_ready);
        WaitForSingleObject(exit_finished,100); /* must remain blocked by publisher */
    }
    uint32_t v=old|((1u<<31)>>(id%32));
    vm_base[ea]=(uint8_t)(v>>24); vm_base[ea+1]=(uint8_t)(v>>16);
    vm_base[ea+2]=(uint8_t)(v>>8); vm_base[ea+3]=(uint8_t)v;
}

static void be32(uint32_t ea, uint32_t v)
{
    vm_base[ea]=(uint8_t)(v>>24); vm_base[ea+1]=(uint8_t)(v>>16);
    vm_base[ea+2]=(uint8_t)(v>>8); vm_base[ea+3]=(uint8_t)v;
}
static void task(uint32_t id)
{
    uint8_t m=(uint8_t)(0x80u>>(id&7));
    vm_base[TS+0x30+id/8]|=m;
    vm_base[TS+0x10+id/8]|=m;
    be32(TS+0x80+0x30*id+0x14, ELF_EA);
    be32(TS+0x80+0x30*id+0x1C, 0x30000+id*128);
}
void spu_taskset_test_handoff(uint32_t ts, uint32_t id, uint32_t elf, uint32_t ctx)
{
    dispatched++;
    if (ts!=TS || elf!=ELF_EA || ctx!=0x30000+id*128) bad++;
    /* Old collector only took a snapshot under its lock. A sibling or PPU
     * can start/finish this task in the gap, and the stale snapshot then
     * starts it again. The corrected collector must already own the slot. */
    if (spu_workload_task_preclaim(ts,id)) { stolen++; spu_workload_task_unclaim(ts,id); }
    /* A simultaneous sibling exit must not collect this or the other slots
     * already selected by the outer collector (they are all reserved). */
    if (dispatched==1) spu_workload_task_exited(ts,128);
}

/* Only execution/image loading is faked. The scheduler and ownership lock
 * are the production implementation linked into this test. */
size_t spu_elf_image_size(const uint8_t* image, size_t max_avail)
{
    (void)max_avail;
    return !memcmp(image,"\177ELF\1\2",6) ? 0x100 : 0;
}
int spu_workload_dispatch_task_async(const uint8_t* image, uint32_t size,
    uint32_t ctx, uint32_t ts, uint32_t id)
{
    (void)size;
    spu_taskset_test_handoff(ts,id,(uint32_t)(image-vm_base),ctx); return 1;
}
int spu_workload_dispatch_task_claimed(const uint8_t* image, uint32_t size,
    uint32_t ctx, uint32_t ts, uint32_t id)
{
    return spu_workload_dispatch_task_async(image,size,ctx,ts,id);
}

int main(void)
{
    vm_base=calloc(1,0x40000); if(!vm_base) return 2;
    memcpy(vm_base+ELF_EA,"\177ELF\1\2",6);
    be32(ELF_EA+0x20,0x100); /* ELF extent, no program/section entries */
    task(0); task(1); task(7); task(8); task(127);
    /* A task already executing elsewhere must be excluded. */
    if(!spu_workload_task_preclaim(TS,8)) return 2;
    spu_workload_task_exited(TS,0);
    if(dispatched!=3 || stolen || bad) {
        fprintf(stderr,"FAIL: dispatched=%u (expected 3), stolen=%u (expected 0), bad=%u\n",
            dispatched,stolen,bad); return 1;
    }
    /* Claims survive the unlocked handoff and reject a second collector. */
    spu_workload_task_exited(TS,128);
    if(dispatched!=3) return 1;
    /* Finishing and explicitly re-creating a slot makes it runnable again. */
    spu_workload_task_exited(TS,1); task(1);
    spu_workload_task_exited(TS,128);
    if(dispatched!=4 || stolen || bad) return 1;
    /* Adjacent bits/another taskset remain independent. */
    if(!spu_workload_task_preclaim(TS+0x2000,1)) return 1;
    if(!(vm_base[TS+0x30+127/8] & 1) || (vm_base[TS+0x30]&0x80)) return 1;
    /* Slot reservation must distinguish unpublished reservations and enabled
     * tasks; two creators can never choose the same slot. */
    if(spu_workload_task_reserve(PUB_TS)!=0) return 1;
    if(spu_workload_task_reserve(PUB_TS)!=1) return 1;
    spu_workload_task_unclaim(PUB_TS,0);
    vm_base[PUB_TS+0x30]=0x80; /* task 0 enabled (not otherwise runnable) */
    if(spu_workload_task_reserve(PUB_TS)!=2) return 1;
    /* Exit must not race with a publisher and have its cleared bit restored
     * from a stale whole-word read. */
    read_ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    exit_finished=CreateEventA(NULL,TRUE,FALSE,NULL);
    HANDLE finisher=CreateThread(NULL,0,finish_old_task,NULL,0,NULL);
    if(!read_ready || !exit_finished || !finisher) return 2;
    inject_publication=1;
    spu_workload_task_publish(PUB_TS,1,ELF_EA,0,NULL,NULL);
    if(WaitForSingleObject(finisher,2000)!=WAIT_OBJECT_0) return 1;
    CloseHandle(finisher); CloseHandle(read_ready); CloseHandle(exit_finished);
    if(vm_base[PUB_TS+0x30]!=0x40) {
        fprintf(stderr,"FAIL: publishing a new task restored exited task 0 (enabled=%02X, expected 40)\n",
            vm_base[PUB_TS+0x30]); return 1;
    }
    free(vm_base);
    puts("SPU taskset schedule: PASS (handoff race, sibling race, running exclusion, slot reuse, taskset isolation)");
    return 0;
}
