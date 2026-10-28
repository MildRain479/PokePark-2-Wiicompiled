#include "flib/RecordTable.h"

#include <string.h>

// Named tables keep their entries sorted by an embedded "Name" field, and
// usually carry a "Ptr" field.

// A module registered with RegisterSystemModule.
class SystemModule {
public:
    virtual void Destroy();
};

// TODO: These keep their C names until the real signatures are known.
extern "C" {
    void RegisterSystemModule(const char* name, void* module);
    void DeleteModuleByName(const char* name);
    void DestroyModule2TableEntry(const char* name);
}

char Module2RT_TableName[] = "Module2RT";

RecordTable* Module2RT_Table;

RecordTable* GetOrCreateNamedRT(const char* name, int nameLength) {
    RecordTable* table = RecordTablePtrByName(name);
    if (table == NULL) {
        table = CreateNewRecordTable(name, 2);
        AddField(table, "Name", RT_TYPE_CHAR, nameLength);
        AddField(table, "Ptr", RT_TYPE_PTR, 1);
    }
    return table;
}

RTEntry* FindEntryByName(RecordTable* table, const char* name) {
    RTEntry* entry;
    u32 nameOffset;
    int len;
    int pos;
    int half;
    int cmp;

    nameOffset = GetFieldDef(table, "Name")->offset;
    len = table->entries.count;
    if (len != 0) {
        pos = 0;
        while (len > 1) {
            half = len >> 1;
            entry = &table->entries.data[pos + half];
            cmp = strcmp((char*)entry->data + nameOffset, name);
            if (cmp > 0) {
                len = half;
            } else if (cmp == 0) {
                return entry;
            } else {
                len -= half;
                pos += half;
            }
        }
        entry = &table->entries.data[pos];
        if (strcmp((char*)entry->data + nameOffset, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

void* RecordTablePtrValByName(RecordTable* table, const char* name) {
    RTEntry* entry = FindEntryByName(table, name);
    if (entry != NULL) {
        return GetRTField<void*>(entry, "Ptr");
    }
    return NULL;
}

void DeleteEntryByName(RecordTable* table, const char* name) {
    RTEntry* entry = FindEntryByName(table, name);
    if (entry != NULL) {
        RemoveSortedEntry(table, entry);
    }
}

RTEntry* AddNamedEntry(RecordTable* table, const char* name) {
    u32 nameOffset = GetFieldDef(table, "Name")->offset;

    // Find the insertion point that keeps the entries sorted by name.
    int pos = 0;
    int len = table->entries.count;
    while (len > 0) {
        int half = len >> 1;
        int cmp = strcmp((char*)table->entries.data[pos + half].data + nameOffset, name);
        if (cmp > 0) {
            len = half;
        } else if (cmp == 0) {
            pos = pos + half + 1;
            break;
        } else {
            len -= half + 1;
            pos = pos + half + 1;
        }
    }

    RTEntry* entry = RecordTableAddElement(table, pos);
    entry->SetEmbeddedStringField("Name", name);
    return entry;
}

RTEntry* AddNamedPtrEntry(RecordTable* table, const char* name, void* ptr) {
    RTEntry* entry = AddNamedEntry(table, name);
    GetRTField<void*>(entry, "Ptr") = ptr;
    return entry;
}

void RegisterSystemModule(const char* name, void* module) {
    Module2RT_Table = GetOrCreateNamedRT(Module2RT_TableName, 0x10);
    AddNamedPtrEntry(Module2RT_Table, name, module);
}

void DeleteModuleByName(const char* name) {
    DeleteEntryByName(Module2RT_Table, name);
}

void DestroyModule2TableEntry(const char* name) {
    SystemModule* module = (SystemModule*)RecordTablePtrValByName(Module2RT_Table, name);
    if (module != NULL) {
        module->Destroy();
    }
}
