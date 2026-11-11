/* PokePark 2's cameras. Field of view: as in the first game
 * (PokePark 1's hle_camera.c), the setting widens the world's cameras while a
 * camera is applied to the scene and while its frustum is worked out for
 * culling. Here the fovy is an argument of the first, and the far plane
 * sits at +0x44 (the aspect is a global, not the camera's). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "host_input.h"

void (*g_host_mouse_read)(float* dx, float* dy);

enum { CAM_PROJECTION = 0x38, CAM_FOVY = 0x3C, CAM_FAR = 0x44 };
#define WORLD_FAR 5000.0f

static float sFovScale = 1.0f; /* tan(fovy / 2) times this */

void hle_set_fov(int percent) { sFovScale = percent / 100.0f; }

static int widens(uint32_t cam, float fovy) {
    return sFovScale != 1.0f && !hle_in_minigame() && LD32(cam + CAM_PROJECTION) == 0 &&
           (float)ld_f32(cam + CAM_FAR) >= WORLD_FAR && fovy > 1.0f && fovy < 170.0f;
}

static float widened(float fovy) {
    return atanf(tanf(fovy * 3.14159265f / 360.0f) * sFovScale) * 360.0f / 3.14159265f;
}

/* void Camera::ApplyToG3dCamera(Camera*, g3d::Camera*, ..., f32 fovy, ...) */
void F_Camera_ApplyToG3dCamera(ppc_t* c) {
    if (widens(c->r[3], (float)c->f[1])) c->f[1] = widened((float)c->f[1]);
    F_Camera_ApplyToG3dCamera_orig(c);
}

/* void FrustumForCamera(const Camera*, Frustum*, f32 far) */
void F_FrustumForCamera(ppc_t* c) {
    uint32_t cam = c->r[3], own = LD32(cam + CAM_FOVY);
    float fovy = (float)ld_f32(cam + CAM_FOVY);
    int wide = widens(cam, fovy);
    if (wide) st_f32(cam + CAM_FOVY, widened(fovy));
    F_FrustumForCamera_orig(c);
    if (wide) ST32(cam + CAM_FOVY, own);
}

/* Mouse camera. The camera that follows the player (vtable 0x80434938; it
 * has no RTTI name) works as the first game's CameraPlayer does: it keeps
 * its direction as Euler angles in degrees, and each update works the yaw
 * out from where the eye was, so that it trails behind. On the Wii nothing
 * turns it. See PokePark 1's hle_camera.c: the mouse turns the yaw, and swings
 * the eye around the point followed by the same angle, so the game goes on
 * from there. The pitch is the game's own, eased toward a target, so the
 * mouse's tilt is an offset carried on the stored value while the eye is
 * placed: the game sees its own value when it eases it. The game places
 * this camera for its own angles, so once turned the eye is brought in to
 * the first wall between it and the point looked at. That too is carried
 * only while the frame is drawn: the game goes on from its own eye, or it
 * would take the shortened distance as its own. B, which looks around, also
 * clears the turn, by the game's own easing of the yaw.
 *
 * RECOMP_MOUSE=<dx>,<dy> (testing): that many pixels every update. */
enum {
    CAM_ID = 0x04,
    CAM_EYE = 0x0C,
    CAM_AT = 0x28,
    CAM_PITCH = 0xF0,   /* degrees */
    CAM_YAW = 0xF4,     /* degrees, -180 to 180 */
    CAM_YAW_TO = 0x100, /* where the yaw eases to while CAM_EASING counts down */
    CAM_EASING = 0x10C,
    CAM_FOCUS = 0x110, /* the point followed */
    MANAGER_CURRENT = 0x58 /* the id of the camera on screen */
};

#define PI 3.14159265f
#define MOUSE_SENSITIVITY 0.3f /* degrees per pixel, as in the first game */
/* A tilt swings the eye up or down around the player, who stays in the middle
 * of the screen: the view moves less than for a turn of the same angle, and
 * at the same rate the tilt felt slow. */
#define TILT_SENSITIVITY (MOUSE_SENSITIVITY * 1.5f)
#define MIN_TILT -20.0f
#define MAX_TILT 45.0f
#define RECENTER_UPDATES 30
#define WALL_MARGIN 0.9f /* of the way to a wall */
#define WALL_WIDTH 5.0f  /* to each side of the eye: a wide view reaches past it */
#define WALL_RISE 5.0f   /* the rays start this far above the point looked at, off the ground */
#define WALL_NEAREST 0.4f /* of the way to the eye, whatever is there */
#define WALL_EASE_IN 0.08f /* of the distance to where a wall allows, each 60th of a second */
#define WALL_EASE_OUT 0.01f /* of the way to the eye, each 60th of a second, once clear */
#define GROUND_CLEARANCE 4.0f
static float sTurn, sTilt;             /* degrees: the mouse's so far */
static float sYawStep, sTiltStep;      /* this update's */
static uint32_t sCurrent;              /* the id of the camera on screen */
static int sRecenter;
static uint32_t sGamePitch, sOurPitch; /* as stored (float bits) */
static uint32_t sTilted;               /* the camera carrying the tilt */
static uint32_t sLogged;
static float sPull = 1;                /* how far toward the eye it is placed */
static uint32_t sPlaced;               /* the camera carrying a placed eye */
static uint32_t sGameEye[3], sOurEye[3]; /* as stored (float bits) */
static uint64_t sPlacedAt;

