#include "flib/RecordTable.h"
#include "flib/ScriptLog.h"
#include "flib/flVecMath.h"
#include "flib/scripting/Syscalls.h"

#include <revolution/OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TODO: These live in TUs that aren't decompiled yet; use C names until the
// real signatures are known.
extern "C" {
    double sin(double x);
    double cos(double x);
    double tan(double x);
    double atan(double x);
    double atan2(double y, double x);
    double asin(double x);
    double acos(double x);
    double ceil(double x);
    double floor(double x);
    double log10(double x);
    double pow(double x, double y);

    f32 fn_80203830(f32 x);
    f32 fn_802038B0(f32 x);
    f32 fn_80203750(f32 x);
    f32 fn_802036A0(f32 x);
}

namespace nsDelhi {
    namespace nsScript {
        class ScriptInterface {
        public:
            virtual u32 virt0x8();
            virtual u32 virt0xC(u32* args, int count);
            virtual u32 virt0x10();
            virtual u32 virt0x14();
            virtual u32 virt0x18(u32 arg, u32* args, int count);
            virtual u32 virt0x1C();
        };
    }
}

char* stack_vprintf(char* out, const char* fmt, u32* args);

u32 fn_802EF620(u32* args, int count) {
    return 0;
}

u32 ErrorPrintSyscall(u32* args, int count) {
    ErrorHandling("%s", (const char*)args[0]);
    return 0;
}

u32 LogPrintfVSyscall(u32* args, int count) {
    char buf[0x400];
    stack_vprintf(buf, (const char*)args[0], (u32*)args[1]);
    LogWrite("%s", buf);
    return 0;
}

u32 LogPrintfSyscall(u32* args, int count) {
    char buf[0x400];
    stack_vprintf(buf, (const char*)args[0], &args[1]);
    LogWrite("%s", buf);
    return 0;
}

u32 LogPrintf2VSyscall(u32* args, int count) {
    char buf[0x400];
    stack_vprintf(buf, (const char*)args[0], (u32*)args[1]);
    LogWrite2("%s", buf);
    return 0;
}

u32 LogPrintf2Syscall(u32* args, int count) {
    char buf[0x400];
    stack_vprintf(buf, (const char*)args[0], &args[1]);
    LogWrite2("%s", buf);
    return 0;
}

u32 ScriptLogSyscall(u32* args, int count) {
    ScriptLog(args[0], (const char*)args[1]);
    return 0;
}

u32 SetLogLevelSyscall0x7(u32* args, int count) {
    if (args[0] != 0) {
        SetLoggingBits(SCRIPTLOG_SCRIPT);
    } else {
        ClearLoggingBits(SCRIPTLOG_SCRIPT);
    }
    return 0;
}

u32 fmulSyscall(u32* args, int count) {
    f32* f = (f32*)args;
    f32 b = f[1];
    return SyscallFloatResult(f[0] * b);
}

u32 fdivSyscall(u32* args, int count) {
    f32* f = (f32*)args;
    f32 b = f[1];
    return SyscallFloatResult(f[0] / b);
}

u32 fn_802EF860(u32* args, int count) {
    u16 angle = args[0];
    return SyscallFloatResult(fn_80203830(__OSu16tof32(&angle) * (1.0f / 256.0f)));
}

u32 fn_802EF8A0(u32* args, int count) {
    u16 angle = args[0];
    return SyscallFloatResult(fn_802038B0(__OSu16tof32(&angle) * (1.0f / 256.0f)));
}

u32 fsinSyscall(u32* args, int count) {
    return SyscallFloatResult(sin(SyscallFloatArg(args[0])));
}

u32 fcosSyscall(u32* args, int count) {
    return SyscallFloatResult(cos(SyscallFloatArg(args[0])));
}

u32 randSyscall(u32* args, int count) {
    s32 range = args[0];
    return (range * (rand() & 0x7FFF)) >> 15;
}

u32 randFloat(u32* args, int count) {
    f32 scale = SyscallFloatArg(args[0]);
    return SyscallFloatResult(scale * ((rand() & 0x7FFF) * (1.0f / 32767.0f)));
}

