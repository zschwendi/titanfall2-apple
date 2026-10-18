/* C helpers of the int_arith family (lifter/insns/int_arith.py): the flag computations of adc,
 * sbb, inc, dec, neg and the logic instructions, and mul, imul, div, idiv.
 *
 * Flag helpers follow the Intel SDM vol. 2 "Flags Affected" sections.  A flag an instruction
 * leaves undefined may be written with anything or left alone; a flag an instruction leaves
 * *unchanged* must never be written.  x_szp, x_add and x_sub are in runtime.h. */
#ifndef TF_INSNS_INT_ARITH_H
#define TF_INSNS_INT_ARITH_H

#include "../runtime.h"

/* N = bits, T = unsigned type. */

#define TF_INT_ARITH(N, T)                                                                     \
TF_INLINE T x_adc##N(Ctx *c, T a, T b) {                                                       \
    T ci = c->cf;                                                                              \
    T r = (T)(a + b + ci);                                                                     \
    c->cf = (uint8_t)((r < a) | (ci & (r == a)));                                              \
    c->of = (uint8_t)((((T)((a ^ r) & (b ^ r))) >> (N - 1)) & 1);                              \
    c->af = (uint8_t)(((a ^ b ^ r) >> 4) & 1);                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
TF_INLINE T x_sbb##N(Ctx *c, T a, T b) {                                                       \
    T ci = c->cf;                                                                              \
    T r = (T)(a - b - ci);                                                                     \
    c->cf = (uint8_t)((a < b) | (ci & (a == b)));                                              \
    c->of = (uint8_t)((((T)((a ^ b) & (a ^ r))) >> (N - 1)) & 1);                              \
    c->af = (uint8_t)(((a ^ b ^ r) >> 4) & 1);                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
/* inc/dec: CF is not affected */                                                              \
TF_INLINE T x_inc##N(Ctx *c, T a) {                                                            \
    T r = (T)(a + 1);                                                                          \
    c->of = (uint8_t)(r == (T)((T)1 << (N - 1)));                                              \
    c->af = (uint8_t)((r & 0xF) == 0);                                                         \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
TF_INLINE T x_dec##N(Ctx *c, T a) {                                                            \
    T r = (T)(a - 1);                                                                          \
    c->of = (uint8_t)(a == (T)((T)1 << (N - 1)));                                              \
    c->af = (uint8_t)((r & 0xF) == 0xF);                                                       \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
/* neg: CF = (source != 0); otherwise as 0 - a */                                              \
TF_INLINE T x_neg##N(Ctx *c, T a) {                                                            \
    T r = (T)(0 - a);                                                                          \
    c->cf = (uint8_t)(a != 0);                                                                 \
    c->of = (uint8_t)(a == (T)((T)1 << (N - 1)));                                              \
    c->af = (uint8_t)(((a ^ r) >> 4) & 1);                                                     \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
/* and/or/xor/test: CF = OF = 0, SF ZF PF from the result, AF undefined (written 0) */         \
TF_INLINE T x_logic##N(Ctx *c, T r) {                                                          \
    c->cf = 0; c->of = 0; c->af = 0;                                                           \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}

TF_INT_ARITH(8,  uint8_t)
TF_INT_ARITH(16, uint16_t)
TF_INT_ARITH(32, uint32_t)
TF_INT_ARITH(64, uint64_t)

/* Two- and three-operand imul: truncated product; CF = OF = "the product did not fit".
 * SF ZF AF PF are undefined (SDM); SF is written with the sign of the truncated result,
 * the others are left alone. */
TF_INLINE uint16_t x_imul16(Ctx *c, uint16_t a, uint16_t b) {
    int32_t p = (int32_t)(int16_t)a * (int32_t)(int16_t)b;
    uint16_t r = (uint16_t)p;
    c->cf = c->of = (uint8_t)(p != (int32_t)(int16_t)r);
    c->sf = (uint8_t)(r >> 15);
    return r;
}
TF_INLINE uint32_t x_imul32(Ctx *c, uint32_t a, uint32_t b) {
    int64_t p = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
    uint32_t r = (uint32_t)p;
    c->cf = c->of = (uint8_t)(p != (int64_t)(int32_t)r);
    c->sf = (uint8_t)(r >> 31);
    return r;
}
TF_INLINE uint64_t x_imul64(Ctx *c, uint64_t a, uint64_t b) {
    __int128 p = (__int128)(int64_t)a * (__int128)(int64_t)b;
    uint64_t r = (uint64_t)p;
    c->cf = c->of = (uint8_t)(p != (__int128)(int64_t)r);
    c->sf = (uint8_t)(r >> 63);
    return r;
}

