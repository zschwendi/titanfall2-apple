/* C helpers of the sse_psubs family: psubsb, psubsw, psubusb and psubusw. Each lane of the
   destination minus the same lane of the source, computed in int and clamped to the lane's
   range: signed bytes to -128..127 and words to -32768..32767 (psubsb, psubsw); unsigned bytes
   to 0..255 and words to 0..65535 (psubusb, psubusw). */
#ifndef TF_INSNS_SSE_PSUBS_H
#define TF_INSNS_SSE_PSUBS_H

#include "../runtime.h"

/* psubsb: r.sb[k] = clamp(d.sb[k] - s.sb[k], -128, 127) */
TF_INLINE Xmm x_psubsb(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++) {
        int v = (int)d.sb[k] - (int)s.sb[k];
        r.sb[k] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
    }
    return r;
}

/* psubsw: r.sw[k] = clamp(d.sw[k] - s.sw[k], -32768, 32767) */
TF_INLINE Xmm x_psubsw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++) {
        int v = (int)d.sw[k] - (int)s.sw[k];
        r.sw[k] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
    return r;
}

/* psubusb: r.b[k] = max(d.b[k] - s.b[k], 0), at most 255 */
TF_INLINE Xmm x_psubusb(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 16; k++) {
        int v = (int)d.b[k] - (int)s.b[k];
        r.b[k] = (uint8_t)(v < 0 ? 0 : v);
    }
    return r;
}

/* psubusw: r.w[k] = max(d.w[k] - s.w[k], 0), at most 65535 */
TF_INLINE Xmm x_psubusw(Xmm d, Xmm s) {
    Xmm r;
    for (int k = 0; k < 8; k++) {
        int v = (int)d.w[k] - (int)s.w[k];
        r.w[k] = (uint16_t)(v < 0 ? 0 : v);
    }
    return r;
}

#endif
