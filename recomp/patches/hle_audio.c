/* Audio: the AI (audio interface, which plays a buffer by DMA) and the DSP
 * (which runs AX, the SDK's mixer).
 *
 * AX advances in 3 ms frames. When AI finishes playing a buffer it raises
 * an interrupt; AX's callback (__AXOutAiCallback) then, if the DSP has
 * finished the previous frame's command list (its task's resume callback,
 * __AXDSPResumeCallback, says so), starts the next frame
 * (__AXOutNewFrame): it syncs the voices into the DSP's parameter blocks,
 * posts a command list, runs the AX and user callbacks (nw4r's sound
 * thread) and queues the next buffer.
 *
 * Here a timer stands in for the AI DMA interrupt at the rate of the
 * buffers the game queues, and the DSP takes mail at once and runs each
 * command list in the tick that follows: it mixes the running voices
 * (dsp_run_voices) and sends the result to the host's audio output
 * (host_audio.h). */
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "host_audio.h"

#define AI_SAMPLE_RATE HOST_AUDIO_RATE /* AX output rate */

static const uint32_t kAXDSPResumeCallback = 0x80231000; /* __AXDSPResumeCallback */

static uint32_t sAiCallback;
static uint32_t sDmaBytes;

/* void __OSInitAudioSystem(void): uploads a small init program to the DSP
 * and waits for it to report back. */
void f_80208330(ppc_t* c) { (void)c; }

/* static void __AI_SRC_INIT(void): times how long the audio interface's
 * sample-rate converter takes to start, by watching its sample counter.
 * There is no audio hardware to time. */
void f_8022F080(ppc_t* c) { (void)c; }

/* static void __AXOutInitDSP(void): boots the AX mixer program on the DSP
 * and waits for it to start. */
void f_80231080(ppc_t* c) { (void)c; }

/* u32 DSPCheckMailToDSP(void): nonzero while the DSP has not taken the
 * last mail. It takes it at once. */
void f_802395B0(ppc_t* c) { c->r[3] = 0; }

/* The DSP's voice parameter blocks (AXPB, 0x140 bytes each): AX copies each
 * voice's settings in before a frame and reads its position and state back
 * after it (__AXSyncPBs). */
#define AX_PBS 0x80486F70       /* __AXGetPBs() */
#define AX_MAX_VOICES 0x80486F7C /* AXGetMaxVoices() */
#define AX_PB_SIZE 0x140
#define AX_FRAME_SAMPLES 96 /* 3 ms at 32 kHz */
enum {
    PB_SRC_TYPE = 0x08,     /* u16: 0 polyphase, 1 linear, 2 none (1:1) */
    PB_STATE = 0x10,        /* u16: 1 while running */
    PB_IS_STREAM = 0x12,    /* u16: a stream keeps its ADPCM history on looping */
    PB_MIX_L = 0x14,        /* u16 volumes (0x8000 = 1.0), each followed by a ramp delta */
    PB_MIX_R = 0x18,
    PB_MIX_S = 0x34,
    PB_VE_VOLUME = 0x6A,    /* u16: the voice's envelope volume */
    PB_LOOP_FLAG = 0x6E,    /* u16 */
    PB_FORMAT = 0x70,       /* u16: 0 ADPCM (nibble addresses), 0x0A PCM16, 0x19 PCM8 */
    PB_LOOP_ADDR = 0x72,    /* u32 */
    PB_END_ADDR = 0x76,     /* u32 */
    PB_CUR_ADDR = 0x7A,     /* u32 */
    PB_ADPCM_COEFS = 0x7E,  /* s16[16]: 8 predictor pairs */
    PB_ADPCM_PS = 0xA0,     /* u16: predictor and scale */
    PB_ADPCM_YN1 = 0xA2,    /* s16: history */
    PB_ADPCM_YN2 = 0xA4,
    PB_SRC_RATIO = 0xA6,    /* u32, 16.16 input samples per output sample */
    PB_SRC_FRAC = 0xAA,     /* u16: position between the last two samples */
    PB_SRC_LAST = 0xAC,     /* s16[4]: the last input samples; [0] older, [1] newer */
    PB_LOOP_PS = 0xB4,      /* u16: ADPCM context at the loop address */
    PB_LOOP_YN1 = 0xB6,
    PB_LOOP_YN2 = 0xB8,
    PB_LPF_ON = 0xBA,       /* u16; then s16 yn1, u16 a0, u16 b0 */
    PB_LPF_YN1 = 0xBC,
    PB_LPF_A0 = 0xBE,
    PB_LPF_B0 = 0xC0,
};

