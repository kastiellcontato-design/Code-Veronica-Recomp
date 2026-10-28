/*
 * ps3recomp - default pad mapping shared by cellPad and title launchers.
 *
 * Each PS3 pad action is driven by up to PAD_BTN_SLOTS controller buttons (by
 * position, see pad_phys.h) and up to PAD_KEY_SLOTS keyboard keys (Windows
 * virtual-key codes); the first slot is the one the game's icons show. cellPad
 * reads overrides from PAD_BTNMAP / PAD_KEYMAP ("cross=south+west,..." and
 * "cross=32+75,circle=8,...": slots joined by '+', "none" / 0 for an empty
 * one), which a launcher sets from its settings.
 */
#ifndef PS3RECOMP_PAD_MAP_H
#define PS3RECOMP_PAD_MAP_H

#include "pad_phys.h"

enum {
    PA_CROSS = 0, PA_CIRCLE, PA_SQUARE, PA_TRIANGLE,
    PA_L1, PA_R1, PA_L2, PA_R2, PA_L3, PA_R3,
    PA_SELECT, PA_START, PA_UP, PA_DOWN, PA_LEFT, PA_RIGHT,
    PA_LS_UP, PA_LS_DOWN, PA_LS_LEFT, PA_LS_RIGHT,
    PA_RS_UP, PA_RS_DOWN, PA_RS_LEFT, PA_RS_RIGHT,
    PA_COUNT
};

#define PAD_KEY_SLOTS 3
#define PAD_BTN_SLOTS 2

static const char* const k_pad_action_id[PA_COUNT] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3",
    "select", "start", "up", "down", "left", "right",
    "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right"
};

/* Same position on the controller: Cross is the south button, and so on. */
static const int k_pad_default_btn[PA_COUNT] = {
    PB_SOUTH, PB_EAST, PB_WEST, PB_NORTH, PB_L1, PB_R1, PB_L2, PB_R2, PB_L3, PB_R3,
    PB_SELECT, PB_START, PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT,
    PB_COUNT, PB_COUNT+1, PB_COUNT+2, PB_COUNT+3, PB_COUNT+4, PB_COUNT+5, PB_COUNT+6, PB_COUNT+7
};

/* Keyboard: WASD drives the left stick; arrow keys drive the D-pad.
 * Existing INI overrides take precedence; Restore defaults uses this table. */
static const int k_pad_default_key[PA_COUNT] = {
    0x20 /* Space */, 0x08 /* Backspace */, 0xA0 /* Left Shift */, 'F',
    'Q', 'E', '1', '3', 'Z', 'C',
    0x09 /* Tab */, 0x0D /* Return */,
    0x26 /* Up */, 0x28 /* Down */, 0x25 /* Left */, 0x27 /* Right */,
    'W', 'S', 'A', 'D', 0, 0, 0, 0
};

#endif