u32 randFloatCentered(u32* args, int count) {
    f32 scale = SyscallFloatArg(args[0]);
    return SyscallFloatResult(scale * ((rand() & 0x7FFF) * (1.0f / 32767.0f)) - 0.5f * scale);
}

u32 sqrtSyscall(u32* args, int count) {
    f32 x = SyscallFloatArg(args[0]);
    return SyscallFloatResult(x <= 0.0f ? 0.0f : x * Newton_Raphson_invsqrt(x));
}

u32 freciprocalSyscall(u32* args, int count) {
    return SyscallFloatResult(1.0f / SyscallFloatArg(args[0]));
}

u32 invSqrtSyscall(u32* args, int count) {
    return SyscallFloatResult(Newton_Raphson_invsqrt(SyscallFloatArg(args[0])));
}

u32 degreesToRadiansSyscall(u32* args, int count) {
    return SyscallFloatResult(0.017453292f * SyscallFloatArg(args[0]));
}

u32 radiansToDegreesSyscall(u32* args, int count) {
    return SyscallFloatResult(57.295776f * SyscallFloatArg(args[0]));
}

u32 fn_802EFB60(u32* args, int count) {
    return (s32)(182.04445f * SyscallFloatArg(args[0]));
}

u32 fn_802EFB90(u32* args, int count) {
    f32 x = SyscallFloatArg(args[0]);
    return SyscallFloatResult(x > 0.0f ? fn_80203750(x) : -(0.0f / 0.0f));
}

u32 log10Syscall(u32* args, int count) {
    return SyscallFloatResult(log10(SyscallFloatArg(args[0])));
}

u32 fn_802EFC00(u32* args, int count) {
    return SyscallFloatResult(fn_802036A0(SyscallFloatArg(args[0])));
}

u32 powSyscall(u32* args, int count) {
    return SyscallFloatResult(pow(SyscallFloatArg(args[0]), SyscallFloatArg(args[1])));
}

u32 tanSyscall(u32* args, int count) {
    return SyscallFloatResult(tan(SyscallFloatArg(args[0])));
}

u32 atanSyscall(u32* args, int count) {
    return SyscallFloatResult(atan(SyscallFloatArg(args[0])));
}

u32 atan2Syscall(u32* args, int count) {
    return SyscallFloatResult(atan2(SyscallFloatArg(args[0]), SyscallFloatArg(args[1])));
}

u32 asinSyscall(u32* args, int count) {
    return SyscallFloatResult(asin(SyscallFloatArg(args[0])));
}

u32 acosSyscall(u32* args, int count) {
    return SyscallFloatResult(acos(SyscallFloatArg(args[0])));
}

u32 ceilSyscall(u32* args, int count) {
    return SyscallFloatResult(ceil(SyscallFloatArg(args[0])));
}

u32 floorSyscall(u32* args, int count) {
    return SyscallFloatResult(floor(SyscallFloatArg(args[0])));
}

u32 fabsSyscall(u32* args, int count) {
    return SyscallFloatResult(__fabsf(SyscallFloatArg(args[0])));
}

u32 utofSyscall(u32* args, int count) {
    return SyscallFloatResult((s32)args[0]);
}

u32 ftoiSyscall(u32* args, int count) {
    return (s32)SyscallFloatArg(args[0]);
}

u32 strlenSyscall(u32* args, int count) {
    const char* str = (const char*)args[0];
    if (str != NULL) {
        return strlen(str);
    }
    return 0;
}

u32 strcmpSyscall(u32* args, int count) {
    return strcmp((const char*)args[0], (const char*)args[1]);
}

u32 stricmpSyscall(u32* args, int count) {
    return stricmp((const char*)args[0], (const char*)args[1]);
}

u32 strcpySyscall(u32* args, int count) {
    strcpy((char*)args[0], (const char*)args[1]);
    return 0;
}

// Strips the directory and extension from a path in place, returning the
// start of the file name.
u32 FilenameWithoutExtensionSyscall(u32* args, int count) {
    char* name = (char*)args[0];
    for (char* p = name; *p != '\0'; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':') {
            name = p + 1;
        } else if (c == '.') {
            *p = '\0';
            break;
        }
    }
    return (u32)name;
}