/* One-operand mul/imul: rdx:rax = rax * src (ax = al * src for 8-bit).
 * mul: CF = OF = (upper half != 0).  imul: CF = OF = (result != sign-extended lower half).
 * SF ZF AF PF are undefined (SDM).  imul writes SF with the sign of the lower half, which is
 * what the oracle does and what Capstone's flag table expects; the rest are left alone. */
#define TF_SET_AX(c, v)  ((c)->r[0] = ((c)->r[0] & ~UINT64_C(0xFFFF)) | (uint64_t)(uint16_t)(v))
#define TF_SET_DX(c, v)  ((c)->r[2] = ((c)->r[2] & ~UINT64_C(0xFFFF)) | (uint64_t)(uint16_t)(v))
TF_INLINE void x_mul1_8(Ctx *c, uint8_t s) {
    uint16_t p = (uint16_t)((uint16_t)(uint8_t)c->r[0] * (uint16_t)s);
    TF_SET_AX(c, p);
    c->cf = c->of = (uint8_t)((p >> 8) != 0);
}
TF_INLINE void x_mul1_16(Ctx *c, uint16_t s) {
    uint32_t p = (uint32_t)(uint16_t)c->r[0] * (uint32_t)s;
    TF_SET_AX(c, p); TF_SET_DX(c, p >> 16);
    c->cf = c->of = (uint8_t)((p >> 16) != 0);
}
TF_INLINE void x_mul1_32(Ctx *c, uint32_t s) {
    uint64_t p = (uint64_t)(uint32_t)c->r[0] * (uint64_t)s;
    c->r[0] = (uint32_t)p; c->r[2] = p >> 32;
    c->cf = c->of = (uint8_t)((p >> 32) != 0);
}
TF_INLINE void x_mul1_64(Ctx *c, uint64_t s) {
    unsigned __int128 p = (unsigned __int128)c->r[0] * s;
    c->r[0] = (uint64_t)p; c->r[2] = (uint64_t)(p >> 64);
    c->cf = c->of = (uint8_t)((uint64_t)(p >> 64) != 0);
}
TF_INLINE void x_imul1_8(Ctx *c, uint8_t s) {
    int16_t p = (int16_t)((int16_t)(int8_t)c->r[0] * (int16_t)(int8_t)s);
    TF_SET_AX(c, p);
    c->cf = c->of = (uint8_t)(p != (int16_t)(int8_t)p);
    c->sf = (uint8_t)(((uint16_t)p >> 7) & 1);
}
TF_INLINE void x_imul1_16(Ctx *c, uint16_t s) {
    int32_t p = (int32_t)(int16_t)c->r[0] * (int32_t)(int16_t)s;
    TF_SET_AX(c, p); TF_SET_DX(c, (uint32_t)p >> 16);
    c->cf = c->of = (uint8_t)(p != (int32_t)(int16_t)p);
    c->sf = (uint8_t)(((uint32_t)p >> 15) & 1);
}
TF_INLINE void x_imul1_32(Ctx *c, uint32_t s) {
    int64_t p = (int64_t)(int32_t)c->r[0] * (int64_t)(int32_t)s;
    c->r[0] = (uint32_t)p; c->r[2] = (uint32_t)((uint64_t)p >> 32);
    c->cf = c->of = (uint8_t)(p != (int64_t)(int32_t)p);
    c->sf = (uint8_t)(((uint64_t)p >> 31) & 1);
}
TF_INLINE void x_imul1_64(Ctx *c, uint64_t s) {
    __int128 p = (__int128)(int64_t)c->r[0] * (__int128)(int64_t)s;
    c->r[0] = (uint64_t)p; c->r[2] = (uint64_t)((unsigned __int128)p >> 64);
    c->cf = c->of = (uint8_t)(p != (__int128)(int64_t)p);
    c->sf = (uint8_t)(((uint64_t)p >> 63) & 1);
}

