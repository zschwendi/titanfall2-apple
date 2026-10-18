/* C helpers of the sse_pmul family: pmullw, pmulhw and pmulhuw (products of 16-bit lanes, low
   or high 16 bits) and pmulld (products of 32-bit lanes, low 32 bits). Each takes the
   destination's old value d and the source s, and returns the new destination. */
#ifndef TF_INSNS_SSE_PMUL_H
#define TF_INSNS_SSE_PMUL_H

#include "../runtime.h"

/* pmullw: r.w[k] = low 16 bits of d.w[k] * s.w[k] */
TF_INLINE Xmm x_pmullw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)((uint32_t)d.w[k] * s.w[k]);
    return r;
}

/* pmulhw: r.w[k] = high 16 bits of the signed product d.sw[k] * s.sw[k] (an arithmetic shift;
   the product fits in 32 bits) */
TF_INLINE Xmm x_pmulhw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)(((int32_t)d.sw[k] * (int32_t)s.sw[k]) >> 16);
    return r;
}

/* pmulhuw: r.w[k] = high 16 bits of the unsigned product d.w[k] * s.w[k] */
TF_INLINE Xmm x_pmulhuw(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 8; k++)
        r.w[k] = (uint16_t)(((uint32_t)d.w[k] * (uint32_t)s.w[k]) >> 16);
    return r;
}

/* pmulld: r.d[k] = low 32 bits of d.d[k] * s.d[k] */
TF_INLINE Xmm x_pmulld(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 4; k++)
        r.d[k] = (uint32_t)((uint64_t)d.d[k] * s.d[k]);
    return r;
}

#endif
