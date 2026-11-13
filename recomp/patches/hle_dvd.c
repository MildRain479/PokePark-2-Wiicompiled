/* The disc, served from files extracted from the user's own copy (see
 * recomp/README.md). The game's DVD library is left as is: it reads the
 * data partition through /dev/di (hle_ios.c), and those reads are mapped
 * back to files through the disc's file table (FST). On Android the disc
 * can instead be read straight from the user's disc image (disc_image.c),
 * whose data partition is read at the same offsets. */
#include <stdlib.h>
#include <string.h>

#include "hle.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION Mutex;
#define mutex_init(m) InitializeCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
typedef CONDITION_VARIABLE Cond;
#define cond_init(c) InitializeConditionVariable(c)
#define cond_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define cond_signal(c) WakeConditionVariable(c)
#else
#include <pthread.h>
typedef pthread_mutex_t Mutex;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
typedef pthread_cond_t Cond;
#define cond_init(c) pthread_cond_init(c, NULL)
#define cond_wait(c, m) pthread_cond_wait(c, m)
#define cond_signal(c) pthread_cond_signal(c)
#endif

#ifndef _WIN32
#define _strdup strdup
#define _fseeki64 fseeko
#endif

typedef struct Region {
    uint64_t offset, size; /* in the data partition */
    char* path;            /* host file */
} Region;

static char sRoot[512];
static Region* sRegions;
static int sRegionCount, sRegionCap;
static uint8_t sBootBin[0x440];
static uint8_t* sFst;
static uint32_t sFstSize;
/* A disc image's partition, read at an offset, and its TMD. */
static int (*sImageRead)(uint64_t offset, void* out, uint32_t len);
static uint8_t* sTmd;
static uint32_t sTmdSize;

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void add_region(uint64_t offset, uint64_t size, const char* path) {
    if (sRegionCount == sRegionCap) {
        sRegionCap = sRegionCap ? sRegionCap * 2 : 512;
        sRegions = (Region*)realloc(sRegions, sRegionCap * sizeof(Region));
    }
    sRegions[sRegionCount].offset = offset;
    sRegions[sRegionCount].size = size;
    sRegions[sRegionCount].path = _strdup(path);
    sRegionCount++;
}

