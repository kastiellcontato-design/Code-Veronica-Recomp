/*
 * ps3recomp - cellFiber HLE implementation
 *
 * Implements PPU fibers using Windows Fibers API or POSIX ucontext.
 * Each CellFiber maps to a native fiber for true cooperative switching.
 */

/* Darwin gates the ucontext routines AND the shape of ucontext_t itself on
 * _XOPEN_SOURCE, so this has to come before the first system header any
 * include below reaches -- see the static assertion further down for what
 * defining it late costs. Nothing above this line. */
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#  define _XOPEN_SOURCE 600
#endif

#include "cellFiber.h"
#include "../../runtime/ppu/ppu_fiber_bridge.h"
#include "../../runtime/ppu/ppu_memory.h"   /* GUEST_PTR, vm_write*: guest EA -> host pointer */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ucontext.h>

#if defined(__APPLE__)
/* getcontext() points uc_mcontext at ucontext_t's own __mcontext_data member
 * and writes uc_mcsize (816) bytes there. That member is declared only when
 * _XOPEN_SOURCE was defined before <sys/_types/_ucontext.h> was first pulled
 * in; without it the struct is the 64-byte header alone and every getcontext,
 * swapcontext and makecontext writes 752 bytes off the end of it -- over
 * FiberSlot::stack, over the next FiberSlot in the array, and past
 * s_scheduler_context into whatever .bss follows. The symptom is not a crash
 * at the write but a fiber that starts with somebody else's argument, or a
 * slot that reports itself in use having never been created.
 *
 * The define above is the fix; this is the tripwire, because moving an
 * #include up here would silently undo it. */
_Static_assert(sizeof(ucontext_t) > 64,
               "ucontext_t has no embedded machine state: _XOPEN_SOURCE was "
               "defined too late and getcontext will write past the struct");
#endif
#endif

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

typedef struct FiberSlot {
    int        in_use;
    u32        state;
    CellFiberEntry entry;
    u64        arg;
    u32        stack_size;
    char       name[32];
    u32        priority;
#ifdef _WIN32
    LPVOID     native_fiber;
#else
    ucontext_t context;
    u8*        stack;
#endif
} FiberSlot;

static FiberSlot s_fibers[CELL_FIBER_MAX_FIBERS];
static int s_initialized = 0;
static s32 s_current_fiber = -1; /* index into s_fibers, or -1 for scheduler */

#ifdef _WIN32
static LPVOID s_scheduler_fiber = NULL;
#else
static ucontext_t s_scheduler_context;
#endif

/* ---------------------------------------------------------------------------
 * Fiber trampoline
 * -----------------------------------------------------------------------*/

#ifdef _WIN32
static void CALLBACK fiber_trampoline(LPVOID param)
{
    u32 idx = (u32)(uintptr_t)param;
    FiberSlot* f = &s_fibers[idx];
    f->state = CELL_FIBER_STATE_RUNNING;
    f->entry(f->arg);
    f->state = CELL_FIBER_STATE_TERMINATED;

    /* Return to scheduler when fiber function returns */
    s_current_fiber = -1;
    SwitchToFiber(s_scheduler_fiber);
}
#else
static void fiber_trampoline(int idx_lo, int idx_hi)
{
    u32 idx = ((u32)idx_hi << 16) | (u32)(u16)idx_lo;
    FiberSlot* f = &s_fibers[idx];
    f->state = CELL_FIBER_STATE_RUNNING;
    f->entry(f->arg);
    f->state = CELL_FIBER_STATE_TERMINATED;
    s_current_fiber = -1;
    setcontext(&s_scheduler_context);
}
#endif

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

s32 cellFiberPpuInitialize(void)
{
    printf("[cellFiber] Initialize()\n");

    if (s_initialized)
        return (s32)CELL_FIBER_ERROR_STAT;

    memset(s_fibers, 0, sizeof(s_fibers));
    s_current_fiber = -1;

#ifdef _WIN32
    /* Convert the current thread to a fiber so we can switch */
    s_scheduler_fiber = ConvertThreadToFiber(NULL);
    if (!s_scheduler_fiber) {
        /* Already a fiber (e.g., re-init) */
        s_scheduler_fiber = GetCurrentFiber();
    }
#else
    getcontext(&s_scheduler_context);
#endif

    s_initialized = 1;
    return CELL_OK;
}

