/* Android: a second screen (the AYN Thor's lower one). The activity's Java
 * side opens a window there (PokeParkActivity.showSecondScreen) and hands
 * its surface and touches here; the game's thread draws in it with Vulkan
 * (vk_backend.c), taking the window at its next frame after it changes.
 *
 * Only on a handheld with two screens (dualScreenHandheld: a second
 * screen, built in or a touch screen plugged in as Retroid's Dual Screen
 * Add-On, a controller, no phone), and only while the Dual screen setting
 * is on: the game's thread opens and closes the window as it says
 * (second_screen_apply). With the screens swapped the game is drawn there,
 * so its touches go to the touch controls (input_android.c) and the main
 * screen's to the friends list.
 *
 * The surface's callbacks come on the activity's UI thread; when one goes,
 * the callback waits (a second at most) for the game's thread to let go
 * of it, as Android takes it away once the callback returns. */
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include "host_settings.h"

void input_android_touch(int action, int id, float x, float y); /* input_android.c */
void win_size(int* w, int* h);                                   /* window_android.c */

static JavaVM* sVm;
static jobject sActivity;
static volatile int sDevice; /* a handheld with two screens */
static volatile int sAgain;  /* a screen was plugged in or out */
static int sShown = -1;
static int sW, sH;  /* the window's size */

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sChanged = PTHREAD_COND_INITIALIZER;
static ANativeWindow* sWindow; /* the screen's, NULL if none */
static ANativeWindow* sHeld;   /* the one the game's thread draws in */
static unsigned sGen;          /* counts sWindow's changes */

/* Touches, in the window's pixels: action 0 down, 1 up, 2 moved. */
typedef struct {
    int action;
    float x, y;
} Touch;
static Touch sTouches[64];
static unsigned sTouchIn, sTouchOut;
static int sPrimary = -1; /* the pointer whose touches go to the list */

int second_screen_device(void) { return sDevice; }
int second_screen_used(void) { return sDevice && g_settings.dual_screen; }
int second_screens_swapped(void) { return second_screen_used() && g_settings.swap_screens; }

/* The game's picture's size: the main window's, or this one's swapped. */
void game_screen_size(int* w, int* h) {
    int sw, sh;
    pthread_mutex_lock(&sLock);
    sw = sWindow ? sW : 0;
    sh = sWindow ? sH : 0;
    pthread_mutex_unlock(&sLock);
    if (second_screens_swapped() && sw > 0 && sh > 0) {
        *w = sw;
        *h = sh;
    } else {
        win_size(w, h);
    }
}

/* A touch for the friends list: the first pointer down's, until it's up. */
void second_touch_push(int action, int id, float x, float y) {
    pthread_mutex_lock(&sLock);
    if (action == 0 && sPrimary < 0) sPrimary = id;
    if (id == sPrimary && sTouchIn - sTouchOut < sizeof(sTouches) / sizeof(sTouches[0])) {
        Touch* t = &sTouches[sTouchIn++ % (sizeof(sTouches) / sizeof(sTouches[0]))];
        t->action = action;
        t->x = x;
        t->y = y;
    }
    if (action == 1 && id == sPrimary) sPrimary = -1;
    pthread_mutex_unlock(&sLock);
}

static void JNICALL second_surface(JNIEnv* env, jclass cls, jobject surface, jint w, jint h) {
    ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    ANativeWindow* old;
    (void)cls;
    fprintf(stderr, "PokePark: second screen's window %p, %dx%d\n", (void*)win, (int)w, (int)h);
    pthread_mutex_lock(&sLock);
    old = sWindow;
    sWindow = win;
    sW = w;
    sH = h;
    sGen++;
    if (old && old != win) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 1;
        while (sHeld == old && pthread_cond_timedwait(&sChanged, &sLock, &until) == 0) {}
    }
    pthread_mutex_unlock(&sLock);
    if (old) ANativeWindow_release(old);
}

static void JNICALL second_touch(JNIEnv* env, jclass cls, jint action, jint id, jfloat x, jfloat y) {
    (void)env;
    (void)cls;
    if (second_screens_swapped()) input_android_touch(action, id, x, y);
    else second_touch_push(action, id, x, y);
}

static void JNICALL second_device(JNIEnv* env, jclass cls, jboolean dual) {
    (void)env;
    (void)cls;
    sDevice = dual;
    sAgain = 1;
}

static void call(JNIEnv* env, const char* method) {
    jclass cls = (*env)->GetObjectClass(env, sActivity);
    (*env)->CallVoidMethod(env, sActivity, (*env)->GetMethodID(env, cls, method, "()V"));
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, cls);
}

/* Registers the activity's native methods here and asks whether this is a
 * handheld with two screens, on the activity's thread. */
void second_screen_start(JavaVM* vm, jobject activity) {
    static const JNINativeMethod kMethods[] = {
        {"secondSurface", "(Landroid/view/Surface;II)V", (void*)second_surface},
        {"secondTouch", "(IIFF)V", (void*)second_touch},
        {"secondDevice", "(Z)V", (void*)second_device},
    };
    JNIEnv* env;
    jclass cls;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
    cls = (*env)->GetObjectClass(env, activity);
    if ((*env)->RegisterNatives(env, cls, kMethods, 3) == 0) {
        sDevice = (*env)->CallBooleanMethod(env, activity, (*env)->GetMethodID(env, cls, "dualScreenHandheld", "()Z"));
        sActivity = (*env)->NewGlobalRef(env, activity);
        sVm = vm;
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        sDevice = 0;
    }
    (*env)->DeleteLocalRef(env, cls);
}

/* On the game's thread, each frame: the window opened or closed as the
 * Dual screen setting says. */
void second_screen_apply(void) {
    JNIEnv* env;
    int want = second_screen_used();
    /* Plugged in or out: its window opened again, or its going noted. */
    if (sAgain) {
        sAgain = 0;
        sShown = -1;
    }
    if (want == sShown || !sVm) return;
    if ((*sVm)->AttachCurrentThread(sVm, &env, NULL) != JNI_OK) return;
    sShown = want;
    call(env, want ? "showSecondScreen" : "hideSecondScreen");
}

/* For the game's thread: how many times the window changed, and the
 * window (NULL if none), to draw in until second_window_drop. */
unsigned second_window_gen(void) {
    unsigned gen;
    pthread_mutex_lock(&sLock);
    gen = sGen;
    pthread_mutex_unlock(&sLock);
    return gen;
}

ANativeWindow* second_window_take(unsigned* gen) {
    ANativeWindow* win;
    pthread_mutex_lock(&sLock);
    win = sHeld = sWindow;
    *gen = sGen;
    pthread_mutex_unlock(&sLock);
    return win;
}

void second_window_drop(void) {
    pthread_mutex_lock(&sLock);
    sHeld = NULL;
    pthread_cond_broadcast(&sChanged);
    pthread_mutex_unlock(&sLock);
}

/* The next touch, if there is one. */
int second_touch_next(int* action, float* x, float* y) {
    int got = 0;
    pthread_mutex_lock(&sLock);
    if (sTouchOut != sTouchIn) {
        const Touch* t = &sTouches[sTouchOut++ % (sizeof(sTouches) / sizeof(sTouches[0]))];
        *action = t->action;
        *x = t->x;
        *y = t->y;
        got = 1;
    }
    pthread_mutex_unlock(&sLock);
    return got;
}
