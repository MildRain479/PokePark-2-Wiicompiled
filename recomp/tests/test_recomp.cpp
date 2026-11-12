// Checks recompiled PokePark 2 functions against the host C library, for
// the game's copies of the MSL C library, and the disc checks.
//
// Usage: test_recomp <path to main.dol>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "recomp_funcs.h"
#include "../boot/disc_verify.h"
extern "C" {
#include "host_sdcard.h"
}

// Guest functions under test (config/S2LE01/symbols.txt).
static const uint32_t kStrlen = 0x803FE8B8;
static const uint32_t kSprintf = 0x804058F8;
static const uint32_t kInitRegisters = 0x80004210;

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

static void test_strings() {
    int before = gFailures;
    const char* words[] = {"", "a", "PokePark", "Pikachu", "Pikachu!", "pikachu", "Wii", "WiiWare", "Treehouse"};
    for (const char* a : words) {
        put_str(kBuf, a);
        gCpu.r[3] = kBuf;
        call(kStrlen);
        check(gCpu.r[3] == strlen(a), "strlen", "\"%s\"", a);
    }
    printf("strlen: %s\n", gFailures == before ? "match" : "MISMATCH");
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

// The disc checks, for the game built (disc_verify.h's hashes).
static void test_disc() {
    char msg[512], sha1[41];
    const char* good = "Format: RVZ\nLossless: true\n\nTitle: " GAME_NAME "\nGame ID: " GAME_ID "\nDisc 1, Revision 0\n\n"
                       "Partition 0\n\tGame ID: RELSAB (0001000000555045)\n\tDisc 1, Revision 3\n";
    const char* verified = "Redump: " GAME_NAME " (USA) \xe2\x9c\x85\nCRC32 : " DISC_REDUMP_CRC32 " \xe2\x9c\x85\n"
                           "SHA-1 : " DISC_REDUMP_SHA1 " \xe2\x9c\x85\n";
    const char* other = "CRC32 : 11111111\nSHA-1 : 0123456789abcdef0123456789abcdef01234567\n";
    std::string sameSha = std::string("CRC32 : 11111111\nSHA-1 : ") + DISC_REDUMP_SHA1 + "\n";
    std::string europe = std::string(GAME_ID, 3) + "P01", japan = std::string(GAME_ID, 3) + "J01";
    const char* dol = "abc";
    static uint8_t zeros[120];
    check(disc_check_info(good, msg, sizeof(msg)) == DISC_OK, "disc", "the good disc refused: %s", msg);
    check(disc_check_info("nonsense", msg, sizeof(msg)) == DISC_UNREADABLE, "disc", "not a disc");
    check(disc_check_info(NULL, msg, sizeof(msg)) == DISC_UNREADABLE, "disc", "no output");
    check(disc_check_id("R8AE01", 0, msg, sizeof(msg)) == DISC_WRONG_GAME,
          "disc", "the other PokePark");
    check(disc_check_id(europe.c_str(), 0, msg, sizeof(msg)) == DISC_WRONG_REGION, "disc", "Europe");
    check(disc_check_id(japan.c_str(), 0, msg, sizeof(msg)) == DISC_WRONG_REGION, "disc", "Japan");
    check(disc_check_id(GAME_ID, 1, msg, sizeof(msg)) == DISC_WRONG_REVISION, "disc", "revision 1");
    check(disc_is_original(verified), "disc", "the original's hashes");
    check(disc_is_original(sameSha.c_str()) == 0, "disc", "another CRC");
    check(disc_is_original(other) == 0, "disc", "other hashes");
    check(disc_is_original("") == 0, "disc", "no hashes");
    check(disc_is_original(NULL) == 0, "disc", "no output");
    disc_sha1((const uint8_t*)dol, 3, sha1);
    check(!strcmp(sha1, "a9993e364706816aba3e25717850c26c9cd0d89d"), "disc", "sha1(abc) = %s", sha1);
    disc_sha1((const uint8_t*)dol, 0, sha1);
    check(!strcmp(sha1, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "disc", "sha1() = %s", sha1);
    // 120 zeros: padding that needs a block of its own.
    disc_sha1(zeros, sizeof(zeros), sha1);
    check(!strcmp(sha1, "b110a88a11436b215220486c1081dec2fb0f389a"), "disc", "sha1(120 zeros) = %s", sha1);
    check(disc_check_dol(zeros, sizeof(zeros), msg, sizeof(msg)) == DISC_WRONG_CODE, "disc", "another main.dol");
}

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

// The SD card (hle_sd.c), through FA's calls as the game's SD manager
// makes them to copy a photo: mount, the free space, the scan for the
// next number, the folder made, the file opened "r+" (missing), made,
// closed and opened again, written and closed; then the scan again.
static std::string gSaved;
static int sd_choose() { return 1; }
static int sd_save(const char* name, const uint8_t* data, uint32_t size) {
    gSaved = std::string(name) + ":" + std::string((const char*)data, size);
    return 1;
}

static int32_t fa(uint32_t addr, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    gCpu.r[3] = a;
    gCpu.r[4] = b;
    gCpu.r[5] = c;
    gCpu.r[6] = d;
    call(addr);
    return (int32_t)gCpu.r[3];
}

static void test_sd() {
    enum : uint32_t {
        kAttach = 0x80373AF0, kMount = 0x80374160, kDevinf = 0x80373FF0, kErrnum = 0x80373E50,
        kOpendir = 0x80374190, kMkdir = 0x80374130, kFopen = 0x80373EF0, kCreate = 0x80373D20,
        kFclose = 0x80373E90, kFsize = 0x80373EC0, kFseek = 0x80373F20, kFwrite = 0x80373FE0,
        kFsfirst = 0x80373F50, kFsnext = 0x80373F80, kFstat = 0x80373FB0
    };
    const uint32_t path = kBuf, mode = kBuf + 0x100, info = kBuf + 0x200, data = kBuf + 0x300, ent = kBuf2;
    g_host_sd_choose = sd_choose;
    g_host_sd_save = sd_save;
    memset(MA(info), 0, 0x40);
    fa(kAttach, 0, 0, 0, info);
    check(*(uint8_t*)MA(info + 8) == 'A' && (*(uint8_t*)MA(info + 9) & 0x10), "SD", "no card in drive A");
    check(fa(kMount, 'A') == 0, "SD", "mount");
    fa(kDevinf, 'A', info);
    check((int32_t)(LD32(info + 4) * LD32(info + 8) * LD32(info + 12)) > 100 * 1024 * 1024, "SD", "free space");
    put_str(path, "\\DCIM\\???NIN01\\S2L_????.JPG");
    check(fa(kFsfirst, path, 0x60, ent) != 0 && fa(kErrnum) == 2, "SD", "an empty card's scan");
    put_str(path, "\\DCIM\\100NIN01");
    check(fa(kOpendir, path) == 0, "SD", "a folder not made yet");
    put_str(path, "\\DCIM");
    check(fa(kMkdir, path) == 0, "SD", "mkdir DCIM");
    put_str(path, "\\DCIM\\100NIN01");
    check(fa(kMkdir, path) == 0 && fa(kOpendir, path) != 0, "SD", "mkdir 100NIN01");
    put_str(path, "\\DCIM\\100NIN01\\S2L_0001.JPG");
    put_str(mode, "r+");
    check(fa(kFopen, path, mode) == 0, "SD", "r+ of a missing file");
    int32_t h = fa(kCreate, path, 0);
    check(h != 0 && fa(kFclose, (uint32_t)h) == 0, "SD", "create");
    h = fa(kFopen, path, mode);
    check(h != 0 && fa(kFsize, (uint32_t)h, info) == 0 && LD32(info) == 0, "SD", "reopened, empty");
    put_str(data, "JPEGDATA");
    check(fa(kFseek, (uint32_t)h, 0, 0) == 0 && fa(kFwrite, data, 1, 8, (uint32_t)h) == 8, "SD", "write");
    check(fa(kFclose, (uint32_t)h) == 0 && gSaved == "S2L_0001.JPG:JPEGDATA", "SD", "saved \"%s\"", gSaved.c_str());
    put_str(path, "\\DCIM\\???NIN01");
    check(fa(kFsfirst, path, 0x10, ent) == 0 && !strcmp((char*)MA(ent + 0x22D), "100NIN01"), "SD", "folder scan");
    put_str(path, "\\DCIM\\100NIN01\\S2L_????.JPG");
    check(fa(kFsfirst, path, 0x60, ent) == 0 && !strcmp((char*)MA(ent + 0x22D), "S2L_0001.JPG") &&
              fa(kFsnext, ent) != 0 && fa(kErrnum) == 2,
          "SD", "file scan");
    put_str(path, "\\DCIM\\100NIN01\\S2L_0001.JPG");
    check(fa(kFstat, path, info) == 0 && LD32(info) == 8 && *(uint8_t*)MA(info + 0x10) == 0x20, "SD", "stat");
    printf("SD card: a photo copied as the game's SD manager does it\n");
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

    test_strings();
    test_sprintf();
    test_disc();
    test_hook();
    test_sd();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
