/* Controller input from the host, for the Wii Remote HLE (hle_wpad.c).
 * The platform layer (recomp/boot/input_win32.c) sets the hooks; without
 * them the remote is connected but nothing is pressed. */
#pragma once

#include <stdint.h>

/* The Wii Remote's buttons, as printed on it. The d-pad directions are the
 * remote's own (UP points toward the IR end); hle_wpad.c turns the
 * on-screen directions below into them for a remote held sideways. */
enum {
    HOST_PAD_SCREEN_UP = 1 << 0,
    HOST_PAD_SCREEN_DOWN = 1 << 1,
    HOST_PAD_SCREEN_LEFT = 1 << 2,
    HOST_PAD_SCREEN_RIGHT = 1 << 3,
    HOST_PAD_A = 1 << 4,
    HOST_PAD_B = 1 << 5,
    HOST_PAD_1 = 1 << 6,
    HOST_PAD_2 = 1 << 7,
    HOST_PAD_PLUS = 1 << 8,
    HOST_PAD_MINUS = 1 << 9,
    HOST_PAD_HOME = 1 << 10,
    HOST_PAD_SHAKE = 1 << 11,
};

/* Fills the HOST_PAD_* bits held on remote chan (0-3); returns 0 if that
 * remote is not connected. */
extern int (*g_host_pad_read)(int chan, uint32_t* held);
/* Turns the rumble motor of remote chan on or off. */
extern void (*g_host_pad_rumble)(int chan, int on);
