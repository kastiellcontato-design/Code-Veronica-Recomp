/* Per-call-site getenv cache for the RSX hot paths.
 *
 * The video files read diagnostic switches with getenv() inline, many of them
 * per draw, per texture bind or per FIFO word. The CRT getenv takes a lock and
 * scans the whole environment each time; at Code Veronica's ~2300 draws per
 * frame that was tens of thousands of calls a frame. The environment is fixed
 * once the title boots (main only calls _putenv_s before the backend starts),
 * so each call site caches its first answer.
 *
 * Include AFTER <stdlib.h>. Only for getenv calls with a string-literal name:
 * the cache is per call site, not per name. The race on first use is benign
 * (every thread computes the same pointer). */
#ifndef RSX_GETENV_CACHE_H
#define RSX_GETENV_CACHE_H

#include <stdlib.h>

#if defined(__clang__) || defined(__GNUC__)
#define getenv(name) (__extension__({                                   \
    static const char* volatile _rsx_ge = (const char*)1;               \
    const char* _rsx_gr = _rsx_ge;                                      \
    if (_rsx_gr == (const char*)1) { _rsx_gr = (getenv)(name); _rsx_ge = _rsx_gr; } \
    _rsx_gr; }))
#endif

#endif
