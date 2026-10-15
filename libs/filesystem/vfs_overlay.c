/* vfs_overlay.c -- see vfs_overlay.h. */
#include "vfs_overlay.h"

static vfs_overlay_fn s_fn;

void vfs_overlay_set(vfs_overlay_fn fn) { s_fn = fn; }

const char* vfs_overlay_resolve(const char* host_path, char* buf, size_t cap)
{
    if (!s_fn || !host_path) return host_path;
    const char* r = s_fn(host_path, buf, cap);
    return r ? r : host_path;
}
