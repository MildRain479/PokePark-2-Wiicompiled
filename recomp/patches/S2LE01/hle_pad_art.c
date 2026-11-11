/* The PokePark Pad's pictures, for the Options book drawn beside it
 * (options.c): the textures of the Pad's layouts, from the disc's
 * Archive/MenuField.dac (LZ11-compressed, a U8 archive holding
 * arc/timg/<name>.tpl). It's read on the game's thread the first time the
 * Pad or the save file screen is up (hle_pad_art_load), and each texture
 * is decoded to 0xAARRGGBB when Options first asks for it.
 *
 * For the friends list on a second screen (hle_friends.c), the Pokémon's
 * icons (Archive/PokemonIcon.dac, Ic<name>.tpl) and their English names
 * (Archive/ResidentMessage.dac, Pokemon.msbt, labelled NAME_<NAME>) are
 * read alike. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"

enum { ARC_MENU, ARC_ICONS, ARC_MESSAGES, ARCS };
static const char* const kArcPath[ARCS] = {"Archive/MenuField.dac", "Archive/PokemonIcon.dac",
                                           "Archive/ResidentMessage.dac"};
static uint8_t* volatile sArcs[ARCS]; /* the archives, decompressed */
static uint32_t sArcSizes[ARCS];
static int sTried;
#define sArc sArcs[ARC_MENU]

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t be16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }

/* LZ11, as the DS and Wii games pack their files. */
static uint8_t* lz11(const uint8_t* in, uint32_t inSize, uint32_t* outSize) {
    uint32_t n = in[1] | in[2] << 8 | in[3] << 16, p = 4, o = 0;
    uint8_t* out;
    if (in[0] != 0x11) return NULL;
    if (!n && inSize >= 8) {
        n = in[4] | in[5] << 8 | in[6] << 16 | (uint32_t)in[7] << 24;
        p = 8;
    }
    out = (uint8_t*)malloc(n ? n : 1);
    if (!out) return NULL;
    while (o < n && p < inSize) {
        int flags = in[p++], b;
        for (b = 0; b < 8 && o < n; b++) {
            if (flags & (0x80 >> b)) {
                uint32_t x, len, dist;
                if (p + 1 >= inSize) break;
                x = in[p];
                if (x >> 4 == 0) {
                    len = ((x & 15) << 4 | in[p + 1] >> 4) + 0x11;
                    dist = ((in[p + 1] & 15) << 8 | in[p + 2]) + 1;
                    p += 3;
                } else if (x >> 4 == 1) {
                    len = ((x & 15) << 12 | in[p + 1] << 4 | in[p + 2] >> 4) + 0x111;
                    dist = ((in[p + 2] & 15) << 8 | in[p + 3]) + 1;
                    p += 4;
                } else {
                    len = (x >> 4) + 1;
                    dist = ((x & 15) << 8 | in[p + 1]) + 1;
                    p += 2;
                }
                if (dist > o) break;
                for (; len && o < n; len--, o++) out[o] = out[o - dist];
            } else {
                out[o++] = in[p++];
            }
        }
    }
    *outSize = n;
    return out;
}

void hle_pad_art_load(void) {
    uint32_t size, n;
    uint8_t *packed, *arc;
    int k;
    if (sTried) return;
    sTried = 1;
    /* The menu's last, as its being there says all are. */
    for (k = ARCS - 1; k >= 0; k--) {
        packed = hle_dvd_load(kArcPath[k], &size);
        if (!packed) {
            HLE_WARN("options: no %s for the Pad's pictures", kArcPath[k]);
            continue;
        }
        arc = lz11(packed, size, &n);
        free(packed);
        if (!arc || n < 0x20 || be32(arc) != 0x55AA382D) {
            free(arc);
            HLE_WARN("options: %s isn't an LZ11 U8 archive", kArcPath[k]);
            continue;
        }
        sArcSizes[k] = n;
        sArcs[k] = arc;
    }
}

/* A file of archive k by its name (they're all named apart). */
static const uint8_t* arc_file_of(int k, const char* name, uint32_t* size) {
    const uint8_t* a = sArcs[k];
    uint32_t root, count, strs, i, sArcSize = sArcSizes[k];
    if (!a) return NULL;
    root = be32(a + 4);
    count = be32(a + root + 8);
    strs = root + count * 12;
    if (strs > sArcSize) return NULL;
    for (i = 1; i < count; i++) {
        const uint8_t* e = a + root + i * 12;
        uint32_t off = be32(e + 4), len = be32(e + 8);
        if (e[0] || strs + (be32(e) & 0xFFFFFF) >= sArcSize) continue;
        if (!strcmp((const char*)a + strs + (be32(e) & 0xFFFFFF), name) && off + len <= sArcSize) {
            *size = len;
            return a + off;
        }
    }
    return NULL;
}

static const uint8_t* arc_file(const char* name, uint32_t* size) { return arc_file_of(ARC_MENU, name, size); }

/* ------------------------------------------------------------------------ */
/* TPL textures, to 0xAARRGGBB */