u32 ModuleByNameSyscall(u32* args, int count) {
    return (u32)RecordTablePtrValByName(Module2RT_Table, (const char*)args[0]);
}

u32 ScriptInterfaceVirt1(u32* args, int count) {
    return ((nsDelhi::nsScript::ScriptInterface*)args[0])->virt0x8();
}

u32 ScriptInterfaceVirt2(u32* args, int count) {
    return ((nsDelhi::nsScript::ScriptInterface*)args[0])->virt0xC(&args[1], count - 1);
}

u32 ScriptInterfaceVirt3(u32* args, int count) {
    return ((nsDelhi::nsScript::ScriptInterface*)args[0])->virt0x10();
}

u32 ScriptInterfaceVirt4(u32* args, int count) {
    return ((nsDelhi::nsScript::ScriptInterface*)args[0])->virt0x14();
}

u32 ScriptInterfaceVirt5(u32* args, int count) {
    return ((nsDelhi::nsScript::ScriptInterface*)args[1])->virt0x18(args[0], &args[2], count - 2);
}

u32 ScriptInterfaceVirt6(u32* args, int count) {
    return ((nsDelhi::nsScript::ScriptInterface*)args[0])->virt0x1C();
}

u32 CreateNewRecordTableSyscall(u32* args, int count) {
    return (u32)CreateNewRecordTable((const char*)args[0], args[1]);
}

u32 RecordTablePtrByNameSyscall(u32* args, int count) {
    return (u32)RecordTablePtrByName((const char*)args[0]);
}

u32 RecordTableGetEntryByName_IndexSyscall(u32* args, int count) {
    RecordTable* table = RecordTablePtrByName((const char*)args[0]);
    return (u32)&table->entries[args[1]];
}

u32 DeleteRecordTableSyscall(u32* args, int count) {
    DeleteRecordTable((RecordTable*)args[0]);
    return 0;
}

u32 GetBaseRecordTableSyscall(u32* args, int count) {
    return (u32)GetBaseRecordTable();
}

u32 RecordTableAddField(u32* args, int count) {
    AddField((RecordTable*)args[0], (const char*)args[1], args[2], args[3]);
    return 0;
}

u32 GetFieldDefByNameSyscall(u32* args, int count) {
    return (u32)GetFieldDef((RecordTable*)args[0], (const char*)args[1]);
}

u32 RecordTableGetElementSize(u32* args, int count) {
    return ((RecordTable*)args[0])->entrySize;
}

u32 RecordTableGetFieldCount(u32* args, int count) {
    return ((RecordTable*)args[0])->fieldCount;
}

u32 GetFieldDefByIndexSyscall(u32* args, int count) {
    return (u32)&((RecordTable*)args[0])->fields[args[1]];
}

u32 RecordTableGetNumElementsSyscall(u32* args, int count) {
    return ((RecordTable*)args[0])->entryCount;
}

u32 RecordTableGetEntryByIndex(u32* args, int count) {
    return (u32)&((RecordTable*)args[0])->entries[args[1]];
}

u32 noopSyscall(u32* args, int count) {
    return 0;
}

u32 RecordTableDeleteUnsortedEntrySyscall(u32* args, int count) {
    RecordTableDeleteEntry((RecordTable*)args[0], args[1]);
    return 0;
}

u32 RecordTableDeleteSortedEntrySyscall(u32* args, int count) {
    RemoveSortedEntry((RecordTable*)args[0], args[1]);
    return 0;
}

u32 AllocNewEntrySyscall(u32* args, int count) {
    return (u32)RecordTableAllocNewEntry((RecordTable*)args[0]);
}

u32 AddNamedEntrySyscall(u32* args, int count) {
    return (u32)AddNamedEntry((RecordTable*)args[0], (const char*)args[1]);
}

u32 FindEntryByNameSyscall(u32* args, int count) {
    return (u32)FindEntryByName((RecordTable*)args[0], (const char*)args[1]);
}

u32 RecordTableInsertNewEntrySyscall(u32* args, int count) {
    return (u32)RecordTableAddElement((RecordTable*)args[0], args[1]);
}

u32 RecordTableFieldGetNameSyscall(u32* args, int count) {
    return (u32)((RTFieldDef*)args[0])->name;
}

