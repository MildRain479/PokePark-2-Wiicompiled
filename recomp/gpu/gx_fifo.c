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
}

static float read_comp(const uint8_t* p, int fmt, int shift) {
    float scale = 1.0f / (float)(1u << shift);
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

static void read_color(const uint8_t* p, int fmt, float out[4]) {
    uint32_t v;
    switch (fmt) {
    case 0: /* RGB565 */
        v = rd16(p);
        out[0] = ((v >> 11) & 31) / 31.0f;
        out[1] = ((v >> 5) & 63) / 63.0f;
        out[2] = (v & 31) / 31.0f;
        out[3] = 1.0f;
        break;
    case 1: /* RGB888 */
    case 2: /* RGB888x */
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f;
        out[3] = 1.0f;
        break;
    case 3: /* RGBA4444 */
        v = rd16(p);
        out[0] = ((v >> 12) & 15) / 15.0f;
        out[1] = ((v >> 8) & 15) / 15.0f;
        out[2] = ((v >> 4) & 15) / 15.0f;
        out[3] = (v & 15) / 15.0f;
        break;
    case 4: /* RGBA6666 */
        v = ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2];
        out[0] = ((v >> 18) & 63) / 63.0f;
        out[1] = ((v >> 12) & 63) / 63.0f;
        out[2] = ((v >> 6) & 63) / 63.0f;
        out[3] = (v & 63) / 63.0f;
        break;
    default: /* RGBA8888 */
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f;
        out[3] = p[3] / 255.0f;
        break;
    }
}

/* Indexed attribute data lives in main memory: array base + index * stride. */
static const uint8_t* array_ptr(int array, uint32_t index) {
    return MA((cp[0xA0 + array] & 0x3FFFFFFF) + index * (cp[0xB0 + array] & 0xFF));
}


static const uint8_t* read_vertex(const uint8_t* p, const VtxFormat* f, GXVertex* v) {
    int i;
    uint32_t mi_a = xf[0x1018], mi_b = xf[0x1019];
    v->mtx[0] = (float)(f->pnmtx ? *p++ : (int)(mi_a & 0x3F));
    for (i = 0; i < 8; i++) {
        int def = i < 4 ? (int)((mi_a >> (6 + 6 * i)) & 0x3F) : (int)((mi_b >> (6 * (i - 4))) & 0x3F);
        v->mtx[1 + i] = (float)(f->texmtx[i] ? *p++ : def);
    }
    v->mtx[9] = v->mtx[10] = v->mtx[11] = 0.0f;
    if (f->pos) {
        const uint8_t* src = p;
        int n = f->pos_cnt ? 3 : 2, cs = comp_size(f->pos_fmt);
        if (f->pos != ATTR_DIRECT) {
            uint32_t idx = f->pos == ATTR_INDEX8 ? *p : rd16(p);
            p += index_size(f->pos);
            src = array_ptr(0, idx);
        } else {
            p += cs * n;
        }
        for (i = 0; i < 3; i++) v->pos[i] = i < n ? read_comp(src + cs * i, f->pos_fmt, f->pos_shift) : 0.0f;
    } else {
        v->pos[0] = v->pos[1] = v->pos[2] = 0.0f;
    }
    if (f->nrm) {
        int cs = comp_size(f->nrm_fmt), shift = f->nrm_fmt == 1 ? 6 : f->nrm_fmt == 3 ? 14 : 0;
        const uint8_t* src = p;
        if (f->nrm != ATTR_DIRECT) {
            uint32_t idx = f->nrm == ATTR_INDEX8 ? *p : rd16(p);
            p += index_size(f->nrm) * (f->nrm_cnt && f->nrm_index3 ? 3 : 1);
            src = array_ptr(1, idx);
        } else {
            p += cs * (f->nrm_cnt ? 9 : 3);
        }
        for (i = 0; i < 3; i++) v->nrm[i] = read_comp(src + cs * i, f->nrm_fmt, shift);
    } else {
        v->nrm[0] = v->nrm[1] = v->nrm[2] = 0.0f;
    }
    for (i = 0; i < 2; i++) {
        if (f->col[i] == ATTR_DIRECT) {
            read_color(p, f->col_fmt[i], v->color[i]);
            p += color_size(f->col_fmt[i]);
        } else if (f->col[i]) {
            uint32_t idx = f->col[i] == ATTR_INDEX8 ? *p : rd16(p);
            p += index_size(f->col[i]);
            read_color(array_ptr(2 + i, idx), f->col_fmt[i], v->color[i]);
        } else {
            v->color[i][0] = v->color[i][1] = v->color[i][2] = v->color[i][3] = 1.0f;
        }
    }
    for (i = 0; i < 8; i++) {
        int n = f->tex_cnt[i] ? 2 : 1, cs = comp_size(f->tex_fmt[i]), k;
        const uint8_t* src = p;
        if (!f->tex[i]) {
            v->tex[i][0] = v->tex[i][1] = 0.0f;
            continue;
        }
        if (f->tex[i] != ATTR_DIRECT) {
            uint32_t idx = f->tex[i] == ATTR_INDEX8 ? *p : rd16(p);
            p += index_size(f->tex[i]);
            src = array_ptr(4 + i, idx);
        } else {
            p += cs * n;
        }
        for (k = 0; k < 2; k++) v->tex[i][k] = k < n ? read_comp(src + cs * k, f->tex_fmt[i], f->tex_shift[i]) : 0.0f;
    }
    return p;
}

