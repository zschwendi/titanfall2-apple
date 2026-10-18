/* C helpers of the sse_pshufd family: pshufd, pshuflw and pshufhw, shuffles of the lanes of an
   xmm register by an imm8 control byte. Lane selector k is bits 2k+1:2k of imm, imm & 0xFF. */
#ifndef TF_INSNS_SSE_PSHUFD_H
#define TF_INSNS_SSE_PSHUFD_H

#include "../runtime.h"

/* pshufd: r.d[k] = s.d[(imm >> 2k) & 3], k = 0..3 */
TF_INLINE Xmm x_pshufd(Xmm s, uint32_t imm) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++)
        r.d[k] = s.d[(imm >> (2 * k)) & 3];
    return r;
}

/* pshuflw: r.w[k] = s.w[(imm >> 2k) & 3], k = 0..3; r.q[1] = s.q[1] */
TF_INLINE Xmm x_pshuflw(Xmm s, uint32_t imm) {
    Xmm r;
    r.q[1] = s.q[1];
    for (uint32_t k = 0; k < 4; k++)
        r.w[k] = s.w[(imm >> (2 * k)) & 3];
    return r;
}

/* pshufhw: r.q[0] = s.q[0]; r.w[4 + k] = s.w[4 + ((imm >> 2k) & 3)], k = 0..3 */
TF_INLINE Xmm x_pshufhw(Xmm s, uint32_t imm) {
    Xmm r;
    r.q[0] = s.q[0];
    for (uint32_t k = 0; k < 4; k++)
        r.w[4 + k] = s.w[4 + ((imm >> (2 * k)) & 3)];
    return r;
}

#endif
