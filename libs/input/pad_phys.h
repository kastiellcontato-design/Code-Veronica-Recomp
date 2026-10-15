/*
 * ps3recomp - physical controller reader
 *
 * One view of whatever gamepads are plugged in, independent of what the
 * guest's pad mapping does with them: Xbox controllers through XInput, and
 * Sony controllers (DualShock 4, DualSense, DualSense Edge -- USB and
 * Bluetooth) read directly over HID, so a PlayStation controller works with no
 * driver or wrapper installed. Buttons are named by POSITION (south = A on
 * Xbox, Cross on PlayStation), which is what lets one mapping serve both.
 *
 * Used by cellPad (the guest's pad) and by a title's launcher (button capture).
 */
#ifndef PS3RECOMP_PAD_PHYS_H
#define PS3RECOMP_PAD_PHYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PB_SOUTH = 0, PB_EAST, PB_WEST, PB_NORTH,   /* A B X Y  /  Cross Circle Square Triangle */
    PB_L1, PB_R1, PB_L2, PB_R2, PB_L3, PB_R3,
    PB_SELECT, PB_START,                          /* View/Back, Menu  /  Share-Create, Options */
    PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT,
    PB_HOME, PB_TOUCH,
    PB_COUNT
};

enum { PAD_KIND_NONE = 0, PAD_KIND_XINPUT = 1, PAD_KIND_DS4 = 2, PAD_KIND_DUALSENSE = 3 };

/* The device the player used last, as the guest's pad sees it (cellPad):
 * a PAD_KIND_* controller, PAD_INPUT_KEYBOARD, or PAD_KIND_NONE before any
 * input. Pressing a mapped key or a button, or pushing a stick, counts. */
#define PAD_INPUT_KEYBOARD 100
int pad_last_input(void);

typedef struct {
    int      connected;
    int      kind;               /* PAD_KIND_* */
    uint32_t buttons;            /* bit (1u << PB_*) */
    uint8_t  lx, ly, rx, ry;     /* 0..255, centre 128, up = 0 (PS3 convention) */
    uint8_t  l2, r2;             /* analog triggers 0..255 */
} PadPhys;

/* Fill `out` for controller `index` (0..PAD_PHYS_MAX-1): XInput pads first,
 * then Sony pads, in connection order. Returns 1 if connected. */
#define PAD_PHYS_MAX 4
int pad_phys_poll(int index, PadPhys* out);

/* Set controller `index`'s motors (same numbering as pad_phys_poll): `large`
 * is the strong low-frequency motor, `weak` the weak high-frequency one,
 * 0..255 each. Sony pads are written only when the values change. */
void pad_phys_rumble(int index, uint8_t large, uint8_t weak);
void pad_phys_rumble_off(void);   /* every controller */

/* 1 when a controller of this PAD_KIND_* uses the Xbox 360 release's layout:
 * pad_phys_poll then reports RT/LT (R2/L2) as R1/L1, RB/LB (R1/L1) as R2/L2,
 * Start as Select and Back (Share/Create) as Start. Set by PAD_360_LAYOUT =
 * on | off | auto (default: Xbox controllers only). */
int pad_phys_360_layout(int kind);

/* Short identifiers used in config strings ("south", "l1", "up", ...). */
const char* pad_phys_id(int pb);
int         pad_phys_from_id(const char* id);   /* -1 if unknown */

#ifdef __cplusplus
}
#endif

#endif