/* ------------------------------------------------------------------------ */
/* Draw state for the backend */

static uint64_t sFrame;

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

/* Hashes every byte, eight at a time: textures are checked for changes on
 * every frame they are used. */
static uint32_t hash_bytes(const uint8_t* p, int n) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t)n;
    int i;
    for (i = 0; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 29;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return (uint32_t)(h ^ (h >> 32));
}

/* EFB copies made to textures, looked up before main memory. */
typedef struct CopyTex {
    uint32_t addr, handle;
    int width, height;
} CopyTex;
static CopyTex sCopies[64];
static int sCopyCount;

void gx_register_copy(uint32_t addr, uint32_t handle, int width, int height) {
    int i;
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
    for (i = 0; i < sCopyCount; i++) {
        if (sCopies[i].addr == addr) return sCopies[i].handle;
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

static GXVertex* sVerts;
static int sVertCap;
static unsigned sDrawCount, sVertexCount;

/* RECOMP_GX_DEBUG_FRAME=n prints every draw of frame n. */
static long sDebugFrame = -1;
static unsigned sOpCount[256];

static void debug_draw(const GXDrawState* st, const GXVertex* v, int n) {
    int i;
    fprintf(stderr, "draw %u: %d verts, vp %.0f,%.0f %.0fx%.0f z %.3f-%.3f, sc %d,%d-%d,%d, stages %d, blend %d %d/%d, z %d/%d/%d, cu %d au %d\n",
            sDrawCount, n, st->viewport[0], st->viewport[1], st->viewport[2], st->viewport[3], st->viewport[4],
            st->viewport[5], st->scissor[0], st->scissor[1], st->scissor[2], st->scissor[3], st->num_stages,
            st->blend_enable, st->blend_src, st->blend_dst, st->z_enable, st->z_func, st->z_update, st->color_update,
            st->alpha_update);
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
        fprintf(stderr, "   v%d mtx %.0f pos %.3f %.3f %.3f col %.2f %.2f %.2f %.2f tex %.3f %.3f\n", i, v[i].mtx[0],
                v[i].pos[0], v[i].pos[1], v[i].pos[2], v[i].color[0][0], v[i].color[0][1], v[i].color[0][2],
                v[i].color[0][3], v[i].tex[0][0], v[i].tex[0][1]);
    }
}

/* Batching: consecutive draws with the same state go to the backend as
 * one. The game sends many small primitives (22,000 a frame in the park),
 * and each backend draw costs far more than its vertices. A batch is
 * drawn before anything that changes what it would look like: different
 * state, an XF write (xf_write), a texture's contents changing
 * (texture_for_map), or an EFB copy, clear or present (efb_copy). */
static GXDrawState sBatchState;
static GXVertex* sBatch;
static int sBatchCount, sBatchCap;

static void flush_batch(void) {
    if (!sBatchCount) return;
    gxb_draw_triangles(&sBatchState, sBatch, sBatchCount);
    sBatchCount = 0;
}

static void emit(GXVertex* tri, int n) {
    GXDrawState st;
    if (n <= 0) return;
    build_state(&st); /* may draw the pending batch, if a texture changes */
    if ((long)sFrame == sDebugFrame) debug_draw(&st, tri, n);
    /* Everything but the XF fields, which build_state leaves zero. */
    if (!sBatchCount || memcmp(&st, &sBatchState, offsetof(GXDrawState, xf)) != 0) {
        flush_batch();
        st.xf = sXfPacked;
        st.xf_dirty_lo = sXfDirtyLo;
        st.xf_dirty_hi = sXfDirtyHi;
        sXfDirtyLo = GX_XF_WORDS;
        sXfDirtyHi = 0;
        sBatchState = st;
    }
    if (sBatchCount + n > sBatchCap) {
        sBatchCap = (sBatchCount + n) * 2;
        sBatch = (GXVertex*)realloc(sBatch, (size_t)sBatchCap * sizeof(GXVertex));
    }
    memcpy(sBatch + sBatchCount, tri, (size_t)n * sizeof(GXVertex));
    sBatchCount += n;
    sDrawCount++;
}

static void draw(int prim, const GXVertex* v, int count) {
    int n = 0, i;
    int max = count * 3;
    if (max > sVertCap) {
        sVertCap = max * 2;
        sVerts = (GXVertex*)realloc(sVerts, sVertCap * sizeof(GXVertex));
    }
    switch (prim) {
    case 0x80: /* quads */
    case 0x88: /* quads (alternate encoding) */
        for (i = 0; i + 3 < count; i += 4) {
            sVerts[n++] = v[i]; sVerts[n++] = v[i + 1]; sVerts[n++] = v[i + 2];
            sVerts[n++] = v[i]; sVerts[n++] = v[i + 2]; sVerts[n++] = v[i + 3];
        }
        break;
    case 0x90: /* triangles */
        for (i = 0; i + 2 < count; i += 3) {
            sVerts[n++] = v[i]; sVerts[n++] = v[i + 1]; sVerts[n++] = v[i + 2];
        }
        break;
    case 0x98: /* triangle strip */
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) { sVerts[n++] = v[i + 1]; sVerts[n++] = v[i]; sVerts[n++] = v[i + 2]; }
            else { sVerts[n++] = v[i]; sVerts[n++] = v[i + 1]; sVerts[n++] = v[i + 2]; }
        }
        break;
    case 0xA0: /* triangle fan */
        for (i = 1; i + 1 < count; i++) {
            sVerts[n++] = v[0]; sVerts[n++] = v[i]; sVerts[n++] = v[i + 1];
        }
        break;
    default: /* lines and points are not drawn yet */
        return;
    }
    emit(sVerts, n);
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
    } else {
        int fmt = (int)(((exec >> 4) & 7) | (((exec >> 3) & 1) << 3));
        gxb_copy_to_texture(dst, x, y, w, h, fmt, 0);
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
    bp[reg] = (bp[reg] & ~bp_mask) | (val & bp_mask);
    bp_mask = 0xFFFFFF;
    val = bp[reg];
    if (reg >= 0xE0 && reg <= 0xE7) set_tev_color((int)reg, val);
    else if (reg == 0x65) load_tlut();
    else if (reg == 0x52) efb_copy(val);
    else if (reg == 0x45) hle_raise_interrupt(INTERRUPT_PE_FINISH); /* draw done */
    else if (reg == 0x47) ST16(PE_TOKEN_REG, val & 0xFFFF);
    else if (reg == 0x48) hle_raise_interrupt_ex(INTERRUPT_PE_TOKEN, set_token, val & 0xFFFF);
}

/* ------------------------------------------------------------------------ */
/* Command parsing */

static GXVertex* sRaw;
static int sRawCap;
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
            VtxFormat f;
            int count, i;
            const uint8_t* vp;
            if (left < 3) { *need = 3; break; }
            get_format(op & 7, &f);
            count = rd16(c + 1);
            if (left < 3 + (size_t)count * f.size) { *need = 3 + (size_t)count * f.size; break; }
            if (count > sRawCap) {
                sRawCap = count * 2;
                sRaw = (GXVertex*)realloc(sRaw, sRawCap * sizeof(GXVertex));
            }
            vp = c + 3;
            for (i = 0; i < count; i++) {
                vp = read_vertex(vp, &f, &sRaw[i]);
            }
            sVertexCount += (unsigned)count;
            draw(op & 0xF8, sRaw, count);
            pos += 3 + (size_t)count * f.size;
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
