/* GX, the graphics API. Commands written to the FIFO are decoded and drawn
 * by recomp/gpu (in the boot target); the GPU finishes instantly, so waits
 * for it decode the pending commands and return. */
#include "recomp_funcs.h"

/* void GXDrawDone(void): queues a draw-done token and sleeps until the
 * GPU's pixel engine reports it. */
void f_80220890(ppc_t* c) {
    (void)c;
    if (g_gx_flush) g_gx_flush();
}
