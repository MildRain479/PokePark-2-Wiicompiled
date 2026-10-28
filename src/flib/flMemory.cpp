#include "flib/flMemory.h"

#include "flib/ScriptLog.h"
#include "flib/flRevolutionAlloc.h"
#include <string.h>

#define CHUNK_AT(base, offset) ((flMemoryChunk*)((u8*)(base) + (offset)))

static char sHeapAllocFormat[0x18] = "HEAP::Alloc Size=%d";

flMemory::flMemory() : mInitialized(FALSE) {}

flMemory::~flMemory() {
    if (mInitialized) {
        flMemoryBlock* block = mBlocks;
        while (block != NULL) {
            flMemoryBlock* next = block->next;
            MEMFreeToAllocator(&MEM1Allocator, block);
            block = next;
        }
        mBlocks = NULL;
        mInitialized = FALSE;
    }
}

void flMemory::clear() {
    mFreeList.size = 0;
    mFreeList.prevFree = NULL;
    mFreeList.next = NULL;
    mFreeList.prev = NULL;
    mBlocks = NULL;
    mGrowSize = 0;
    mAllocCount = 0;
    mAllocTotal = 0;
}

void flMemory::init(u32 size, u32 growSize) {
    if (mInitialized) {
        return;
    }

    mFreeList.size = 0;
    mFreeList.prevFree = NULL;
    mFreeList.next = NULL;
    mFreeList.prev = NULL;
    mBlocks = NULL;
    mAllocCount = 0;
    mAllocTotal = 0;
    mGrowSize = growSize;
    if (size != 0) {
        DEBUGPrint2(sHeapAllocFormat, size);
        AddBlock(MEMAllocFromAllocator(&MEM1Allocator, size), size);
    }
    mInitialized = TRUE;
}

void DEBUGPrint2(const char* fmt, ...) {}

void flMemory::release() {
    if (mInitialized) {
        flMemoryBlock* block = mBlocks;
        while (block != NULL) {
            flMemoryBlock* next = block->next;
            MEMFreeToAllocator(&MEM1Allocator, block);
            block = next;
        }
        mBlocks = NULL;
        mInitialized = FALSE;
    }
}

void flMemory::AddBlock(void* mem, u32 size) {
    flMemoryBlock* block = (flMemoryBlock*)mem;
    flMemoryChunk* chunk = (flMemoryChunk*)(block + 1);

    block->prev = NULL;
    flMemoryBlock* head = mBlocks;
    block->next = head;
    if (head != NULL) {
        head->prev = block;
    }
    block->size = size;
    block->_c = 0;
    mBlocks = block;

    chunk->size = size - 0x20;
    chunk->prevFree = (flMemoryChunk*)1;
    chunk->next = mFreeList.next;
    if (chunk->next != NULL) {
        chunk->next->prev = chunk;
    }
    chunk->prev = &mFreeList;
    mFreeList.next = chunk;

    // Terminator chunk: records the block's original free size and owner.
    flMemoryChunk* end = CHUNK_AT(chunk, chunk->size);
    end->size = 0;
    end->prevFree = chunk;
    end->next = (flMemoryChunk*)chunk->size;
    end->prev = (flMemoryChunk*)block;
}

void flMemory::Free(void* ptr) {
    u32* p = (u32*)ptr;
    while (p[-1] == 0xFFFFFFFF) {
        p--;
    }

    flMemoryChunk* chunk = (flMemoryChunk*)(p - 2);
    flMemoryChunk* next = CHUNK_AT(chunk, chunk->size);
    if (p == NULL) {
        return;
    }

    mAllocCount--;
    mAllocTotal -= chunk->size;

    flMemoryChunk* prev = chunk->prevFree;
    if (prev != NULL) {
        if ((u32)next->prevFree == 1) {
            next->prev->next = next->next;
            if (next->next != NULL) {
                next->next->prev = next->prev;
            }
            prev->size += chunk->size + next->size;
            next = CHUNK_AT(prev, prev->size);
        } else {
            prev->size = prev->size + chunk->size;
        }
    } else {
        chunk->prevFree = (flMemoryChunk*)1;
        prev = chunk;
        if ((u32)next->prevFree == 1) {
            chunk->size = chunk->size + next->size;
            flMemoryChunk* before = next->prev;
            chunk->prev = before;
            before->next = chunk;
            chunk->next = next->next;
            if (chunk->next != NULL) {
                next->next->prev = chunk;
            }
            next = CHUNK_AT(chunk, chunk->size);
        } else {
            flMemoryChunk* c = next;
            while (c->size != 0) {
                if ((u32)c->prevFree == 1) {
                    chunk->next = c;
                    if ((chunk->prev = c->prev) != NULL) {
                        chunk->prev->next = chunk;
                    }
                    c->prev = chunk;
                    break;
                }
                c = CHUNK_AT(c, c->size);
            }
            if (c->size == 0) {
                flMemoryChunk* tail = &mFreeList;
                while (tail->next != NULL) {
                    tail = tail->next;
                }
                chunk->next = tail->next;
                tail->next = chunk;
                chunk->prev = tail;
            }
        }
    }

    next->prevFree = prev;

    // Release the block once it is completely free again.
    if (mGrowSize == 0) {
        return;
    }
    if (next->size != 0) {
        return;
    }
    if (prev->size != (s32)next->next) {
        return;
    }

    flMemoryBlock* block = (flMemoryBlock*)next->prev;
    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        mBlocks = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }

    prev->prev->next = prev->next;
    if (prev->next != NULL) {
        prev->next->prev = prev->prev;
    }

    MEMFreeToAllocator(&MEM1Allocator, block);
}

