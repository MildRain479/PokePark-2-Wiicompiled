/* Making a language pack (langpack.h) in the game, as
 * tools/recomp/langpack.py does on a PC: the same steps, in C.
 *
 * The USA program already knows Europe's languages (its table of file
 * endings has _Jp, _Us, _En, _Sp, _It, _Fr and _Ge), and the European disc
 * is the same game with each language's files beside the English ones. A
 * pack is that disc's files of one language, under the USA names:
 *
 *   text/files: Message/EU_<language>/* as Message/US_English/*, and
 *   Archive/<name>_<ending>.dac and Hbm/Hbm_<ending>.dac as <name>.dac.
 *
 *   voices/files: the sound banks with voices in them (Sound/<name>.acb and
 *   _streamfiles.awb) and the movies with them (Movie/<name>.mo), the
 *   European disc's French and German ones (_Fr, _Ge), and the Japanese
 *   disc's own.
 *
 * The Japanese disc's text is kept elsewhere than the USA's, so its pack's
 * text is the USA files with the Japanese messages put in them (jp_text).
 *
 * Written to <pack>.new, then put in the pack's place: at once if the game
 * isn't using the pack, or else as the game next starts
 * (langpack_promote). */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_settings.h"
#include "langpack.h"
#include "tr.h"

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* ------------------------------------------------------------------------ */
/* Discs */

typedef struct {
    char path[256];
    uint64_t offset;
    uint32_t size;
} FstFile;

/* Every file of a disc's FST (count in *count). */
static FstFile* fst_files(const LangDisc* d, int* count) {
    const uint8_t* f = d->fst;
    uint32_t n, strings, i, ends[32];
    char names[32][128];
    int depth = 0, cap = 512;
    FstFile* out;
    *count = 0;
    if (!f || d->fst_size < 12) return NULL;
    n = be32(f + 8);
    strings = n * 12;
    if (strings > d->fst_size) return NULL;
    out = (FstFile*)malloc(sizeof(FstFile) * cap);
    for (i = 1; out && i < n; i++) {
        const uint8_t* e = f + i * 12;
        uint32_t name = strings + (be32(e) & 0xFFFFFF);
        const char* nm = name < d->fst_size ? (const char*)f + name : "";
        while (depth && i >= ends[depth - 1]) depth--;
        if (f[i * 12]) {
            if (depth < 32) {
                ends[depth] = be32(e + 8);
                snprintf(names[depth], sizeof(names[depth]), "%s", nm);
                depth++;
            }
        } else {
            FstFile* x;
            size_t at = 0;
            int k;
            if (*count == cap) {
                FstFile* more = (FstFile*)realloc(out, sizeof(FstFile) * (cap *= 2));
                if (!more) break;
                out = more;
            }
            x = &out[(*count)++];
            for (k = 0; k < depth; k++) {
                at += snprintf(x->path + at, sizeof(x->path) - at, "%s/", names[k]);
                if (at >= sizeof(x->path)) at = sizeof(x->path) - 1;
            }
            snprintf(x->path + at, sizeof(x->path) - at, "%s", nm);
            x->offset = (uint64_t)be32(e + 4) << 2;
            x->size = be32(e + 8);
        }
    }
    return out;
}

static const FstFile* find_file(const FstFile* f, int n, const char* path) {
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(f[i].path, path)) return &f[i];
    return NULL;
}

typedef struct {
    uint8_t* fst;
    char root[1024];
} Folder;

static int folder_read(void* user, const char* path, uint64_t offset, uint32_t pos, void* out, uint32_t size) {
    Folder* f = (Folder*)user;
    char full[1400];
    FILE* fp;
    int ok;
    (void)offset;
    snprintf(full, sizeof(full), "%s/files/%s", f->root, path);
    if (!(fp = fopen(full, "rb"))) return 0;
    ok = fseek(fp, (long)pos, SEEK_SET) == 0 && fread(out, 1, size, fp) == size;
    fclose(fp);
    return ok;
}

