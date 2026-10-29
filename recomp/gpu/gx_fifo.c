/* GX command processor: decodes the FIFO written through the write-gather
 * pipe and keeps the GPU's register state.
 *
 * Commands (first byte):
 *   0x00        NOP
 *   0x08 r v32  load CP register r
 *   0x10 h32 .. load XF registers/memory: (count-1) << 16 | address
 *   0x20..0x38  load indexed XF data (matrices) from a CP array
 *   0x40 a s    call a display list in memory
 *   0x48        invalidate the vertex cache
 *   0x61 v32    load BP register (v >> 24)
 *   0x80..0xBF  draw: (primitive | vertex format), u16 count, vertices
 * Register layouts follow the hardware; see Dolphin's VideoCommon for a
 * reference description. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gx.h"

#define LOG(...) do { if (sLog) { fprintf(stderr, "gx: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* Pixel-engine interrupts, raised when the commands that request them are
 * decoded (the GPU finishes instantly). */
#define INTERRUPT_PE_TOKEN 18
#define INTERRUPT_PE_FINISH 19
#define PE_TOKEN_REG 0xCC00100Eu
void hle_raise_interrupt(int n);
void hle_raise_interrupt_ex(int n, void (*before)(uint32_t data), uint32_t data);

/* Each token interrupt reports its own token, even when several are
 * queued before the first is handled. */
static void set_token(uint32_t token) { ST16(PE_TOKEN_REG, token); }

static int sLog;
static int sReady;

/* ------------------------------------------------------------------------ */
/* Register state */

static uint32_t cp[256];
static uint32_t bp[256];
static uint32_t bp_mask = 0xFFFFFF;
static uint32_t xf[0x1100];
/* XF memory 0x000-0x67F and registers 0x1000-0x105F, packed for the vertex
 * shader (gx.h), and the words written since the last draw. */
static uint32_t sXfPacked[GX_XF_WORDS];
static int sXfDirtyLo = 0, sXfDirtyHi = GX_XF_WORDS;

static void flush_batch(void);
/* Set when anything build_state reads may have changed: draws in between
 * reuse the state built last. */
static int sStateDirty = 1;

static void xf_write(uint32_t addr, uint32_t value) {
    int i;
    if (addr >= 0x1100) return;
    xf[addr] = value;
    if (addr < GX_XF_MEM_WORDS) i = (int)addr;
    else if (addr >= 0x1000 && addr < 0x1000 + GX_XF_REG_WORDS) i = GX_XF_MEM_WORDS + (int)(addr - 0x1000);
    else return;
    if (sXfPacked[i] == value) return;
    /* The pending batch is drawn with the XF state as it is now. */
    flush_batch();
    sXfPacked[i] = value;
    if (addr >= 0x101A && addr <= 0x101F) sStateDirty = 1; /* the viewport */
    if (i < sXfDirtyLo) sXfDirtyLo = i;
    if (i + 1 > sXfDirtyHi) sXfDirtyHi = i + 1;
}
static uint8_t tmem[1 << 20];

static float xf_float(uint32_t addr) {
    float f;
    memcpy(&f, &xf[addr], 4);
    return f;
}

/* TEV color registers: the initial PREV/REG0..2 values and the konst
 * colors, signed 11-bit (color) / 8-bit (konst) per component. */
static int16_t tev_color[4][4];
static int16_t tev_konst[4][4];

/* ------------------------------------------------------------------------ */
/* FIFO capture */

static uint8_t* sFifo;
static size_t sFifoLen, sFifoCap;
static size_t sNeed;

static void fifo_append(uint32_t value, int size) {
    int i;
    if (sFifoLen + 4 > sFifoCap) {
        sFifoCap = sFifoCap ? sFifoCap * 2 : (1u << 20);
        sFifo = (uint8_t*)realloc(sFifo, sFifoCap);
    }
    for (i = size - 1; i >= 0; i--) sFifo[sFifoLen++] = (uint8_t)(value >> (8 * i));
    /* Decode each command as soon as it is complete, as the GPU would:
     * indexed matrix loads, vertex arrays and textures are read from
     * memory the game reuses soon after. */
    if (sFifoLen >= sNeed) gx_flush();
}

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ------------------------------------------------------------------------ */
/* Vertex formats */

enum { ATTR_NONE, ATTR_DIRECT, ATTR_INDEX8, ATTR_INDEX16 };

typedef struct VtxFormat {
    int pnmtx, texmtx[8];   /* per-vertex matrix indices present */
    int pos, nrm, col[2], tex[8];   /* ATTR_* */
    int pos_cnt, pos_fmt, pos_shift;
    int nrm_cnt, nrm_fmt, nrm_index3;
    int col_cnt[2], col_fmt[2];
    int tex_cnt[8], tex_fmt[8], tex_shift[8];
    int size;
    /* Worked out once for read_vertex: component counts and sizes, the
     * fixed-point scales, and which texture matrices and coordinates are
     * present (bit i for each). */
    int pos_n, pos_cs, nrm_cs, nrm_bytes, tex_n[8], tex_cs[8], texmtx_mask, tex_mask;
    float pos_scale, nrm_scale, tex_scale[8];
} VtxFormat;

static int comp_size(int fmt) {
    static const int s[8] = {1, 1, 2, 2, 4, 0, 0, 0};
    return s[fmt & 7];
}

static int color_size(int fmt) {
    static const int s[8] = {2, 3, 4, 2, 3, 4, 0, 0};
    return s[fmt & 7];
}

static int index_size(int type) { return type == ATTR_INDEX8 ? 1 : 2; }

