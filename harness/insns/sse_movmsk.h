/* C helpers of the sse_movmsk family: movmskps, movmskpd and pmovmskb. Each gathers the sign
   bit of every lane of an xmm register into the low bits of a 32-bit result; bits above the
   lane count are zero. */
#ifndef TF_INSNS_SSE_MOVMSK_H
#define TF_INSNS_SSE_MOVMSK_H

#include "../runtime.h"

/* movmskps: bit k = bit 31 of s.d[k], k = 0..3 */
TF_INLINE uint32_t x_movmskps(Xmm s) {
    uint32_t r = 0;
    for (uint32_t k = 0; k < 4; k++)
        r |= (s.d[k] >> 31) << k;
    return r;
}

/* movmskpd: bit k = bit 63 of s.q[k], k = 0..1 */
TF_INLINE uint32_t x_movmskpd(Xmm s) {
    uint32_t r = 0;
    for (uint32_t k = 0; k < 2; k++)
        r |= (uint32_t)(s.q[k] >> 63) << k;
    return r;
}

/* pmovmskb: bit k = bit 7 of s.b[k], k = 0..15 */
TF_INLINE uint32_t x_pmovmskb(Xmm s) {
    uint32_t r = 0;
    for (uint32_t k = 0; k < 16; k++)
        r |= (uint32_t)(s.b[k] >> 7) << k;
    return r;
}

#endif
