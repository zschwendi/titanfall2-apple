/* C helpers of the sse_pack family: packuswb, packsswb and packssdw. Each narrows the lanes of
   the destination d and of the source s with saturation; the low half of the result is made
   from d, the high half from s. */
#ifndef TF_INSNS_SSE_PACK_H
#define TF_INSNS_SSE_PACK_H

#include "../runtime.h"

/* a signed 16-bit value clamped to 0..255 */
TF_INLINE uint8_t x_sse_pack_u8(int16_t v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* a signed 16-bit value clamped to -128..127 */
TF_INLINE int8_t x_sse_pack_s8(int16_t v) {
    return (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
}

/* a signed 32-bit value clamped to -32768..32767 */
TF_INLINE int16_t x_sse_pack_s16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* packuswb: r.b[k] = u8(d.sw[k]), r.b[8 + k] = u8(s.sw[k]) */
TF_INLINE Xmm x_packuswb(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (int k = 0; k < 8; k++) {
        r.b[k] = x_sse_pack_u8(d.sw[k]);
        r.b[8 + k] = x_sse_pack_u8(s.sw[k]);
    }
    return r;
}

/* packsswb: r.sb[k] = s8(d.sw[k]), r.sb[8 + k] = s8(s.sw[k]) */
TF_INLINE Xmm x_packsswb(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (int k = 0; k < 8; k++) {
        r.sb[k] = x_sse_pack_s8(d.sw[k]);
        r.sb[8 + k] = x_sse_pack_s8(s.sw[k]);
    }
    return r;
}

/* packssdw: r.sw[k] = s16(d.i[k]), r.sw[4 + k] = s16(s.i[k]) */
TF_INLINE Xmm x_packssdw(Xmm d, Xmm s) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (int k = 0; k < 4; k++) {
        r.sw[k] = x_sse_pack_s16(d.i[k]);
        r.sw[4 + k] = x_sse_pack_s16(s.i[k]);
    }
    return r;
}

#endif
