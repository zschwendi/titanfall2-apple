/* C helpers of the shift_bit family (lifter/insns/shift_bit.py): shl, shr, sar, rol, ror and the
 * double shifts shld and shrd.  The bit tests and scans need no helper.
 *
 * Flags follow the Intel SDM vol. 2 "Flags Affected" sections; x_szp is in runtime.h. */
#ifndef TF_INSNS_SHIFT_BIT_H
#define TF_INSNS_SHIFT_BIT_H

#include "../runtime.h"

/* N = bits, T = unsigned type, S = signed type, M = count mask (SDM: 5 bits, 6 for 64-bit). */

#define TF_SHIFT(N, T, S, M)                                                                   \
/* Shifts: count masked to M; a masked count of 0 changes no flag.  OF is defined for a        \
 * count of 1 only; CF is undefined for shl/shr when count >= N (8/16-bit only).  AF           \
 * undefined (written 0). */                                                                   \
TF_INLINE T x_shl##N(Ctx *c, T a, uint8_t cnt) {                                               \
    unsigned n = cnt & M;                                                                      \
    if (n == 0) return a;                                                                      \
    T r; uint8_t cf;                                                                           \
    if (n < N) { r = (T)(a << n); cf = (uint8_t)((a >> (N - n)) & 1); }                        \
    else       { r = 0;           cf = (uint8_t)(n == N ? (a & 1) : 0); }                      \
    c->cf = cf;                                                                                \
    c->of = (uint8_t)(((r >> (N - 1)) & 1) ^ cf);                                              \
    c->af = 0;                                                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
TF_INLINE T x_shr##N(Ctx *c, T a, uint8_t cnt) {                                               \
    unsigned n = cnt & M;                                                                      \
    if (n == 0) return a;                                                                      \
    T r; uint8_t cf;                                                                           \
    if (n < N) { r = (T)(a >> n); cf = (uint8_t)((a >> (n - 1)) & 1); }                        \
    else       { r = 0;           cf = (uint8_t)(n == N ? ((a >> (N - 1)) & 1) : 0); }         \
    c->cf = cf;                                                                                \
    c->of = (uint8_t)((a >> (N - 1)) & 1);                                                     \
    c->af = 0;                                                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
TF_INLINE T x_sar##N(Ctx *c, T a, uint8_t cnt) {                                               \
    unsigned n = cnt & M;                                                                      \
    if (n == 0) return a;                                                                      \
    S s = (S)a;                                                                                \
    T r; uint8_t cf;                                                                           \
    if (n < N) { r = (T)(s >> n); cf = (uint8_t)((s >> (n - 1)) & 1); }                        \
    else       { r = (T)(s >> (N - 1)); cf = (uint8_t)((a >> (N - 1)) & 1); } /* all sign */   \
    c->cf = cf;                                                                                \
    c->of = 0;                                                                                 \
    c->af = 0;                                                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
/* Rotates: only CF and OF change, and only when the masked count is non-zero.  The            \
 * rotation amount is the masked count modulo N.  OF defined for a count of 1 only. */         \
TF_INLINE T x_rol##N(Ctx *c, T a, uint8_t cnt) {                                               \
    unsigned n = cnt & M;                                                                      \
    if (n == 0) return a;                                                                      \
    unsigned m = n % N;                                                                        \
    T r = m ? (T)((T)(a << m) | (T)(a >> (N - m))) : a;                                        \
    c->cf = (uint8_t)(r & 1);                                                                  \
    c->of = (uint8_t)(((r >> (N - 1)) ^ r) & 1);                                               \
    return r;                                                                                  \
}                                                                                              \
TF_INLINE T x_ror##N(Ctx *c, T a, uint8_t cnt) {                                               \
    unsigned n = cnt & M;                                                                      \
    if (n == 0) return a;                                                                      \
    unsigned m = n % N;                                                                        \
    T r = m ? (T)((T)(a >> m) | (T)(a << (N - m))) : a;                                        \
    c->cf = (uint8_t)((r >> (N - 1)) & 1);                                                     \
    c->of = (uint8_t)(((r >> (N - 1)) ^ (r >> (N - 2))) & 1);                                  \
    return r;                                                                                  \
}

