#ifndef SPU_FAULT_SNAPSHOT_H
#define SPU_FAULT_SNAPSHOT_H

#include "spu_context.h"

/* Failure-only, opt-in snapshot. Never changes DMA acceptance or guest state.
 * SPU_FAULT_DIR must name an existing directory. At most two files/process. */
int spu_fault_snapshot(const spu_context* ctx, uint32_t taskset,
                       uint32_t task, uint32_t args);
/* Bounded history, enabled only alongside SPU_FAULT_DIR. image=0 associates
 * HLE events with the taskset identified by the image-10 dispatcher. */
void spu_fault_event(const char* phase, uint32_t taskset, uint32_t task,
                     int image, uint32_t detail);

#endif
