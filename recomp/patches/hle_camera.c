/* Mouse camera. The game's player camera (nsDelhi::nsCamera::CameraPlayer)
 * keeps its direction as Euler angles in degrees, each easing toward a
 * target, and places the eye from them every frame before applying its
 * own distance and wall collision. On the Wii, B only recenters it behind
 * Pikachu.
 *
 * The mouse turns the yaw and its target directly: the game carries the
 * turn on as if its own camera had swung there. The pitch is the game's
 * (it follows the slope ahead), so the mouse's tilt is an offset: the
 * stored pitch carries it, which is what places the eye, and the two
 * functions that work the pitch out see the game's own value. B, which
 * recenters the game's camera, also clears the tilt. */
#include "hle.h"
#include "host_input.h"

void (*g_host_mouse_read)(float* dx, float* dy);

/* nsDelhi::nsCamera::CameraPlayer */
enum {
    CAM_PITCH = 0xC8, /* degrees, 0-360; more raises the eye */
    CAM_YAW = 0xCC,   /* degrees, 0-360; more turns the view left */
    CAM_YAW_TO = 0xD8 /* the yaw's easing target */
};

/* Degrees per pixel of mouse movement, and how far the mouse can tilt the
 * camera from the game's own pitch. */
#define MOUSE_SENSITIVITY 0.3f
#define MIN_TILT -20.0f
#define MAX_TILT 45.0f

static uint32_t sCam;                     /* the camera being turned */
static float sTilt;                       /* the mouse's pitch offset */
static uint32_t sGamePitch, sOurPitch;    /* as stored (float bits) */

static float wrap360(float a) { return a >= 360.0f ? a - 360.0f : a < 0.0f ? a + 360.0f : a; }

/* Takes the tilt off the stored pitch, if it is still the one put there,
 * restoring the game's value exactly. */
static void untilt(uint32_t cam) {
    if (cam == sCam && LD32(cam + CAM_PITCH) == sOurPitch) ST32(cam + CAM_PITCH, sGamePitch);
}

static void tilt(uint32_t cam) {
    if (cam != sCam) return;
    sGamePitch = LD32(cam + CAM_PITCH);
    st_f32(cam + CAM_PITCH, wrap360((float)ld_f32(cam + CAM_PITCH) + sTilt));
    sOurPitch = LD32(cam + CAM_PITCH);
}

/* CameraPlayer::virt0x18, the per-frame update. */
void f_80023630(ppc_t* c) {
    static uint32_t lastHeld;
    uint32_t cam = c->r[3], held = 0;
    float dx = 0, dy = 0, yaw;
    if (g_host_mouse_read) g_host_mouse_read(&dx, &dy);
    if (g_host_pad_read) g_host_pad_read(0, &held);
    untilt(cam);
    if (cam != sCam || (held & ~lastHeld & HOST_PAD_B)) {
        sCam = cam;
        sTilt = 0;
    }
    lastHeld = held;
    /* Turning right lowers the yaw: both the angle and its target, so the
     * game's easing between them is unchanged. */
    yaw = -dx * MOUSE_SENSITIVITY;
    if (yaw != 0) {
        float cur = (float)ld_f32(cam + CAM_YAW) + yaw, wrapped = wrap360(cur);
        st_f32(cam + CAM_YAW, wrapped);
        st_f32(cam + CAM_YAW_TO, (float)ld_f32(cam + CAM_YAW_TO) + yaw + (wrapped - cur));
    }
    /* Moving the mouse down raises the eye to look down on Pikachu. */
    sTilt += dy * MOUSE_SENSITIVITY;
    if (sTilt < MIN_TILT) sTilt = MIN_TILT;
    if (sTilt > MAX_TILT) sTilt = MAX_TILT;
    tilt(cam);
    f_80023630_orig(c);
}

/* bool EaseAngle(f32* angle, const f32* target, f32* step) (fn_800284C0) */
void f_800284C0(ppc_t* c) {
    uint32_t cam = c->r[3] - CAM_PITCH;
    if (cam != sCam) {
        f_800284C0_orig(c);
        return;
    }
    untilt(cam);
    f_800284C0_orig(c);
    tilt(cam);
}

/* CameraPlayer's pitch target from the slope ahead, and the step easing
 * the pitch there (fn_80025280). */
void f_80025280(ppc_t* c) {
    uint32_t cam = c->r[3];
    untilt(cam);
    f_80025280_orig(c);
    tilt(cam);
}
