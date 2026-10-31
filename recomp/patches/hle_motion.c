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
 *     moves the pointer, as two sensor-bar dots in the IR camera.
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
void (*g_host_sticks_read)(float* lx, float* ly, float* rx, float* ry);

/* One g in the accelerometer's units (hle_wpad.c's), and a swing. */
#define G 104
#define SWING_REPORTS 6 /* 30 ms at 200 reports a second */
#define PI 3.14159265f

enum { HOLD_SIDEWAYS, HOLD_UPRIGHT };
enum { USE_SHAKE = 1, USE_STEER = 2, USE_PITCH = 4, USE_SWING = 8, USE_PUSH = 16, USE_BAT = 32, USE_POINTER = 64 };

static const struct {
    int at, hold, use;
} kMinigames[] = {
    {1, HOLD_SIDEWAYS, USE_SHAKE},                          /* Absol's Hurdle Bounce */
    {2, HOLD_SIDEWAYS, USE_STEER | USE_PITCH | USE_POINTER}, /* Rayquaza's Balloon Panic */
    {3, HOLD_UPRIGHT, USE_SWING | USE_PUSH},                /* Venusaur's Vine Swing */
    {4, HOLD_UPRIGHT, USE_SWING | USE_PUSH},                /* Tangrowth's Swing-Along */
    {5, HOLD_SIDEWAYS, USE_SHAKE},                          /* Dusknoir's Speed Slam */
    {6, HOLD_SIDEWAYS, USE_STEER},                          /* Gyarados' Aqua Dash: a button for speed */
    {7, HOLD_SIDEWAYS, USE_STEER | USE_PITCH},              /* Pelipper's Circle Circuit */
    {9, HOLD_SIDEWAYS, USE_STEER},                          /* Empoleon's Snow Slide */
    {10, HOLD_SIDEWAYS, 0},                                 /* Bastiodon's Panel Crush: buttons */
    {11, HOLD_SIDEWAYS, USE_STEER | USE_PITCH},             /* Rhyperior's Bumper Burn */
    {12, HOLD_UPRIGHT, USE_BAT | USE_SHAKE},                /* Blaziken's Boulder Bash */
    {13, HOLD_SIDEWAYS, USE_POINTER | USE_SHAKE},           /* Rotom's Spooky Shoot-'em-Up */
    {15, HOLD_SIDEWAYS, USE_STEER | USE_PITCH},             /* Salamence's Air Race */
    {16, HOLD_SIDEWAYS, USE_SHAKE},                         /* Bulbasaur's Daring Dash */
};

/* How far the keys and stick tilt the remote, and how quickly it gets
 * there (a share of the way each report: about 30 ms). */
#define MAX_STEER (35.0f * PI / 180.0f)
#define MAX_PITCH (30.0f * PI / 180.0f)
#define EASE 0.15f

/* The pointer: how far a pixel of mouse movement and a second of full
 * stick move it (the screen is -1 to 1 across), and where its dots sit in
 * the IR camera's 1024x768 view. */
#define POINTER_PER_PIXEL (1.0f / 600.0f)
#define POINTER_PER_STICK_SECOND 1.5f
#define DOT_SPREAD 100 /* each dot from the middle */

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

int hle_motion(uint32_t held, uint32_t report, int16_t acc[3], int16_t dots[4][2]) {
    static float steer, pitch, px, py;
    static Gesture push, bat;
    int at = hle_minigame(), i, use = -1, hold = HOLD_SIDEWAYS, swing;
    float lx = 0, ly = 0, rx = 0, ry = 0, x, y, z, wantSteer = 0, wantPitch = 0;
    if (!at) return 0;
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
        wantSteer = lx != 0 ? lx : (held & HOST_PAD_SCREEN_RIGHT ? 1.0f : 0) - (held & HOST_PAD_SCREEN_LEFT ? 1.0f : 0);
        if (flip("RECOMP_TILT_FLIP", "lr")) wantSteer = -wantSteer;
    }
    if (use & USE_PITCH) {
        /* Up climbs: the remote tilted back. */
        wantPitch = ly != 0 ? -ly : (held & HOST_PAD_SCREEN_DOWN ? 1.0f : 0) - (held & HOST_PAD_SCREEN_UP ? 1.0f : 0);
        if (flip("RECOMP_TILT_FLIP", "fb")) wantPitch = -wantPitch;
    }
    steer += (wantSteer * MAX_STEER - steer) * EASE;
    pitch += (wantPitch * MAX_PITCH - pitch) * EASE;

    /* Gravity, as the accelerometer reads it (up along an axis reads +g on
     * it), in the remote's axes: x across its face, y along it (toward the
     * pointer end), z out of its face. Sideways, the pointer end is to the
     * left: steering right lifts it, and tilting forward lifts the edge
     * nearest the player (+x). Upright, the pointer end is up and the face
     * toward the player. */
    if (hold == HOLD_UPRIGHT) {
        x = 0;
        y = (float)G;
        z = 0;
    } else {
        x = sinf(pitch) * cosf(steer) * G;
        y = sinf(steer) * G;
        z = cosf(pitch) * cosf(steer) * G;
    }

    /* Bursts of acceleration on top. */
    swing = (use & (USE_SHAKE | USE_SWING | USE_BAT)) ? shake_swing(held, report) : 0;
    if (swing) {
        /* Up and down, out of its face, whichever way it is held: running
         * reads only that (in Bulbasaur's dash, a shake along its length
         * did nothing); upright, that is toward the screen and away. With
         * a bat that would swing it, so there the shake (to recover from
         * a boulder) goes from side to side. */
        if (use & USE_BAT) x += swing * G * 5 / 2;
        else z += swing * G * 5 / 2;
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
    /* The pointer, as the IR camera sees the sensor bar: the dots move
     * the other way to the pointer (pointing right puts the bar to the left
     * of the view, pointing up puts it lower). */
    {
        float dx = 0, dy = 0;
        if (g_host_pointer_read) g_host_pointer_read(&dx, &dy);
        px += dx * POINTER_PER_PIXEL + rx * POINTER_PER_STICK_SECOND / 200.0f;
        py += -dy * POINTER_PER_PIXEL + ry * POINTER_PER_STICK_SECOND / 200.0f;
        px = px > 1 ? 1 : px < -1 ? -1 : px;
        py = py > 1 ? 1 : py < -1 ? -1 : py;
    }
    {
        float yUp = flip("RECOMP_POINTER_FLIP", "y") ? -py : py;
        /* Measured against KPAD's pointer (pos, -1 to 1, y down): x moves
         * 1/225 of the view per pixel; y 1/182, and with the sensor bar
         * below the screen (SYSCONF) the middle is 102 pixels lower. */
        int mx = (int)(512 - px * 225), my = (int)(486 + yUp * 226);
        dots[0][0] = (int16_t)(mx - DOT_SPREAD);
        dots[0][1] = (int16_t)my;
        dots[1][0] = (int16_t)(mx + DOT_SPREAD);
        dots[1][1] = (int16_t)my;
    }
    return 3;
}
