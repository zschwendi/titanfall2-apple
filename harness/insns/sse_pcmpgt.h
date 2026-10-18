/* C helpers of the sse_pcmpgt family: pcmpgtb, pcmpgtw, pcmpgtd. Each lane of the result is all
   ones where d > s as signed numbers, all zeros otherwise. */
#ifndef TF_INSNS_SSE_PCMPGT_H
#define TF_INSNS_SSE_PCMPGT_H

#include "../runtime.h"

/* pcmpgtb: r.b[k] = (d.sb[k] > s.sb[k]) ? 0xFF : 0 */
TF_INLINE Xmm x_pcmpgtb(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++)
        r.b[k] = (d.sb[k] > s.sb[k]) ? 0xFF : 0;
    return r;
}

/* pcmpgtw: r.w[k] = (d.sw[k] > s.sw[k]) ? 0xFFFF : 0 */
TF_INLINE Xmm x_pcmpgtw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++)
        r.w[k] = (d.sw[k] > s.sw[k]) ? 0xFFFF : 0;
    return r;
}

/* pcmpgtd: r.d[k] = (d.i[k] > s.i[k]) ? 0xFFFFFFFF : 0 */
TF_INLINE Xmm x_pcmpgtd(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 4; k++)
        r.d[k] = (d.i[k] > s.i[k]) ? 0xFFFFFFFFu : 0;
    return r;
}

#endif