u32 RecordTableFieldGetOffsetSyscall(u32* args, int count) {
    return ((RTFieldDef*)args[0])->offset;
}

u32 RecordTableFieldGetTypeSyscall(u32* args, int count) {
    return ((RTFieldDef*)args[0])->type;
}

u32 RecordTableFieldGetSizeSyscall(u32* args, int count) {
    return ((RTFieldDef*)args[0])->size;
}

u32 RecordTableFieldGetArraySizeSyscall(u32* args, int count) {
    return ((RTFieldDef*)args[0])->arraySize;
}

u32 RecordTablePropGetArrayElemOffsetSyscall(u32* args, int count) {
    RTFieldDef* def = (RTFieldDef*)args[0];
    return def->offset + args[1] * def->size;
}

u32 RecordTableEntryGetRecordTableSyscall(u32* args, int count) {
    return (u32)((RTEntry*)args[0])->table;
}

u32 RecordTableEntryGetIntPropSyscall(u32* args, int count) {
    return GetRTField<s32>((RTEntry*)args[0], (const char*)args[1]);
}

u32 RecordTableEntrySetIntPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    s32 value = args[2];
    GetRTField<s32>(entry, (const char*)args[1]) = value;
    return 0;
}

u32 RecordTableEntryGetShortPropSyscall(u32* args, int count) {
    return GetRTField<s16>((RTEntry*)args[0], (const char*)args[1]);
}

u32 RecordTableEntrySetShortSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    s16 value = args[2];
    GetRTField<s16>(entry, (const char*)args[1]) = value;
    return 0;
}

u32 RecordTableEntryGetCharPropSyscall(u32* args, int count) {
    return GetRTField<s8>((RTEntry*)args[0], (const char*)args[1]);
}

u32 RecordTableEntrySetCharPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    s8 value = args[2];
    GetRTField<s8>(entry, (const char*)args[1]) = value;
    return 0;
}

u32 RecordTableGetFloatPropSyscall(u32* args, int count) {
    return SyscallFloatResult(GetRTField<f32>((RTEntry*)args[0], (const char*)args[1]));
}

u32 RecordTableSetFloatPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    f32 value = SyscallFloatArg(args[2]);
    GetRTField<f32>(entry, (const char*)args[1]) = value;
    return 0;
}

u32 RecordTableEntryGetPointerPropSyscall(u32* args, int count) {
    return (u32)GetRTField<void*>((RTEntry*)args[0], (const char*)args[1]);
}

u32 RecordTableEntrySetPointerPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    void* value = (void*)args[2];
    GetRTField<void*>(entry, (const char*)args[1]) = value;
    return 0;
}

u32 RecordTableEntryGetEmbeddedStringPropSyscall(u32* args, int count) {
    return (u32)&GetRTField<char>((RTEntry*)args[0], (const char*)args[1]);
}

u32 RecordTableEntrySetEmbeddedStringPropSyscall(u32* args, int count) {
    ((RTEntry*)args[0])->SetEmbeddedStringField((const char*)args[1], (const char*)args[2]);
    return 0;
}

u32 RecordTableEntryGetAllocatedStringPropSyscall(u32* args, int count) {
    return (u32)GetRTField<char*>((RTEntry*)args[0], (const char*)args[1]);
}

u32 RecordTableEntrySetAllocatedStringPropSyscall(u32* args, int count) {
    ((RTEntry*)args[0])->SetAllocatedStringField((const char*)args[1], (const char*)args[2]);
    return 0;
}

u32 RecordTableEntryGetIntArrayPropElementSyscall(u32* args, int count) {
    int index = args[2];
    RTEntry* entry = (RTEntry*)args[0];
    return GetRTArrayField<s32>(entry, (const char*)args[1], index);
}

u32 RecordTableEntrySetIntArrayPropElementSyscall(u32* args, int count) {
    int index;
    RTEntry* entry = (RTEntry*)args[0];
    s32 value = args[3];
    index = args[2];
    GetRTArrayField<s32>(entry, (const char*)args[1], index) = value;
    return 0;
}

