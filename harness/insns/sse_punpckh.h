/* C helpers of the sse_punpckh family: punpckhbw, punpckhwd, punpckhdq and punpckhqdq, the
   interleaving of the high halves of two xmm registers. d is the destination (first operand),
   s the source; the result takes lanes alternately from d and s, low lane first. */
#ifndef TF_INSNS_SSE_PUNPCKH_H
#define TF_INSNS_SSE_PUNPCKH_H

#include "../runtime.h"

/* punpckhbw: r.b[2k] = d.b[8+k], r.b[2k+1] = s.b[8+k], k = 0..7 */
TF_INLINE Xmm x_punpckhbw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++) {
        r.b[2 * k] = d.b[8 + k];
        r.b[2 * k + 1] = s.b[8 + k];
    }
    return r;
}

/* punpckhwd: r.w[2k] = d.w[4+k], r.w[2k+1] = s.w[4+k], k = 0..3 */
TF_INLINE Xmm x_punpckhwd(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++) {
        r.w[2 * k] = d.w[4 + k];
        r.w[2 * k + 1] = s.w[4 + k];
    }
    return r;
}

/* punpckhdq: r.d = {d.d[2], s.d[2], d.d[3], s.d[3]} */
TF_INLINE Xmm x_punpckhdq(Xmm d, Xmm s) {
    Xmm r;
    r.d[0] = d.d[2];
    r.d[1] = s.d[2];
    r.d[2] = d.d[3];
    r.d[3] = s.d[3];
    return r;
}

/* punpckhqdq: r.q = {d.q[1], s.q[1]} */
TF_INLINE Xmm x_punpckhqdq(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = d.q[1];
    r.q[1] = s.q[1];
    return r;
}

#endif