/* Firmware imports the underscored export; keep the friendly SDK alias too. */
s32 _cellFiberPpuInitialize(void)
{
    return cellFiberPpuInitialize();
}

s32 cellFiberPpuFinalize(void)
{
    printf("[cellFiber] Finalize()\n");

    if (!s_initialized)
        return (s32)CELL_FIBER_ERROR_STAT;

    for (int i = 0; i < CELL_FIBER_MAX_FIBERS; i++) {
        if (s_fibers[i].in_use) {
#ifdef _WIN32
            if (s_fibers[i].native_fiber)
                DeleteFiber(s_fibers[i].native_fiber);
#else
            free(s_fibers[i].stack);
#endif
            s_fibers[i].in_use = 0;
        }
    }

#ifdef _WIN32
    ConvertFiberToThread();
    s_scheduler_fiber = NULL;
#endif

    s_initialized = 0;
    return CELL_OK;
}

s32 cellFiberPpuCreateFiber(CellFiber* fiber, CellFiberEntry entry,
                              u64 arg, const CellFiberAttribute* attr)
{
    if (!s_initialized)
        return (s32)CELL_FIBER_ERROR_STAT;

    if (!fiber || !entry)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;

    /* Find a free slot */
    int idx = -1;
    for (int i = 0; i < CELL_FIBER_MAX_FIBERS; i++) {
        if (!s_fibers[i].in_use) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return (s32)CELL_FIBER_ERROR_NOMEM;

    FiberSlot* f = &s_fibers[idx];
    memset(f, 0, sizeof(FiberSlot));
    f->in_use = 1;
    f->entry = entry;
    f->arg = arg;
    f->state = CELL_FIBER_STATE_INITIALIZED;
    f->stack_size = CELL_FIBER_DEFAULT_STACK_SIZE;

    /* CellFiberAttribute is { const char* name; u32 priority; u32 stackSize; }.
     * The guest lays that out as three 4-byte fields; the host struct leads with
     * an 8-byte pointer, so a cast would read priority and stackSize from the
     * wrong offsets even after the address is translated. */
    if (attr) {
        u32 attr_ea    = (u32)(uintptr_t)attr;
        u32 name_ea    = vm_read32(attr_ea + 0);
        u32 priority   = vm_read32(attr_ea + 4);
        u32 stack_size = vm_read32(attr_ea + 8);
        if (stack_size > 0)
            f->stack_size = stack_size;
        if (name_ea)
            strncpy(f->name, (const char*)(vm_base + name_ea), sizeof(f->name) - 1);
        f->priority = priority;
    }

    printf("[cellFiber] CreateFiber(id=%d, name=%s, stack=%u)\n",
           idx, f->name[0] ? f->name : "(unnamed)", f->stack_size);

#ifdef _WIN32
    f->native_fiber = CreateFiber(f->stack_size, fiber_trampoline,
                                   (LPVOID)(uintptr_t)idx);
    if (!f->native_fiber) {
        f->in_use = 0;
        return (s32)CELL_FIBER_ERROR_NOMEM;
    }
#else
    f->stack = (u8*)malloc(f->stack_size);
    if (!f->stack) {
        f->in_use = 0;
        return (s32)CELL_FIBER_ERROR_NOMEM;
    }
    getcontext(&f->context);
    f->context.uc_stack.ss_sp = f->stack;
    f->context.uc_stack.ss_size = f->stack_size;
    f->context.uc_link = &s_scheduler_context;
    makecontext(&f->context, (void (*)(void))fiber_trampoline, 2,
                (int)(idx & 0xFFFF), (int)(idx >> 16));
#endif

    vm_write32((u32)(uintptr_t)fiber, (u32)idx);
    return CELL_OK;
}

s32 cellFiberPpuDeleteFiber(CellFiber fiber)
{
    if (!s_initialized)
        return (s32)CELL_FIBER_ERROR_STAT;

    u32 idx = fiber;
    if (idx >= CELL_FIBER_MAX_FIBERS || !s_fibers[idx].in_use)
        return (s32)CELL_FIBER_ERROR_INVAL;

    if (s_fibers[idx].state == CELL_FIBER_STATE_RUNNING)
        return (s32)CELL_FIBER_ERROR_BUSY;

    printf("[cellFiber] DeleteFiber(id=%u)\n", idx);

#ifdef _WIN32
    if (s_fibers[idx].native_fiber)
        DeleteFiber(s_fibers[idx].native_fiber);
#else
    free(s_fibers[idx].stack);
#endif

    s_fibers[idx].in_use = 0;
    return CELL_OK;
}

s32 cellFiberPpuSwitchFiber(CellFiber fiber)
{
    if (!s_initialized)
        return (s32)CELL_FIBER_ERROR_STAT;

    u32 idx = fiber;
    if (idx >= CELL_FIBER_MAX_FIBERS || !s_fibers[idx].in_use)
        return (s32)CELL_FIBER_ERROR_INVAL;

    if (s_fibers[idx].state == CELL_FIBER_STATE_TERMINATED)
        return (s32)CELL_FIBER_ERROR_STAT;

    /* Who is being switched away from, decided before s_current_fiber moves.
     * Both branches below need it and one of them used to read it after the
     * assignment, by which point it always said "the target". */
    s32 prev = s_current_fiber;

    /* Suspend current fiber if one is running */
    if (prev >= 0 && prev != (s32)idx)
        s_fibers[prev].state = CELL_FIBER_STATE_SUSPENDED;

    s_current_fiber = (s32)idx;
    s_fibers[idx].state = CELL_FIBER_STATE_RUNNING;

#ifdef _WIN32
    SwitchToFiber(s_fibers[idx].native_fiber);
#else
    /* SwitchToFiber saves the running fiber's state into the running fiber,
     * whichever one that is; swapcontext has to be told. A switch issued from
     * inside a fiber saves that fiber, and only a switch issued from the
     * scheduler saves the scheduler.
     *
     * The old selection tested s_current_fiber after assigning idx to it, so
     * "the caller is a different fiber" was never true and every switch saved
     * into s_scheduler_context. Two costs, both silent: the scheduler's own
     * saved context was overwritten by a fiber's, so the eventual return to
     * the scheduler resumed a fiber's stack instead of main's; and the
     * calling fiber's context was never updated, so switching back to it
     * restarted it from its entry point on the stack it was already using. */
    ucontext_t* from = (prev >= 0 && prev != (s32)idx)
                       ? &s_fibers[prev].context
                       : &s_scheduler_context;
    swapcontext(from, &s_fibers[idx].context);
#endif

    return CELL_OK;
}

s32 cellFiberPpuYieldFiber(void)
{
    if (!s_initialized || s_current_fiber < 0)
        return (s32)CELL_FIBER_ERROR_PERM;

    s_fibers[s_current_fiber].state = CELL_FIBER_STATE_SUSPENDED;
    s32 prev = s_current_fiber;
    s_current_fiber = -1;

#ifdef _WIN32
    SwitchToFiber(s_scheduler_fiber);
#else
    swapcontext(&s_fibers[prev].context, &s_scheduler_context);
#endif
    (void)prev;

    return CELL_OK;
}

s32 cellFiberPpuExitFiber(void)
{
    if (!s_initialized || s_current_fiber < 0)
        return (s32)CELL_FIBER_ERROR_PERM;

    s_fibers[s_current_fiber].state = CELL_FIBER_STATE_TERMINATED;
    s_current_fiber = -1;

#ifdef _WIN32
    SwitchToFiber(s_scheduler_fiber);
#else
    setcontext(&s_scheduler_context);
#endif

    return CELL_OK; /* unreachable */
}

s32 cellFiberPpuGetCurrentFiber(CellFiber* fiber)
{
    if (!fiber)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;

    if (s_current_fiber < 0)
        return (s32)CELL_FIBER_ERROR_PERM;

    vm_write32((u32)(uintptr_t)fiber, (u32)s_current_fiber);
    return CELL_OK;
}

s32 cellFiberPpuGetFiberState(CellFiber fiber, u32* state)
{
    u32 idx = fiber;
    if (idx >= CELL_FIBER_MAX_FIBERS || !s_fibers[idx].in_use)
        return (s32)CELL_FIBER_ERROR_INVAL;

    if (!state)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;

    vm_write32((u32)(uintptr_t)state, s_fibers[idx].state);
    return CELL_OK;
}

s32 cellFiberPpuAttributeInitialize(CellFiberAttribute* attr)
{
    if (!attr)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;

    u32 attr_ea = (u32)(uintptr_t)attr;
    vm_write32(attr_ea + 0, 0);                                  /* name      */
    vm_write32(attr_ea + 4, 0);                                  /* priority  */
    vm_write32(attr_ea + 8, CELL_FIBER_DEFAULT_STACK_SIZE);      /* stackSize */
    return CELL_OK;
}

s32 cellFiberPpuSleep(void)
{
    if (!s_initialized || s_current_fiber < 0)
        return (s32)CELL_FIBER_ERROR_PERM;

    printf("[cellFiber] Sleep(fiber=%d)\n", s_current_fiber);
    s_fibers[s_current_fiber].state = CELL_FIBER_STATE_SUSPENDED;
    s32 prev = s_current_fiber;
    s_current_fiber = -1;

#ifdef _WIN32
    SwitchToFiber(s_scheduler_fiber);
#else
    swapcontext(&s_fibers[prev].context, &s_scheduler_context);
#endif
    (void)prev;

    return CELL_OK;
}

s32 cellFiberPpuWakeup(CellFiber fiber)
{
    u32 idx = fiber;
    if (idx >= CELL_FIBER_MAX_FIBERS || !s_fibers[idx].in_use)
        return (s32)CELL_FIBER_ERROR_INVAL;

    if (s_fibers[idx].state != CELL_FIBER_STATE_SUSPENDED) {
        printf("[cellFiber] Wakeup(fiber=%u): not suspended (state=%u)\n",
               idx, s_fibers[idx].state);
        return (s32)CELL_FIBER_ERROR_STAT;
    }

    printf("[cellFiber] Wakeup(fiber=%u)\n", idx);
    s_fibers[idx].state = CELL_FIBER_STATE_INITIALIZED; /* ready to run */
    return CELL_OK;
}

/* -------------------------------------------------------------------------
 * CellFiber PPU context support
 *
 * The game imports the lower-level CellFiberPpuContext API in addition to
 * the simple CellFiber API above.  A context is an opaque 640-byte guest
 * object; keep its entry point and arguments in a host-side table, and run
 * that guest entry through the same lifted-code callback bridge used by the
 * rest of the HLE layer. Windows contexts use native fibers to preserve
 * suspended C stacks and each context's guest registers and dispatch state.
 * Other hosts retain the synchronous callback fallback.
 * ----------------------------------------------------------------------*/

#define CELL_FIBER_PPU_CONTEXT_SIZE 640u
#define CELL_FIBER_PPU_CONTEXT_MAX 128

typedef struct PpuContextRecord {
    u32 context_ea;
    u32 entry_opd;
    u32 stack_ea;
    u32 stack_size;
    u64 arg;
    int active;
    int running;
#ifdef _WIN32
    LPVOID native_context;
    DWORD owner_thread;
    u32 incoming_from;
    int terminated;
    PpuFiberBridgeState bridge;
#endif
} PpuContextRecord;

static PpuContextRecord s_ppu_contexts[CELL_FIBER_PPU_CONTEXT_MAX];
static atomic_flag s_ppu_context_lock = ATOMIC_FLAG_INIT;

#ifdef _MSC_VER
static __declspec(thread) u32 s_active_ppu_context;
static __declspec(thread) u32 s_context_return_cause;
#else
static __thread u32 s_active_ppu_context;
static __thread u32 s_context_return_cause;
#endif

extern u64 ppu_guest_call(u32 opd,
                          u64 a0, u64 a1, u64 a2, u64 a3,
                          u64 a4, u64 a5, u64 a6, u64 a7);
extern int ppu_guest_call_return_to_caller(void);

static void ppu_context_lock(void)
{
    while (atomic_flag_test_and_set_explicit(&s_ppu_context_lock, memory_order_acquire)) { }
}

static void ppu_context_unlock(void)
{
    atomic_flag_clear_explicit(&s_ppu_context_lock, memory_order_release);
}

static int ppu_context_find(u32 ea)
{
    for (int i = 0; i < CELL_FIBER_PPU_CONTEXT_MAX; ++i)
        if (s_ppu_contexts[i].active && s_ppu_contexts[i].context_ea == ea)
            return i;
    return -1;
}

static s32 ppu_context_run(u32 context_ea, u32 cause_ea, u32 fiber_from_ea)
{
    PpuContextRecord record;
    int slot;
    u32 from = s_active_ppu_context;

    if (!context_ea)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;
    if (context_ea & 0xFu)
        return (s32)CELL_FIBER_ERROR_ALIGN;

    ppu_context_lock();
    slot = ppu_context_find(context_ea);
    if (slot < 0) {
        ppu_context_unlock();
        fprintf(stderr, "[cellFiber] ContextRun(context=0x%08X) has no initialized entry\n",
                context_ea);
        return (s32)CELL_FIBER_ERROR_INVAL;
    }
    if (s_ppu_contexts[slot].running) {
        ppu_context_unlock();
        return (s32)CELL_FIBER_ERROR_BUSY;
    }
    s_ppu_contexts[slot].running = 1;
    record = s_ppu_contexts[slot];
    ppu_context_unlock();

    if (fiber_from_ea)
        vm_write64(fiber_from_ea, (u64)from);

    {
        static int logged = 0;
        if (logged < 24) {
            fprintf(stderr, "[cellFiber] ContextRun(ctx=0x%08X entryOPD=0x%08X "
                            "code=0x%08X toc=0x%08X arg=0x%016llX from=0x%08X)\n",
                    context_ea, record.entry_opd,
                    vm_read32(record.entry_opd), vm_read32(record.entry_opd + 4),
                    (unsigned long long)record.arg, from);
            ++logged;
        }
    }

    s_active_ppu_context = context_ea;
    s_context_return_cause = 0;
    (void)ppu_guest_call(record.entry_opd, record.arg, from, 0, 0, 0, 0, 0, 0);
    s_active_ppu_context = from;
    if (cause_ea)
        vm_write32(cause_ea, s_context_return_cause);

    ppu_context_lock();
    slot = ppu_context_find(context_ea);
    if (slot >= 0)
        s_ppu_contexts[slot].running = 0;
    ppu_context_unlock();
    return CELL_OK;
}

#ifdef _WIN32
static __declspec(thread) LPVOID s_context_thread_fiber;
static __declspec(thread) PpuFiberBridgeState s_context_thread_bridge;
static __declspec(thread) u32 s_context_return_from;

static void ppu_context_native_return(u32 cause)
{
    int slot = ppu_context_find(s_active_ppu_context);
    PpuContextRecord* record = &s_ppu_contexts[slot];
    ppu_fiber_bridge_save(&record->bridge);
    record->running = 0;
    s_context_return_cause = cause;
    s_context_return_from = record->context_ea;
    s_active_ppu_context = 0;
    ppu_fiber_bridge_restore(&s_context_thread_bridge);
    SwitchToFiber(s_context_thread_fiber);
}

static void CALLBACK ppu_context_native_entry(LPVOID parameter)
{
    PpuContextRecord* record = (PpuContextRecord*)parameter;
    ppu_fiber_guest_start(record->entry_opd, record->arg,
                          record->stack_ea + record->stack_size,
                          record->incoming_from);
    record->terminated = 1;
    ppu_context_native_return(0);
    abort(); /* A terminated context must never be resumed. */
}

static s32 ppu_context_native_switch(u32 context_ea, u32 cause_ea,
                                     u32 fiber_from_ea, int run_from_thread)
{
    u32 from = s_active_ppu_context;
    if (!context_ea) return (s32)CELL_FIBER_ERROR_NULL_POINTER;
    if (context_ea & 15u) return (s32)CELL_FIBER_ERROR_ALIGN;
    if (run_from_thread ? from != 0 : from == 0)
        return (s32)CELL_FIBER_ERROR_PERM;
    if (from == context_ea) return (s32)CELL_FIBER_ERROR_DEADLK;

    ppu_context_lock();
    int slot = ppu_context_find(context_ea);
    if (slot < 0) { ppu_context_unlock(); return (s32)CELL_FIBER_ERROR_INVAL; }
    PpuContextRecord* record = &s_ppu_contexts[slot];
    if (record->running || (record->owner_thread && record->owner_thread != GetCurrentThreadId())) {
        ppu_context_unlock(); return (s32)CELL_FIBER_ERROR_BUSY;
    }
    if (record->terminated) { ppu_context_unlock(); return (s32)CELL_FIBER_ERROR_STAT; }
    if (!record->native_context) {
        /* Large reserve, small commit: lifted C++ chains use more host stack
         * than the original PPU frame, without requiring a large resident stack. */
        record->native_context = CreateFiberEx(0x10000, 64u * 1024u * 1024u,
            FIBER_FLAG_FLOAT_SWITCH, ppu_context_native_entry, record);
        if (!record->native_context) { ppu_context_unlock(); return (s32)CELL_FIBER_ERROR_NOMEM; }
        record->owner_thread = GetCurrentThreadId();
        ppu_fiber_bridge_save(&record->bridge);
    }
    record->running = 1;
    record->incoming_from = from;
    ppu_context_unlock();

    if (run_from_thread) {
        if (!s_context_thread_fiber) {
            s_context_thread_fiber = IsThreadAFiber() ? GetCurrentFiber()
                : ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
            if (!s_context_thread_fiber) {
                record->running = 0;
                return (s32)CELL_FIBER_ERROR_STAT;
            }
        }
        s_context_return_cause = 0;
        s_context_return_from = 0;
        ppu_fiber_bridge_save(&s_context_thread_bridge);
    } else {
        PpuContextRecord* caller = &s_ppu_contexts[ppu_context_find(from)];
        ppu_fiber_bridge_save(&caller->bridge);
        caller->running = 0;
    }
    s_active_ppu_context = context_ea;
    ppu_fiber_bridge_restore(&record->bridge);
    { static LONG count;
      if (InterlockedIncrement(&count) <= 40)
          fprintf(stderr, "[cellFiber] native %s from=0x%08X to=0x%08X\n",
                  run_from_thread ? "run" : "switch", from, context_ea); }
    SwitchToFiber(record->native_context);
    /* The context transferring back restored this caller's guest/TLS state.
     * fiberFrom is a guest pointer (four bytes), not a host pointer/u64. */
    if (cause_ea) vm_write32(cause_ea, s_context_return_cause);
    if (fiber_from_ea) {
        u32 resumed_by = run_from_thread ? s_context_return_from
            : s_ppu_contexts[ppu_context_find(from)].incoming_from;
        vm_write32(fiber_from_ea, resumed_by);
    }
    return CELL_OK;
}
#endif

/* NID 0x31252EC3: _cellFiberPpuContextAttributeInitialize(attr, sdkVersion) */
s32 _cellFiberPpuContextAttributeInitialize(u64 attr_ea, u32 sdk_version,
                                             u64 a2, u64 a3, u64 a4, u64 a5,
                                             u64 a6, u64 a7)
{
    (void)sdk_version; (void)a2; (void)a3; (void)a4;
    (void)a5; (void)a6; (void)a7;
    if (!attr_ea)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;
    if ((u32)attr_ea & 7u)
        return (s32)CELL_FIBER_ERROR_ALIGN;

    for (u32 o = 0; o < 128u; o += 4u)
        vm_write32((u32)attr_ea + o, 0);
    return CELL_OK;
}

/* NID 0x72086315: cellFiberPpuContextInitialize(context, entry, arg,
 *                                                 stack, stackSize, attr) */
s32 cellFiberPpuContextInitialize(u64 context_ea, u64 entry_opd, u64 arg,
                                  u64 stack_ea, u32 stack_size, u64 attr_ea,
                                  u64 a6, u64 a7)
{
    int slot = -1;
    (void)attr_ea; (void)a6; (void)a7;

    if (!context_ea || !entry_opd || !stack_ea)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;
    if ((u32)context_ea & 0xFu)
        return (s32)CELL_FIBER_ERROR_ALIGN;

    ppu_context_lock();
    slot = ppu_context_find((u32)context_ea);
    if (slot < 0) {
        for (slot = 0; slot < CELL_FIBER_PPU_CONTEXT_MAX; ++slot)
            if (!s_ppu_contexts[slot].active)
                break;
    }
    if (slot >= CELL_FIBER_PPU_CONTEXT_MAX) {
        ppu_context_unlock();
        return (s32)CELL_FIBER_ERROR_NOMEM;
    }
    if (s_ppu_contexts[slot].running) {
        ppu_context_unlock(); return (s32)CELL_FIBER_ERROR_BUSY;
    }
#ifdef _WIN32
    if (s_ppu_contexts[slot].native_context)
        DeleteFiber(s_ppu_contexts[slot].native_context);
#endif
    memset(&s_ppu_contexts[slot], 0, sizeof(s_ppu_contexts[slot]));
    s_ppu_contexts[slot].context_ea = (u32)context_ea;
    s_ppu_contexts[slot].entry_opd = (u32)entry_opd;
    s_ppu_contexts[slot].stack_ea = (u32)stack_ea;
    s_ppu_contexts[slot].stack_size = stack_size;
    s_ppu_contexts[slot].arg = arg;
    s_ppu_contexts[slot].active = 1;
    s_ppu_contexts[slot].running = 0;
    ppu_context_unlock();

    for (u32 o = 0; o < CELL_FIBER_PPU_CONTEXT_SIZE; o += 4u)
        vm_write32((u32)context_ea + o, 0);

    {
        static int logged = 0;
        if (logged < 24) {
            fprintf(stderr, "[cellFiber] ContextInitialize(ctx=0x%08X entryOPD=0x%08X "
                            "arg=0x%016llX stack=0x%08X size=0x%X)\n",
                    (u32)context_ea, (u32)entry_opd, (unsigned long long)arg,
                    (u32)stack_ea, stack_size);
            ++logged;
        }
    }
    return CELL_OK;
}

/* NID 0xABA1C563: run the initialized entry on the guest PPU. */
s32 cellFiberPpuContextRun(u64 context_ea, u64 cause_ea, u64 fiber_from_ea,
                           u64 option_ea, u64 a4, u64 a5, u64 a6, u64 a7)
{
    (void)option_ea; (void)a4; (void)a5; (void)a6; (void)a7;
#ifdef _WIN32
    return ppu_context_native_switch((u32)context_ea, (u32)cause_ea, (u32)fiber_from_ea, 1);
#else
    return ppu_context_run((u32)context_ea, (u32)cause_ea, (u32)fiber_from_ea);
#endif
}

/* NID 0xD0066B17: suspend this context and resume the target context. */
s32 cellFiberPpuContextSwitch(u64 context_ea, u64 fiber_from_ea,
                              u64 option_ea, u64 a3, u64 a4, u64 a5,
                              u64 a6, u64 a7)
{
    (void)option_ea; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
#ifdef _WIN32
    return ppu_context_native_switch((u32)context_ea, 0, (u32)fiber_from_ea, 0);
#else
    return ppu_context_run((u32)context_ea, 0, (u32)fiber_from_ea);
#endif
}

/* NID 0x34A81091: return the context currently executing on this PPU thread. */
u64 cellFiberPpuContextSelf(u64 a0, u64 a1, u64 a2, u64 a3,
                            u64 a4, u64 a5, u64 a6, u64 a7)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    (void)a4; (void)a5; (void)a6; (void)a7;
    return (u64)s_active_ppu_context;
}

