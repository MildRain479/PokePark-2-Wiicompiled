/* Sound output through waveOut. The DSP HLE hands over 3 ms frames as AX
 * mixes them; they are gathered into blocks and queued to the device,
 * which plays them back to back.
 *
 * The game's audio timer runs on host time, so it keeps pace with the
 * device. If the game stalls and the queue runs dry there is a gap; if
 * frames come faster than they play (after a stall, the timer catches up)
 * the excess is dropped, so latency stays near BLOCKS * BLOCK_FRAMES.
 *
 * RECOMP_AUDIO=0 turns sound off. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_audio.h"

#pragma comment(lib, "winmm.lib")

#define BLOCK_FRAMES 768 /* 24 ms */
#define BLOCKS 6

static HWAVEOUT sOut;
static WAVEHDR sHdr[BLOCKS];
static int16_t sData[BLOCKS][BLOCK_FRAMES * 2];
static int sFill;  /* block being filled */
static int sFrames; /* frames in it */

static void push(const int16_t* lr, int frames) {
    while (frames > 0) {
        WAVEHDR* h = &sHdr[sFill];
        int n;
        if (!(h->dwFlags & WHDR_DONE) && (h->dwFlags & WHDR_INQUEUE)) return; /* queue full: drop */
        n = BLOCK_FRAMES - sFrames;
        if (n > frames) n = frames;
        memcpy(&sData[sFill][sFrames * 2], lr, (size_t)n * 4);
        sFrames += n;
        lr += n * 2;
        frames -= n;
        if (sFrames == BLOCK_FRAMES) {
            h->dwFlags &= ~WHDR_DONE;
            waveOutWrite(sOut, h, sizeof(*h));
            sFill = (sFill + 1) % BLOCKS;
            sFrames = 0;
        }
    }
}

void audio_init(void) {
    WAVEFORMATEX fmt;
    const char* e = getenv("RECOMP_AUDIO");
    int i;
    if (e && *e == '0') return;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = HOST_AUDIO_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = HOST_AUDIO_RATE * 4;
    if (waveOutOpen(&sOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        fprintf(stderr, "audio: no output device; running without sound\n");
        return;
    }
    fprintf(stderr, "audio: waveOut, %d Hz stereo, %d ms latency\n", HOST_AUDIO_RATE, BLOCKS * BLOCK_FRAMES * 1000 / HOST_AUDIO_RATE);
    for (i = 0; i < BLOCKS; i++) {
        sHdr[i].lpData = (LPSTR)sData[i];
        sHdr[i].dwBufferLength = sizeof(sData[i]);
        waveOutPrepareHeader(sOut, &sHdr[i], sizeof(sHdr[i]));
        sHdr[i].dwFlags |= WHDR_DONE;
    }
    g_host_audio_push = push;
}