int langdisc_folder(LangDisc* d, const char* root) {
    Folder* f = (Folder*)calloc(1, sizeof(Folder));
    char path[1100];
    FILE* fp;
    long n;
    memset(d, 0, sizeof(*d));
    if (!f) return 0;
    snprintf(f->root, sizeof(f->root), "%s", root);
    snprintf(path, sizeof(path), "%s/sys/fst.bin", root);
    if (!(fp = fopen(path, "rb"))) {
        free(f);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    f->fst = (uint8_t*)malloc(n > 0 ? n : 1);
    if (n <= 0 || !f->fst || fread(f->fst, 1, n, fp) != (size_t)n) {
        fclose(fp);
        free(f->fst);
        free(f);
        return 0;
    }
    fclose(fp);
    d->fst = f->fst;
    d->fst_size = (uint32_t)n;
    d->read = folder_read;
    d->user = f;
    return 1;
}

void langdisc_free(LangDisc* d) {
    if (d->read == folder_read && d->user) {
        free(((Folder*)d->user)->fst);
        free(d->user);
    }
    memset(d, 0, sizeof(*d));
}

/* The languages a disc's packs are (LANGUAGE_*), into out (4 at most);
 * how many. */
static int languages_of(const char* id, int* out) {
    if (!strncmp(id, "S2LJ01", 6)) {
        out[0] = LANGUAGE_JP;
        return 1;
    }
    if (!strncmp(id, "S2LP01", 6)) {
        out[0] = LANGUAGE_FR;
        out[1] = LANGUAGE_DE;
        out[2] = LANGUAGE_IT;
        out[3] = LANGUAGE_ES;
        return 4;
    }
    return 0;
}

int langpack_language_of(const char* id) {
    int l[4];
    return languages_of(id, l) ? l[0] : -1;
}

/* ------------------------------------------------------------------------ */
/* Files */

#ifdef _WIN32
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static void make_dirs(const char* file) {
    char p[1400];
    char* s;
    snprintf(p, sizeof(p), "%s", file);
    for (s = p + 1; *s; s++) {
        if (*s == '/' || *s == '\\') {
            char c = *s;
            *s = '\0';
            MKDIR(p);
            *s = c;
        }
    }
}

static void remove_tree(const char* dir) {
#ifdef _WIN32
    char pattern[1400], full[1400];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(full);
            else DeleteFileA(full);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    char full[1400];
    if (d) {
        while ((e = readdir(d)) != NULL) {
            struct stat st;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(full);
            else unlink(full);
        }
        closedir(d);
    }
    rmdir(dir);
#endif
}

static int exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static int write_file(const char* path, const void* data, size_t n) {
    FILE* f;
    int ok;
    make_dirs(path);
    if (!(f = fopen(path, "wb"))) return 0;
    ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

/* ------------------------------------------------------------------------ */
/* Bytes */

typedef struct {
    uint8_t* p;
    size_t n, cap;
} Buf;

static int buf_reserve(Buf* b, size_t more) {
    size_t cap;
    uint8_t* p;
    if (b->n + more <= b->cap) return 1;
    cap = b->cap ? b->cap : 4096;
    while (cap < b->n + more) cap *= 2;
    p = (uint8_t*)realloc(b->p, cap);
    if (!p) return 0;
    b->p = p;
    b->cap = cap;
    return 1;
}

static int buf_add(Buf* b, const void* data, size_t n) {
    if (!buf_reserve(b, n)) return 0;
    if (data) memcpy(b->p + b->n, data, n);
    else memset(b->p + b->n, 0, n);
    b->n += n;
    return 1;
}

static void buf_pad(Buf* b, size_t to) {
    while (b->n % to) buf_add(b, NULL, 1);
}

static void buf_free(Buf* b) {
    free(b->p);
    memset(b, 0, sizeof(*b));
}

static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ------------------------------------------------------------------------ */
/* LZ11 (the disc's .dac archives are U8 in it) */

static int lz11_decompress(const uint8_t* src, size_t n, Buf* out) {
    size_t size, i = 4;
    if (n < 4 || src[0] != 0x11) return 0;
    size = src[1] | src[2] << 8 | (size_t)src[3] << 16;
    if (size == 0) {
        if (n < 8) return 0;
        size = src[4] | src[5] << 8 | (size_t)src[6] << 16 | (size_t)src[7] << 24;
        i = 8;
    }
    out->n = 0;
    if (!buf_reserve(out, size)) return 0;
    while (out->n < size) {
        int bit, flags;
        if (i >= n) return 0;
        flags = src[i++];
        for (bit = 0; bit < 8 && out->n < size; bit++) {
            size_t len, disp, k;
            if (!(flags & (0x80 >> bit))) {
                if (i >= n) return 0;
                out->p[out->n++] = src[i++];
                continue;
            }
            if (i + 1 >= n) return 0;
            switch (src[i] >> 4) {
            case 0:
                if (i + 2 >= n) return 0;
                len = ((src[i] & 15) << 4 | src[i + 1] >> 4) + 0x11;
                disp = ((src[i + 1] & 15) << 8 | src[i + 2]) + 1;
                i += 3;
                break;
            case 1:
                if (i + 3 >= n) return 0;
                len = ((size_t)(src[i] & 15) << 12 | src[i + 1] << 4 | src[i + 2] >> 4) + 0x111;
                disp = ((src[i + 2] & 15) << 8 | src[i + 3]) + 1;
                i += 4;
                break;
            default:
                len = (src[i] >> 4) + 1;
                disp = ((src[i] & 15) << 8 | src[i + 1]) + 1;
                i += 2;
            }
            if (disp > out->n || out->n + len > size) return 0;
            for (k = 0; k < len; k++, out->n++) out->p[out->n] = out->p[out->n - disp];
        }
    }
    return 1;
}

#define HASH_BITS 16
static uint32_t hash3(const uint8_t* p) { return ((uint32_t)(p[0] << 16 | p[1] << 8 | p[2]) * 2654435761u) >> (32 - HASH_BITS); }

/* Greedy LZ11 with hash chains over the 4 KB window (as langpack.py). */
static int lz11_compress(const uint8_t* data, size_t n, Buf* out) {
    int32_t* head = (int32_t*)malloc(sizeof(int32_t) << HASH_BITS);
    int32_t* prev = (int32_t*)malloc(sizeof(int32_t) * (n ? n : 1));
    size_t i = 0, flag_pos = 0, p;
    int bit = 8;
    uint8_t hdr[8] = {0x11, (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16)};
    if (!head || !prev) {
        free(head);
        free(prev);
        return 0;
    }
    memset(head, 0xFF, sizeof(int32_t) << HASH_BITS);
    out->n = 0;
    if (n >= 1u << 24) {
        hdr[1] = hdr[2] = hdr[3] = 0;
        hdr[4] = (uint8_t)n, hdr[5] = (uint8_t)(n >> 8), hdr[6] = (uint8_t)(n >> 16), hdr[7] = (uint8_t)(n >> 24);
        buf_add(out, hdr, 8);
    } else {
        buf_add(out, hdr, 4);
    }
#define INSERT(q)                            \
    do {                                     \
        if ((q) + 3 <= n) {                  \
            uint32_t h_ = hash3(data + (q)); \
            prev[q] = head[h_];              \
            head[h_] = (int32_t)(q);         \
        }                                    \
    } while (0)
    while (i < n) {
        size_t best_len = 0, best_disp = 0;
        if (bit == 8) {
            if (!buf_reserve(out, 1 + 8 * 4)) break;
            flag_pos = out->n;
            out->p[out->n++] = 0;
            bit = 0;
        }
        if (i + 3 <= n) {
            int32_t cand = head[hash3(data + i)];
            size_t limit = n - i < 0x10110 ? n - i : 0x10110;
            int tries = 48;
            while (cand >= 0 && i - (size_t)cand <= 0x1000 && tries--) {
                size_t len = 0;
                while (len < limit && data[cand + len] == data[i + len]) len++;
                if (len > best_len) {
                    best_len = len;
                    best_disp = i - (size_t)cand;
                    if (len == limit) break;
                }
                cand = prev[cand];
            }
        }
        if (best_len >= 3) {
            size_t d = best_disp - 1;
            uint8_t c[4];
            out->p[flag_pos] |= (uint8_t)(0x80 >> bit);
            if (best_len <= 0x10) {
                c[0] = (uint8_t)((best_len - 1) << 4 | d >> 8), c[1] = (uint8_t)d;
                buf_add(out, c, 2);
            } else if (best_len <= 0x110) {
                size_t m = best_len - 0x11;
                c[0] = (uint8_t)(m >> 4), c[1] = (uint8_t)((m & 15) << 4 | d >> 8), c[2] = (uint8_t)d;
                buf_add(out, c, 3);
            } else {
                size_t m = best_len - 0x111;
                c[0] = (uint8_t)(0x10 | m >> 12), c[1] = (uint8_t)(m >> 4), c[2] = (uint8_t)((m & 15) << 4 | d >> 8),
                c[3] = (uint8_t)d;
                buf_add(out, c, 4);
            }
            for (p = i; p < i + best_len; p++) INSERT(p);
            i += best_len;
        } else {
            buf_add(out, data + i, 1);
            INSERT(i);
            i++;
        }
        bit++;
    }
#undef INSERT
    free(head);
    free(prev);
    buf_pad(out, 4);
    return i >= n;
}

/* ------------------------------------------------------------------------ */
/* U8 archives */

typedef struct {
    char path[256];
    uint32_t node, off, size;
} U8File;

static int is_u8(const uint8_t* p, size_t n) { return n >= 16 && p[0] == 0x55 && p[1] == 0xAA && p[2] == 0x38 && p[3] == 0x2D; }

/* Every file of an archive in node order, or NULL (count in *count). */
static U8File* u8_files(const uint8_t* d, size_t len, int* count) {
    uint32_t root, n, strings, i, ends[32];
    char names[32][128];
    int depth = 0, cap = 64;
    U8File* files;
    *count = 0;
    if (!is_u8(d, len)) return NULL;
    root = be32(d + 4);
    if ((uint64_t)root + 12 > len) return NULL;
    n = be32(d + root + 8);
    strings = root + n * 12;
    if (strings > len) return NULL;
    files = (U8File*)malloc(sizeof(U8File) * cap);
    for (i = 1; files && i < n; i++) {
        const uint8_t* e = d + root + i * 12;
        uint32_t t = be32(e), name = strings + (t & 0xFFFFFF);
        char nm[128];
        while (depth && i >= ends[depth - 1]) depth--;
        snprintf(nm, sizeof(nm), "%s", name < len ? (const char*)d + name : "");
        if (t >> 24) {
            if (depth < 32) {
                ends[depth] = be32(e + 8);
                snprintf(names[depth], sizeof(names[depth]), "%s", nm);
                depth++;
            }
        } else {
            U8File* f;
            int k;
            size_t at = 0;
            if (*count == cap) {
                U8File* more = (U8File*)realloc(files, sizeof(U8File) * (cap *= 2));
                if (!more) break;
                files = more;
            }
            f = &files[(*count)++];
            f->path[0] = '\0';
            for (k = 0; k < depth; k++) {
                if (!names[k][0] || !strcmp(names[k], ".")) continue;
                at += snprintf(f->path + at, sizeof(f->path) - at, "%s/", names[k]);
                if (at >= sizeof(f->path)) at = sizeof(f->path) - 1;
            }
            snprintf(f->path + at, sizeof(f->path) - at, "%s", nm);
            f->node = i;
            f->off = be32(e + 4);
            f->size = be32(e + 8);
            if ((uint64_t)f->off + f->size > len) (*count)--;
        }
    }
    return files;
}

static const U8File* u8_find(const U8File* f, int n, const char* path) {
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(f[i].path, path)) return &f[i];
    return NULL;
}

/* The archive with replace[i] (where set) in place of files[i]: the header
 * and the node and name tables kept, the data laid out again in its old
 * order, each file on 32 bytes. */
static int u8_replace(const uint8_t* d, const U8File* files, int count, const Buf* replace, Buf* out) {
    uint32_t root = be32(d + 4), start = be32(d + 12);
    int i, k;
    int* order = (int*)malloc(sizeof(int) * (count ? count : 1));
    if (!order) return 0;
    for (i = 0; i < count; i++) order[i] = i;
    for (i = 1; i < count; i++) { /* by offset (insertion: archives are short) */
        int v = order[i];
        for (k = i; k > 0 && files[order[k - 1]].off > files[v].off; k--) order[k] = order[k - 1];
        order[k] = v;
    }
    out->n = 0;
    if (!buf_add(out, d, start)) {
        free(order);
        return 0;
    }
    for (k = 0; k < count; k++) {
        const U8File* f = &files[order[k]];
        const uint8_t* body = replace[order[k]].p ? replace[order[k]].p : d + f->off;
        size_t size = replace[order[k]].p ? replace[order[k]].n : f->size;
        buf_pad(out, 32);
        put32(out->p + root + f->node * 12 + 4, (uint32_t)out->n);
        put32(out->p + root + f->node * 12 + 8, (uint32_t)size);
        if (!buf_add(out, body, size)) {
            free(order);
            return 0;
        }
    }
    buf_pad(out, 32);
    free(order);
    return 1;
}

/* A file of an archive, decompressed if it is. */
static int unpack(const uint8_t* p, size_t n, Buf* out) {
    out->n = 0;
    if (n && p[0] == 0x11) return lz11_decompress(p, n, out);
    return buf_add(out, p, n);
}

/* ------------------------------------------------------------------------ */
/* Message files (MSBT: big-endian on the Wii) */

typedef struct {
    const uint8_t* data; /* the section's data, after its 16-byte head */
    uint32_t size;
    const uint8_t* head;
} MsbtSec;

/* A message file's sections (8 at most); how many, 0 if it isn't one. */
static int msbt_sections(const uint8_t* d, size_t n, MsbtSec* s) {
    size_t p = 0x20;
    int count, i;
    if (n < 0x20 || memcmp(d, "MsgStdBn", 8) || d[8] != 0xFE || d[9] != 0xFF) return 0;
    count = be16(d + 0x0E);
    if (count > 8) return 0;
    for (i = 0; i < count; i++) {
        if (p + 16 > n) return 0;
        s[i].head = d + p;
        s[i].size = be32(d + p + 4);
        s[i].data = d + p + 16;
        if (p + 16 + s[i].size > n) return 0;
        p = (p + 16 + s[i].size + 15) & ~(size_t)15;
    }
    return count;
}

static const MsbtSec* msbt_find(const MsbtSec* s, int n, const char* magic) {
    int i;
    for (i = 0; i < n; i++)
        if (!memcmp(s[i].head, magic, 4)) return &s[i];
    return NULL;
}

/* The message a label names in a file's LBL1, or -1. */
static int64_t msbt_label(const MsbtSec* lbl, const uint8_t* name, int len) {
    uint32_t slots = be32(lbl->data), i;
    for (i = 0; i < slots && 4 + 8 * i + 8 <= lbl->size; i++) {
        uint32_t count = be32(lbl->data + 4 + 8 * i), off = be32(lbl->data + 8 + 8 * i), k;
        for (k = 0; k < count && off < lbl->size; k++) {
            int l = lbl->data[off];
            if (off + 1 + l + 4 > lbl->size) return -1;
            if (l == len && !memcmp(lbl->data + off + 1, name, (size_t)len)) return be32(lbl->data + off + 1 + l);
            off += 5 + l;
        }
    }
    return -1;
}

/* A message's text in a TXT2: where it is and how long. */
static int msbt_text(const MsbtSec* txt, uint32_t i, const uint8_t** p, uint32_t* n) {
    uint32_t count = be32(txt->data), a, b;
    if (i >= count || 4 + 4 * count > txt->size) return 0;
    a = be32(txt->data + 4 + 4 * i);
    b = i + 1 < count ? be32(txt->data + 8 + 4 * i) : txt->size;
    if (a > b || b > txt->size) return 0;
    *p = txt->data + a;
    *n = b - a;
    return 1;
}

/* The USA message file with each message the other has (by label) in its
 * place; the rest of it (other sections, the messages the other lacks)
 * the USA's. How many were taken in *taken; 0 if either isn't one. */
static int msbt_merge(const uint8_t* usa, size_t un, const uint8_t* other, size_t on, Buf* out, int* taken) {
    MsbtSec su[8], so[8];
    int nu = msbt_sections(usa, un, su), no = msbt_sections(other, on, so), i;
    const MsbtSec *lu, *lo, *tu, *to;
    uint32_t count, k, slots;
    Buf txt = {0};
    *taken = 0;
    if (!nu || !no) return 0;
    lu = msbt_find(su, nu, "LBL1"), lo = msbt_find(so, no, "LBL1");
    tu = msbt_find(su, nu, "TXT2"), to = msbt_find(so, no, "TXT2");
    if (!lu || !lo || !tu || !to || tu->size < 4) return 0;
    count = be32(tu->data);
    {
        /* Which text each USA message takes: its own, or the other's. */
        const uint8_t** text = (const uint8_t**)calloc(count ? count : 1, sizeof(*text));
        uint32_t* len = (uint32_t*)calloc(count ? count : 1, sizeof(*len));
        uint32_t pos;
        if (!text || !len) {
            free(text);
            free(len);
            return 0;
        }
        for (k = 0; k < count; k++) msbt_text(tu, k, &text[k], &len[k]);
        slots = be32(lu->data);
        for (k = 0; k < slots && 4 + 8 * k + 8 <= lu->size; k++) {
            uint32_t n = be32(lu->data + 4 + 8 * k), off = be32(lu->data + 8 + 8 * k), e;
            for (e = 0; e < n && off < lu->size; e++) {
                int l = lu->data[off];
                uint32_t idx;
                int64_t j;
                const uint8_t* tp;
                uint32_t tn;
                if (off + 1 + l + 4 > lu->size) break;
                idx = be32(lu->data + off + 1 + l);
                j = msbt_label(lo, lu->data + off + 1, l);
                if (j >= 0 && idx < count && msbt_text(to, (uint32_t)j, &tp, &tn)) {
                    text[idx] = tp;
                    len[idx] = tn;
                    (*taken)++;
                }
                off += 5 + l;
            }
        }
        /* The TXT2 again: the count, the offsets, the texts. */
        buf_add(&txt, NULL, 4 + 4 * (size_t)count);
        put32(txt.p, count);
        pos = 4 + 4 * count;
        for (k = 0; k < count; k++) {
            put32(txt.p + 4 + 4 * k, pos);
            pos += len[k];
        }
        for (k = 0; k < count; k++) buf_add(&txt, text[k], len[k]);
        free(text);
        free(len);
    }
    out->n = 0;
    buf_add(out, usa, 0x20);
    for (i = 0; i < nu; i++) {
        const uint8_t* data = &su[i] == tu ? txt.p : su[i].data;
        uint32_t size = &su[i] == tu ? (uint32_t)txt.n : su[i].size;
        uint8_t head[16];
        memcpy(head, su[i].head, 16);
        put32(head + 4, size);
        buf_add(out, head, 16);
        buf_add(out, data, size);
        while (out->n % 16) buf_add(out, "\xAB", 1);
    }
    put32(out->p + 0x12, (uint32_t)out->n);
    buf_free(&txt);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Pictures (TPL) */

/* Whether a picture can take the other disc's place: the same pictures,
 * of the same sizes and formats (the USA menus' layouts are drawn for
 * them), and palettes of the same format. A palette may have more or
 * fewer colours: the game reads its size from the file. */
static int tpl_fits(const uint8_t* a, size_t an, const uint8_t* b, size_t bn) {
    static const uint8_t kTpl[4] = {0x00, 0x20, 0xAF, 0x30};
    uint32_t n, i;
    if (an < 12 || bn < 12 || memcmp(a, kTpl, 4) || memcmp(b, kTpl, 4)) return 0;
    n = be32(a + 4);
    if (n != be32(b + 4) || be32(a + 8) + 8 * (uint64_t)n > an || be32(b + 8) + 8 * (uint64_t)n > bn) return 0;
    for (i = 0; i < n; i++) {
        uint32_t ia = be32(a + be32(a + 8) + 8 * i), pa = be32(a + be32(a + 8) + 8 * i + 4);
        uint32_t ib = be32(b + be32(b + 8) + 8 * i), pb = be32(b + be32(b + 8) + 8 * i + 4);
        if ((uint64_t)ia + 8 > an || (uint64_t)ib + 8 > bn || memcmp(a + ia, b + ib, 8)) return 0; /* height, width, format */
        if (!pa != !pb) return 0;
        if (pa && ((uint64_t)pa + 8 > an || (uint64_t)pb + 8 > bn || be32(a + pa + 4) != be32(b + pb + 4))) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------ */
/* The sound archive */

static volatile int sPercent;


/* ------------------------------------------------------------------------ */
/* What goes in a pack */

/* The files with voices in them: the European disc has French and German
 * ones of each; the Japanese disc's files of these names are its voices. */
static const char* const kVoiceSounds[] = {
    "M_AT_01", "M_MIJU_AT_VOICE", "M_POKA_AT_VOICE", "M_TUTA_AT_VOICE", "S_DEMO_COMMON", "S_Fd0101", "S_Fd0102",
    "S_Fd0301", "S_Fd0302", "S_Fd0303", "S_Fd0401", "S_Fd0402", "S_Fd0501", "S_Fd0502", "S_Fd0503", "S_Fd0601",
    "S_Fd0605", "S_Fd0801", "S_MIJU_VOICE", "S_POKA_VOICE", "S_POKE_VOICE", "S_RICH_COMMON", "S_TUTA_VOICE",
};
static const char* const kVoiceMovies[] = {"DmCh1110_01", "DmCh1130_00", "DmCh4180_01",
                                           "DmCh5300_00", "DmCh5400_00", "DmCh5410_00"};
#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))

/* The European disc's file ending and message folder for lang. */
static const char* ending_of(int lang) {
    switch (lang) {
    case LANGUAGE_FR: return "_Fr";
    case LANGUAGE_DE: return "_Ge";
    case LANGUAGE_IT: return "_It";
    case LANGUAGE_ES: return "_Sp";
    default: return NULL;
    }
}

static const char* folder_of(int lang) {
    switch (lang) {
    case LANGUAGE_FR: return "EU_French";
    case LANGUAGE_DE: return "EU_German";
    case LANGUAGE_IT: return "EU_Italian";
    case LANGUAGE_ES: return "EU_Spanish";
    default: return NULL;
    }
}

/* Sound/S_POKE_VOICE.acb -> Sound/S_POKE_VOICE_Fr.acb; an AWB's ending goes
 * before _streamfiles (S_POKE_VOICE_Fr_streamfiles.awb). */
static void with_ending(const char* path, const char* ending, char* out, size_t n) {
    static const char kAwb[] = "_streamfiles.awb";
    size_t len = strlen(path), a = sizeof(kAwb) - 1;
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, '/');
    size_t stem;
    if (len > a && !strcmp(path + len - a, kAwb)) stem = len - a;
    else stem = dot && (!slash || dot > slash) ? (size_t)(dot - path) : len;
    snprintf(out, n, "%.*s%s%s", (int)stem, path, ending, path + stem);
}

static int starts_with(const char* s, const char* p) { return !strncmp(s, p, strlen(p)); }

/* Words in pictures the Japanese disc draws smaller (its words are
 * shorter): each is set in a clear picture of the USA one's size, so the
 * USA layouts show it at its own scale, at the left or the middle. Into
 * out (the other disc's file, its new pixels after it): 0 if it isn't one
 * of those (8-bit, one picture, no bigger). As tpl_pad in langpack.py. */
static int tpl_pad(const char* name, const uint8_t* a, size_t an, const uint8_t* b, size_t bn, Buf* out) {
    static const char* const kLeft[] = {"ListText", "CmListText"};
    static const char* const kMiddle[] = {"ActionText", "ButtonNoText", "ButtonYesText", "ButtonBibText", "ButtonSpeedText",
                                          "KeyTextView"};
    static const uint8_t kTpl[4] = {0x00, 0x20, 0xAF, 0x30};
    uint32_t ia, pa, ib, pb, W, H, w, h, f, data, x, y, x0, y0, at;
    size_t size, i;
    int left = 0, middle = 0, clear = 0;
    for (i = 0; i < sizeof(kLeft) / sizeof(kLeft[0]); i++) left |= starts_with(name, kLeft[i]);
    for (i = 0; i < sizeof(kMiddle) / sizeof(kMiddle[0]); i++) middle |= starts_with(name, kMiddle[i]);
    if (!left && !middle) return 0;
    if (an < 12 || bn < 12 || memcmp(a, kTpl, 4) || memcmp(b, kTpl, 4) || be32(a + 4) != 1 || be32(b + 4) != 1)
        return 0;
    if ((uint64_t)be32(a + 8) + 8 > an || (uint64_t)be32(b + 8) + 8 > bn) return 0;
    ia = be32(a + be32(a + 8));
    pa = be32(a + be32(a + 8) + 4);
    ib = be32(b + be32(b + 8));
    pb = be32(b + be32(b + 8) + 4);
    if ((uint64_t)ia + 12 > an || (uint64_t)ib + 12 > bn) return 0;
    H = be16(a + ia);
    W = be16(a + ia + 2);
    f = be32(a + ia + 4);
    h = be16(b + ib);
    w = be16(b + ib + 2);
    data = be32(b + ib + 8);
    if (f != be32(b + ib + 4) || (f != 2 && f != 9) || !pa != !pb || w > W || h > H || (w == W && h == H) || !w || !h)
        return 0;
    if (pa && ((uint64_t)pa + 12 > an || (uint64_t)pb + 12 > bn || be32(a + pa + 4) != be32(b + pb + 4))) return 0;
    if ((uint64_t)data + ((w + 7) / 8) * ((h + 3) / 4) * 32 > bn) return 0;
    if (f == 9) { /* C8: a clear colour of its RGB5A3 palette */
        uint32_t count = be16(b + pb), pdata = be32(b + pb + 8), k;
        if (!pb || be32(b + pb + 4) != 2 || (uint64_t)pdata + 2 * count > bn) return 0;
        for (k = 0; k < count && (be16(b + pdata + 2 * k) & 0xF000); k++) {}
        if (k == count) return 0;
        clear = (int)k;
    }
    out->n = 0;
    if (!buf_add(out, b, bn)) return 0;
    while (out->n % 32)
        if (!buf_add(out, NULL, 1)) return 0;
    at = (uint32_t)out->n;
    size = (size_t)((W + 7) / 8) * ((H + 3) / 4) * 32;
    if (!buf_reserve(out, size)) return 0;
    memset(out->p + at, clear, size);
    out->n += size;
    x0 = left ? 0 : (W - w) / 2;
    y0 = (H - h) / 2;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint32_t X = x0 + x, Y = y0 + y;
            out->p[at + ((Y / 4) * ((W + 7) / 8) + X / 8) * 32 + (Y % 4) * 8 + X % 8] =
                b[data + ((y / 4) * ((w + 7) / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8];
        }
    }
    out->p[ib] = (uint8_t)(H >> 8);
    out->p[ib + 1] = (uint8_t)H;
    out->p[ib + 2] = (uint8_t)(W >> 8);
    out->p[ib + 3] = (uint8_t)W;
    put32(out->p + ib + 8, at);
    return 1;
}

/* One file to copy: the USA path it takes and the other disc's file. */
typedef struct {
    const FstFile* from;
    char to[300];
    int voices;
} Copy;

/* What lang's pack takes from the other disc: into out (cap of them); how
 * many. */
static int plan(int lang, const FstFile* fu, int nu, const FstFile* fo, int no, Copy* out, int cap) {
    const char* ending = ending_of(lang);
    const char* folder = folder_of(lang);
    char theirs[600];
    int n = 0, i;
    if (LANGUAGE_HAS_TEXT(lang) && ending && folder) {
        for (i = 0; i < nu && n < cap; i++) {
            const char* p = fu[i].path;
            const FstFile* o = NULL;
            if (starts_with(p, "Message/US_English/")) {
                snprintf(theirs, sizeof(theirs), "Message/%s/%s", folder, p + strlen("Message/US_English/"));
                o = find_file(fo, no, theirs);
            } else if (starts_with(p, "Archive/") || starts_with(p, "Hbm/")) {
                with_ending(p, ending, theirs, sizeof(theirs));
                o = find_file(fo, no, theirs);
            }
            if (o) {
                out[n].from = o;
                snprintf(out[n].to, sizeof(out[n].to), "%s", p);
                out[n++].voices = 0;
            }
        }
    }
    if (LANGUAGE_HAS_VOICES(lang) && lang != LANGUAGE_EN) {
        for (i = 0; i < 2 * COUNT(kVoiceSounds) + COUNT(kVoiceMovies) && n < cap; i++) {
            char ours[300];
            const FstFile* o;
            if (i < 2 * COUNT(kVoiceSounds))
                snprintf(ours, sizeof(ours), i % 2 ? "Sound/%s_streamfiles.awb" : "Sound/%s.acb", kVoiceSounds[i / 2]);
            else snprintf(ours, sizeof(ours), "Movie/%s.mo", kVoiceMovies[i - 2 * COUNT(kVoiceSounds)]);
            if (!find_file(fu, nu, ours)) continue;
            if (ending) with_ending(ours, ending, theirs, sizeof(theirs));
            else snprintf(theirs, sizeof(theirs), "%s", ours);
            if (!(o = find_file(fo, no, theirs))) continue;
            out[n].from = o;
            snprintf(out[n].to, sizeof(out[n].to), "%s", ours);
            out[n++].voices = 1;
        }
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/* The job */

static volatile int sPercent;
static uint64_t sDone, sTotal; /* bytes, for the progress */

/* A disc's file copied to path, in pieces (a movie is 90 MB). */
static int copy_file(const LangDisc* d, const FstFile* f, const char* path) {
    enum { PIECE = 4 << 20 };
    uint8_t* buf = (uint8_t*)malloc(PIECE);
    uint32_t pos = 0;
    FILE* fp;
    int ok = buf != NULL;
    make_dirs(path);
    if (!ok || !(fp = fopen(path, "wb"))) {
        free(buf);
        return 0;
    }
    while (ok && pos < f->size) {
        uint32_t n = f->size - pos < PIECE ? f->size - pos : PIECE;
        ok = d->read(d->user, f->path, f->offset, pos, buf, n) && fwrite(buf, 1, n, fp) == n;
        pos += n;
        sDone += n;
        sPercent = sTotal ? (int)(99.0 * sDone / sTotal) : 0;
    }
    free(buf);
    return fclose(fp) == 0 && ok;
}

/* A disc's file, whole; its size counted as done. */
static int read_whole(const LangDisc* d, const FstFile* f, Buf* out) {
    out->n = 0;
    if (!buf_reserve(out, f->size ? f->size : 1)) return 0;
    if (f->size && !d->read(d->user, f->path, f->offset, 0, out->p, f->size)) return 0;
    out->n = f->size;
    return 1;
}

static void count_done(uint32_t bytes) {
    sDone += bytes;
    sPercent = sTotal ? (int)(99.0 * sDone / sTotal) : 0;
}

/* ------------------------------------------------------------------------ */
/* The Japanese text
 *
 * The Japanese disc has the USA's messages (the same labels in nearly
 * every file), kept elsewhere: each area's in its Field/<area>Res.dac (in
 * Temporary/Field/Park<area>Res.dan), the menus' under Message/JP_Kanji/
 * in their archives, the shared ones in Archive/ResidentMessage_JP.dac
 * (Kanji/ for those with kanji). Each file has two versions, kana only
 * (JP_Japanese) and with kanji (JP_Kanji); the pack takes the kanji one
 * where there is one. It keeps the USA files and archives and puts the
 * Japanese text in them message by message, by label (msbt_merge), and
 * the Japanese font in place of the USA one, and the pictures with words
 * that fit (tpl_fits). Kept English: the staff roll (a different list,
 * with its own table) and the nicknames (the Japanese game makes them of
 * two halves, the USA one has whole names the Japanese file lacks). */

static const char* const kJapaneseKeep[] = {"NickName.msbt", "StaffRoll.msbt"};

/* The USA message file name with the Japanese text merged into out; 1 if
 * that changed it. */
static int jp_merge(const char* name, const uint8_t* usa, size_t un, const uint8_t* theirs, size_t tn, Buf* out,
                    int* messages) {
    int taken, i;
    for (i = 0; i < (int)(sizeof(kJapaneseKeep) / sizeof(kJapaneseKeep[0])); i++)
        if (!strcmp(name, kJapaneseKeep[i])) return 0;
    if (!theirs || !msbt_merge(usa, un, theirs, tn, out, &taken)) return 0;
    *messages += taken;
    return out->n != un || memcmp(out->p, usa, un) != 0;
}

/* The kanji version of a file in an archive, or its kana one. */
static const U8File* jp_version(const U8File* f, int n, const char* kanji, const char* kana) {
    const U8File* x = u8_find(f, n, kanji);
    return x ? x : u8_find(f, n, kana);
}

static int ends_with(const char* s, const char* e) {
    size_t a = strlen(s), b = strlen(e);
    return a >= b && !strcmp(s + a - b, e);
}

/* The Japanese disc's archive for a USA one (its own name, or with _JP). */
static const FstFile* jp_archive(const FstFile* fo, int no, const char* usa_path) {
    char theirs[300];
    const FstFile* o = find_file(fo, no, usa_path);
    if (o || !ends_with(usa_path, ".dac")) return o;
    snprintf(theirs, sizeof(theirs), "%.*s_JP.dac", (int)(strlen(usa_path) - 4), usa_path);
    return find_file(fo, no, theirs);
}

/* How much of the Japanese disc the text reads, for the progress. */
static uint64_t jp_text_bytes(const FstFile* fu, int nu, const FstFile* fo, int no) {
    uint64_t total = 0;
    char last[8] = "", path[64];
    int i;
    for (i = 0; i < nu; i++) {
        const char* p = fu[i].path;
        const FstFile* o = NULL;
        if (starts_with(p, "Message/US_English/") && strlen(p) >= 25 && strncmp(p + 19, last, 6)) {
            snprintf(last, sizeof(last), "%.6s", p + 19);
            snprintf(path, sizeof(path), "Field/%sRes.dac", last);
            o = find_file(fo, no, path);
        } else if (starts_with(p, "Archive/")) {
            o = jp_archive(fo, no, p);
        }
        if (o) total += o->size;
    }
    return total;
}

/* Writes the Japanese text files to dir/text/files; 0 if one couldn't be
 * written. How many files and messages in *files, *messages. */
static int jp_text(const LangDisc* usa, const FstFile* fu, int nu, const LangDisc* other, const FstFile* fo, int no,
                   const char* dir, int* files, int* messages) {
    Buf raw = {0}, area = {0}, mine = {0}, out = {0}, packed = {0}, theirs = {0};
    U8File *af = NULL, *df = NULL;
    int an = 0, dn = 0, i, ok = 1;
    char cur[8] = "", path[1500];
    const uint8_t* dan = NULL;
    size_t dan_n = 0;
    *files = *messages = 0;

    /* The areas' messages. */
    for (i = 0; ok && i < nu; i++) {
        const char* p = fu[i].path;
        const char* name = p + 19;
        const U8File *x, *t;
        char kanji[300], kana[300];
        if (!starts_with(p, "Message/US_English/") || strlen(name) < 6) continue;
        if (strncmp(name, cur, 6)) {
            const FstFile* o;
            snprintf(cur, sizeof(cur), "%.6s", name);
            free(af);
            free(df);
            af = df = NULL;
            an = dn = 0;
            dan = NULL;
            snprintf(path, sizeof(path), "Field/%sRes.dac", cur);
            if (!(o = find_file(fo, no, path))) continue;
            if (!read_whole(other, o, &raw) || !unpack(raw.p, raw.n, &area)) {
                count_done(o->size);
                continue;
            }
            count_done(o->size);
            af = u8_files(area.p, area.n, &an);
            snprintf(path, sizeof(path), "Temporary/Field/Park%sRes.dan", cur);
            if ((x = u8_find(af, an, path)) != NULL && is_u8(area.p + x->off, x->size)) {
                dan = area.p + x->off;
                dan_n = x->size;
                df = u8_files(dan, dan_n, &dn);
            }
        }
        if (!df) continue;
        snprintf(kanji, sizeof(kanji), "Message/JP_Kanji/%s", name);
        snprintf(kana, sizeof(kana), "Message/JP_Japanese/%s", name);
        if (!(t = jp_version(df, dn, kanji, kana)) || !read_whole(usa, &fu[i], &mine)) continue;
        if (jp_merge(name, mine.p, mine.n, dan + t->off, t->size, &out, messages)) {
            snprintf(path, sizeof(path), "%s/text/files/%s", dir, p);
            if (!write_file(path, out.p, out.n)) ok = 0;
            else (*files)++;
        }
    }
    free(af);
    free(df);
    af = df = NULL;

    /* The archives: their messages, the font, the pictures with words. */
    for (i = 0; ok && i < nu; i++) {
        const char* p = fu[i].path;
        const FstFile* o;
        U8File *uf, *jf;
        int un = 0, jn = 0, k, changed = 0;
        Buf* rep;
        if (!starts_with(p, "Archive/") || !(o = jp_archive(fo, no, p))) continue;
        if (!read_whole(other, o, &raw) || !unpack(raw.p, raw.n, &theirs) || !read_whole(usa, &fu[i], &mine) ||
            !unpack(mine.p, mine.n, &area)) {
            count_done(o->size);
            continue;
        }
        count_done(o->size);
        uf = u8_files(area.p, area.n, &un);
        jf = u8_files(theirs.p, theirs.n, &jn);
        rep = (Buf*)calloc(un ? un : 1, sizeof(Buf));
        for (k = 0; uf && jf && rep && k < un; k++) {
            const char* fp = uf[k].path;
            const char* slash = strrchr(fp, '/');
            const char* name = slash ? slash + 1 : fp;
            const uint8_t* body = area.p + uf[k].off;
            const U8File* t = NULL;
            if (!strcmp(fp, "Font/Standerd_US_EU.brfnt")) {
                if ((t = u8_find(jf, jn, "Font/Standerd_JP.brfnt")) != NULL &&
                    (t->size != uf[k].size || memcmp(theirs.p + t->off, body, t->size)))
                    changed |= buf_add(&rep[k], theirs.p + t->off, t->size);
            } else if (ends_with(name, ".msbt") && (!slash || (slash - fp == 7 && starts_with(fp, "Message/")))) {
                char kanji[300], kana[300];
                if (slash) {
                    snprintf(kanji, sizeof(kanji), "Message/JP_Kanji/%s", name);
                    snprintf(kana, sizeof(kana), "Message/JP_Japanese/%s", name);
                } else {
                    snprintf(kanji, sizeof(kanji), "Kanji/%s", name);
                    snprintf(kana, sizeof(kana), "%s", name);
                }
                if ((t = jp_version(jf, jn, kanji, kana)) != NULL &&
                    jp_merge(name, body, uf[k].size, theirs.p + t->off, t->size, &rep[k], messages))
                    changed = 1;
                else rep[k].n = 0, buf_free(&rep[k]);
            } else if (ends_with(name, ".tpl") && (t = u8_find(jf, jn, fp)) != NULL) {
                if (tpl_fits(body, uf[k].size, theirs.p + t->off, t->size)) {
                    if (t->size != uf[k].size || memcmp(theirs.p + t->off, body, t->size))
                        changed |= buf_add(&rep[k], theirs.p + t->off, t->size);
                } else if (tpl_pad(name, body, uf[k].size, theirs.p + t->off, t->size, &rep[k])) {
                    changed = 1;
                } else {
                    rep[k].n = 0;
                }
            }
        }
        if (changed && u8_replace(area.p, uf, un, rep, &out) && lz11_compress(out.p, out.n, &packed)) {
            snprintf(path, sizeof(path), "%s/text/files/%s", dir, p);
            if (!write_file(path, packed.p, packed.n)) ok = 0;
            else (*files)++;
        }
        for (k = 0; rep && k < un; k++) buf_free(&rep[k]);
        free(rep);
        free(uf);
        free(jf);
    }
    buf_free(&raw);
    buf_free(&area);
    buf_free(&mine);
    buf_free(&out);
    buf_free(&packed);
    buf_free(&theirs);
    return ok;
}

static volatile int sState = LANGPACK_IDLE;
static char sMessage[256];

int langpack_state(int* percent, const char** message) {
    if (percent) *percent = sPercent;
    if (message) *message = sMessage;
    return sState;
}

void langpack_fail(const char* message) {
    snprintf(sMessage, sizeof(sMessage), "%s", tr(message));
    sState = LANGPACK_FAILED;
    fprintf(stderr, "language pack: %s\n", message);
}

void langpack_set_running(const char* message) {
    snprintf(sMessage, sizeof(sMessage), "%s", tr(message));
    sPercent = 0;
    sState = LANGPACK_RUNNING;
}

/* One of several packs from one disc: which and of how many (for the
 * message). */
static int sOf, sOfN;

void langpack_run(int lang, const LangDisc* usa, const LangDisc* other) {
    enum { MOST = 512 };
    char out[1100], path[1500];
    int nu = 0, no = 0, i, n, text = 0, voices = 0;
    FstFile* fu = fst_files(usa, &nu);
    FstFile* fo = fst_files(other, &no);
    Copy* copies = (Copy*)malloc(sizeof(Copy) * MOST);
    if (sOfN > 1)
        snprintf(sMessage, sizeof(sMessage), tr("Making the %s pack (%d of %d)"), tr(settings_language_name(lang)), sOf,
                 sOfN);
    else
        snprintf(sMessage, sizeof(sMessage), tr("Making the %s pack"), tr(settings_language_name(lang)));
    sPercent = 0;
    sState = LANGPACK_RUNNING;
    if (!fu || !fo || !copies) {
        langpack_fail("The discs' file tables couldn't be read");
        goto end;
    }
    n = plan(lang, fu, nu, fo, no, copies, MOST);
    if (!n) {
        langpack_fail("Nothing on that disc fits this game");
        goto end;
    }
    snprintf(out, sizeof(out), "%s.new", settings_language_dir(lang));
    remove_tree(out);
    sDone = sTotal = 0;
    for (i = 0; i < n; i++) sTotal += copies[i].from->size;
    if (lang == LANGUAGE_JP) {
        int messages = 0;
        sTotal += jp_text_bytes(fu, nu, fo, no);
        if (!jp_text(usa, fu, nu, other, fo, no, out, &text, &messages)) {
            langpack_fail("The pack couldn't be written: is the storage full?");
            goto end;
        }
        fprintf(stderr, "language pack: %d Japanese messages\n", messages);
    }
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "%s/%s/files/%s", out, copies[i].voices ? "voices" : "text", copies[i].to);
        if (!copy_file(other, copies[i].from, path)) {
            langpack_fail("The pack couldn't be written: is the storage full?");
            goto end;
        }
        if (copies[i].voices) voices++;
        else text++;
    }
    snprintf(path, sizeof(path), "%s/complete", out);
    write_file(path, "1", 1);
    fprintf(stderr, "language pack: %d text files and %d voice files to %s\n", text, voices, out);
    /* In use, the pack the game started with stays until it starts again. */
    if (g_language_started != lang && g_cries_started != lang) langpack_promote(lang);
    snprintf(sMessage, sizeof(sMessage), tr("%s ready"), tr(settings_language_name(lang)));
    if (g_language_started == lang || g_cries_started == lang)
        snprintf(sMessage + strlen(sMessage), sizeof(sMessage) - strlen(sMessage), "%s", tr(": restart"));
    sPercent = 100;
    sState = LANGPACK_DONE;
end:
    free(copies);
    free(fu);
    free(fo);
}

int langpack_run_disc(const char* id, const LangDisc* usa, const LangDisc* other) {
    int langs[4], n = languages_of(id, langs), i, restart = 0;
    char done[256] = "";
    for (i = 0; i < n; i++) {
        sOf = i + 1;
        sOfN = n;
        langpack_run(langs[i], usa, other);
        if (sState != LANGPACK_DONE) break;
        if (g_language_started == langs[i] || g_cries_started == langs[i]) restart = 1;
        snprintf(done + strlen(done), sizeof(done) - strlen(done), "%s%s", i ? tr(i == n - 1 ? " and " : ", ") : "",
                 tr(settings_language_name(langs[i])));
    }
    sOf = sOfN = 0;
    if (n > 1 && sState == LANGPACK_DONE)
        snprintf(sMessage, sizeof(sMessage), tr("%s ready"), done);
        if (restart) snprintf(sMessage + strlen(sMessage), sizeof(sMessage) - strlen(sMessage), "%s", tr(": restart"));
    return n;
}

void langpack_promote(int lang) {
    char dir[1100], fresh[1100], mark[1200];
    snprintf(dir, sizeof(dir), "%s", settings_language_dir(lang));
    snprintf(fresh, sizeof(fresh), "%s.new", dir);
    snprintf(mark, sizeof(mark), "%s/complete", fresh);
    if (!exists(mark)) return;
    remove_tree(dir);
    if (rename(fresh, dir) != 0) fprintf(stderr, "language pack: %s couldn't be put in place\n", fresh);
}

typedef struct {
    void (*fn)(void*);
    void* arg;
} Thread;

#ifdef _WIN32
static DWORD WINAPI thread_main(void* p)
#else
static void* thread_main(void* p)
#endif
{
    Thread t = *(Thread*)p;
    free(p);
    t.fn(t.arg);
    return 0;
}

int langpack_thread(void (*fn)(void*), void* arg) {
    Thread* j = (Thread*)malloc(sizeof(Thread));
    if (!j) return -1;
    j->fn = fn;
    j->arg = arg;
#ifdef _WIN32
    {
        HANDLE t = CreateThread(NULL, 0, thread_main, j, 0, NULL);
        if (!t) {
            free(j);
            return -1;
        }
        SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(t);
    }
#else
    {
        pthread_t t;
        if (pthread_create(&t, NULL, thread_main, j)) {
            free(j);
            return -1;
        }
        pthread_detach(t);
    }
#endif
    return 0;
}