static void get_format(int vat, VtxFormat* f) {
    uint32_t lo = cp[0x50], hi = cp[0x60];
    uint32_t a = cp[0x70 + vat], b = cp[0x80 + vat], c = cp[0x90 + vat];
    int i, size = 0;
    memset(f, 0, sizeof(*f));
    f->pnmtx = lo & 1;
    for (i = 0; i < 8; i++) f->texmtx[i] = (lo >> (1 + i)) & 1;
    f->pos = (lo >> 9) & 3;
    f->nrm = (lo >> 11) & 3;
    f->col[0] = (lo >> 13) & 3;
    f->col[1] = (lo >> 15) & 3;
    for (i = 0; i < 8; i++) f->tex[i] = (hi >> (2 * i)) & 3;

    f->pos_cnt = a & 1;
    f->pos_fmt = (a >> 1) & 7;
    f->pos_shift = (a >> 4) & 31;
    f->nrm_cnt = (a >> 9) & 1;
    f->nrm_fmt = (a >> 10) & 7;
    f->col_cnt[0] = (a >> 13) & 1;
    f->col_fmt[0] = (a >> 14) & 7;
    f->col_cnt[1] = (a >> 17) & 1;
    f->col_fmt[1] = (a >> 18) & 7;
    f->tex_cnt[0] = (a >> 21) & 1;
    f->tex_fmt[0] = (a >> 22) & 7;
    f->tex_shift[0] = (a >> 25) & 31;
    f->nrm_index3 = (a >> 31) & 1;
    f->tex_cnt[1] = b & 1;
    f->tex_fmt[1] = (b >> 1) & 7;
    f->tex_shift[1] = (b >> 4) & 31;
    f->tex_cnt[2] = (b >> 9) & 1;
    f->tex_fmt[2] = (b >> 10) & 7;
    f->tex_shift[2] = (b >> 13) & 31;
    f->tex_cnt[3] = (b >> 18) & 1;
    f->tex_fmt[3] = (b >> 19) & 7;
    f->tex_shift[3] = (b >> 22) & 31;
    f->tex_cnt[4] = (b >> 27) & 1;
    f->tex_fmt[4] = (b >> 28) & 7;
    f->tex_shift[4] = c & 31;
    f->tex_cnt[5] = (c >> 5) & 1;
    f->tex_fmt[5] = (c >> 6) & 7;
    f->tex_shift[5] = (c >> 9) & 31;
    f->tex_cnt[6] = (c >> 14) & 1;
    f->tex_fmt[6] = (c >> 15) & 7;
    f->tex_shift[6] = (c >> 18) & 31;
    f->tex_cnt[7] = (c >> 23) & 1;
    f->tex_fmt[7] = (c >> 24) & 7;
    f->tex_shift[7] = (c >> 27) & 31;

    if (f->pnmtx) size++;
    for (i = 0; i < 8; i++) size += f->texmtx[i];
    if (f->pos == ATTR_DIRECT) size += comp_size(f->pos_fmt) * (f->pos_cnt ? 3 : 2);
    else if (f->pos) size += index_size(f->pos);
    if (f->nrm == ATTR_DIRECT) size += comp_size(f->nrm_fmt) * (f->nrm_cnt ? 9 : 3);
    else if (f->nrm) size += index_size(f->nrm) * (f->nrm_cnt && f->nrm_index3 ? 3 : 1);
    for (i = 0; i < 2; i++) {
        if (f->col[i] == ATTR_DIRECT) size += color_size(f->col_fmt[i]);
        else if (f->col[i]) size += index_size(f->col[i]);
    }
    for (i = 0; i < 8; i++) {
        if (f->tex[i] == ATTR_DIRECT) size += comp_size(f->tex_fmt[i]) * (f->tex_cnt[i] ? 2 : 1);
        else if (f->tex[i]) size += index_size(f->tex[i]);
    }
    f->size = size;

    f->pos_n = f->pos_cnt ? 3 : 2;
    f->pos_cs = comp_size(f->pos_fmt);
    f->pos_scale = 1.0f / (float)(1u << f->pos_shift);
    f->nrm_cs = comp_size(f->nrm_fmt);
    f->nrm_scale = f->nrm_fmt == 1 ? 1.0f / 64 : f->nrm_fmt == 3 ? 1.0f / 16384 : 1.0f;
    f->nrm_bytes = f->nrm == ATTR_DIRECT ? f->nrm_cs * (f->nrm_cnt ? 9 : 3)
                                         : index_size(f->nrm) * (f->nrm_cnt && f->nrm_index3 ? 3 : 1);
    for (i = 0; i < 8; i++) {
        f->tex_n[i] = f->tex_cnt[i] ? 2 : 1;
        f->tex_cs[i] = comp_size(f->tex_fmt[i]);
        f->tex_scale[i] = 1.0f / (float)(1u << f->tex_shift[i]);
        if (f->texmtx[i]) f->texmtx_mask |= 1 << i;
        if (f->tex[i]) f->tex_mask |= 1 << i;
    }
}

/* The vertex formats, worked out again only when their registers change
 * (a draw command names one; the park sends 22,000 a frame). */
static VtxFormat sFormats[8];
static uint32_t sFormatRegs[8][5];
static uint8_t sFormatValid[8];

static const VtxFormat* format(int vat) {
    uint32_t regs[5];
    regs[0] = cp[0x50];
    regs[1] = cp[0x60];
    regs[2] = cp[0x70 + vat];
    regs[3] = cp[0x80 + vat];
    regs[4] = cp[0x90 + vat];
    if (!sFormatValid[vat] || memcmp(regs, sFormatRegs[vat], sizeof(regs)) != 0) {
        get_format(vat, &sFormats[vat]);
        memcpy(sFormatRegs[vat], regs, sizeof(regs));
        sFormatValid[vat] = 1;
    }
    return &sFormats[vat];
}

static float read_comp(const uint8_t* p, int fmt, float scale) {
    switch (fmt) {
    case 0: return p[0] * scale;
    case 1: return (int8_t)p[0] * scale;
    case 2: return rd16(p) * scale;
    case 3: return (int16_t)rd16(p) * scale;
    default: {
        uint32_t u = rd32(p);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    }
}

/* Colors widen to 8 bits per channel as the GPU widens them, repeating
 * the high bits in the low ones. */
static void read_color(const uint8_t* p, int fmt, uint8_t out[4]) {
    uint32_t v;
    switch (fmt) {
    case 0: /* RGB565 */
        v = rd16(p);
        out[0] = (uint8_t)(((v >> 8) & 0xF8) | ((v >> 13) & 7));
        out[1] = (uint8_t)(((v >> 3) & 0xFC) | ((v >> 9) & 3));
        out[2] = (uint8_t)(((v << 3) & 0xF8) | ((v >> 2) & 7));
        out[3] = 255;
        break;
    case 1: /* RGB888 */
    case 2: /* RGB888x */
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = 255;
        break;
    case 3: /* RGBA4444 */
        out[0] = (uint8_t)((p[0] >> 4) * 17);
        out[1] = (uint8_t)((p[0] & 15) * 17);
        out[2] = (uint8_t)((p[1] >> 4) * 17);
        out[3] = (uint8_t)((p[1] & 15) * 17);
        break;
    case 4: /* RGBA6666 */
        v = ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2];
        out[0] = (uint8_t)((((v >> 18) & 63) << 2) | ((v >> 22) & 3));
        out[1] = (uint8_t)((((v >> 12) & 63) << 2) | ((v >> 16) & 3));
        out[2] = (uint8_t)((((v >> 6) & 63) << 2) | ((v >> 10) & 3));
        out[3] = (uint8_t)(((v & 63) << 2) | ((v >> 4) & 3));
        break;
    default: /* RGBA8888 */
        memcpy(out, p, 4);
        break;
    }
}

