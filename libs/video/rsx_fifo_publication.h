#ifndef RSX_FIFO_PUBLICATION_H
#define RSX_FIFO_PUBLICATION_H
#include <stdint.h>
/* Only a transfer containing the complete published PUT word qualifies.
 * Vertex/texture DMA into an RSX mapping does not imply runnable commands. */
static inline int rsx_fifo_publication_contains(uint32_t addr,uint32_t size,uint32_t put_addr) {
    return put_addr!=0 && size>=4 && (uint64_t)addr<=put_addr &&
           (uint64_t)addr+size>=(uint64_t)put_addr+4;
}
#endif
