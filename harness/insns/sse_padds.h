/* C helpers of the sse_padds family: paddsb, paddsw, paddusb and paddusw, the saturating
   packed adds. Each lane is computed in int and clamped to the lane's range. */
#ifndef TF_INSNS_SSE_PADDS_H
#define TF_INSNS_SSE_PADDS_H

#include "../runtime.h"

/* paddsb: r.sb[k] = clamp(d.sb[k] + s.sb[k], -128, 127) */
TF_INLINE Xmm x_paddsb(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++) {
        int v = d.sb[k] + s.sb[k];
        r.b[k] = (uint8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
    }
    return r;
}

/* paddsw: r.sw[k] = clamp(d.sw[k] + s.sw[k], -32768, 32767) */
TF_INLINE Xmm x_paddsw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++) {
        int v = d.sw[k] + s.sw[k];
        r.w[k] = (uint16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return r;
}

/* paddusb: r.b[k] = min(d.b[k] + s.b[k], 255) */
TF_INLINE Xmm x_paddusb(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++) {
        int v = d.b[k] + s.b[k];
        r.b[k] = (uint8_t)(v > 255 ? 255 : v);
    }
    return r;
}

/* paddusw: r.w[k] = min(d.w[k] + s.w[k], 65535) */
TF_INLINE Xmm x_paddusw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++) {
        int v = d.w[k] + s.w[k];
        r.w[k] = (uint16_t)(v > 65535 ? 65535 : v);
    }
    return r;
}

#endif