/* Indexed attribute data lives in main memory: array base + index * stride. */
static const uint8_t* array_ptr(int array, uint32_t index) {
    return MA((cp[0xA0 + array] & 0x3FFFFFFF) + index * (cp[0xB0 + array] & 0xFF));
}

static uint32_t read_index(const uint8_t** p, int type) {
    uint32_t idx;
    if (type == ATTR_INDEX8) return *(*p)++;
    idx = rd16(*p);
    *p += 2;
    return idx;
}

/* Reads the attributes the format has into v, which holds the defaults
 * for the rest (vertex_template). */
static const uint8_t* read_vertex(const uint8_t* p, const VtxFormat* f, GXVertex* v) {
    int i;
    if (f->pnmtx) v->mtx[0] = *p++;
    if (f->texmtx_mask) {
        for (i = 0; i < 8; i++) {
            if (f->texmtx_mask & (1 << i)) v->mtx[1 + i] = *p++;
        }
    }
    if (f->pos) {
        const uint8_t* src = p;
        if (f->pos != ATTR_DIRECT) src = array_ptr(0, read_index(&p, f->pos));
        else p += f->pos_cs * f->pos_n;
        v->pos[0] = read_comp(src, f->pos_fmt, f->pos_scale);
        v->pos[1] = read_comp(src + f->pos_cs, f->pos_fmt, f->pos_scale);
        if (f->pos_n == 3) v->pos[2] = read_comp(src + 2 * f->pos_cs, f->pos_fmt, f->pos_scale);
    }
    if (f->nrm) {
        const uint8_t* src = p;
        if (f->nrm != ATTR_DIRECT) src = array_ptr(1, f->nrm == ATTR_INDEX8 ? *p : rd16(p));
        p += f->nrm_bytes;
        for (i = 0; i < 3; i++) v->nrm[i] = read_comp(src + f->nrm_cs * i, f->nrm_fmt, f->nrm_scale);
    }
    for (i = 0; i < 2; i++) {
        if (f->col[i] == ATTR_DIRECT) {
            read_color(p, f->col_fmt[i], v->color[i]);
            p += color_size(f->col_fmt[i]);
        } else if (f->col[i]) {
            read_color(array_ptr(2 + i, read_index(&p, f->col[i])), f->col_fmt[i], v->color[i]);
        }
    }
    if (f->tex_mask) {
        for (i = 0; i < 8; i++) {
            const uint8_t* src = p;
            if (!(f->tex_mask & (1 << i))) continue;
            if (f->tex[i] != ATTR_DIRECT) src = array_ptr(4 + i, read_index(&p, f->tex[i]));
            else p += f->tex_cs[i] * f->tex_n[i];
            v->tex[i][0] = read_comp(src, f->tex_fmt[i], f->tex_scale[i]);
            if (f->tex_n[i] == 2) v->tex[i][1] = read_comp(src + f->tex_cs[i], f->tex_fmt[i], f->tex_scale[i]);
        }
    }
    return p;
}

/* What a vertex holds before its attributes are read: the default matrix
 * indices (MATINDEX_A and B), white for absent colors, zeros. */
static void vertex_template(GXVertex* t) {
    uint32_t mi_a = xf[0x1018], mi_b = xf[0x1019];
    int i;
    memset(t, 0, sizeof(*t));
    t->mtx[0] = (uint8_t)(mi_a & 0x3F);
    for (i = 0; i < 8; i++) t->mtx[1 + i] = (uint8_t)(i < 4 ? (mi_a >> (6 + 6 * i)) & 0x3F : (mi_b >> (6 * (i - 4))) & 0x3F);
    memset(t->color, 255, sizeof(t->color));
}

/* ------------------------------------------------------------------------ */
/* Draw state for the backend */

static uint64_t sFrame;
static long sDebugFrame; /* defined with its value below */

typedef struct TexEntry {
    uint64_t key;
    uint32_t handle, hash;
    uint64_t checked;
} TexEntry;
#define TEX_CACHE 4096
#define TEX_WAYS 4
static TexEntry sTexCache[TEX_CACHE];

/* Spreads every bit of a key over the result (splitmix64's finalizer). The
 * set index needs the address bits, which sit high in the key: without
 * this, all textures of one size and format shared a set and kept
 * evicting each other. */
static uint64_t mix64(uint64_t k) {
    k ^= k >> 30;
    k *= 0xBF58476D1CE4E5B9ull;
    k ^= k >> 27;
    k *= 0x94D049BB133111EBull;
    return k ^ (k >> 31);
}

/* Hashes every byte: textures are checked for changes on every frame they
 * are used. Four independent lanes of eight bytes, so the multiplies
 * overlap instead of each waiting for the last. */
static uint32_t hash_bytes(const uint8_t* p, int n) {
    const uint64_t m = 0xFF51AFD7ED558CCDull;
    uint64_t h0 = 0x9E3779B97F4A7C15ull ^ (uint64_t)n, h1 = 0xC2B2AE3D27D4EB4Full, h2 = 0x165667B19E3779F9ull,
             h3 = 0x27D4EB2F165667C5ull, h;
    int i;
    for (i = 0; i + 32 <= n; i += 32) {
        uint64_t w[4];
        memcpy(w, p + i, 32);
        h0 = (h0 ^ w[0]) * m;
        h1 = (h1 ^ w[1]) * m;
        h2 = (h2 ^ w[2]) * m;
        h3 = (h3 ^ w[3]) * m;
        h0 ^= h0 >> 29;
        h1 ^= h1 >> 29;
        h2 ^= h2 >> 29;
        h3 ^= h3 >> 29;
    }
    h = h0 ^ (h1 * 0x9E3779B97F4A7C15ull) ^ (h2 * 0xC2B2AE3D27D4EB4Full) ^ (h3 * 0x165667B19E3779F9ull);
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * m;
        h ^= h >> 29;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    h ^= h >> 32;
    return (uint32_t)h;
}

/* EFB copies made to textures, looked up before main memory. */
typedef struct CopyTex {
    uint32_t addr, handle;
    int width, height;
    int bytes;        /* size of the copy in guest memory */
    uint32_t mem_hash; /* of that memory when the copy was made */
    uint64_t checked;  /* frame its memory was last checked in, plus 1 */
} CopyTex;
static CopyTex sCopies[64];
static int sCopyCount;

/* Size in guest memory of an EFB copy of the given copy format (the
 * texture copy formats, R4 to GB8), from its bytes per texel. */
static int copy_bytes(int fmt, int width, int height) {
    static const int kTexFmt[16] = {0, 1, 1, 3, 3, 3, 6, 1, 1, 1, 1, 3, 3, 1, 1, 1}; /* I4, I8, IA8, RGBA8 sizes */
    return gx_texture_size(kTexFmt[fmt & 15], width, height);
}

