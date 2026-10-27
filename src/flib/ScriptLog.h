#ifndef FLIB_SCRIPTLOG_H
#define FLIB_SCRIPTLOG_H

#include <stdarg.h>
#include <types.h>

// ScriptLoggingFlags bits
#define SCRIPTLOG_STDOUT 0x2
#define SCRIPTLOG_FILE 0x4
#define SCRIPTLOG_CALLBACK 0x10
#define SCRIPTLOG_SCRIPT 0x20

extern u32 ScriptLoggingFlags;

void ScriptLogPrintf(const char* fmt, va_list args);
void ErrorHandling(const char* fmt, ...);
void LogWrite(const char* fmt, ...);
void LogWrite2(const char* fmt, ...);
void SetLoggingBits(u32 bits);
void ClearLoggingBits(u32 bits);
void ScriptLog(int id, const char* name);

#endif
