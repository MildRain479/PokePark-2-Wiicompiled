/* Runtime support for recompiled code: guest memory, DOL loading,
 * indirect calls, and the rarer instructions. */
#include <stdio.h>
#include <stdlib.h>

#include "ppc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#else
#include <signal.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#endif

uint8_t* g_mem;

static void wgpipe_discard(uint32_t value, int size) { (void)value; (void)size; }
void (*g_wgpipe_write)(uint32_t value, int size) = wgpipe_discard;
void (*g_gx_flush)(void);
void (*g_gx_copy_sync)(uint32_t addr);
uint64_t (*g_host_frame_due)(void);
void (*g_host_frame_render)(void);

#define GUEST_SPACE 0x40000000u

/* Physical ranges that are backed by memory: MEM1, the hardware
 * registers, MEM2, and the locked cache. */
static const struct {
    uint32_t base, size;
} kRegions[] = {
    {0x00000000u, 0x01800000u},
    {0x0C000000u, 0x02000000u}, /* GameCube-era registers and Hollywood's (0xCD800000) */
    {0x10000000u, 0x04000000u},
    {0x20000000u, 0x00004000u},
};

int ppc_mem_init(void) {
    size_t i;
#ifdef _WIN32
    /* Written pages are tracked (write watch) so the GPU code can tell
     * which textures may have changed (gx_fifo.c). */
    g_mem = (uint8_t*)VirtualAlloc(NULL, GUEST_SPACE, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_NOACCESS);
    if (!g_mem) return -1;
    for (i = 0; i < sizeof(kRegions) / sizeof(kRegions[0]); i++) {
        if (!VirtualAlloc(g_mem + kRegions[i].base, kRegions[i].size, MEM_COMMIT, PAGE_READWRITE)) return -1;
    }
#else
    g_mem = (uint8_t*)mmap(NULL, GUEST_SPACE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_mem == MAP_FAILED) return -1;
    for (i = 0; i < sizeof(kRegions) / sizeof(kRegions[0]); i++) {
        if (mprotect(g_mem + kRegions[i].base, kRegions[i].size, PROT_READ | PROT_WRITE)) return -1;
    }
#endif
    return 0;
}

/* Which guest pages have been written, for the texture cache (gx_fifo.c),
 * where Windows' write watch isn't: a page a texture is on is made
 * read-only before the texture is hashed, and the first write to it
 * faults here, which notes the write (a stamp, from a count of the writes
 * noted) and makes the page writable again. Pages no texture is on are
 * never protected, so the game's other writes cost nothing; hashing every
 * texture every frame took a tenth of the game thread on the Thor. Writes
 * the kernel makes (read() into guest memory) fail rather than fault, so
 * they go through ppc_mem_host_write first. RECOMP_MEM_TRACK=0 turns it
 * off (every texture is hashed every frame). */
#ifdef _WIN32
int ppc_mem_watch(uint32_t addr, uint32_t size, uint32_t* stamp) {
    (void)addr, (void)size, (void)stamp;
    return 0;
}
int ppc_mem_written(uint32_t addr, uint32_t size, uint32_t stamp) {
    (void)addr, (void)size, (void)stamp;
    return 1;
}
void ppc_mem_host_write(uint32_t addr, uint32_t size) { (void)addr, (void)size; }
#else
#define TRACK_MEM1 0x01800000u
#define TRACK_PAGES ((TRACK_MEM1 + 0x04000000u) >> 12)
static uint32_t sStamp;                 /* counts the writes noted */
static uint32_t sWritten[TRACK_PAGES];  /* the stamp of each page's last noted write */
static uint8_t sWatched[TRACK_PAGES];   /* read-only, its next write to be noted */
static int sTrack = -1;                 /* 1 working, 0 not, -1 not set up yet */
static uint32_t sHostPage;              /* the host's page size: 4 KB, or 16 KB on some */
static atomic_flag sTrackLock = ATOMIC_FLAG_INIT;
static struct sigaction sPrevSegv;

/* The tracked page (4 KB) a physical address is in, or -1 outside MEM1
 * and MEM2. */
static int track_page(uint32_t phys) {
    if (phys < TRACK_MEM1) return (int)(phys >> 12);
    if (phys - 0x10000000u < 0x04000000u) return (int)((TRACK_MEM1 + (phys - 0x10000000u)) >> 12);
    return -1;
}

static void track_lock(void) {
    while (atomic_flag_test_and_set_explicit(&sTrackLock, memory_order_acquire)) {
    }
}
static void track_unlock(void) { atomic_flag_clear_explicit(&sTrackLock, memory_order_release); }

