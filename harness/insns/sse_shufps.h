/* C helpers of the sse_shufps family: shufps and shufpd. Each takes two lanes from the destination
   and two from the source, chosen by the immediate byte n (passed as a number, n = imm8 & 0xFF). */
#ifndef TF_INSNS_SSE_SHUFPS_H
#define TF_INSNS_SSE_SHUFPS_H

#include "../runtime.h"

/* shufps: r.d[0] = dst.d[n & 3]; r.d[1] = dst.d[(n >> 2) & 3];
           r.d[2] = src.d[(n >> 4) & 3]; r.d[3] = src.d[(n >> 6) & 3] */
TF_INLINE Xmm x_shufps(Xmm dst, Xmm src, uint32_t n) {
    Xmm r;
    r.d[0] = dst.d[n & 3];
    r.d[1] = dst.d[(n >> 2) & 3];
    r.d[2] = src.d[(n >> 4) & 3];
    r.d[3] = src.d[(n >> 6) & 3];
    return r;
}

/* shufpd: r.q[0] = dst.q[n & 1]; r.q[1] = src.q[(n >> 1) & 1]; the other bits of n are ignored */
TF_INLINE Xmm x_shufpd(Xmm dst, Xmm src, uint32_t n) {
    Xmm r;
    r.q[0] = dst.q[n & 1];
    r.q[1] = src.q[(n >> 1) & 1];
    return r;
}

#endif
