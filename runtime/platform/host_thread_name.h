/* Name a host thread so profilers, debuggers and the [PERF] per-thread CPU
 * report can tell PPU, SPU and SPURS workers apart. No-op off Windows and on
 * systems without SetThreadDescription (resolved at run time). */
#ifndef PS3RECOMP_HOST_THREAD_NAME_H
#define PS3RECOMP_HOST_THREAD_NAME_H

#ifdef _WIN32
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

static inline void ps3_host_thread_name(HANDLE h, const char* fmt, ...)
{
    typedef HRESULT (WINAPI *set_desc_fn)(HANDLE, PCWSTR);
    static set_desc_fn fn = NULL;
    static int looked = 0;
    if (!looked) {
        HMODULE k = GetModuleHandleA("kernel32.dll");
        fn = k ? (set_desc_fn)(void*)GetProcAddress(k, "SetThreadDescription") : NULL;
        looked = 1;
    }
    if (!fn || !h) return;
    char s[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    wchar_t w[96];
    int i = 0;
    for (; s[i] && i < 95; i++) w[i] = (wchar_t)(unsigned char)s[i];
    w[i] = 0;
    fn(h, w);
}
#else
static inline void ps3_host_thread_name(void* h, const char* fmt, ...) { (void)h; (void)fmt; }
#endif

#endif