/* Notes a write to the host page at phys (aligned) and makes it writable;
 * with the lock held. */
static void unwatch(uint32_t phys) {
    int pg = track_page(phys), i, n = (int)(sHostPage >> 12);
    uint32_t stamp = ++sStamp;
    for (i = 0; i < n; i++) {
        sWritten[pg + i] = stamp;
        __atomic_store_n(&sWatched[pg + i], 0, __ATOMIC_RELEASE);
    }
    mprotect(g_mem + phys, sHostPage, PROT_READ | PROT_WRITE);
}

static void on_write(int sig, siginfo_t* info, void* context) {
    uintptr_t at = (uintptr_t)info->si_addr - (uintptr_t)g_mem;
    if (at < GUEST_SPACE && track_page((uint32_t)at) >= 0) {
        /* MEM1 and MEM2 are only ever read-only here, so the write is one
         * to note, unless another thread already did (then the page is
         * writable and the write goes through when retried). */
        uint32_t phys = (uint32_t)at & ~(sHostPage - 1);
        track_lock();
        if (sWatched[track_page(phys)]) unwatch(phys);
        track_unlock();
        return;
    }
    if (sPrevSegv.sa_flags & SA_SIGINFO) sPrevSegv.sa_sigaction(sig, info, context);
    else if (sPrevSegv.sa_handler != SIG_DFL && sPrevSegv.sa_handler != SIG_IGN) sPrevSegv.sa_handler(sig);
    else sigaction(SIGSEGV, &sPrevSegv, NULL); /* the fault comes again, as it would have */
}

static int track_start(void) {
    const char* env = getenv("RECOMP_MEM_TRACK");
    struct sigaction sa;
    long page = sysconf(_SC_PAGESIZE);
    if ((env && *env == '0') || page < 4096 || (page & (page - 1)) || page > 0x10000) return 0;
    sHostPage = (uint32_t)page;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_write;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    return sigaction(SIGSEGV, &sa, &sPrevSegv) == 0;
}

/* The first and last host pages of [addr, addr + size), or 0 if the range
 * isn't all in MEM1 or all in MEM2. */
static int host_pages(uint32_t addr, uint32_t size, uint32_t* first, uint32_t* last) {
    uint32_t phys = addr & 0x3FFFFFFFu, end = phys + (size ? size : 1) - 1;
    int a = track_page(phys), b = track_page(end);
    if (a < 0 || b < 0 || b - a != (int)((end >> 12) - (phys >> 12))) return 0;
    *first = phys & ~(sHostPage - 1);
    *last = end & ~(sHostPage - 1);
    return 1;
}

int ppc_mem_watch(uint32_t addr, uint32_t size, uint32_t* stamp) {
    uint32_t first, last, p, run = 0;
    int have_run = 0;
    if (sTrack < 0) sTrack = track_start();
    if (!sTrack || !host_pages(addr, size, &first, &last)) return 0;
    track_lock();
    for (p = first;; p += sHostPage) {
        int pg = track_page(p), i, n = (int)(sHostPage >> 12), all = 1;
        for (i = 0; i < n; i++) all &= sWatched[pg + i];
        if (!all) {
            for (i = 0; i < n; i++) sWatched[pg + i] = 1;
            if (!have_run) run = p, have_run = 1;
        }
        /* Pages left writable next to each other are protected together. */
        if (have_run && (all || p == last)) {
            mprotect(g_mem + run, (all ? p : p + sHostPage) - run, PROT_READ);
            have_run = 0;
        }
        if (p == last) break;
    }
    *stamp = sStamp;
    track_unlock();
    return 1;
}

int ppc_mem_written(uint32_t addr, uint32_t size, uint32_t stamp) {
    uint32_t phys = addr & 0x3FFFFFFFu, end = phys + (size ? size : 1) - 1;
    int a = track_page(phys), b = track_page(end), i;
    if (sTrack != 1 || a < 0 || b < 0 || b - a != (int)((end >> 12) - (phys >> 12))) return 1;
    for (i = a; i <= b; i++) {
        if (!__atomic_load_n(&sWatched[i], __ATOMIC_ACQUIRE) || sWritten[i] > stamp) return 1;
    }
    return 0;
}