/* The copy lives on the GPU only: guest memory at its address keeps what
 * was there. If that memory changes later, the game has put something
 * else there (a texture loaded from disc, say), and the copy no longer
 * stands for it. */
static int copy_still_valid(CopyTex* t) {
    /* Once a frame: a full-screen copy is over a megabyte, and a frame can
     * sample it many times. */
    if (t->checked == sFrame + 1) return 1;
    if (t->bytes > 0 && hash_bytes(MA(t->addr), t->bytes) != t->mem_hash) return 0;
    t->checked = sFrame + 1;
    return 1;
}

void gx_register_copy(uint32_t addr, uint32_t handle, int width, int height, int fmt) {
    int i;
    if (getenv("RECOMP_GX_COPY_LOG")) {
        static uint32_t seen[256];
        static int nseen;
        int k;
        for (k = 0; k < nseen && seen[k] != addr; k++) {}
        if (k == nseen && nseen < 256) {
            seen[nseen++] = addr;
            fprintf(stderr, "gx: EFB copy target %08X %dx%d fmt %d (frame %llu, %d registered)\n", addr, width, height,
                    fmt, (unsigned long long)sFrame, sCopyCount);
        }
    }
    for (i = 0; i < sCopyCount; i++) {
        if (sCopies[i].addr == addr) break;
    }
    if (i == sCopyCount) {
        if (sCopyCount == 64) i = 0;
        else sCopyCount++;
    }
    sCopies[i].addr = addr;
    sCopies[i].handle = handle;
    sCopies[i].width = width;
    sCopies[i].height = height;
    sCopies[i].bytes = copy_bytes(fmt, width, height);
    sCopies[i].mem_hash = sCopies[i].bytes > 0 ? hash_bytes(MA(addr), sCopies[i].bytes) : 0;
    sCopies[i].checked = sFrame + 1;
    sStateDirty = 1; /* draws sampling addr now get the copy */
}

static uint32_t texture_for_map(int map) {
    int r = map < 4 ? map : map - 4 + 0x20;
    uint32_t mode0 = bp[0x80 + r], img0 = bp[0x88 + r], img3 = bp[0x94 + r], tlut = bp[0x98 + r];
    int width = (img0 & 0x3FF) + 1, height = ((img0 >> 10) & 0x3FF) + 1, fmt = (img0 >> 20) & 15;
    uint32_t addr = (img3 & 0xFFFFFF) << 5;
    int size = gx_texture_size(fmt, width, height), i;
    uint64_t key;
    TexEntry* e;
    uint32_t h;
    if ((long)sFrame == sDebugFrame) {
        int copy = -1;
        for (i = 0; i < sCopyCount; i++) {
            if (sCopies[i].addr == addr) copy = i;
        }
        fprintf(stderr, "   map %d: %08X fmt %d %dx%d mode %X tlut %X%s", map, addr, fmt, width, height, mode0, tlut,
                copy >= 0 ? " (EFB copy" : "\n");
        if (copy >= 0) fprintf(stderr, " %dx%d)\n", sCopies[copy].width, sCopies[copy].height);
    }
    for (i = 0; i < sCopyCount; i++) {
        if (sCopies[i].addr != addr) continue;
        if (copy_still_valid(&sCopies[i])) return sCopies[i].handle;
        /* Stale: forget it and use what is in memory now. */
        sCopies[i] = sCopies[--sCopyCount];
        break;
    }
    if (!addr || size <= 0) return 0;
    key = ((uint64_t)addr << 32) ^ ((uint64_t)fmt << 28) ^ ((uint64_t)width << 14) ^ (uint64_t)height ^
          ((uint64_t)(tlut & 0xFFF) << 40) ^ ((uint64_t)(mode0 & 0x1F) << 52);
    {
        /* Four ways per set, so textures used together (a movie's Y, U and
         * V planes) don't evict each other; a miss takes the way used
         * longest ago. */
        TexEntry* set = &sTexCache[(size_t)(mix64(key) % (TEX_CACHE / TEX_WAYS)) * TEX_WAYS];
        int w;
        e = NULL;
        for (w = 0; w < TEX_WAYS && !e; w++) {
            if (set[w].handle && set[w].key == key) e = &set[w];
        }
        for (w = 0; w < TEX_WAYS && !e; w++) {
            if (!set[w].handle) e = &set[w];
        }
        if (!e) {
            e = &set[0];
            for (w = 1; w < TEX_WAYS; w++) {
                if (set[w].checked < e->checked) e = &set[w];
            }
        }
    }
    if (e->handle && e->key == key && e->checked == sFrame) return e->handle;
    h = hash_bytes(MA(addr), size);
    if (fmt >= 8 && fmt <= 10) h ^= hash_bytes(&tmem[((tlut & 0x3FF) << 9) & 0xFFFFF], fmt == 8 ? 32 : 512);
    if (e->key != key || e->hash != h || !e->handle) {
        uint8_t* rgba = (uint8_t*)malloc((size_t)width * height * 4);
        gx_decode_texture(rgba, MA(addr), fmt, width, height, &tmem[((tlut & 0x3FF) << 9) & 0xFFFFF], (tlut >> 10) & 3);
        {
            /* RECOMP_GX_DUMP_TEX=<address>: save that texture, as decoded,
             * to tex_<address>.bmp (once). */
            const char* want = getenv("RECOMP_GX_DUMP_TEX");
            static int dumped;
            if (want && !dumped && (strtoul(want, NULL, 16) & 0x3FFFFFFF) == (addr & 0x3FFFFFFF)) {
                char path[64];
                FILE* f;
                snprintf(path, sizeof(path), "tex_%08X.bmp", addr);
                if ((f = fopen(path, "wb")) != NULL) {
                    uint8_t hdr[54] = {'B', 'M'};
                    int y, x;
                    *(uint32_t*)(hdr + 2) = 54 + (uint32_t)(width * height * 4);
                    hdr[10] = 54;
                    hdr[14] = 40;
                    *(int32_t*)(hdr + 18) = width;
                    *(int32_t*)(hdr + 22) = -height;
                    hdr[26] = 1;
                    hdr[28] = 32;
                    fwrite(hdr, 1, 54, f);
                    for (y = 0; y < height; y++) {
                        for (x = 0; x < width; x++) {
                            const uint8_t* px = rgba + (y * width + x) * 4;
                            uint8_t bgra[4] = {px[2], px[1], px[0], px[3]};
                            fwrite(bgra, 1, 4, f);
                        }
                    }
                    fclose(f);
                    fprintf(stderr, "gx: saved %s (fmt %d %dx%d)\n", path, fmt, width, height);
                }
                dumped = 1;
            }
        }
        if (e->handle && e->key == key) {
            /* Same texture, new contents (a movie frame, say): the key
             * covers size and sampling, so update it in place. */
            flush_batch();
            gxb_texture_update(e->handle, width, height, rgba);
        } else {
            /* The way goes to another texture. The old one is freed (or
             * GPU memory grows without bound) once the frame is
             * presented: draws made this frame may still use it. */
            if (e->handle) gxb_texture_free(e->handle);
            e->handle = gxb_texture(key, width, height, rgba, mode0 & 3, (mode0 >> 2) & 3, (mode0 >> 4) & 1);
        }
        free(rgba);
        e->key = key;
        e->hash = h;
    }
    e->checked = sFrame;
    return e->handle;
}

