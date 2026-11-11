/* OS console output. OSReport and OSPanic go nowhere on a retail Wii; here
 * they print to stderr, which shows the game's and the SDK's own
 * diagnostics. */
#include <stdlib.h>
#include <string.h>

#include "hle.h"

/* Formats a guest printf-style string whose arguments are in registers:
 * integers from r[first_gpr] to r10, floating point from f1 to f8 (the
 * PowerPC EABI for variadic calls). Arguments that spill to the stack are
 * not read. */
static void format_guest(ppc_t* c, uint32_t fmt, int first_gpr, char* out, size_t n) {
    char f[512], spec[32], tmp[256];
    int gpr = first_gpr, fpr = 1;
    size_t o = 0, i;
    hle_read_str(fmt, f, sizeof(f));
    for (i = 0; f[i] && o + 1 < n; i++) {
        size_t s = 0;
        if (f[i] != '%') {
            out[o++] = f[i];
            continue;
        }
        spec[s++] = f[i++];
        while (f[i] && strchr("-+ #0123456789.lhz", f[i]) && s < sizeof(spec) - 2) spec[s++] = f[i++];
        if (!f[i]) break;
        spec[s++] = f[i];
        spec[s] = '\0';
        tmp[0] = '\0';
        switch (f[i]) {
        case '%':
            snprintf(tmp, sizeof(tmp), "%%");
            break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'c': case 'o': {
            char clean[32];
            size_t k, m = 0;
            for (k = 0; spec[k]; k++) {
                if (spec[k] != 'l' && spec[k] != 'h' && spec[k] != 'z') clean[m++] = spec[k];
            }
            clean[m] = '\0';
            snprintf(tmp, sizeof(tmp), clean, gpr <= 10 ? (int)c->r[gpr] : 0);
            gpr++;
            break;
        }
        case 'p':
            snprintf(tmp, sizeof(tmp), "%08X", gpr <= 10 ? c->r[gpr] : 0);
            gpr++;
            break;
        case 's': {
            char str[200];
            if (gpr <= 10 && c->r[gpr]) hle_read_str(c->r[gpr], str, sizeof(str));
            else strcpy(str, "(null)");
            gpr++;
            spec[s - 1] = 's';
            snprintf(tmp, sizeof(tmp), spec, str);
            break;
        }
        case 'f': case 'g': case 'e':
            snprintf(tmp, sizeof(tmp), spec, fpr <= 8 ? c->f[fpr] : 0.0);
            fpr++;
            break;
        default:
            snprintf(tmp, sizeof(tmp), "%s", spec);
            break;
        }
        for (s = 0; tmp[s] && o + 1 < n; s++) out[o++] = tmp[s];
    }
    out[o] = '\0';
}

/* void OSReport(const char* fmt, ...) */
void F_OSReport(ppc_t* c) {
    char msg[1024];
    format_guest(c, c->r[3], 4, msg, sizeof(msg));
    fprintf(stderr, "OSReport: %s%s", msg, msg[0] && msg[strlen(msg) - 1] == '\n' ? "" : "\n");
}

/* void OSPanic(const char* file, int line, const char* fmt, ...) */
void F_OSPanic(ppc_t* c) {
    char msg[1024], file[128];
    hle_read_str(c->r[3], file, sizeof(file));
    format_guest(c, c->r[5], 6, msg, sizeof(msg));
    fprintf(stderr, "OSPanic (%s:%d): %s\n", file, (int)c->r[4], msg);
    ppc_backtrace(c);
    exit(6);
}

/* void* MEMAllocFromAllocator(MEMAllocator* allocator, u32 size): failed
 * allocations are logged. */
void F_MEMAllocFromAllocator(ppc_t* c) {
    uint32_t allocator = c->r[3], size = c->r[4];
    F_MEMAllocFromAllocator_orig(c);
    if (size >= 0x80000 && hle_log_enabled()) {
        HLE_LOG("MEMAllocFromAllocator(%08X, %u KB) = %08X, from %s", allocator, size >> 10, c->r[3], ppc_func_name(c->lr));
        if (hle_log_enabled() > 2) ppc_backtrace(c);
    }
    if (!c->r[3]) {
        uint32_t heap = LD32(allocator + 4);
        /* MEMGetAllocatableSizeForExpHeapEx(heap, 4): the largest block. */
        uint32_t largest = heap && A_MEMGetAllocatableSizeForExpHeapEx ? hle_call(c, A_MEMGetAllocatableSizeForExpHeapEx, heap, 4) : 0;
        HLE_WARN("MEMAllocFromAllocator(%08X, %u) failed; heap %08X, largest free block %u", allocator, size, heap,
                 largest);
        if (hle_log_enabled()) ppc_backtrace(c);
    }
}
