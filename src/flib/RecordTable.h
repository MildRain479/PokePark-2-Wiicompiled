#ifndef FLIB_RECORDTABLE_H
#define FLIB_RECORDTABLE_H

#include <types.h>

struct RTFieldDef {
    u32 offset;       // at 0x0
    const char* name; // at 0x4
    u16 type;         // at 0x8
    u16 size;         // at 0xA
    u32 arraySize;    // at 0xC
};

struct RecordTable;

struct RTEntry {
    RecordTable* table; // at 0x0
    u8* data;           // at 0x4

    // TODO: Defined in a TU that isn't decompiled yet; parameter types are
    // guesses from the call sites.
    void SetEmbeddedStringField(const char* name, const char* value);
    void SetAllocatedStringField(const char* name, const char* value);
};

struct RecordTable {
    RTFieldDef* fields; // at 0x0
    u32 _4;
    u32 fieldCount;     // at 0x8
    RTEntry* entries;   // at 0xC
    u32 _10;
    u32 entryCount;     // at 0x14
    u32 entrySize;      // at 0x18
};

// TODO: These live in a TU that isn't decompiled yet; use C names until the
// real signatures are known.
extern "C" {
    RecordTable* GetOrCreateNamedRT(const char* name, int unk);
    RecordTable* CreateNewRecordTable(const char* name, int unk);
    RecordTable* RecordTablePtrByName(const char* name);
    void* RecordTablePtrValByName(RecordTable* table, const char* name);
    void DeleteRecordTable(RecordTable* table);
    RecordTable* GetBaseRecordTable();
    void AddField(RecordTable* table, const char* name, int type, int count);
    RTEntry* FindEntryByName(RecordTable* table, const char* name);
    RTEntry* AddNamedEntry(RecordTable* table, const char* name);
    RTEntry* AddNamedPtrEntry(RecordTable* table, const char* name, void* ptr);
    RTEntry* RecordTableAllocNewEntry(RecordTable* table);
    RTEntry* RecordTableAddElement(RecordTable* table, u32 unk);
    void RecordTableDeleteEntry(RecordTable* table, u32 unk);
    void RemoveSortedEntry(RecordTable* table, u32 unk);
    RTFieldDef* GetFieldDef(RecordTable* table, const char* name);
}

extern RecordTable* Module2RT_Table;

template <typename T>
inline T& GetRTField(RTEntry* entry, const char* name) {
    RTFieldDef* def = GetFieldDef(entry->table, name);
    return *(T*)(entry->data + def->offset);
}

template <typename T>
inline T& GetRTArrayField(RTEntry* entry, const char* name, int index) {
    RTFieldDef* def = GetFieldDef(entry->table, name);
    return *(T*)(entry->data + def->offset + index * def->size);
}

#endif