u32 RecordTableEntryGetShortArrayPropElementSyscall(u32* args, int count) {
    int index = args[2];
    RTEntry* entry = (RTEntry*)args[0];
    return GetRTArrayField<s16>(entry, (const char*)args[1], index);
}

u32 RecordTableEntrySetShortArrayPropElementSyscall(u32* args, int count) {
    int index;
    RTEntry* entry = (RTEntry*)args[0];
    s16 value = args[3];
    index = args[2];
    GetRTArrayField<s16>(entry, (const char*)args[1], index) = value;
    return 0;
}

u32 RecordTableEntryGetCharArrayPropElementSyscall(u32* args, int count) {
    int index = args[2];
    RTEntry* entry = (RTEntry*)args[0];
    return GetRTArrayField<s8>(entry, (const char*)args[1], index);
}

u32 RecordTableEntrySetCharArrayPropElementSyscall(u32* args, int count) {
    int index;
    RTEntry* entry = (RTEntry*)args[0];
    s8 value = args[3];
    index = args[2];
    GetRTArrayField<s8>(entry, (const char*)args[1], index) = value;
    return 0;
}

u32 RecordTableEntryGetFloatArrayPropElementSyscall(u32* args, int count) {
    int index = args[2];
    RTEntry* entry = (RTEntry*)args[0];
    return SyscallFloatResult(GetRTArrayField<f32>(entry, (const char*)args[1], index));
}

u32 RecordTableEntrySetFloatArrayPropElementSyscall(u32* args, int count) {
    int index;
    RTEntry* entry = (RTEntry*)args[0];
    f32 value = SyscallFloatArg(args[3]);
    index = args[2];
    GetRTArrayField<f32>(entry, (const char*)args[1], index) = value;
    return 0;
}

u32 RecordTableEntryFillIntArrayPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    RTFieldDef* def = GetFieldDef(entry->table, (const char*)args[1]);
    int n = count - 2;
    for (int i = 0; i < n; i++) {
        *(s32*)(entry->data + def->offset + i * def->size) = args[i + 2];
    }
    return 0;
}

u32 RecordTableEntryFillShortArrayPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    RTFieldDef* def = GetFieldDef(entry->table, (const char*)args[1]);
    int n = count - 2;
    for (int i = 0; i < n; i++) {
        *(s16*)(entry->data + def->offset + i * def->size) = args[i + 2];
    }
    return 0;
}

u32 RecordTableEntryFillCharArrayPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    RTFieldDef* def = GetFieldDef(entry->table, (const char*)args[1]);
    int n = count - 2;
    for (int i = 0; i < n; i++) {
        *(s8*)(entry->data + def->offset + i * def->size) = args[i + 2];
    }
    return 0;
}

u32 RecordTableEntryFillFloatArrayPropSyscall(u32* args, int count) {
    RTEntry* entry = (RTEntry*)args[0];
    RTFieldDef* def = GetFieldDef(entry->table, (const char*)args[1]);
    int n = count - 2;
    for (int i = 0; i < n; i++) {
        *(f32*)(entry->data + def->offset + i * def->size) = ((f32*)args)[i + 2];
    }
    return 0;
}