enum { FMT_ADPCM = 0x00, FMT_PCM16 = 0x0A, FMT_PCM8 = 0x19 };

void (*g_host_audio_push)(const int16_t* lr, int frames);

static uint32_t ld32_pair(uint32_t a) { return (uint32_t)LD16(a) << 16 | LD16(a + 2); }
static void st32_pair(uint32_t a, uint32_t v) {
    ST16(a, (uint16_t)(v >> 16));
    ST16(a + 2, (uint16_t)v);
}

static int16_t clamp16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

/* One voice's decoder state for the frame, kept in registers and written
 * back to its parameter block at the end. */
typedef struct {
    uint32_t pb, cur, end, loop;
    int format, looping, stream, running;
    int32_t ps, yn1, yn2;
} Voice;

/* Reads the sample at the voice's position and moves on. Addresses are
 * physical: ADPCM in nibbles (each 8-byte frame starts with a predictor
 * and scale byte, then 14 samples), PCM16 in samples, PCM8 in bytes.
 * Past the end address a voice always goes back to its loop address, and
 * stops there unless it loops; nw4r points the loop address of a one-shot
 * sound at a buffer of silence and counts the sound finished once the
 * position is inside it. */
static int32_t read_sample(Voice* v) {
    int32_t s;
    if (!v->running) return 0;
    switch (v->format) {
    case FMT_ADPCM: {
        int32_t nibble, c1, c2;
        uint8_t b;
        if ((v->cur & 15) < 2) v->cur = (v->cur & ~15u) + 2;
        if ((v->cur & 15) == 2) v->ps = LD8((v->cur & ~15u) >> 1);
        b = LD8(v->cur >> 1);
        nibble = (v->cur & 1) ? (b & 15) : (b >> 4);
        if (nibble >= 8) nibble -= 16;
        c1 = (int16_t)LD16(v->pb + PB_ADPCM_COEFS + (v->ps >> 4 & 7) * 4);
        c2 = (int16_t)LD16(v->pb + PB_ADPCM_COEFS + (v->ps >> 4 & 7) * 4 + 2);
        s = clamp16((c1 * v->yn1 + c2 * v->yn2 + ((nibble << (v->ps & 15)) << 11) + 1024) >> 11);
        v->yn2 = v->yn1;
        v->yn1 = s;
        break;
    }
    case FMT_PCM16: s = (int16_t)LD16(v->cur * 2); break;
    case FMT_PCM8: s = (int8_t)LD8(v->cur) * 256; break;
    default: s = 0; break;
    }
    if (v->cur == v->end) {
        v->cur = v->loop;
        if (!v->looping) {
            v->running = 0;
        } else if (v->format == FMT_ADPCM && !v->stream) {
            v->ps = LD16(v->pb + PB_LOOP_PS);
            v->yn1 = (int16_t)LD16(v->pb + PB_LOOP_YN1);
            v->yn2 = (int16_t)LD16(v->pb + PB_LOOP_YN2);
        }
    } else {
        v->cur++;
    }
    return s;
}

/* Mixes one voice's frame into the main left/right buses: decode,
 * sample-rate conversion (linear between the last two input samples),
 * envelope volume, the optional one-pole low-pass filter, and the mixer
 * volumes. Ramps (the deltas) are not applied: AX rewrites the volumes
 * every frame, so a change lands within 3 ms. The aux (effects) buses and
 * the remote speaker are not mixed. */
