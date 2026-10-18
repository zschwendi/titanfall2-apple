/* C helpers of the sse_padd family: paddb, paddw, paddd and paddq, wrapping lane-wise additions
   of two xmm values. Each lane is added in its own unsigned type and the sum wraps. */
#ifndef TF_INSNS_SSE_PADD_H
#define TF_INSNS_SSE_PADD_H

#include "../runtime.h"

/* paddb: r.b[k] = (d.b[k] + s.b[k]) mod 2^8 */
TF_INLINE Xmm x_paddb(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 16; k++)
        r.b[k] = (uint8_t)(d.b[k] + s.b[k]);
    return r;
}

/* paddw: r.w[k] = (d.w[k] + s.w[k]) mod 2^16 */
TF_INLINE Xmm x_paddw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)(d.w[k] + s.w[k]);
    return r;
}

/* paddd: r.d[k] = (d.d[k] + s.d[k]) mod 2^32 */
TF_INLINE Xmm x_paddd(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++)
        r.d[k] = (uint32_t)(d.d[k] + s.d[k]);
    return r;
}

/* paddq: r.q[k] = (d.q[k] + s.q[k]) mod 2^64 */
TF_INLINE Xmm x_paddq(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 2; k++)
        r.q[k] = (uint64_t)(d.q[k] + s.q[k]);
    return r;
}

#endif