ScriptSyscall SYSCALL_PAGE0[0x90] = {
    fn_802EF620,
    ErrorPrintSyscall,
    LogPrintfVSyscall,
    LogPrintfSyscall,
    LogPrintf2VSyscall,
    LogPrintf2Syscall,
    ScriptLogSyscall,
    SetLogLevelSyscall0x7,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ModuleByNameSyscall,
    ScriptInterfaceVirt1,
    ScriptInterfaceVirt2,
    ScriptInterfaceVirt3,
    ScriptInterfaceVirt4,
    ScriptInterfaceVirt5,
    ScriptInterfaceVirt6,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    fmulSyscall,
    fdivSyscall,
    fsinSyscall,
    fcosSyscall,
    randSyscall,
    sqrtSyscall,
    freciprocalSyscall,
    invSqrtSyscall,
    fn_802EF860,
    fn_802EF8A0,
    randFloat,
    randFloatCentered,
    degreesToRadiansSyscall,
    radiansToDegreesSyscall,
    fn_802EFB60,
    fn_802EFB90,
    log10Syscall,
    fn_802EFC00,
    powSyscall,
    tanSyscall,
    atanSyscall,
    atan2Syscall,
    asinSyscall,
    acosSyscall,
    ceilSyscall,
    floorSyscall,
    fabsSyscall,
    utofSyscall,
    ftoiSyscall,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    strlenSyscall,
    strcmpSyscall,
    stricmpSyscall,
    strcpySyscall,
    FilenameWithoutExtensionSyscall,
    NULL,
    NULL,
    NULL,
    CreateNewRecordTableSyscall,
    RecordTablePtrByNameSyscall,
    DeleteRecordTableSyscall,
    GetBaseRecordTableSyscall,
    RecordTableGetEntryByName_IndexSyscall,
    RecordTableAddField,
    GetFieldDefByNameSyscall,
    RecordTableGetElementSize,
    RecordTableGetFieldCount,
    GetFieldDefByIndexSyscall,
    RecordTableGetNumElementsSyscall,
    RecordTableGetEntryByIndex,
    noopSyscall,
    RecordTableDeleteUnsortedEntrySyscall,
    RecordTableDeleteSortedEntrySyscall,
    AllocNewEntrySyscall,
    RecordTableInsertNewEntrySyscall,
    NULL,
    RecordTableFieldGetNameSyscall,
    RecordTableFieldGetOffsetSyscall,
    RecordTableFieldGetTypeSyscall,
    RecordTableFieldGetSizeSyscall,
    RecordTableFieldGetArraySizeSyscall,
    RecordTablePropGetArrayElemOffsetSyscall,
    NULL,
    RecordTableEntryGetRecordTableSyscall,
    RecordTableEntryGetCharPropSyscall,
    RecordTableEntrySetCharPropSyscall,
    RecordTableEntryGetShortPropSyscall,
    RecordTableEntrySetShortSyscall,
    RecordTableEntryGetIntPropSyscall,
    RecordTableEntrySetIntPropSyscall,
    RecordTableGetFloatPropSyscall,
    RecordTableSetFloatPropSyscall,
    RecordTableEntryGetPointerPropSyscall,
    RecordTableEntrySetPointerPropSyscall,
    RecordTableEntryGetEmbeddedStringPropSyscall,
    RecordTableEntrySetEmbeddedStringPropSyscall,
    RecordTableEntryGetAllocatedStringPropSyscall,
    RecordTableEntrySetAllocatedStringPropSyscall,
    RecordTableEntryGetCharArrayPropElementSyscall,
    RecordTableEntrySetCharArrayPropElementSyscall,
    RecordTableEntryGetShortArrayPropElementSyscall,
    RecordTableEntrySetShortArrayPropElementSyscall,
    RecordTableEntryGetIntArrayPropElementSyscall,
    RecordTableEntrySetIntArrayPropElementSyscall,
    RecordTableEntryGetFloatArrayPropElementSyscall,
    RecordTableEntrySetFloatArrayPropElementSyscall,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    RecordTableEntryFillCharArrayPropSyscall,
    RecordTableEntryFillShortArrayPropSyscall,
    RecordTableEntryFillIntArrayPropSyscall,
    RecordTableEntryFillFloatArrayPropSyscall,
    NULL,
    NULL,
    AddNamedEntrySyscall,
    FindEntryByNameSyscall,
};

static char sNullString[] = "<NULL>";

static inline char* CopyString(char* out, const char* str) {
    if (str == NULL) {
        str = "<NULL>";
    }
    while ((*out++ = *str++) != '\0') {
    }
    return out - 1;
}

char* stack_vprintf_d_specifier(char* out, int value, int width, char pad);
char* stack_vprintf_s_specifier(char* out, const char* str, int width, int maxLen);