static uint32_t argb(int a, int r, int g, int b) { return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b; }
static uint32_t rgb565(uint32_t v) { return argb(255, (v >> 11 & 31) * 255 / 31, (v >> 5 & 63) * 255 / 63, (v & 31) * 255 / 31); }
static uint32_t rgb5a3(uint32_t v) {
    if (v & 0x8000) return argb(255, (v >> 10 & 31) * 255 / 31, (v >> 5 & 31) * 255 / 31, (v & 31) * 255 / 31);
    return argb((v >> 12 & 7) * 255 / 7, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17);
}

static uint32_t mix(uint32_t a, uint32_t b, int wa, int wb) {
    int k, d = wa + wb;
    uint32_t o = 0;
    for (k = 0; k < 32; k += 8) o |= (((a >> k & 255) * wa + (b >> k & 255) * wb) / d) << k;
    return o;
}

static uint32_t* decode_tpl(const uint8_t* t, uint32_t size, int* w, int* h) {
    uint32_t img, pal, fmt, off, x, y, pal8[256];
    int bw, bh, bits, W, H, bx, by;
    uint32_t* px;
    const uint8_t* d;
    if (size < 12 || be32(t) != 0x0020AF30) return NULL;
    img = be32(t + be32(t + 8));
    pal = be32(t + be32(t + 8) + 4);
    H = (int)be16(t + img);
    W = (int)be16(t + img + 2);
    fmt = be32(t + img + 4);
    off = be32(t + img + 8);
    if (pal) {
        uint32_t n = be16(t + pal), pf = be32(t + pal + 4), po = be32(t + pal + 8), i;
        for (i = 0; i < n && i < 256; i++) {
            uint32_t v = be16(t + po + i * 2);
            pal8[i] = pf == 0 ? argb(v & 255, v >> 8, v >> 8, v >> 8) : pf == 1 ? rgb565(v) : rgb5a3(v);
        }
    }
    switch (fmt) {
    case 0: case 8: case 14: bw = 8; bh = 8; bits = 4; break;
    case 1: case 2: case 9: bw = 8; bh = 4; bits = 8; break;
    case 3: case 4: case 5: bw = 4; bh = 4; bits = 16; break;
    case 6: bw = 4; bh = 4; bits = 32; break;
    default: return NULL;
    }
    px = (uint32_t*)calloc((size_t)W * H, 4);
    if (!px) return NULL;
    d = t + off;
    for (by = 0; by < H; by += bh) {
        for (bx = 0; bx < W; bx += bw) {
            uint32_t blk[64];
            int i, n = bw * bh;
            if (d + n * bits / 8 > t + size) break;
            if (fmt == 14) { /* four 4x4 DXT1 blocks */
                int s;
                for (s = 0; s < 4; s++, d += 8) {
                    uint32_t c0 = be16(d), c1 = be16(d + 2), bitsw = be32(d + 4), p[4];
                    p[0] = rgb565(c0);
                    p[1] = rgb565(c1);
                    if (c0 > c1) {
                        p[2] = mix(p[0], p[1], 2, 1);
                        p[3] = mix(p[0], p[1], 1, 2);
                    } else {
                        p[2] = mix(p[0], p[1], 1, 1);
                        p[3] = 0;
                    }
                    for (i = 0; i < 16; i++) blk[((s >> 1) * 4 + i / 4) * 8 + (s & 1) * 4 + i % 4] = p[bitsw >> (30 - 2 * i) & 3];
                }
            } else if (fmt == 6) { /* AR planes, then GB */
                for (i = 0; i < 16; i++) blk[i] = argb(d[i * 2], d[i * 2 + 1], d[32 + i * 2], d[33 + i * 2]);
                d += 64;
            } else {
                for (i = 0; i < n; i++) {
                    uint32_t v;
                    if (bits == 4) v = i & 1 ? d[i / 2] & 15 : d[i / 2] >> 4;
                    else if (bits == 8) v = d[i];
                    else v = be16(d + i * 2);
                    switch (fmt) {
                    case 0: blk[i] = argb(255, v * 17, v * 17, v * 17); break;
                    case 1: blk[i] = argb(255, v, v, v); break;
                    case 2: blk[i] = argb((v >> 4) * 17, (v & 15) * 17, (v & 15) * 17, (v & 15) * 17); break;
                    case 3: blk[i] = argb(v >> 8, v & 255, v & 255, v & 255); break;
                    case 4: blk[i] = rgb565(v); break;
                    case 5: blk[i] = rgb5a3(v); break;
                    default: blk[i] = pal ? pal8[v & 255] : 0; break; /* C4, C8 */
                    }
                }
                d += n * bits / 8;
            }
            for (y = 0; y < (uint32_t)bh; y++)
                for (x = 0; x < (uint32_t)bw; x++)
                    if (bx + (int)x < W && by + (int)y < H) px[(by + y) * W + bx + x] = blk[y * bw + x];
        }
    }
    *w = W;
    *h = H;
    return px;
}

