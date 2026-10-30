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
    /* The second shake key: in minigames, a shake is a press of one
     * shake key after the other (hle_wpad.c). */
    HOST_PAD_SHAKE2 = 1 << 12,
};

/* Fills the HOST_PAD_* bits held on remote chan (0-3); returns 0 if that
 * remote is not connected. */
extern int (*g_host_pad_read)(int chan, uint32_t* held);
/* Turns the rumble motor of remote chan on or off. */
extern void (*g_host_pad_rumble)(int chan, int on);
/* Mouse movement since the last call, in pixels (x right, y down), while
 * the mouse is captured for the camera; 0 otherwise. Set by the platform
 * layer, read by the camera patch (hle_camera.c). */
extern void (*g_host_mouse_read)(float* dx, float* dy);
/* The same movement, collected separately for the remote's pointer in
 * minigames that use it (hle_motion.c). */
extern void (*g_host_pointer_read)(float* dx, float* dy);
/* The controller's sticks, -1 to 1 each (x right, y up), dead zone taken
 * off; 0 without a controller. */
extern void (*g_host_sticks_read)(float* lx, float* ly, float* rx, float* ry);
/* Button prompts (hle_prompts.c). The icon for a Wii Remote button (a
 * HOST_PAD_* bit; HOST_PAD_SCREEN_UP stands for the whole d-pad), as
 * coverage 0-255 in a w x h box, top row first: what the player has bound
 * to it, on the device used last. Returns 0 if there is none (the game's
 * own glyph stays). */
extern int (*g_host_prompt_icon)(uint32_t button, uint8_t* coverage, int w, int h);
/* Changes whenever those icons may have. */
extern uint32_t (*g_host_prompt_generation)(void);
