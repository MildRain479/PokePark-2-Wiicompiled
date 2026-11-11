/* Minigame controls: keys, sticks and the mouse as the Wii Remote's
 * motion. The minigames are scripts, and the only motion they ask about
 * (AttractionInput::virt0x18) is the remote's tilt (KPAD's acceleration,
 * x and y), how upright it is (KPAD's acc_vertical), and in two of them
 * where it points; besides those, shakes, swings and pushes are bursts of
 * acceleration. Each minigame's instructions (Lobby.bmg) say how it is
 * played; here each gets the remote held as they ask, moved by:
 *
 *   running (Absol, Bulbasaur, Dusknoir): shake = the two shake keys by
 *     turns (Q and E, LB and RB): a swing up or down for each press after
 *     the other key's, so the faster they go by turns, the faster the
 *     shaking (about 8 presses a second wins Bulbasaur's dash in 9 s);
 *   swinging on a vine or rope (Venusaur, Tangrowth): held upright; the
 *     shake keys by turns swing it back and forth, in time with the vine;
 *     B pushes it forward to jump, as the game asks ("Push [B] as shown"),
 *     and so does 2 (Space);
 *   bashing boulders (Blaziken): held upright; 1 or 2 (left click, Space)
 *     swings it like a bat, toward the screen; the shake keys by turns
 *     shake it from side to side to recover;
 *   steering (Gyarados, Empoleon): held sideways, tilted left and right
 *     by left/right (A/D, arrows) or the left stick, in proportion;
 *   flying and rolling (Pelipper, Salamence, Rhyperior, Rayquaza): also
 *     tilted forward and back, by up/down or the stick: up tilts it back
 *     (climb), down forward (dive);
 *   pointing (Rotom, Rayquaza's last part): the mouse or the right stick
 *     moves the pointer, as two sensor-bar dots in the IR camera; past
 *     the screen's sides it turns Rotom's view.
 *
 * Which minigame takes what is minigames.h, one for each game.
 *
 * The d-pad still goes to the game as well. RECOMP_TILT_FLIP=lr, fb or
 * lrfb turns tilting the other way, and RECOMP_POINTER_FLIP=y moves the
 * pointer the other way up, in case a minigame expects it so. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "host_input.h"

void (*g_host_pointer_read)(float* dx, float* dy);

/* One g in the accelerometer's units (hle_wpad.c's), and a swing. */
#define G 104
#define SWING_REPORTS 6 /* 30 ms at 200 reports a second */
#define PI 3.14159265f

enum { HOLD_SIDEWAYS, HOLD_UPRIGHT };
enum { USE_SHAKE = 1, USE_STEER = 2, USE_PITCH = 4, USE_SWING = 8, USE_PUSH = 16, USE_BAT = 32, USE_POINTER = 64, USE_ANY_WAY = 128 };

#include "minigames.h"

/* How far the keys and stick tilt the remote, and how quickly it gets
 * there (a share of the way each report: about 30 ms). The minigames'
 * scripts read the accelerometer themselves (FScriptLoader::syscall0x15);
 * at 35 degrees their turns were too wide to make, so left and right go
 * all the way over. They also go by readings of about 1 g alone, as
 * from a remote held still: at 2.4 g across, Gyarados' Aqua Dash kept
 * turning right for as long as left was held after it. */
#define MAX_STEER (90.0f * PI / 180.0f)
/* Forward and back as far as climbing goes: Pelipper's balloon climbs
 * as fast at 50 degrees as at 70, and at 30 too slowly. */
#define MAX_PITCH (50.0f * PI / 180.0f)
#define EASE 0.15f

/* The pointer: how far a pixel of mouse movement and a second of full
 * stick move it (the screen is -1 to 1 across), and where its dots sit in
 * the IR camera's 1024x768 view. */
#define POINTER_PER_PIXEL (1.0f / 600.0f)
#define POINTER_PER_STICK_SECOND 1.5f
#define DOT_SPREAD 100 /* each dot from the middle */

/* How far the pointer goes across, past the screen's edges: Rotom's game
 * turns to look around while it points past them (from about 1.4; the
 * cursor stays at the edge). Much past 1.6 the game loses the pointer
 * (1.8 hid it and stopped the turning). */
#define POINTER_X_LIMIT 1.6f

static int flip(const char* env, const char* what) {
    const char* e = getenv(env);
    return e && strstr(e, what) != NULL;
}

