// Checks recompiled game functions against two references:
//  - the decompiled source in src/, compiled natively for the host, and
//  - the host C library, for the game's copies of the MSL C library.
//
// Usage: test_recomp <path to main.dol>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "recomp_funcs.h"

// The decompiled source, built for the host.
#include "flib/CodepointConvert.h"
void Demangle_FName(const u32* in, char* out);

// Guest functions under test.
static const uint32_t kUnicodeToSJIS = 0x802EDC10;
static const uint32_t kEncodeCodepoint = 0x802EDA20;
static const uint32_t kDemangleFName = 0x802ED920;
static const uint32_t kStrlen = 0x802F3464;
static const uint32_t kStrcmp = 0x802FA7B8;
static const uint32_t kSprintf = 0x802FA410;
static const uint32_t kAtoi = 0x802FAF30;
static const uint32_t kSin = 0x803012AC;
static const uint32_t kCos = 0x80300DA4;
static const uint32_t kTan = 0x80301378;
static const uint32_t kAtan = 0x803009FC;
static const uint32_t kAtan2 = 0x803013F8;
static const uint32_t kPow = 0x80301404;
static const uint32_t kSqrt = 0x80301650;
static const uint32_t kFmod = 0x803013FC;
static const uint32_t kInitRegisters = 0x800041C0;

// Scratch guest memory for arguments and results, well above the DOL's bss.
static const uint32_t kBuf = 0x81000000;
static const uint32_t kBuf2 = 0x81001000;

static ppc_t gCpu;
static ppc_t gReset;
static int gFailures;
static int gChecks;

static void call(uint32_t addr) {
    ppc_func fn = ppc_lookup(addr);
    if (!fn) {
        fprintf(stderr, "no function at %08X\n", addr);
        exit(1);
    }
    // Restore the stack and small-data pointers between calls so one
    // test can't leak state into the next.
    gCpu.r[1] = gReset.r[1];
    gCpu.r[2] = gReset.r[2];
    gCpu.r[13] = gReset.r[13];
    fn(&gCpu);
}

static void check(bool ok, const char* test, const char* fmt, ...) {
    gChecks++;
    if (ok) return;
    if (++gFailures <= 20) {
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "FAIL %s: ", test);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }
}

static void put_str(uint32_t addr, const char* s) { memcpy(MA(addr), s, strlen(s) + 1); }

static double ulps(double a, double b) {
    if (a == b || (std::isnan(a) && std::isnan(b))) return 0;
    int64_t ia, ib;
    memcpy(&ia, &a, 8);
    memcpy(&ib, &b, 8);
    if (ia < 0) ia = INT64_MIN - ia;
    if (ib < 0) ib = INT64_MIN - ib;
    return std::fabs((double)(ia - ib));
}

static void test_unicode_to_sjis() {
    int before = gFailures;
    for (int64_t cp = -0x100; cp < 0x110100; cp++) {
        gCpu.r[3] = (uint32_t)cp;
        call(kUnicodeToSJIS);
        int32_t want = UnicodeToSJIS((s32)cp);
        check((int32_t)gCpu.r[3] == want, "UnicodeToSJIS", "cp %X: got %X want %X", (unsigned)cp, gCpu.r[3], want);
    }
    printf("UnicodeToSJIS: %s over 1.1M code points\n", gFailures == before ? "match" : "MISMATCH");
}

static void test_encode_codepoint() {
    int before = gFailures;
    std::mt19937 rng(1);
    for (int enc = 0; enc <= 4; enc++) {
        for (int n = 0; n < 300000; n++) {
            int32_t cp = n < 200000 ? n : (int32_t)rng();
            char host[8] = {0};
            int want = EncodeCodepoint(host, cp, enc);
            if (enc == ENCODING_UTF16) {
                // The host wrote native-endian u16s; the guest writes big-endian.
                for (int i = 0; i + 1 < 8; i += 2) std::swap(host[i], host[i + 1]);
            }
            memset(MA(kBuf), 0, 8);
            gCpu.r[3] = kBuf;
            gCpu.r[4] = (uint32_t)cp;
            gCpu.r[5] = (uint32_t)enc;
            call(kEncodeCodepoint);
            int got = (int32_t)gCpu.r[3];
            bool ok = got == want && (want <= 0 || memcmp(MA(kBuf), host, want) == 0);
            check(ok, "EncodeCodepoint", "enc %d cp %X: got %d want %d", enc, cp, got, want);
        }
    }
    printf("EncodeCodepoint: %s over 1.5M inputs\n", gFailures == before ? "match" : "MISMATCH");
}

