/* C helpers of the sse_pavg family: pavgb and pavgw, the rounded average of unsigned lanes,
   r = (d + s + 1) >> 1 computed without overflow. */
#ifndef TF_INSNS_SSE_PAVG_H
#define TF_INSNS_SSE_PAVG_H

#include "../runtime.h"

/* pavgb: r.b[k] = (uint8_t)(((unsigned)d.b[k] + s.b[k] + 1) >> 1) */
TF_INLINE Xmm x_pavgb(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 16; k++)
        r.b[k] = (uint8_t)(((uint32_t)d.b[k] + s.b[k] + 1) >> 1);
    return r;
}

/* pavgw: r.w[k] = (uint16_t)(((uint32_t)d.w[k] + s.w[k] + 1) >> 1) */
TF_INLINE Xmm x_pavgw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)(((uint32_t)d.w[k] + s.w[k] + 1) >> 1);
    return r;
}

#endif
