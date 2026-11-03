/* GX texture formats, decoded to RGBA8.
 *
 * Textures are stored in tiles (blocks) of 32 bytes, row by row; the
 * block size depends on the format. RGBA8 tiles are 64 bytes: 32 bytes
 * of alpha/red followed by 32 bytes of green/blue. CMPR is DXT1-like,
 * with 8x8 tiles made of four 4x4 sub-blocks and big-endian colors. */
#include <string.h>

#include "gx.h"

enum {
    FMT_I4 = 0, FMT_I8 = 1, FMT_IA4 = 2, FMT_IA8 = 3, FMT_RGB565 = 4, FMT_RGB5A3 = 5, FMT_RGBA8 = 6,
    FMT_C4 = 8, FMT_C8 = 9, FMT_C14X2 = 10, FMT_CMPR = 14,
};

static void block_dims(int fmt, int* bw, int* bh, int* bytes) {
    switch (fmt) {
    case FMT_I4: case FMT_C4: case FMT_CMPR: *bw = 8; *bh = 8; *bytes = 32; break;
    case FMT_I8: case FMT_IA4: case FMT_C8: *bw = 8; *bh = 4; *bytes = 32; break;
    case FMT_RGBA8: *bw = 4; *bh = 4; *bytes = 64; break;
    default: *bw = 4; *bh = 4; *bytes = 32; break;
    }
}

int gx_texture_size(int fmt, int width, int height) {
    int bw, bh, bytes;
    switch (fmt) {
    case FMT_I4: case FMT_I8: case FMT_IA4: case FMT_IA8: case FMT_RGB565: case FMT_RGB5A3:
    case FMT_RGBA8: case FMT_C4: case FMT_C8: case FMT_C14X2: case FMT_CMPR: break;
    default: return -1;
    }
    block_dims(fmt, &bw, &bh, &bytes);
    return ((width + bw - 1) / bw) * ((height + bh - 1) / bh) * bytes;
}

static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void put(uint8_t* out, int width, int height, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* d;
    if (x >= width || y >= height) return;
    d = out + ((size_t)y * width + x) * 4;
    d[0] = r;
    d[1] = g;
    d[2] = b;
    d[3] = a;
}