void ppc_mem_host_write(uint32_t addr, uint32_t size) {
    uint32_t first, last, p;
    if (sTrack != 1 || !size || !host_pages(addr, size, &first, &last)) return;
    track_lock();
    for (p = first;; p += sHostPage) {
        int pg = track_page(p), i, n = (int)(sHostPage >> 12), any = 0;
        for (i = 0; i < n; i++) any |= sWatched[pg + i];
        if (any) unwatch(p);
        if (p == last) break;
    }
    track_unlock();
}
#endif

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int ppc_load_dol(const char* path, uint32_t* entry) {
    FILE* f = fopen(path, "rb");
    uint8_t hdr[0x100];
    int i;
    if (!f) return -1;
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return -1; }
    /* The bss range in the header spans .sdata and .sdata2 too, so clear
     * it before the sections are copied in, not after. */
    memset(MA(be32(hdr + 0xD8)), 0, be32(hdr + 0xDC));
    for (i = 0; i < 18; i++) {
        uint32_t off = be32(hdr + 4 * i), addr = be32(hdr + 0x48 + 4 * i), size = be32(hdr + 0x90 + 4 * i);
        if (!size) continue;
        fseek(f, (long)off, SEEK_SET);
        if (fread(MA(addr), 1, size, f) != size) { fclose(f); return -1; }
    }
    if (entry) *entry = be32(hdr + 0xE0);
    fclose(f);
    return 0;
}

