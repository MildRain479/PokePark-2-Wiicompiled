/* The Wii system font. OSInitFont reads it from the console's boot ROM
 * over EXI (ReadFont unpacks it into the caller's buffer) and expands its
 * 2-bit glyph sheet to I4. There is no boot ROM here, so the font failed
 * to load and the game drew its placeholder graphic ("DummySysFont", a
 * Japanese name label) where system-font text belongs.
 *
 * ReadFont is replaced: for the ANSI encoding a US console uses, it builds
 * the unpacked font image itself, in the ROM font's format, from glyphs
 * the host renders (host_font.h), and OSInitFont continues as usual. The
 * Shift-JIS font (Japanese consoles) is not provided. */
#include <string.h>

#include "hle.h"
#include "host_font.h"

int (*g_host_font_glyph)(uint32_t ch, int cell, int ascent, uint8_t* coverage);

enum { FONT_ENCODE_ANSI = 0 };

/* The font's geometry: 224 characters (0x20-0xFF) in 24x24 cells, 21 x 21
 * to a 512x512 sheet, which fills the buffer the SDK has the game allocate
 * for an ANSI font (OS_FONT_SIZE_ANSI, 0x20120 bytes). */
#define FIRST_CHAR 0x20
#define LAST_CHAR 0xFF
#define CELL 24
#define ASCENT 19
#define DESCENT 5
#define SHEET 512
#define COLUMNS (SHEET / CELL)
#define WIDTH_TABLE 0x30
#define SHEET_IMAGE 0x120
#define SHEET_I4_BYTES (SHEET * SHEET / 2)

/* Windows-1252, which the ANSI font follows, for 0x80-0x9F. */
static const uint16_t kCp1252[32] = {
    0x20AC, 0x0020, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0020, 0x017D, 0x0020,
    0x0020, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0020, 0x017E, 0x0178,
};

static void put16(uint32_t a, uint32_t v) { ST16(a, (uint16_t)v); }

/* Writes the unpacked ANSI font into fontData: header, width table, and
 * the sheet as 2-bit pixels in the order ExpandFontSheet turns into I4
 * tiles (8x8 texel blocks, row-major; four pixels a byte, first in the
 * top bits). */
static int build_ansi_font(uint32_t font) {
    static uint8_t sheet[SHEET * SHEET];
    uint8_t coverage[CELL * CELL];
    int ch, max_width = 0, p;
    memset(sheet, 0, sizeof(sheet));
    for (ch = FIRST_CHAR; ch <= LAST_CHAR; ch++) {
        int index = ch - FIRST_CHAR, x0 = (index % COLUMNS) * CELL, y0 = (index / COLUMNS) * CELL, x, y, w;
        uint32_t u = ch >= 0x80 && ch < 0xA0 ? kCp1252[ch - 0x80] : (uint32_t)ch;
        memset(coverage, 0, sizeof(coverage));
        w = g_host_font_glyph(u, CELL, ASCENT, coverage);
        if (w < 0) return 0;
        if (w > CELL) w = CELL;
        if (w > max_width) max_width = w;
        ST8(font + WIDTH_TABLE + (uint32_t)index, (uint8_t)w);
        for (y = 0; y < CELL; y++) {
            for (x = 0; x < CELL; x++) sheet[(y0 + y) * SHEET + x0 + x] = (uint8_t)((coverage[y * CELL + x] + 42) / 85);
        }
    }
    put16(font + 0x00, FONT_ENCODE_ANSI);
    put16(font + 0x02, FIRST_CHAR);
    put16(font + 0x04, LAST_CHAR);
    put16(font + 0x06, '?');           /* invalid character */
    put16(font + 0x08, ASCENT);
    put16(font + 0x0A, DESCENT);
    put16(font + 0x0C, (uint32_t)max_width);
    put16(font + 0x0E, 2);             /* leading */
    put16(font + 0x10, CELL);
    put16(font + 0x12, CELL);
    ST32(font + 0x14, SHEET_I4_BYTES); /* one sheet */
    put16(font + 0x18, 0);             /* sheets expand to GX_TF_I4 */
    put16(font + 0x1A, COLUMNS);
    put16(font + 0x1C, COLUMNS);
    put16(font + 0x1E, SHEET);
    put16(font + 0x20, SHEET);
    put16(font + 0x22, WIDTH_TABLE);
    ST32(font + 0x24, SHEET_IMAGE);
    ST32(font + 0x28, SHEET_I4_BYTES);
    ST8(font + 0x2C, 0x00);            /* the four 2-bit levels as I4 */
    ST8(font + 0x2D, 0x55);
    ST8(font + 0x2E, 0xAA);
    ST8(font + 0x2F, 0xFF);
    for (p = 0; p < SHEET * SHEET; p += 4) {
        uint8_t b = 0;
        int k;
        for (k = 0; k < 4; k++) {
            int q = p + k, tile = q / 64, x = (tile % (SHEET / 8)) * 8 + q % 8, y = (tile / (SHEET / 8)) * 8 + (q % 64) / 8;
            b = (uint8_t)(b << 2 | sheet[y * SHEET + x]);
        }
        ST8(font + SHEET_IMAGE + (uint32_t)(p / 4), b);
    }
    return 1;
}

/* static u32 ReadFont(void* img, u16 encode, void* fontData) */
void F_ReadFont(ppc_t* c) {
    uint32_t encode = c->r[4] & 0xFFFF, font = c->r[5];
    if (encode == FONT_ENCODE_ANSI && g_host_font_glyph && build_ansi_font(font)) {
        HLE_LOG("system font: built the ANSI font from host glyphs");
        c->r[3] = SHEET_IMAGE + SHEET_I4_BYTES / 2;
        return;
    }
    HLE_WARN("system font (encoding %u): not available", encode);
    c->r[3] = 0;
}
