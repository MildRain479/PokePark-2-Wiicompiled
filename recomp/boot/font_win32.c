/* Glyphs for the Wii system font (hle_font.c), rendered with GDI. The
 * console's font is a rounded sans-serif; Segoe UI Semibold is the
 * closest that every Windows install has. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#include "host_font.h"

#pragma comment(lib, "gdi32.lib")

static HDC sDC;
static HFONT sFont;
static HBITMAP sBitmap;
static uint32_t* sPixels;
static int sCell;

static int render(uint32_t ch, int cell, int ascent, uint8_t* coverage) {
    WCHAR text[2];
    SIZE size;
    TEXTMETRICW tm;
    int x, y;
    if (cell != sCell) return -1;
    text[0] = (WCHAR)ch;
    text[1] = 0;
    memset(sPixels, 0, (size_t)cell * cell * 4);
    GetTextMetricsW(sDC, &tm);
    /* The font's own baseline sits at tmAscent; move it to `ascent`. */
    TextOutW(sDC, 0, ascent - tm.tmAscent, text, 1);
    GdiFlush();
    for (y = 0; y < cell; y++) {
        for (x = 0; x < cell; x++) coverage[y * cell + x] = (uint8_t)(sPixels[y * cell + x] & 0xFF);
    }
    if (!GetTextExtentPoint32W(sDC, text, 1, &size)) return -1;
    return (int)size.cx;
}

void font_init(void) {
    BITMAPINFO bi;
    const int cell = 24;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = cell;
    bi.bmiHeader.biHeight = -cell; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    sDC = CreateCompatibleDC(NULL);
    sBitmap = CreateDIBSection(sDC, &bi, DIB_RGB_COLORS, (void**)&sPixels, NULL, 0);
    if (!sDC || !sBitmap) return;
    SelectObject(sDC, sBitmap);
    /* About 20 px tall in a 24 px cell, like the console font. */
    sFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    if (!sFont) return;
    SelectObject(sDC, sFont);
    SetBkMode(sDC, TRANSPARENT);
    SetTextColor(sDC, RGB(255, 255, 255));
    sCell = cell;
    g_host_font_glyph = render;
}
