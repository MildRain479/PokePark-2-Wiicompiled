/* Audio: the AI (audio interface, which plays a buffer by DMA) and the DSP
 * (which runs AX, the SDK's mixer).
 *
 * AX advances in frames. When AI finishes playing a buffer it raises an
 * interrupt; AX's callback (__AXOutAiCallback) then, if the DSP finished
 * the previous frame, starts the next one (__AXOutNewFrame): it syncs the
 * voices, posts a command list to the DSP, runs the AX and user callbacks
 * (nw4r's sound thread) and queues the next buffer.
 *
 * Here the DSP finishes instantly and takes mail at once, and a timer
 * stands in for the AI DMA interrupt at the rate of the buffers the game
 * queues. The frames run on time but are silent: nothing mixes the voices
 * yet. */
#include "hle.h"

#define AI_SAMPLE_RATE 32000u /* AX output rate */

static const uint32_t kAXDSPDoneCallback = 0x80231060; /* __AXDSPDoneCallback */

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

/* One AX frame: the DSP reports the previous frame done, then the AI DMA
 * interrupt asks for the next. */
static void ai_frame(ppc_t* c) {
    hle_interrupt(c, kAXDSPDoneCallback, 0, 0);
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
