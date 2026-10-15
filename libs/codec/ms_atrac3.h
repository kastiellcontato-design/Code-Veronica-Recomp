/* Native decode for the MultiStream ATRAC3 plug-in -- see ms_atrac3.c. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* decodeATRACPackets: `ls` is the SPU local store, r[0..7] = $r3..$r10.
 * Returns the value the plug-in leaves in $r3. */
uint32_t ms_atrac3_decode(uint8_t* ls, const uint32_t r[8]);

#ifdef __cplusplus
}
#endif