static int16_t clamp16(float v) { return (int16_t)(v > 511 ? 511 : v < -512 ? -512 : v); }

/* A swing of the shake keys by turns: +1 or -1 while one lasts, else 0.
 * Holding a key, or the same key again, makes none. */
static int shake_swing(uint32_t held, uint32_t report) {
    static uint32_t last, swingEnd;
    static int side, sign;
    uint32_t pressed = held & ~last & (HOST_PAD_SHAKE | HOST_PAD_SHAKE2);
    int which = pressed == HOST_PAD_SHAKE ? 1 : pressed == HOST_PAD_SHAKE2 ? 2 : 0;
    last = held;
    if (which && which != side) {
        side = which;
        sign = which == 1 ? 1 : -1;
        swingEnd = report + SWING_REPORTS;
    }
    return (int32_t)(swingEnd - report) > 0 ? sign : 0;
}

/* A pair of opposite keys: +1 for plus, -1 for minus, and with both held
 * the one pressed last, so rolling from one to the other turns straight
 * over instead of stopping until the first is let go. */
typedef struct {
    uint32_t last;
    int side;
} KeyPair;

static float key_pair(KeyPair* k, uint32_t held, uint32_t plus, uint32_t minus) {
    uint32_t pressed = held & ~k->last;
    k->last = held;
    if (!(held & minus)) k->side = held & plus ? 1 : 0;
    else if (!(held & plus)) k->side = -1;
    else if (pressed & plus) k->side = 1;
    else if (pressed & minus) k->side = -1;
    return (float)k->side;
}

/* A gesture set off by a press of one of the keys in mask, lasting
 * length reports: the reports since it began, or -1 if none is under way. */
typedef struct {
    uint32_t last, began;
    int on;
} Gesture;

static int gesture(Gesture* g, uint32_t held, uint32_t mask, uint32_t report, int length) {
    if (g->on && report - g->began >= (uint32_t)length) g->on = 0;
    if ((held & mask) && !(g->last & mask) && !g->on) {
        g->began = report;
        g->on = 1;
    }
    g->last = held;
    return g->on ? (int)(report - g->began) : -1;
}

/* The pointer, as the IR camera sees the sensor bar: the dots move the
 * other way to the pointer (pointing right puts the bar to the left of the
 * view, pointing up puts it lower). */
static void pointer_dots(float rx, float ry, int16_t dots[4][2]) {
    static float px, py;
    float dx = 0, dy = 0, yUp;
    int mx, my;
    if (g_host_pointer_read) g_host_pointer_read(&dx, &dy);
    px += dx * POINTER_PER_PIXEL + rx * POINTER_PER_STICK_SECOND / 200.0f;
    py += -dy * POINTER_PER_PIXEL + ry * POINTER_PER_STICK_SECOND / 200.0f;
    px = px > POINTER_X_LIMIT ? POINTER_X_LIMIT : px < -POINTER_X_LIMIT ? -POINTER_X_LIMIT : px;
    py = py > 1 ? 1 : py < -1 ? -1 : py;
    yUp = flip("RECOMP_POINTER_FLIP", "y") ? -py : py;
    /* Measured against KPAD's pointer (pos, -1 to 1, y down): x moves 1/225
     * of the view per pixel; y 1/182, and with the sensor bar below the
     * screen (SYSCONF) the middle is 102 pixels lower. */
    mx = (int)(512 - px * 225);
    my = (int)(486 + yUp * 226);
    dots[0][0] = (int16_t)(mx - DOT_SPREAD);
    dots[0][1] = (int16_t)my;
    dots[1][0] = (int16_t)(mx + DOT_SPREAD);
    dots[1][1] = (int16_t)my;
}

