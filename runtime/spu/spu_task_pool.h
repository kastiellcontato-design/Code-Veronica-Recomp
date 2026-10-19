/* Windows SPURS host-thread reuse. No guest scheduling/claims live here.
 * FIFO handoff; sleeping workers consume no execution permit. The pool grows
 * at its concurrent high-water mark: a WAIT_SIGNAL service must never prevent
 * its producer from getting a host thread. SPU_MAX_RUNNING stays in the runner.
 * Header-private so the standalone stress test exercises the production code. */
#ifndef SPU_TASK_POOL_H
#define SPU_TASK_POOL_H
#include <windows.h>
#include <stdlib.h>
#ifndef SPU_POOL_CREATE_THREAD
#define SPU_POOL_CREATE_THREAD CreateThread
#endif

typedef struct spu_pool_job { struct spu_pool_job* next; } spu_pool_job;
typedef struct spu_pool_worker spu_pool_worker;
typedef struct {
    SRWLOCK lock;
    CONDITION_VARIABLE cv;
    spu_pool_job *head, *tail;
    spu_pool_worker *idle, *workers;
    unsigned threads, busy, queued;
    unsigned long long submitted, created, fallback;
    int initialized;
    void (*run)(spu_pool_job*);
#ifdef SPU_POOL_TEST
    int stopping;
#endif
} spu_task_pool;

struct spu_pool_worker {
    spu_task_pool* pool;
    CONDITION_VARIABLE cv;
    spu_pool_worker *next_idle, *next_worker;
    int scheduled;
};

static DWORD WINAPI spu_task_pool_worker(LPVOID arg)
{
    spu_pool_worker* w = (spu_pool_worker*)arg;
    spu_task_pool* p = w->pool;
    { ULONG guarantee = 256 * 1024; SetThreadStackGuarantee(&guarantee); }
    AcquireSRWLockExclusive(&p->lock);
    for (;;) {
        while (!w->scheduled) {
#ifdef SPU_POOL_TEST
            if (p->stopping) {
                --p->threads;
                WakeAllConditionVariable(&p->cv);
                ReleaseSRWLockExclusive(&p->lock);
                return 0;
            }
#endif
            SleepConditionVariableSRW(&w->cv, &p->lock, INFINITE, 0);
        }
        w->scheduled = 0;
        spu_pool_job* job = p->head;
        p->head = job->next;
        if (!p->head) p->tail = NULL;
        --p->queued;
        ++p->busy;
        ReleaseSRWLockExclusive(&p->lock);
        p->run(job); /* may free job; never touch it again */
        AcquireSRWLockExclusive(&p->lock);
        --p->busy;
        w->next_idle = p->idle;
        p->idle = w;
#ifdef SPU_POOL_TEST
        WakeAllConditionVariable(&p->cv);
#endif
    }
}

/* Called with the lock held. Count STARTING workers as available, too:
 * otherwise rapid dispatches create extra threads before the first one wakes. */
static int spu_task_pool_grow(spu_task_pool* p)
{
    spu_pool_worker* w = (spu_pool_worker*)calloc(1, sizeof *w);
    if (!w) return 0;
    w->pool = p;
    ++p->threads;
    HANDLE th = SPU_POOL_CREATE_THREAD(NULL, 256u << 20, spu_task_pool_worker, w,
                                      STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!th) { --p->threads; free(w); return 0; }
    ++p->created;
    w->next_idle = p->idle;
    p->idle = w;  /* available even before its host thread has started */
    w->next_worker = p->workers;
    p->workers = w;
    CloseHandle(th);
    return 1;
}

/* 0 leaves ownership entirely with the caller, including on CreateThread
 * failure. Never leave an accepted task waiting behind blocked services.
 * run/warm are configuration constants for the lifetime of this pool. */
static int spu_task_pool_submit(spu_task_pool* p, spu_pool_job* job,
                                void (*run)(spu_pool_job*), unsigned warm)
{
    AcquireSRWLockExclusive(&p->lock);
    if (!p->initialized) {
        p->run = run;
        p->initialized = 1;
        for (unsigned i = 0; i < warm; ++i)
            if (!spu_task_pool_grow(p)) break;
    }
    if (!p->idle && !spu_task_pool_grow(p)) {
        ++p->fallback;
        ReleaseSRWLockExclusive(&p->lock);
        return 0;
    }
    job->next = NULL;
    if (p->tail) p->tail->next = job; else p->head = job;
    p->tail = job;
    ++p->queued;
    ++p->submitted;
    /* Pick a recently used idle HOST thread (LIFO) to retain warm CPU caches.
     * Work remains strictly FIFO: the selected worker still pops head under
     * the lock. Exactly one worker is reserved/woken per accepted job, so a
     * busy worker cannot steal an already assigned wakeup. No affinity/spin. */
    spu_pool_worker* w = p->idle;
    p->idle = w->next_idle;
    w->scheduled = 1;
    WakeConditionVariable(&w->cv);
    ReleaseSRWLockExclusive(&p->lock);
    return 1;
}
#endif
