/* vfs_overlay.h -- let a title serve its own copy of a game file.
 *
 * A port can swap a data file the guest opens for a generated one (a mod, a
 * localised asset, a different set of button icons) without the runtime
 * knowing anything about the title. The title registers one callback; every
 * guest open (cellFsOpen, cellFsSdataOpen, sys_fs_open) passes the resolved
 * host path through it right after EDAT decryption.
 */
#ifndef PS3RECOMP_VFS_OVERLAY_H
#define PS3RECOMP_VFS_OVERLAY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return host_path to open the file as it is, or a path written into buf. */
typedef const char* (*vfs_overlay_fn)(const char* host_path, char* buf, size_t cap);

void vfs_overlay_set(vfs_overlay_fn fn);

/* Run the registered callback (host_path when none is set). */
const char* vfs_overlay_resolve(const char* host_path, char* buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_VFS_OVERLAY_H */
