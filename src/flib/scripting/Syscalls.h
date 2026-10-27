#ifndef FLIB_SCRIPTING_SYSCALLS_H
#define FLIB_SCRIPTING_SYSCALLS_H

#include <types.h>

// Script syscalls receive their arguments as an array of 32-bit words, and
// return a 32-bit word. Floats are passed through by bit pattern.
typedef u32 (*ScriptSyscall)(u32* args);

inline f32 SyscallFloatArg(u32& arg) {
    return *(f32*)&arg;
}

inline u32 SyscallFloatResult(f32 value) {
    return *(u32*)&value;
}

// TODO: Lives in a TU that isn't decompiled yet; use its C name until the
// real signature is known.
extern "C" void SetSyscallPage(int page, ScriptSyscall* syscalls);

void SetSyscallPage1();

#endif
