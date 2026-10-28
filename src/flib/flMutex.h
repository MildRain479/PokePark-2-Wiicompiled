#ifndef FLIB_FLMUTEX_H
#define FLIB_FLMUTEX_H

#include <revolution/OS.h>

class flMutexBase {
public:
    ~flMutexBase() {
        if (mInitialized) {
            mInitialized = FALSE;
        }
    }

    OSMutex mMutex;    // at 0x0
    BOOL mInitialized; // at 0x18
};

class flMutex : public flMutexBase {
public:
    flMutex();
    ~flMutex();

    void Init();
    void clear();

    void lock() { OSLockMutex(&mMutex); }
    void unlock() { OSUnlockMutex(&mMutex); }
};

#endif
