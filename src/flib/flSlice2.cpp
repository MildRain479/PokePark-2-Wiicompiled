#include "flib/flSlice2.h"

RecordTable* Slice2GRPRT_Table;
flSlice2::SliceAllocator SliceAllocatorGlobal;

// Appends a slice to the end of a stage's slice list.
void ScheduleGRPRTEntry(const char* stage, flSlice2::BaseSlice* slice) {
    if (Slice2GRPRT_Table == NULL) {
        Slice2GRPRT_Table = GetOrCreateNamedRT("Slice2GRPRT", 12);
        AddField(Slice2GRPRT_Table, "Bottom", 9, 1);
    }

    RTEntry* entry = FindEntryByName(Slice2GRPRT_Table, stage);
    flSlice2::BaseSlice* head;
    flSlice2::BaseSlice* tail;
    if (entry == NULL) {
        entry = AddNamedPtrEntry(Slice2GRPRT_Table, stage, NULL);
        head = NULL;
        tail = NULL;
    } else {
        head = GetRTField<flSlice2::BaseSlice*>(entry, "Ptr");
        tail = GetRTField<flSlice2::BaseSlice*>(entry, "Bottom");
    }

    slice->mNext = NULL;
    if (head == NULL) {
        head = slice;
    }
    if (tail != NULL) {
        tail->mNext = slice;
    }

    GetRTField<flSlice2::BaseSlice*>(entry, "Ptr") = head;
    GetRTField<flSlice2::BaseSlice*>(entry, "Bottom") = slice;
}

// Updates every live slice in a stage, then unlinks and deletes the finished
// ones.
void UpdateStage(const char* stage, void* arg) {
    RTEntry* entry = FindEntryByName(Slice2GRPRT_Table, stage);
    if (entry == NULL) {
        return;
    }

    flSlice2::BaseSlice* head = GetRTField<flSlice2::BaseSlice*>(entry, "Ptr");
    flSlice2::BaseSlice* slice;
    for (slice = head; slice != NULL; slice = slice->mNext) {
        if (!(slice->mFlags & 1)) {
            slice->Update(arg);
        }
    }

    // slice is reused to hold the next node
    flSlice2::BaseSlice* cur = head;
    flSlice2::BaseSlice* prev = NULL;
    while (cur != NULL) {
        slice = cur->mNext;
        if (cur->mFlags & 1) {
            if (prev != NULL) {
                prev->mNext = slice;
            } else {
                head = slice;
            }
            cur->OnRemove();
            delete cur;
        } else {
            prev = cur;
        }
        cur = slice;
    }

    entry = FindEntryByName(Slice2GRPRT_Table, stage);
    GetRTField<flSlice2::BaseSlice*>(entry, "Ptr") = head;
    GetRTField<flSlice2::BaseSlice*>(entry, "Bottom") = prev;
}
