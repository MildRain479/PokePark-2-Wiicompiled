#include "flib/RecordTable.h"

#include <string.h>

void RTEntry::SetEmbeddedStringField(const char* name, const char* value) {
    RTFieldDef* def = GetFieldDef(table, name);
    char* str = (char*)(data + def->offset);
    str[0] = '\0';
    if (value != NULL) {
        u32 size = def->arraySize;
        strncpy(str, value, size);
        str[size - 1] = '\0';
    }
}

void* RecordTableAllocator::allocate(u32 size, u32 align) {
    return gMemoryManager.allocate(size, align, NULL, 0, NULL);
}

void RTEntry::SetAllocatedStringField(const char* name, const char* value) {
    RTFieldDef* def = GetFieldDef(table, name);
    char** str = (char**)(data + def->offset);
    if (*str != NULL) {
        gMemoryManager.free(*str);
        *str = NULL;
    }
    if (value != NULL) {
        *str = (char*)RTAllocator()->allocate(strlen(value) + 1, 4);
        strcpy(*str, value);
    }
}

void RecordTable::RecordTableClear() {
    int n = fields.count;
    for (int i = 0; i < n; i++) {
        if (fields.data[i].name != NULL) {
            gMemoryManager.free((void*)fields.data[i].name);
        }
    }

    n = entries.count;
    for (int i = 0; i < n; i++) {
        entries.data[i].Free();
    }

    entries.Clear();
    fields.Clear();
    entrySize = 0;
}

void RecordTableAllocator::free(void* ptr) {
    gMemoryManager.free(ptr);
}

void AddField(RecordTable* table, const char* name, int type, int arraySize) {
    int count = table->fields.count;

    // Find the insertion point that keeps the fields sorted by name.
    int len = count;
    int pos = 0;
    while (len > 0) {
        int half = len >> 1;
        int cmp = strcmp(table->fields.data[pos + half].name, name);
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

    table->fields.Append();
    if (count - pos != 0) {
        memmove(&table->fields.data[pos + 1], &table->fields.data[pos], (count - pos) * sizeof(RTFieldDef));
    }

    RTFieldDef* def = &table->fields.data[pos];
    def->type = type;
    def->arraySize = arraySize;
    char* str = (char*)RTAllocator()->allocate(strlen(name) + 1, 4);
    strcpy(str, name);
    def->name = str;
    def->size = RecordTable::ElementSizes[type].size;
    def->offset = table->entrySize;
    table->entrySize += (arraySize * def->size + 3) & ~3;
}

RTFieldDef* GetFieldDef(RecordTable* table, const char* name) {
    int len = table->fields.count;
    if (len != 0) {
        int pos = 0;
        while (len > 1) {
            int half = len >> 1;
            RTFieldDef* def = &table->fields.data[pos + half];
            int cmp = strcmp(def->name, name);
            if (cmp > 0) {
                len = half;
            } else if (cmp == 0) {
                return def;
            } else {
                len -= half;
                pos += half;
            }
        }
        RTFieldDef* def = &table->fields.data[pos];
        if (strcmp(def->name, name) == 0) {
            return def;
        }
    }
    return NULL;
}

RTEntry* RecordTableAllocNewEntry(RecordTable* table) {
    RTEntry* entry = table->entries.Append();
    entry->table = table;
    u32 size = table->entrySize;
    entry->data = (u8*)RTAllocator()->allocate(size, 4);
    memset(entry->data, 0, size);
    return entry;
}

RTEntry* RecordTableAddElement(RecordTable* table, int index) {
    int count = table->entries.count;
    table->entries.Append();
    if (count - index != 0) {
        memmove(&table->entries.data[index + 1], &table->entries.data[index], (count - index) * sizeof(RTEntry));
    }

    RTEntry* entry = &table->entries.data[index];
    entry->table = table;
    u32 size = table->entrySize;
    entry->data = (u8*)RTAllocator()->allocate(size, 4);
    memset(entry->data, 0, size);
    return entry;
}

// Removes an entry by moving the last entry into its place.
void RecordTableDeleteEntry(RecordTable* table, RTEntry* entry) {
    entry->Free();
    int index = entry - table->entries.data;
    table->entries.data[index].~RTEntry();
    int last = --table->entries.count;
    if (index < last) {
        memcpy(&table->entries.data[index], &table->entries.data[last], sizeof(RTEntry));
    }
}

// Removes an entry and shifts the following entries down, keeping them sorted.
void RemoveSortedEntry(RecordTable* table, RTEntry* entry) {
    entry->Free();
    int index = entry - table->entries.data;
    table->entries.data[index].~RTEntry();
    int count = --table->entries.count;
    if (index < count) {
        memmove(&table->entries.data[index], &table->entries.data[index + 1], (count - index) * sizeof(RTEntry));
    }
}

static RecordTable* MetaTable;

static inline void CreateMetaTable() {
    if (MetaTable == NULL) {
        MetaTable = new RecordTable(1);
        AddField(MetaTable, "Name", RT_TYPE_CHAR, 0x10);
        AddField(MetaTable, "Ptr", RT_TYPE_PTR, 1);
    }
}

RecordTable* CreateNewRecordTable(const char* name, int fieldCapacity) {
    int capacity = 1;
    if (fieldCapacity > 1) {
        while (fieldCapacity > capacity) {
            capacity <<= 1;
        }
        capacity >>= 1;
    }
    RecordTable* table = new RecordTable(capacity);
    CreateMetaTable();
    AddNamedPtrEntry(MetaTable, name, table);
    return table;
}

void DeleteRecordTable(const char* name) {
    RecordTable* table = (RecordTable*)RecordTablePtrValByName(MetaTable, name);
    if (table != NULL) {
        delete table;
    }
    DeleteEntryByName(MetaTable, name);
}

RecordTable* RecordTablePtrByName(const char* name) {
    return (RecordTable*)RecordTablePtrValByName(MetaTable, name);
}

RecordTable* GetBaseRecordTable() {
    return MetaTable;
}

void InitMainRecordTable() {
    CreateMetaTable();
}

RTTypeSize RecordTable::ElementSizes[10] = {
    {0, 0}, {1, 1}, {2, 2}, {3, 4}, {4, 8}, {5, 4}, {6, 8}, {7, 4}, {8, 4}, {9, 4},
};

RecordTableAllocator gRecordTableAllocator;
