/* The port's own words in the game's language: the Options book, the
 * friends list on a second screen, the touch controls and the controls'
 * instructions (hle_text.c). The game's own text comes from its files (a
 * language pack's, hle_dvd.c); these are the port's, so they're here, in
 * UTF-8, one row per English phrase (tr.c). */
#pragma once

#include <stdint.h>

/* LANGUAGE_* (host_settings.h) of the game's text, as it started. */
extern int g_tr_language;

/* The phrase in that language, or english itself if it has none. */
const char* tr(const char* english);

/* The next character of UTF-8 text at *s, moving *s past it; 0 at its end.
 * A byte that doesn't start a UTF-8 character is read as Latin-1. */
uint32_t tr_next(const char** s);

/* Character ch as UTF-8 into out (4 bytes at most); how many. */
int tr_put(uint32_t ch, char* out);
