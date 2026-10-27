#ifndef FLIB_FLMUTEX_H
#define FLIB_FLMUTEX_H

#include <revolution/OS.h>

class flMutex {
public:
    flMutex();
    ~flMutex();

    void Init();
    void clear();

    void lock() { OSLockMutex(&mMutex); }
    void unlock() { OSUnlockMutex(&mMutex); }

private:
    OSMutex mMutex;    // at 0x0
    BOOL mInitialized; // at 0x18
};

#endif
