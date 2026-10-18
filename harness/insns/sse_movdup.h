/* C helpers of the sse_movdup family: movddup, movshdup and movsldup, the SSE3 moves that copy
   lanes of the source into pairs of the destination. Each returns the whole destination. */
#ifndef TF_INSNS_SSE_MOVDUP_H
#define TF_INSNS_SSE_MOVDUP_H

#include "../runtime.h"

/* movddup: r.q[0] = r.q[1] = lo, where lo is the low qword of the source (xmm or m64). */
TF_INLINE Xmm x_movddup(uint64_t lo) {
    Xmm r;
    r.q[0] = lo;
    r.q[1] = lo;
    return r;
}

/* movshdup: r.d[0] = r.d[1] = s.d[1]; r.d[2] = r.d[3] = s.d[3]. */
TF_INLINE Xmm x_movshdup(Xmm s) {
    Xmm r;
    r.d[0] = s.d[1];
    r.d[1] = s.d[1];
    r.d[2] = s.d[3];
    r.d[3] = s.d[3];
    return r;
}

/* movsldup: r.d[0] = r.d[1] = s.d[0]; r.d[2] = r.d[3] = s.d[2]. */
TF_INLINE Xmm x_movsldup(Xmm s) {
    Xmm r;
    r.d[0] = s.d[0];
    r.d[1] = s.d[0];
    r.d[2] = s.d[2];
    r.d[3] = s.d[2];
    return r;
}

#endif
