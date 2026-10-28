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
 * command list in the tick that follows. It moves the voices on but does
 * not mix them yet, so the game is silent, but voice positions, loops and
 * ends behave as on a Wii: streaming players such as THP's soundtrack
 * refill their buffers and advance. */
#include "hle.h"

#define AI_SAMPLE_RATE 32000u /* AX output rate */

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
    PB_SRC_TYPE = 0x08,   /* u16: 2 plays at 1:1, ignoring the ratio */
    PB_STATE = 0x10,      /* u16: 1 while running */
    PB_LOOP_FLAG = 0x6E,  /* u16 */
    PB_FORMAT = 0x70,     /* u16: 0 ADPCM (nibble addresses), 0x0A PCM16, 0x19 PCM8 */
    PB_LOOP_ADDR = 0x72,  /* u32 */
    PB_END_ADDR = 0x76,   /* u32 */
    PB_CUR_ADDR = 0x7A,   /* u32 */
    PB_SRC_RATIO = 0xA6,  /* u32, 16.16 input samples per output sample */
    PB_SRC_FRAC = 0xAA,   /* u16: fraction of the current input sample */
};

static uint32_t ld32_pair(uint32_t a) { return (uint32_t)LD16(a) << 16 | LD16(a + 2); }
static void st32_pair(uint32_t a, uint32_t v) {
    ST16(a, (uint16_t)(v >> 16));
    ST16(a + 2, (uint16_t)v);
}

/* The next sample's address. ADPCM addresses count nibbles, and each
 * 8-byte frame starts with a 2-nibble header that holds no sample. */
static uint32_t next_addr(uint32_t addr, int adpcm) {
    addr++;
    if (adpcm && (addr & 15) < 2) addr = (addr & ~15u) + 2;
    return addr;
}

/* What the DSP does to each running voice in a frame, except producing
 * sound: moves its position on by the frame's input samples. Past the end
 * address a voice always goes back to its loop address, and stops there
 * unless it loops; nw4r points the loop address of a one-shot sound at a
 * buffer of silence and counts the sound finished once the position is
 * inside it. Streaming players (THP's soundtrack, nw4r's streams) refill
 * their buffers as the position passes them. */
static void dsp_run_voices(void) {
    uint32_t pbs = LD32(AX_PBS), count = LD32(AX_MAX_VOICES), i;
    for (i = 0; pbs && i < count; i++) {
        uint32_t pb = pbs + i * AX_PB_SIZE, cur, end, ratio, step, n;
        int adpcm;
        if (LD16(pb + PB_STATE) != 1) continue;
        adpcm = LD16(pb + PB_FORMAT) == 0;
        cur = ld32_pair(pb + PB_CUR_ADDR);
        end = ld32_pair(pb + PB_END_ADDR);
        ratio = LD16(pb + PB_SRC_TYPE) == 2 ? 0x10000 : ld32_pair(pb + PB_SRC_RATIO);
        step = LD16(pb + PB_SRC_FRAC) + (uint32_t)((uint64_t)ratio * AX_FRAME_SAMPLES);
        ST16(pb + PB_SRC_FRAC, (uint16_t)step);
        for (n = step >> 16; n; n--) {
            if (cur == end) {
                cur = ld32_pair(pb + PB_LOOP_ADDR);
                if (!LD16(pb + PB_LOOP_FLAG)) {
                    ST16(pb + PB_STATE, 0);
                    break;
                }
            } else {
                cur = next_addr(cur, adpcm);
            }
        }
        st32_pair(pb + PB_CUR_ADDR, cur);
    }
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
