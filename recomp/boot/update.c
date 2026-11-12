/* The update check: at start (unless the Updates setting is off), asks
 * GitHub for the project's latest release, once, on a thread of its own,
 * and compares its tag with this build's version (version.h). Options
 * shows what it found, and opens the release page in the browser to
 * download it from; nothing is downloaded or installed here. On Windows
 * it asks with WinHTTP; on Android the activity's Java side asks
 * (PokeParkActivity.java) and opens the page.
 *
 * RECOMP_UPDATE_VERSION=<version> checks as if this build were that
 * version (to see a newer release offered). */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#endif
#ifdef __ANDROID__
#include <android_native_app_glue.h>
#include <jni.h>
#include <pthread.h>
extern struct android_app* g_android_app; /* window_android.c */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_settings.h"
#include "version.h"

#define API_HOST "api.github.com"
#define API_PATH "/repos/MildRain479/PokePark-2-Wiicompiled/releases/latest"
#define RELEASES_PAGE "https://github.com/MildRain479/PokePark-2-Wiicompiled/releases/latest"
#define USER_AGENT "PokePark-2-Wiicompiled/" RECOMP_VERSION
#define MAX_REPLY (256 * 1024)

static volatile int sState; /* UPDATE_* */
static volatile int sStarted;
static char sTag[32]; /* the newer release's tag, as shown */

int update_state(void) { return g_settings.check_updates ? sState : UPDATE_OFF; }
const char* update_tag(void) { return sTag; }

/* "v1.2.3-beta" as {1, 2, 3}; 0 if it isn't a version. */
static int parse_version(const char* s, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) >= 2;
}

/* What the release's JSON says (its tag), compared with this build. */
static int judge(const char* json) {
    const char* p = json ? strstr(json, "\"tag_name\"") : NULL;
    char tag[64];
    const char* mine = getenv("RECOMP_UPDATE_VERSION");
    int theirs[3], ours[3], i, n = 0;
    if (!p || !(p = strchr(p + 10, ':')) || !(p = strchr(p, '"'))) {
        fprintf(stderr, "update: couldn't ask GitHub for the latest release\n");
        return UPDATE_FAILED;
    }
    /* Only a tag's characters, so nothing odd is drawn. */
    for (p++; *p && *p != '"' && n < (int)sizeof(tag) - 1; p++) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '.' || *p == '-')
            tag[n++] = *p;
        else
            return UPDATE_FAILED;
    }
    tag[n] = '\0';
    if (!mine || !*mine) mine = RECOMP_VERSION;
    fprintf(stderr, "update: the latest release is %s; this is %s\n", tag, mine);
    if (!parse_version(tag, theirs) || !parse_version(mine, ours)) return UPDATE_FAILED;
    for (i = 0; i < 3 && theirs[i] == ours[i]; i++) {}
    if (i == 3 || theirs[i] < ours[i]) return UPDATE_CURRENT;
    /* Shown as "v1.2.3": the numbers, without a suffix like -beta. */
    snprintf(sTag, sizeof(sTag), "v%d.%d.%d", theirs[0], theirs[1], theirs[2]);
    return UPDATE_NEWER;
}

/* The latest release's JSON (malloc'd), or NULL. */
#ifdef _WIN32
static char* fetch(void) {
    HINTERNET s = WinHttpOpen(L"" USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = NULL, r = NULL;
    char* buf = NULL;
    DWORD status = 0, size = sizeof(status), got;
    int n = 0;
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 10000, 10000, 10000, 10000);
    if (!(c = WinHttpConnect(s, L"" API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0))) goto done;
    if (!(r = WinHttpOpenRequest(c, L"GET", L"" API_PATH, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 WINHTTP_FLAG_SECURE)))
        goto done;
    WinHttpAddRequestHeaders(r, L"Accept: application/vnd.github+json", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r, NULL) ||
        !WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200 || !(buf = (char*)malloc(MAX_REPLY + 1)))
        goto done;
    while (n < MAX_REPLY && WinHttpReadData(r, buf + n, (DWORD)(MAX_REPLY - n), &got) && got) n += (int)got;
    buf[n] = '\0';
done:
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return buf;
}

static DWORD WINAPI check_thread(LPVOID arg) {
#else
static char* fetch(void) {
    JavaVM* vm = g_android_app ? g_android_app->activity->vm : NULL;
    jobject act = g_android_app ? g_android_app->activity->clazz : NULL;
    JNIEnv* env;
    jclass cls;
    jstring url, body;
    char* buf = NULL;
    if (!vm || (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return NULL;
    cls = (*env)->GetObjectClass(env, act);
    url = (*env)->NewStringUTF(env, "https://" API_HOST API_PATH);
    body = (jstring)(*env)->CallObjectMethod(
        env, act, (*env)->GetMethodID(env, cls, "httpGet", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"), url,
        (*env)->NewStringUTF(env, USER_AGENT));
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        body = NULL;
    }
    if (body) {
        const char* s = (*env)->GetStringUTFChars(env, body, NULL);
        if (s) {
            buf = strdup(s);
            (*env)->ReleaseStringUTFChars(env, body, s);
        }
    }
    (*vm)->DetachCurrentThread(vm);
    return buf;
}

static void* check_thread(void* arg) {
#endif
    char* json = fetch();
    int r = judge(json);
    (void)arg;
    free(json);
    if (sState == UPDATE_CHECKING) sState = r;
    sStarted = 0;
    return 0;
}

void update_check(void) {
    return; /* no releases of its own yet: the first game's would be offered */
    if (!g_settings.check_updates || sStarted || sState == UPDATE_CURRENT || sState == UPDATE_NEWER) return;
    sStarted = 1;
    sState = UPDATE_CHECKING;
#ifdef _WIN32
    {
        HANDLE t = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
        else sState = UPDATE_FAILED, sStarted = 0;
    }
#else
    {
        pthread_t t;
        if (pthread_create(&t, NULL, check_thread, NULL) == 0) pthread_detach(t);
        else sState = UPDATE_FAILED, sStarted = 0;
    }
#endif
}

void update_open(void) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", RELEASES_PAGE, NULL, NULL, SW_SHOWNORMAL);
#else
    JavaVM* vm = g_android_app ? g_android_app->activity->vm : NULL;
    jobject act = g_android_app ? g_android_app->activity->clazz : NULL;
    JNIEnv* env;
    jclass cls;
    jstring url;
    if (!vm || (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return; /* (this thread's, if it has one) */
    cls = (*env)->GetObjectClass(env, act);
    url = (*env)->NewStringUTF(env, RELEASES_PAGE);
    (*env)->CallVoidMethod(env, act, (*env)->GetMethodID(env, cls, "openPage", "(Ljava/lang/String;)V"), url);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, url);
    (*env)->DeleteLocalRef(env, cls);
#endif
}