static void build_state(GXDrawState* st) {
    uint32_t genmode = bp[0x00], ac = bp[0xF3], blend = bp[0x41], zmode = bp[0x40];
    int i, k;
    memset(st, 0, sizeof(*st));
    st->num_stages = (int)((genmode >> 10) & 15) + 1;
    st->num_texcoords = (int)(genmode & 15);
    for (i = 0; i < st->num_stages; i++) {
        GXTevStage* s = &st->stage[i];
        uint32_t ce = bp[0xC0 + 2 * i], ae = bp[0xC1 + 2 * i];
        uint32_t order = bp[0x28 + i / 2] >> ((i & 1) * 12);
        uint32_t ksel = bp[0xF6 + i / 2] >> ((i & 1) * 10);
        s->color_in[0] = (ce >> 12) & 15;
        s->color_in[1] = (ce >> 8) & 15;
        s->color_in[2] = (ce >> 4) & 15;
        s->color_in[3] = ce & 15;
        s->color_bias = (ce >> 16) & 3;
        s->color_op = (ce >> 18) & 1;
        s->color_clamp = (ce >> 19) & 1;
        s->color_scale = (ce >> 20) & 3;
        s->color_dest = (ce >> 22) & 3;
        s->alpha_in[0] = (ae >> 13) & 7;
        s->alpha_in[1] = (ae >> 10) & 7;
        s->alpha_in[2] = (ae >> 7) & 7;
        s->alpha_in[3] = (ae >> 4) & 7;
        s->ras_swap = ae & 3;
        s->tex_swap = (ae >> 2) & 3;
        s->alpha_bias = (ae >> 16) & 3;
        s->alpha_op = (ae >> 18) & 1;
        s->alpha_clamp = (ae >> 19) & 1;
        s->alpha_scale = (ae >> 20) & 3;
        s->alpha_dest = (ae >> 22) & 3;
        s->texmap = order & 7;
        s->texcoord = (order >> 3) & 7;
        s->tex_enable = (order >> 6) & 1;
        s->channel = (order >> 7) & 7;
        s->kcolor_sel = (ksel >> 4) & 31;
        s->kalpha_sel = (ksel >> 9) & 31;
    }
    for (i = 0; i < 4; i++) {
        uint32_t a = bp[0xF6 + 2 * i], b = bp[0xF7 + 2 * i];
        st->swap_table[i][0] = a & 3;
        st->swap_table[i][1] = (a >> 2) & 3;
        st->swap_table[i][2] = b & 3;
        st->swap_table[i][3] = (b >> 2) & 3;
        for (k = 0; k < 4; k++) {
            st->tev_reg[i][k] = tev_color[i][k] / 255.0f;
            st->konst[i][k] = tev_konst[i][k] / 255.0f;
        }
    }
    for (i = 0; i < st->num_stages; i++) {
        const GXTevStage* s = &st->stage[i];
        if (s->tex_enable && !st->texture[s->texmap]) st->texture[s->texmap] = texture_for_map(s->texmap);
    }
    st->alpha_ref[0] = ac & 0xFF;
    st->alpha_ref[1] = (ac >> 8) & 0xFF;
    st->alpha_func[0] = (ac >> 16) & 7;
    st->alpha_func[1] = (ac >> 19) & 7;
    st->alpha_logic = (ac >> 22) & 3;
    st->blend_enable = blend & 1;
    st->logic_enable = (blend >> 1) & 1;
    st->color_update = (blend >> 3) & 1;
    st->alpha_update = (blend >> 4) & 1;
    st->blend_dst = (blend >> 5) & 7;
    st->blend_src = (blend >> 8) & 7;
    st->blend_subtract = (blend >> 11) & 1;
    st->logic_op = (blend >> 12) & 15;
    st->z_enable = zmode & 1;
    st->z_func = (zmode >> 1) & 7;
    st->z_update = (zmode >> 4) & 1;
    st->efb_alpha = (bp[0x43] & 7) == 1;
    st->cull_mode = (genmode >> 14) & 3;
    {
        float wd = xf_float(0x101A), ht = xf_float(0x101B), zr = xf_float(0x101C);
        float xo = xf_float(0x101D), yo = xf_float(0x101E), fz = xf_float(0x101F);
        st->viewport[0] = xo - 342.0f - wd;
        st->viewport[1] = yo - 342.0f + ht;
        st->viewport[2] = 2.0f * wd;
        st->viewport[3] = -2.0f * ht;
        st->viewport[4] = (fz - zr) / 16777216.0f;
        st->viewport[5] = fz / 16777216.0f;
    }
    {
        uint32_t tl = bp[0x20], br = bp[0x21], off = bp[0x59];
        int ox = (int)(off & 0x3FF) * 2 - 342, oy = (int)((off >> 10) & 0x3FF) * 2 - 342;
        st->scissor[0] = (int)((tl >> 12) & 0x7FF) - 342 - ox;
        st->scissor[1] = (int)(tl & 0x7FF) - 342 - oy;
        st->scissor[2] = (int)((br >> 12) & 0x7FF) - 342 - ox + 1;
        st->scissor[3] = (int)(br & 0x7FF) - 342 - oy + 1;
    }
}

/* ------------------------------------------------------------------------ */
/* Draws */

static unsigned sDrawCount, sVertexCount;

/* RECOMP_GX_DEBUG_FRAME=n prints every draw of frame n. */
static long sDebugFrame = -1;
static unsigned sOpCount[256];

