#include "win32_compat.h"
static bool inject_wait_failure;
static DWORD test_wait(HANDLE event,DWORD ms) {
    return inject_wait_failure?WAIT_FAILED:WaitForSingleObject(event,ms);
}
#define WaitForSingleObject test_wait
#include "../../../libs/video/rsx_fifo_idle.h"
#undef WaitForSingleObject
#include "../../../libs/video/rsx_fifo_probe.h"
#include "../../../libs/video/rsx_fifo_publication.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <cstdio>
int main(){
    assert(!rsx_fifo_idle_enabled(nullptr,nullptr,nullptr));
    assert(!rsx_fifo_idle_enabled("0","1",nullptr));
    assert(!rsx_fifo_idle_enabled("1","0",nullptr));
    assert(rsx_fifo_idle_enabled("1","1",nullptr));
    assert(!rsx_fifo_idle_enabled("1","1","0"));
    // Real walker header layout; pending does not mean runnable.
    assert(rsx_fifo_probe_classify(0x100,0x100,0,0)==RSX_FIFO_EMPTY);
    assert(rsx_fifo_probe_classify(0x100,0x200,0,0)==RSX_FIFO_UNMAPPED);
    assert(rsx_fifo_probe_classify(0x100,0x200,0x20000100,1)==RSX_FIFO_PARK);
    assert(rsx_fifo_probe_classify(0x100,0x200,0x20000104,1)==RSX_FIFO_PENDING);
    assert(rsx_fifo_probe_classify(0x100,0x200,(1u<<18)|0x68,1)==RSX_FIFO_SEMAPHORE);
    assert(rsx_fifo_probe_classify(0x100,0x200,(2u<<29)|(1u<<18)|0x68,1)==RSX_FIFO_SEMAPHORE);
    assert(rsx_fifo_probe_classify(0x100,0x200,(1u<<18)|(1u<<13)|0x68,1)==RSX_FIFO_PENDING);
    assert(rsx_fifo_probe_classify(0x100,0x200,0x68,1)==RSX_FIFO_PENDING);
    assert(rsx_fifo_probe_classify(0x100,0x200,0x00020000,1)==RSX_FIFO_PENDING);
    rsx_fifo_probe a={0x100,0x200,0x20000100,RSX_FIFO_PARK},b=a;
    b.head=0x20000104;assert(rsx_fifo_probe_head_changed(&a,&b));
    b.get+=4;assert(!rsx_fifo_probe_head_changed(&a,&b));
    b=a;b.head=0;b.state=RSX_FIFO_PROBE_BUSY;assert(!rsx_fifo_probe_head_changed(&a,&b));
    b.state=RSX_FIFO_EMPTY;assert(!rsx_fifo_probe_head_changed(&a,&b));
    assert(rsx_fifo_publication_contains(0x20002000,4,0x20002000));
    assert(rsx_fifo_publication_contains(0x20001ffc,8,0x20002000));
    assert(!rsx_fifo_publication_contains(0x20002001,4,0x20002000));
    assert(!rsx_fifo_publication_contains(0x20002000,3,0x20002000));
    assert(!rsx_fifo_publication_contains(0x20002004,4,0x20002000));
    assert(!rsx_fifo_publication_contains(0xfffffffcu,8,0x20002000));
    assert(!rsx_fifo_publication_contains(0,4,0));
    HANDLE event=CreateEventA(nullptr,FALSE,FALSE,nullptr);assert(event);
    // Signal before wait: retained; coalescing signals consume only once.
    SetEvent(event);SetEvent(event);assert(rsx_fifo_idle_wait(event,0)==1);
    assert(rsx_fifo_idle_wait(event,0)==0);
    // Consumer waits; producer wakes it. Generous timeout avoids slow-CI races.
    std::thread producer([&]{Sleep(20);SetEvent(event);});
    auto begin=std::chrono::steady_clock::now();assert(rsx_fifo_idle_wait(event,2000)==1);
    auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    producer.join();assert(elapsed<1900);
    assert(rsx_fifo_idle_wait(event,1)==0);
    // No handle or a failed OS wait retains bounded Sleep, not busy retry.
    begin=std::chrono::steady_clock::now();assert(rsx_fifo_idle_wait(nullptr,20)==-1);
    assert((std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()>=15));
    inject_wait_failure=true;assert(rsx_fifo_idle_wait(event,1)==-1);
    inject_wait_failure=false;
    CloseHandle(event);
    printf("PASS FIFO delivery classification + idle: mode gating, pre-signal/coalescing, cross-thread wake (%.1fms), timeout and failure fallback\n",elapsed);
}