int hle_motion(uint32_t held, uint32_t report, int16_t acc[3], int16_t dots[4][2]) {
    static float steer, pitch;
    static Gesture push, bat;
    static KeyPair leftRight, upDown;
    int at = hle_minigame(), i, use = -1, hold = HOLD_SIDEWAYS, swing;
    float lx = 0, ly = 0, rx = 0, ry = 0, x, y, z, wantSteer = 0, wantPitch = 0;
    if (!at) {
        /* Outside minigames: the pointer alone, where the game asks for it. */
        if (!hle_pointer_wanted(held)) return 0;
        if (g_host_sticks_read) g_host_sticks_read(&lx, &ly, &rx, &ry);
        pointer_dots(rx, ry, dots);
        acc[0] = acc[1] = 0;
        acc[2] = G;
        return 2;
    }
    for (i = 0; i < (int)(sizeof(kMinigames) / sizeof(kMinigames[0])); i++) {
        if (kMinigames[i].at == at) {
            use = kMinigames[i].use;
            hold = kMinigames[i].hold;
        }
    }
    if (use < 0) use = USE_SHAKE; /* not one we know: shaking, which is common */
    if (g_host_sticks_read) g_host_sticks_read(&lx, &ly, &rx, &ry);

    /* Tilt, toward where the keys or the stick put it. */
    if (use & USE_STEER) {
        /* Right lowers the pointer end (to the left, held sideways), as
         * the minigames turn right: lifting it turned them left. */
        wantSteer = key_pair(&leftRight, held, HOST_PAD_SCREEN_LEFT, HOST_PAD_SCREEN_RIGHT);
        if (lx != 0) wantSteer = -lx;
        if (flip("RECOMP_TILT_FLIP", "lr")) wantSteer = -wantSteer;
    }
    if (use & USE_PITCH) {
        /* Up climbs: the remote tilted back. */
        wantPitch = key_pair(&upDown, held, HOST_PAD_SCREEN_DOWN, HOST_PAD_SCREEN_UP);
        if (ly != 0) wantPitch = -ly;
        if (flip("RECOMP_TILT_FLIP", "fb")) wantPitch = -wantPitch;
    }
    steer += (wantSteer * MAX_STEER - steer) * EASE;
    pitch += (wantPitch * MAX_PITCH - pitch) * EASE;

    /* Gravity, as the accelerometer reads it (up along an axis reads +g on
     * it), in the remote's axes: x across its face, y along it (toward the
     * pointer end), z out of its face. Sideways, the pointer end is to the
     * left: a positive steer lifts it (turning left), and tilting forward
     * lifts the edge nearest the player (+x). Upright, the pointer end is up and the face
     * toward the player. Steering and pitch each set their own axis, as
     * if tilted one way and then the other, so steering hard keeps most of
     * the climb or dive (a single rotation would take it away); z, out of
     * the face, makes up the rest of g. Together they could come to more
     * than g, which the games would pass over, so then both shrink. */
    if (hold == HOLD_UPRIGHT) {
        x = 0;
        y = (float)G;
        z = 0;
    } else {
        float across;
        x = sinf(pitch) * G;
        y = sinf(steer) * G;
        across = sqrtf(x * x + y * y);
        if (across > G) {
            x = x * G / across;
            y = y * G / across;
            across = (float)G;
        }
        z = sqrtf((float)G * G - across * across);
    }

    /* Bursts of acceleration on top. */
    swing = (use & (USE_SHAKE | USE_SWING | USE_BAT | USE_ANY_WAY)) ? shake_swing(held, report) : 0;
    if (swing) {
        /* Up and down, out of its face, whichever way it is held: running
         * reads only that (in Bulbasaur's dash, a shake along its length
         * did nothing); upright, that is toward the screen and away. With
         * a bat that would swing it, so there the shake (to recover from
         * a boulder) goes from side to side. */
        if (use & USE_BAT) x += swing * G * 5 / 2;
        else z += swing * G * 5 / 2;
        /* PokePark 2's Power Bash goes by the swing's size whatever its
         * direction, along the remote's length included. */
        if (use & USE_ANY_WAY) {
            x += swing * G * 5 / 2;
            y += swing * G * 5 / 2;
        }
    }
    if (use & USE_PUSH) {
        /* A push toward the screen: a thrust, then the stop. */
        int t = gesture(&push, held, HOST_PAD_B | HOST_PAD_2, report, 10);
        if (t >= 0) z += t < 5 ? -3 * G : 3 * G / 2;
    }
    if (use & USE_BAT) {
        /* A bat's swing toward the screen, then the follow-through: the
         * one way Blaziken's game counts (scored 400 one run; swung across
         * the body or along its length, 0). */
        int t = gesture(&bat, held, HOST_PAD_1 | HOST_PAD_2, report, 14);
        if (t >= 0) z += t < 8 ? 3 * G : -2 * G;
    }
    acc[0] = clamp16(x);
    acc[1] = clamp16(y);
    acc[2] = clamp16(z);

    if (!(use & USE_POINTER)) return 1;
    pointer_dots(rx, ry, dots);
    return 3;
}