static void debug_draw(const GXDrawState* st, const GXVertex* v, int n) {
    int i;
    fprintf(stderr, "draw %u: %d verts, vp %.0f,%.0f %.0fx%.0f z %.3f-%.3f, sc %d,%d-%d,%d, stages %d, blend %d %d/%d sub %d lop %d/%d, z %d/%d/%d, cu %d au %d\n",
            sDrawCount, n, st->viewport[0], st->viewport[1], st->viewport[2], st->viewport[3], st->viewport[4],
            st->viewport[5], st->scissor[0], st->scissor[1], st->scissor[2], st->scissor[3], st->num_stages,
            st->blend_enable, st->blend_src, st->blend_dst, st->blend_subtract, st->logic_enable, st->logic_op, st->z_enable,
            st->z_func, st->z_update, st->color_update, st->alpha_update);
    fprintf(stderr, "   alpha test %d %d / %d %d logic %d; chan0 ctrl %X/%X mat %08X amb %08X; konst0 %.2f %.2f %.2f %.2f\n",
            st->alpha_func[0], st->alpha_ref[0], st->alpha_func[1], st->alpha_ref[1], st->alpha_logic, xf[0x100E],
            xf[0x1010], xf[0x100C], xf[0x100A], st->konst[0][0], st->konst[0][1], st->konst[0][2], st->konst[0][3]);
    fprintf(stderr, "   tev regs: prev %.2f %.2f %.2f %.2f, c0 %.2f %.2f %.2f %.2f, c1 %.2f %.2f %.2f %.2f, c2 %.2f %.2f %.2f %.2f\n",
            st->tev_reg[0][0], st->tev_reg[0][1], st->tev_reg[0][2], st->tev_reg[0][3], st->tev_reg[1][0],
            st->tev_reg[1][1], st->tev_reg[1][2], st->tev_reg[1][3], st->tev_reg[2][0], st->tev_reg[2][1],
            st->tev_reg[2][2], st->tev_reg[2][3], st->tev_reg[3][0], st->tev_reg[3][1], st->tev_reg[3][2],
            st->tev_reg[3][3]);
    if (xf[0x100E] & 2) {
        /* A lit channel: its lights' color, cos and distance attenuation,
         * position and direction. */
        uint32_t mask = ((xf[0x100E] >> 2) & 15) | (((xf[0x100E] >> 11) & 15) << 4), j;
        for (j = 0; j < 8; j++) {
            uint32_t b = 0x600 + j * 16;
            if (!(mask & (1u << j))) continue;
            fprintf(stderr, "   light %u: color %08X cos %g %g %g dist %g %g %g pos %g %g %g dir %g %g %g\n", j, xf[b + 3],
                    xf_float(b + 4), xf_float(b + 5), xf_float(b + 6), xf_float(b + 7), xf_float(b + 8), xf_float(b + 9),
                    xf_float(b + 10), xf_float(b + 11), xf_float(b + 12), xf_float(b + 13), xf_float(b + 14),
                    xf_float(b + 15));
        }
    }
    for (i = 0; i < st->num_stages && i < 3; i++) {
        const GXTevStage* t = &st->stage[i];
        fprintf(stderr, "   stage %d: c %d %d %d %d a %d %d %d %d tex %d(map %d coord %d) ch %d k %d/%d -> %d/%d\n", i,
                t->color_in[0], t->color_in[1], t->color_in[2], t->color_in[3], t->alpha_in[0], t->alpha_in[1],
                t->alpha_in[2], t->alpha_in[3], t->tex_enable, t->texmap, t->texcoord, t->channel, t->kcolor_sel,
                t->kalpha_sel, t->color_dest, t->alpha_dest);
    }
    if (sDrawCount == 1) {
        int k, nz = 0;
        fprintf(stderr, "   xf rows with data:");
        for (k = 0; k < 0x100; k += 4) {
            if (xf[k] | xf[k + 1] | xf[k + 2] | xf[k + 3]) { fprintf(stderr, " %d", k / 4); nz++; }
        }
        fprintf(stderr, " (%d); MATINDEX_A %08X, CP 0xAC %08X 0xBC %08X\n", nz, xf[0x1018], cp[0xAC], cp[0xBC]);
    }
    for (i = 0; i < n && i < 3; i++) {
        /* The vertex's view-space z and its depth as GX computes it (the
         * shader does the same on the GPU). */
        uint32_t m = (uint32_t)v[i].mtx[0] * 4 + 8;
        float vz = xf_float(m) * v[i].pos[0] + xf_float(m + 1) * v[i].pos[1] + xf_float(m + 2) * v[i].pos[2] + xf_float(m + 3);
        float cz = xf[0x1026] ? xf_float(0x1024) * vz + xf_float(0x1025) : xf_float(0x1024) * vz + xf_float(0x1025);
        float cw = xf[0x1026] ? 1.0f : -vz;
        uint32_t mr = (uint32_t)v[i].mtx[0] * 4;
        float vx = xf_float(mr) * v[i].pos[0] + xf_float(mr + 1) * v[i].pos[1] + xf_float(mr + 2) * v[i].pos[2] + xf_float(mr + 3);
        float vy = xf_float(mr + 4) * v[i].pos[0] + xf_float(mr + 5) * v[i].pos[1] + xf_float(mr + 6) * v[i].pos[2] + xf_float(mr + 7);
        float cx = xf[0x1026] ? xf_float(0x1020) * vx + xf_float(0x1021) : xf_float(0x1020) * vx + xf_float(0x1021) * vz;
        float cy = xf[0x1026] ? xf_float(0x1022) * vy + xf_float(0x1023) : xf_float(0x1022) * vy + xf_float(0x1023) * vz;
        /* Screen position in the 640x456 EFB (y down). */
        float sx = cw != 0 ? (cx / cw * 0.5f + 0.5f) * st->viewport[2] + st->viewport[0] : 0;
        float sy = cw != 0 ? (0.5f - cy / cw * 0.5f) * st->viewport[3] + st->viewport[1] : 0;
        fprintf(stderr, "   v%d mtx %.0f pos %.3f %.3f %.3f col %.2f %.2f %.2f %.2f tex %.3f %.3f  view z %.6f depth %.8f screen %.0f,%.0f\n", i,
                (float)v[i].mtx[0], v[i].pos[0], v[i].pos[1], v[i].pos[2], v[i].color[0][0] / 255.0f,
                v[i].color[0][1] / 255.0f, v[i].color[0][2] / 255.0f, v[i].color[0][3] / 255.0f, v[i].tex[0][0], v[i].tex[0][1], vz, cw != 0 ? 1.0f + cz / cw : 0.0f, sx, sy);
    }
}

/* Batching: consecutive draws with the same state go to the backend as
 * one. The game sends many small primitives (22,000 a frame in the park),
 * and each backend draw costs far more than its vertices. A batch is
 * drawn before anything that changes what it would look like: different
 * state, an XF write (xf_write), a texture's contents changing
 * (texture_for_map), or an EFB copy, clear or present (efb_copy).
 *
 * Vertices are decoded straight into the batch, each once, and the
 * primitives become indices into them. */
