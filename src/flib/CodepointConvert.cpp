#include "flib/CodepointConvert.h"

#include <stddef.h>

#include "flib/UnicodeToSJIS.inc"

int EncodeCodepoint(char* dst, s32 codepoint, int encoding) {
    switch (encoding) {
    case ENCODING_UTF16:
        // Note: 0x10000 is not subtracted before splitting into surrogates.
        if (codepoint >= 0x10000) {
            ((u16*)dst)[0] = 0xD800 | ((codepoint >> 10) & 0x3FF);
            ((u16*)dst)[1] = 0xDC00 | (codepoint & 0x3FF);
            return 4;
        }
        *(u16*)dst = codepoint;
        return 2;
    default:
        if (codepoint >= 0x100) {
            dst[0] = (codepoint >> 8) & 0xFF;
            dst[1] = codepoint;
            return 2;
        }
        dst[0] = codepoint;
        return 1;
    case ENCODING_UTF8:
        if (codepoint < 0x80) {
            dst[0] = codepoint;
            return 1;
        } else if (codepoint < 0x800) {
            dst[0] = 0xC0 | ((codepoint >> 6) & 0x1F);
            dst[1] = 0x80 | (codepoint & 0x3F);
            return 2;
        } else if (codepoint < 0x10000) {
            dst[0] = 0xE0 | ((codepoint >> 12) & 0xF);
            dst[1] = 0x80 | ((codepoint >> 6) & 0x3F);
            dst[2] = 0x80 | (codepoint & 0x3F);
            return 3;
        } else if (codepoint < 0x200000) {
            dst[0] = 0xF0 | ((codepoint >> 18) & 0x7);
            dst[1] = 0x80 | ((codepoint >> 12) & 0x3F);
            dst[2] = 0x80 | ((codepoint >> 6) & 0x3F);
            dst[3] = 0x80 | (codepoint & 0x3F);
            return 4;
        } else if (codepoint < 0x4000000) {
            dst[0] = 0xF8 | ((codepoint >> 24) & 0x3);
            dst[1] = 0x80 | ((codepoint >> 18) & 0x3F);
            dst[2] = 0x80 | ((codepoint >> 12) & 0x3F);
            dst[3] = 0x80 | ((codepoint >> 6) & 0x3F);
            dst[4] = 0x80 | (codepoint & 0x3F);
            return 5;
        } else if (codepoint < 0x80000000) {
            dst[0] = 0xFC | ((codepoint >> 30) & 0x1);
            dst[1] = 0x80 | ((codepoint >> 24) & 0x3F);
            dst[2] = 0x80 | ((codepoint >> 18) & 0x3F);
            dst[3] = 0x80 | ((codepoint >> 12) & 0x3F);
            dst[4] = 0x80 | ((codepoint >> 6) & 0x3F);
            dst[5] = 0x80 | (codepoint & 0x3F);
            return 6;
        }
        return -1;
    }
}

s32 UnicodeToSJIS(s32 codepoint) {
    if (codepoint < 0x100) {
        return codepoint;
    }
    u16* row = sUnicodeToSJIS[(codepoint >> 8) & 0xFF];
    if (row != NULL) {
        return row[codepoint & 0xFF];
    }
    return 0;
}
