#pragma once
/* A native menu may reserve two virtual keys before keyboard-to-pad mapping.
 * Physical pad input is separate. Zero/zero releases the reservation. */
#ifdef __cplusplus
extern "C" {
#endif
void pad_keyboard_reserve(unsigned first,unsigned second);
unsigned pad_keyboard_reserved_keys(void);
static inline int pad_keyboard_key_down(unsigned reserved,unsigned vk,int down){
    return down && vk!=(reserved&0xffffu) && vk!=(reserved>>16);
}
#ifdef __cplusplus
}
#endif
