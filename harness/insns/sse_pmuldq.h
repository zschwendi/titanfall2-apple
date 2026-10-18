/* C helpers of the sse_pmuldq family: pmuludq and pmaddubsw.
   pmuludq multiplies the even unsigned dwords of two xmm values into two qwords; pmaddubsw
   multiplies unsigned bytes of the destination by signed bytes of the source, adds adjacent
   pairs and saturates each sum to a signed word. */
#ifndef TF_INSNS_SSE_PMULDQ_H
#define TF_INSNS_SSE_PMULDQ_H

#include "../runtime.h"

/* pmuludq: r.q[k] = (uint64_t)d.d[2k] * (uint64_t)s.d[2k] for k = 0, 1 */
TF_INLINE Xmm x_pmuludq(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = (uint64_t)d.d[0] * (uint64_t)s.d[0];
    r.q[1] = (uint64_t)d.d[2] * (uint64_t)s.d[2];
    return r;
}

/* pmaddubsw: r.sw[k] = saturate16(d.b[2k] * s.sb[2k] + d.b[2k+1] * s.sb[2k+1]), k = 0..7.
   The products and their sum are computed in int: each product is at most 255 * 128 in
   magnitude, so the sum fits without overflow. */
TF_INLINE Xmm x_pmaddubsw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++) {
        int t = (int)d.b[2 * k] * (int)s.sb[2 * k]
              + (int)d.b[2 * k + 1] * (int)s.sb[2 * k + 1];
        r.sw[k] = (int16_t)(t > 32767 ? 32767 : t < -32768 ? -32768 : t);
    }
    return r;
}

#endif
