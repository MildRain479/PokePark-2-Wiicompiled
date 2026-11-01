/* Button prompts. The game's text shows a Wii Remote button with a tag
 * (0x1A, its size, group 5, then the button) that draws a character of
 * its font: U+E040 + n, the Wii's button glyphs (power, the d-pad, A, B,
 * HOME, +, -, 1, 2). Here those glyphs are redrawn, in the font itself,
 * as icons of what the player has bound to each button, on the keyboard
 * and mouse or the controller, whichever was used last (host_input.h);
 * a button with no icon keeps the game's glyph. They are redrawn when the
 * font is loaded and whenever the bindings or the device change; text
 * drawn after that shows them.
 *
 * A font (RFNT) is a header and blocks; its glyphs are cells of 4-bit
 * sheets (TGLP: cell size, baseline, sheet layout, and once loaded a
 * pointer to the sheets), found through its character maps (CMAP). */
#include <string.h>

#include "hle.h"
#include "host_input.h"

int (*g_host_prompt_icon)(uint32_t button, uint8_t* coverage, int w, int h);
uint32_t (*g_host_prompt_generation)(void);

#define FIRST_ICON 0xE041 /* the d-pad; then A, B, HOME, +, -, 1, 2 */
#define ICONS 8
static const uint32_t kButton[ICONS] = {
    HOST_PAD_SCREEN_UP, HOST_PAD_A, HOST_PAD_B, HOST_PAD_HOME, HOST_PAD_PLUS, HOST_PAD_MINUS, HOST_PAD_1, HOST_PAD_2,
};

#define MAX_FONTS 4
#define MAX_CELL (64 * 64)

typedef struct {
    uint32_t font, sheets;
    int cw, ch, baseline, sheetSize, perRow, perSheet, sheetWidth, fmt;
    int glyph[ICONS];          /* glyph index, or -1 */
    uint8_t* original[ICONS];  /* the game's pixels, 0-15 per pixel, cw x ch */
} Font;

static Font sFonts[MAX_FONTS];
static int sFontCount;
static uint32_t sGeneration = 0xFFFFFFFFu;