/* NID 0xB3A48079: forget an initialized context. */
s32 cellFiberPpuContextFinalize(u64 context_ea, u64 a1, u64 a2, u64 a3,
                                u64 a4, u64 a5, u64 a6, u64 a7)
{
    int slot;
    (void)a1; (void)a2; (void)a3; (void)a4;
    (void)a5; (void)a6; (void)a7;
    if (!context_ea)
        return (s32)CELL_FIBER_ERROR_NULL_POINTER;
    if ((u32)context_ea & 0xFu)
        return (s32)CELL_FIBER_ERROR_ALIGN;
    ppu_context_lock();
    slot = ppu_context_find((u32)context_ea);
    if (slot >= 0) {
        if (s_ppu_contexts[slot].running) {
            ppu_context_unlock(); return (s32)CELL_FIBER_ERROR_BUSY;
        }
#ifdef _WIN32
        if (s_ppu_contexts[slot].native_context)
            DeleteFiber(s_ppu_contexts[slot].native_context);
#endif
        memset(&s_ppu_contexts[slot], 0, sizeof(s_ppu_contexts[slot]));
    }
    ppu_context_unlock();
    return slot >= 0 ? CELL_OK : (s32)CELL_FIBER_ERROR_INVAL;
}

/* NID 0x01036193: cooperatively return from the current guest Fiber context.
 * The runtime currently models this as a return to the ContextRun host wrapper;
 * it does not preserve a resumable guest stack. */
