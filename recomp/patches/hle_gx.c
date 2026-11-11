/* GX, the graphics API. Commands written to the FIFO are decoded and drawn
 * by recomp/gpu (in the boot target); the GPU finishes instantly, so waits
 * for it decode the pending commands and return. */
#include "hle.h"

/* void GXDrawDone(void): queues a draw-done token and sleeps until the
 * GPU's pixel engine reports it. */
void F_GXDrawDone(ppc_t* c) {
    (void)c;
    if (g_gx_flush) g_gx_flush();
}

/* The JPEG encoder (TMCC's), for the photos taken with minus: (context,
 * RGB565 source, width, height, ...). The source is the screen's EFB
 * copy, which the game makes every frame and the GPU module keeps on the
 * GPU; it goes into memory first. */
void F_fn_8028EF88(ppc_t* c) {
    if (g_gx_copy_sync) g_gx_copy_sync(c->r[4] & 0x3FFFFFFF);
    F_fn_8028EF88_orig(c);
}
