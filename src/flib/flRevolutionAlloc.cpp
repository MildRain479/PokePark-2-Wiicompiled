#include "flib/flRevolutionAlloc.h"

#include "flib/flMutex.h"
#include <revolution/OS.h>

static MEMAllocator sMem1HeapAllocator;
static MEMAllocator sMem2HeapAllocator;
MEMAllocator MEM1Allocator;
MEMAllocator MEM2Allocator;
static flMutex sMutex;

static MEMAllocatorFuncs sAllocatorFuncs = {flRevolution::alloc, flRevolution::free};

void* flRevolution::alloc(MEMAllocator* allocator, u32 size) {
    sMutex.lock();
    void* block = MEMAllocFromAllocator((MEMAllocator*)allocator->heap, size);
    sMutex.unlock();
    return block;
}

void flRevolution::free(MEMAllocator* allocator, void* block) {
    sMutex.lock();
    MEMFreeToAllocator((MEMAllocator*)allocator->heap, block);
    sMutex.unlock();
}

void initMemoryAlloc() {
    MEMiHeapHead* heap;

    sMutex.Init();

    void* mem1Lo = OSGetMEM1ArenaLo();
    void* mem1Hi = OSGetMEM1ArenaHi();
    u32 mem1Size = (u32)mem1Hi - (u32)mem1Lo;
    OSReport("MEM1 free size=%d (%dK) (%dM)\n", mem1Size, mem1Size >> 10, mem1Size >> 20);
    heap = MEMCreateExpHeapEx(mem1Lo, mem1Size, 0);
    if (heap == NULL) {
        OSPanic("flRevolutionAlloc.cpp", 157, "mem1 heap allocation error\n");
    }
    OSSetMEM1ArenaLo(mem1Hi);
    MEMSetAllocModeForExpHeap(heap, 0);
    MEMInitAllocatorForExpHeap(&sMem1HeapAllocator, heap, 32);

    void* mem2Lo = OSGetMEM2ArenaLo();
    void* mem2Hi = OSGetMEM2ArenaHi();
    u32 mem2Size = (u32)mem2Hi - (u32)mem2Lo;
    OSReport("MEM2 free size=%d (%dK) (%dM)\n", mem2Size, mem2Size >> 10, mem2Size >> 20);
    heap = MEMCreateExpHeapEx(mem2Lo, mem2Size, 0);
    if (heap == NULL) {
        OSPanic("flRevolutionAlloc.cpp", 174, "mem2 heap allocation error\n");
    }
    OSSetMEM2ArenaLo(mem2Hi);
    MEMSetAllocModeForExpHeap(heap, 0);
    MEMInitAllocatorForExpHeap(&sMem2HeapAllocator, heap, 32);

    MEM1Allocator.funcs = &sAllocatorFuncs;
    MEM1Allocator.heap = (MEMiHeapHead*)&sMem1HeapAllocator;
    MEM1Allocator.heapParam1 = 0;
    MEM1Allocator.heapParam2 = 0;

    MEM2Allocator.funcs = &sAllocatorFuncs;
    MEM2Allocator.heap = (MEMiHeapHead*)&sMem2HeapAllocator;
    MEM2Allocator.heapParam1 = 0;
    MEM2Allocator.heapParam2 = 1;
}

void flRevolution::clear() {
    sMutex.clear();
}
