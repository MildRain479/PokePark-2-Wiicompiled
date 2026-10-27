#include "flib/flRevolutionAlloc.h"
#include "flib/scripting/Syscalls.h"

#include <revolution/DVD.h>
#include <revolution/OS.h>
#include <revolution/PAD.h>
#include <revolution/VI.h>
#include <stdlib.h>

// TODO: Move these declarations into flib headers once their TUs are decompiled.
// Class layouts and parameter types are guesses based on call sites.
class flMemoryManager {
public:
    void init(u32 size, u32 flags);
    void clear();
};

class flMemoryMutex : public flMemoryManager {
public:
    static flMemoryMutex _static;

    u8 _unk[0x4C];
};

extern "C" {
    void fn_802ED900(int);
    void fn_802ED910();
    void fn_802EDC70();
    void InitSyscall0Page();
    void InitMainRecordTable();
}

extern "C" void flOSInit() {
    DVDInit();
    VIInit();
    PADInit();
    initMemoryAlloc();
    fn_802ED900(1);
    flMemoryMutex::_static.init(0x500000, 0);
    srand(OSGetTick());
    InitSyscall0Page();
    InitMainRecordTable();
    SetSyscallPage1();
}

extern "C" void flOSCleanup() {
    fn_802EDC70();
    flMemoryMutex::_static.clear();
    fn_802ED910();
    flRevolution::clear();
}