static uint8_t* read_file(const char* path, uint32_t* size) {
    FILE* f = fopen(path, "rb");
    uint8_t* data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t*)malloc(n ? n : 1);
    if (fread(data, 1, n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (size) *size = (uint32_t)n;
    return data;
}

/* Walks the FST, adding a region for every file. Wii FSTs store file
 * offsets divided by 4. */
static void add_fst_files(void) {
    uint32_t count = be32(sFst + 8);
    const char* names = (const char*)sFst + count * 12;
    uint32_t dir_end[64];
    char dir_path[64][256];
    int depth = 0;
    uint32_t i;
    strcpy(dir_path[0], "files");
    dir_end[0] = count;
    for (i = 1; i < count; i++) {
        const uint8_t* e = sFst + i * 12;
        const char* name = names + (be32(e) & 0xFFFFFF);
        char path[512];
        while (depth > 0 && i >= dir_end[depth]) depth--;
        if (e[0]) {
            if (depth + 1 < 64) {
                depth++;
                snprintf(dir_path[depth], sizeof(dir_path[depth]), "%s/%s", dir_path[depth - 1], name);
                dir_end[depth] = be32(e + 8);
            }
        } else {
            snprintf(path, sizeof(path), "%s/%s/%s", sRoot, dir_path[depth], name);
            add_region((uint64_t)be32(e + 4) << 2, be32(e + 8), path);
        }
    }
}

int hle_dvd_init(const char* root) {
    uint8_t* boot;
    char path[600];
    uint32_t size;
    strncpy(sRoot, root, sizeof(sRoot) - 1);
    snprintf(path, sizeof(path), "%s/sys/boot.bin", sRoot);
    boot = read_file(path, &size);
    if (!boot || size < sizeof(sBootBin)) {
        HLE_WARN("no disc at %s (extract it with dtk; see recomp/README.md)", sRoot);
        return -1;
    }
    memcpy(sBootBin, boot, sizeof(sBootBin));
    free(boot);
    snprintf(path, sizeof(path), "%s/sys/fst.bin", sRoot);
    sFst = read_file(path, &sFstSize);
    if (!sFst) return -1;
    /* The system area, then every file. */
    snprintf(path, sizeof(path), "%s/sys/boot.bin", sRoot);
    add_region(0, 0x440, path);
    snprintf(path, sizeof(path), "%s/sys/bi2.bin", sRoot);
    add_region(0x440, 0x2000, path);
    snprintf(path, sizeof(path), "%s/sys/apploader.img", sRoot);
    add_region(0x2440, 0x3B000, path);
    snprintf(path, sizeof(path), "%s/sys/fst.bin", sRoot);
    add_region((uint64_t)be32(sBootBin + 0x424) << 2, sFstSize, path);
    add_fst_files();
    HLE_LOG("disc: %d files from %s", sRegionCount, sRoot);
    return 0;
}

int hle_dvd_init_image(const uint8_t* boot, const uint8_t* fst, uint32_t fstSize, const uint8_t* tmd,
                       uint32_t tmdSize, int (*read)(uint64_t offset, void* out, uint32_t len)) {
    memcpy(sBootBin, boot, sizeof(sBootBin));
    sFst = (uint8_t*)malloc(fstSize);
    memcpy(sFst, fst, fstSize);
    sFstSize = fstSize;
    if (tmd && tmdSize) {
        sTmd = (uint8_t*)malloc(tmdSize);
        memcpy(sTmd, tmd, tmdSize);
        sTmdSize = tmdSize;
    }
    sImageRead = read;
    HLE_LOG("disc: from a disc image, %u files", be32(sFst + 8));
    return 0;
}

/* What the apploader leaves in memory: the disc header at 0x80000000 and
 * the FST at the top of MEM1, just below the arena. */
void hle_dvd_boot_setup(void) {
    uint32_t arena_hi = LD32(0x80000034);
    uint32_t fst_addr = (arena_hi - sFstSize) & ~31u;
    if (!sFst) return;
    memcpy(MA(0x80000000), sBootBin, 0x20);
    memcpy(MA(fst_addr), sFst, sFstSize);
    ST32(0x80000034, fst_addr); /* arena hi */
    ST32(0x80000038, fst_addr); /* FST */
    ST32(0x8000003C, sFstSize); /* max FST size */
    ST32(0x80003110, fst_addr); /* MEM1 arena end */
}

/* Files kept open between reads: the game reads the same archives again
 * and again, and opening a file on Windows (with the virus scanner's look
 * at it) was most of a 490 ms stall as an area loaded. */
#define OPEN_FILES 32
static struct {
    int region; /* -1: none */
    FILE* f;
} sOpen[OPEN_FILES];
static unsigned sOpenNext;

static FILE* open_region(int i) {
    int k;
    for (k = 0; k < OPEN_FILES; k++) {
        if (sOpen[k].f && sOpen[k].region == i) return sOpen[k].f;
    }
    k = (int)(sOpenNext++ % OPEN_FILES);
    if (sOpen[k].f) fclose(sOpen[k].f);
    sOpen[k].f = fopen(sRegions[i].path, "rb");
    sOpen[k].region = i;
    return sOpen[k].f;
}

/* Copies len bytes at a data-partition offset to guest memory. */
static int read_now(uint64_t offset, uint32_t len, uint32_t dst);

/* Reads, one at a time (the open files and a disc image's reader are
 * shared): the game's own, made as it waits, and the asynchronous ones,
 * made on a thread of their own. */
static Mutex sReadLock;
static int sReadLockMade; /* (made on the game's thread, before the reader starts) */

static void read_lock(void) {
    if (!sReadLockMade) {
        mutex_init(&sReadLock);
        sReadLockMade = 1;
    }
    mutex_lock(&sReadLock);
}

int hle_dvd_read(uint64_t offset, uint32_t len, uint32_t dst) {
    int r;
    read_lock();
    r = read_now(offset, len, dst);
    mutex_unlock(&sReadLock);
    return r;
}

/* Reads the game asked for with a callback. On the Wii the drive reads
 * while the game goes on (drawing, playing its sound), the callback
 * coming when the data is in. Made as the request came, a read the file
 * cache didn't have stopped the game for as long as the disk took: a
 * first visit to a town stalled it for up to half a second, several
 * times. Here a thread reads them in order, and hle_dvd_poll (on the
 * game's thread, at each delivery point) calls each one's done once it
 * is in. RECOMP_DVD_SYNC=1 makes them as they are asked for instead. */
#define JOBS 16
typedef struct {
    uint64_t offset;
    uint32_t len, dst, a, b;
    void (*done)(int ok, uint32_t a, uint32_t b);
    int ok;
} Job;
static Job sJobs[JOBS];
static unsigned sJobHead, sJobRead, sJobTail; /* done up to head, read up to read, queued up to tail */
static Cond sJobQueued;
static volatile long sJobsRead; /* sJobRead, for hle_dvd_poll to look at without the lock */

#ifdef _WIN32
static DWORD WINAPI reader(LPVOID arg)
#else
static void* reader(void* arg)
#endif
{
    (void)arg;
    read_lock();
    for (;;) {
        Job* j;
        while (sJobRead == sJobTail) cond_wait(&sJobQueued, &sReadLock);
        j = &sJobs[sJobRead % JOBS];
        j->ok = read_now(j->offset, j->len, j->dst);
        sJobRead++;
#ifdef _WIN32
        InterlockedExchange(&sJobsRead, (long)sJobRead);
#else
        __atomic_store_n(&sJobsRead, (long)sJobRead, __ATOMIC_RELEASE);
#endif
    }
    return 0;
}

int hle_dvd_read_async(uint64_t offset, uint32_t len, uint32_t dst, void (*done)(int ok, uint32_t a, uint32_t b),
                       uint32_t a, uint32_t b) {
    static int started, sync = -1;
    Job* j;
    if (sync < 0) sync = getenv("RECOMP_DVD_SYNC") && atoi(getenv("RECOMP_DVD_SYNC"));
    if (sync) return 0;
    read_lock();
    if (!started) {
        cond_init(&sJobQueued);
#ifdef _WIN32
        {
            HANDLE t = CreateThread(NULL, 0, reader, NULL, 0, NULL);
            if (t) CloseHandle(t);
            started = t ? 1 : -1;
        }
#else
        {
            pthread_t t;
            started = pthread_create(&t, NULL, reader, NULL) == 0 ? 1 : -1;
            if (started > 0) pthread_detach(t);
        }
#endif
    }
    if (started < 0 || sJobTail - sJobHead >= JOBS) {
        mutex_unlock(&sReadLock);
        return 0;
    }
    j = &sJobs[sJobTail % JOBS];
    j->offset = offset;
    j->len = len;
    j->dst = dst;
    j->done = done;
    j->a = a;
    j->b = b;
    sJobTail++;
    cond_signal(&sJobQueued);
    mutex_unlock(&sReadLock);
    return 1;
}

void hle_dvd_poll(void) {
#ifdef _WIN32
    unsigned read = (unsigned)InterlockedCompareExchange(&sJobsRead, 0, 0);
#else
    unsigned read = (unsigned)__atomic_load_n(&sJobsRead, __ATOMIC_ACQUIRE);
#endif
    while (sJobHead != read) {
        Job j = sJobs[sJobHead % JOBS];
        sJobHead++;
        j.done(j.ok, j.a, j.b);
    }
}

static int read_now(uint64_t offset, uint32_t len, uint32_t dst) {
    uint64_t end = offset + len;
    int i, ok = 1;
    ppc_mem_host_write(dst, len);
    if (sImageRead) {
        HLE_LOG("disc read %08llX+%X", (unsigned long long)offset, (unsigned)len);
        if (sImageRead(offset, MA(dst), len)) return 1;
        memset(MA(dst), 0, len);
        HLE_WARN("disc read %08llX+%X failed", (unsigned long long)offset, (unsigned)len);
        return 0;
    }
    memset(MA(dst), 0, len);
    for (i = 0; i < sRegionCount; i++) {
        const Region* r = &sRegions[i];
        uint64_t lo = offset > r->offset ? offset : r->offset;
        uint64_t hi = end < r->offset + r->size ? end : r->offset + r->size;
        FILE* f;
        if (lo >= hi) continue;
        f = open_region(i);
        if (!f) {
            HLE_WARN("disc read needs %s, which is missing", r->path);
            ok = 0;
            continue;
        }
        _fseeki64(f, (int64_t)(lo - r->offset), SEEK_SET);
        if (fread(MA(dst + (uint32_t)(lo - offset)), 1, (size_t)(hi - lo), f) != hi - lo) ok = 0;
        HLE_LOG("disc read %08llX+%X: %s", (unsigned long long)lo, (unsigned)(hi - lo), r->path + strlen(sRoot) + 1);
    }
    return ok;
}

/* A disc file by its path under files/ ("Archive/MenuField.dac"), read
 * whole into memory of the host's (free it); NULL if it isn't there. Not
 * thread-safe with the game's reads: call it from the game's thread. */
uint8_t* hle_dvd_load(const char* path, uint32_t* size) {
    uint32_t count, i, end, offset = 0, len = 0;
    const char *names, *p = path;
    uint8_t* data;
    char host[1024];
    if (!sFst) return NULL;
    count = be32(sFst + 8);
    names = (const char*)sFst + count * 12;
    /* Down the tree a name at a time: a directory's entries run to its end. */
    for (i = 1, end = count; i < end && *p;) {
        const uint8_t* e = sFst + i * 12;
        const char* name = names + (be32(e) & 0xFFFFFF);
        size_t n = strcspn(p, "/");
        if (strlen(name) == n && !strncmp(name, p, n)) {
            if (!e[0]) {
                if (p[n]) return NULL;
                offset = be32(e + 4);
                len = be32(e + 8);
                break;
            }
            end = be32(e + 8);
            p += n + (p[n] == '/');
            i++;
        } else {
            i = e[0] ? be32(e + 8) : i + 1; /* past this directory, or on */
        }
    }
    if (!len) return NULL;
    if (!sImageRead) {
        snprintf(host, sizeof(host), "%s/files/%s", sRoot, path);
        data = read_file(host, size);
        return data && *size == len ? data : (free(data), (uint8_t*)NULL);
    }
    data = (uint8_t*)malloc(len);
    if (data && !sImageRead((uint64_t)offset << 2, data, len)) {
        free(data);
        return NULL;
    }
    *size = len;
    return data;
}

/* The partition's TMD, which DVDLowOpenPartition returns. */
uint32_t hle_dvd_tmd(uint32_t dst, uint32_t max) {
    char path[600];
    uint32_t size;
    uint8_t* tmd;
    if (sTmd) {
        size = sTmdSize < max ? sTmdSize : max;
        memcpy(MA(dst), sTmd, size);
        return size;
    }
    snprintf(path, sizeof(path), "%s/tmd.bin", sRoot);
    tmd = read_file(path, &size);
    if (!tmd) return 0;
    if (size > max) size = max;
    memcpy(MA(dst), tmd, size);
    free(tmd);
    return size;
}

const uint8_t* hle_dvd_disc_id(void) { return sBootBin; }

/* BOOL DVDReadAsyncPrio(DVDFileInfo* file, void* addr, s32 length, s32 offset,
 *                       DVDCallback callback, s32 prio): logged only. */
void F_DVDReadAsyncPrio(ppc_t* c) {
    if (hle_log_enabled()) {
        HLE_LOG("DVDReadAsyncPrio(file %08X, addr %08X, len %X, offset %X)", c->r[3], c->r[4], c->r[5], c->r[6]);
        if (hle_log_enabled() > 1) ppc_backtrace(c);
    }
    F_DVDReadAsyncPrio_orig(c);
}
