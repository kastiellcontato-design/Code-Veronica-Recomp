/* Windows FIFO idle policy. No guest clock or command execution here. */
#ifndef RSX_FIFO_IDLE_H
#define RSX_FIFO_IDLE_H
#include <string.h>
static inline int rsx_fifo_idle_enabled(const char* fps,const char* interpolation,const char* override_value) {
    return fps&&!strcmp(fps,"1")&&interpolation&&!strcmp(interpolation,"1")&&
           !(override_value&&!strcmp(override_value,"0"));
}
#include "../../runtime/platform/win32_compat.h"
/* Event is auto-reset: signals coalesce and a signal before wait is retained.
 * Timeouts preserve the old idle interval. Failed/missing handles must not
 * turn into an unbounded busy retry; fall back to the original sleep. */
static inline int rsx_fifo_idle_wait(HANDLE event,unsigned ms) {
    if(event) {
        DWORD result=WaitForSingleObject(event,ms);
        if(result==WAIT_OBJECT_0)return 1;
        if(result==WAIT_TIMEOUT)return 0;
    }
    Sleep(ms);return -1;
}
#endif