/* div/idiv: return 1 where x86 raises #DE (divisor 0 or quotient does not fit); the caller
 * traps.  All six status flags are undefined afterwards and are left alone. */
TF_INLINE int x_div8(Ctx *c, uint8_t d) {
    uint16_t n = (uint16_t)c->r[0];
    if (d == 0) return 1;
    uint16_t q = (uint16_t)(n / d), rem = (uint16_t)(n % d);
    if (q > 0xFF) return 1;
    TF_SET_AX(c, (uint16_t)((rem << 8) | q));
    return 0;
}
TF_INLINE int x_div16(Ctx *c, uint16_t d) {
    uint32_t n = ((uint32_t)(uint16_t)c->r[2] << 16) | (uint16_t)c->r[0];
    if (d == 0) return 1;
    uint32_t q = n / d, rem = n % d;
    if (q > 0xFFFF) return 1;
    TF_SET_AX(c, q); TF_SET_DX(c, rem);
    return 0;
}
TF_INLINE int x_div32(Ctx *c, uint32_t d) {
    uint64_t n = ((uint64_t)(uint32_t)c->r[2] << 32) | (uint32_t)c->r[0];
    if (d == 0) return 1;
    uint64_t q = n / d, rem = n % d;
    if (q > 0xFFFFFFFFu) return 1;
    c->r[0] = (uint32_t)q; c->r[2] = (uint32_t)rem;
    return 0;
}
TF_INLINE int x_div64(Ctx *c, uint64_t d) {
    if (d == 0 || c->r[2] >= d) return 1;
    unsigned __int128 n = ((unsigned __int128)c->r[2] << 64) | c->r[0];
    c->r[0] = (uint64_t)(n / d); c->r[2] = (uint64_t)(n % d);
    return 0;
}
TF_INLINE int x_idiv8(Ctx *c, uint8_t d8) {
    int32_t n = (int16_t)c->r[0], d = (int8_t)d8;
    if (d == 0) return 1;
    int32_t q = n / d, rem = n % d;
    if (q < -128 || q > 127) return 1;
    TF_SET_AX(c, (uint16_t)((((uint32_t)rem & 0xFF) << 8) | ((uint32_t)q & 0xFF)));
    return 0;
}
TF_INLINE int x_idiv16(Ctx *c, uint16_t d16) {
    int64_t n = (int32_t)(((uint32_t)(uint16_t)c->r[2] << 16) | (uint16_t)c->r[0]), d = (int16_t)d16;
    if (d == 0) return 1;
    int64_t q = n / d, rem = n % d;
    if (q < -32768 || q > 32767) return 1;
    TF_SET_AX(c, q); TF_SET_DX(c, rem);
    return 0;
}
TF_INLINE int x_idiv32(Ctx *c, uint32_t d32) {
    __int128 n = (int64_t)(((uint64_t)(uint32_t)c->r[2] << 32) | (uint32_t)c->r[0]), d = (int32_t)d32;
    if (d == 0) return 1;
    __int128 q = n / d, rem = n % d;
    if (q < INT32_MIN || q > INT32_MAX) return 1;
    c->r[0] = (uint32_t)(int32_t)q; c->r[2] = (uint32_t)(int32_t)rem;
    return 0;
}
TF_INLINE int x_idiv64(Ctx *c, uint64_t d64) {
    __int128 n = (__int128)(((unsigned __int128)c->r[2] << 64) | c->r[0]);
    int64_t d = (int64_t)d64;
    if (d == 0) return 1;
    if (d == -1) {                       /* avoid INT128_MIN / -1 */
        if (n > (__int128)INT64_MAX + 1 || n < -(__int128)INT64_MAX) return 1;
        c->r[0] = (uint64_t)(-n); c->r[2] = 0;
        return 0;
    }
    __int128 q = n / d, rem = n % d;
    if (q < INT64_MIN || q > INT64_MAX) return 1;
    c->r[0] = (uint64_t)(int64_t)q; c->r[2] = (uint64_t)(int64_t)rem;
    return 0;
}

#endif /* TF_INSNS_INT_ARITH_H */
