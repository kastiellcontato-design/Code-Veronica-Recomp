/* Read-only FIFO delivery diagnostics. A pending header is not proof that
 * execution can advance; in particular, a self-jump is an intentional park. */
#ifndef RSX_FIFO_PROBE_H
#define RSX_FIFO_PROBE_H
#include <stdint.h>
enum { RSX_FIFO_INACTIVE, RSX_FIFO_EMPTY, RSX_FIFO_PARK,
       RSX_FIFO_SEMAPHORE, RSX_FIFO_PENDING, RSX_FIFO_UNMAPPED,
       RSX_FIFO_PROBE_BUSY, RSX_FIFO_PROBE_STATES };
typedef struct rsx_fifo_probe {
    uint32_t get, put, head;
    unsigned state;
} rsx_fifo_probe;
static inline unsigned rsx_fifo_probe_classify(uint32_t get, uint32_t put,
                                              uint32_t head, int mapped) {
    if(get==put)return RSX_FIFO_EMPTY;
    if(!mapped)return RSX_FIFO_UNMAPPED;
    if((head>>29)==1 && (head&0x1ffffffcu)==get)return RSX_FIFO_PARK;
    /* Native walker decodes incrementing/nonincrementing method headers.
     * A semaphore header is a candidate wait, not necessarily blocked. */
    unsigned type=head>>29;
    if((type==0 || type==2) && (head&3u)!=2 &&
       ((head>>18)&0x7ffu)!=0 && ((head>>13)&7u)==0 &&
       (head&0x1ffcu)==0x68u)return RSX_FIFO_SEMAPHORE;
    return RSX_FIFO_PENDING;
}
static inline int rsx_fifo_probe_head_changed(const rsx_fifo_probe* a,
                                             const rsx_fifo_probe* b) {
    return a->state!=RSX_FIFO_INACTIVE && a->state!=RSX_FIFO_PROBE_BUSY &&
           b->state!=RSX_FIFO_INACTIVE && b->state!=RSX_FIFO_PROBE_BUSY &&
           a->state!=RSX_FIFO_UNMAPPED && b->state!=RSX_FIFO_UNMAPPED &&
           a->state!=RSX_FIFO_EMPTY && b->state!=RSX_FIFO_EMPTY &&
           a->get==b->get && a->head!=b->head;
}
#endif
