#ifndef FLIB_FLVECMATH_H
#define FLIB_FLVECMATH_H

#include <revolution/MTX.h>

// TODO: Lives in a TU that isn't decompiled yet; use its C name until the
// real signature is known.
extern "C" f32 Newton_Raphson_invsqrt(f32 x);

// Paired-single Vec helpers. They match the inline asm used by the original
// code; NW4R's math headers have equivalents.

inline void flVecAdd(register Vec* out, register const Vec* a, register const Vec* b) {
    register f32 t1, t0;
    asm {
        psq_l t0, 0(a), 0, 0
        psq_l t1, 0(b), 0, 0
        ps_add t0, t0, t1
        psq_st t0, 0(out), 0, 0
        psq_l t0, 8(a), 1, 0
        psq_l t1, 8(b), 1, 0
        ps_add t0, t0, t1
        psq_st t0, 8(out), 1, 0
    }
}

inline void flVecSub(register Vec* out, register const Vec* a, register const Vec* b) {
    register f32 t1, t0;
    asm {
        psq_l t0, 0(a), 0, 0
        psq_l t1, 0(b), 0, 0
        ps_sub t0, t0, t1
        psq_st t0, 0(out), 0, 0
        psq_l t0, 8(a), 1, 0
        psq_l t1, 8(b), 1, 0
        ps_sub t0, t0, t1
        psq_st t0, 8(out), 1, 0
    }
}

inline void flVecScale(register Vec* out, register const Vec* in, register f32 scale) {
    register f32 t0;
    asm {
        psq_l t0, 0(in), 0, 0
        ps_muls0 t0, t0, scale
        psq_st t0, 0(out), 0, 0
        psq_l t0, 8(in), 1, 0
        ps_muls0 t0, t0, scale
        psq_st t0, 8(out), 1, 0
    }
}

inline f32 flVecSquareMag(register const Vec* v) {
    register f32 z, xy;
    asm {
        psq_l xy, 0(v), 0, 0
        ps_mul xy, xy, xy
        lfs z, 8(v)
        ps_madd z, z, z, xy
        ps_sum0 z, z, xy, xy
    }
    return z;
}

inline f32 flVecInvMag(register const Vec* v) {
    register f32 xy, z, sq;
    asm {
        psq_l xy, 0(v), 0, 0
        ps_mul xy, xy, xy
        lfs z, 8(v)
        ps_madd sq, z, z, xy
        ps_sum0 sq, sq, xy, xy
    }
    return Newton_Raphson_invsqrt(sq);
}

inline f32 flVecDot(register const Vec* a, register const Vec* b) {
    register f32 sum, ax, bx, prodYZ, res;
    asm {
        psq_l prodYZ, 4(a), 0, 0
        psq_l sum, 4(b), 0, 0
        ps_mul prodYZ, prodYZ, sum
        psq_l ax, 0(a), 1, 0
        psq_l bx, 0(b), 1, 0
        ps_madd sum, ax, bx, prodYZ
        ps_sum0 res, sum, prodYZ, prodYZ
    }
    return res;
}

#endif