static GXDrawState sState, sBatchState;
static GXVertex* sBatch;
static uint32_t* sIndex;
static int sBatchVerts, sBatchCap, sIndexCount, sIndexCap;

static void flush_batch(void) {
    if (!sIndexCount) return;
    gxb_draw_triangles(&sBatchState, sBatch, sBatchVerts, sIndex, sIndexCount);
    sBatchVerts = sIndexCount = 0;
}

/* Makes the batch one drawn with the current state, drawing the pending
 * one first if its state differs. */
static void use_current_state(void) {
    int rebuilt = sStateDirty;
    if (rebuilt) {
        build_state(&sState); /* may draw the pending batch, if a texture changes */
        sStateDirty = 0;
    }
    /* Everything but the XF fields, which build_state leaves zero. A
     * pending batch already has the current state unless it was rebuilt. */
    if (!sIndexCount || (rebuilt && memcmp(&sState, &sBatchState, offsetof(GXDrawState, xf)) != 0)) {
        flush_batch();
        sBatchState = sState;
        sBatchState.xf = sXfPacked;
        sBatchState.xf_dirty_lo = sXfDirtyLo;
        sBatchState.xf_dirty_hi = sXfDirtyHi;
        sXfDirtyLo = GX_XF_WORDS;
        sXfDirtyHi = 0;
    }
}

/* Draws count vertices of format f from p as primitive prim. */
static void draw(int prim, const uint8_t* p, const VtxFormat* f, int count) {
    static int skipLo = -2, skipHi = -1;
    GXVertex tpl, *v;
    uint32_t* ix;
    uint32_t base;
    int tris, i;
    switch (prim) {
    case 0x80: /* quads */
    case 0x88: /* quads (alternate encoding) */
        tris = count / 4 * 2;
        break;
    case 0x90: tris = count / 3; break; /* triangles */
    case 0x98: /* triangle strip */
    case 0xA0: /* triangle fan */
        tris = count - 2;
        break;
    default: return; /* lines and points are not drawn yet */
    }
    if (tris <= 0) return;
    if (skipLo == -2) {
        /* RECOMP_GX_SKIP=<first>-<last>: drop those draws (numbered from 0
         * in each frame), to find which draws make up something on
         * screen. */
        const char* e = getenv("RECOMP_GX_SKIP");
        skipLo = -1;
        if (e && sscanf(e, "%d-%d", &skipLo, &skipHi) != 2) skipLo = skipHi = -1;
    }
    if ((int)sDrawCount >= skipLo && (int)sDrawCount <= skipHi) {
        sDrawCount++;
        return;
    }
    use_current_state();
    if (sBatchVerts + count > sBatchCap) {
        sBatchCap = (sBatchVerts + count) * 2;
        sBatch = (GXVertex*)realloc(sBatch, (size_t)sBatchCap * sizeof(GXVertex));
    }
    if (sIndexCount + 3 * tris > sIndexCap) {
        sIndexCap = (sIndexCount + 3 * tris) * 2;
        sIndex = (uint32_t*)realloc(sIndex, (size_t)sIndexCap * sizeof(uint32_t));
    }
    base = (uint32_t)sBatchVerts;
    v = sBatch + base;
    vertex_template(&tpl);
    for (i = 0; i < count; i++) {
        v[i] = tpl;
        p = read_vertex(p, f, &v[i]);
    }
    if ((long)sFrame == sDebugFrame) debug_draw(&sBatchState, v, count);
    ix = sIndex + sIndexCount;
    switch (prim) {
    case 0x80:
    case 0x88:
        for (i = 0; i + 3 < count; i += 4) {
            *ix++ = base + i; *ix++ = base + i + 1; *ix++ = base + i + 2;
            *ix++ = base + i; *ix++ = base + i + 2; *ix++ = base + i + 3;
        }
        break;
    case 0x90:
        for (i = 0; i + 2 < count; i += 3) {
            *ix++ = base + i; *ix++ = base + i + 1; *ix++ = base + i + 2;
        }
        break;
    case 0x98: /* every other triangle turned round, to keep the winding */
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) { *ix++ = base + i + 1; *ix++ = base + i; *ix++ = base + i + 2; }
            else { *ix++ = base + i; *ix++ = base + i + 1; *ix++ = base + i + 2; }
        }
        break;
    default:
        for (i = 1; i + 1 < count; i++) {
            *ix++ = base; *ix++ = base + i; *ix++ = base + i + 1;
        }
        break;
    }
    sBatchVerts += count;
    sIndexCount = (int)(ix - sIndex);
    sDrawCount++;
}

/* ------------------------------------------------------------------------ */
/* Register writes */

static void load_xf(uint32_t addr, const uint8_t* data, int count) {
    int i;
    for (i = 0; i < count; i++) {
        xf_write(addr + (uint32_t)i, rd32(data + 4 * i));
    }
}

static void load_indexed_xf(int n, uint32_t value) {
    uint32_t index = value >> 16, len = ((value >> 12) & 15) + 1, addr = value & 0xFFF, i;
    const uint8_t* src = array_ptr(12 + n, index);
    for (i = 0; i < len; i++) {
        xf_write(addr + i, rd32(src + 4 * i));
    }
}

static void load_tlut(void) {
    uint32_t src = (bp[0x64] & 0xFFFFFF) << 5;
    uint32_t dst = (bp[0x65] & 0x3FF) << 9;
    uint32_t bytes = ((bp[0x65] >> 10) & 0x7FF) * 32;
    if (dst + bytes > sizeof(tmem)) bytes = sizeof(tmem) - dst;
    memcpy(&tmem[dst], MA(src), bytes);
}

static void set_tev_color(int reg, uint32_t v) {
    int i = (reg - 0xE0) / 2, high = (reg - 0xE0) & 1, konst = (v >> 23) & 1;
    int16_t (*dst)[4] = konst ? tev_konst : tev_color;
    int lo = (int)(v & 0x7FF), hi = (int)((v >> 12) & 0x7FF);
    if (lo & 0x400) lo -= 0x800;
    if (hi & 0x400) hi -= 0x800;
    if (!high) {
        dst[i][0] = (int16_t)lo; /* R */
        dst[i][3] = (int16_t)hi; /* A */
    } else {
        dst[i][2] = (int16_t)lo; /* B */
        dst[i][1] = (int16_t)hi; /* G */
    }
}

