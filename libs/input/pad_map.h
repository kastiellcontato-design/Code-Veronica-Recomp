/*
 * ps3recomp - default pad mapping shared by cellPad and title launchers.
 *
 * Each PS3 pad action is driven by one controller button (by position, see
 * pad_phys.h) and one keyboard key (a Windows virtual-key code). cellPad reads
 * overrides from PAD_BTNMAP / PAD_KEYMAP ("cross=south,circle=east,..." and
 * "cross=32,circle=8,..."), which a launcher sets from its settings.
 */
#ifndef PS3RECOMP_PAD_MAP_H
#define PS3RECOMP_PAD_MAP_H

#include "pad_phys.h"

enum {
    PA_CROSS = 0, PA_CIRCLE, PA_SQUARE, PA_TRIANGLE,
    PA_L1, PA_R1, PA_L2, PA_R2, PA_L3, PA_R3,
    PA_SELECT, PA_START, PA_UP, PA_DOWN, PA_LEFT, PA_RIGHT,
    PA_COUNT
};

static const char* const k_pad_action_id[PA_COUNT] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3",
    "select", "start", "up", "down", "left", "right"
};

/* Same position on the controller: Cross is the south button, and so on. */
static const int k_pad_default_btn[PA_COUNT] = {
    PB_SOUTH, PB_EAST, PB_WEST, PB_NORTH, PB_L1, PB_R1, PB_L2, PB_R2, PB_L3, PB_R3,
    PB_SELECT, PB_START, PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT
};

/* Keyboard: WASD moves, Space acts, Shift runs (Square), E/Q are R1/L1. */
static const int k_pad_default_key[PA_COUNT] = {
    0x20 /* Space */, 0x08 /* Backspace */, 0xA0 /* Left Shift */, 'F',
    'Q', 'E', '1', '3', 'Z', 'C',
    0x09 /* Tab */, 0x0D /* Return */, 'W', 'S', 'A', 'D'
};

#endif
