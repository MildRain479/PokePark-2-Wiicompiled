/* Mouse camera. The game's player camera (nsDelhi::nsCamera::CameraPlayer)
 * places its eye itself every frame; on the Wii, B only recenters it
 * behind Pikachu. The mouse adds a turn around the camera's target: after
 * each update the eye is orbited by the accumulated yaw and pitch, and
 * before the next one the game's own eye is put back, so its camera logic
 * never sees the turn. B clears it, as it recenters the game's camera. */
#include <math.h>

#include "hle.h"
#include "host_input.h"

void (*g_host_mouse_read)(float* dx, float* dy);

/* nsDelhi::nsCamera::Camera */
enum {
    CAM_POS = 0x0C,     /* eye, VEC3 */
    CAM_POSTURE = 0x24, /* 0: CAM_TARGET is a point; 1: a rotation */
    CAM_TARGET = 0x28,  /* VEC3 */
};

/* Radians per pixel of mouse movement, and the eye's elevation limits
 * (above the target's horizon; radians). */
#define MOUSE_SENSITIVITY 0.005f
#define MIN_ELEVATION -0.2f
#define MAX_ELEVATION 1.2f

static uint32_t sCam;
static uint32_t sGameEye[3], sOurEye[3]; /* as stored (float bits) */
static float sYaw, sPitch;

static float elevation(float x, float y, float z) { return atan2f(y, sqrtf(x * x + z * z)); }

/* Orbits the eye around the target by sYaw (to the right) and sPitch (eye
 * higher), keeping its distance. */
static void orbit(uint32_t cam) {
    float tx = (float)ld_f32(cam + CAM_TARGET), ty = (float)ld_f32(cam + CAM_TARGET + 4),
          tz = (float)ld_f32(cam + CAM_TARGET + 8);
    float x = (float)ld_f32(cam + CAM_POS) - tx, y = (float)ld_f32(cam + CAM_POS + 4) - ty,
          z = (float)ld_f32(cam + CAM_POS + 8) - tz;
    float h = sqrtf(x * x + z * z), r = sqrtf(x * x + y * y + z * z), e, c, s, nx, nz, nh;
    if (h < 1e-3f) return;
    /* Turning right swings the eye clockwise seen from above (+Y up). */
    c = cosf(-sYaw);
    s = sinf(-sYaw);
    nx = x * c + z * s;
    nz = -x * s + z * c;
    e = elevation(x, y, z) + sPitch;
    if (e < MIN_ELEVATION) e = MIN_ELEVATION;
    if (e > MAX_ELEVATION) e = MAX_ELEVATION;
    nh = r * cosf(e);
    st_f32(cam + CAM_POS, tx + nx / h * nh);
    st_f32(cam + CAM_POS + 4, ty + r * sinf(e));
    st_f32(cam + CAM_POS + 8, tz + nz / h * nh);
}

/* CameraPlayer::virt0x18, the per-frame update. */
void f_80023630(ppc_t* c) {
    uint32_t cam = c->r[3], held = 0;
    float dx = 0, dy = 0, e;
    int i, ours = cam == sCam;
    static uint32_t lastHeld;
    for (i = 0; i < 3; i++) ours = ours && LD32(cam + CAM_POS + 4 * i) == sOurEye[i];
    if (ours) {
        for (i = 0; i < 3; i++) ST32(cam + CAM_POS + 4 * i, sGameEye[i]);
    } else if (cam != sCam) {
        sCam = cam;
        sYaw = sPitch = 0;
    }
    f_80023630_orig(c);
    if (g_host_mouse_read) g_host_mouse_read(&dx, &dy);
    if (g_host_pad_read) g_host_pad_read(0, &held);
    if (held & ~lastHeld & HOST_PAD_B) sYaw = sPitch = 0;
    lastHeld = held;
    for (i = 0; i < 3; i++) sGameEye[i] = LD32(cam + CAM_POS + 4 * i);
    if (LD32(cam + CAM_POSTURE) != 0) {
        sCam = 0;
        return;
    }
    sYaw = fmodf(sYaw + dx * MOUSE_SENSITIVITY, 6.2831853f);
    sPitch += dy * MOUSE_SENSITIVITY;
    /* Keep the pitch within what the limits allow from the game's own
     * elevation, so moving back down responds at once. */
    e = elevation((float)ld_f32(cam + CAM_POS) - (float)ld_f32(cam + CAM_TARGET),
                  (float)ld_f32(cam + CAM_POS + 4) - (float)ld_f32(cam + CAM_TARGET + 4),
                  (float)ld_f32(cam + CAM_POS + 8) - (float)ld_f32(cam + CAM_TARGET + 8));
    if (e + sPitch < MIN_ELEVATION) sPitch = MIN_ELEVATION - e;
    if (e + sPitch > MAX_ELEVATION) sPitch = MAX_ELEVATION - e;
    if (sYaw != 0 || sPitch != 0) orbit(cam);
    for (i = 0; i < 3; i++) sOurEye[i] = LD32(cam + CAM_POS + 4 * i);
}
