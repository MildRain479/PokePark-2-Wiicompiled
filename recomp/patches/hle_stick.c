/* Moving in any direction with the controller's left stick. The game was
 * made for the Wii Remote held sideways, whose d-pad gives eight
 * directions; the stick reaches the game as d-pad presses (input_win32.c)
 * so menus and the rest work as before. Pikachu's movement, though, takes
 * its direction as two floats from its control module (x right, y up, of
 * length 1: the d-pad's eight), and there the d-pad's is replaced by the
 * stick's own.
 *
 * Every Pokémon has a control module, the ones the game moves too, and
 * they all name pad 0. Pikachu's is told apart by its direction: one of
 * the d-pad's eight exactly, and near the stick's (the d-pad presses came
 * from the stick). The others' wander freely.
 *
 * RECOMP_STICK=<x>,<y> (testing): the left stick held there. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "host_input.h"

void (*g_host_sticks_read)(float* lx, float* ly, float* rx, float* ry);

static float as_float(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static uint32_t as_bits(float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

/* Whether v is 0, 1 or the diagonal's 0.7071, either way. */
static int dpad_component(float v) {
    v = fabsf(v);
    return v == 0.0f || fabsf(v - 1.0f) < 1e-4f || fabsf(v - 0.70710678f) < 1e-4f;
}

static void left_stick(float* x, float* y) {
    static int env = -1;
    static float ex, ey;
    float rx, ry;
    if (env < 0) {
        const char* e = getenv("RECOMP_STICK");
        env = e && sscanf(e, "%f,%f", &ex, &ey) == 2;
    }
    *x = *y = 0;
    if (env) {
        *x = ex;
        *y = ey;
    } else if (g_host_sticks_read) {
        g_host_sticks_read(x, y, &rx, &ry);
    }
}

/* Vec2 ControlModuleImpl::virt0x8_0x20(void): the direction to move in. */
void F_ControlModuleImpl_virt0x8_0x20(ppc_t* c) {
    float x, y, sx, sy, len;
    F_ControlModuleImpl_virt0x8_0x20_orig(c);
    x = as_float(c->r[3]);
    y = as_float(c->r[4]);
    if ((x == 0.0f && y == 0.0f) || !dpad_component(x) || !dpad_component(y)) return;
    left_stick(&sx, &sy);
    len = sqrtf(sx * sx + sy * sy);
    if (len < 0.2f) return; /* the keys, or a d-pad */
    sx /= len;
    sy /= len;
    if (sx * x + sy * y < 0.64f) return; /* more than 50 degrees off: not from the stick */
    c->r[3] = as_bits(sx);
    c->r[4] = as_bits(sy);
}