/* A texture of the Pad's by its name ("PauseTopBookKanagu"), decoded once;
 * NULL until the archive is read, or if there's none so named. (alpha is
 * the first game's, whose textures kept their alpha apart.) */
const uint32_t* hle_pause_image(const char* name, const char* alpha, int* w, int* h) {
    static struct {
        char name[40];
        uint32_t* px;
        int w, h;
    } cache[64];
    static int n;
    char file[48];
    const uint8_t* t;
    uint32_t size;
    int i;
    (void)alpha;
    for (i = 0; i < n; i++) {
        if (!strcmp(cache[i].name, name)) {
            *w = cache[i].w;
            *h = cache[i].h;
            return cache[i].px;
        }
    }
    if (!sArc || n == 64 || strlen(name) + 5 > sizeof(file)) return NULL;
    snprintf(file, sizeof(file), "%s.tpl", name);
    if (!(t = arc_file(file, &size))) return NULL;
    snprintf(cache[n].name, sizeof(cache[n].name), "%s", name);
    cache[n].px = decode_tpl(t, size, &cache[n].w, &cache[n].h);
    *w = cache[n].w;
    *h = cache[n].h;
    return cache[n++].px;
}

/* ------------------------------------------------------------------------ */
/* The friends list's: a Pokémon's icon and English name, by its internal
 * name ("Mijumaru") */

/* Its icon (64 x 64), decoded once; NULL if it has none or the archive
 * isn't read yet. */
const uint32_t* hle_friend_icon(const char* name, int* w, int* h) {
    static struct {
        char name[32];
        uint32_t* px;
        int w, h;
    } cache[300];
    static int n;
    char file[48];
    const uint8_t* t;
    uint32_t size;
    int i;
    for (i = 0; i < n; i++) {
        if (!strcmp(cache[i].name, name)) {
            *w = cache[i].w;
            *h = cache[i].h;
            return cache[i].px;
        }
    }
    if (!sArcs[ARC_ICONS] || n == 300 || strlen(name) + 7 > sizeof(file)) return NULL;
    snprintf(file, sizeof(file), "Ic%s.tpl", name);
    if (!(t = arc_file_of(ARC_ICONS, file, &size))) return NULL;
    snprintf(cache[n].name, sizeof(cache[n].name), "%s", name);
    cache[n].px = decode_tpl(t, size, &cache[n].w, &cache[n].h);
    *w = cache[n].w;
    *h = cache[n].h;
    return cache[n++].px;
}

/* Whether it has an icon, without decoding it. */
int hle_friend_has_icon(const char* name) {
    char file[48];
    uint32_t size;
    if (strlen(name) + 7 > sizeof(file)) return 0;
    snprintf(file, sizeof(file), "Ic%s.tpl", name);
    return arc_file_of(ARC_ICONS, file, &size) != NULL;
}

/* Its English name, in the font's Latin-1, into out: 0 if the messages
 * have none labelled for it (MSBT: LBL1's labels give TXT2's texts, in
 * UTF-16, with tags 0x0E group, kind, size and data). */
int hle_friend_name(const char* name, char* out, int size) {
    const uint8_t *m, *lbl = NULL, *txt = NULL;
    uint32_t msize, p, sections, s, i, k, want = ~0u;
    char label[48];
    if (!(m = arc_file_of(ARC_MESSAGES, "Pokemon.msbt", &msize)) || msize < 0x20 || m[8] != 0xFE) return 0;
    snprintf(label, sizeof(label), "NAME_%s", name);
    for (i = 5; label[i]; i++) label[i] = (char)toupper((unsigned char)label[i]);
    sections = be16(m + 0x0E);
    for (p = 0x20, s = 0; s < sections && p + 16 <= msize; s++) {
        uint32_t len = be32(m + p + 4);
        if (p + 16 + len > msize) break;
        if (!memcmp(m + p, "LBL1", 4)) lbl = m + p + 16;
        if (!memcmp(m + p, "TXT2", 4)) txt = m + p + 16;
        p += 16 + ((len + 15) & ~15u);
    }
    if (!lbl || !txt) return 0;
    for (i = 0; i < be32(lbl) && want == ~0u; i++) {
        const uint8_t* e = lbl + be32(lbl + 8 + i * 8);
        for (k = 0; k < be32(lbl + 4 + i * 8); k++) {
            uint32_t ln = e[0];
            if (ln == strlen(label) && !memcmp(e + 1, label, ln)) {
                want = be32(e + 1 + ln);
                break;
            }
            e += 5 + ln;
        }
    }
    if (want == ~0u || want >= be32(txt)) return 0;
    {
        const uint8_t* t = txt + be32(txt + 4 + want * 4);
        int o = 0;
        for (;;) {
            uint32_t ch = be16(t);
            t += 2;
            if (!ch) break;
            if (ch == 0x0E) { /* a tag: group, kind, its data's size, its data */
                t += 4 + be16(t + 4) + 2;
                continue;
            }
            if (ch >= 32 && o < size - 1) out[o++] = (char)(ch < 256 ? ch : '?');
        }
        out[o] = 0;
        return o > 0;
    }
}
