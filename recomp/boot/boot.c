/* Boot harness: loads main.dol, sets up low memory the way the Wii's
 * system menu and apploader leave it, and runs the game's __start.
 *
 * Usage: boot <main.dol> [timeout seconds, default 10]
 *
 * The disc is read from a "disc" directory next to the directory holding
 * main.dol (orig/R8AE01/disc for orig/R8AE01/sys/main.dol), holding sys/
 * and files/ as extracted by dtk (see recomp/README.md). */
#include <stdio.h>
#include <stdlib.h>

#include "gx.h"
#include "recomp_funcs.h"

int hle_dvd_init(const char* root);
void hle_dvd_boot_setup(void);
void hle_nand_set_root(const char* root);
void hle_dump_threads(void);
void input_init(void);

static ppc_t* gCpu;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* RECOMP_WATCH: the watched guest range and its write-protected page. */
static uint32_t gWatchPhys, gWatchLen;
static uint8_t* gWatchPage;

static LONG CALLBACK on_fault(EXCEPTION_POINTERS* info) {
    EXCEPTION_RECORD* r = info->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && gWatchPage) {
        /* Finished a write elsewhere on the watched page: protect it again. */
        DWORD old;
        VirtualProtect(gWatchPage, 0x1000, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t a = (uintptr_t)r->ExceptionInformation[1];
        if (gWatchPage && (uint8_t*)a >= gWatchPage && (uint8_t*)a < gWatchPage + 0x1000 &&
            (uint32_t)(a - (uintptr_t)g_mem - gWatchPhys) >= gWatchLen) {
            /* Not the watched range: let this one write through, then
             * single-step and re-protect. */
            DWORD old;
            VirtualProtect(gWatchPage, 0x1000, PAGE_READWRITE, &old);
            info->ContextRecord->EFlags |= 0x100;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (a >= (uintptr_t)g_mem && a < (uintptr_t)g_mem + 0x40000000u) {
            fprintf(stderr, "boot: guest memory fault at physical %08X (%s)\n", (unsigned)(a - (uintptr_t)g_mem),
                    r->ExceptionInformation[0] ? "write" : "read");
            if (gCpu) ppc_backtrace(gCpu);
        } else {
            fprintf(stderr, "boot: host memory fault at %p\n", (void*)a);
        }
        ppc_dump_trace();
        fflush(stderr);
        ExitProcess(3);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Stops a boot that hangs (usually polling hardware that isn't emulated)
 * and shows where it was. */
static int by_calls(const void* a, const void* b) {
    uint32_t x = g_calls[*(const uint32_t*)a], y = g_calls[*(const uint32_t*)b];
    return x < y ? 1 : x > y ? -1 : 0;
}

/* RECOMP_CALLS=<n>: the n functions called most in the run's last 5 s. */
static void dump_calls(int n) {
    static uint32_t idx[0x100000];
    uint32_t i, used = 0;
    for (i = 0; i < 0x100000; i++) {
        if (g_calls[i]) idx[used++] = i;
    }
    qsort(idx, used, sizeof(idx[0]), by_calls);
    fprintf(stderr, "most called in the last 5 s:\n");
    for (i = 0; i < used && (int)i < n; i++) {
        uint32_t a = 0x80000000u | idx[i] << 2;
        fprintf(stderr, "  %8u  %08X %s\n", g_calls[idx[i]], a, ppc_func_name(a));
    }
}

static DWORD WINAPI watchdog(LPVOID arg) {
    DWORD secs = (DWORD)(uintptr_t)arg;
    const char* calls = getenv("RECOMP_CALLS");
    if (calls && secs > 5) {
        Sleep((secs - 5) * 1000);
        memset(g_calls, 0, sizeof(g_calls));
        Sleep(5000);
        dump_calls(atoi(calls));
    } else {
        Sleep(secs * 1000);
    }
    fprintf(stderr, "boot: still running after %u s; stopping\n", (unsigned)secs);
    ppc_dump_trace();
    if (gCpu) ppc_backtrace(gCpu);
    hle_dump_threads();
    {
        /* RECOMP_PEEK=<address>,<length>: dump guest memory on timeout. */
        const char* p = getenv("RECOMP_PEEK");
        if (p) {
            char* end;
            uint32_t addr = (uint32_t)strtoul(p, &end, 16), len = *end == ',' ? (uint32_t)strtoul(end + 1, NULL, 16) : 0x40, i;
            for (i = 0; i < len; i += 4) {
                if (i % 16 == 0) fprintf(stderr, "\n  %08X:", addr + i);
                fprintf(stderr, " %08X", LD32(addr + i));
            }
            fputc(10, stderr);
        }
    }
    fflush(stderr);
    ExitProcess(4);
    return 0;
}
#endif

/* What the system menu leaves in low memory before jumping to the game,
 * following Dolphin's HLE boot (Core/Boot/Boot_BS2Emu.cpp). */
static void setup_low_memory(void) {
    static const char kDiscId[] = "R8AE01";
    memcpy(MA(0x80000000), kDiscId, 6);
    ST32(0x80000018, 0x5D1C9EA3); /* Wii disc magic */
    ST32(0x80000020, 0x0D15EA5E); /* boot magic */
    ST32(0x80000024, 0x00000001);
    ST32(0x80000028, 0x01800000); /* MEM1 size */
    ST32(0x8000002C, 0x00000023); /* console type: retail Wii */
    ST32(0x80000030, 0x00000000); /* arena lo: set by the OS */
    ST32(0x80000034, 0x817FEC60); /* arena hi */
    ST32(0x800000CC, 0x00000000); /* video mode: NTSC */
    ST32(0x800000F0, 0x01800000); /* simulated memory size */
    ST32(0x800000F8, 0x0E7BE2C0); /* bus clock, 243 MHz */
    ST32(0x800000FC, 0x2B73A840); /* CPU clock, 729 MHz */
    ST32(0x800030D8, 0x00000000); /* system time */
    ST16(0x800030E6, 0x0000);     /* retail console (bit 15 would mean a development kit) */
    ST32(0x80003100, 0x01800000); /* MEM1 physical size */
    ST32(0x80003104, 0x01800000); /* MEM1 simulated size */
    ST32(0x80003108, 0x817FEC60); /* end of usable MEM1 */
    ST32(0x8000310C, 0x00000000);
    ST32(0x80003110, 0x817FEC60); /* MEM1 arena end */
    ST32(0x80003118, 0x04000000); /* MEM2 physical size */
    ST32(0x8000311C, 0x04000000); /* MEM2 simulated size */
    /* On a Wii, IOS keeps the top of MEM2 (0x933E0000 up). Nothing runs on
     * the Starlet here, so the game gets that memory too, and the IPC
     * buffer moves to the very top. */
    ST32(0x80003120, 0x94000000); /* end of MEM2 */
    ST32(0x80003124, 0x90000800); /* usable MEM2 start */
    ST32(0x80003128, 0x93FE0000); /* usable MEM2 end */
    ST32(0x80003130, 0x93FE0000); /* IOS IPC buffer start */
    ST32(0x80003134, 0x94000000); /* IOS IPC buffer end */
    ST32(0x80003138, 0x00000011); /* Hollywood revision */
    ST32(0x80003140, 0x00090204); /* IOS version: IOS9 v516 */
    ST32(0x80003144, 0x00062507); /* IOS build date */
    ST32(0x80003158, 0x0000FF16); /* DDR vendor */
    ST32(0x80003188, 0x00090204); /* IOS version the apploader saw */
}

int main(int argc, char** argv) {
    ppc_t* c;
    uint32_t entry;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <main.dol>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
    AddVectoredExceptionHandler(1, on_fault);
    CreateThread(NULL, 0, watchdog, (LPVOID)(uintptr_t)(argc > 2 ? atoi(argv[2]) : 10), 0, NULL);
#endif
    if (ppc_mem_init() != 0 || ppc_load_dol(argv[1], &entry) != 0) {
        fprintf(stderr, "could not load %s\n", argv[1]);
        return 2;
    }
    setup_low_memory();
    {
        /* <dir>/sys/main.dol -> <dir>/disc */
        char root[1024];
        char* slash;
        strncpy(root, argv[1], sizeof(root) - 16);
        root[sizeof(root) - 16] = '\0';
        for (slash = root; *slash; slash++) {
            if (*slash == '\\') *slash = '/';
        }
        slash = strrchr(root, '/');
        if (slash) *slash = '\0';
        slash = strrchr(root, '/');
        if (slash) strcpy(slash + 1, "disc");
        else strcpy(root, "disc");
        if (hle_dvd_init(root) == 0) hle_dvd_boot_setup();
        /* Saves live in "nand" beside "disc". */
        strcpy(root + strlen(root) - 4, "nand");
        hle_nand_set_root(root);
    }
    {
        /* RECOMP_WATCH=<guest address>[,<length>]: stop on the first write
         * to that range, with a trace (for finding memory corruption). */
        const char* w = getenv("RECOMP_WATCH");
        if (w) {
            DWORD old;
            char* end;
            uint32_t addr = (uint32_t)strtoul(w, &end, 16);
            gWatchLen = *end == ',' ? (uint32_t)strtoul(end + 1, NULL, 16) : 4;
            gWatchPhys = addr & 0x3FFFFFFF;
            gWatchPage = MA(addr & ~0xFFFu);
            VirtualProtect(gWatchPage, 0x1000, PAGE_READONLY, &old);
        }
    }
    gx_init();
    input_init();
    c = gCpu = (ppc_t*)calloc(1, sizeof(ppc_t));
    c->msr = 0x00002032; /* FP available, instruction and data translation on */
    printf("boot: entering __start at %08X\n", entry);
    ppc_lookup(entry)(c);
    printf("boot: __start returned\n");
    return 0;
}
