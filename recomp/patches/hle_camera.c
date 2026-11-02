/* Mouse camera. The game's player camera (nsDelhi::nsCamera::CameraPlayer)
 * keeps its direction as Euler angles in degrees, each easing toward a
 * target, and places the eye from them every frame before applying its
 * own distance and wall collision. On the Wii, B only recenters it behind
 * Pikachu.
 *
 * The mouse turns the yaw and its target directly: the game carries the
 * turn on as if its own camera had swung there. While Pikachu moves, the
 * game works the yaw out afresh each frame from where the eye was, so
 * that the camera trails behind; the mouse swings that eye around the
 * point looked at by the same angle, so the turn holds while running
 * too. The pitch is the game's (it follows the slope ahead), so the
 * mouse's tilt is an offset: the stored pitch carries it, which is what
 * places the eye, and the two functions that work the pitch out see the
 * game's own value. B, which recenters the game's camera, also clears the
 * tilt. */
#include <math.h>

#include "hle.h"
#include "host_input.h"

void (*g_host_mouse_read)(float* dx, float* dy);

/* nsDelhi::nsCamera::CameraPlayer */
enum {
    CAM_EYE = 0x0C,    /* the eye last placed */
    CAM_PITCH = 0xC8,  /* degrees, 0-360; more raises the eye */
    CAM_YAW = 0xCC,    /* degrees, 0-360; more turns the view left */
    CAM_YAW_TO = 0xD8, /* the yaw's easing target */
    CAM_AT = 0xEC      /* the point looked at, trailing Pikachu */
};

/* Degrees per pixel of mouse movement, and how far the mouse can tilt the
 * camera from the game's own pitch. */
#define MOUSE_SENSITIVITY 0.3f
#define MIN_TILT -20.0f
#define MAX_TILT 45.0f

static uint32_t sCam;                     /* the camera being turned */
static float sTilt;                       /* the mouse's pitch offset */
static uint32_t sGamePitch, sOurPitch;    /* as stored (float bits) */

/* Swings the eye around the point looked at, so that the yaw from it to
 * that point grows by deg (Atan2Fixed(x, z), as the game takes it). */
static void swing_eye(uint32_t cam, float deg) {
    float r = deg * 3.14159265f / 180.0f, s = sinf(r), co = cosf(r);
    float x = (float)ld_f32(cam + CAM_EYE) - (float)ld_f32(cam + CAM_AT);
    float z = (float)ld_f32(cam + CAM_EYE + 8) - (float)ld_f32(cam + CAM_AT + 8);
    st_f32(cam + CAM_EYE, (float)ld_f32(cam + CAM_AT) + x * co + z * s);
    st_f32(cam + CAM_EYE + 8, (float)ld_f32(cam + CAM_AT + 8) + z * co - x * s);
}

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
        swing_eye(cam, yaw);
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

/* Field of view. A camera (nsDelhi::nsCamera::Camera) keeps its
 * projection: perspective or not, vertical field of view in degrees,
 * aspect, near and far. The park's cameras use 45 degrees (37.8 in some
 * places), 16:9, far plane 9999; close-ups and models in the 2D screens
 * use 20.9 degrees and far plane 1000. The setting widens (or narrows)
 * the world's cameras in the park, not in minigames (whose cameras are
 * set for their own courses: at 150% the end of Pelipper's Circle
 * Circuit looked through the ground): while the camera is applied to the scene
 * (ApplyToG3dCamera) and while its frustum is worked out for culling
 * (FrustumForCamera), so both agree; anything else in the game still
 * reads its own value. */
enum { CAM_PROJECTION = 0x38, CAM_FOVY = 0x3C, CAM_FAR = 0x48 };
#define WORLD_FAR 5000.0f

static float sFovScale = 1.0f; /* tan(fovy / 2) times this */

void hle_set_fov(int percent) { sFovScale = percent / 100.0f; }

/* Widens camera cam's field of view, returning its own value to put back
 * (0 if left alone). */
static uint32_t widen(uint32_t cam) {
    uint32_t own = LD32(cam + CAM_FOVY);
    float fovy = (float)ld_f32(cam + CAM_FOVY), half;
    if (sFovScale == 1.0f || hle_in_minigame() || LD32(cam + CAM_PROJECTION) != 0 || (float)ld_f32(cam + CAM_FAR) < WORLD_FAR) return 0;
    if (!(fovy > 1.0f && fovy < 170.0f)) return 0;
    half = atanf(tanf(fovy * 3.14159265f / 360.0f) * sFovScale);
    st_f32(cam + CAM_FOVY, half * 360.0f / 3.14159265f);
    return own;
}

/* void Camera::ApplyToG3dCamera(nw4r::g3d::Camera*, const VEC3* offset) */
void f_800197F0(ppc_t* c) {
    uint32_t cam = c->r[3], own = widen(cam);
    f_800197F0_orig(c);
    if (own) ST32(cam + CAM_FOVY, own);
}

/* void FrustumForCamera(const Camera*, Frustum*, float far) */
void f_80019AF0(ppc_t* c) {
    uint32_t cam = c->r[3], own = widen(cam);
    f_80019AF0_orig(c);
    if (own) ST32(cam + CAM_FOVY, own);
}