int ppc_load_dol_mem(const uint8_t* dol, size_t len, uint32_t* entry) {
    int i;
    if (len < 0x100) return -1;
    memset(MA(be32(dol + 0xD8)), 0, be32(dol + 0xDC)); /* as above */
    for (i = 0; i < 18; i++) {
        uint32_t off = be32(dol + 4 * i), addr = be32(dol + 0x48 + 4 * i), size = be32(dol + 0x90 + 4 * i);
        if (!size) continue;
        if ((uint64_t)off + size > len) return -1;
        memcpy(MA(addr), dol + off, size);
    }
    if (entry) *entry = be32(dol + 0xE0);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Indirect calls */

#define MAX_OVERRIDES 256
static struct {
    uint32_t addr;
    ppc_func fn;
} sOverrides[MAX_OVERRIDES];
static int sOverrideCount;

/* Indirect calls (bctrl, function pointers, virtual calls) look their
 * target up here. For main.dol's code, below 0x80800000, a table indexed
 * by address answers in one load; it is built on first use and follows
 * overrides. */
#define DIRECT_BASE 0x80000000u
#define DIRECT_COUNT 0x200000u /* 8 MB of code, one entry per word */
static ppc_func* sDirect;

static void build_direct(void) {
    unsigned i;
    sDirect = (ppc_func*)calloc(DIRECT_COUNT, sizeof(ppc_func));
    if (!sDirect) return;
    for (i = 0; i < ppc_func_count; i++) {
        uint32_t a = ppc_func_table[i].addr;
        if (a - DIRECT_BASE < DIRECT_COUNT * 4) sDirect[(a - DIRECT_BASE) >> 2] = ppc_func_table[i].fn;
    }
    for (i = 0; i < (unsigned)sOverrideCount; i++) {
        uint32_t a = sOverrides[i].addr;
        if (a - DIRECT_BASE < DIRECT_COUNT * 4) sDirect[(a - DIRECT_BASE) >> 2] = sOverrides[i].fn;
    }
}

void ppc_set_override(uint32_t addr, ppc_func fn) {
    int i;
    if (sDirect && addr - DIRECT_BASE < DIRECT_COUNT * 4) sDirect[(addr - DIRECT_BASE) >> 2] = fn;
    for (i = 0; i < sOverrideCount; i++) {
        if (sOverrides[i].addr == addr) { sOverrides[i].fn = fn; return; }
    }
    if (sOverrideCount < MAX_OVERRIDES) {
        sOverrides[sOverrideCount].addr = addr;
        sOverrides[sOverrideCount].fn = fn;
        sOverrideCount++;
    }
}

ppc_func ppc_lookup(uint32_t addr) {
    unsigned lo = 0, hi = ppc_func_count;
    int i;
    if (!sDirect) build_direct();
    if (sDirect && addr - DIRECT_BASE < DIRECT_COUNT * 4 && !(addr & 3)) return sDirect[(addr - DIRECT_BASE) >> 2];
    for (i = 0; i < sOverrideCount; i++) {
        if (sOverrides[i].addr == addr) return sOverrides[i].fn;
    }
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (ppc_func_table[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo < ppc_func_count && ppc_func_table[lo].addr == addr) return ppc_func_table[lo].fn;
    return NULL;
}

void ppc_icall(ppc_t* c, uint32_t addr) {
    ppc_func fn = ppc_lookup(addr);
    if (!fn) ppc_unresolved(c, addr);
    else fn(c);
}

uint32_t g_trace[256];
unsigned g_trace_pos;
uint32_t g_calls[0x100000];
int g_count_calls;

/* Name of the function containing addr, for diagnostics. */
const char* ppc_func_name(uint32_t addr) {
    unsigned lo = 0, hi = ppc_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (ppc_func_table[mid].addr <= addr) lo = mid + 1;
        else hi = mid;
    }
    return lo ? ppc_func_table[lo - 1].name : "?";
}

void ppc_dump_trace(void) {
#ifdef RECOMP_TRACE
    /* Snapshot the position: the watchdog dumps while the game runs. */
    unsigned end = g_trace_pos;
    unsigned i, n = end < 200 ? end : 200;
    fprintf(stderr, "last %u functions entered (oldest first):\n", n);
    for (i = end - n; i != end; i++) {
        uint32_t a = g_trace[i & 255];
        fprintf(stderr, "  %08X %s\n", a, ppc_func_name(a));
    }
#endif
}

/* Walks the guest stack's back chain. Each frame starts with a pointer to
 * the caller's frame, and a function saves its return address 4 bytes
 * into its caller's frame. */
void ppc_backtrace(ppc_t* c) {
    uint32_t sp = c->r[1];
    int depth;
    fprintf(stderr, "guest backtrace (lr %08X %s):\n", c->lr, ppc_func_name(c->lr));
    for (depth = 0; depth < 24; depth++) {
        uint32_t next, ra;
        if (sp < 0x80000000u || sp >= 0x81800000u) break;
        next = LD32(sp);
        if (next < 0x80000000u || next >= 0x81800000u || next <= sp) break;
        ra = LD32(next + 4);
        fprintf(stderr, "  %08X %s\n", ra, ppc_func_name(ra));
        sp = next;
    }
}

static void fatal(const char* what, uint32_t addr, uint32_t extra) {
    fprintf(stderr, "recomp: %s at %08X [%s] (%08X)\n", what, addr, ppc_func_name(addr), extra);
    ppc_dump_trace();
    exit(1);
}

void ppc_unresolved(ppc_t* c, uint32_t addr) { fatal("call to unknown address", addr, c->lr); }
void ppc_unimpl(ppc_t* c, uint32_t addr, uint32_t ins) { (void)c; fatal("untranslated instruction", addr, ins); }
void ppc_trap(ppc_t* c, uint32_t addr) { fatal("trap", addr, c->lr); }
/* The only system call the RVL OS installs (__OSSystemCallVector) syncs
 * the instruction pipeline and caches, which needs nothing here. */
void ppc_syscall(ppc_t* c) { (void)c; }
/* rfi continues at SRR0 with MSR = SRR1. The SDK uses it to enter real
 * mode (RealMode -> BATConfig) and to come back from it; returning to
 * the caller's return address is treated as a plain return. Thread
 * switches (OSLoadContext) are handled by hooks, not here. */
void ppc_rfi(ppc_t* c) {
    uint32_t target = c->spr[26];
    ppc_func fn;
    c->msr = c->spr[27];
    if (target < 0x01800000u) target |= 0x80000000u; /* physical -> cached virtual */
    if (target == c->lr) return;
    fn = ppc_lookup(target);
    if (!fn) fatal("rfi to unknown address", target, c->lr);
    fn(c);
}

/* ------------------------------------------------------------------------ */
/* Special-purpose registers */

/* Decrementer: counts down at the time-base rate; the exception is taken
 * when it passes zero. Only the deadline is tracked. */
static uint64_t sDecDeadline;
static int sDecArmed;

int ppc_dec_deadline(uint64_t* deadline) {
    if (!sDecArmed) return 0;
    *deadline = sDecDeadline;
    return 1;
}

void ppc_dec_disarm(void) { sDecArmed = 0; }

uint32_t ppc_mfspr(ppc_t* c, int spr) {
    if (spr == 22) return sDecArmed ? (uint32_t)(sDecDeadline - ppc_timebase()) : 0xFFFFFFFFu;
    if (spr == 1) return (c->xer_so << 31) | (c->xer_ca << 29) | (c->xer_low & 0x7F);
    if (spr >= 912 && spr <= 919) return c->gqr[spr - 912];
    return c->spr[spr];
}

void ppc_mtspr(ppc_t* c, int spr, uint32_t v) {
    if (spr == 1) {
        c->xer_so = v >> 31;
        c->xer_ca = (v >> 29) & 1;
        c->xer_low = v & 0x7F;
    } else if (spr >= 912 && spr <= 919) {
        c->gqr[spr - 912] = v;
    } else if (spr == 22) {
        /* A negative value means the exception is already pending. */
        sDecDeadline = ppc_timebase() + (uint64_t)(int64_t)(int32_t)v;
        sDecArmed = 1;
    } else if (spr == 923 && (v & 2)) {
        /* DMAL with the trigger bit: a locked-cache DMA. DMAU holds the
         * memory address and the high bits of the length; DMAL the
         * locked-cache address, the direction (bit 4: 1 = memory to
         * cache), and the low bits of the length, in 32-byte blocks
         * (0 = 128). The transfer completes at once. */
        uint32_t dmau = c->spr[922];
        uint32_t blocks = ((dmau & 0x1F) << 2) | ((v >> 2) & 3);
        uint32_t mem = dmau & ~0x1Fu, lc = v & ~0x1Fu;
        if (!blocks) blocks = 128;
        if (v & 0x10) memmove(MA(lc), MA(mem), blocks * 32);
        else memmove(MA(mem), MA(lc), blocks * 32);
        c->spr[923] = v & ~2u;
    } else {
        c->spr[spr] = v;
    }
}

/* The time base runs at a quarter of the 243 MHz bus clock. */
/* The time base follows the host clock. It is read very often (every
 * check for a due interrupt, which the game makes each time it enables
 * them), so where the CPU's timestamp counter runs at a fixed rate it is
 * read from that (a few nanoseconds), at a rate measured against the
 * performance counter over the first second; until then, and on CPUs
 * without one, from the performance counter itself. Only the game's
 * thread reads it. */
#ifdef _WIN32
static int invariant_tsc(void) {
    int r[4];
    __cpuid(r, 0x80000000);
    if ((unsigned)r[0] < 0x80000007u) return 0;
    __cpuid(r, 0x80000007);
    return (r[3] >> 8) & 1;
}
#endif

#ifndef _WIN32
static uint64_t sPausedTicks;
#endif

uint64_t ppc_timebase(void) {
    uint64_t ticks;
#ifdef _WIN32
    enum { UNSET, COUNTER, MEASURING, TSC };
    static int mode;
    static double scale, perTsc;
    static uint64_t tsc0, tb0;
    static LONGLONG qpc0;
    LARGE_INTEGER now;
    uint64_t tsc;
    if (mode == TSC) return tb0 + (uint64_t)((double)(__rdtsc() - tsc0) * perTsc);
    if (mode == UNSET) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        scale = 60750000.0 / (double)freq.QuadPart;
        mode = invariant_tsc() ? MEASURING : COUNTER;
        QueryPerformanceCounter(&now);
        tsc0 = __rdtsc();
        qpc0 = now.QuadPart;
    }
    QueryPerformanceCounter(&now);
    tsc = __rdtsc();
    ticks = (uint64_t)((double)now.QuadPart * scale);
    if (mode == MEASURING && (double)(now.QuadPart - qpc0) * scale >= 60750000.0 && tsc > tsc0) {
        /* Carries on from this reading, so the time base never steps. */
        perTsc = (double)(now.QuadPart - qpc0) * scale / (double)(tsc - tsc0);
        tsc0 = tsc;
        tb0 = ticks;
        mode = TSC;
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ticks = (uint64_t)ts.tv_sec * 60750000ull + (uint64_t)ts.tv_nsec * 60750000ull / 1000000000ull - sPausedTicks;
#endif
    return ticks;
}

#ifndef _WIN32
/* Stops and restarts the time base, so time spent away (an Android app
 * in the background) isn't caught up on when the game carries on. */
void ppc_timebase_pause(int paused) {
    static uint64_t since;
    if (paused && !since) {
        since = ppc_timebase() + 1;
    } else if (!paused && since) {
        sPausedTicks += ppc_timebase() + 1 - since;
        since = 0;
    }
}
#endif

uint32_t ppc_mftb(ppc_t* c, int tbr) {
    uint64_t ticks = ppc_timebase();
    (void)c;
    return tbr == 269 ? (uint32_t)(ticks >> 32) : (uint32_t)ticks;
}

double ppc_fctiw(ppc_t* c, double v) {
    double r;
    int32_t i;
    switch (c->fpscr & 3) {
    case 0: r = nearbyint(v); break; /* host default is round-to-nearest-even */
    case 1: r = trunc(v); break;
    case 2: r = ceil(v); break;
    default: r = floor(v); break;
    }
    if (r != r) i = (int32_t)0x80000000;
    else if (r >= 2147483647.0) i = 0x7FFFFFFF;
    else if (r <= -2147483648.0) i = (int32_t)0x80000000;
    else i = (int32_t)r;
    return bits_f64(0xFFF8000000000000ull | (uint32_t)i);
}