static void test_demangle() {
    int before = gFailures;
    std::mt19937 rng(2);
    for (int n = 0; n < 200000; n++) {
        u32 in[2] = {(u32)rng(), (u32)rng()};
        if (n & 1) in[0] = in[0] % (40u * 40 * 40 * 40 * 40 * 40);  // keep some words "full"
        char host[16];
        memset(host, 0x55, sizeof(host));
        Demangle_FName(in, host);
        ST32(kBuf, in[0]);
        ST32(kBuf + 4, in[1]);
        memset(MA(kBuf2), 0x55, 16);
        gCpu.r[3] = kBuf;
        gCpu.r[4] = kBuf2;
        call(kDemangleFName);
        check(memcmp(MA(kBuf2), host, 14) == 0, "Demangle_FName", "%08X %08X", (unsigned)in[0], (unsigned)in[1]);
    }
    printf("Demangle_FName: %s over 200K inputs\n", gFailures == before ? "match" : "MISMATCH");
}

static void test_strings() {
    int before = gFailures;
    const char* words[] = {"", "a", "PokePark", "Pikachu", "Pikachu!", "pikachu", "Wii", "WiiWare", "Treehouse"};
    for (const char* a : words) {
        put_str(kBuf, a);
        gCpu.r[3] = kBuf;
        call(kStrlen);
        check(gCpu.r[3] == strlen(a), "strlen", "\"%s\"", a);
        for (const char* b : words) {
            put_str(kBuf, a);
            put_str(kBuf2, b);
            gCpu.r[3] = kBuf;
            gCpu.r[4] = kBuf2;
            call(kStrcmp);
            int got = (int32_t)gCpu.r[3], want = strcmp(a, b);
            check((got > 0) == (want > 0) && (got < 0) == (want < 0), "strcmp", "\"%s\" \"%s\"", a, b);
        }
    }
    const char* nums[] = {"0", "42", "-17", "  123abc", "2147483647", "-2147483648"};
    for (const char* s : nums) {
        put_str(kBuf, s);
        gCpu.r[3] = kBuf;
        call(kAtoi);
        check((int32_t)gCpu.r[3] == atoi(s), "atoi", "\"%s\" got %d", s, (int32_t)gCpu.r[3]);
    }
    printf("strlen/strcmp/atoi: %s\n", gFailures == before ? "match" : "MISMATCH");
}

static void test_sprintf() {
    int before = gFailures;
    struct Case {
        const char* fmt;
        int a, b;
        const char* s;
    } cases[] = {
        {"%d", 12345, 0, ""},
        {"%d %d", -7, 2147483647, ""},
        {"%08X|%x", 0xBEEF, 0x80001234, ""},
        {"[%5d] [%-5d]", 42, 42, ""},
        {"%s and %d", 3, 0, ""},
        {"%c%c", 'P', 'K', ""},
        {"%u %o", 4000000000u, 8, ""},
    };
    for (const Case& k : cases) {
        char want[256];
        put_str(kBuf2, k.fmt);
        put_str(kBuf2 + 0x200, "Pikachu");
        gCpu.r[3] = kBuf;
        gCpu.r[4] = kBuf2;
        if (strstr(k.fmt, "%s")) {
            snprintf(want, sizeof(want), k.fmt, "Pikachu", k.a);
            gCpu.r[5] = kBuf2 + 0x200;
            gCpu.r[6] = (uint32_t)k.a;
        } else {
            snprintf(want, sizeof(want), k.fmt, k.a, k.b);
            gCpu.r[5] = (uint32_t)k.a;
            gCpu.r[6] = (uint32_t)k.b;
        }
        cr_setbit(&gCpu, 6, 0);  // no FP arguments in registers
        call(kSprintf);
        check(strcmp((char*)MA(kBuf), want) == 0, "sprintf", "\"%s\": got \"%s\" want \"%s\"", k.fmt,
              (char*)MA(kBuf), want);
    }
    // Floating point goes through f1 with CR bit 6 set, as MWCC passes it.
    double vals[] = {0.0, 1.5, -2.25, 3.14159265358979, 1234567.875, 1e-3};
    for (double v : vals) {
        char want[64];
        snprintf(want, sizeof(want), "%.3f", v);
        put_str(kBuf2, "%.3f");
        gCpu.r[3] = kBuf;
        gCpu.r[4] = kBuf2;
        gCpu.f[1] = v;
        cr_setbit(&gCpu, 6, 1);
        call(kSprintf);
        check(strcmp((char*)MA(kBuf), want) == 0, "sprintf %f", "%g: got \"%s\" want \"%s\"", v,
              (char*)MA(kBuf), want);
    }
    printf("sprintf: %s\n", gFailures == before ? "match" : "MISMATCH");
}