TF_SHIFT(8,  uint8_t,  int8_t,  31)
TF_SHIFT(16, uint16_t, int16_t, 31)
TF_SHIFT(32, uint32_t, int32_t, 31)
TF_SHIFT(64, uint64_t, int64_t, 63)

/* Double shifts.  Count masked as above; count 0 changes nothing; for the 16-bit form a count
 * above 16 gives an undefined result and flags (SDM) - computed here without C undefined
 * behaviour.  CF = last bit shifted out, OF (count 1) = sign change, AF undefined. */
TF_INLINE uint16_t x_shld16(Ctx *c, uint16_t a, uint16_t b, uint8_t cnt) {
    unsigned n = cnt & 31;
    if (n == 0) return a;
    uint32_t t = ((uint32_t)a << 16) | b;
    uint16_t r = (uint16_t)((uint32_t)(t << n) >> 16);
    c->cf = (uint8_t)(n <= 16 ? (a >> (16 - n)) & 1 : (b >> (32 - n)) & 1);
    c->of = (uint8_t)(((r ^ a) >> 15) & 1);
    c->af = 0;
    x_szp16(c, r);
    return r;
}
TF_INLINE uint32_t x_shld32(Ctx *c, uint32_t a, uint32_t b, uint8_t cnt) {
    unsigned n = cnt & 31;
    if (n == 0) return a;
    uint32_t r = (uint32_t)((a << n) | (b >> (32 - n)));
    c->cf = (uint8_t)((a >> (32 - n)) & 1);
    c->of = (uint8_t)(((r ^ a) >> 31) & 1);
    c->af = 0;
    x_szp32(c, r);
    return r;
}
TF_INLINE uint64_t x_shld64(Ctx *c, uint64_t a, uint64_t b, uint8_t cnt) {
    unsigned n = cnt & 63;
    if (n == 0) return a;
    uint64_t r = (a << n) | (b >> (64 - n));
    c->cf = (uint8_t)((a >> (64 - n)) & 1);
    c->of = (uint8_t)(((r ^ a) >> 63) & 1);
    c->af = 0;
    x_szp64(c, r);
    return r;
}
TF_INLINE uint16_t x_shrd16(Ctx *c, uint16_t a, uint16_t b, uint8_t cnt) {
    unsigned n = cnt & 31;
    if (n == 0) return a;
    uint32_t t = ((uint32_t)b << 16) | a;
    uint16_t r = (uint16_t)(t >> n);
    c->cf = (uint8_t)((t >> (n - 1)) & 1);
    c->of = (uint8_t)(((r ^ a) >> 15) & 1);
    c->af = 0;
    x_szp16(c, r);
    return r;
}
TF_INLINE uint32_t x_shrd32(Ctx *c, uint32_t a, uint32_t b, uint8_t cnt) {
    unsigned n = cnt & 31;
    if (n == 0) return a;
    uint32_t r = (uint32_t)((a >> n) | (b << (32 - n)));
    c->cf = (uint8_t)((a >> (n - 1)) & 1);
    c->of = (uint8_t)(((r ^ a) >> 31) & 1);
    c->af = 0;
    x_szp32(c, r);
    return r;
}
TF_INLINE uint64_t x_shrd64(Ctx *c, uint64_t a, uint64_t b, uint8_t cnt) {
    unsigned n = cnt & 63;
    if (n == 0) return a;
    uint64_t r = (a >> n) | (b << (64 - n));
    c->cf = (uint8_t)((a >> (n - 1)) & 1);
    c->of = (uint8_t)(((r ^ a) >> 63) & 1);
    c->af = 0;
    x_szp64(c, r);
    return r;
}

#endif /* TF_INSNS_SHIFT_BIT_H */
