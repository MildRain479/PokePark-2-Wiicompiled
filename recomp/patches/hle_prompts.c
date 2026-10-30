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

/* Redraws the glyphs if what they should show has changed. Called on each
 * report of the remote (hle_wpad.c). */
void hle_prompts_update(void) {
    uint32_t gen;
    int i;
    if (!sFontCount || !g_host_prompt_generation) return;
    gen = g_host_prompt_generation();
    if (gen == sGeneration) return;
    sGeneration = gen;
    for (i = 0; i < sFontCount; i++) draw_icons(&sFonts[i]);
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