void* flMemory::Allocate(int size, u32 align) {
    u32 mask;
    flMemoryChunk* chunk;
    int minSize;
    int chunkSize;

    chunk = mFreeList.next;
    if (size <= 0) {
        return NULL;
    }

    if (align < 4) {
        align = 4;
    }
    mask = align - 1;
    minSize = (int)align > 0x10 ? align : 0x10;

    chunkSize = size + 8;
    if (chunkSize < minSize) {
        chunkSize = minSize;
    }
    if (align > 4) {
        chunkSize += mask;
        chunkSize = (chunkSize + mask) & ~mask;
    } else {
        chunkSize = (chunkSize + 3) & ~3;
    }

    for (; chunk != NULL; chunk = chunk->next) {
        if ((int)chunk->size < chunkSize) {
            continue;
        }

        int rest = chunk->size - chunkSize;
        flMemoryChunk* after = CHUNK_AT(chunk, chunk->size);
        if (rest >= minSize) {
            flMemoryChunk* remainder = CHUNK_AT(chunk, chunkSize);
            remainder->size = rest;
            remainder->prevFree = (flMemoryChunk*)1;
            remainder->prev = chunk->prev;
            chunk->prev->next = remainder;
            if ((remainder->next = chunk->next) != NULL) {
                remainder->next->prev = remainder;
            }
            chunk->size = chunkSize;
            after->prevFree = remainder;
        } else {
            chunk->prev->next = chunk->next;
            if (chunk->next != NULL) {
                chunk->next->prev = chunk->prev;
            }
            after->prevFree = NULL;
        }

        u32* data = (u32*)((u8*)chunk + 8);
        chunk->prevFree = NULL;
        u32* aligned = (u32*)(((u32)data + mask) & ~mask);
        while (data < aligned) {
            *data++ = 0xFFFFFFFF;
        }

        mAllocCount++;
        mAllocTotal += chunk->size;
        return data;
    }

    u32 growSize = mGrowSize;
    if (growSize != 0) {
        if (chunkSize >= growSize - 0x28) {
            growSize = chunkSize + 0x28;
        }
        DEBUGPrint2(sHeapAllocFormat, growSize);
        void* mem = MEMAllocFromAllocator(&MEM1Allocator, growSize);
        if (growSize != 0 && mem != NULL) {
            AddBlock(mem, growSize);
            return Allocate(size, align);
        }
    }
    return NULL;
}

int flMemory::noop(int a, int b) {
    return 0;
}

flMutex::flMutex() {
    mInitialized = FALSE;
}

flMutex::~flMutex() {
    if (mInitialized) {
        mInitialized = FALSE;
    }
}

void flMutex::Init() {
    OSInitMutex(&mMutex);
    mInitialized = TRUE;
}

void flMutex::clear() {
    if (mInitialized) {
        mInitialized = FALSE;
    }
}

flMemoryManager::~flMemoryManager() {
    if (mInitialized) {
        mMutex.lock();
        mHeap.release();
        mMutex.unlock();
        mMutex.clear();
        mInitialized = FALSE;
    }
}

void flMemoryManager::init(u32 size, u32 growSize) {
    if (mInitialized) {
        return;
    }
    mAllocCount = 0;
    mAllocTotal = 0;
    mMutex.Init();
    mMutex.lock();
    mHeap.init(size, growSize);
    mMutex.unlock();
    mInitialized = TRUE;
}

void flMemoryManager::clear() {
    if (mInitialized) {
        mMutex.lock();
        mHeap.release();
        mMutex.unlock();
        mMutex.clear();
        mInitialized = FALSE;
    }
}

void flMemoryManager::free(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    flMemoryChunk* chunk = ((flMemoryChunk**)ptr)[-1];
    mMutex.lock();

    flMemoryAllocHeader* header = ((flMemoryAllocHeader**)ptr)[-1];
    if (header->magic != 0x4375) {
        DEBUGPrint2("flMemory::CHK dirty memory adr:%x id:%x size:%d %s %d %s", header, header->magic,
                    header->size, header->file, header->line, header->function);
    }

    u32 size = header->size;
    u8* guard = (u8*)ptr + size;
    if (memcmp(guard, "d7hV", 4) != 0) {
        DEBUGPrint2("flMemory::CHK memory over adr:%x id:%x eid:%02x%02x%02x%02x size:%d %s %d %s", header,
                    header->magic, guard[0], guard[1], guard[2], guard[3], size, header->file, header->line,
                    header->function);
    }

    header = ((flMemoryAllocHeader**)ptr)[-1];
    u8* end = (u8*)ptr + header->size;
    header->magic = 0x5124;
    memcpy(end, "-@tY", 4);

    mAllocCount--;
    mAllocTotal -= chunk->size;
    mHeap.Free(chunk);
    mMutex.unlock();
}

void* flMemoryManager::allocate(u32 size, u32 align, const char* file, int line, const char* function) {
    mMutex.lock();
    flMemoryAllocHeader* header = (flMemoryAllocHeader*)mHeap.Allocate(size + align + 0x18, 4);
    if (header == NULL) {
        mMutex.unlock();
        flLog("flMemory::not enough memory %s %d %s", file, line, function);
        return NULL;
    }

    u8* ptr = (u8*)(header + 1);
    if (align > 4) {
        ptr = (u8*)((u32)(ptr + align - 1) & -align);
    }

    header->size = size;
    header->magic = 0x4375;
    header->align = align;
    header->file = file;
    header->line = line;
    header->function = function;
    header->self = header;
    ((flMemoryAllocHeader**)ptr)[-1] = header;
    memcpy(ptr + size, "d7hV", 4);

    mAllocCount++;
    mAllocTotal += size;
    mHeap.noop(0, 0);
    mMutex.unlock();
    return ptr;
}

void flLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ScriptLogPrintf(fmt, args);
}

flMemoryManager gMemoryManager;
