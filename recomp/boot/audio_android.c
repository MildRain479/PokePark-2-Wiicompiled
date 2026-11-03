/* Sound output through AAudio, as audio_win32.c through waveOut. The DSP
 * HLE hands over 3 ms frames as AX mixes them, into a ring the stream's
 * callback takes them from.
 *
 * As on Windows, playback starts only once PRIME_FRAMES are queued, which
 * absorbs the jitter of the game's audio timer; if the ring runs dry (a
 * long stall), it plays silence until primed again. Frames that arrive
 * with the ring full are dropped, which bounds the latency.
 *
 * RECOMP_AUDIO=0 turns sound off. */
#include <aaudio/AAudio.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_audio.h"

#define RING_FRAMES 4096  /* 128 ms */
#define PRIME_FRAMES 2560 /* 80 ms */

static AAudioStream* sStream;
static int16_t sRing[RING_FRAMES * 2];
static atomic_uint sWritten, sRead; /* frames, ever */
static int sPriming = 1;            /* the callback's */

static void push(const int16_t* lr, int frames) {
    unsigned w = atomic_load_explicit(&sWritten, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&sRead, memory_order_acquire);
    int i;
    if (frames > RING_FRAMES - (int)(w - r)) frames = RING_FRAMES - (int)(w - r); /* full: drop the rest */
    for (i = 0; i < frames; i++) {
        unsigned at = (w + i) % RING_FRAMES;
        sRing[at * 2] = lr[i * 2];
        sRing[at * 2 + 1] = lr[i * 2 + 1];
    }
    atomic_store_explicit(&sWritten, w + frames, memory_order_release);
}

static aaudio_data_callback_result_t callback(AAudioStream* s, void* user, void* data, int32_t frames) {
    unsigned r = atomic_load_explicit(&sRead, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&sWritten, memory_order_acquire);
    int16_t* out = (int16_t*)data;
    int i, have = (int)(w - r);
    (void)s;
    (void)user;
    if (sPriming && have >= PRIME_FRAMES) sPriming = 0;
    if (!sPriming && have == 0) sPriming = 1; /* ran dry: prime again */
    if (sPriming) have = 0;
    if (have > frames) have = frames;
    for (i = 0; i < have; i++) {
        unsigned at = (r + i) % RING_FRAMES;
        out[i * 2] = sRing[at * 2];
        out[i * 2 + 1] = sRing[at * 2 + 1];
    }
    memset(out + have * 2, 0, (size_t)(frames - have) * 4);
    atomic_store_explicit(&sRead, r + have, memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* While the app is in the background (window_android.c). */
void audio_android_pause(int paused) {
    if (!sStream) return;
    if (paused) AAudioStream_requestPause(sStream);
    else AAudioStream_requestStart(sStream);
}

void audio_init(void) {
    AAudioStreamBuilder* b;
    const char* e = getenv("RECOMP_AUDIO");
    if (e && *e == '0') return;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, HOST_AUDIO_RATE);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setDataCallback(b, callback, NULL);
    if (AAudioStreamBuilder_openStream(b, &sStream) != AAUDIO_OK) {
        fprintf(stderr, "audio: no output stream; running without sound\n");
        sStream = NULL;
    }
    AAudioStreamBuilder_delete(b);
    if (!sStream) return;
    if (AAudioStream_requestStart(sStream) != AAUDIO_OK) {
        fprintf(stderr, "audio: the stream wouldn't start; running without sound\n");
        return;
    }
    fprintf(stderr, "audio: AAudio, %d Hz stereo, %d ms buffered\n", AAudioStream_getSampleRate(sStream),
            PRIME_FRAMES * 1000 / HOST_AUDIO_RATE);
    g_host_audio_push = push;
}
