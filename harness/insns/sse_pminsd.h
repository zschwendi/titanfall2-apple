/* C helpers of the sse_pminsd family: pminsd, the minimum of signed 32-bit lanes. */
#ifndef TF_INSNS_SSE_PMINSD_H
#define TF_INSNS_SSE_PMINSD_H

#include "../runtime.h"

/* pminsd: r.i[k] = d.i[k] < s.i[k] ? d.i[k] : s.i[k], k = 0..3 */
TF_INLINE Xmm x_pminsd(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++)
        r.i[k] = d.i[k] < s.i[k] ? d.i[k] : s.i[k];
    return r;
}

#endif
