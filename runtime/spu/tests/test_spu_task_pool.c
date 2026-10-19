/* Production pool, real Windows threads; no game assets or synthetic input. */
#include <assert.h>
#include <stdio.h>
static volatile long fail_create;
#include <windows.h>
static HANDLE create_thread(LPSECURITY_ATTRIBUTES a, SIZE_T b, LPTHREAD_START_ROUTINE c,
                             LPVOID d, DWORD e, LPDWORD f)
{
    if (InterlockedCompareExchange(&fail_create, 0, 1) == 1) return NULL;
    return CreateThread(a,b,c,d,e,f);
}
#define SPU_POOL_TEST
#define SPU_POOL_CREATE_THREAD create_thread
#include "../spu_task_pool.h"

typedef struct { spu_pool_job node; HANDLE done, gate; volatile LONG seen; DWORD host_tid; } job;
static volatile LONG duplicates;
static void run(spu_pool_job* node)
{
    job* j = (job*)node;
    j->host_tid = GetCurrentThreadId();
    if (InterlockedIncrement(&j->seen) != 1) InterlockedIncrement(&duplicates);
    if (j->gate) assert(WaitForSingleObject(j->gate, 10000) == WAIT_OBJECT_0);
    if (j->done) SetEvent(j->done);
}
static void stop(spu_task_pool* p)
{
    AcquireSRWLockExclusive(&p->lock);
    while (p->busy || p->queued)
        assert(SleepConditionVariableSRW(&p->cv, &p->lock, 10000, 0));
    p->stopping = 1;
    for (spu_pool_worker* w=p->workers;w;w=w->next_worker)
        WakeAllConditionVariable(&w->cv);
    while (p->threads)
        assert(SleepConditionVariableSRW(&p->cv, &p->lock, 10000, 0));
    ReleaseSRWLockExclusive(&p->lock);
    spu_pool_worker* w=p->workers;
    while (w) { spu_pool_worker* next=w->next_worker; free(w); w=next; }
}

enum { PRODUCERS=8, JOBS=500 };
static spu_task_pool stress_pool;
static job jobs[PRODUCERS][JOBS];
static DWORD WINAPI produce(LPVOID arg)
{
    unsigned index = (unsigned)(ULONG_PTR)arg;
    HANDLE done = CreateEvent(NULL,FALSE,FALSE,NULL);
    assert(done);
    for (unsigned n=0; n<JOBS; ++n) {
        jobs[index][n].done=done;
        assert(spu_task_pool_submit(&stress_pool,&jobs[index][n].node,run,4));
        assert(WaitForSingleObject(done,10000)==WAIT_OBJECT_0);
    }
    CloseHandle(done);
    return 0;
}
int main(void)
{
    /* Startup burst + rapid completion across concurrent dispatchers: exactly
     * once and only a bounded high-water mark of threads, not one per task. */
    HANDLE producers[PRODUCERS];
    for (unsigned i=0;i<PRODUCERS;++i) {
        producers[i]=CreateThread(NULL,0,produce,(LPVOID)(ULONG_PTR)i,0,NULL);
        assert(producers[i]);
    }
    assert(WaitForMultipleObjects(PRODUCERS,producers,TRUE,30000)==WAIT_OBJECT_0);
    for (unsigned i=0;i<PRODUCERS;++i) CloseHandle(producers[i]);
    stop(&stress_pool);
    assert(stress_pool.submitted==PRODUCERS*JOBS);
    for (unsigned i=0;i<PRODUCERS;++i)
        for (unsigned j=0;j<JOBS;++j) assert(jobs[i][j].seen==1);
    assert(!duplicates && stress_pool.created<=2*PRODUCERS);
    printf("pool: %llu jobs exactly once, %llu reusable workers\n",
           stress_pool.submitted,stress_pool.created);

    /* Every worker parked in a guest service: a producer still gets a thread.
     * Also inject creation failure with all existing threads blocked: rejected
     * work remains the caller's and is safe to run via detached fallback. */
    spu_task_pool parked={0};
    HANDLE gate=CreateEvent(NULL,TRUE,FALSE,NULL);
    HANDLE done=CreateEvent(NULL,FALSE,FALSE,NULL);
    job service={0}, failed={0}, next={0};
    service.gate=gate;
    assert(spu_task_pool_submit(&parked,&service.node,run,1));
    while (!InterlockedCompareExchange(&service.seen,0,0)) Sleep(1);
    InterlockedExchange(&fail_create,1);
    assert(!spu_task_pool_submit(&parked,&failed.node,run,1));
    assert(!failed.seen && !parked.queued && parked.fallback==1);
    run(&failed.node); /* same ownership contract as detached fallback */
    next.done=done;
    assert(spu_task_pool_submit(&parked,&next.node,run,1));
    assert(WaitForSingleObject(done,10000)==WAIT_OBJECT_0);
    assert(parked.created==2 && failed.seen==1);
    SetEvent(gate); stop(&parked);
    CloseHandle(gate); CloseHandle(done);

    /* With several idle workers, sequential work returns to the recently
     * used one rather than rotating through cold host threads. */
    spu_task_pool locality={0}; job recent[2]={0};
    for (unsigned i=0;i<2;++i) {
        assert(spu_task_pool_submit(&locality,&recent[i].node,run,4));
        AcquireSRWLockExclusive(&locality.lock);
        while (locality.busy || locality.queued)
            assert(SleepConditionVariableSRW(&locality.cv,&locality.lock,10000,0));
        ReleaseSRWLockExclusive(&locality.lock);
    }
    assert(recent[0].host_tid==recent[1].host_tid);
    stop(&locality);
    puts("pool: recently used idle worker retained for sequential jobs");

    /* Failure before any worker exists must not strand an accepted job. */
    spu_task_pool empty={0}; job first={0};
    InterlockedExchange(&fail_create,1);
    assert(!spu_task_pool_submit(&empty,&first.node,run,0));
    assert(!empty.head && !empty.queued && !empty.threads && !first.seen);
    assert(spu_task_pool_submit(&empty,&first.node,run,0));
    stop(&empty);
    assert(first.seen==1 && !duplicates);
    puts("pool: blocked-service progress and thread-creation-failure fallback OK");
    return 0;
}
