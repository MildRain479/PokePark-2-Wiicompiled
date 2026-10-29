/* THP movie video, decoded natively. A THP video frame is a baseline JPEG
 * (8-bit, Huffman, 4:2:0 YCbCr), and the THP library's THPVideoDecode
 * writes its three planes as GX I8 textures (8x4-pixel tiles). The
 * recompiled decoder took ~12 ms a frame, which made frames late while a
 * movie plays; this one does the same work in native code.
 *
 * RECOMP_THP=orig uses the game's own decoder; RECOMP_THP=check runs both
 * on every frame and reports how far the outputs differ. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"

#define THP_OK 0
#define THP_ERROR 1

typedef struct {
    uint8_t lookup_len[256]; /* codes up to 8 bits: length and symbol by 8-bit prefix */
    uint8_t lookup_sym[256];
    uint16_t maxcode[18];    /* largest code of each length, +1 (0 if none) */
    int32_t valptr[17];      /* index of the first symbol of each length, minus its first code */
    uint8_t sym[256];
} Huff;

typedef struct {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t bits;
    int count;
} Bits;

typedef struct {
    int h, v, tq, td, ta;
    int width, height; /* plane size in pixels */
    uint32_t out;      /* guest address of the plane's I8 texture */
    int dc;
} Comp;

static const uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static float sIdct[8][8]; /* sIdct[x][u] = C(u)/2 * cos((2x+1)u*pi/16) */

static void build_huff(Huff* h, const uint8_t* counts, const uint8_t* syms) {
    int len, i, k = 0, code = 0;
    memset(h, 0, sizeof(*h));
    for (len = 1; len <= 16; len++) {
        h->valptr[len] = k - code;
        for (i = 0; i < counts[len - 1]; i++) {
            h->sym[k] = syms[k];
            if (len <= 8) {
                /* Every 8-bit prefix that starts with this code. */
                int fill = 1 << (8 - len), j;
                for (j = 0; j < fill; j++) {
                    h->lookup_len[(code << (8 - len)) | j] = (uint8_t)len;
                    h->lookup_sym[(code << (8 - len)) | j] = syms[k];
                }
            }
            k++;
            code++;
        }
        h->maxcode[len] = (uint16_t)code; /* codes of this length are < code */
        code <<= 1;
    }
}

/* Keeps at least 16 bits in the buffer. Unlike JPEG, THP's entropy-coded
 * data has no 0xFF 0x00 byte stuffing: every byte is data. */
static void fill(Bits* b) {
    while (b->count <= 24) {
        uint32_t byte = 0;
        if (b->p < b->end) byte = *b->p++;
        b->bits |= byte << (24 - b->count);
        b->count += 8;
    }
}

static int get_bits(Bits* b, int n) {
    int v;
    if (!n) return 0;
    fill(b);
    v = (int)(b->bits >> (32 - n));
    b->bits <<= n;
    b->count -= n;
    return v;
}

static int decode_huff(Bits* b, const Huff* h) {
    int len, code;
    fill(b);
    len = h->lookup_len[b->bits >> 24];
    if (len) {
        int s = h->lookup_sym[b->bits >> 24];
        b->bits <<= len;
        b->count -= len;
        return s;
    }
    code = (int)(b->bits >> 23); /* 9 bits */
    for (len = 9; len <= 16; len++) {
        if (code < h->maxcode[len]) {
            b->bits <<= len;
            b->count -= len;
            return h->sym[h->valptr[len] + code];
        }
        code = (int)(b->bits >> (31 - len));
    }
    return -1;
}

