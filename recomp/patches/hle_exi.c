/* EXI (the serial bus behind the memory-card slots, RTC/SRAM and debug
 * adapters). No devices are attached: every transfer completes at once
 * and reads back zeros, which the SDK sees as "no device". */
#include "recomp_funcs.h"

#define EXI_CR(chan) (0xCD00680Cu + (chan) * 0x14u)

/* BOOL EXISync(EXIChannel chan): waits for the channel's transfer-start
 * bit to clear, then finishes the transfer in software. */
void f_802144F0(ppc_t* c) {
    uint32_t cr = EXI_CR(c->r[3]);
    ST32(cr, LD32(cr) & ~1u);
    f_802144F0_orig(c);
}
