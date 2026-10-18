/* C helpers of the sse_unpck family: unpcklps, unpckhps, unpcklpd, unpckhpd, the interleaves of
   the low or high halves of two xmm registers. Bits are moved, not computed: integer lanes. */
#ifndef TF_INSNS_SSE_UNPCK_H
#define TF_INSNS_SSE_UNPCK_H

#include "../runtime.h"

/* unpcklps: r.d = {d.d[0], s.d[0], d.d[1], s.d[1]} */
TF_INLINE Xmm x_unpcklps(Xmm d, Xmm s) {
    Xmm r;
    r.d[0] = d.d[0];
    r.d[1] = s.d[0];
    r.d[2] = d.d[1];
    r.d[3] = s.d[1];
    return r;
}

/* unpckhps: r.d = {d.d[2], s.d[2], d.d[3], s.d[3]} */
TF_INLINE Xmm x_unpckhps(Xmm d, Xmm s) {
    Xmm r;
    r.d[0] = d.d[2];
    r.d[1] = s.d[2];
    r.d[2] = d.d[3];
    r.d[3] = s.d[3];
    return r;
}

/* unpcklpd: r.q = {d.q[0], s.q[0]} */
TF_INLINE Xmm x_unpcklpd(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = d.q[0];
    r.q[1] = s.q[0];
    return r;
}

/* unpckhpd: r.q = {d.q[1], s.q[1]} */
TF_INLINE Xmm x_unpckhpd(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = d.q[1];
    r.q[1] = s.q[1];
    return r;
}

#endif
