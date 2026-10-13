/* State that must follow a guest PPU context across native fiber switches. */
#ifndef PS3RECOMP_PPU_FIBER_BRIDGE_H
#define PS3RECOMP_PPU_FIBER_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PpuFiberBridgeState {
    void* active_ctx;
    void* return_env;
    int return_requested;
    void (*trampoline)(void*);
    uint32_t callstack[128];
    int call_depth;
    int indirect_depth;
} PpuFiberBridgeState;
void ppu_fiber_bridge_save(PpuFiberBridgeState* state);
void ppu_fiber_bridge_restore(const PpuFiberBridgeState* state);
void ppu_fiber_guest_start(uint32_t opd, uint64_t arg, uint32_t stack_top,
                           uint32_t from);
#ifdef __cplusplus
}
#endif
#endif