static void mix_voice(uint32_t pb, int32_t* outL, int32_t* outR) {
    Voice v;
    uint32_t ratio, frac;
    int32_t last0, last1, ve, volL, volR, lpfOn, lpfY, lpfA, lpfB;
    int i;
    v.pb = pb;
    v.cur = ld32_pair(pb + PB_CUR_ADDR);
    v.end = ld32_pair(pb + PB_END_ADDR);
    v.loop = ld32_pair(pb + PB_LOOP_ADDR);
    v.format = LD16(pb + PB_FORMAT);
    v.looping = LD16(pb + PB_LOOP_FLAG) != 0;
    v.stream = LD16(pb + PB_IS_STREAM) != 0;
    v.running = 1;
    v.ps = LD16(pb + PB_ADPCM_PS);
    v.yn1 = (int16_t)LD16(pb + PB_ADPCM_YN1);
    v.yn2 = (int16_t)LD16(pb + PB_ADPCM_YN2);
    ratio = LD16(pb + PB_SRC_TYPE) == 2 ? 0x10000 : ld32_pair(pb + PB_SRC_RATIO);
    frac = LD16(pb + PB_SRC_FRAC);
    last0 = (int16_t)LD16(pb + PB_SRC_LAST);
    last1 = (int16_t)LD16(pb + PB_SRC_LAST + 2);
    ve = LD16(pb + PB_VE_VOLUME);
    /* Surround is folded into both sides at half volume. */
    volL = LD16(pb + PB_MIX_L) + LD16(pb + PB_MIX_S) / 2;
    volR = LD16(pb + PB_MIX_R) + LD16(pb + PB_MIX_S) / 2;
    lpfOn = LD16(pb + PB_LPF_ON);
    lpfY = (int16_t)LD16(pb + PB_LPF_YN1);
    lpfA = LD16(pb + PB_LPF_A0);
    lpfB = LD16(pb + PB_LPF_B0);
    for (i = 0; i < AX_FRAME_SAMPLES; i++) {
        int32_t s = last0 + (int32_t)(((int64_t)(last1 - last0) * (int32_t)frac) >> 16);
        s = (s * ve) >> 15;
        if (lpfOn) s = lpfY = clamp16((lpfA * s + lpfB * lpfY) >> 15);
        outL[i] += (s * volL) >> 15;
        outR[i] += (s * volR) >> 15;
        for (frac += ratio; frac >= 0x10000; frac -= 0x10000) {
            last0 = last1;
            last1 = read_sample(&v);
        }
    }
    st32_pair(pb + PB_CUR_ADDR, v.cur);
    ST16(pb + PB_SRC_FRAC, (uint16_t)frac);
    ST16(pb + PB_SRC_LAST, (uint16_t)last0);
    ST16(pb + PB_SRC_LAST + 2, (uint16_t)last1);
    ST16(pb + PB_ADPCM_PS, (uint16_t)v.ps);
    ST16(pb + PB_ADPCM_YN1, (uint16_t)v.yn1);
    ST16(pb + PB_ADPCM_YN2, (uint16_t)v.yn2);
    ST16(pb + PB_LPF_YN1, (uint16_t)lpfY);
    if (!v.running) ST16(pb + PB_STATE, 0);
}

/* RECOMP_AUDIO_DUMP=<file.wav>: also writes the mix to a WAV file. */
static FILE* sDump;
static uint32_t sDumpFrames;

static void dump_header(void) {
    uint8_t h[44];
    uint32_t data = sDumpFrames * 4;
    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)(data + 36); h[5] = (uint8_t)((data + 36) >> 8); h[6] = (uint8_t)((data + 36) >> 16); h[7] = (uint8_t)((data + 36) >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;
    h[20] = 1; h[21] = 0;  /* PCM */
    h[22] = 2; h[23] = 0;  /* stereo */
    h[24] = (uint8_t)AI_SAMPLE_RATE; h[25] = (uint8_t)(AI_SAMPLE_RATE >> 8); h[26] = h[27] = 0;
    h[28] = (uint8_t)(AI_SAMPLE_RATE * 4); h[29] = (uint8_t)((AI_SAMPLE_RATE * 4) >> 8); h[30] = (uint8_t)((AI_SAMPLE_RATE * 4) >> 16); h[31] = 0;
    h[32] = 4; h[33] = 0;
    h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)data; h[41] = (uint8_t)(data >> 8); h[42] = (uint8_t)(data >> 16); h[43] = (uint8_t)(data >> 24);
    fseek(sDump, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), sDump);
    fseek(sDump, 0, SEEK_END);
}

