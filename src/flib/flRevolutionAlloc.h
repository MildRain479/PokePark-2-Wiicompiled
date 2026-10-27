#ifndef FLIB_FLREVOLUTIONALLOC_H
#define FLIB_FLREVOLUTIONALLOC_H

#include <revolution/MEM.h>

// MEMAllocator callbacks that serialise access to an inner allocator, which
// is stored in the outer allocator's heap field.
namespace flRevolution {
    void* alloc(MEMAllocator* allocator, u32 size);
    void free(MEMAllocator* allocator, void* block);
    void clear();
}

extern MEMAllocator MEM1Allocator;
extern MEMAllocator MEM2Allocator;

void initMemoryAlloc();

#endif
