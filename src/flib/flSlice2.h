#ifndef FLIB_FLSLICE2_H
#define FLIB_FLSLICE2_H

#include "flib/RecordTable.h"

namespace flSlice2 {
    class BaseSlice {
    public:
        BaseSlice* mNext; // at 0x0
        u32 mFlags;       // at 0x4

        virtual ~BaseSlice();
        virtual void OnRemove();
        virtual void Update(void* arg);
    };

    class SliceAllocator {
    public:
        virtual void* AllocateSlice(u32 size);
        virtual void FreeSlice(void* slice);
    };
}

extern RecordTable* Slice2GRPRT_Table;
extern flSlice2::SliceAllocator SliceAllocatorGlobal;

void ScheduleGRPRTEntry(const char* stage, flSlice2::BaseSlice* slice);
void UpdateStage(const char* stage, void* arg);

#endif
