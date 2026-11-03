/* Glyphs for the Wii system font (hle_font.c), as font_win32.c renders
 * them with GDI, here with Android's own text drawing (android.graphics,
 * through JNI): sans-serif medium (Roboto Medium on most phones), the
 * closest to the console's rounded sans-serif that every phone has. */
#include <android/bitmap.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <string.h>

#include "host_font.h"

extern struct android_app* g_android_app; /* window_android.c */

static JNIEnv* sEnv; /* the game thread's: glyphs are asked for only there */
static jobject sPaint, sBitmap, sCanvas;
static jmethodID sDrawText, sMeasureText, sEraseColor;
static int sCell;

static int render(uint32_t ch, int cell, int ascent, uint8_t* coverage) {
    JNIEnv* env = sEnv;
    jchar c = (jchar)ch;
    jstring s;
    AndroidBitmapInfo info;
    void* px;
    float advance;
    int y;
    if (cell != sCell || ch > 0xFFFF) return -1;
    s = (*env)->NewString(env, &c, 1);
    (*env)->CallVoidMethod(env, sBitmap, sEraseColor, 0);
    (*env)->CallVoidMethod(env, sCanvas, sDrawText, s, 0.0f, (float)ascent, sPaint);
    advance = (*env)->CallFloatMethod(env, sPaint, sMeasureText, s);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    if (AndroidBitmap_getInfo(env, sBitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        AndroidBitmap_lockPixels(env, sBitmap, &px) != ANDROID_BITMAP_RESULT_SUCCESS)
        return -1;
    for (y = 0; y < cell; y++) memcpy(coverage + y * cell, (const uint8_t*)px + (size_t)y * info.stride, (size_t)cell);
    AndroidBitmap_unlockPixels(env, sBitmap);
    return (int)(advance + 0.5f);
}

void font_init(void) {
    const int cell = 24;
    JNIEnv* env;
    jclass typeface, paint, bitmap, config, canvas;
    jobject tf, cfg, o;
    jstring family;
    if (!g_android_app || (*g_android_app->activity->vm)->AttachCurrentThread(g_android_app->activity->vm, &env, NULL))
        return;
    sEnv = env;
    typeface = (*env)->FindClass(env, "android/graphics/Typeface");
    paint = (*env)->FindClass(env, "android/graphics/Paint");
    bitmap = (*env)->FindClass(env, "android/graphics/Bitmap");
    config = (*env)->FindClass(env, "android/graphics/Bitmap$Config");
    canvas = (*env)->FindClass(env, "android/graphics/Canvas");
    if (!typeface || !paint || !bitmap || !config || !canvas) goto fail;
    family = (*env)->NewStringUTF(env, "sans-serif-medium");
    tf = (*env)->CallStaticObjectMethod(
        env, typeface, (*env)->GetStaticMethodID(env, typeface, "create", "(Ljava/lang/String;I)Landroid/graphics/Typeface;"),
        family, 0);
    /* About 20 px tall in a 24 px cell, like the console font; white,
     * though only the coverage (an alpha-only bitmap) is kept. */
    o = (*env)->NewObject(env, paint, (*env)->GetMethodID(env, paint, "<init>", "(I)V"), 1 /* ANTI_ALIAS_FLAG */);
    (*env)->CallObjectMethod(env, o, (*env)->GetMethodID(env, paint, "setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;"), tf);
    (*env)->CallVoidMethod(env, o, (*env)->GetMethodID(env, paint, "setTextSize", "(F)V"), 20.0f);
    (*env)->CallVoidMethod(env, o, (*env)->GetMethodID(env, paint, "setColor", "(I)V"), (jint)0xFFFFFFFF);
    sPaint = (*env)->NewGlobalRef(env, o);
    cfg = (*env)->GetStaticObjectField(env, config, (*env)->GetStaticFieldID(env, config, "ALPHA_8", "Landroid/graphics/Bitmap$Config;"));
    o = (*env)->CallStaticObjectMethod(
        env, bitmap, (*env)->GetStaticMethodID(env, bitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;"),
        cell, cell, cfg);
    sBitmap = (*env)->NewGlobalRef(env, o);
    o = (*env)->NewObject(env, canvas, (*env)->GetMethodID(env, canvas, "<init>", "(Landroid/graphics/Bitmap;)V"), sBitmap);
    sCanvas = (*env)->NewGlobalRef(env, o);
    sDrawText = (*env)->GetMethodID(env, canvas, "drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V");
    sMeasureText = (*env)->GetMethodID(env, paint, "measureText", "(Ljava/lang/String;)F");
    sEraseColor = (*env)->GetMethodID(env, bitmap, "eraseColor", "(I)V");
    if ((*env)->ExceptionCheck(env) || !sDrawText || !sMeasureText || !sEraseColor) goto fail;
    sCell = cell;
    g_host_font_glyph = render;
    return;
fail:
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}