static void rgb565(uint16_t v, uint8_t* c) {
    c[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
    c[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
    c[2] = (uint8_t)((v & 31) * 255 / 31);
    c[3] = 255;
}

static void rgb5a3(uint16_t v, uint8_t* c) {
    if (v & 0x8000) {
        c[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
        c[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
        c[2] = (uint8_t)((v & 31) * 255 / 31);
        c[3] = 255;
    } else {
        c[0] = (uint8_t)(((v >> 8) & 15) * 17);
        c[1] = (uint8_t)(((v >> 4) & 15) * 17);
        c[2] = (uint8_t)((v & 15) * 17);
        c[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7);
    }
}

/* A palette entry; palettes hold big-endian 16-bit entries. */
static void tlut_color(const uint8_t* tlut, int fmt, int index, uint8_t* c) {
    uint16_t v = be16(tlut + 2 * index);
    switch (fmt) {
    case 0: /* IA8: alpha in the high byte */
        c[0] = c[1] = c[2] = (uint8_t)(v & 0xFF);
        c[3] = (uint8_t)(v >> 8);
        break;
    case 1: rgb565(v, c); break;
    default: rgb5a3(v, c); break;
    }
}

static void decode_cmpr_sub(uint8_t* out, int width, int height, int x0, int y0, const uint8_t* src) {
    uint16_t c0 = be16(src), c1 = be16(src + 2);
    uint8_t pal[4][4];
    int x, y;
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    if (c0 > c1) {
        int k;
        for (k = 0; k < 3; k++) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        int k;
        for (k = 0; k < 3; k++) {
            pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = pal[2][k];
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    for (y = 0; y < 4; y++) {
        uint8_t bits = src[4 + y];
        for (x = 0; x < 4; x++) {
            const uint8_t* c = pal[(bits >> (6 - 2 * x)) & 3];
            put(out, width, height, x0 + x, y0 + y, c[0], c[1], c[2], c[3]);
        }
    }
}

/* Whether a CMPR level is opaque: no sub-block with a transparent texel
 * (index 3 where the first color isn't the larger). Then it is BC1 as it
 * is, in another order; a transparent texel isn't, as BC1 makes it black
 * where the Wii keeps the two colors' average. */
static int cmpr_opaque(const uint8_t* src, int width, int height) {
    int n = ((width + 7) / 8) * ((height + 7) / 8) * 4, i, y;
    for (i = 0; i < n; i++, src += 8) {
        if (be16(src) > be16(src + 2)) continue;
        for (y = 0; y < 4; y++) {
            uint8_t b = src[4 + y];
            if ((b & 0xC0) == 0xC0 || (b & 0x30) == 0x30 || (b & 0x0C) == 0x0C || (b & 0x03) == 0x03) return 0;
        }
    }
    return 1;
}

int gx_texture_pix(int fmt, const uint8_t* src, int width, int height, int levels, unsigned allowed) {
    int l;
    switch (fmt) {
    case FMT_I4: case FMT_I8:
        return allowed & (1u << GX_PIX_I8) ? GX_PIX_I8 : GX_PIX_RGBA8;
    case FMT_IA4: case FMT_IA8:
        return allowed & (1u << GX_PIX_IA8) ? GX_PIX_IA8 : GX_PIX_RGBA8;
    case FMT_CMPR:
        if (!(allowed & (1u << GX_PIX_BC1))) return GX_PIX_RGBA8;
        for (l = 0; l < levels; l++) {
            if (!cmpr_opaque(src, width, height)) return GX_PIX_RGBA8;
            src += gx_texture_size(fmt, width, height);
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
        }
        return GX_PIX_BC1;
    }
    return GX_PIX_RGBA8;
}

size_t gx_pix_size(int pix, int width, int height) {
    switch (pix) {
    case GX_PIX_I8: return (size_t)width * height;
    case GX_PIX_IA8: return (size_t)width * height * 2;
    case GX_PIX_BC1: return (size_t)((width + 3) / 4) * ((height + 3) / 4) * 8;
    }
    return (size_t)width * height * 4;
}

void gx_decode_texture_pix(uint8_t* out, const uint8_t* src, int fmt, int width, int height, int pix) {
    int bw, bh, bytes, bx, by, x, y;
    if (pix == GX_PIX_BC1) {
        /* Each 4x4 block, from sub-block (X & 1) + 2 (Y & 1) of 8x8 tile
         * (X / 2, Y / 2): colors little-endian, and each row's indices
         * first texel lowest. */
        int nx = (width + 3) / 4, ny = (height + 3) / 4, tiles = (width + 7) / 8;
        for (y = 0; y < ny; y++) {
            for (x = 0; x < nx; x++) {
                const uint8_t* b = src + ((y / 2) * tiles + x / 2) * 32 + ((y & 1) * 2 + (x & 1)) * 8;
                int r;
                out[0] = b[1];
                out[1] = b[0];
                out[2] = b[3];
                out[3] = b[2];
                for (r = 0; r < 4; r++) {
                    uint8_t v = b[4 + r];
                    out[4 + r] = (uint8_t)((v >> 6) | ((v >> 2) & 0x0C) | ((v << 2) & 0x30) | (v << 6));
                }
                out += 8;
            }
        }
        return;
    }
    block_dims(fmt, &bw, &bh, &bytes);
    for (by = 0; by < height; by += bh) {
        for (bx = 0; bx < width; bx += bw) {
            const uint8_t* b = src;
            src += bytes;
            for (y = 0; y < bh && by + y < height; y++) {
                for (x = 0; x < bw && bx + x < width; x++) {
                    size_t at = (size_t)(by + y) * width + bx + x;
                    switch (fmt) {
                    case FMT_I4: out[at] = (uint8_t)(((b[y * 4 + x / 2] >> ((x & 1) ? 0 : 4)) & 15) * 17); break;
                    case FMT_I8: out[at] = b[y * 8 + x]; break;
                    case FMT_IA4:
                        out[2 * at] = (uint8_t)((b[y * 8 + x] & 15) * 17);
                        out[2 * at + 1] = (uint8_t)((b[y * 8 + x] >> 4) * 17);
                        break;
                    case FMT_IA8:
                        out[2 * at] = b[(y * 4 + x) * 2 + 1];
                        out[2 * at + 1] = b[(y * 4 + x) * 2];
                        break;
                    }
                }
            }
        }
    }
}

void gx_decode_texture(uint8_t* out, const uint8_t* src, int fmt, int width, int height,
                       const uint8_t* tlut, int tlut_fmt) {
    int bw, bh, bytes, bx, by, x, y;
    block_dims(fmt, &bw, &bh, &bytes);
    for (by = 0; by < height; by += bh) {
        for (bx = 0; bx < width; bx += bw) {
            const uint8_t* b = src;
            src += bytes;
            switch (fmt) {
            case FMT_I4:
            case FMT_C4:
                for (y = 0; y < 8; y++) {
                    for (x = 0; x < 8; x++) {
                        int v = (b[y * 4 + x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                        uint8_t c[4];
                        if (fmt == FMT_C4) tlut_color(tlut, tlut_fmt, v, c);
                        else c[0] = c[1] = c[2] = c[3] = (uint8_t)(v * 17);
                        put(out, width, height, bx + x, by + y, c[0], c[1], c[2], c[3]);
                    }
                }
                break;
            case FMT_I8:
            case FMT_C8:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 8; x++) {
                        uint8_t v = b[y * 8 + x], c[4];
                        if (fmt == FMT_C8) tlut_color(tlut, tlut_fmt, v, c);
                        else c[0] = c[1] = c[2] = c[3] = v;
                        put(out, width, height, bx + x, by + y, c[0], c[1], c[2], c[3]);
                    }
                }
                break;
            case FMT_IA4:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 8; x++) {
                        uint8_t v = b[y * 8 + x], i = (uint8_t)((v & 15) * 17), a = (uint8_t)((v >> 4) * 17);
                        put(out, width, height, bx + x, by + y, i, i, i, a);
                    }
                }
                break;
            case FMT_IA8:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        const uint8_t* p = b + (y * 4 + x) * 2;
                        put(out, width, height, bx + x, by + y, p[1], p[1], p[1], p[0]);
                    }
                }
                break;
            case FMT_RGB565:
            case FMT_RGB5A3:
            case FMT_C14X2:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        uint16_t v = be16(b + (y * 4 + x) * 2);
                        uint8_t c[4];
                        if (fmt == FMT_RGB565) rgb565(v, c);
                        else if (fmt == FMT_RGB5A3) rgb5a3(v, c);
                        else tlut_color(tlut, tlut_fmt, v & 0x3FFF, c);
                        put(out, width, height, bx + x, by + y, c[0], c[1], c[2], c[3]);
                    }
                }
                break;
            case FMT_RGBA8:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        int i = (y * 4 + x) * 2;
                        put(out, width, height, bx + x, by + y, b[i + 1], b[32 + i], b[32 + i + 1], b[i]);
                    }
                }
                break;
            case FMT_CMPR:
                decode_cmpr_sub(out, width, height, bx, by, b);
                decode_cmpr_sub(out, width, height, bx + 4, by, b + 8);
                decode_cmpr_sub(out, width, height, bx, by + 4, b + 16);
                decode_cmpr_sub(out, width, height, bx + 4, by + 4, b + 24);
                break;
            }
        }
    }
}
