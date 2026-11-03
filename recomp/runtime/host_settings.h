/* User settings, kept in settings.ini beside the program and changed in
 * Options in the pause menu (and the settings menu, menu_win32.c;
 * settings.c reads and writes them). For the
 * platform layer (recomp/boot, recomp/gpu); the game patches don't see
 * them. RECOMP_SETTINGS=<file> reads and writes another file instead. */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* What can be bound: the Wii Remote's buttons, held sideways. */
enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_2, ACT_1, ACT_A, ACT_B, ACT_PLUS, ACT_MINUS, ACT_HOME, ACT_SHAKE, ACT_SHAKE2,
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
enum { RENDERER_OPENGL, RENDERER_VULKAN };
#define MAX_MSAA 8

typedef struct {
    int fullscreen;          /* borderless, on the window's monitor */
    int scale;               /* internal resolution multiplier; 0 picks one for the window */
    int msaa;                /* anti-aliasing samples: 1 (off), 2, 4 or 8 */
    int frame_rate;          /* 30 (the game's own), 60, or 120-240 (interpolated) */
    int fov;                 /* field of view, percent of the game's (tangent of half the angle) */
    int aniso;               /* anisotropic filtering: 1 (off), 2, 4, 8 or 16 */
    int renderer;            /* RENDERER_*: what draws, from the next start */
    int rumble;              /* controller rumble: 0 off, 1 light, 2 normal, 3 strong */
    int touch_controls;      /* Android: the touch screen's controls, shown while it was used last; 0 never */
    int text_speed;          /* dialogue: 1 (the game's), 2, 3, or 0 (a page at once) */
    int berries;             /* berries collected, percent of the game's */
    int prices;              /* berries spent, percent of the game's */
    int damage_dealt;        /* damage Pikachu deals in battles, percent of the game's */
    int damage_taken;        /* damage Pikachu takes in battles, percent of the game's */
    int chase_speed;         /* the speed of the Pokémon Pikachu chases, percent of the game's */
    float mouse_sensitivity; /* camera turn per pixel, times the default */
    int invert_tilt;         /* mouse up looks down */
    int key[ACT_COUNT][BIND_SLOTS];      /* Windows virtual-key codes (mouse buttons too); 0 = none */
    uint32_t pad[ACT_COUNT][BIND_SLOTS]; /* controller buttons (XPAD_*); 0 = none */
} HostSettings;

extern HostSettings g_settings;

void settings_load(void);
void settings_save(void);
void settings_default_controls(void);

/* The name of the first key (or, with controller, controller button)
 * bound to action act, as the menu shows it; "" if none is. */
void settings_binding_name(int act, int controller, char* out, size_t n);
/* How many times the settings have been saved (so changed). */
uint32_t settings_generation(void);
/* Whether the controller was used more recently than the keyboard and
 * mouse (input_win32.c). */
int input_controller_last(void);
/* A short buzz at the rumble setting's strength (input_win32.c). */
void input_rumble_preview(void);
/* Sets up the button prompt icons (prompts.c). */
void prompts_init(void);

/* What settings_step changes. */
enum { SET_FULLSCREEN, SET_SCALE, SET_MSAA, SET_FRAME_RATE, SET_FOV, SET_SENSITIVITY, SET_INVERT, SET_ANISO,
       SET_RUMBLE, SET_TEXT_SPEED, SET_BERRIES, SET_PRICES,
       SET_DAMAGE_DEALT, SET_DAMAGE_TAKEN, SET_CHASE_SPEED, SET_RENDERER, SET_TOUCH };
/* Changes a setting a step (d: -1 or 1; on and off settings flip), applies
 * and saves it. */
void settings_step(int which, int d);
/* Binds key vk (or, if 0, controller button pad) to action act's slot,
 * taking it off any other (one key or button does one thing), and saves. */
void settings_bind(int act, int slot, int vk, uint32_t pad);
/* Names, as the menus show them: "-" for none. */
void settings_key_name(int vk, char* out, size_t n);
const char* settings_pad_name(uint32_t b);

/* The Options tab of the pause menu (options.c), as for the settings
 * menu below; used by it while that is closed. */
int options_is_open(void);
int options_window_message(unsigned msg, uintptr_t wp, intptr_t lp);
void options_controller(uint32_t buttons, uint32_t nav);
const uint32_t* options_draw(int w, int h);
/* How many times options_draw has drawn its image again (it returns the
 * same buffer while nothing changed). */
unsigned options_draws(void);
/* Android: a tap at window pixel (x, y); returns 1 if the tab used it. */
int options_touch(int x, int y);
/* Scripted presses (HOST_PAD_* bits held), as the controller's; returns 1
 * if the tab is open. */
int options_script(uint32_t held);
/* Android: the touch controls' buttons held, as for options_script. */
int options_touch_buttons(uint32_t held);

/* The settings menu (menu_win32.c; in public builds, without
 * RECOMP_DEBUG_MENU, these are the Options tab's alone). */
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
/* Android: changes whenever menu_draw's image may have, so a device can
 * leave an unchanged one where it is (input_android.c). */
unsigned menu_generation(void);
