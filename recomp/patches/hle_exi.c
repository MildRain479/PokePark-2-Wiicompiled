/* EXI (the serial bus behind the memory-card slots, RTC/SRAM and debug
 * adapters). No devices are attached: every transfer completes at once
 * and reads back zeros, which the SDK sees as "no device". */
#include "hle.h"

#define EXI_CR(chan) (0xCD00680Cu + (chan) * 0x14u)

/* BOOL EXISync(EXIChannel chan): waits for the channel's transfer-start
 * bit to clear, then finishes the transfer in software. */
void F_EXISync(ppc_t* c) {
    uint32_t cr = EXI_CR(c->r[3]);
    ST32(cr, LD32(cr) & ~1u);
    F_EXISync_orig(c);
}
