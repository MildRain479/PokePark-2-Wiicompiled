#ifndef FLIB_CODEPOINTCONVERT_H
#define FLIB_CODEPOINTCONVERT_H

#include <types.h>

// Output encodings for EncodeCodepoint. Any other value writes the code as
// one byte, or as two big-endian bytes when it doesn't fit (Shift-JIS).
enum {
    ENCODING_UTF8 = 1,
    ENCODING_UTF16 = 2,
    ENCODING_SJIS = 3,
};

// Writes one code point to dst and returns the number of bytes written,
// or -1 if it can't be encoded.
int EncodeCodepoint(char* dst, s32 codepoint, int encoding);

// Returns the Shift-JIS code for a Unicode code point. Code points below
// 0x100 are returned unchanged; unmapped ones return 0.
s32 UnicodeToSJIS(s32 codepoint);

#endif