/* A pixel of a 4-bit (I4) sheet: 8x8 tiles of 32 bytes, row by row. */
static uint32_t i4_byte(const Font* f, int sheet, int x, int y) {
    int tiles = f->sheetWidth / 8;
    return f->sheets + (uint32_t)sheet * f->sheetSize + (uint32_t)(((y / 8) * tiles + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2);
}

static int get_px(const Font* f, int sheet, int x, int y) {
    uint8_t b = LD8(i4_byte(f, sheet, x, y));
    return x % 2 ? b & 15 : b >> 4;
}

static void set_px(const Font* f, int sheet, int x, int y, int v) {
    uint32_t a = i4_byte(f, sheet, x, y);
    uint8_t b = LD8(a);
    ST8(a, (uint8_t)(x % 2 ? (b & 0xF0) | v : (b & 0x0F) | (v << 4)));
}

/* Where a glyph's cell is: its sheet and top-left pixel. */
static void cell_of(const Font* f, int glyph, int* sheet, int* x0, int* y0) {
    int k = glyph % f->perSheet;
    *sheet = glyph / f->perSheet;
    *x0 = (k % f->perRow) * (f->cw + 1);
    *y0 = (k / f->perRow) * (f->ch + 1);
}

/* The glyph a character maps to in font data at base (block by block). */
static int glyph_of(uint32_t base, uint32_t size, uint16_t code) {
    uint32_t pos = base + LD16(base + 0xC), end = base + size;
    while (pos + 8 <= end) {
        uint32_t blockSize = LD32(pos + 4);
        if (LD32(pos) == 0x434D4150) { /* CMAP */
            uint16_t lo = LD16(pos + 8), hi = LD16(pos + 10), method = LD16(pos + 12);
            if (code >= lo && code <= hi) {
                if (method == 0) return LD16(pos + 0x14) + (code - lo); /* direct */
                if (method == 1) return (int16_t)LD16(pos + 0x14 + 2 * (code - lo)) == -1 ? -1 : LD16(pos + 0x14 + 2 * (code - lo));
                if (method == 2) { /* scan */
                    int n = LD16(pos + 0x14), i;
                    for (i = 0; i < n; i++) {
                        if (LD16(pos + 0x16 + 4 * i) == code) return LD16(pos + 0x18 + 4 * i);
                    }
                }
            }
        }
        if (blockSize < 8) break;
        pos += blockSize;
    }
    return -1;
}

static void draw_icons(Font* f) {
    static uint8_t cov[MAX_CELL];
    int i, x, y;
    for (i = 0; i < ICONS; i++) {
        int sheet, x0, y0, s, ox, oy, drew = 0;
        if (f->glyph[i] < 0) continue;
        cell_of(f, f->glyph[i], &sheet, &x0, &y0);
        if (!f->original[i]) {
            f->original[i] = (uint8_t*)malloc((size_t)f->cw * f->ch);
            for (y = 0; y < f->ch; y++) {
                for (x = 0; x < f->cw; x++) f->original[i][y * f->cw + x] = (uint8_t)get_px(f, sheet, x0 + x, y0 + y);
            }
        }
        /* Square, up to the baseline, as the Wii's glyphs sit. */
        s = f->baseline < f->cw ? f->baseline : f->cw;
        s -= 2;
        ox = (f->cw - s) / 2;
        oy = f->baseline - s - 1;
        if (oy < 0) oy = 0;
        if (s > 0 && s * s <= MAX_CELL && g_host_prompt_icon) drew = g_host_prompt_icon(kButton[i], cov, s, s);
        for (y = 0; y < f->ch; y++) {
            for (x = 0; x < f->cw; x++) {
                int v = f->original[i][y * f->cw + x];
                if (drew) {
                    int ix = x - ox, iy = y - oy;
                    v = ix >= 0 && iy >= 0 && ix < s && iy < s ? (cov[iy * s + ix] * 15 + 127) / 255 : 0;
                }
                set_px(f, sheet, x0 + x, y0 + y, v);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* The menus' button bar ("+ Skip", "1 Back", "2 Next"): its buttons are
 * pictures, in a texture named KeyButton (256x256, 8-bit colour indices
 * into 192 RGB5A3 colours) in each menu's model (lobbies, start menu,
 * pause, photos). Each button there, a silver disc in a coloured ring, is
 * redrawn as the icon for what is bound to it: a white key in the ring's
 * colour with a white edge, as the bar's words are drawn, in the colours
 * the texture has. */

#define BAR_SIZE 256
#define BAR_COLOURS 192
#define MAX_BARS 8
#define BAR_EDGE 2 /* the white edge around an icon */

static const struct {
    int x0, y0, x1, y1; /* inclusive */
    uint32_t button;
} kBarButton[] = {
    {2, 2, 38, 37, HOST_PAD_1},       {2, 42, 38, 77, HOST_PAD_2},      {1, 82, 37, 117, HOST_PAD_PLUS},
    {1, 122, 37, 157, HOST_PAD_MINUS}, {3, 164, 36, 195, HOST_PAD_B},   {2, 202, 37, 237, HOST_PAD_A},
    {42, 202, 77, 237, HOST_PAD_SCREEN_UP}, /* the d-pad */
};
#define BAR_BUTTONS ((int)(sizeof(kBarButton) / sizeof(kBarButton[0])))

typedef struct {
    uint32_t tex, image;
    uint8_t rgba[BAR_COLOURS][4];
    uint8_t* original; /* the game's indices, BAR_SIZE x BAR_SIZE */
} Bar;

static Bar sBars[MAX_BARS];
static int sBarCount;

/* A texel of an 8-bit (CI8) texture: 8x4 tiles of 32 bytes, row by row. */
static uint32_t ci8_byte(uint32_t image, int x, int y) {
    return image + (uint32_t)(((y / 4) * (BAR_SIZE / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8);
}

/* Whether the TEX0 at tex is still a bar texture (its memory may have
 * been freed and reused). */
static int is_bar(uint32_t tex) {
    uint32_t name = tex + LD32(tex + 0x14);
    return LD32(tex) == 0x54455830 && LD32(name) == 0x4B657942 && LD32(name + 4) == 0x7574746F && /* KeyButto */
           LD8(name + 8) == 'n' && LD8(name + 9) == 0 && LD16(tex + 0x1C) == BAR_SIZE && LD16(tex + 0x1E) == BAR_SIZE &&
           LD32(tex + 0x20) == 9; /* CI8 */
}

/* The colour nearest r g b a, weighing its colour by how opaque it is. */
static int nearest(const Bar* b, int r, int g, int bl, int a) {
    int i, best = 0;
    long bestD = -1;
    for (i = 0; i < BAR_COLOURS; i++) {
        const uint8_t* p = b->rgba[i];
        long da = (long)(p[3] - a) * 2, dr = p[0] - r, dg = p[1] - g, db = p[2] - bl;
        long d = da * da + (dr * dr + dg * dg + db * db) * (a + p[3]) / 510;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

static void draw_bar_button(Bar* b, int k) {
    enum { MAX_BOX = 40 };
    static uint8_t cov[MAX_BOX * MAX_BOX], inside[MAX_BOX * MAX_BOX];
    static int stack[MAX_BOX * MAX_BOX];
    int x0 = kBarButton[k].x0, y0 = kBarButton[k].y0;
    int w = kBarButton[k].x1 - x0 + 1, h = kBarButton[k].y1 - y0 + 1;
    int s = (w < h ? w : h) - 2 * BAR_EDGE, ox = (w - s) / 2, oy = (h - s) / 2;
    int x, y, i, n = 0, ring = -1, count[BAR_COLOURS] = {0}, transparent;
    if (s <= 0 || w > MAX_BOX || h > MAX_BOX) return;
    /* On the keyboard the d-pad would be four keys too small to read here:
     * the game's cross, which says "directions" as well, stays. (The
     * generation's low bit: the controller was used last.) */
    if (!g_host_prompt_icon || (kBarButton[k].button == HOST_PAD_SCREEN_UP && !(sGeneration & 1)) ||
        !g_host_prompt_icon(kBarButton[k].button, cov, s, s)) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) ST8(ci8_byte(b->image, x0 + x, y0 + y), b->original[(y0 + y) * BAR_SIZE + x0 + x]);
        }
        return;
    }
    /* The ring's colour: the commonest strong one, opaque, in the game's button. */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) count[b->original[(y0 + y) * BAR_SIZE + x0 + x]]++;
    }
    for (i = 0; i < BAR_COLOURS; i++) {
        const uint8_t* p = b->rgba[i];
        int hi = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        int lo = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
        if (p[3] == 255 && hi - lo > 100 && (ring < 0 || count[i] > count[ring])) ring = i;
    }
    if (ring < 0) return;
    transparent = nearest(b, 0, 0, 0, 0);
    /* Inside the key: what can't be reached from the box's edge without
     * crossing its outline. */
    memset(inside, 1, (size_t)s * s);
    for (i = 0; i < s * s; i++) {
        x = i % s;
        y = i / s;
        if ((x == 0 || y == 0 || x == s - 1 || y == s - 1) && cov[i] < 128) {
            inside[i] = 0;
            stack[n++] = i;
        }
    }
    while (n) {
        static const int kStep[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        int at = stack[--n], d;
        for (d = 0; d < 4; d++) {
            int nx = at % s + kStep[d][0], ny = at / s + kStep[d][1], j = ny * s + nx;
            if (nx < 0 || ny < 0 || nx >= s || ny >= s || !inside[j] || cov[j] >= 128) continue;
            inside[j] = 0;
            stack[n++] = j;
        }
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int ix = x - ox, iy = y - oy, line = 0, white = 0, dx, dy, r, g, bl, a;
            if (ix >= 0 && iy >= 0 && ix < s && iy < s) {
                line = cov[iy * s + ix];
                if (inside[iy * s + ix]) white = 255;
            }
            /* The edge: the key, grown by BAR_EDGE. */
            for (dy = -BAR_EDGE; dy <= BAR_EDGE; dy++) {
                for (dx = -BAR_EDGE; dx <= BAR_EDGE; dx++) {
                    int jx = ix + dx, jy = iy + dy, v;
                    if (dx * dx + dy * dy > BAR_EDGE * BAR_EDGE + 1 || jx < 0 || jy < 0 || jx >= s || jy >= s) continue;
                    v = inside[jy * s + jx] ? 255 : cov[jy * s + jx];
                    if (v > white) white = v;
                }
            }
            /* Solid or clear: the texture's part-clear colours are the ring's,
             * which would fringe the white edge. */
            a = line > white ? line : white;
            a = a >= 96 ? 255 : 0;
            r = (255 * (255 - line) + b->rgba[ring][0] * line) / 255;
            g = (255 * (255 - line) + b->rgba[ring][1] * line) / 255;
            bl = (255 * (255 - line) + b->rgba[ring][2] * line) / 255;
            ST8(ci8_byte(b->image, x0 + x, y0 + y), (uint8_t)(a ? nearest(b, r, g, bl, a) : transparent));
        }
    }
}

static void draw_bars(void) {
    int i, k;
    for (i = 0; i < sBarCount; i++) {
        if (!is_bar(sBars[i].tex)) { /* gone */
            free(sBars[i].original);
            sBars[i--] = sBars[--sBarCount];
            continue;
        }
        for (k = 0; k < BAR_BUTTONS; k++) draw_bar_button(&sBars[i], k);
    }
}

/* void ResTex::Init(): a texture of a model file (TEX0), as the file is
 * set up after loading. */
void f_801B3090(ppc_t* c) {
    uint32_t tex = c->r[3] ? LD32(c->r[3]) : 0, file, size, pos, plt = 0, pal;
    Bar* b;
    int i, x, y;
    f_801B3090_orig(c);
    if (!tex || !is_bar(tex)) return;
    for (i = 0; i < sBarCount; i++) {
        if (sBars[i].tex == tex) break;
    }
    if (i == sBarCount) {
        /* Its colours: the PLT0 of the same name, in the same file. */
        file = tex + LD32(tex + 0xC);
        if (LD32(file) != 0x62726573) return; /* bres */
        size = LD32(file + 8);
        for (pos = file; pos + 0x20 <= file + size; pos += 4) {
            if (LD32(pos) == 0x504C5430) { /* PLT0 */
                uint32_t name = pos + LD32(pos + 0x14);
                if (LD32(name) == 0x4B657942 && LD32(name + 4) == 0x7574746F && LD8(name + 8) == 'n' && !LD8(name + 9)) {
                    plt = pos;
                    break;
                }
            }
        }
        if (!plt || LD32(plt + 0x18) != 2 || LD16(plt + 0x1C) < BAR_COLOURS || sBarCount == MAX_BARS) return;
        b = &sBars[sBarCount];
        b->original = (uint8_t*)malloc(BAR_SIZE * BAR_SIZE);
        if (!b->original) return;
        b->tex = tex;
        b->image = tex + LD32(tex + 0x10);
        pal = plt + LD32(plt + 0x10);
        for (x = 0; x < BAR_COLOURS; x++) {
            uint16_t v = LD16(pal + 2 * x);
            uint8_t* p = b->rgba[x];
            if (v & 0x8000) {
                p[0] = (uint8_t)((v >> 10 & 31) * 255 / 31);
                p[1] = (uint8_t)((v >> 5 & 31) * 255 / 31);
                p[2] = (uint8_t)((v & 31) * 255 / 31);
                p[3] = 255;
            } else {
                p[0] = (uint8_t)((v >> 8 & 15) * 17);
                p[1] = (uint8_t)((v >> 4 & 15) * 17);
                p[2] = (uint8_t)((v & 15) * 17);
                p[3] = (uint8_t)((v >> 12 & 7) * 255 / 7);
            }
        }
        sBarCount++;
        HLE_LOG("prompts: button bar texture %08X", tex);
    }
    /* Loaded again (or for the first time): as the game has it. */
    b = &sBars[i];
    for (y = 0; y < BAR_SIZE; y++) {
        for (x = 0; x < BAR_SIZE; x++) b->original[y * BAR_SIZE + x] = LD8(ci8_byte(b->image, x, y));
    }
    sGeneration = 0xFFFFFFFFu; /* draw it */
}

/* Redraws the glyphs and the bars if what they should show has changed.
 * Called on each report of the remote (hle_wpad.c). */
void hle_prompts_update(void) {
    uint32_t gen;
    int i;
    if ((!sFontCount && !sBarCount) || !g_host_prompt_generation) return;
    gen = g_host_prompt_generation();
    if (gen == sGeneration) return;
    sGeneration = gen;
    for (i = 0; i < sFontCount; i++) draw_icons(&sFonts[i]);
    draw_bars();
}

/* void ResFontBase::SetResourceBuffer(void* font, FontInformation*): a
 * font, loaded and its offsets made pointers. */
void f_801FA730(ppc_t* c) {
    uint32_t base = c->r[4], size, pos, image;
    Font* f;
    int i, have = 0;
    f_801FA730_orig(c);
    /* RFNT, or RFNU once unpacked (its offsets made pointers). */
    if (!base || (LD32(base) != 0x52464E54 && LD32(base) != 0x52464E55)) return;
    for (i = 0; i < sFontCount; i++) {
        if (sFonts[i].font == base) return;
    }
    if (sFontCount == MAX_FONTS) return;
    size = LD32(base + 8);
    for (pos = base + LD16(base + 0xC); pos + 8 <= base + size && LD32(pos) != 0x54474C50; pos += LD32(pos + 4)) { /* TGLP */
        if (LD32(pos + 4) < 8) return;
    }
    if (pos + 0x20 > base + size) return;
    f = &sFonts[sFontCount];
    memset(f, 0, sizeof(*f));
    f->font = base;
    f->cw = LD8(pos + 8);
    f->ch = LD8(pos + 9);
    f->baseline = (int8_t)LD8(pos + 10);
    f->sheetSize = (int)LD32(pos + 12);
    f->fmt = LD16(pos + 18);
    f->perRow = LD16(pos + 20);
    f->perSheet = LD16(pos + 20) * LD16(pos + 22);
    f->sheetWidth = LD16(pos + 24);
    image = LD32(pos + 28);
    f->sheets = image >= 0x80000000u ? image : base + image;
    if (f->fmt != 0 || f->cw * f->ch > MAX_CELL || !f->perSheet) return; /* I4 only */
    for (i = 0; i < ICONS; i++) {
        f->glyph[i] = glyph_of(base, size, (uint16_t)(FIRST_ICON + i));
        if (f->glyph[i] >= 0) have = 1;
    }
    if (!have) return;
    sFontCount++;
    HLE_LOG("prompts: font %08X, cells %dx%d, button glyphs from %d", base, f->cw, f->ch, f->glyph[0]);
    sGeneration = 0xFFFFFFFFu; /* draw them */
}
