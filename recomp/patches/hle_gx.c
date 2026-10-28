/* GX, the graphics API. There is no GPU yet: commands written to the
 * FIFO are dropped, and waits for the GPU return at once. The renderer
 * will replace this by consuming the FIFO. */
#include "recomp_funcs.h"

/* void GXDrawDone(void): queues a draw-done token and sleeps until the
 * GPU's pixel engine reports it. */
void f_80220890(ppc_t* c) { (void)c; }
