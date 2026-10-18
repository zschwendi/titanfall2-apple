/* C helpers of the sse_psub family: psubb, psubw, psubd, psubq, the wrapping subtraction of
   each lane: r.<lane>[k] = d.<lane>[k] - s.<lane>[k], modulo the lane size. */
#ifndef TF_INSNS_SSE_PSUB_H
#define TF_INSNS_SSE_PSUB_H

#include "../runtime.h"

/* psubb: 16 byte lanes */
TF_INLINE Xmm x_psubb(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 16; k++)
        r.b[k] = (uint8_t)(d.b[k] - s.b[k]);
    return r;
}

/* psubw: 8 word lanes */
TF_INLINE Xmm x_psubw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)(d.w[k] - s.w[k]);
    return r;
}

/* psubd: 4 doubleword lanes */
TF_INLINE Xmm x_psubd(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++)
        r.d[k] = d.d[k] - s.d[k];
    return r;
}

/* psubq: 2 quadword lanes */
TF_INLINE Xmm x_psubq(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 2; k++)
        r.q[k] = d.q[k] - s.q[k];
    return r;
}

#endif