/* bool CollisionManager::Ray(this, const VEC3* from, VEC3* hit, const VEC3* along, mask, BOOL, 0, 0),
 * with the mask and flag the camera's own checks use. How far along the ray
 * the first wall is, or 1 with none. */
static float wall_along(ppc_t* c, const float from[3], const float along[3]) {
    ppc_t saved;
    uint32_t scratch;
    int i, found;
    float hit[3], dot = 0, len = 0;
    memcpy(&saved, c, sizeof(saved));
    c->r[1] = (c->r[1] - 0x200) & ~7u;
    scratch = c->r[1] + 0x10;
    for (i = 0; i < 3; i++) {
        st_f32(scratch + i * 4, from[i]);
        st_f32(scratch + 12 + i * 4, from[i]);
        st_f32(scratch + 24 + i * 4, along[i]);
    }
    c->lr = 0;
    ppc_icall(c, A_CollisionManager_Get);
    c->r[4] = scratch;
    c->r[5] = scratch + 12;
    c->r[6] = scratch + 24;
    c->r[7] = 0x0C;
    c->r[8] = 1;
    c->r[9] = c->r[10] = 0;
    c->lr = 0;
    ppc_icall(c, A_CollisionManager_Ray);
    found = c->r[3] == 1;
    for (i = 0; i < 3; i++) hit[i] = (float)ld_f32(scratch + 12 + i * 4);
    memcpy(c, &saved, sizeof(saved));
    if (!found) return 1;
    for (i = 0; i < 3; i++) {
        dot += (hit[i] - from[i]) * along[i];
        len += along[i] * along[i];
    }
    return len > 0 && dot < len ? (dot > 0 ? dot / len : 0) : 1;
}

static void read_mouse(float* dx, float* dy) {
    static int env = -1;
    static float ex, ey;
    if (env < 0) {
        const char* e = getenv("RECOMP_MOUSE");
        env = e && sscanf(e, "%f,%f", &ex, &ey) == 2;
    }
    *dx = *dy = 0;
    if (env) {
        *dx = ex;
        *dy = ey;
    } else if (g_host_mouse_read) {
        g_host_mouse_read(dx, dy);
    }
}

/* void CameraManager::Update(this): every update, around the cameras' own. */
void F_CameraManager_Update(ppc_t* c) {
    static uint32_t lastHeld, lastCurrent;
    uint32_t held = 0;
    float dx, dy, tilt;
    sCurrent = LD32(c->r[3] + MANAGER_CURRENT);
    if (sCurrent != lastCurrent) {
        HLE_LOG("camera: %u on screen", sCurrent);
        lastCurrent = sCurrent;
    }
    read_mouse(&dx, &dy);
    if (g_host_pad_read) g_host_pad_read(0, &held);
    if (held & ~lastHeld & HOST_PAD_B) sRecenter = 1;
    lastHeld = held;
    sYawStep = -dx * MOUSE_SENSITIVITY;
    tilt = sTilt + dy * TILT_SENSITIVITY; /* moving the mouse down raises the eye */
    tilt = tilt < MIN_TILT ? MIN_TILT : tilt > MAX_TILT ? MAX_TILT : tilt;
    sTiltStep = tilt - sTilt;
    F_CameraManager_Update_orig(c);
}

static float wrap180(float a) { return a >= 180.0f ? a - 360.0f : a < -180.0f ? a + 360.0f : a; }

/* Swings the eye around the point followed. */
static void swing_eye(uint32_t cam, float deg) {
    float r = deg * PI / 180.0f, s = sinf(r), co = cosf(r);
    float fx = (float)ld_f32(cam + CAM_FOCUS), fz = (float)ld_f32(cam + CAM_FOCUS + 8);
    float x = (float)ld_f32(cam + CAM_EYE) - fx, z = (float)ld_f32(cam + CAM_EYE + 8) - fz;
    st_f32(cam + CAM_EYE, fx + x * co + z * s);
    st_f32(cam + CAM_EYE + 8, fz + z * co - x * s);
}

/* Takes the tilt off the stored pitch, if it is still the one put there. */
static void untilt(uint32_t cam) {
    if (cam == sTilted && LD32(cam + CAM_PITCH) == sOurPitch) ST32(cam + CAM_PITCH, sGamePitch);
}

/* void fn_80024C50(this, ...): eases the pitch. The update has no other
 * need of the game's own value after this. */
void F_CameraFollow_EasePitch(ppc_t* c) {
    uint32_t cam = c->r[3];
    F_CameraFollow_EasePitch_orig(c);
    if (sTilt == 0 || LD32(cam + CAM_ID) != sCurrent || hle_in_minigame()) return;
    sTilted = cam;
    sGamePitch = LD32(cam + CAM_PITCH);
    st_f32(cam + CAM_PITCH, (float)ld_f32(cam + CAM_PITCH) + sTilt);
    sOurPitch = LD32(cam + CAM_PITCH);
}

