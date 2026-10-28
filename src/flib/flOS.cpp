#include "flib/flMemory.h"
#include "flib/flRevolutionAlloc.h"
#include "flib/scripting/Syscalls.h"

#include <revolution/DVD.h>
#include <revolution/OS.h>
#include <revolution/PAD.h>
#include <revolution/VI.h>
#include <stdlib.h>

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
    gMemoryManager.init(0x500000, 0);
    srand(OSGetTick());
    InitSyscall0Page();
    InitMainRecordTable();
    SetSyscallPage1();
}

extern "C" void flOSCleanup() {
    fn_802EDC70();
    gMemoryManager.clear();
    fn_802ED910();
    flRevolution::clear();
}
