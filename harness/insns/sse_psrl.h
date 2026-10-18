/* C helpers of the sse_psrl family: psrlw, psrld, psrlq, packed right shifts of every lane by a
   count n with zeros coming in. n is an unsigned 64-bit count (imm8 & 0xFF, or the low qword of
   the source); a count at or above the lane width (16, 32, 64 bits) gives all zeros. */
#ifndef TF_INSNS_SSE_PSRL_H
#define TF_INSNS_SSE_PSRL_H

#include "../runtime.h"

/* psrlw: r.w[k] = (n < 16) ? d.w[k] >> n : 0 */
TF_INLINE Xmm x_psrlw(Xmm d, uint64_t n) {
    Xmm r;
    for (int k = 0; k < 8; k++)
        r.w[k] = (n < 16) ? (uint16_t)(d.w[k] >> n) : 0;
    return r;
}

/* psrld: r.d[k] = (n < 32) ? d.d[k] >> n : 0 */
TF_INLINE Xmm x_psrld(Xmm d, uint64_t n) {
    Xmm r;
    for (int k = 0; k < 4; k++)
        r.d[k] = (n < 32) ? (uint32_t)(d.d[k] >> n) : 0;
    return r;
}

/* psrlq: r.q[k] = (n < 64) ? d.q[k] >> n : 0 */
TF_INLINE Xmm x_psrlq(Xmm d, uint64_t n) {
    Xmm r;
    for (int k = 0; k < 2; k++)
        r.q[k] = (n < 64) ? d.q[k] >> n : 0;
    return r;
}

#endif