/* Puts the game's eye back, if it is still the one put there. */
static void unplace(uint32_t cam) {
    int i;
    if (cam != sPlaced) return;
    sPlaced = 0;
    for (i = 0; i < 3; i++)
        if (LD32(cam + CAM_EYE + i * 4) != sOurEye[i]) return;
    for (i = 0; i < 3; i++) ST32(cam + CAM_EYE + i * 4, sGameEye[i]);
}

/* How far in a wall allows, of the way to the eye: 1 with none, past each
 * side of the eye as well as to it. Only what is level with the rays' start
 * or above counts: the ground is seen to on its own. */
static float wall_limit(ppc_t* c, const float at[3], const float along[3]) {
    float from[3], ray[3], side[3], t, nearest = 1;
    float len = sqrtf(along[0] * along[0] + along[2] * along[2]);
    int i, s;
    if (len < 0.001f) return 1;
    memcpy(from, at, sizeof(from));
    from[1] += WALL_RISE;
    side[0] = -along[2] / len * WALL_WIDTH;
    side[1] = 0;
    side[2] = along[0] / len * WALL_WIDTH;
    for (s = -1; s <= 1; s++) {
        for (i = 0; i < 3; i++) ray[i] = at[i] + along[i] + side[i] * s - from[i];
        if (ray[1] < 0) ray[1] = 0;
        t = wall_along(c, from, ray);
        if (t < nearest) nearest = t;
    }
    if (nearest < 1) nearest *= WALL_MARGIN;
    return nearest < WALL_NEAREST ? WALL_NEAREST : nearest;
}

/* The camera's update, virt0x24. */
void F_CameraFollow_Update(ppc_t* c) {
    uint32_t cam = c->r[3];
    int ours = LD32(cam + CAM_ID) == sCurrent && !hle_in_minigame();
    float at[3], along[3], ray[3], down[3], eye[3], t, limit, steps;
    uint64_t now;
    int i, turned;
    if (ours && cam != sLogged) {
        sLogged = cam;
        HLE_LOG("camera: %u turns", sCurrent);
    }
    untilt(cam);
    unplace(cam);
    if (ours && (sYawStep != 0 || sTiltStep != 0)) {
        st_f32(cam + CAM_YAW, wrap180((float)ld_f32(cam + CAM_YAW) + sYawStep));
        if (sYawStep != 0) swing_eye(cam, sYawStep);
        sTurn = wrap180(sTurn + sYawStep);
        sTilt += sTiltStep;
        sYawStep = sTiltStep = 0;
    }
    if (ours && sRecenter) {
        sRecenter = 0;
        st_f32(cam + CAM_YAW_TO, wrap180((float)ld_f32(cam + CAM_YAW) - sTurn));
        ST32(cam + CAM_EASING, RECENTER_UPDATES);
        sTurn = sTilt = 0;
    }
    F_CameraFollow_Update_orig(c);
    turned = sTurn != 0 || sTilt != 0;
    if (!ours || (!turned && sPull >= 1)) {
        sPull = 1;
        return;
    }
    for (i = 0; i < 3; i++) {
        at[i] = (float)ld_f32(cam + CAM_AT + i * 4);
        along[i] = (float)ld_f32(cam + CAM_EYE + i * 4) - at[i];
    }
    /* Smoothly either way: a wall's edge passing a ray is a jump in the limit. */
    now = ppc_timebase();
    steps = (float)((double)(now - sPlacedAt) / PPC_TB_HZ * 60);
    steps = steps > 4 ? 4 : steps;
    sPlacedAt = now;
    limit = turned ? wall_limit(c, at, along) : 1;
    if (limit < sPull) sPull += (limit - sPull) * (steps * WALL_EASE_IN > 1 ? 1 : steps * WALL_EASE_IN);
    else sPull = sPull + WALL_EASE_OUT * steps > limit ? limit : sPull + WALL_EASE_OUT * steps;
    for (i = 0; i < 3; i++) eye[i] = at[i] + along[i] * sPull;
    /* The ground: the eye stays above it, by looking down from over the eye. */
    for (i = 0; i < 3; i++) ray[i] = eye[i];
    if (ray[1] < at[1] + WALL_RISE) ray[1] = at[1] + WALL_RISE;
    down[0] = down[2] = 0;
    down[1] = eye[1] - GROUND_CLEARANCE - ray[1];
    t = wall_along(c, ray, down);
    if (t < 1 && eye[1] < ray[1] + down[1] * t + GROUND_CLEARANCE) eye[1] = ray[1] + down[1] * t + GROUND_CLEARANCE;
    sPlaced = cam;
    for (i = 0; i < 3; i++) {
        sGameEye[i] = LD32(cam + CAM_EYE + i * 4);
        st_f32(cam + CAM_EYE + i * 4, eye[i]);
        sOurEye[i] = LD32(cam + CAM_EYE + i * 4);
    }
}
