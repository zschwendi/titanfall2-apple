/* C helpers of the sse_psll family: psllw, pslld and psllq, each shifts every lane of an xmm
   register left by the same count n. n is a uint64_t: a count above the lane width (15, 31 or
   63) gives all zeros, so no lane is shifted by its width or more. */
#ifndef TF_INSNS_SSE_PSLL_H
#define TF_INSNS_SSE_PSLL_H

#include "../runtime.h"

/* psllw: r.w[k] = s.w[k] << n */
TF_INLINE Xmm x_psllw(Xmm s, uint64_t n) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    if (n > 15)
        return r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)(s.w[k] << n);
    return r;
}

/* pslld: r.d[k] = s.d[k] << n */
TF_INLINE Xmm x_pslld(Xmm s, uint64_t n) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    if (n > 31)
        return r;
    for (uint32_t k = 0; k < 4; k++)
        r.d[k] = s.d[k] << n;
    return r;
}

/* psllq: r.q[k] = s.q[k] << n */
TF_INLINE Xmm x_psllq(Xmm s, uint64_t n) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    if (n > 63)
        return r;
    for (uint32_t k = 0; k < 2; k++)
        r.q[k] = s.q[k] << n;
    return r;
}

#endif
