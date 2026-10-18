#ifndef PS3RECOMP_PAD_SHORTCUT_GATE_H
#define PS3RECOMP_PAD_SHORTCUT_GATE_H
#include "pad_phys.h"
/* Per-controller latch, before user remapping. Full shoulder prefix reserves
 * the shortcut; keep its release tail away from the guest as well. */
static inline int pad_shortcut_gate(unsigned* held, uint32_t buttons) {
    const uint32_t prefix=(1u<<PB_L1)|(1u<<PB_R1)|(1u<<PB_L2)|(1u<<PB_R2);
    const uint32_t reserved=prefix|(1u<<PB_SOUTH)|(1u<<PB_EAST)|(1u<<PB_WEST)|(1u<<PB_NORTH)|(1u<<PB_L3)|(1u<<PB_R3);
    if((buttons&prefix)==prefix)*held=1;
    if(!*held)return 0;
    if(!(buttons&reserved))*held=0;
    return 1;
}
#endif
