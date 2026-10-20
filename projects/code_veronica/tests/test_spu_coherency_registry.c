/* Exercise the production registry and wake logic with only host/MFC edges
 * stubbed. No game assets, fake rendering or timing-dependent assertions. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../../runtime/spu/spu_coherency.c"
static volatile LONG wakes, releases;
void spu_ch_wake(spu_context* ctx) { (void)ctx;InterlockedIncrement(&wakes); }
void spu_mfc_release(spu_context* ctx) { (void)ctx;InterlockedIncrement(&releases); }
void spu_mfc_forget_range(uintptr_t lo,uintptr_t hi) { (void)lo;(void)hi; }
static unsigned occurrences(spu_context* ctx) {
    unsigned n=0;for(int i=0;i<s_coh_n;i++)n+=s_coh_ctxs[i]==ctx;return n;
}
static void reserve(spu_context* ctx) {
    spu_lockline_lock();ctx->resv_valid=1;ctx->resv_ea=0x123400;
    spu_coh_reserve(ctx,ctx->resv_ea);spu_lockline_unlock();
}
static DWORD WINAPI churn(void* p) {
    spu_context* ctx=(spu_context*)p;
    for(int k=0;k<2000;k++) {
        reserve(ctx);reserve(ctx);
        spu_lockline_lock();assert(occurrences(ctx)==1);
        spu_coh_notify_write_except(0x123400,ctx);spu_lockline_unlock();
        spu_coh_unregister(ctx);
    }
    return 0;
}
int main(void) {
    spu_context* contexts=(spu_context*)_aligned_malloc(64*sizeof(spu_context),128);
    assert(contexts);memset(contexts,0,64*sizeof(spu_context));
    spu_coh_watch_line(0x20002000);assert(spu_coh_is_reserved(0x20002000));
    assert(spu_coh_is_reserved(0x2000207c));assert(!spu_coh_is_reserved(0x20002080));
    assert(s_coh_n==0 && wakes==0); /* watch routes stores, never fake SPU event */
    reserve(contexts);reserve(contexts+1);spu_coh_unregister(contexts);
    for(int i=0;i<500;i++)reserve(contexts+1);
    assert(occurrences(contexts+1)==1); /* regression: hole BEFORE existing */
    spu_coh_unregister(contexts+1);assert(s_coh_n==0);
    for(int i=0;i<64;i++)reserve(contexts+i);
    assert(s_coh_n==64);
    for(int i=0;i<64;i++)assert(occurrences(contexts+i)==1);
    spu_lockline_lock();spu_coh_notify_write(0x123400);spu_lockline_unlock();
    assert(wakes==64);
    for(int i=0;i<64;i++){assert(!contexts[i].resv_valid);assert(contexts[i].event_status&SPU_EVENT_LR);}
    for(int i=0;i<64;i++)spu_coh_unregister(contexts+i);
    assert(s_coh_n==0);
    // Defensive cleanup removes all historical duplicates, never stale pointers.
    s_coh_ctxs[0]=s_coh_ctxs[3]=contexts;s_coh_n=4;
    spu_coh_unregister(contexts);assert(s_coh_n==0);
    reserve(contexts);reserve(contexts+1);
    spu_coh_forget_range((uintptr_t)contexts,(uintptr_t)(contexts+2));assert(s_coh_n==0);
    HANDLE threads[8];
    for(int i=0;i<8;i++){threads[i]=CreateThread(NULL,0,churn,contexts+i,0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(8,threads,TRUE,30000)==WAIT_OBJECT_0);
    for(int i=0;i<8;i++)CloseHandle(threads[i]);
    assert(s_coh_n==0);for(int i=0;i<64;i++)assert(!s_coh_ctxs[i]);
    _aligned_free(contexts);
    puts("PASS production SPU coherence: hole-before-existing, 64 unique contexts, exact wake, cleanup, 16000 concurrent lifecycles");
    return 0;
}
