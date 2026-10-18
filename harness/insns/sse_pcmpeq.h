/* C helpers of the sse_pcmpeq family: pcmpeqb, pcmpeqw and pcmpeqd, lane-wise equality of two
   xmm values. An equal lane becomes all ones, an unequal lane all zeros. */
#ifndef TF_INSNS_SSE_PCMPEQ_H
#define TF_INSNS_SSE_PCMPEQ_H

#include "../runtime.h"

/* pcmpeqb: r.b[k] = (d.b[k] == s.b[k]) ? 0xff : 0 */
TF_INLINE Xmm x_pcmpeqb(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++)
        r.b[k] = d.b[k] == s.b[k] ? 0xFF : 0;
    return r;
}

/* pcmpeqw: r.w[k] = (d.w[k] == s.w[k]) ? 0xffff : 0 */
TF_INLINE Xmm x_pcmpeqw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++)
        r.w[k] = d.w[k] == s.w[k] ? 0xFFFF : 0;
    return r;
}

/* pcmpeqd: r.d[k] = (d.d[k] == s.d[k]) ? 0xffffffff : 0 */
TF_INLINE Xmm x_pcmpeqd(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 4; k++)
        r.d[k] = d.d[k] == s.d[k] ? 0xFFFFFFFFu : 0;
    return r;
}

#endif
