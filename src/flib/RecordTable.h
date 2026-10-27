#ifndef FLIB_RECORDTABLE_H
#define FLIB_RECORDTABLE_H

#include <types.h>

struct RecordTable;

struct RTFieldDef {
    u32 offset; // at 0x0
};

struct RTEntry {
    RecordTable* table; // at 0x0
    u8* data;           // at 0x4
};

// TODO: These live in a TU that isn't decompiled yet; use C names until the
// real signatures are known.
extern "C" {
    RecordTable* GetOrCreateNamedRT(const char* name, int unk);
    void AddField(RecordTable* table, const char* name, int type, int count);
    RTEntry* FindEntryByName(RecordTable* table, const char* name);
    RTEntry* AddNamedPtrEntry(RecordTable* table, const char* name, void* ptr);
    RTFieldDef* GetFieldDef(RecordTable* table, const char* name);
}

template <typename T>
inline T& GetRTField(RTEntry* entry, const char* name) {
    RTFieldDef* def = GetFieldDef(entry->table, name);
    return *(T*)(entry->data + def->offset);
}

#endif
