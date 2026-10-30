/* User settings, kept in settings.ini beside the program and changed in
 * the settings menu (Backspace, or Back+Start on a controller). For the
 * platform layer (recomp/boot, recomp/gpu); the game patches don't see
 * them. RECOMP_SETTINGS=<file> reads and writes another file instead. */
#pragma once

#include <stdint.h>

/* What can be bound: the Wii Remote's buttons, held sideways. */
enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_2, ACT_1, ACT_A, ACT_B, ACT_PLUS, ACT_MINUS, ACT_HOME, ACT_SHAKE,
    ACT_COUNT
};
#define BIND_SLOTS 2

/* Controller buttons: XInput's, plus the triggers above its 16 bits. */
enum {
    XPAD_UP = 0x0001, XPAD_DOWN = 0x0002, XPAD_LEFT = 0x0004, XPAD_RIGHT = 0x0008,
    XPAD_START = 0x0010, XPAD_BACK = 0x0020, XPAD_LTHUMB = 0x0040, XPAD_RTHUMB = 0x0080,
    XPAD_LB = 0x0100, XPAD_RB = 0x0200, XPAD_A = 0x1000, XPAD_B = 0x2000, XPAD_X = 0x4000, XPAD_Y = 0x8000,
    XPAD_LT = 0x10000, XPAD_RT = 0x20000,
};

#define MAX_SCALE 6
#define MAX_MSAA 8

typedef struct {
    int fullscreen;          /* borderless, on the window's monitor */
    int scale;               /* internal resolution multiplier; 0 picks one for the window */
    int msaa;                /* anti-aliasing samples: 1 (off), 2, 4 or 8 */
    int frame_rate;          /* 30 (the game's own), 60, or 120-240 (interpolated) */
    int fov;                 /* field of view, percent of the game's (tangent of half the angle) */
    float mouse_sensitivity; /* camera turn per pixel, times the default */
    int invert_tilt;         /* mouse up looks down */
    int key[ACT_COUNT][BIND_SLOTS];      /* Windows virtual-key codes (mouse buttons too); 0 = none */
    uint32_t pad[ACT_COUNT][BIND_SLOTS]; /* controller buttons (XPAD_*); 0 = none */
} HostSettings;

extern HostSettings g_settings;

void settings_load(void);
void settings_save(void);
void settings_default_controls(void);

/* The settings menu (menu_win32.c). */
int menu_is_open(void);
/* Takes the window's keyboard and mouse messages while open; returns 1 if
 * it used the message. */
int menu_window_message(void* wnd, unsigned msg, uintptr_t wp, intptr_t lp);
/* The controller's buttons held (XPAD_*), at every input poll: without
 * and with the left stick's directions as the d-pad's. Opens and closes
 * the menu on Back+Start, and works it while open. */
void menu_controller(uint32_t buttons, uint32_t nav);
/* The menu as a w x h BGRA image with alpha, top row first, to lay over
 * the picture; NULL when closed. */
const uint32_t* menu_draw(int w, int h);
