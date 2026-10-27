#include "flib/flVecMath.h"
#include "flib/scripting/Syscalls.h"

// TODO: These live in TUs that aren't decompiled yet; use C names until the
// real signatures are known.
extern "C" {
    void Vec3ElementMax(Vec* out, const Vec* a, const Vec* b);
    void Vec3ElementMin(Vec* out, const Vec* a, const Vec* b);
}

u32 Vec3VecToResSyscall(u32* args) {
    return args[0];
}

u32 Vec3ZeroSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    v->x = 0.0f;
    v->y = 0.0f;
    v->z = 0.0f;
    return 0;
}

u32 Vec3GetXSyscall(u32* args) {
    return SyscallFloatResult(((Vec*)args[0])->x);
}

u32 Vec3GetYSyscall(u32* args) {
    return SyscallFloatResult(((Vec*)args[0])->y);
}

u32 Vec3GetZSyscall(u32* args) {
    return SyscallFloatResult(((Vec*)args[0])->z);
}

u32 Vec3SetXSyscall(u32* args) {
    f32 value = SyscallFloatArg(args[1]);
    ((Vec*)args[0])->x = value;
    return SyscallFloatResult(value);
}

u32 Vec3SetYSyscall(u32* args) {
    f32 value = SyscallFloatArg(args[1]);
    ((Vec*)args[0])->y = value;
    return SyscallFloatResult(value);
}

u32 Vec3SetZSyscall(u32* args) {
    f32 value = SyscallFloatArg(args[1]);
    ((Vec*)args[0])->z = value;
    return SyscallFloatResult(value);
}

u32 Vec3InitVecSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    v->x = SyscallFloatArg(args[1]);
    v->y = SyscallFloatArg(args[2]);
    v->z = SyscallFloatArg(args[3]);
    return (u32)v;
}

// TODO: Doesn't match. The original copies z, y, x and also leaves dead stores
// of all three components in three separate stack temporaries.
u32 Vec3CopySyscall(u32* args) {
    Vec* dst = (Vec*)args[0];
    *dst = *(Vec*)args[1];
    return (u32)dst;
}

u32 Vec3AddEqSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    flVecAdd(v, v, (Vec*)args[1]);
    return 0;
}

u32 Vec3SubEqSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    flVecSub(v, v, (Vec*)args[1]);
    return 0;
}

u32 Vec3ElementWiseProduct(u32* args) {
    Vec* v = (Vec*)args[0];
    Vec* other = (Vec*)args[1];
    v->x *= other->x;
    v->y *= other->y;
    v->z *= other->z;
    return 0;
}

u32 Vec3fmulSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    flVecScale(v, v, SyscallFloatArg(args[1]));
    return 0;
}

u32 Vec3DInvertSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    v->x = -v->x;
    v->y = -v->y;
    v->z = -v->z;
    return 0;
}

u32 Vec3LengthSqSyscall(u32* args) {
    return SyscallFloatResult(flVecSquareMag((Vec*)args[0]));
}

u32 Vec3LengthSyscall(u32* args) {
    return SyscallFloatResult(PSVECMag((Vec*)args[0]));
}

u32 Vec3LengthReciprocalSyscall(u32* args) {
    return SyscallFloatResult(flVecInvMag((Vec*)args[0]));
}

u32 Vec3NormalizeSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    PSVECNormalize(v, v);
    return 0;
}

u32 Vec3DotProductSyscall(u32* args) {
    return SyscallFloatResult(flVecDot((Vec*)args[0], (Vec*)args[1]));
}

u32 Vec3CrossProductSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    PSVECCrossProduct(v, (Vec*)args[1], v);
    return 0;
}

u32 Vec3ElementMaxSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    Vec3ElementMax(v, v, (Vec*)args[1]);
    return 0;
}

u32 Vec3ElementMinSyscall(u32* args) {
    Vec* v = (Vec*)args[0];
    Vec3ElementMin(v, v, (Vec*)args[1]);
    return 0;
}

u32 Vec3SubSyscall(u32* args) {
    Vec* out = (Vec*)args[0];
    Vec* a = (Vec*)args[1];
    Vec* b = (Vec*)args[2];
    out->x = a->x - b->x;
    out->y = a->y - b->y;
    out->z = a->z - b->z;
    return (u32)out;
}

ScriptSyscall SYSCALL_PAGE1[48] = {
    Vec3VecToResSyscall,
    Vec3InitVecSyscall,
    Vec3CopySyscall,
    Vec3SubSyscall,
    Vec3ZeroSyscall,
    Vec3GetXSyscall,
    Vec3GetYSyscall,
    Vec3GetZSyscall,
    Vec3SetXSyscall,
    Vec3SetYSyscall,
    Vec3SetZSyscall,
    Vec3AddEqSyscall,
    Vec3SubEqSyscall,
    Vec3ElementWiseProduct,
    Vec3fmulSyscall,
    Vec3DInvertSyscall,
    Vec3LengthSqSyscall,
    Vec3LengthSyscall,
    Vec3LengthReciprocalSyscall,
    Vec3NormalizeSyscall,
    Vec3DotProductSyscall,
    Vec3CrossProductSyscall,
    Vec3ElementMaxSyscall,
    Vec3ElementMinSyscall,
};

void SetSyscallPage1() {
    SetSyscallPage(1, SYSCALL_PAGE1);
}
