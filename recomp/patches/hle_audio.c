/* Audio DSP. The port will mix audio on the host, so the boot-time DSP
 * handshake is skipped. */
#include "recomp_funcs.h"

/* void __OSInitAudioSystem(void): uploads a small init program to the DSP
 * and waits for it to report back. */
void f_80208330(ppc_t* c) { (void)c; }

/* static void __AI_SRC_INIT(void): times how long the audio interface's
 * sample-rate converter takes to start, by watching its sample counter.
 * There is no audio hardware to time. */
void f_8022F080(ppc_t* c) { (void)c; }

/* static void __AXOutInitDSP(void): boots the AX mixer program on the DSP
 * and waits for it to start. Audio stays silent until AX is mixed on the
 * host. */
void f_80231080(ppc_t* c) { (void)c; }