static void efb_copy(uint32_t exec) {
    uint32_t tl = bp[0x49], wh = bp[0x4A];
    int x = (int)(tl & 0x3FF), y = (int)((tl >> 10) & 0x3FF);
    int w = (int)(wh & 0x3FF) + 1, h = (int)((wh >> 10) & 0x3FF) + 1;
    int clear = (exec >> 11) & 1, to_xfb = (exec >> 14) & 1;
    uint32_t dst = (bp[0x4B] & 0xFFFFFF) << 5;
    if ((long)sFrame == sDebugFrame) {
        fprintf(stderr, "efb copy %dx%d at (%d,%d) to %s %08X, exec %08X, clear %d\n", w, h, x, y, to_xfb ? "XFB" : "texture",
                dst, exec, clear);
    }
    if (to_xfb && (long)sFrame == sDebugFrame) {
        int k;
        fprintf(stderr, "commands in frame:");
        for (k = 0; k < 256; k++) if (sOpCount[k]) fprintf(stderr, " %02X:%u", k, sOpCount[k]);
        fputc(10, stderr);
    }
    flush_batch();
    if (to_xfb) {
        LOG("frame %llu: copy to XFB %dx%d at (%d,%d), %u draws, %u vertices", (unsigned long long)sFrame, w, h, x, y,
            sDrawCount, sVertexCount);
        gxb_present(x, y, w, h);
        sFrame++;
        sDrawCount = sVertexCount = 0;
        sStateDirty = 1; /* textures are checked again each frame */
    } else {
        int fmt = (int)(((exec >> 4) & 7) | (((exec >> 3) & 1) << 3));
        gxb_copy_to_texture(dst, x, y, w, h, fmt, (exec >> 15) & 1, (bp[0x43] & 7) == 1);
    }
    if (clear) {
        uint32_t rgba = ((bp[0x4F] & 0xFF) << 24) | (((bp[0x50] >> 8) & 0xFF) << 16) | ((bp[0x50] & 0xFF) << 8) |
                        ((bp[0x4F] >> 8) & 0xFF);
        uint32_t blend = bp[0x41], zmode = bp[0x40];
        gxb_clear((blend >> 3) & 1, (blend >> 4) & 1, (zmode >> 4) & 1, rgba, bp[0x51] & 0xFFFFFF);
    }
}

static void bp_write(uint32_t v) {
    uint32_t reg = v >> 24, val = v & 0xFFFFFF;
    if (reg == 0xFE) {
        bp_mask = val;
        return;
    }
    val = (bp[reg] & ~bp_mask) | (val & bp_mask);
    bp_mask = 0xFFFFFF;
    if (bp[reg] != val) sStateDirty = 1;
    bp[reg] = val;
    if (reg >= 0xE0 && reg <= 0xE7) set_tev_color((int)reg, val);
    else if (reg == 0x65) load_tlut();
    else if (reg == 0x52) efb_copy(val);
    else if (reg == 0x45) hle_raise_interrupt(INTERRUPT_PE_FINISH); /* draw done */
    else if (reg == 0x47) ST16(PE_TOKEN_REG, val & 0xFFFF);
    else if (reg == 0x48) hle_raise_interrupt_ex(INTERRUPT_PE_TOKEN, set_token, val & 0xFFFF);
}

/* ------------------------------------------------------------------------ */
/* Command parsing */

static unsigned sErrors;

/* Decodes complete commands from p[0..len) and returns the bytes used.
 * *need is set to the size of the incomplete command left at the end. */
static size_t run(const uint8_t* p, size_t len, int depth, size_t* need) {
    size_t pos = 0;
    *need = 1;
    while (pos < len) {
        const uint8_t* c = p + pos;
        size_t left = len - pos;
        uint8_t op = c[0];
        if ((long)sFrame == sDebugFrame && depth >= 0) sOpCount[op]++;
        if (op == 0x00 || op == 0x48 || op == 0x44) {
            pos++;
        } else if (op == 0x08) {
            if (left < 6) { *need = 6; break; }
            cp[c[1]] = rd32(c + 2);
            if (c[1] == 0x30) xf_write(0x1018, cp[0x30]);
            if (c[1] == 0x40) xf_write(0x1019, cp[0x40]);
            pos += 6;
        } else if (op == 0x10) {
            uint32_t h, n;
            if (left < 5) { *need = 5; break; }
            h = rd32(c + 1);
            n = ((h >> 16) & 15) + 1;
            if (left < 5 + 4 * n) { *need = 5 + 4 * n; break; }
            load_xf(h & 0xFFFF, c + 5, (int)n);
            pos += 5 + 4 * n;
        } else if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
            if (left < 5) { *need = 5; break; }
            load_indexed_xf((op - 0x20) >> 3, rd32(c + 1));
            pos += 5;
        } else if (op == 0x40) {
            uint32_t addr, size;
            if (left < 9) { *need = 9; break; }
            addr = rd32(c + 1) & 0x3FFFFFFF;
            size = rd32(c + 5);
            if (depth == 0 && size < (16u << 20)) {
                size_t dl_need;
                run(MA(addr), size, 1, &dl_need);
            }
            pos += 9;
        } else if (op == 0x61) {
            if (left < 5) { *need = 5; break; }
            bp_write(rd32(c + 1));
            pos += 5;
        } else if (op >= 0x80 && op < 0xC0) {
            const VtxFormat* f;
            int count;
            if (left < 3) { *need = 3; break; }
            f = format(op & 7);
            count = rd16(c + 1);
            if (left < 3 + (size_t)count * f->size) { *need = 3 + (size_t)count * f->size; break; }
            sVertexCount += (unsigned)count;
            draw(op & 0xF8, c + 3, f, count);
            pos += 3 + (size_t)count * f->size;
        } else {
            if (sErrors++ < 20) {
                fprintf(stderr, "gx: unknown command %02X at offset %zu (%s); skipping a byte\n", op, pos,
                        depth ? "display list" : "FIFO");
            }
            if (sErrors == 1) {
                size_t from = pos > 96 ? pos - 96 : 0, k;
                for (k = from; k < pos + 32 && k < len; k++) {
                    fprintf(stderr, "%s%02X", (k - from) % 16 == 0 ? "\n  " : " ", p[k]);
                }
                fputc('\n', stderr);
            }
            pos++;
        }
    }
    return pos;
}

void gx_flush(void) {
    size_t used;
    if (!sReady || !sFifoLen) return;
    used = run(sFifo, sFifoLen, 0, &sNeed);
    memmove(sFifo, sFifo + used, sFifoLen - used);
    sFifoLen -= used;
}

void gx_init(void) {
    const char* v = getenv("RECOMP_GX_LOG");
    sLog = v && *v && *v != '0';
    if ((v = getenv("RECOMP_GX_DEBUG_FRAME"))) sDebugFrame = atol(v);
    bp[0x41] = 0x18; /* color and alpha updates on */
    gxb_init();
    g_wgpipe_write = fifo_append;
    g_gx_flush = gx_flush;
    sReady = 1;
}
