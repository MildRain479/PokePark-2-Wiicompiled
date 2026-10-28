#include "flib/ScriptLog.h"

#include "flib/CodepointConvert.h"

#include <stdio.h>
#include <string.h>

typedef void (*ScriptLogCallback)(const char* msg, void* arg1, void* arg2);

static char sLogBuffer[0x400];

u32 ScriptLoggingFlags = SCRIPTLOG_STDOUT | 0x1;

static ScriptLogCallback sLogCallback;
static void* sLogCallbackArg1;
static void* sLogCallbackArg2;
static FILE* sLogFile;
static char* sLogBufferPos;
static int sLastScriptId;
static const char* sLastScriptName;

static inline void LogWriteFile(const char* str) {
    if (sLogFile == NULL) {
        return;
    }
    while (*str != '\0') {
        *sLogBufferPos++ = *str++;
        if (sLogBufferPos >= sLogBuffer + sizeof(sLogBuffer) && sLogFile != NULL) {
            u32 size = sLogBufferPos - sLogBuffer;
            if (size != 0) {
                fwrite(sLogBuffer, size, 1, sLogFile);
                fflush(sLogFile);
            }
            sLogBufferPos = sLogBuffer;
        }
    }
}

static inline void LogOutput(const char* msg) {
    u32 flags = ScriptLoggingFlags;

    if (flags & SCRIPTLOG_FILE) {
        LogWriteFile(msg);
    }

    if (flags & SCRIPTLOG_STDOUT) {
        fputs(msg, stdout);
    }

    if ((flags & SCRIPTLOG_CALLBACK) && sLogCallback != NULL) {
        sLogCallback(msg, sLogCallbackArg1, sLogCallbackArg2);
    }
}

void ScriptLogPrintf(const char* fmt, va_list args) {
    char buf[0x200];
    vsprintf(buf, fmt, args);
    strcat(buf, "\n");
    LogOutput(buf);
}

void ErrorHandling(const char* fmt, ...) {
    va_list args;
    char buf[0x200];

    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    strcat(buf, "\n");
    LogOutput(buf);
}

void LogWrite(const char* fmt, ...) {
    va_list args;
    char buf[0x200];

    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    LogOutput(buf);
}

static inline int Utf8CharLength(const u8* s) {
    int c = *s;
    if (c < 0x80) {
        return 1;
    } else if (c < 0xE0) {
        return 2;
    } else if (c < 0xF0) {
        return 3;
    } else if (c < 0xF8) {
        return 4;
    } else if (c < 0xFC) {
        return 5;
    } else if (c < 0xFE) {
        return 6;
    }
    return -1;
}

static inline s32 Utf8Decode(const char* str) {
    const u8* s = (const u8*)str;
    int c = s[0];
    if (c < 0x80) {
        return c;
    } else if (c < 0xE0) {
        return ((c & 0x1F) << 6) + (s[1] & 0x3F);
    } else if (c < 0xF0) {
        return ((c & 0x0F) << 12) + ((s[1] & 0x3F) << 6) + (s[2] & 0x3F);
    } else if (c < 0xF8) {
        return ((c & 0x07) << 18) + ((s[1] & 0x3F) << 12) + ((s[2] & 0x3F) << 6) + (s[3] & 0x3F);
    } else if (c < 0xFC) {
        return ((c & 0x03) << 24) + ((s[1] & 0x3F) << 18) + ((s[2] & 0x3F) << 12) +
               ((s[3] & 0x3F) << 6) + (s[4] & 0x3F);
    } else if (c < 0xFE) {
        return ((c & 0x01) << 30) + ((s[1] & 0x3F) << 24) + ((s[2] & 0x3F) << 18) +
               ((s[3] & 0x3F) << 12) + ((s[4] & 0x3F) << 6) + (s[5] & 0x3F);
    }
    return -1;
}

// Like LogWrite, but re-encodes the UTF-8 message before output.
void LogWrite2(const char* fmt, ...) {
    va_list args;
    char converted[0x200];
    char buf[0x200];
    char* dst;
    const char* src;

    va_start(args, fmt);
    vsprintf(buf, fmt, args);

    src = buf;
    dst = converted;
    while (*src != '\0') {
        int len = Utf8CharLength((const u8*)src);
        s32 codepoint = Utf8Decode(src);
        src += len;
        dst += EncodeCodepoint(dst, UnicodeToSJIS(codepoint), ENCODING_SJIS);
    }
    *dst = '\0';

    LogOutput(converted);
}

void SetLoggingBits(u32 bits) {
    ScriptLoggingFlags |= bits;
}

void ClearLoggingBits(u32 bits) {
    ScriptLoggingFlags &= ~bits;
}

void ScriptLog(int id, const char* name) {
    sLastScriptId = id;
    sLastScriptName = name;
    if (ScriptLoggingFlags & SCRIPTLOG_SCRIPT) {
        ErrorHandling("ScriptLog: %4d %s", id, name != NULL ? name : "-");
    }
}
