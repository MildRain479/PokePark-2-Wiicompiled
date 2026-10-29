/* Glyphs from the host, for the Wii system font (hle_font.c). The platform
 * layer (recomp/boot/font_win32.c) sets the hook; without it the game
 * gets no system font, as on a console whose font failed to load. */
#pragma once

#include <stdint.h>

/* Renders Unicode character ch into a cell x cell coverage map (0..255,
 * row-major) with its baseline at row `ascent`, and returns its advance
 * width in pixels, or -1 if the host can't render it. */
extern int (*g_host_font_glyph)(uint32_t ch, int cell, int ascent, uint8_t* coverage);
