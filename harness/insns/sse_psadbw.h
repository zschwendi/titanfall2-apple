/* C helpers of the sse_psadbw family: psadbw, the sum of the absolute differences of the bytes
   of each 64-bit half. Each half's sum is at most 8 * 255 = 2040, so bits 63:16 of each half
   are 0. */
#ifndef TF_INSNS_SSE_PSADBW_H
#define TF_INSNS_SSE_PSADBW_H

#include "../runtime.h"

/* psadbw: for h = 0, 1: r.q[h] = sum over k = 0..7 of |d.b[8h + k] - s.b[8h + k]| */
TF_INLINE Xmm x_psadbw(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (uint32_t h = 0; h < 2; h++) {
        uint32_t sum = 0;
        for (uint32_t k = 0; k < 8; k++) {
            uint32_t i = 8 * h + k;
            uint32_t a = d.b[i];
            uint32_t b = s.b[i];
            sum += (a > b) ? (a - b) : (b - a);
        }
        r.q[h] = sum;
    }
    return r;
}

#endif