// recomp/patches/example_hook.c wraps the game's strlen. sprintf's "%s"
// calls it directly, so a working hook sees that call.
extern "C" unsigned g_strlen_calls;

static void test_hook() {
    unsigned before = g_strlen_calls;
    put_str(kBuf2, "[%s]");
    put_str(kBuf2 + 0x200, "Pikachu");
    gCpu.r[3] = kBuf;
    gCpu.r[4] = kBuf2;
    gCpu.r[5] = kBuf2 + 0x200;
    cr_setbit(&gCpu, 6, 0);
    call(kSprintf);
    bool ok = g_strlen_calls > before && strcmp((char*)MA(kBuf), "[Pikachu]") == 0;
    check(ok, "hook", "strlen hook saw %u calls, output \"%s\"", g_strlen_calls - before, (char*)MA(kBuf));
    printf("hook: %s (game-internal call to strlen reached the patch)\n", ok ? "works" : "FAILED");
}

static void test_math() {
    struct Fn {
        const char* name;
        uint32_t addr;
        double (*host)(double);
    } fns[] = {
        {"sin", kSin, [](double x) { return std::sin(x); }},
        {"cos", kCos, [](double x) { return std::cos(x); }},
        {"tan", kTan, [](double x) { return std::tan(x); }},
        {"atan", kAtan, [](double x) { return std::atan(x); }},
        {"sqrt", kSqrt, [](double x) { return std::sqrt(std::fabs(x)); }},
    };
    std::mt19937_64 rng(3);
    std::uniform_real_distribution<double> dist(-100.0, 100.0);
    for (const Fn& f : fns) {
        double worst = 0;
        for (int n = 0; n < 100000; n++) {
            double x = dist(rng);
            if (f.addr == kSqrt) x = std::fabs(x);
            gCpu.f[1] = x;
            call(f.addr);
            double u = ulps(gCpu.f[1], f.host(x));
            if (u > 1 && worst <= 1) fprintf(stderr, "  %s(%.17g) = %.17g, host %.17g\n", f.name, x, gCpu.f[1], f.host(x));
            worst = std::fmax(worst, u);
        }
        // fdlibm and the host library may round the last bit differently.
        check(worst <= 1, f.name, "worst error %.0f ulp", worst);
        printf("%s: worst difference from host libm %.0f ulp over 100K inputs\n", f.name, worst);
    }
    double worst = 0;
    for (int n = 0; n < 100000; n++) {
        double x = std::fabs(dist(rng)) / 10, y = dist(rng) / 10;
        gCpu.f[1] = x;
        gCpu.f[2] = y;
        call(kPow);
        worst = std::fmax(worst, ulps(gCpu.f[1], std::pow(x, y)));
        gCpu.f[1] = y;
        gCpu.f[2] = x;
        call(kAtan2);
        worst = std::fmax(worst, ulps(gCpu.f[1], std::atan2(y, x)));
        gCpu.f[1] = y * 10;
        gCpu.f[2] = x + 0.5;
        call(kFmod);
        worst = std::fmax(worst, ulps(gCpu.f[1], std::fmod(y * 10, x + 0.5)));
    }
    check(worst <= 1, "pow/atan2/fmod", "worst error %.0f ulp", worst);
    printf("pow/atan2/fmod: worst difference from host libm %.0f ulp over 300K inputs\n", worst);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <main.dol>\n", argv[0]);
        return 2;
    }
    if (ppc_mem_init() != 0 || ppc_load_dol(argv[1], NULL) != 0) {
        fprintf(stderr, "could not load %s\n", argv[1]);
        return 2;
    }
    // Let the game's own startup code set r1, r2 and r13.
    call(kInitRegisters);
    gReset = gCpu;
    gReset.r[1] = 0x80F00000;  // a stack clear of the arena used for test buffers
    printf("recompiled functions: %u; r2=%08X r13=%08X\n", ppc_func_count, gCpu.r[2], gCpu.r[13]);

    test_unicode_to_sjis();
    test_encode_codepoint();
    test_demangle();
    test_strings();
    test_sprintf();
    test_hook();
    test_math();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
