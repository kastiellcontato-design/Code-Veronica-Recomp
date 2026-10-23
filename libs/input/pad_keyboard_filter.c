#include "pad_keyboard_filter.h"
#include <stdatomic.h>
static atomic_uint reserved_keys=0;
void pad_keyboard_reserve(unsigned first,unsigned second){
    unsigned keys=(first<256u?first:0u)|((second<256u?second:0u)<<16);
    atomic_store_explicit(&reserved_keys,keys,memory_order_relaxed);
}
unsigned pad_keyboard_reserved_keys(void){return atomic_load_explicit(&reserved_keys,memory_order_relaxed);}