static void dump(const int16_t* lr, int frames) {
    static int opened;
    if (!opened) {
        const char* path = getenv("RECOMP_AUDIO_DUMP");
        opened = 1;
        if (path && *path && (sDump = fopen(path, "wb")) != NULL) dump_header();
    }
    if (!sDump) return;
    fwrite(lr, 4, (size_t)frames, sDump);
    sDumpFrames += (uint32_t)frames;
    /* Keep the header current about once a second. */
    if (sDumpFrames % (AX_FRAME_SAMPLES * 333) < (uint32_t)frames) {
        dump_header();
        fflush(sDump);
    }
}

/* What the DSP does with a command list: mixes every running voice into
 * the output for one frame, moving each voice through its samples.
 * Streaming players (THP's soundtrack, nw4r's streams) refill their
 * buffers as the positions pass them. The mix goes to the host's audio
 * output in place of the AI buffer the DSP would fill. */
static void dsp_run_voices(void) {
    uint32_t pbs = LD32(AX_PBS), count = LD32(AX_MAX_VOICES), i;
    int32_t mixL[AX_FRAME_SAMPLES], mixR[AX_FRAME_SAMPLES];
    int16_t out[AX_FRAME_SAMPLES * 2];
    memset(mixL, 0, sizeof(mixL));
    memset(mixR, 0, sizeof(mixR));
    for (i = 0; pbs && i < count; i++) {
        uint32_t pb = pbs + i * AX_PB_SIZE;
        if (LD16(pb + PB_STATE) == 1) mix_voice(pb, mixL, mixR);
    }
    for (i = 0; i < AX_FRAME_SAMPLES; i++) {
        out[i * 2] = clamp16(mixL[i]);
        out[i * 2 + 1] = clamp16(mixR[i]);
    }
    dump(out, AX_FRAME_SAMPLES);
    if (g_host_audio_push) g_host_audio_push(out, AX_FRAME_SAMPLES);
}

/* One AX frame: the DSP runs the last command list's voices and hands
 * back to AX, then the AI DMA interrupt asks for the next frame. */
static void ai_frame(ppc_t* c) {
    dsp_run_voices();
    hle_interrupt(c, kAXDSPResumeCallback, 0, 0);
    if (sAiCallback) hle_interrupt(c, sAiCallback, 0, 0);
}

static void update_timer(void) {
    /* 16-bit stereo: 4 bytes per sample frame. */
    uint32_t samples = sDmaBytes / 4;
    if (!samples) return;
    hle_timer_start(HLE_TIMER_AUDIO, (uint64_t)samples * PPC_TB_HZ / AI_SAMPLE_RATE, ai_frame);
}

/* AIDCallback AIRegisterDMACallback(AIDCallback callback) */
void f_8022EC80(ppc_t* c) {
    sAiCallback = c->r[3];
    f_8022EC80_orig(c);
}

/* void AIInitDMA(u32 start, u32 length) */
void f_8022ECD0(ppc_t* c) {
    if (c->r[4] != sDmaBytes) {
        sDmaBytes = c->r[4];
        HLE_LOG("AI DMA buffer: %u bytes (%.2f ms)", sDmaBytes, sDmaBytes / 4 * 1000.0 / AI_SAMPLE_RATE);
    }
    f_8022ECD0_orig(c);
}

/* void AIStartDMA(void) */
void f_8022ED50(ppc_t* c) {
    update_timer();
    f_8022ED50_orig(c);
}

/* void AIStopDMA(void) */
void f_8022ED70(ppc_t* c) {
    hle_timer_stop(HLE_TIMER_AUDIO);
    f_8022ED70_orig(c);
}
