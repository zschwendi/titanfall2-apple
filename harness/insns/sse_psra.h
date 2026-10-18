/* C helpers of the sse_psra family: psraw and psrad, arithmetic right shifts of the signed 16-bit
   and 32-bit lanes of an xmm register. n is the whole 64-bit count; a count above 15 (words) or
   31 (doublewords) is taken as 15 or 31, which leaves every lane equal to its sign bit. */
#ifndef TF_INSNS_SSE_PSRA_H
#define TF_INSNS_SSE_PSRA_H

#include "../runtime.h"

/* psraw: r.sw[k] = s.sw[k] >> min(n, 15) */
TF_INLINE Xmm x_psraw(Xmm s, uint64_t n) {
    Xmm r;
    int c = n > 15 ? 15 : (int)n;
    for (int k = 0; k < 8; k++)
        r.sw[k] = (int16_t)(s.sw[k] >> c);
    return r;
}

/* psrad: r.i[k] = s.i[k] >> min(n, 31) */
TF_INLINE Xmm x_psrad(Xmm s, uint64_t n) {
    Xmm r;
    int c = n > 31 ? 31 : (int)n;
    for (int k = 0; k < 4; k++)
        r.i[k] = s.i[k] >> c;
    return r;
}

#endif
