/* Guest CPU state and memory helpers used by the generated code. */
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef _MSC_VER
#include <stdlib.h>
#define BSWAP16 _byteswap_ushort
#define BSWAP32 _byteswap_ulong
#define BSWAP64 _byteswap_uint64
#else
#define BSWAP16 __builtin_bswap16
#define BSWAP32 __builtin_bswap32
#define BSWAP64 __builtin_bswap64
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ppc_t {
    uint32_t r[32];
    double f[32];   /* ps0 of each paired-single register */
    double ps1[32]; /* ps1 of each paired-single register */
    uint32_t cr, lr, ctr;
    uint32_t xer_ca, xer_so, xer_low;
    uint32_t msr, fpscr;
    uint32_t gqr[8];
    uint32_t sr[16];
    uint32_t spr[1024];
} ppc_t;

typedef void (*ppc_func)(ppc_t* c);

typedef struct ppc_func_entry {
    uint32_t addr;
    ppc_func fn;
    const char* name;
} ppc_func_entry;

extern const ppc_func_entry ppc_func_table[];
extern const unsigned ppc_func_count;

/* Guest memory. Masking with 0x3FFFFFFF folds the cached (0x8xxxxxxx) and
 * uncached (0xCxxxxxxx) views onto one physical range, keeps MEM2
 * (0x9/0xD) apart from MEM1, and gives the locked cache (0xE0000000) its
 * own range. */
extern uint8_t* g_mem;
#define MA(a) (g_mem + ((uint32_t)(a) & 0x3FFFFFFFu))

static inline uint32_t LD8(uint32_t a) { return *MA(a); }
static inline uint32_t LD16(uint32_t a) { uint16_t v; memcpy(&v, MA(a), 2); return BSWAP16(v); }
static inline uint32_t LD32(uint32_t a) { uint32_t v; memcpy(&v, MA(a), 4); return BSWAP32(v); }
static inline uint64_t LD64(uint32_t a) { uint64_t v; memcpy(&v, MA(a), 8); return BSWAP64(v); }
static inline uint32_t LD16BR(uint32_t a) { uint16_t v; memcpy(&v, MA(a), 2); return v; }
static inline uint32_t LD32BR(uint32_t a) { uint32_t v; memcpy(&v, MA(a), 4); return v; }
/* The GPU's write-gather pipe: every store to its 32-byte window appends
 * to the graphics command FIFO instead of memory (paired-single stores
 * write their second value 4 bytes in). */
#define WGPIPE_ADDR 0xCC008000u
#define IS_WGPIPE(a) ((uint32_t)((a) - WGPIPE_ADDR) < 32u)
extern void (*g_wgpipe_write)(uint32_t value, int size);
/* Decodes pending GPU commands; set by the GPU module when present. */
extern void (*g_gx_flush)(void);

static inline void ST8(uint32_t a, uint32_t v) {
    if (IS_WGPIPE(a)) { g_wgpipe_write(v & 0xFF, 1); return; }
    *MA(a) = (uint8_t)v;
}
static inline void ST16(uint32_t a, uint32_t v) {
    uint16_t t;
    if (IS_WGPIPE(a)) { g_wgpipe_write(v & 0xFFFF, 2); return; }
    t = BSWAP16((uint16_t)v);
    memcpy(MA(a), &t, 2);
}
static inline void ST32(uint32_t a, uint32_t v) {
    uint32_t t;
    if (IS_WGPIPE(a)) { g_wgpipe_write(v, 4); return; }
    t = BSWAP32(v);
    memcpy(MA(a), &t, 4);
}
static inline void ST64(uint32_t a, uint64_t v) {
    uint64_t t;
    if (IS_WGPIPE(a)) { g_wgpipe_write((uint32_t)(v >> 32), 4); g_wgpipe_write((uint32_t)v, 4); return; }
    t = BSWAP64(v);
    memcpy(MA(a), &t, 8);
}
static inline void ST16BR(uint32_t a, uint32_t v) { uint16_t t = (uint16_t)v; memcpy(MA(a), &t, 2); }
static inline void ST32BR(uint32_t a, uint32_t v) { memcpy(MA(a), &v, 4); }