static int extend(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

/* Inverse DCT of one block, level-shifted and clamped to 0..255. */
static void idct8x8(const float* in, uint8_t out[64]) {
    float tmp[64];
    int x, y, u;
    for (y = 0; y < 8; y++) {      /* rows: tmp[y][x] = sum_u in[y][u] * c(x,u) */
        const float* row = in + y * 8;
        for (x = 0; x < 8; x++) {
            float s = 0;
            for (u = 0; u < 8; u++) s += row[u] * sIdct[x][u];
            tmp[y * 8 + x] = s;
        }
    }
    for (x = 0; x < 8; x++) {      /* columns */
        for (y = 0; y < 8; y++) {
            float s = 0;
            int v;
            for (u = 0; u < 8; u++) s += tmp[u * 8 + x] * sIdct[y][u];
            v = (int)floorf(s + 128.0f);
            out[y * 8 + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

/* Writes an 8x8 block at (px, py) of a plane stored as I8 tiles. */
static void store_block(const Comp* c, int px, int py, const uint8_t blk[64]) {
    int y;
    int tiles_x = c->width / 8;
    for (y = 0; y < 8; y++) {
        int yy = py + y;
        uint32_t tile, dst;
        if (yy >= c->height) break;
        tile = (uint32_t)((yy / 4) * tiles_x + px / 8);
        dst = c->out + tile * 32 + (uint32_t)(yy % 4) * 8;
        memcpy(MA(dst), blk + y * 8, 8);
    }
}

static int decode_frame(uint32_t file, uint32_t tileY, uint32_t tileU, uint32_t tileV) {
    /* Frames are under 256 KB; the scan stops at its end marker. */
    const uint8_t* p = MA(file);
    const uint8_t* end = p + 0x80000;
    uint8_t qt[4][64];
    Huff dc[4], ac[4];
    Comp comp[3];
    int ncomp = 0, width = 0, height = 0, hmax = 1, vmax = 1, i;
    if (!sIdct[0][0]) {
        int x, u;
        for (x = 0; x < 8; x++)
            for (u = 0; u < 8; u++)
                sIdct[x][u] = (float)((u ? 0.5 : 0.5 / sqrt(2.0)) * cos((2 * x + 1) * u * 3.14159265358979323846 / 16));
    }
    if (p[0] != 0xFF || p[1] != 0xD8) return THP_ERROR;
    p += 2;
    for (;;) {
        int marker, len;
        if (p + 4 > end || p[0] != 0xFF) return THP_ERROR;
        marker = p[1];
        len = p[2] << 8 | p[3];
        if (marker == 0xDB) { /* DQT: 8-bit tables */
            const uint8_t* q = p + 4;
            while (q < p + 2 + len) {
                memcpy(qt[q[0] & 3], q + 1, 64);
                q += 65;
            }
        } else if (marker == 0xC4) { /* DHT */
            const uint8_t* q = p + 4;
            while (q < p + 2 + len) {
                int cls = q[0] >> 4, id = q[0] & 3, total = 0, k;
                for (k = 0; k < 16; k++) total += q[1 + k];
                build_huff(cls ? &ac[id] : &dc[id], q + 1, q + 17);
                q += 17 + total;
            }
        } else if (marker == 0xC0) { /* SOF0 */
            height = p[5] << 8 | p[6];
            width = p[7] << 8 | p[8];
            ncomp = p[9];
            if (ncomp != 3) return THP_ERROR;
            for (i = 0; i < 3; i++) {
                comp[i].h = p[11 + i * 3] >> 4;
                comp[i].v = p[11 + i * 3] & 15;
                comp[i].tq = p[12 + i * 3] & 3;
                if (comp[i].h > hmax) hmax = comp[i].h;
                if (comp[i].v > vmax) vmax = comp[i].v;
            }
        } else if (marker == 0xDA) { /* SOS: the data follows */
            for (i = 0; i < 3; i++) {
                comp[i].td = p[6 + i * 2] >> 4;
                comp[i].ta = p[6 + i * 2] & 15;
            }
            p += 2 + len;
            break;
        } else if (marker == 0xD9) {
            return THP_ERROR;
        }
        p += 2 + len;
    }
    if (!width || !height) return THP_ERROR;
    {
        uint32_t outs[3];
        int mcu_w = 8 * hmax, mcu_h = 8 * vmax, mx, my;
        Bits b;
        outs[0] = tileY;
        outs[1] = tileU;
        outs[2] = tileV;
        for (i = 0; i < 3; i++) {
            comp[i].width = (width * comp[i].h + hmax - 1) / hmax;
            comp[i].height = (height * comp[i].v + vmax - 1) / vmax;
            comp[i].out = outs[i];
            comp[i].dc = 0;
        }
        b.p = p;
        b.end = end;
        b.bits = 0;
        b.count = 0;
        for (my = 0; my < (height + mcu_h - 1) / mcu_h; my++) {
            for (mx = 0; mx < (width + mcu_w - 1) / mcu_w; mx++) {
                for (i = 0; i < 3; i++) {
                    Comp* c = &comp[i];
                    int bx, by;
                    for (by = 0; by < c->v; by++) {
                        for (bx = 0; bx < c->h; bx++) {
                            float coef[64];
                            uint8_t blk[64];
                            const uint8_t* q = qt[c->tq];
                            int s, k;
                            memset(coef, 0, sizeof(coef));
                            s = decode_huff(&b, &dc[c->td]);
                            if (s < 0) return THP_ERROR;
                            c->dc += extend(get_bits(&b, s), s);
                            coef[0] = (float)(c->dc * q[0]);
                            for (k = 1; k < 64;) {
                                int rs = decode_huff(&b, &ac[c->ta]), r, sz;
                                if (rs < 0) return THP_ERROR;
                                r = rs >> 4;
                                sz = rs & 15;
                                if (!sz) {
                                    if (r != 15) break; /* end of block */
                                    k += 16;
                                    continue;
                                }
                                k += r;
                                if (k > 63) return THP_ERROR;
                                coef[kZigzag[k]] = (float)(extend(get_bits(&b, sz), sz) * q[k]);
                                k++;
                            }
                            idct8x8(coef, blk);
                            store_block(c, (mx * c->h + bx) * 8, (my * c->v + by) * 8, blk);
                        }
                    }
                }
            }
        }
    }
    return THP_OK;
}

static int mode(void) {
    static int m = -1;
    if (m < 0) {
        const char* e = getenv("RECOMP_THP");
        m = !e ? 0 : !strcmp(e, "orig") ? 1 : !strcmp(e, "check") ? 2 : 0;
    }
    return m;
}

/* Runs both decoders and reports the largest difference per frame. */
static void check(ppc_t* c, uint32_t file, uint32_t ty, uint32_t tu, uint32_t tv) {
    static int frames;
    /* The planes' sizes, from the frame's SOF0. */
    const uint8_t* p = MA(file) + 2;
    int width = 0, height = 0, n, i, worst = 0, differ = 0;
    uint8_t* mine;
    while (p[0] == 0xFF && p[1] != 0xC0) p += 2 + (p[2] << 8 | p[3]);
    if (p[1] == 0xC0) {
        height = p[5] << 8 | p[6];
        width = p[7] << 8 | p[8];
    }
    n = width * height * 3 / 2;
    mine = (uint8_t*)malloc((size_t)n);
    decode_frame(file, ty, tu, tv);
    memcpy(mine, MA(ty), (size_t)width * height);
    memcpy(mine + width * height, MA(tu), (size_t)width * height / 4);
    memcpy(mine + width * height * 5 / 4, MA(tv), (size_t)width * height / 4);
    f_8028B590_orig(c);
    for (i = 0; i < n; i++) {
        uint8_t ref = i < width * height ? *MA(ty + i) : i < width * height * 5 / 4 ? *MA(tu + i - width * height)
                                                                                 : *MA(tv + i - width * height * 5 / 4);
        int d = abs((int)ref - mine[i]);
        if (d > worst) worst = d;
        if (d > 1) differ++;
    }
    fprintf(stderr, "thp check: frame %d (%dx%d): largest difference %d, %d of %d bytes differ by more than 1\n",
            frames++, width, height, worst, differ, n);
    free(mine);
}

/* s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work) */
void f_8028B590(ppc_t* c) {
    uint32_t file = c->r[3], ty = c->r[4], tu = c->r[5], tv = c->r[6];
    if (mode() == 1) {
        f_8028B590_orig(c);
        return;
    }
    if (mode() == 2) {
        check(c, file, ty, tu, tv);
        return;
    }
    if (decode_frame(file, ty, tu, tv) != THP_OK) {
        HLE_WARN("THP frame at %08X: native decode failed; using the game's decoder", file);
        f_8028B590_orig(c);
        return;
    }
    c->r[3] = THP_OK;
}
