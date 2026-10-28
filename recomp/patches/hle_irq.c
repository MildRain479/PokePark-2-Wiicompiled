/* Interrupt delivery. Real hardware raises an interrupt that the CPU takes
 * once external interrupts are enabled; here, pending events are checked
 * whenever the game enables interrupts (OSEnableInterrupts or
 * OSRestoreInterrupts) and run as interrupts (hle_interrupt). Events:
 *  - completions queued by HLE devices (hle_post), such as IOS replies;
 *  - the decrementer, which drives OSAlarm. */
#include <stdlib.h>
#include <string.h>

#include "hle.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define QUEUE_SIZE 64

static const uint32_t kDecrementerHandler = 0x80207B30; /* DecrementerExceptionHandler */
#define EXC_DECREMENTER 8

typedef struct {
    uint32_t fn, r3, r4;
    void (*after)(ppc_t* c, uint32_t data);
    uint32_t data;
} Pending;

static Pending sQueue[QUEUE_SIZE];
static unsigned sHead, sTail;

int hle_log_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* v = getenv("RECOMP_HLE_LOG");
        enabled = v ? atoi(v) : 0;
    }
    return enabled;
}

void hle_read_str(uint32_t addr, char* out, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n; i++) {
        out[i] = (char)LD8(addr + (uint32_t)i);
        if (!out[i]) return;
    }
    out[i] = '\0';
}

void hle_post(uint32_t fn, uint32_t r3, uint32_t r4, void (*after)(ppc_t* c, uint32_t data), uint32_t data) {
    Pending* p;
    if (sTail - sHead >= QUEUE_SIZE) {
        HLE_WARN("interrupt queue full; dropping a completion");
        return;
    }
    p = &sQueue[sTail++ % QUEUE_SIZE];
    p->fn = fn;
    p->r3 = r3;
    p->r4 = r4;
    p->after = after;
    p->data = data;
}

uint32_t hle_call(ppc_t* c, uint32_t fn, uint32_t r3, uint32_t r4) {
    ppc_t saved;
    uint32_t ret;
    memcpy(&saved, c, sizeof(saved));
    c->r[1] = (c->r[1] - 0x200) & ~7u;
    c->r[3] = r3;
    c->r[4] = r4;
    c->lr = 0;
    ppc_icall(c, fn);
    ret = c->r[3];
    memcpy(c, &saved, sizeof(saved));
    return ret;
}

static int dec_expired(void) {
    uint64_t deadline;
    return ppc_dec_deadline(&deadline) && (int64_t)(ppc_timebase() - deadline) >= 0;
}

/* Vertical retrace, 59.94 times a second (NTSC), once VIInit has
 * registered its handler for the VI interrupt. */
#define INTERRUPT_PI_VI 24
#define VI_DI0 0xCC002030u
static const uint32_t kInterruptHandlerTable = 0x80486AF0; /* holds the table's address */
static const uint64_t kRetracePeriod = PPC_TB_HZ * 1001 / 60000;
static uint64_t sNextRetrace;
uint32_t g_retrace_count;

static uint32_t interrupt_handler(int n) {
    uint32_t table = LD32(kInterruptHandlerTable);
    return table ? LD32(table + 4u * n) : 0;
}

static int retrace_due(void) {
    if (!interrupt_handler(INTERRUPT_PI_VI)) return 0;
    if (!sNextRetrace) sNextRetrace = ppc_timebase() + kRetracePeriod;
    return (int64_t)(ppc_timebase() - sNextRetrace) >= 0;
}

void hle_deliver(ppc_t* c) {
    /* Interrupts are off inside a handler; a thread the handler switched
     * to takes them as usual. */
    if (hle_in_interrupt()) return;
    for (;;) {
        if (sHead != sTail) {
            Pending p = sQueue[sHead++ % QUEUE_SIZE];
            HLE_LOG("interrupt: %08X [%s](%08X, %08X)", p.fn, ppc_func_name(p.fn), p.r3, p.r4);
            if (p.fn) hle_interrupt(c, p.fn, p.r3, p.r4);
            if (p.after) p.after(c, p.data);
        } else if (dec_expired()) {
            ppc_dec_disarm();
            hle_interrupt(c, kDecrementerHandler, EXC_DECREMENTER, LD32(0x800000D4));
        } else if (retrace_due()) {
            sNextRetrace += kRetracePeriod;
            g_retrace_count++;
            ST16(VI_DI0, LD16(VI_DI0) | 0x8000);
            hle_interrupt(c, interrupt_handler(INTERRUPT_PI_VI), INTERRUPT_PI_VI, LD32(0x800000D4));
        } else {
            break;
        }
    }
}

int hle_wait_for_event(ppc_t* c) {
    uint64_t deadline = 0, dec;
    int have = 0;
    if (sHead == sTail && !dec_expired() && !retrace_due()) {
        if (ppc_dec_deadline(&dec)) {
            deadline = dec;
            have = 1;
        }
        if (interrupt_handler(INTERRUPT_PI_VI) && (!have || (int64_t)(sNextRetrace - deadline) < 0)) {
            deadline = sNextRetrace;
            have = 1;
        }
        if (!have) return 0;
        /* Sleep until the next timer event. */
        for (;;) {
            int64_t left = (int64_t)(deadline - ppc_timebase());
            if (left <= 0) break;
#ifdef _WIN32
            Sleep((DWORD)(left * 1000 / (int64_t)PPC_TB_HZ));
#endif
        }
    }
    hle_deliver(c);
    return 1;
}

/* BOOL OSEnableInterrupts(void) */
void f_8020D310(ppc_t* c) {
    uint32_t lr = c->lr;
    f_8020D310_orig(c);
    hle_deliver(c);
    c->lr = lr;
    hle_idle_check(c);
}

/* BOOL OSRestoreInterrupts(BOOL level) */
void f_8020D330(ppc_t* c) {
    uint32_t enable = c->r[3];
    f_8020D330_orig(c);
    if (enable) hle_deliver(c);
}
