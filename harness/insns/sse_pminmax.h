/* C helpers of the sse_pminmax family: pmaxsw, pminsw (signed 16-bit lanes) and pmaxub, pminub
   (unsigned 8-bit lanes). Lane k of the result is the maximum or minimum of lane k of the
   destination d and of the source s. */
#ifndef TF_INSNS_SSE_PMINMAX_H
#define TF_INSNS_SSE_PMINMAX_H

#include "../runtime.h"

/* pmaxsw: r.sw[k] = max(d.sw[k], s.sw[k]), signed */
TF_INLINE Xmm x_pmaxsw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++)
        r.sw[k] = d.sw[k] > s.sw[k] ? d.sw[k] : s.sw[k];
    return r;
}

/* pminsw: r.sw[k] = min(d.sw[k], s.sw[k]), signed */
TF_INLINE Xmm x_pminsw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++)
        r.sw[k] = d.sw[k] < s.sw[k] ? d.sw[k] : s.sw[k];
    return r;
}

/* pmaxub: r.b[k] = max(d.b[k], s.b[k]), unsigned */
TF_INLINE Xmm x_pmaxub(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++)
        r.b[k] = d.b[k] > s.b[k] ? d.b[k] : s.b[k];
    return r;
}

/* pminub: r.b[k] = min(d.b[k], s.b[k]), unsigned */
TF_INLINE Xmm x_pminub(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++)
        r.b[k] = d.b[k] < s.b[k] ? d.b[k] : s.b[k];
    return r;
}

#endif