// A minimal printf over the script argument stack. Supports %d, %s, %c, %x
// and %f, the '-' and '0' flags, and a width and precision.
char* stack_vprintf(char* out, const char* fmt, u32* args) {
    char spec[16];
    char hex[16];

    while (*fmt != '\0') {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }

        int num = 0;
        int width = 0;
        int sign = 1;
        char pad = ' ';
        for (;;) {
            char c = *++fmt;
            switch (c) {
            case 'f': {
                f32 value = SyscallFloatArg(*args++);
                sprintf(spec, "%%%d.%df", width, num != 0 ? num : 6);
                sprintf(out, spec, value);
                out += strlen(out);
                goto next;
            }
            case 's':
                out = stack_vprintf_s_specifier(out, (const char*)*args++, num * sign, -1);
                goto next;
            case 'c':
                *out++ = *args++;
                goto next;
            case 'd':
                out = stack_vprintf_d_specifier(out, *args++, num * sign, pad);
                goto next;
            case 'x': {
                int w = num * sign;
                u32 value = *args++;
                if (w == 0) {
                    char* p = &hex[15];
                    *p = '\0';
                    do {
                        int digit = value & 0xF;
                        *--p = digit > 9 ? digit + 'A' - 10 : digit + '0';
                    } while ((value >>= 4) != 0);
                    out = CopyString(out, p);
                } else {
                    out += w;
                    *out = '\0';
                    char* p = out;
                    do {
                        int digit = value & 0xF;
                        *--p = digit > 9 ? digit + 'A' - 10 : digit + '0';
                        w--;
                    } while ((value >>= 4) != 0 && w > 0);
                    while (w > 0) {
                        *--p = pad;
                        w--;
                    }
                }
                goto next;
            }
            case '.':
                width = num * sign;
                num = 0;
                sign = 1;
                continue;
            case '-':
                sign = -1;
                continue;
            case '0':
                if (num == 0) {
                    pad = '0';
                }
                // fallthrough
            default:
                if ((u8)(*fmt - '0') <= 9) {
                    num = num * 10 + (*fmt & 0xF);
                    continue;
                }
                *out++ = *fmt;
                goto next;
            }
        }
    next:
        fmt++;
    }

    *out = '\0';
    return out;
}

static inline char* IntToString(char* end, int value, BOOL* negative) {
    *negative = FALSE;
    if (value < 0) {
        *negative = TRUE;
        value = -value;
    }
    do {
        *--end = value % 10 + '0';
    } while ((value /= 10) != 0);
    return end;
}

char* stack_vprintf_d_specifier(char* out, int value, int width, char pad) {
    if (width < 0) {
        char buf[32];
        BOOL negative = FALSE;
        char* p = &buf[31];
        *p = negative;
        if (value < 0) {
            negative = TRUE;
            value = -value;
        }
        do {
            *--p = value % 10 + '0';
        } while ((value /= 10) != 0);
        char* cur = out;
        if (negative) {
            *cur++ = '-';
        }
        cur = CopyString(cur, p);
        char* end = out - width;
        while (cur < end) {
            *cur++ = ' ';
        }
        *cur = 0;
        return cur;
    } else if (width == 0) {
        char buf[32];
        BOOL negative = FALSE;
        char* p = &buf[31];
        *p = negative;
        if (value < 0) {
            negative = TRUE;
            value = -value;
        }
        do {
            *--p = value % 10 + '0';
        } while ((value /= 10) != 0);
        if (negative) {
            *out++ = '-';
        }
        return CopyString(out, p);
    } else {
        out += width;
        *out = 0;
        char* p = out;
        BOOL negative = FALSE;
        if (value < 0) {
            negative = TRUE;
            value = -value;
        }
        do {
            *--p = value % 10 + '0';
            width--;
        } while ((value /= 10) != 0 && width > 0);
        if (negative && width > 0) {
            *--p = '-';
            width--;
        }
        while (width > 0) {
            *--p = pad;
            width--;
        }
        return out;
    }
}

char* stack_vprintf_s_specifier(char* out, const char* str, int width, int maxLen) {
    if (str == NULL) {
        str = sNullString;
    }

    if (width < 0) {
        width = -width;
        while (width != 0 && maxLen-- != 0 && *str != '\0') {
            *out++ = *str++;
            width--;
        }
        while (width > 0) {
            *out++ = ' ';
            width--;
        }
    } else {
        const char* p = str;
        int n = maxLen;
        while (n-- != 0 && *p != '\0') {
            p++;
            width--;
        }
        while (width > 0) {
            *out++ = ' ';
            width--;
        }
        while (maxLen-- != 0 && *str != '\0') {
            *out++ = *str++;
        }
    }

    *out = '\0';
    return out;
}