s32 cellFiberPpuContextReturnToThread(u64 cause, u64 a1, u64 a2, u64 a3,
                                      u64 a4, u64 a5, u64 a6, u64 a7)
{
    static int logged = 0;
    (void)a1; (void)a2; (void)a3; (void)a4;
    (void)a5; (void)a6; (void)a7;
    if (logged++ < 24)
        fprintf(stderr, "[cellFiber] ContextReturnToThread(cause=0x%08X ctx=0x%08X)\n",
                (u32)cause, s_active_ppu_context);
#ifdef _WIN32
    if (!s_active_ppu_context || !s_context_thread_fiber)
        return (s32)CELL_FIBER_ERROR_PERM;
    ppu_context_native_return((u32)cause);
    return CELL_OK;
#else
    s_context_return_cause = (u32)cause;
    if (!ppu_guest_call_return_to_caller())
        return (s32)CELL_FIBER_ERROR_PERM;
    return CELL_OK; /* reached only if the host helper changes its contract */
#endif
}

/* Opt-in port diagnostics, called only from the host frame clock. */
void cellFiberPpuContextReport(void)
{
#ifdef _WIN32
    for (int i = 0; i < CELL_FIBER_PPU_CONTEXT_MAX; ++i) {
        PpuContextRecord* r = &s_ppu_contexts[i];
        if (!r->context_ea) continue;
        ppu_context* c = (ppu_context*)r->bridge.active_ctx;
        fprintf(stderr, "[fiber-progress] ctx=0x%08X owner=%lu active=%d ended=%d guest=%u lr=%08X sp=%08X r3=%08X r31=%08X\n",
            r->context_ea, (unsigned long)r->owner_thread, r->active, r->terminated,
            c ? (unsigned)c->thread_id : 0, c ? (u32)c->lr : 0,
            c ? (u32)c->gpr[1] : 0, c ? (u32)c->gpr[3] : 0,
            c ? (u32)c->gpr[31] : 0);
    }
#endif
}
