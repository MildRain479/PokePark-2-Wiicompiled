#ifndef FLIB_RECORDTABLE_H
#define FLIB_RECORDTABLE_H

#include <types.h>

#include <new>
#include <string.h>

#include "flib/flMemory.h"

// Field types (RTFieldDef::type).
static const int RT_TYPE_CHAR = 1;   // Embedded string of arraySize characters
static const int RT_TYPE_STRING = 8; // Heap-allocated string, freed with its entry
static const int RT_TYPE_PTR = 9;

// Allocates the tables' own storage (field and entry arrays, entry data,
// field names).
class RecordTableAllocator {
public:
    virtual void* allocate(u32 size, u32 align);
    virtual void free(void* ptr);
};

extern RecordTableAllocator gRecordTableAllocator;

inline RecordTableAllocator* RTAllocator() {
    return &gRecordTableAllocator;
}

// A growable array. It doubles its capacity when full.
template <typename T>
struct RTArray {
    T* data;      // at 0x0
    int capacity; // at 0x4
    int count;    // at 0x8

    RTArray(int initialCapacity) : data(NULL), capacity(initialCapacity), count(0) {}

    ~RTArray() { Destroy(); }

    void Destroy() {
        T* d = data;
        int n = count;
        for (int i = 0; i < n; i++) {
            d[i].~T();
        }
        if (d != NULL) {
            RTAllocator()->free(d);
        }
        data = NULL;
    }

    void Clear() {
        Destroy();
        capacity = 1;
        count = 0;
    }

    // Removes an element by moving the last element into its place.
    void RemoveSwap(int index) {
        data[index].~T();
        count--;
        if (index < count) {
            memcpy(&data[index], &data[count], sizeof(T));
        }
    }

    int Count() const { return count; }

    T* Append() {
        if (data == NULL || count >= capacity) {
            T* newData;
            int oldCapacity = capacity;
            capacity = oldCapacity * 2;
            newData = (T*)RTAllocator()->allocate(oldCapacity * 2 * sizeof(T), 4);
            if (data != NULL) {
                memcpy(newData, data, oldCapacity * sizeof(T));
                if (data != NULL) {
                    RTAllocator()->free(data);
                }
            }
            data = newData;
        }
        return new (&data[count++]) T;
    }
};

struct RTFieldDef {
    u32 offset;       // at 0x0
    const char* name; // at 0x4
    u16 type;         // at 0x8
    u16 size;         // at 0xA
    u32 arraySize;    // at 0xC

    RTFieldDef() : name(NULL) {}
};

struct RecordTable;

struct RTEntry {
    RecordTable* table; // at 0x0
    u8* data;           // at 0x4

    RTEntry() : table(NULL), data(NULL) {}
    ~RTEntry() { Free(); }

    inline void Free();
    inline void Init(RecordTable* table);

    void SetEmbeddedStringField(const char* name, const char* value);
    void SetAllocatedStringField(const char* name, const char* value);
};

// Size of one element of each field type.
struct RTTypeSize {
    u32 type;
    u32 size;
};

struct RecordTable {
    RTArray<RTFieldDef> fields; // at 0x0, sorted by name
    RTArray<RTEntry> entries;   // at 0xC
    u32 entrySize;              // at 0x18

    RecordTable(int fieldCapacity) : fields(fieldCapacity), entries(1), entrySize(0) {}
    inline ~RecordTable();

    void* operator new(size_t size) { return RTAllocator()->allocate(size, 4); }
    void operator delete(void* ptr) { RTAllocator()->free(ptr); }

    void RecordTableClear();

    static RTTypeSize ElementSizes[10];
};

// TODO: These keep their C names until the real signatures are known.
extern "C" {
    RecordTable* GetOrCreateNamedRT(const char* name, int nameLength);
    RecordTable* CreateNewRecordTable(const char* name, int fieldCapacity);
    RecordTable* RecordTablePtrByName(const char* name);
    void* RecordTablePtrValByName(RecordTable* table, const char* name);
    void DeleteRecordTable(const char* name);
    RecordTable* GetBaseRecordTable();
    void InitMainRecordTable();
    void AddField(RecordTable* table, const char* name, int type, int arraySize);
    RTEntry* FindEntryByName(RecordTable* table, const char* name);
    void DeleteEntryByName(RecordTable* table, const char* name);
    RTEntry* AddNamedEntry(RecordTable* table, const char* name);
    RTEntry* AddNamedPtrEntry(RecordTable* table, const char* name, void* ptr);
    RTEntry* RecordTableAllocNewEntry(RecordTable* table);
    RTEntry* RecordTableAddElement(RecordTable* table, int index);
    void RecordTableDeleteEntry(RecordTable* table, RTEntry* entry);
    void RemoveSortedEntry(RecordTable* table, RTEntry* entry);
    RTFieldDef* GetFieldDef(RecordTable* table, const char* name);
}

extern RecordTable* Module2RT_Table;

inline RecordTable::~RecordTable() {
    if (this != NULL) {
        RecordTableClear();
    }
}

// Frees the entry's data, along with any heap-allocated strings in it.
inline void RTEntry::Free() {
    if (data != NULL) {
        int n = table->fields.count;
        for (int i = 0; i < n; i++) {
            RTFieldDef* def = &table->fields.data[i];
            int type = def->type;
            if (type == RT_TYPE_STRING) {
                char** str = (char**)(data + def->offset);
                if (*str != NULL) {
                    gMemoryManager.free(*str);
                    *str = NULL;
                }
            }
        }
        if (data != NULL) {
            gMemoryManager.free(data);
            data = NULL;
        }
    }
}

inline void RTEntry::Init(RecordTable* table) {
    this->table = table;
    u32 size = table->entrySize;
    data = (u8*)RTAllocator()->allocate(size, 4);
    memset(data, 0, size);
}

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
