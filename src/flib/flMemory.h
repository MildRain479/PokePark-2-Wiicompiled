#ifndef FLIB_FLMEMORY_H
#define FLIB_FLMEMORY_H

#include "flib/flMutex.h"

// A node in the heap. Every chunk starts with this header; free chunks are
// also linked into the heap's free list.
struct flMemoryChunk {
    s32 size;                // at 0x0
    flMemoryChunk* prevFree; // at 0x4: 1 if this chunk is free, otherwise the
                             // preceding chunk if that one is free
    flMemoryChunk* next;     // at 0x8
    flMemoryChunk* prev;     // at 0xC
};

// A block of memory taken from MEM1 and carved into chunks.
struct flMemoryBlock {
    flMemoryBlock* next; // at 0x0
    flMemoryBlock* prev; // at 0x4
    u32 size;            // at 0x8
    u32 _c;
};

// Header placed in front of each allocation for debugging.
struct flMemoryAllocHeader {
    u32 size;                  // at 0x0
    u16 magic;                 // at 0x4
    u16 align;                 // at 0x6
    const char* file;          // at 0x8
    int line;                  // at 0xC
    const char* function;      // at 0x10
    flMemoryAllocHeader* self; // at 0x14
};

class flMemory {
public:
    flMemory();
    ~flMemory();

    void clear();
    void init(u32 size, u32 growSize);
    void release();
    void AddBlock(void* block, u32 size);
    void Free(void* ptr);
    void* Allocate(int size, u32 align);
    int noop(int a, int b);

    BOOL mInitialized;        // at 0x0
    flMemoryChunk mFreeList;  // at 0x4
    flMemoryBlock* mBlocks;   // at 0x14
    u32 mGrowSize;            // at 0x18
    u32 mAllocCount;          // at 0x1C
    u32 mAllocTotal;          // at 0x20
};

class flMemoryManager {
public:
    flMemoryManager() { mInitialized = FALSE; }
    ~flMemoryManager();

    void init(u32 size, u32 growSize);
    void clear();
    void free(void* ptr);
    void* allocate(u32 size, u32 align, const char* file, int line, const char* function);

    BOOL mInitialized; // at 0x0
    u32 mAllocCount;   // at 0x4
    u32 mAllocTotal;   // at 0x8
    flMutex mMutex;    // at 0xC
    flMemory mHeap;    // at 0x28
};

extern flMemoryManager gMemoryManager;

void DEBUGPrint2(const char* fmt, ...);
void flLog(const char* fmt, ...);

#endif
