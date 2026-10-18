/* C helpers of the sse_punpckl family: punpcklbw, punpcklwd, punpckldq and punpcklqdq, the
   interleaves of the low halves of two xmm registers. d is the destination before the
   instruction, s the source; the result is returned, so the caller reads both first. */
#ifndef TF_INSNS_SSE_PUNPCKL_H
#define TF_INSNS_SSE_PUNPCKL_H

#include "../runtime.h"

/* punpcklbw: r.b[2k] = d.b[k], r.b[2k+1] = s.b[k], k = 0..7 */
TF_INLINE Xmm x_punpcklbw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++) {
        r.b[2 * k] = d.b[k];
        r.b[2 * k + 1] = s.b[k];
    }
    return r;
}

/* punpcklwd: r.w[2k] = d.w[k], r.w[2k+1] = s.w[k], k = 0..3 */
TF_INLINE Xmm x_punpcklwd(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++) {
        r.w[2 * k] = d.w[k];
        r.w[2 * k + 1] = s.w[k];
    }
    return r;
}

/* punpckldq: r.d = {d.d[0], s.d[0], d.d[1], s.d[1]} */
TF_INLINE Xmm x_punpckldq(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 2; k++) {
        r.d[2 * k] = d.d[k];
        r.d[2 * k + 1] = s.d[k];
    }
    return r;
}

/* punpcklqdq: r.q = {d.q[0], s.q[0]} */
TF_INLINE Xmm x_punpcklqdq(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = d.q[0];
    r.q[1] = s.q[0];
    return r;
}

#endif