static inline uint64_t f64_bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double bits_f64(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline float bits_f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t f32_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static inline double ld_f32(uint32_t a) { return (double)bits_f32(LD32(a)); }
static inline double ld_f64(uint32_t a) { return bits_f64(LD64(a)); }
static inline void st_f32(uint32_t a, double v) { ST32(a, f32_bits((float)v)); }
static inline void st_f64(uint32_t a, double v) { ST64(a, f64_bits(v)); }

/* Function-entry trace: a ring of the most recent guest functions entered,
 * printed when the runtime stops on an error. Compiled out unless
 * RECOMP_TRACE is defined. */
#ifdef RECOMP_TRACE
extern uint32_t g_trace[256];
extern unsigned g_trace_pos;
#define PPC_TRACE(a) (g_trace[g_trace_pos++ & 255] = (a))
#else
#define PPC_TRACE(a) ((void)0)
#endif
void ppc_dump_trace(void);
void ppc_backtrace(ppc_t* c);
const char* ppc_func_name(uint32_t addr);

static inline uint32_t ROTL(uint32_t x, uint32_t n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }

/* Condition register: PowerPC numbers bits from the MSB, so CR bit b is
 * (cr >> (31 - b)) & 1, and field n covers bits 4n..4n+3. */
#define CRBIT(c, b) (((c)->cr >> (31 - (b))) & 1u)
static inline void cr_setbit(ppc_t* c, int b, uint32_t v) {
    c->cr = (c->cr & ~(0x80000000u >> b)) | ((v & 1u) << (31 - b));
}
static inline void cr_setfield(ppc_t* c, int n, uint32_t v) {
    int sh = 28 - 4 * n;
    c->cr = (c->cr & ~(0xFu << sh)) | ((v & 0xFu) << sh);
}
static inline void cmp_s(ppc_t* c, int n, int32_t a, int32_t b) {
    cr_setfield(c, n, (a < b ? 8u : a > b ? 4u : 2u) | c->xer_so);
}
static inline void cmp_u(ppc_t* c, int n, uint32_t a, uint32_t b) {
    cr_setfield(c, n, (a < b ? 8u : a > b ? 4u : 2u) | c->xer_so);
}
#define RC0(c, v) cmp_s((c), 0, (int32_t)(v), 0)
static inline void fcmp(ppc_t* c, int n, double a, double b) {
    cr_setfield(c, n, (a < b) ? 8u : (a > b) ? 4u : (a == b) ? 2u : 1u);
}

static inline uint32_t ppc_cntlzw(uint32_t v) {
    uint32_t n = 0;
    if (v == 0) return 32;
    while (!(v & 0x80000000u)) { v <<= 1; n++; }
    return n;
}
static inline uint32_t ppc_srawi(ppc_t* c, uint32_t v, uint32_t sh) {
    int32_t s = (int32_t)v;
    c->xer_ca = (s < 0) && sh && (v & ((1u << sh) - 1));
    return (uint32_t)(s >> sh);
}
static inline uint32_t ppc_sraw(ppc_t* c, uint32_t v, uint32_t rb) {
    uint32_t sh = rb & 0x3F;
    if (sh >= 32) {
        c->xer_ca = (int32_t)v < 0;
        return (int32_t)v < 0 ? 0xFFFFFFFFu : 0;
    }
    return ppc_srawi(c, v, sh);
}
/* Division by zero and INT_MIN / -1 are undefined on the CPU; these
 * return what Broadway is observed to return instead of trapping. */
static inline uint32_t ppc_divw(uint32_t a, uint32_t b) {
    if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) return (int32_t)a < 0 ? 0xFFFFFFFFu : 0;
    return (uint32_t)((int32_t)a / (int32_t)b);
}
static inline uint32_t ppc_divwu(uint32_t a, uint32_t b) { return b ? a / b : 0; }

static inline double ppc_fctiwz(double v) {
    int32_t i;
    if (v != v) i = (int32_t)0x80000000;
    else if (v >= 2147483647.0) i = 0x7FFFFFFF;
    else if (v <= -2147483648.0) i = (int32_t)0x80000000;
    else i = (int32_t)v;
    return bits_f64(0xFFF8000000000000ull | (uint32_t)i);
}
double ppc_fctiw(ppc_t* c, double v);

static inline void ppc_dcbz(uint32_t a) { memset(MA(a & ~31u), 0, 32); }

void psq_load(ppc_t* c, int fd, uint32_t ea, int w, int i);
void psq_store(ppc_t* c, int fs, uint32_t ea, int w, int i);
uint32_t ppc_mfspr(ppc_t* c, int spr);
void ppc_mtspr(ppc_t* c, int spr, uint32_t v);
uint32_t ppc_mftb(ppc_t* c, int tbr);

/* Guest time base (60.75 MHz) and the decrementer deadline. */
uint64_t ppc_timebase(void);
int ppc_dec_deadline(uint64_t* deadline);
void ppc_dec_disarm(void);
#define PPC_TB_HZ 60750000ull

void ppc_icall(ppc_t* c, uint32_t addr);
void ppc_unresolved(ppc_t* c, uint32_t addr);
void ppc_unimpl(ppc_t* c, uint32_t addr, uint32_t ins);
void ppc_trap(ppc_t* c, uint32_t addr);
void ppc_syscall(ppc_t* c);
void ppc_rfi(ppc_t* c);

/* Runtime setup. */
int ppc_mem_init(void);
int ppc_load_dol(const char* path, uint32_t* entry);
ppc_func ppc_lookup(uint32_t addr);

/* Optional host overrides: when set, ppc_icall and the generated stubs
 * call these instead of the recompiled function at that address. */
void ppc_set_override(uint32_t addr, ppc_func fn);

#ifdef __cplusplus
}
#endif
